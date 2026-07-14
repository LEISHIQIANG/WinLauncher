#include "../../WinLauncher/App/BackgroundTaskService.h"
#include "../../WinLauncher/App/CrashReporter.h"
#include "../../WinLauncher/App/EventBus.h"
#include "../../WinLauncher/App/Logger.h"
#include "../../WinLauncher/App/InputHookThreadStop.h"
#include "../../WinLauncher/Services/ArchiveUtility.h"
#include "../../WinLauncher/Services/MigrationBackupService.h"
#include <Windows.h>
#include <shellapi.h>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;

static int Fail(const wchar_t* message)
{
    fwprintf(stderr, L"[FAIL] %s\n", message);
    return 1;
}

static std::wstring MakeTempDirectory()
{
    wchar_t temp[MAX_PATH]{};
    GetTempPathW(MAX_PATH, temp);
    std::wstring path = std::wstring(temp) + L"WinLauncherNativeTests_" + std::to_wstring(GetCurrentProcessId());
    fs::create_directories(path);
    return path;
}

static bool HasNonEmptyCrashArtifacts(const std::wstring& directory)
{
    bool dump = false;
    bool text = false;
    for (const auto& entry : fs::directory_iterator(directory))
    {
        if (!entry.is_regular_file() || entry.file_size() == 0) continue;
        if (entry.path().extension() == L".dmp") dump = true;
        if (entry.path().extension() == L".txt") text = true;
    }
    return dump && text;
}

static void AppendU16(std::vector<unsigned char>& bytes, uint16_t value)
{
    bytes.push_back(static_cast<unsigned char>(value & 0xff));
    bytes.push_back(static_cast<unsigned char>((value >> 8) & 0xff));
}

static void AppendU32(std::vector<unsigned char>& bytes, uint32_t value)
{
    AppendU16(bytes, static_cast<uint16_t>(value & 0xffff));
    AppendU16(bytes, static_cast<uint16_t>((value >> 16) & 0xffff));
}

static void WriteCentralDirectoryOnlyZip(const std::wstring& path, const std::string& name)
{
    std::vector<unsigned char> bytes;
    AppendU32(bytes, 0x02014b50);
    AppendU16(bytes, 20); AppendU16(bytes, 20); AppendU16(bytes, 0); AppendU16(bytes, 0);
    AppendU16(bytes, 0); AppendU16(bytes, 0); AppendU32(bytes, 0); AppendU32(bytes, 0); AppendU32(bytes, 0);
    AppendU16(bytes, static_cast<uint16_t>(name.size())); AppendU16(bytes, 0); AppendU16(bytes, 0);
    AppendU16(bytes, 0); AppendU16(bytes, 0); AppendU32(bytes, 0); AppendU32(bytes, 0);
    bytes.insert(bytes.end(), name.begin(), name.end());
    const uint32_t centralSize = static_cast<uint32_t>(bytes.size());
    AppendU32(bytes, 0x06054b50);
    AppendU16(bytes, 0); AppendU16(bytes, 0); AppendU16(bytes, 1); AppendU16(bytes, 1);
    AppendU32(bytes, centralSize); AppendU32(bytes, 0); AppendU16(bytes, 0);
    FILE* file = _wfopen(path.c_str(), L"wb");
    fwrite(bytes.data(), 1, bytes.size(), file);
    fclose(file);
}

static DWORD WINAPI CooperativeHookLikeThread(LPVOID)
{
    MSG message{};
    PeekMessageW(&message, nullptr, 0, 0, PM_NOREMOVE);
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {}
    return 37;
}

static DWORD WINAPI BlockedHookLikeThread(LPVOID)
{
    Sleep(10000);
    return 0;
}

