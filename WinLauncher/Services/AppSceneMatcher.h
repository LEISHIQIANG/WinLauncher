#pragma once
#include "../Model/ShortcutInfo.h"
#include <Windows.h>
#include <algorithm>
#include <cwctype>
#include <string>
#include <vector>

namespace AppScene
{
    struct AppIdentity
    {
        std::wstring exePath;
        std::wstring exeName;
        std::wstring windowTitle;
        bool valid = false;
    };

    struct AppCandidate
    {
        std::wstring exePath;
        std::wstring exeName;
        std::wstring windowTitle;
        std::wstring displayName;
    };

    inline std::wstring ToLower(std::wstring value)
    {
        std::transform(value.begin(), value.end(), value.begin(), [](wchar_t c) {
            return (wchar_t)towlower(c);
        });
        return value;
    }

    inline std::wstring FileNameOf(const std::wstring& path)
    {
        size_t pos = path.find_last_of(L"\\/");
        if (pos == std::wstring::npos)
            return path;
        return path.substr(pos + 1);
    }

    inline std::wstring StripExeExtension(const std::wstring& name)
    {
        if (name.size() > 4)
        {
            std::wstring suffix = ToLower(name.substr(name.size() - 4));
            if (suffix == L".exe")
                return name.substr(0, name.size() - 4);
        }
        return name;
    }

    inline std::wstring TrimDisplayName(std::wstring value)
    {
        while (!value.empty() && iswspace(value.front()))
            value.erase(value.begin());
        while (!value.empty() && iswspace(value.back()))
            value.pop_back();
        return value;
    }

    inline std::wstring FriendlyNameForExe(const std::wstring& exeName)
    {
        std::wstring key = ToLower(exeName);
        if (key == L"chrome.exe") return L"Google Chrome";
        if (key == L"msedge.exe") return L"Microsoft Edge";
        if (key == L"firefox.exe") return L"Firefox";
        if (key == L"wechat.exe" || key == L"weixin.exe") return L"WeChat";
        if (key == L"qq.exe" || key == L"ntqq.exe") return L"QQ";
        if (key == L"dingtalk.exe") return L"DingTalk";
        if (key == L"feishu.exe" || key == L"lark.exe") return L"飞书";
        if (key == L"teams.exe" || key == L"msteams.exe") return L"Teams";
        if (key == L"code.exe") return L"VS Code";
        if (key == L"cursor.exe") return L"Cursor";
        if (key == L"devenv.exe") return L"Visual Studio";
        if (key == L"idea64.exe") return L"IntelliJ IDEA";
        if (key == L"pycharm64.exe") return L"PyCharm";
        if (key == L"clion64.exe") return L"CLion";
        if (key == L"webstorm64.exe") return L"WebStorm";
        if (key == L"explorer.exe") return L"资源管理器";
        if (key == L"cmd.exe") return L"命令提示符";
        if (key == L"powershell.exe") return L"PowerShell";
        if (key == L"windowsterminal.exe" || key == L"wt.exe") return L"Windows Terminal";
        if (key == L"notepad.exe") return L"记事本";
        if (key == L"excel.exe") return L"Excel";
        if (key == L"winword.exe") return L"Word";
        if (key == L"powerpnt.exe") return L"PowerPoint";
        if (key == L"photoshop.exe") return L"Photoshop";
        if (key == L"figma.exe") return L"Figma";
        if (key == L"codex.exe") return L"Codex";
        return StripExeExtension(exeName);
    }

    inline std::wstring FriendlyNameForWindow(const AppIdentity& identity)
    {
        std::wstring friendly = FriendlyNameForExe(identity.exeName);
        if (!identity.windowTitle.empty())
        {
            std::wstring title = TrimDisplayName(identity.windowTitle);
            if (!title.empty() && title.size() <= 24)
                return title;
        }
        return friendly;
    }

    inline std::wstring GetWindowTextValue(HWND hwnd)
    {
        int len = GetWindowTextLengthW(hwnd);
        if (len <= 0)
            return L"";

        std::wstring text((size_t)len + 1, L'\0');
        GetWindowTextW(hwnd, text.data(), len + 1);
        text.resize((size_t)len);
        return text;
    }

