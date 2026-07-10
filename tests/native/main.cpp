#include "../../WinLauncher/App/BackgroundTaskService.h"
#include "../../WinLauncher/App/CrashReporter.h"
#include "../../WinLauncher/App/EventBus.h"
#include "../../WinLauncher/App/Logger.h"
#include "../../WinLauncher/App/InputHookThreadStop.h"
#include <Windows.h>
#include <shellapi.h>
#include <chrono>
#include <cstdio>
#include <filesystem>
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
        const auto result = InputHookThreadStop::RequestStopAndClose(thread, GetThreadId(thread), 1000, []() {});
        if (!result.quitPosted || result.waitResult != WAIT_OBJECT_0 || result.forceTerminated || result.exitCode != 37)
            return Fail(L"cooperative input-hook shutdown did not complete cleanly");
    }

    {
        HANDLE thread = CreateThread(nullptr, 0, BlockedHookLikeThread, nullptr, 0, nullptr);
        if (!thread) return Fail(L"unable to start blocked hook-like thread");
        bool cleanupCalled = false;
        const auto result = InputHookThreadStop::RequestStopAndClose(thread, GetThreadId(thread), 20, [&cleanupCalled]() { cleanupCalled = true; });
        if (result.waitResult != WAIT_TIMEOUT || !cleanupCalled || !result.forceTerminated)
            return Fail(L"input-hook timeout fallback did not run after cooperative shutdown timed out");
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

    fwprintf(stdout, L"[PASS] native async, callback, and crash tests\n");
    return 0;
}
