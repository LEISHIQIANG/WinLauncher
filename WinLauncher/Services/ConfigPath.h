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

    inline std::wstring PrepareUserConfigDirectory()
    {
        std::wstring userConfigDir = GetUserConfigDirectory();
        EnsureDirectoryExists(userConfigDir);
        return userConfigDir;
    }
}
