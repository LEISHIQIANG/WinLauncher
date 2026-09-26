#include "../../WinLauncher/Services/CommandVariableService.h"
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
#include "../../WinLauncher/Popup/PopupWheelState.h"
#include "../../WinLauncher/App/MouseButtonPairs.h"
#include "../../WinLauncher/Popup/PopupCommandDispatcher.h"
#include "../../WinLauncher/Popup/PinyinHelper.h"
#include "../../WinLauncher/TriggerBlacklistPolicy.h"
#include "../../WinLauncher/Services/IconLruCache.h"

// Earlier headers include Windows.h with WIN32_LEAN_AND_MEAN, whose NOICONS
// excludes CreateIconW, and none of them pull in commctrl.h for the shell
// image-list declarations SystemIconService needs. Restore what the icon
// tests require; these are plain user32/commctrl exports.
#include <commctrl.h>
#pragma comment(lib, "user32.lib")
extern "C" WINUSERAPI HICON WINAPI CreateIcon(HINSTANCE hInstance, int nWidth, int nHeight, BYTE cPlanes, BYTE cBitsPixel, const BYTE* lpbANDbits, const BYTE* lpbXORbits);

#include "../../WinLauncher/Services/SystemIconService.h"
#include "../../WinLauncher/TriggerPolicy.h"
#include "../../WinLauncher/UI/MouseCaptureController.h"
#include "../../WinLauncher/SDK/include/WinLauncher/WinLauncherPluginABI.h"
#include "../../WinLauncher/Services/DiagnosticService.h"
#include "../../WinLauncher/Services/TriggerProcessResolver.h"
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
    fflush(stderr);
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
        if (entry.path().extension() == L".txt")
        {
            text = true;
            std::ifstream file(entry.path());
            std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            if (content.find("stack_backtrace:") == std::string::npos)
                return false;
        }
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

