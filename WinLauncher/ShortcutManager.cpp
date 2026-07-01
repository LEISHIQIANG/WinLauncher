#include "ShortcutManager.h"
#include "Services/ConfigPath.h"
#include "Services/SyncFolderService.h"
#include "resource.h"
#include <shlobj.h>
#include <objbase.h>

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
    const int sizes[] = { SHIL_JUMBO, SHIL_EXTRALARGE, SHIL_LARGE };
    for (int size : sizes)
    {
        if (SUCCEEDED(SHGetImageList(size, IID_PPV_ARGS(&list))))
            return list;
    }
    return nullptr;
}

static std::wstring GenerateShortcutId()
{
    GUID guid;
    if (SUCCEEDED(CoCreateGuid(&guid)))
    {
        wchar_t buf[40]{};
        if (StringFromGUID2(guid, buf, 40) > 0)
        {
            return buf;
        }
    }
    return L"{" + std::to_wstring(GetTickCount64()) + L"-" + std::to_wstring(rand()) + L"}";
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

static HICON GetAssociatedIcon(const std::wstring& sampleName, DWORD attributes)
{
    SHFILEINFOW sfi{};
    if (SHGetFileInfoW(sampleName.c_str(), attributes, &sfi, sizeof(sfi),
        SHGFI_ICON | SHGFI_LARGEICON | SHGFI_USEFILEATTRIBUTES))
    {
        return sfi.hIcon;
    }
    return nullptr;
}

static std::wstring GetSystemFilePath(const wchar_t* fileName)
{
    wchar_t sysDir[MAX_PATH]{};
    if (GetSystemDirectoryW(sysDir, MAX_PATH) == 0)
        return L"";
    std::wstring path = sysDir;
    path += L"\\";
    path += fileName;
    return path;
}

static HICON GetBuiltinIconById(const std::wstring& iconId)
{
    if (iconId == L"folder")
        return (HICON)LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_FOLDER_ICON), IMAGE_ICON, 256, 256, LR_DEFAULTCOLOR);
    if (iconId == L"link")
        return GetAssociatedIcon(L"WinLauncher.lnk", FILE_ATTRIBUTE_NORMAL);
    if (iconId == L"exe")
        return GetAssociatedIcon(L"WinLauncher.exe", FILE_ATTRIBUTE_NORMAL);
    if (iconId == L"url")
        return GetAssociatedIcon(L"WinLauncher.url", FILE_ATTRIBUTE_NORMAL);
    if (iconId == L"command")
        return ShortcutManager::GetShortcutIcon(GetSystemFilePath(L"cmd.exe"));
    if (iconId == L"hotkey")
        return ShortcutManager::GetShortcutIcon(GetSystemFilePath(L"osk.exe"));
    if (iconId == L"macro")
        return ShortcutManager::GetShortcutIcon(GetSystemFilePath(L"wscript.exe"));
    if (iconId == L"batch")
        return GetAssociatedIcon(L"WinLauncher.bat", FILE_ATTRIBUTE_NORMAL);
    if (iconId == L"timezone_cn_la")
        return (HICON)LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_TIME_ICON), IMAGE_ICON, 256, 256, LR_DEFAULTCOLOR);

    return (HICON)LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, 256, 256, LR_DEFAULTCOLOR);
}

static HICON GetDefaultIconForType(Model::ShortcutType type, Model::ShortcutTargetKind targetKind)
{
    if (type == Model::ShortcutType::File)
    {
        switch (targetKind)
        {
        case Model::ShortcutTargetKind::Folder:
            return GetBuiltinIconById(L"folder");
        case Model::ShortcutTargetKind::Link:
            return GetBuiltinIconById(L"link");
        case Model::ShortcutTargetKind::Exe:
            return GetBuiltinIconById(L"exe");
        case Model::ShortcutTargetKind::File:
            return GetAssociatedIcon(L"WinLauncher.file", FILE_ATTRIBUTE_NORMAL);
        default:
            return GetBuiltinIconById(L"app");
        }
    }

    switch (type)
    {
    case Model::ShortcutType::Hotkey:
        return GetBuiltinIconById(L"hotkey");
    case Model::ShortcutType::Url:
        return GetBuiltinIconById(L"url");
    case Model::ShortcutType::Command:
        return GetBuiltinIconById(L"command");
    case Model::ShortcutType::Macro:
        return GetBuiltinIconById(L"macro");
    case Model::ShortcutType::Batch:
        return GetBuiltinIconById(L"batch");
    case Model::ShortcutType::BuiltinIcon:
        return GetBuiltinIconById(L"app");
    case Model::ShortcutType::System:
        return GetBuiltinIconById(L"app");
    default:
        return GetBuiltinIconById(L"app");
    }
}