    inline std::wstring QueryProcessImagePath(DWORD pid)
    {
        if (pid == 0)
            return L"";

        HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (!hProcess)
            return L"";

        wchar_t buffer[MAX_PATH * 4]{};
        DWORD size = (DWORD)(sizeof(buffer) / sizeof(buffer[0]));
        std::wstring path;
        if (QueryFullProcessImageNameW(hProcess, 0, buffer, &size) && size > 0)
        {
            path.assign(buffer, size);
        }
        CloseHandle(hProcess);
        return path;
    }

    inline AppIdentity IdentifyWindow(HWND hwnd)
    {
        AppIdentity identity;
        if (!hwnd)
            return identity;

        HWND root = GetAncestor(hwnd, GA_ROOT);
        if (root)
            hwnd = root;

        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        std::wstring path = QueryProcessImagePath(pid);
        if (path.empty())
            return identity;

        identity.exePath = path;
        identity.exeName = FileNameOf(path);
        identity.windowTitle = GetWindowTextValue(hwnd);
        identity.valid = true;
        return identity;
    }

    inline AppIdentity IdentifyTriggerApp(POINT triggerPoint)
    {
        DWORD selfPid = GetCurrentProcessId();
        HWND hwndAtPoint = WindowFromPoint(triggerPoint);
        AppIdentity fromPoint = IdentifyWindow(hwndAtPoint);

        DWORD pointPid = 0;
        if (hwndAtPoint)
            GetWindowThreadProcessId(GetAncestor(hwndAtPoint, GA_ROOT), &pointPid);

        if (fromPoint.valid && pointPid != selfPid)
            return fromPoint;

        HWND foreground = GetForegroundWindow();
        AppIdentity foregroundApp = IdentifyWindow(foreground);
        if (foregroundApp.valid)
            return foregroundApp;

        return fromPoint;
    }

    inline bool MatchesStoredApp(const std::wstring& storedApp, const AppIdentity& app)
    {
        if (storedApp.empty() || !app.valid)
            return false;

        std::wstring stored = ToLower(storedApp);
        std::wstring path = ToLower(app.exePath);
        std::wstring name = ToLower(app.exeName);

        bool looksLikePath = stored.find(L'\\') != std::wstring::npos || stored.find(L'/') != std::wstring::npos;
        if (looksLikePath)
            return stored == path;
        return stored == name || stored == FileNameOf(path);
    }

    inline bool IsPageVisibleForApp(const std::vector<std::wstring>& sceneApps, Model::PageSceneMode mode, const AppIdentity& app)
    {
        if (sceneApps.empty() || !app.valid)
            return true;

        bool matched = false;
        for (const auto& stored : sceneApps)
        {
            if (MatchesStoredApp(stored, app))
            {
                matched = true;
                break;
            }
        }

        if (mode == Model::PageSceneMode::Blacklist)
            return !matched;
        return matched;
    }

    inline bool IsDuplicateCandidate(const std::vector<AppCandidate>& apps, const std::wstring& exePath, const std::wstring& exeName)
    {
        std::wstring path = ToLower(exePath);
        std::wstring name = ToLower(exeName);
        for (const auto& app : apps)
        {
            if ((!path.empty() && ToLower(app.exePath) == path) ||
                (!name.empty() && ToLower(app.exeName) == name))
                return true;
        }
        return false;
    }

    inline void AddCandidateIfMissing(std::vector<AppCandidate>& apps, const std::wstring& exeName, const std::wstring& displayName)
    {
        if (exeName.empty() || IsDuplicateCandidate(apps, L"", exeName))
            return;
        apps.push_back({ L"", exeName, L"", displayName.empty() ? FriendlyNameForExe(exeName) : displayName });
    }

