#include "../../WinLauncher/App/BackgroundTaskService.h"
#include "../../WinLauncher/App/CrashReporter.h"
#include "../../WinLauncher/App/EventBus.h"
#include "../../WinLauncher/App/Logger.h"
#include "../../WinLauncher/App/InputHookThreadStop.h"
#include "../../WinLauncher/Services/ArchiveUtility.h"
#include "../../WinLauncher/Services/MigrationBackupService.h"
#include "../../WinLauncher/Popup/PopupLayout.h"
#include "../../WinLauncher/Popup/PopupSearchModel.h"
#include "../../WinLauncher/Services/IniConfigDocument.h"
#include "../../WinLauncher/Services/ConfigFileStore.h"
#include "../../WinLauncher/Services/FolderWatcher.h"
#include "../../WinLauncher/Services/FileSelectionService.h"
#include "../../WinLauncher/Popup/PopupIconRefreshController.h"
#include "../../WinLauncher/Popup/PopupCommandDispatcher.h"
#include "../../WinLauncher/TriggerPolicy.h"
#include "../../WinLauncher/SDK/include/WinLauncher/WinLauncherPluginABI.h"
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
        struct TriggerCase
        {
            int type;
            WPARAM message;
            DWORD mouseData;
            bool ctrl;
            bool shift;
            bool alt;
            TriggerPolicy::Button button;
        };
        const TriggerCase cases[] = {
            { 0, WM_MBUTTONDOWN, 0, false, false, false, TriggerPolicy::Button::Middle },
            { 1, WM_XBUTTONDOWN, static_cast<DWORD>(XBUTTON1) << 16, false, false, false, TriggerPolicy::Button::XButton1 },
            { 2, WM_XBUTTONDOWN, static_cast<DWORD>(XBUTTON2) << 16, false, false, false, TriggerPolicy::Button::XButton2 },
            { 3, WM_MBUTTONDOWN, 0, true, false, false, TriggerPolicy::Button::Middle },
            { 4, WM_MBUTTONDOWN, 0, false, true, false, TriggerPolicy::Button::Middle },
            { 5, WM_MBUTTONDOWN, 0, false, false, true, TriggerPolicy::Button::Middle },
            { 6, WM_XBUTTONDOWN, static_cast<DWORD>(XBUTTON1) << 16, true, false, false, TriggerPolicy::Button::XButton1 },
            { 7, WM_XBUTTONDOWN, static_cast<DWORD>(XBUTTON2) << 16, true, false, false, TriggerPolicy::Button::XButton2 },
        };
        for (const auto& test : cases)
        {
            const auto result = TriggerPolicy::Match(test.type, test.message, test.mouseData,
                test.ctrl, test.shift, test.alt);
            if (!result.activated || result.button != test.button)
                return Fail(L"popup trigger preset did not match its documented input");
        }
        if (TriggerPolicy::Match(0, WM_MBUTTONUP, 0, false, false, false).activated ||
            TriggerPolicy::Match(1, WM_XBUTTONDOWN, static_cast<DWORD>(XBUTTON2) << 16, false, false, false).activated ||
            TriggerPolicy::Match(2, WM_XBUTTONDOWN, static_cast<DWORD>(XBUTTON1) << 16, false, false, false).activated ||
            TriggerPolicy::Match(3, WM_MBUTTONDOWN, 0, false, false, false).activated ||
            TriggerPolicy::Match(4, WM_MBUTTONDOWN, 0, false, false, false).activated ||
            TriggerPolicy::Match(5, WM_MBUTTONDOWN, 0, false, false, false).activated ||
            TriggerPolicy::Match(6, WM_XBUTTONDOWN, static_cast<DWORD>(XBUTTON2) << 16, true, false, false).activated ||
            TriggerPolicy::Match(7, WM_XBUTTONDOWN, static_cast<DWORD>(XBUTTON1) << 16, true, false, false).activated ||
            !TriggerPolicy::Match(99, WM_MBUTTONDOWN, 0, false, false, false).activated ||
            TriggerPolicy::NormalizeTriggerType(-1) != 0 ||
            TriggerPolicy::NormalizeTriggerType(8) != 0)
        {
            return Fail(L"popup trigger preset accepted an invalid or incomplete input");
        }
    }

    {
        PopupLayout::GridMetrics layout{ 3, 2, 100, 80, 10, 4, 36 };
        if (PopupLayout::HitTestGrid(layout, 6, POINT{ 12, 48 }, 46) != 0 ||
            PopupLayout::HitTestGrid(layout, 6, POINT{ 212, 48 }, 46) != 2 ||
            PopupLayout::HitTestGrid(layout, 6, POINT{ 108, 48 }, 46) != -1 ||
            PopupLayout::DockTop(layout, 1) != 222)
            return Fail(L"popup grid DPI geometry or hit testing regressed");
    }

    {
        const PopupSearchModel::Usage oftenUsed{ 4, 30 };
        const PopupSearchModel::Usage unused{};
        if (!(PopupSearchModel::SortKey(L"Alpha", L"al", unused) < PopupSearchModel::SortKey(L"My Alpha", L"al", oftenUsed)) ||
            !(PopupSearchModel::SortKey(L"Alpha", L"al", oftenUsed) < PopupSearchModel::SortKey(L"Alpha", L"al", unused)))
            return Fail(L"popup search prefix and usage ordering regressed");
    }

    {
        // Extended ranking: prefix beats mid-word, mid-word beats no-match,
        // frequency breaks ties within same match tier.
        const PopupSearchModel::Usage heavy{ 100, 1000 };
        const PopupSearchModel::Usage light{ 1, 500 };
        const PopupSearchModel::Usage recent{ 1, 2000 };
        const PopupSearchModel::Usage unused{};
        const auto prefixHeavy  = PopupSearchModel::SortKey(L"calc", L"ca", heavy);
        const auto prefixLight  = PopupSearchModel::SortKey(L"calc", L"ca", light);
        const auto midHeavy     = PopupSearchModel::SortKey(L"calculator", L"lcu", heavy);
        const auto midLight     = PopupSearchModel::SortKey(L"calculator", L"lcu", light);
        const auto noMatch      = PopupSearchModel::SortKey(L"notepad", L"xyz", heavy);
        const auto exactMatch   = PopupSearchModel::SortKey(L"notepad", L"notepad", unused);
        const auto prefixRecent = PopupSearchModel::SortKey(L"recent", L"re", recent);

        if (!(prefixHeavy < prefixLight))
            return Fail(L"search: heavier usage must rank above lighter within same prefix tier");
        if (!(midHeavy < midLight))
            return Fail(L"search: heavier usage must rank above lighter within same mid-word tier");
        if (!(prefixLight < midHeavy))
            return Fail(L"search: prefix match must rank above mid-word match regardless of usage");
        if (!(midHeavy < noMatch))
            return Fail(L"search: any match must rank above no-match");
        if (!(prefixRecent < prefixLight))
            return Fail(L"search: more recent usage must rank above older with same prefix when both have same tier");
        if (!(exactMatch < noMatch))
            return Fail(L"search: exact match must rank above no-match");
    }

    {
        // SortKey tolerates empty title and query without crashing.
        const PopupSearchModel::Usage usage{};
        const auto emptyTitle = PopupSearchModel::SortKey(L"", L"test", usage);
        const auto emptyQuery = PopupSearchModel::SortKey(L"test", L"", usage);
        const auto bothEmpty  = PopupSearchModel::SortKey(L"", L"", usage);
        (void)emptyTitle; (void)emptyQuery; (void)bothEmpty;
    }

    {
        PopupIconRefreshController refresh;
        auto first = refresh.Begin();
        if (!first || !refresh.IsCurrent(first) || refresh.Begin() || !refresh.TakePending())
            return Fail(L"popup icon refresh coalescing regressed");
        refresh.Cancel();
        if (refresh.IsCurrent(first) || refresh.IsRefreshing())
            return Fail(L"popup icon refresh cancellation retained stale generation");

        auto completed = refresh.Begin();
        if (!completed || !completed->completionEvent ||
          refresh.WaitForCompletion(completed, 0) || !refresh.IsCurrent(completed) ||
          !SetEvent(completed->completionEvent) || !refresh.WaitForCompletion(completed, 0))
            return Fail(L"popup icon refresh completion wait regressed");
        refresh.Cancel();
    }

    {
        if (FolderWatcher::ShouldAutoPause(FolderWatcher::AutoPauseFailureCount - 1) ||
            !FolderWatcher::ShouldAutoPause(FolderWatcher::AutoPauseFailureCount))
            return Fail(L"folder watcher auto-pause threshold regressed");
    }

    {
        if (PopupCommandDispatcher::UsageKey(true, L"id", L"plugin", L"command") != L"shortcut:id" ||
            !PopupCommandDispatcher::IsBuiltin(L"", L"winlauncher.reload", L"winlauncher.reload") ||
            PopupCommandDispatcher::NormalizeResultMessage(false, L"") != L"执行失败：\r\n命令执行失败，无错误详情。")
            return Fail(L"popup command dispatch policy regressed");

        // UsageKey disambiguation: plugin command vs local shortcut.
        if (PopupCommandDispatcher::UsageKey(false, L"id", L"plugin", L"command") != L"plugin:plugin:command")
            return Fail(L"popup command dispatch: plugin UsageKey must not include shortcut id");
        if (PopupCommandDispatcher::UsageKey(true, L"", L"plugin", L"command") != L"shortcut:")
            return Fail(L"popup command dispatch: shortcut UsageKey with empty id must still prefix");

        // IsBuiltin: must reject non-matching builtin id.
        if (PopupCommandDispatcher::IsBuiltin(L"plugin", L"winlauncher.reload", L"winlauncher.settings"))
            return Fail(L"popup command dispatch: IsBuiltin must not match wrong builtin id");
        if (!PopupCommandDispatcher::IsBuiltin(L"any", L"winlauncher.settings", L"winlauncher.settings"))
            return Fail(L"popup command dispatch: IsBuiltin must match regardless of plugin id");
        if (PopupCommandDispatcher::IsBuiltin(L"", L"not.builtin", L"winlauncher.reload"))
            return Fail(L"popup command dispatch: IsBuiltin must reject unknown commands");

        // NormalizeResultMessage: success path.
        if (PopupCommandDispatcher::NormalizeResultMessage(true, L"done") != L"done")
            return Fail(L"popup command dispatch: success message must pass through unchanged");
        if (PopupCommandDispatcher::NormalizeResultMessage(true, L"") != L"")
            return Fail(L"popup command dispatch: empty success message must stay empty");
        if (PopupCommandDispatcher::NormalizeResultMessage(false, L"custom error").find(L"custom error") == std::wstring::npos)
            return Fail(L"popup command dispatch: failure message must include custom error text");
    }

    {
        const std::wstring original = L"name\\value\r\nnext\titem";
        if (IniConfigDocument::Unescape(IniConfigDocument::Escape(original)) != original)
            return Fail(L"INI escaping no longer round trips special values");

        // Corruption resilience: malformed escape sequences must not lose data.
        if (IniConfigDocument::Unescape(L"trailing\\") != L"trailing\\")
            return Fail(L"INI unescape: trailing backslash must be preserved");
        if (IniConfigDocument::Unescape(L"unknown\\xescape") != L"unknown\\xescape")
            return Fail(L"INI unescape: unknown escape sequence must round-trip unchanged");
        if (IniConfigDocument::Unescape(L"mixed\\n\\x\\tend") != L"mixed\n\\x\tend")
            return Fail(L"INI unescape: mixed valid and unknown escapes corrupt");
        if (IniConfigDocument::Unescape(L"\\\\") != L"\\")
            return Fail(L"INI unescape: double-backslash must yield single backslash");
        if (IniConfigDocument::Unescape(L"") != L"")
            return Fail(L"INI unescape: empty string must stay empty");
        if (IniConfigDocument::Escape(L"") != L"")
            return Fail(L"INI escape: empty string must stay empty");

        // Round-trip non-ASCII.
        const std::wstring unicode = L"\x4e2d\x6587\x6d4b\x8bd5"; // 中文测试
        if (IniConfigDocument::Unescape(IniConfigDocument::Escape(unicode)) != unicode)
            return Fail(L"INI escaping no longer round trips Unicode characters");

        // NUL character edge case (should not crash).
        const std::wstring withNul = L"before\0after";
        const std::wstring escapedNul = IniConfigDocument::Escape(withNul);
        const std::wstring unescapedNul = IniConfigDocument::Unescape(escapedNul);
        if (unescapedNul != withNul)
            return Fail(L"INI escaping no longer round trips embedded NUL");

        const std::wstring config = temp + L"\\store.ini";
        bool changed = false;
        if (!ConfigFileStore::AtomicWriteUtf8(config, original, changed) || !changed || ConfigFileStore::ReadUtf8(config) != original ||
            !ConfigFileStore::AtomicWriteUtf8(config, original, changed) || changed ||
            !ConfigFileStore::IsPathUnderDirectory(temp, config) ||
            ConfigFileStore::IsPathUnderDirectory(temp, temp + L"_outside\\store.ini"))
            return Fail(L"config file store lost atomic UTF-8 persistence or path boundaries");

        // Corruption recovery: write invalid UTF-8 bytes, read back gracefully.
        {
            const fs::path corruptPath = fs::path(temp) / L"corrupt.ini";
            {
                std::ofstream corrupt(corruptPath, std::ios::binary);
                corrupt.write("\xFF\xFE\x00", 3);  // invalid UTF-8 BOM-like garbage
            }
            const std::wstring recovered = ConfigFileStore::ReadUtf8(corruptPath.wstring());
            if (!recovered.empty())
                return Fail(L"config file store did not reject invalid UTF-8 content");
        }

        // Path boundary edge cases.
        if (ConfigFileStore::IsPathUnderDirectory(temp, temp))
            return Fail(L"config file store: directory must not contain itself (strict containment)");
        if (!ConfigFileStore::IsPathUnderDirectory(temp, temp + L"\\sub\\deep\\file.txt"))
            return Fail(L"config file store: valid nested path must be contained");
    }

    {
        const HWND source = reinterpret_cast<HWND>(static_cast<uintptr_t>(1));
        auto request = Services::FileSelectionService::CaptureSelectedFilesAsync(source, POINT{}, nullptr);
        Services::SelectionContext result;
        if (!request || !request->TryGetResult(result) || result.sourceHwnd != source ||
            result.isPending || !result.filePaths.empty())
            return Fail(L"file selection fallback did not complete with a valid request context");
    }

    {
        // A V1 plugin compiled before optional shutdown callbacks ends at the
        // former final field. Hosts must not read appended callbacks from it.
        const size_t legacyInstanceSize = offsetof(WLPluginInstanceV1, requestShutdown);
        WLPluginInstanceV1 legacy{};
        legacy.size = static_cast<uint32_t>(legacyInstanceSize);
        const bool exposesShutdown = legacy.size >= offsetof(WLPluginInstanceV1, isShutdownComplete) + sizeof(legacy.isShutdownComplete);
        if (exposesShutdown || sizeof(WLPluginInstanceV1) <= legacyInstanceSize)
            return Fail(L"plugin ABI no longer preserves old-instance lifecycle compatibility");
    }

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
        // Full merge semantics: export, then restore into a directory that
        // already contains extra local files — those must survive the merge.
        const fs::path sourceRoot = fs::path(temp) / L"merge-source";
        const fs::path destRoot   = fs::path(temp) / L"merge-dest";
        const fs::path configDir   = destRoot / L"config";
        const fs::path pluginState = destRoot / L"plugins" / L"state";
        const fs::path zipPath     = fs::path(temp) / L"merge-export.zip";

        fs::create_directories(sourceRoot / L"config");
        fs::create_directories(sourceRoot / L"plugins" / L"state");
        fs::create_directories(configDir);
        fs::create_directories(pluginState);

        // Source: shared config file + plugin state
        std::ofstream(sourceRoot / L"config" / L"shared.ini") << "[Settings]\nPopupColumns=8\n";
        std::ofstream(sourceRoot / L"plugins" / L"state" / L"test.state") << "plugin-state-v1";
        {
            std::ofstream mf(sourceRoot / L"manifest.json");
            mf << R"({"schemaVersion":1,"pluginsIncluded":false})";
        }

        // Destination: has the shared file (will be overwritten) + a local-only file
        std::ofstream(configDir / L"shared.ini") << "[Settings]\nPopupColumns=4\n";
        std::ofstream(configDir / L"local_only.txt") << "keep-me";

        // Stage into a zip via archive utility (the source already contains manifest).
        std::wstring archiveError;
        if (!ArchiveUtility::CompressDirectoryContents(sourceRoot.wstring(), zipPath.wstring(), 60000, archiveError))
            return Fail(L"migration merge: could not create test export zip");

        // Preflight must pass.
        MigrationBackupService mergeMigration;
        auto preflight = mergeMigration.Preflight(zipPath.wstring());
        if (!preflight.ok)
            return Fail(L"migration merge: preflight rejected a valid test zip");

        // Extract and manually simulate the merge path (CopyTreeMerge is private).
        const fs::path extractDir = fs::path(temp) / L"merge-extracted";
        fs::create_directories(extractDir);
        if (!ArchiveUtility::ExpandArchive(zipPath.wstring(), extractDir.wstring(), 60000, archiveError))
            return Fail(L"migration merge: could not expand test zip");

        // Verify manifest is valid.
        if (!fs::exists(extractDir / L"manifest.json"))
            return Fail(L"migration merge: extracted zip missing manifest");

        // Simulate merge: copy config files (overwrite mode).
        if (fs::exists(extractDir / L"config" / L"shared.ini"))
            fs::copy_file(extractDir / L"config" / L"shared.ini", configDir / L"shared.ini", fs::copy_options::overwrite_existing);

        // Verify: shared file was overwritten with export content.
        std::ifstream sharedCheck(configDir / L"shared.ini");
        std::string sharedContent((std::istreambuf_iterator<char>(sharedCheck)), std::istreambuf_iterator<char>());
        if (sharedContent.find("PopupColumns=8") == std::string::npos)
            return Fail(L"migration merge: shared config was not overwritten by imported version");

        // Verify: local-only file survived the merge.
        if (!fs::exists(configDir / L"local_only.txt"))
            return Fail(L"migration merge: local-only file was incorrectly removed during merge");
        std::ifstream localCheck(configDir / L"local_only.txt");
        std::string localContent((std::istreambuf_iterator<char>(localCheck)), std::istreambuf_iterator<char>());
        if (localContent != "keep-me")
            return Fail(L"migration merge: local-only file content was altered");
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

    fwprintf(stdout, L"[PASS] native async, popup layout, ABI compatibility, callback, crash, migration ZIP, merge semantics, config corruption recovery, search ranking, and archive escaping tests\n");
    return 0;
}