static Model::IconSource NormalizeIconSource(Model::IconSource source, const std::wstring& iconPath, const std::wstring& builtinIconId)
{
    if (source == Model::IconSource::CustomPath && iconPath.empty())
        return Model::IconSource::Auto;
    if (source == Model::IconSource::Builtin && builtinIconId.empty())
        return Model::IconSource::Auto;
    return source;
}

static std::vector<std::wstring> Split(const std::wstring& s, wchar_t delim)
{
    std::vector<std::wstring> result;
    std::wstring current;
    for (wchar_t ch : s)
    {
        if (ch == delim)
        {
            result.push_back(current);
            current.clear();
        }
        else
        {
            current.push_back(ch);
        }
    }
    result.push_back(current);
    return result;
}

static std::wstring EscapeConfigValue(const std::wstring& value)
{
    std::wstring result;
    result.reserve(value.size());
    for (wchar_t ch : value)
    {
        switch (ch)
        {
        case L'\\': result += L"\\\\"; break;
        case L'\r': result += L"\\r"; break;
        case L'\n': result += L"\\n"; break;
        case L'\t': result += L"\\t"; break;
        default: result.push_back(ch); break;
        }
    }
    return result;
}

static std::wstring UnescapeConfigValue(const std::wstring& value)
{
    std::wstring result;
    result.reserve(value.size());
    bool escaping = false;
    for (wchar_t ch : value)
    {
        if (!escaping)
        {
            if (ch == L'\\')
            {
                escaping = true;
            }
            else
            {
                result.push_back(ch);
            }
            continue;
        }

        switch (ch)
        {
        case L'\\': result.push_back(L'\\'); break;
        case L'r': result.push_back(L'\r'); break;
        case L'n': result.push_back(L'\n'); break;
        case L't': result.push_back(L'\t'); break;
        default:
            result.push_back(L'\\');
            result.push_back(ch);
            break;
        }
        escaping = false;
    }
    if (escaping)
        result.push_back(L'\\');
    return result;
}

