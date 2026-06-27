#include "ShortcutManager.h"
#include "Services/ConfigPath.h"
#include "Services/SyncFolderService.h"
#include "resource.h"
#include <shlobj.h>

#include <shlwapi.h>
#include <commoncontrols.h>
#include <fstream>
#include <sstream>
#include <algorithm>

#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

static IImageList* GetSystemImageList()
{
    IImageList* list = nullptr;
    const int sizes[] = { SHIL_EXTRALARGE, SHIL_JUMBO, SHIL_LARGE };
    for (int size : sizes)
    {
        if (SUCCEEDED(SHGetImageList(size, IID_PPV_ARGS(&list))))
            return list;
    }
    return nullptr;
}

std::vector<RendShortcutInfo> ShortcutManager::LoadShortcuts(const std::wstring& configDir)
{
    std::vector<RendShortcutInfo> result;

    std::wstring searchPath = configDir + L"\\*.lnk";
    WIN32_FIND_DATAW ffd;
    HANDLE hFind = FindFirstFileW(searchPath.c_str(), &ffd);

    if (hFind == INVALID_HANDLE_VALUE)
        return result;

    do
    {
        if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            continue;

        std::wstring fullPath = configDir + L"\\" + ffd.cFileName;

        RendShortcutInfo info;
        info.name = ffd.cFileName;
        size_t dot = info.name.rfind(L'.');
        if (dot != std::wstring::npos)
            info.name = info.name.substr(0, dot);

        IShellLinkW* psl = nullptr;
        HRESULT hr = CoCreateInstance(CLSID_ShellLink, nullptr,
                                      CLSCTX_INPROC_SERVER,
                                      IID_IShellLinkW, (void**)&psl);
        if (SUCCEEDED(hr) && psl)
        {
            IPersistFile* ppf = nullptr;
            hr = psl->QueryInterface(IID_IPersistFile, (void**)&ppf);
            if (SUCCEEDED(hr) && ppf)
            {
                hr = ppf->Load(fullPath.c_str(), STGM_READ);
                if (SUCCEEDED(hr))
                {
                    wchar_t buf[MAX_PATH]{};
                    if (SUCCEEDED(psl->GetPath(buf, MAX_PATH, nullptr, SLGP_RAWPATH)))
                        info.targetPath = buf;
                    wchar_t args[4096]{};
                    if (SUCCEEDED(psl->GetArguments(args, 4096)))
                        info.arguments = args;
                }
                ppf->Release();
            }
            psl->Release();
        }

        info.hIcon = GetShortcutIcon(info.targetPath.empty() ? fullPath : info.targetPath);

        result.push_back(info);
    } while (FindNextFileW(hFind, &ffd));


    FindClose(hFind);
    return result;
}

void ShortcutManager::FreeShortcuts(std::vector<RendShortcutInfo>& shortcuts)
{
    for (auto& s : shortcuts)
    {
        if (s.hIcon)
        {
            DestroyIcon(s.hIcon);
            s.hIcon = nullptr;
        }
    }
    shortcuts.clear();
}

std::wstring ShortcutManager::FindConfigDir()
{
    return ConfigPath::PrepareUserConfigDirectory();
}

static std::wstring ReadFileToString(const std::wstring& path)
{
    std::ifstream fs(path, std::ios::binary);
    if (!fs) return L"";
    std::string bytes((std::istreambuf_iterator<char>(fs)), std::istreambuf_iterator<char>());
    if (bytes.empty()) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, bytes.c_str(), (int)bytes.size(), nullptr, 0);
    std::wstring wstr(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, bytes.c_str(), (int)bytes.size(), &wstr[0], len);
    return wstr;
}

static bool WriteStringToFile(const std::wstring& path, const std::wstring& wstr)
{
    std::ofstream fs(path, std::ios::binary | std::ios::trunc);
    if (!fs) return false;
    if (wstr.empty()) return true;
    int len = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), nullptr, 0, nullptr, nullptr);
    std::string bytes(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), &bytes[0], len, nullptr, nullptr);
    fs.write(bytes.data(), bytes.size());
    return true;
}


