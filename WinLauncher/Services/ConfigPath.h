#pragma once

#include <Windows.h>
#include <shlobj.h>
#include <string>

#pragma comment(lib, "shell32.lib")

namespace ConfigPath
{
    inline bool EnsureDirectoryExists(const std::wstring& path)
    {
        if (path.empty()) return false;

        DWORD attrs = GetFileAttributesW(path.c_str());
        if (attrs != INVALID_FILE_ATTRIBUTES)
            return (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;

        int result = SHCreateDirectoryExW(nullptr, path.c_str(), nullptr);
        return result == ERROR_SUCCESS || result == ERROR_ALREADY_EXISTS || result == ERROR_FILE_EXISTS;
    }

    inline std::wstring GetUserRoamingDirectory()
    {
        PWSTR roamingPath = nullptr;
        HRESULT hr = SHGetKnownFolderPath(FOLDERID_RoamingAppData, KF_FLAG_CREATE, nullptr, &roamingPath);
        if (SUCCEEDED(hr) && roamingPath)
        {
            std::wstring result(roamingPath);
            CoTaskMemFree(roamingPath);
            return result;
        }

        wchar_t expanded[MAX_PATH]{};
        DWORD len = ExpandEnvironmentStringsW(L"%USERPROFILE%\\AppData\\Roaming", expanded, MAX_PATH);
        if (len > 0 && len < MAX_PATH && std::wstring(expanded).find(L'%') == std::wstring::npos)
            return expanded;

        wchar_t userName[MAX_PATH]{};
        len = GetEnvironmentVariableW(L"USERNAME", userName, MAX_PATH);
        if (len > 0 && len < MAX_PATH)
            return std::wstring(L"C:\\Users\\") + userName + L"\\AppData\\Roaming";

        return L"C:\\Users\\Default\\AppData\\Roaming";
    }

    inline std::wstring GetUserConfigDirectory()
    {
        return GetUserRoamingDirectory() + L"\\WinLauncher\\config";
    }

    inline std::wstring GetUserDataDirectory()
    {
        return GetUserRoamingDirectory() + L"\\WinLauncher";
    }

    inline std::wstring GetUserPluginDirectory()
    {
        return GetUserDataDirectory() + L"\\plugins";
    }

    inline std::wstring GetUserPluginInstalledDirectory()
    {
        return GetUserPluginDirectory() + L"\\installed";
    }

    inline std::wstring GetUserPluginStateDirectory()
    {
        return GetUserPluginDirectory() + L"\\state";
    }

    inline std::wstring GetUserPluginCacheDirectory()
    {
        return GetUserPluginDirectory() + L"\\cache";
    }

    inline std::wstring PrepareUserConfigDirectory()
    {
        std::wstring userConfigDir = GetUserConfigDirectory();
        EnsureDirectoryExists(userConfigDir);
        return userConfigDir;
    }

    inline std::wstring PrepareUserPluginDirectory()
    {
        std::wstring userDataDir = GetUserDataDirectory();
        EnsureDirectoryExists(userDataDir);

        std::wstring pluginDir = GetUserPluginDirectory();
        EnsureDirectoryExists(pluginDir);
        return pluginDir;
    }

    inline std::wstring PrepareUserPluginInstalledDirectory()
    {
        PrepareUserPluginDirectory();
        std::wstring installedDir = GetUserPluginInstalledDirectory();
        EnsureDirectoryExists(installedDir);
        return installedDir;
    }

    inline std::wstring PrepareUserPluginStateDirectory()
    {
        PrepareUserPluginDirectory();
        std::wstring stateDir = GetUserPluginStateDirectory();
        EnsureDirectoryExists(stateDir);
        return stateDir;
    }

    inline std::wstring PrepareUserPluginCacheDirectory()
    {
        PrepareUserPluginDirectory();
        std::wstring cacheDir = GetUserPluginCacheDirectory();
        EnsureDirectoryExists(cacheDir);
        return cacheDir;
    }
}