static void TrimConfigLine(std::wstring& line)
{
    while (!line.empty() && (line.back() == L'\r' || line.back() == L'\n' || line.back() == L' ' || line.back() == L'\t'))
        line.pop_back();
    size_t start = 0;
    while (start < line.size() && (line[start] == L' ' || line[start] == L'\t'))
        start++;
    if (start > 0)
        line = line.substr(start);
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
    RendShortcutInfo* currentItem = nullptr;
    int currentPageIndex = -1;
    int currentItemIndex = -1;

    while (std::getline(wss, line))
    {
        TrimConfigLine(line);

        if (line.empty()) continue;

        if (line.front() == L'[' && line.back() == L']')
        {
            std::wstring sec = line.substr(1, line.size() - 2);
            if (sec.rfind(L"Page:", 0) == 0)
            {
                currentPage = nullptr;
                currentItem = nullptr;
                currentPageIndex = -1;
                currentItemIndex = -1;
                try { currentPageIndex = std::stoi(sec.substr(5)); } catch (...) { currentPageIndex = -1; }
                if (currentPageIndex >= 0)
                {
                    if ((int)pages.size() <= currentPageIndex)
                        pages.resize(currentPageIndex + 1);
                    currentPage = &pages[currentPageIndex];
                }
            }
            else if (sec.rfind(L"Item:", 0) == 0)
            {
                currentPage = nullptr;
                currentItem = nullptr;
                currentPageIndex = -1;
                currentItemIndex = -1;
                std::wstring rest = sec.substr(5);
                size_t colon = rest.find(L':');
                if (colon != std::wstring::npos)
                {
                    try
                    {
                        currentPageIndex = std::stoi(rest.substr(0, colon));
                        currentItemIndex = std::stoi(rest.substr(colon + 1));
                    }
                    catch (...)
                    {
                        currentPageIndex = -1;
                        currentItemIndex = -1;
                    }
                }
                if (currentPageIndex >= 0 && currentItemIndex >= 0)
                {
                    if ((int)pages.size() <= currentPageIndex)
                        pages.resize(currentPageIndex + 1);
                    currentPage = &pages[currentPageIndex];
                    if ((int)currentPage->shortcuts.size() <= currentItemIndex)
                        currentPage->shortcuts.resize(currentItemIndex + 1);
                    currentItem = &currentPage->shortcuts[currentItemIndex];
                }
            }
        }
        else if (currentItem)
        {
            size_t eq = line.find(L'=');
            if (eq != std::wstring::npos)
            {
                std::wstring key = line.substr(0, eq);
                std::wstring val = UnescapeConfigValue(line.substr(eq + 1));
                TrimConfigLine(key); TrimConfigLine(val);

                if (key == L"Name") currentItem->name = val;
                else if (key == L"Id") currentItem->id = val;
                else if (key == L"Type") currentItem->type = Model::ShortcutTypeFromKey(val);
                else if (key == L"TargetKind") currentItem->targetKind = Model::ShortcutTargetKindFromKey(val);
                else if (key == L"TargetPath") currentItem->targetPath = val;
                else if (key == L"Arguments") currentItem->arguments = val;
                else if (key == L"RunAsAdmin") currentItem->runAsAdmin = (val == L"1");
                else if (key == L"IconSource") currentItem->iconSource = Model::IconSourceFromKey(val);
                else if (key == L"IconPath") currentItem->iconPath = val;
                else if (key == L"BuiltinIconId") currentItem->builtinIconId = val;
                else if (key == L"IconInvertLight") currentItem->iconInvertLight = (val == L"1");
                else if (key == L"IconInvertDark") currentItem->iconInvertDark = (val == L"1");
            }
        }
        else if (currentPage)
        {
            size_t eq = line.find(L'=');
            if (eq != std::wstring::npos)
            {
                std::wstring key = line.substr(0, eq);
                std::wstring val = UnescapeConfigValue(line.substr(eq + 1));
                TrimConfigLine(key); TrimConfigLine(val);

                if (key == L"Name") currentPage->name = val;
                else if (key == L"IsSyncFolder")
                {
                    currentPage->isSyncFolder = (val == L"1");
                }
                else if (key == L"FolderPath") currentPage->folderPath = val;
            }
        }
    }

    for (auto& page : pages)
    {
        for (auto& sc : page.shortcuts)
        {
            if (sc.id.empty())
                sc.id = GenerateShortcutId();
            if (sc.targetKind == Model::ShortcutTargetKind::Unknown)
                sc.targetKind = ShortcutManager::InferTargetKind(sc.targetPath);
            if (sc.iconSource == Model::IconSource::Auto && !sc.iconPath.empty())
                sc.iconSource = Model::IconSource::CustomPath;
            sc.hIcon = GetShortcutIcon(sc);
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
                    RendShortcutInfo disk = *it;
                    disk.id = saved.id;
                    reconciled.push_back(std::move(disk));
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

    int pageIndex = 0;
    for (const auto& page : pages)
    {
        content += L"[Page:" + std::to_wstring(pageIndex) + L"]\r\n";
        content += L"Name=" + EscapeConfigValue(page.name) + L"\r\n";
        content += L"ShortcutCount=" + std::to_wstring(page.shortcuts.size()) + L"\r\n";
        if (!page.folderPath.empty())
        {
            content += L"IsSyncFolder=" + std::to_wstring(page.isSyncFolder ? 1 : 0) + L"\r\n";
            content += L"FolderPath=" + EscapeConfigValue(page.folderPath) + L"\r\n";
        }
        content += L"\r\n";

        int shortcutIndex = 0;
        for (const auto& sc : page.shortcuts)
        {
            content += L"[Item:" + std::to_wstring(pageIndex) + L":" + std::to_wstring(shortcutIndex) + L"]\r\n";
            content += L"Id=" + EscapeConfigValue(sc.id.empty() ? GenerateShortcutId() : sc.id) + L"\r\n";
            content += L"Name=" + EscapeConfigValue(sc.name) + L"\r\n";
            content += L"Type=" + std::wstring(Model::ShortcutTypeKey(sc.type)) + L"\r\n";
            content += L"TargetKind=" + std::wstring(Model::ShortcutTargetKindKey(sc.targetKind)) + L"\r\n";
            content += L"TargetPath=" + EscapeConfigValue(sc.targetPath) + L"\r\n";
            content += L"Arguments=" + EscapeConfigValue(sc.arguments) + L"\r\n";
            content += L"RunAsAdmin=" + std::to_wstring(sc.runAsAdmin ? 1 : 0) + L"\r\n";
            content += L"IconSource=" + std::wstring(Model::IconSourceKey(sc.iconSource)) + L"\r\n";
            content += L"IconPath=" + EscapeConfigValue(sc.iconPath) + L"\r\n";
            content += L"BuiltinIconId=" + EscapeConfigValue(sc.builtinIconId) + L"\r\n";
            content += L"IconInvertLight=" + std::to_wstring(sc.iconInvertLight ? 1 : 0) + L"\r\n";
            content += L"IconInvertDark=" + std::to_wstring(sc.iconInvertDark ? 1 : 0) + L"\r\n\r\n";
            shortcutIndex++;
        }
        pageIndex++;
    }

    WriteStringToFile(configFilePath, content);
}

#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")

class GdiPlusManager
{
public:
    static void EnsureInitialized()
    {
        static GdiPlusManager instance;
    }
private:
    GdiPlusManager()
    {
        Gdiplus::GdiplusStartupInput si;
        Gdiplus::GdiplusStartup(&m_token, &si, nullptr);
    }
    ~GdiPlusManager()
    {
        Gdiplus::GdiplusShutdown(m_token);
    }
    ULONG_PTR m_token = 0;
};

static bool IsImageExtension(const std::wstring& path)
{
    const wchar_t* ext = PathFindExtensionW(path.c_str());
    if (ext && *ext)
    {
        return (_wcsicmp(ext, L".png") == 0 ||
                _wcsicmp(ext, L".jpg") == 0 ||
                _wcsicmp(ext, L".jpeg") == 0 ||
                _wcsicmp(ext, L".bmp") == 0 ||
                _wcsicmp(ext, L".gif") == 0 ||
                _wcsicmp(ext, L".ico") == 0);
    }
    return false;
}

static HICON LoadIconViaGdiplus(const std::wstring& path)
{
    GdiPlusManager::EnsureInitialized();
    Gdiplus::Bitmap* bmp = Gdiplus::Bitmap::FromFile(path.c_str());
    HICON hIcon = nullptr;
    if (bmp)
    {
        if (bmp->GetLastStatus() == Gdiplus::Ok)
        {
            bmp->GetHICON(&hIcon);
        }
        delete bmp;
    }
    return hIcon;
}

HICON ShortcutManager::GetShortcutIcon(const std::wstring& targetPath)
{
    if (targetPath.empty()) return nullptr;

    bool isDir = false;
    bool isFile = false;
    if (!targetPath.empty())
    {
        DWORD attr = GetFileAttributesW(targetPath.c_str());
        if (attr != INVALID_FILE_ATTRIBUTES)
        {
            isDir = (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
            isFile = !isDir;
        }
    }

    if (isDir)
    {
        HICON hIcon = (HICON)LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_FOLDER_ICON), IMAGE_ICON, 256, 256, LR_DEFAULTCOLOR);
        if (hIcon)
            return hIcon;
    }

    if (isFile && IsImageExtension(targetPath))
    {
        HICON hIcon = LoadIconViaGdiplus(targetPath);
        if (hIcon)
            return hIcon;
    }

    HICON hIcon = nullptr;

    const wchar_t* ext = PathFindExtensionW(targetPath.c_str());
    bool isExeOrDllOrIco = false;
    if (ext && *ext)
    {
        isExeOrDllOrIco = (_wcsicmp(ext, L".exe") == 0 ||
                           _wcsicmp(ext, L".dll") == 0 ||
                           _wcsicmp(ext, L".ico") == 0);
    }

    if (isExeOrDllOrIco)
    {
        UINT extracted = PrivateExtractIconsW(targetPath.c_str(), 0, 256, 256, &hIcon, nullptr, 1, LR_DEFAULTCOLOR);
        if (extracted > 0 && hIcon)
        {
            return hIcon;
        }
    }

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

Model::ShortcutTargetKind ShortcutManager::InferTargetKind(const std::wstring& path)
{
    if (path.empty())
        return Model::ShortcutTargetKind::Unknown;

    DWORD attr = GetFileAttributesW(path.c_str());
    if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY))
        return Model::ShortcutTargetKind::Folder;

    const wchar_t* ext = PathFindExtensionW(path.c_str());
    if (ext && *ext)
    {
        if (_wcsicmp(ext, L".lnk") == 0) return Model::ShortcutTargetKind::Link;
        if (_wcsicmp(ext, L".exe") == 0) return Model::ShortcutTargetKind::Exe;
        return Model::ShortcutTargetKind::File;
    }

    return Model::ShortcutTargetKind::Unknown;
}

