#include "SyncFolderService.h"
#include <Windows.h>
#include <algorithm>

std::vector<Model::ShortcutInfo> SyncFolderService::LoadShortcuts(const std::wstring& folderPath)
{
    std::vector<Model::ShortcutInfo> result;
    std::wstring searchPath = folderPath + L"\\*";
    WIN32_FIND_DATAW ffd;
    HANDLE hFind = FindFirstFileW(searchPath.c_str(), &ffd);
    if (hFind == INVALID_HANDLE_VALUE) return result;

    do
    {
        if (ShouldIgnoreFile(ffd))
            continue;

        std::wstring fullPath = folderPath + L"\\" + ffd.cFileName;
        Model::ShortcutInfo info;
        info.name = ffd.cFileName;

        if (!(ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
        {
            size_t dot = info.name.rfind(L'.');
            if (dot != std::wstring::npos)
                info.name = info.name.substr(0, dot);
        }

        info.targetPath = fullPath;
        info.arguments = L"";
        info.type = Model::ShortcutType::File;
        info.targetKind = ShortcutManager::InferTargetKind(fullPath);
        info.iconSource = Model::IconSource::Auto;
        result.push_back(std::move(info));
    } while (FindNextFileW(hFind, &ffd));
    FindClose(hFind);

    std::sort(result.begin(), result.end(), [](const Model::ShortcutInfo& a, const Model::ShortcutInfo& b) {
        return a.name < b.name;
    });

    return result;
}

std::vector<RendShortcutInfo> SyncFolderService::LoadRendShortcuts(const std::wstring& folderPath)
{
    std::vector<RendShortcutInfo> result;
    std::wstring searchPath = folderPath + L"\\*";
    WIN32_FIND_DATAW ffd;
    HANDLE hFind = FindFirstFileW(searchPath.c_str(), &ffd);
    if (hFind == INVALID_HANDLE_VALUE) return result;

    do
    {
        if (ShouldIgnoreFile(ffd))
            continue;

        std::wstring fullPath = folderPath + L"\\" + ffd.cFileName;
        RendShortcutInfo info;
        info.name = ffd.cFileName;

        if (!(ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
        {
            size_t dot = info.name.rfind(L'.');
            if (dot != std::wstring::npos)
                info.name = info.name.substr(0, dot);
        }

        info.targetPath = fullPath;
        info.arguments = L"";
        info.type = Model::ShortcutType::File;
        info.targetKind = ShortcutManager::InferTargetKind(fullPath);
        info.iconSource = Model::IconSource::Auto;
        info.hIcon = ShortcutManager::GetShortcutIcon(info);
        result.push_back(std::move(info));
    } while (FindNextFileW(hFind, &ffd));
    FindClose(hFind);

    std::sort(result.begin(), result.end(), [](const RendShortcutInfo& a, const RendShortcutInfo& b) {
        return a.name < b.name;
    });

    return result;
}

bool SyncFolderService::ShouldIgnoreFile(const WIN32_FIND_DATAW& ffd)
{
    if (wcscmp(ffd.cFileName, L".") == 0 || wcscmp(ffd.cFileName, L"..") == 0)
        return true;
    if (ffd.cFileName[0] == L'.')
        return true;
    if ((ffd.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) || (ffd.dwFileAttributes & FILE_ATTRIBUTE_SYSTEM))
        return true;
    std::wstring name = ffd.cFileName;
    if (name.size() >= 4 && _wcsicmp(name.c_str() + name.size() - 4, L".ini") == 0)
        return true;
    return false;
}