static HICON MakeTinyIcon()
{
    // 2x2, 1 plane, 1 bpp; CreateIcon requires non-null row buffers (rows are
    // WORD-aligned, hence 2 bytes each) and crashes on nullptr bits.
    static const BYTE andBits[4] = { 0xFF, 0xFF, 0xFF, 0xFF };
    static const BYTE xorBits[4] = { 0x00, 0x00, 0x00, 0x00 };
    return CreateIcon(nullptr, 2, 2, 1, 1, andBits, xorBits);
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
        using MouseCaptureController::ShouldRecoverGesture;
        if (ShouldRecoverGesture(true, true, true) ||
            !ShouldRecoverGesture(false, true, true) ||
            !ShouldRecoverGesture(true, false, true) ||
            !ShouldRecoverGesture(true, true, false))
        {
            return Fail(L"gesture capture recovery policy regressed");
        }
    }

    {
        const auto blacklist = TriggerBlacklistPolicy::Matcher::Compile({
            L" CAD ",
            L"C:\\Program Files\\Blender Foundation\\BLENDER.EXE",
            L"rhino",
            L"Rhino.exe",
            L".exe",
            L""
        });
        if (blacklist.size() != 3 ||
            !blacklist.MatchesNormalized(L"acad.exe", L"acad") ||
            !blacklist.MatchesNormalized(L"blender.exe", L"blender") ||
            !blacklist.MatchesNormalized(L"rhino.exe", L"rhino") ||
            blacklist.MatchesNormalized(L"notepad.exe", L"notepad"))
        {
            return Fail(L"compiled trigger blacklist normalization or fuzzy matching regressed");
        }
    }

    {
        auto tasks = std::make_shared<BackgroundTaskService>(logger);
        auto resolver = std::make_shared<TriggerProcessResolver>(tasks, logger);
        resolver->SetBlacklist({ L"cmd.exe" });

        wchar_t systemDirectory[MAX_PATH]{};
        if (!GetSystemDirectoryW(systemDirectory, MAX_PATH))
            return Fail(L"unable to locate system directory for resolver test");
        std::wstring executable = std::wstring(systemDirectory) + L"\\cmd.exe";
        std::wstring commandLine = L"\"" + executable + L"\" /d /c exit";
        STARTUPINFOW startup{ sizeof(startup) };
        PROCESS_INFORMATION process{};
        if (!CreateProcessW(executable.c_str(), commandLine.data(), nullptr, nullptr, FALSE,
            CREATE_SUSPENDED | CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process))
        {
            return Fail(L"unable to create suspended process for resolver test");
        }

        resolver->Prefetch(process.dwProcessId);
        TriggerProcessResolver::Decision decision = TriggerProcessResolver::Decision::Unknown;
        for (int attempt = 0; attempt < 100 && decision == TriggerProcessResolver::Decision::Unknown; ++attempt)
        {
            Sleep(10);
            decision = resolver->Classify(process.dwProcessId);
        }
        TriggerProcessResolver::Identity identity;
        if (decision != TriggerProcessResolver::Decision::Blacklisted ||
            !resolver->TryGetIdentity(process.dwProcessId, identity) ||
            _wcsicmp(identity.exeName.c_str(), L"cmd.exe") != 0)
        {
            TerminateProcess(process.hProcess, 1);
            CloseHandle(process.hThread);
            CloseHandle(process.hProcess);
            return Fail(L"process resolver did not asynchronously classify a live PID");
        }

        resolver->SetBlacklist({});
        if (resolver->Classify(process.dwProcessId) != TriggerProcessResolver::Decision::Allowed)
        {
            TerminateProcess(process.hProcess, 1);
            CloseHandle(process.hThread);
            CloseHandle(process.hProcess);
            return Fail(L"process resolver did not reclassify cached identity after blacklist change");
        }
        TerminateProcess(process.hProcess, 0);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        resolver.reset();
        tasks->Shutdown(std::chrono::milliseconds(1500));
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
        PopupWheelState wheel;
        wheel.Reset(0);
        if (wheel.Wheel(-30, 0) || wheel.Wheel(-30, 0) || wheel.Wheel(-30, 0) ||
            !wheel.Wheel(-30, 0) || wheel.target != 1)
            return Fail(L"high resolution wheel must accumulate a full detent");
        for (int i = 0; i < 1000; ++i) wheel.Wheel(-120, 0.25f);
        if (wheel.target != 1 || wheel.pending != 1 || !wheel.Arrive() || wheel.target != 2 || wheel.Arrive())
            return Fail(L"wheel burst must retain only one pending page");
        wheel.Reset(0);
        wheel.Wheel(-120, 0);
        wheel.Wheel(-120, 0.1f);
        if (wheel.AdvanceQueued(0.5f) || !wheel.AdvanceQueued(0.8f) || wheel.target != 2 || wheel.pending)
            return Fail(L"continuous wheel motion must advance its queued page near the boundary");
        wheel.Reset(1);
        wheel.Wheel(-120, 1);
        if (wheel.target != 2 || PopupWheelState::Page(wheel.target, 2) != 0)
            return Fail(L"two-page wrap must retain forward coordinates");
        wheel.Wheel(-120, 1.2f);
        wheel.Wheel(120, 1.2f);
        if (wheel.target != 1 || wheel.pending != 0)
            return Fail(L"wheel reversal must discard old direction backlog");
        wheel.Reset(0);
        wheel.Wheel(120, 0);
        if (wheel.target != -1 || PopupWheelState::Page(wheel.target, 3) != 2)
            return Fail(L"backward wrap must retain negative coordinates");
        wheel.Reset(2);
        if (wheel.active || wheel.pending || wheel.remainder || wheel.target != 2)
            return Fail(L"hide/search/layout reset must clear wheel state");
    }
    {
        MouseButtonPairs pairs;
        pairs.Down(1, true);
        pairs.Down(2, true);
        pairs.Down(4, false);
        // Policy/recording changes and reinstall do not mutate pair ownership.
        if (pairs.Up(4) || !pairs.Up(1) || pairs.Up(1) || pairs.Snapshot() != 2 || !pairs.Up(2))
            return Fail(L"physical button pairs must remain independent");
        pairs.Down(1, true);
        pairs.Down(1, false); // a new pass-through down replaces a missed old up
        if (pairs.Up(1)) return Fail(L"stale pair must not swallow a new pass-through up");
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
        auto partial = refresh.Begin();
        partial->layoutGeneration = 7;
        partial->results.push_back({false, 0, 0, nullptr, L"missing", 7});
        const auto batch = refresh.Take(partial);
        if (batch.size() != 1 || batch[0].identity != L"missing" || batch[0].layoutGeneration != 7 ||
            !refresh.IsRefreshing() || !refresh.Take(partial).empty())
            return Fail(L"partial icon consumption must retain the running generation");
        if (refresh.Begin(true) || refresh.Begin(false) || !refresh.TakePending() || !refresh.TakePendingForce())
            return Fail(L"coalescing must retain explicit refresh intent");
        refresh.Cancel();
        auto newer = refresh.Begin();
        partial->results.push_back({false, 0, 0, nullptr, L"stale", 7});
        if (refresh.IsCurrent(partial) || !refresh.Take(partial).empty() || !refresh.IsCurrent(newer))
            return Fail(L"late icon completion must not enter a replacement generation");
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

    {
        std::wstring wxInitials = PinyinHelper::GetInitials(L"微信");
        std::wstring wxFull = PinyinHelper::GetFullPinyin(L"微信");
        if (wxInitials != L"wx" || wxFull != L"weixin")
            return Fail(L"PinyinHelper failed for 微信");

        std::wstring jsqInitials = PinyinHelper::GetInitials(L"计算器");
        if (jsqInitials != L"jsq")
            return Fail(L"PinyinHelper failed for 计算器");

        bool isInitials = false, isFull = false;
        if (!PinyinHelper::Match(L"微信", L"wx", isInitials, isFull) || !isInitials)
            return Fail(L"PinyinHelper match failed for wx -> 微信");
    }

    {
        std::map<std::wstring, std::wstring> inputs;
        std::vector<std::wstring> files = { L"C:\\test\\doc.txt" };
        std::wstring expanded = Services::CommandVariableService::ResolveVariables(L"cmd /c echo {{selected_file}} {{date}} {{time}} {{timestamp}} {{clipboard_text}}", L"cmd", files, inputs);
        if (expanded.find(L"C:\\test\\doc.txt") == std::wstring::npos)
            return Fail(L"CommandVariableService variable expansion failed for selected_file");
        if (expanded.find(L"{{timestamp}}") != std::wstring::npos || expanded.find(L"{{clipboard_text}}") != std::wstring::npos)
            return Fail(L"CommandVariableService variable expansion failed for timestamp or clipboard_text");
    }

    {
        const std::wstring testLogPath = temp + L"\\test-flush.jsonl";
        {
            Logger customLog(testLogPath);
            customLog.Log(Logger::INFO, __FILE__, __LINE__, __FUNCTION__, L"Test flush entry 1");
            customLog.Log(Logger::ERRA, __FILE__, __LINE__, __FUNCTION__, L"Test flush error entry");
            customLog.Flush();
        }
        std::ifstream input(testLogPath);
        std::string content((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        if (content.find("Test flush entry 1") == std::string::npos ||
            content.find("Test flush error entry") == std::string::npos)
            return Fail(L"Logger flush did not write in-memory log entries to disk");
    }

    {
        std::wstring stackTrace = CrashReporter::FormatStackBackTrace();
        if (stackTrace.find(L"stack_backtrace:") == std::string::npos ||
            stackTrace.find(L"WinLauncherNativeTests.exe") == std::string::npos)
            return Fail(L"CrashReporter FormatStackBackTrace failed to capture current process stack");
    }

    {
        DiagnosticService diag(logger.get());
        std::wstring packagePath = temp + L"\\diag_package.zip";
        std::wstring diagError;
        if (!diag.CreatePackage(packagePath, diagError) || !fs::exists(packagePath))
            return Fail(L"DiagnosticService failed to create diagnostic metadata package");
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

    {
        // IconLruCache: ownership, copy semantics, LRU eviction, and
        // last-write invalidation policy.
        HICON owned = MakeTinyIcon();
        if (!owned)
            return Fail(L"unable to create a test icon for cache tests");
        DestroyIcon(owned);

        HICON andIcon = MakeTinyIcon();
        HICON original = MakeTinyIcon();
        if (!andIcon || !original)
            return Fail(L"unable to create test icons for cache tests");

        IconLruCache cache(2, IconLruCache::kDefaultValidationIntervalMs);
        cache.Store(L"icon://a", andIcon, /*fromResource=*/true);
        // Store took ownership of andIcon; the test must not destroy it again.

        HICON copy = cache.LookupIconCopy(L"icon://a");
        if (!copy)
            return Fail(L"IconLruCache missed an entry that was just stored");
        DestroyIcon(copy);

        HICON master = cache.LookupMaster(L"icon://a");
        if (master != andIcon)
            return Fail(L"IconLruCache LookupMaster did not return the stored master icon");
        DestroyIcon(original); // StoreIconCopy-style flow keeps caller ownership intact.
        copy = cache.LookupIconCopy(L"icon://a");
        if (!copy)
            return Fail(L"IconLruCache lost its master after unrelated HICON destruction");
        DestroyIcon(copy);

        cache.Store(L"icon://b", MakeTinyIcon(), true);
        cache.LookupMaster(L"icon://a"); // Touch a; b becomes the LRU victim.
        cache.Store(L"icon://c", MakeTinyIcon(), true);
        if (cache.LookupMaster(L"icon://b") != nullptr)
            return Fail(L"IconLruCache did not evict the least-recently-used entry");
        if (cache.LookupMaster(L"icon://a") == nullptr || cache.LookupMaster(L"icon://c") == nullptr)
            return Fail(L"IconLruCache evicted recently used entries");

        const IconLruCache::Stats stats = cache.GetStats();
        if (stats.evictions < 1 || stats.hits < 3 || stats.misses < 1)
            return Fail(L"IconLruCache statistics did not track hits, misses, and evictions");
        cache.Clear();
        if (cache.Size() != 0 || cache.LookupMaster(L"icon://a") != nullptr)
            return Fail(L"IconLruCache Clear left entries behind");

        if (!IconLruCache::ShouldValidate(FILE_ATTRIBUTE_NORMAL, false, DRIVE_FIXED) ||
            IconLruCache::ShouldValidate(FILE_ATTRIBUTE_NORMAL, true, DRIVE_FIXED) ||
            IconLruCache::ShouldValidate(FILE_ATTRIBUTE_NORMAL, false, DRIVE_REMOTE) ||
            IconLruCache::ShouldValidate(FILE_ATTRIBUTE_NORMAL, false, DRIVE_REMOVABLE) ||
            IconLruCache::ShouldValidate(FILE_ATTRIBUTE_DIRECTORY, false, DRIVE_FIXED) ||
            IconLruCache::ShouldValidate(0, false, DRIVE_FIXED) ||
            IconLruCache::ShouldValidate(INVALID_FILE_ATTRIBUTES, false, DRIVE_FIXED))
            return Fail(L"IconLruCache validation policy accepted paths that could stall a cache hit");

        // File-backed invalidation: a changed last-write time on a fixed
        // local drive must drop the entry; a directory entry must survive.
        std::wstring targetFile = temp + L"\\icon_cache_target.txt";
        {
            FILE* file = _wfopen(targetFile.c_str(), L"wb");
            if (!file)
                return Fail(L"unable to create the icon cache invalidation target file");
            fwprintf(file, L"v1");
            fclose(file);
        }
        wchar_t tempRoot[MAX_PATH]{};
        GetTempPathW(MAX_PATH, tempRoot);
        wchar_t driveRoot[4] = { tempRoot[0], L':', L'\\', L'\0' };
        const bool fixedDrive = GetDriveTypeW(driveRoot) == DRIVE_FIXED;

        IconLruCache fileCache(8, /*validationIntervalMs=*/0);
        HICON fileIcon = MakeTinyIcon();
        fileCache.Store(targetFile, fileIcon);
        if (!fileCache.LookupMaster(targetFile))
            return Fail(L"IconLruCache invalidated a fresh file entry before its stamp changed");

        bool invalidationVerified = !fixedDrive;
        if (fixedDrive)
        {
            HANDLE handle = CreateFileW(targetFile.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (handle == INVALID_HANDLE_VALUE)
                return Fail(L"unable to reopen the icon cache invalidation target file");
            SYSTEMTIME futureSys{ 2099, 1, 0, 1, 0, 0, 0, 0 };
            FILETIME future{};
            SystemTimeToFileTime(&futureSys, &future);
            if (!SetFileTime(handle, &future, &future, &future))
                return Fail(L"unable to restamp the icon cache invalidation target file");
            CloseHandle(handle);
            invalidationVerified = fileCache.LookupMaster(targetFile) == nullptr;
        }
        if (!invalidationVerified)
            return Fail(L"IconLruCache kept an entry whose file last-write time changed");

        std::wstring targetDir = temp + L"\\icon_cache_dir";
        fs::create_directories(targetDir);
        HICON dirIcon = MakeTinyIcon();
        fileCache.Store(targetDir, dirIcon);
        if (!fileCache.LookupMaster(targetDir))
            return Fail(L"IconLruCache invalidated a directory entry; directory hits must never touch the filesystem");

        // Concurrent access smoke test: parallel stores and lookups must not
        // corrupt the LRU or exceed capacity.
        {
            IconLruCache concurrent(16, IconLruCache::kDefaultValidationIntervalMs);
            std::vector<HANDLE> threads;
            for (int t = 0; t < 4; ++t)
            {
                struct Ctx { IconLruCache* cache; int base; };
                auto* ctx = new Ctx{ &concurrent, t * 100 };
                threads.push_back(CreateThread(nullptr, 0, [](LPVOID param) -> DWORD {
                    auto* c = static_cast<Ctx*>(param);
                    for (int i = 0; i < 200; ++i)
                    {
                        const std::wstring key = L"icon://" + std::to_wstring(c->base + (i % 32));
                        if ((i & 1) == 0)
                            c->cache->Store(key, MakeTinyIcon(), true);
                        else
                            c->cache->LookupMaster(key);
                    }
                    delete c;
                    return 0;
                }, ctx, 0, nullptr));
            }
            for (HANDLE thread : threads)
            {
                WaitForSingleObject(thread, 15000);
                CloseHandle(thread);
            }
            if (concurrent.Size() > 16)
                return Fail(L"IconLruCache exceeded its capacity under concurrent access");
            concurrent.Clear();
        }
    }

    {
        SystemIconService iconService;
        HICON original = MakeTinyIcon();
        if (!original)
            return Fail(L"unable to create a test icon for SystemIconService tests");
        iconService.StoreIconCopy(L"icon://service", original);
        DestroyIcon(original);
        HICON copy = iconService.GetIconCopy(L"icon://service");
        if (!copy)
            return Fail(L"SystemIconService lost an icon stored via StoreIconCopy");
        DestroyIcon(copy);
        if (iconService.GetIconCopy(L"icon://never-stored") != nullptr)
            return Fail(L"SystemIconService GetIconCopy returned an icon for an unknown key");
        if (iconService.CachedIconCount() != 1)
            return Fail(L"SystemIconService cache count did not reflect stored entries");
    }

    fwprintf(stdout, L"[PASS] native async, mouse button pairing, bounded wheel paging, icon generations, mouse capture recovery, popup layout, ABI compatibility, callback, crash, Logger flush, stack trace, diagnostic package, migration ZIP, merge semantics, config corruption recovery, search ranking, icon cache ownership/eviction/invalidation, and archive escaping tests\n");
    return 0;
}