HICON ShortcutManager::GetShortcutIcon(const RendShortcutInfo& shortcut)
{
    Model::IconSource source = NormalizeIconSource(shortcut.iconSource, shortcut.iconPath, shortcut.builtinIconId);
    if (source == Model::IconSource::CustomPath)
    {
        HICON hIcon = GetShortcutIcon(shortcut.iconPath);
        if (hIcon) return hIcon;
    }
    else if (source == Model::IconSource::Builtin)
    {
        HICON hIcon = GetBuiltinIconById(shortcut.builtinIconId);
        if (hIcon) return hIcon;
    }

    if (shortcut.type == Model::ShortcutType::System && source == Model::IconSource::Auto)
    {
        if (shortcut.targetPath == L":config_window")
        {
            HICON hIcon = (HICON)LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_SETTING_ICON), IMAGE_ICON, 256, 256, LR_DEFAULTCOLOR);
            if (hIcon) return hIcon;
        }
        else if (shortcut.targetPath == L":topmost_toggle")
        {
            HICON hIcon = (HICON)LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_UP_ARROW_ICON), IMAGE_ICON, 256, 256, LR_DEFAULTCOLOR);
            if (hIcon) return hIcon;
        }
    }

    if ((shortcut.type == Model::ShortcutType::File || shortcut.type == Model::ShortcutType::System) && !shortcut.targetPath.empty())
    {
        HICON hIcon = GetShortcutIcon(shortcut.targetPath);
        if (hIcon) return hIcon;
    }

    if (shortcut.type != Model::ShortcutType::File && shortcut.type != Model::ShortcutType::System)
    {
        return nullptr;
    }

    Model::ShortcutTargetKind kind = shortcut.targetKind;
    if (kind == Model::ShortcutTargetKind::Unknown)
        kind = InferTargetKind(shortcut.targetPath);
    return GetDefaultIconForType(shortcut.type, kind);
}