int wmain(int argc, wchar_t** argv)
{
    if (argc >= 3 && wcscmp(argv[1], L"--crash-child") == 0)
    {
        CrashReporter reporter(argv[2]);
        CrashReporter::RecordBreadcrumb(L"test", L"intentional crash child");
        volatile int* invalid = nullptr;
        *invalid = 7;
        return 99;
    }

    std::wstring temp = MakeTempDirectory();
    auto logger = std::make_shared<Logger>(temp + L"\\native-tests.log");

    {
        HANDLE thread = CreateThread(nullptr, 0, CooperativeHookLikeThread, nullptr, 0, nullptr);
        if (!thread) return Fail(L"unable to start cooperative hook-like thread");
        const auto result = InputHookThreadStop::RequestStop(thread, GetThreadId(thread), 1000);
        if (!result.quitPosted || result.waitResult != WAIT_OBJECT_0 || result.timedOut || result.exitCode != 37)
            return Fail(L"cooperative input-hook shutdown did not complete cleanly");
        CloseHandle(thread);
    }

    {
        HANDLE thread = CreateThread(nullptr, 0, BlockedHookLikeThread, nullptr, 0, nullptr);
        if (!thread) return Fail(L"unable to start blocked hook-like thread");
        const auto result = InputHookThreadStop::RequestStop(thread, GetThreadId(thread), 20);
        if (result.waitResult != WAIT_TIMEOUT || !result.timedOut || InputHookThreadStop::ReapIfExited(thread))
            return Fail(L"input-hook timeout must retain a live thread for safe later reaping");
        CloseHandle(thread);
    }

    {
        BackgroundTaskService tasks(logger);
        HANDLE completed = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        tasks.Submit(L"test.throw", BackgroundTaskService::Priority::Normal,
            [](const std::shared_ptr<BackgroundTaskService::CancellationToken>&) { throw std::runtime_error("expected"); });
        tasks.Submit(L"test.after_throw", BackgroundTaskService::Priority::Normal,
            [completed](const std::shared_ptr<BackgroundTaskService::CancellationToken>&) { SetEvent(completed); });
        if (WaitForSingleObject(completed, 3000) != WAIT_OBJECT_0) return Fail(L"task exception stopped worker progress");
        CloseHandle(completed);
        tasks.Shutdown(std::chrono::milliseconds(1500));
    }

    {
        BackgroundTaskService tasks(logger);
        HANDLE gate = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        HANDLE cancelledTaskRan = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!gate || !cancelledTaskRan) return Fail(L"unable to create cancellation test events");

        // Occupy the interactive worker, then cancel a queued operation.  This
        // mirrors a command panel being replaced before its old worker starts.
        tasks.Submit(L"test.cancel.gate", BackgroundTaskService::Priority::Interactive,
            [gate](const std::shared_ptr<BackgroundTaskService::CancellationToken>&) {
                WaitForSingleObject(gate, 1000);
            });
        auto cancelled = tasks.Submit(L"test.cancel.queued", BackgroundTaskService::Priority::Interactive,
            [cancelledTaskRan](const std::shared_ptr<BackgroundTaskService::CancellationToken>&) {
                SetEvent(cancelledTaskRan);
            });
        if (!cancelled) return Fail(L"unable to queue cancellable task");
        cancelled.Cancel();
        SetEvent(gate);
        Sleep(100);
        if (WaitForSingleObject(cancelledTaskRan, 0) == WAIT_OBJECT_0)
            return Fail(L"cancelled queued task still executed");
        CloseHandle(gate);
        CloseHandle(cancelledTaskRan);
        tasks.Shutdown(std::chrono::milliseconds(1500));
    }

    {
        auto bus = std::make_shared<EventBus>(logger);
        int called = 0;
        EventBus::Token second = 0;
        bus->Subscribe(EventType::ConfigChanged, [&]() { bus->Unsubscribe(EventType::ConfigChanged, second); });
        second = bus->Subscribe(EventType::ConfigChanged, [&]() { called += 100; });
        bus->Subscribe(EventType::ConfigChanged, [&]() { throw std::runtime_error("expected callback failure"); });
        bus->Subscribe(EventType::ConfigChanged, [&]() { called += 1; });
        bus->Publish(EventType::ConfigChanged);
        if (called != 1) return Fail(L"event bus did not skip unsubscribed callback or isolate exception");
    }

    {
        MigrationBackupService migration;
        const std::wstring maliciousZip = temp + L"\\migration-traversal.zip";
        WriteCentralDirectoryOnlyZip(maliciousZip, "../outside.txt");
        if (migration.Preflight(maliciousZip).ok)
            return Fail(L"migration preflight accepted a traversal ZIP before extraction");
        const std::wstring allowedZip = temp + L"\\migration-manifest.zip";
        WriteCentralDirectoryOnlyZip(allowedZip, "manifest.json");
        if (!migration.Preflight(allowedZip).ok)
            return Fail(L"migration preflight rejected an allowed central-directory entry");
    }

    {
        const fs::path quotedSource = fs::path(temp) / L"archive's-source";
        const fs::path quotedZip = fs::path(temp) / L"export's.zip";
        const fs::path quotedExtract = fs::path(temp) / L"archive's-expanded";
        fs::create_directories(quotedSource);
        std::ofstream(quotedSource / L"metadata.txt") << "archive escaping regression";

        std::wstring archiveError;
        if (!ArchiveUtility::CompressDirectoryContents(quotedSource.wstring(), quotedZip.wstring(), 10000, archiveError))
            return Fail(L"archive export failed for a path containing an apostrophe");
        if (!ArchiveUtility::ExpandArchive(quotedZip.wstring(), quotedExtract.wstring(), 10000, archiveError))
            return Fail(L"archive import failed for a path containing an apostrophe");
        if (!fs::exists(quotedExtract / L"metadata.txt"))
            return Fail(L"archive round trip lost a file for a path containing an apostrophe");
    }

    std::wstring crashDir = temp + L"\\crash";
    fs::create_directories(crashDir);
    wchar_t exePath[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::wstring command = L"\"" + std::wstring(exePath) + L"\" --crash-child \"" + crashDir + L"\"";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process))
        return Fail(L"unable to launch crash helper");
    WaitForSingleObject(process.hProcess, 7000);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    if (!HasNonEmptyCrashArtifacts(crashDir)) return Fail(L"crash reporter did not create non-empty dump and metadata");

    fwprintf(stdout, L"[PASS] native async, callback, crash, migration ZIP, and archive escaping tests\n");
    return 0;
}