std::vector<RendPopupPage> ShortcutManager::LoadConfig(const std::wstring& configDir)
{
    std::wstring configFilePath = configDir + L"\\launcher_config.ini";

    // If config file doesn't exist, create a minimal default config in the fixed user directory.
    if (GetFileAttributesW(configFilePath.c_str()) == INVALID_FILE_ATTRIBUTES)
    {
        std::vector<RendPopupPage> pages;
        RendPopupPage dockPage;
        dockPage.name = L"DOCK";
        pages.push_back(dockPage);

        RendPopupPage commonPage;
        commonPage.name = L"常用";
        pages.push_back(commonPage);

        SaveConfig(configDir, pages);
        return pages;
    }

    std::wstring content = ReadFileToString(configFilePath);
    std::vector<RendPopupPage> pages;

    std::wstringstream wss(content);
    std::wstring line;
    RendPopupPage* currentPage = nullptr;



    while (std::getline(wss, line))
    {
        while (!line.empty() && (line.back() == L'\r' || line.back() == L'\n' || line.back() == L' ' || line.back() == L'\t'))
            line.pop_back();
        size_t start = 0;
        while (start < line.size() && (line[start] == L' ' || line[start] == L'\t'))
            start++;
        if (start > 0)
            line = line.substr(start);

        if (line.empty()) continue;

        if (line.front() == L'[' && line.back() == L']')
        {
            std::wstring sec = line.substr(1, line.size() - 2);
            if (sec.rfind(L"Page:", 0) == 0)
            {
                RendPopupPage page;
                page.name = sec.substr(5);
                page.isSyncFolder = false;
                page.folderPath = L"";
                pages.push_back(page);
                currentPage = &pages.back();
            }
        }
        else if (currentPage)
        {
            if (line.rfind(L"IsSyncFolder=", 0) == 0)
            {
                try { currentPage->isSyncFolder = (std::stoi(line.substr(13)) != 0); } catch (...) {}
            }
            else if (line.rfind(L"FolderPath=", 0) == 0)
            {
                currentPage->folderPath = line.substr(11);
            }
            else if (line.rfind(L"Shortcut=", 0) == 0)
            {
                std::wstring val = line.substr(9);
                size_t p1 = val.find(L'|');
                if (p1 != std::wstring::npos)
                {
                    std::wstring name = val.substr(0, p1);
                    std::wstring rest = val.substr(p1 + 1);
                    size_t p2 = rest.find(L'|');
                    std::wstring targetPath;
                    std::wstring arguments;
                    std::wstring iconPath;
                    if (p2 != std::wstring::npos)
                    {
                        targetPath = rest.substr(0, p2);
                        std::wstring rest2 = rest.substr(p2 + 1);
                        size_t p3 = rest2.find(L'|');
                        if (p3 != std::wstring::npos)
                        {
                            arguments = rest2.substr(0, p3);
                            iconPath = rest2.substr(p3 + 1);
                        }
                        else
                        {
                            arguments = rest2;
                        }
                    }
                    else
                    {
                        targetPath = rest;
                    }

                    RendShortcutInfo info;
                    info.name = name;
                    info.targetPath = targetPath;
                    info.arguments = arguments;
                    info.iconPath = iconPath;

                    info.hIcon = GetShortcutIcon(iconPath.empty() ? targetPath : iconPath);

                    currentPage->shortcuts.push_back(info);
                }
            }
        }
    }

    for (auto& page : pages)
    {
        if (page.isSyncFolder && !page.folderPath.empty())
        {
            std::vector<RendShortcutInfo> diskShortcuts = SyncFolderService::LoadRendShortcuts(page.folderPath);
            std::vector<RendShortcutInfo> reconciled;
            std::vector<bool> matched(diskShortcuts.size(), false);

            for (const auto& saved : page.shortcuts)
            {
                auto it = std::find_if(diskShortcuts.begin(), diskShortcuts.end(), [&](const RendShortcutInfo& disk) {
                    return _wcsicmp(disk.targetPath.c_str(), saved.targetPath.c_str()) == 0;
                });
                if (it != diskShortcuts.end())
                {
                    reconciled.push_back(*it);
                    matched[std::distance(diskShortcuts.begin(), it)] = true;
                }
            }

            for (size_t i = 0; i < diskShortcuts.size(); ++i)
            {
                if (!matched[i])
                {
                    reconciled.push_back(diskShortcuts[i]);
                }
            }

            page.shortcuts = std::move(reconciled);
        }
    }


    return pages;
}

void ShortcutManager::SaveConfig(const std::wstring& configDir, const std::vector<RendPopupPage>& pages)
{
    ConfigPath::EnsureDirectoryExists(configDir);
    std::wstring configFilePath = configDir + L"\\launcher_config.ini";
    std::wstring content;

    for (const auto& page : pages)
    {
        content += L"[Page:" + page.name + L"]\r\n";
        if (!page.folderPath.empty())
        {
            content += L"IsSyncFolder=" + std::to_wstring(page.isSyncFolder ? 1 : 0) + L"\r\n";
            content += L"FolderPath=" + page.folderPath + L"\r\n";
        }
        for (const auto& sc : page.shortcuts)
        {
            content += L"Shortcut=" + sc.name + L"|" + sc.targetPath + L"|" + sc.arguments + L"|" + sc.iconPath + L"\r\n";
        }
        content += L"\r\n";
    }

    WriteStringToFile(configFilePath, content);
}

HICON ShortcutManager::GetShortcutIcon(const std::wstring& targetPath)
{
    bool isDir = false;
    if (!targetPath.empty())
    {
        DWORD attr = GetFileAttributesW(targetPath.c_str());
        isDir = (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY));
    }

    if (isDir)
    {
        HICON hIcon = (HICON)LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_FOLDER_ICON), IMAGE_ICON, 48, 48, LR_DEFAULTCOLOR);
        if (hIcon)
            return hIcon;
    }

    HICON hIcon = nullptr;
    IImageList* sysImgList = GetSystemImageList();
    if (sysImgList)
    {
        SHFILEINFOW sfi{};
        SHGetFileInfoW(targetPath.c_str(), 0, &sfi, sizeof(sfi), SHGFI_SYSICONINDEX);
        if (sysImgList->GetIcon(sfi.iIcon, ILD_NORMAL, &hIcon) != S_OK)
            hIcon = nullptr;
        sysImgList->Release();
    }

    if (!hIcon)
    {
        SHFILEINFOW sfi{};
        if (SHGetFileInfoW(targetPath.c_str(), 0, &sfi, sizeof(sfi), SHGFI_ICON | SHGFI_LARGEICON))
            hIcon = sfi.hIcon;
    }

    return hIcon;
}