bool ShortcutManager::UsesGeneratedDefaultIcon(const RendShortcutInfo& shortcut)
{
    Model::IconSource source = NormalizeIconSource(shortcut.iconSource, shortcut.iconPath, shortcut.builtinIconId);
    if (source != Model::IconSource::Auto)
        return false;

    if (shortcut.type != Model::ShortcutType::File)
        return false;

    Model::ShortcutTargetKind kind = shortcut.targetKind;
    if (kind == Model::ShortcutTargetKind::Unknown)
        kind = InferTargetKind(shortcut.targetPath);

    return kind == Model::ShortcutTargetKind::File ||
           kind == Model::ShortcutTargetKind::Link ||
           kind == Model::ShortcutTargetKind::Unknown;
}

void ShortcutManager::RefreshShortcutIcon(RendShortcutInfo& shortcut)
{
    if (shortcut.hIcon)
    {
        DestroyIcon(shortcut.hIcon);
        shortcut.hIcon = nullptr;
    }
    shortcut.hIcon = GetShortcutIcon(shortcut);
}

HICON ShortcutManager::GetShortcutIcon(const Model::ShortcutInfo& shortcut)
{
    RendShortcutInfo renderInfo;
    renderInfo.id = shortcut.id;
    renderInfo.name = shortcut.name;
    renderInfo.targetPath = shortcut.targetPath;
    renderInfo.arguments = shortcut.arguments;
    renderInfo.iconPath = shortcut.iconPath;
    renderInfo.runAsAdmin = shortcut.runAsAdmin;
    renderInfo.type = shortcut.type;
    renderInfo.targetKind = shortcut.targetKind;
    renderInfo.iconSource = shortcut.iconSource;
    renderInfo.builtinIconId = shortcut.builtinIconId;
    renderInfo.iconInvertLight = shortcut.iconInvertLight;
    renderInfo.iconInvertDark = shortcut.iconInvertDark;
    return GetShortcutIcon(renderInfo);
}