    inline void AddCommonCandidates(std::vector<AppCandidate>& apps)
    {
        AddCandidateIfMissing(apps, L"chrome.exe", L"Google Chrome");
        AddCandidateIfMissing(apps, L"msedge.exe", L"Microsoft Edge");
        AddCandidateIfMissing(apps, L"firefox.exe", L"Firefox");
        AddCandidateIfMissing(apps, L"wechat.exe", L"WeChat");
        AddCandidateIfMissing(apps, L"qq.exe", L"QQ");
        AddCandidateIfMissing(apps, L"dingtalk.exe", L"DingTalk");
        AddCandidateIfMissing(apps, L"feishu.exe", L"飞书");
        AddCandidateIfMissing(apps, L"teams.exe", L"Teams");
        AddCandidateIfMissing(apps, L"code.exe", L"VS Code");
        AddCandidateIfMissing(apps, L"cursor.exe", L"Cursor");
        AddCandidateIfMissing(apps, L"devenv.exe", L"Visual Studio");
        AddCandidateIfMissing(apps, L"idea64.exe", L"IntelliJ IDEA");
        AddCandidateIfMissing(apps, L"pycharm64.exe", L"PyCharm");
        AddCandidateIfMissing(apps, L"explorer.exe", L"资源管理器");
        AddCandidateIfMissing(apps, L"windowsterminal.exe", L"Windows Terminal");
        AddCandidateIfMissing(apps, L"powershell.exe", L"PowerShell");
        AddCandidateIfMissing(apps, L"cmd.exe", L"命令提示符");
        AddCandidateIfMissing(apps, L"notepad.exe", L"记事本");
        AddCandidateIfMissing(apps, L"excel.exe", L"Excel");
        AddCandidateIfMissing(apps, L"winword.exe", L"Word");
        AddCandidateIfMissing(apps, L"powerpnt.exe", L"PowerPoint");
        AddCandidateIfMissing(apps, L"figma.exe", L"Figma");
        AddCandidateIfMissing(apps, L"codex.exe", L"Codex");
    }

    inline AppCandidate CandidateFromStoredApp(const std::wstring& storedApp)
    {
        std::wstring exeName = FileNameOf(storedApp);
        if (exeName.empty())
            exeName = storedApp;

        bool looksLikePath = storedApp.find(L'\\') != std::wstring::npos || storedApp.find(L'/') != std::wstring::npos;
        return {
            looksLikePath ? storedApp : L"",
            exeName,
            L"",
            FriendlyNameForExe(exeName)
        };
    }

    inline BOOL CALLBACK CollectWindowProc(HWND hwnd, LPARAM lParam)
    {
        if (!IsWindowVisible(hwnd) || GetAncestor(hwnd, GA_ROOT) != hwnd)
            return TRUE;

        LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
        if ((exStyle & WS_EX_TOOLWINDOW) != 0)
            return TRUE;

        std::wstring title = GetWindowTextValue(hwnd);
        if (title.empty())
            return TRUE;

        AppIdentity identity = IdentifyWindow(hwnd);
        if (!identity.valid || identity.exePath.empty())
            return TRUE;

        auto* apps = reinterpret_cast<std::vector<AppCandidate>*>(lParam);
        if (!apps || IsDuplicateCandidate(*apps, identity.exePath, identity.exeName))
            return TRUE;

        apps->push_back({ identity.exePath, identity.exeName, title, FriendlyNameForWindow(identity) });
        return TRUE;
    }

    inline std::vector<AppCandidate> CollectForegroundApps()
    {
        std::vector<AppCandidate> apps;
        EnumWindows(CollectWindowProc, reinterpret_cast<LPARAM>(&apps));
        std::sort(apps.begin(), apps.end(), [](const AppCandidate& a, const AppCandidate& b) {
            return ToLower(a.displayName) < ToLower(b.displayName);
        });
        return apps;
    }

    inline std::vector<AppCandidate> CollectRunningApps()
    {
        std::vector<AppCandidate> apps = CollectForegroundApps();
        AddCommonCandidates(apps);
        std::sort(apps.begin(), apps.end(), [](const AppCandidate& a, const AppCandidate& b) {
            return ToLower(a.displayName) < ToLower(b.displayName);
        });
        return apps;
    }
}
