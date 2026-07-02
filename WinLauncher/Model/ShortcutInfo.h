#pragma once
#include <string>
#include <vector>

namespace Model
{
    enum class ShortcutType
    {
        File = 0,
        Hotkey = 1,
        Url = 2,
        Command = 3,
        Macro = 4,
        Batch = 5,
        BuiltinIcon = 6,
        System = 7
    };

    enum class ShortcutTargetKind
    {
        Unknown = 0,
        Link = 1,
        Exe = 2,
        Folder = 3,
        File = 4
    };

    enum class IconSource
    {
        Auto = 0,
        CustomPath = 1,
        Builtin = 2
    };

    enum class PageSceneMode
    {
        Whitelist = 0,
        Blacklist = 1
    };

    struct ShortcutInfo
    {
        std::wstring id;
        std::wstring name;
        std::wstring targetPath;
        std::wstring arguments;
        std::wstring iconPath;
        bool         runAsAdmin = false;
        ShortcutType type = ShortcutType::File;
        ShortcutTargetKind targetKind = ShortcutTargetKind::Unknown;
        IconSource   iconSource = IconSource::Auto;
        std::wstring builtinIconId;
        bool         iconInvertLight = false;
        bool         iconInvertDark = false;

        bool IsValid() const { return !targetPath.empty(); }
    };

    struct PopupPage
    {
        std::wstring name;
        std::vector<ShortcutInfo> shortcuts;
        bool isSyncFolder = false;
        std::wstring folderPath;
        PageSceneMode sceneMode = PageSceneMode::Whitelist;
        std::vector<std::wstring> sceneApps;
        std::vector<std::wstring> sceneAvailableApps;
    };

    inline const wchar_t* PageSceneModeKey(PageSceneMode mode)
    {
        switch (mode)
        {
        case PageSceneMode::Blacklist: return L"blacklist";
        default:                       return L"whitelist";
        }
    }

    inline PageSceneMode PageSceneModeFromKey(const std::wstring& key)
    {
        if (key == L"blacklist" || key == L"1") return PageSceneMode::Blacklist;
        return PageSceneMode::Whitelist;
    }

    inline const wchar_t* ShortcutTypeKey(ShortcutType type)
    {
        switch (type)
        {
        case ShortcutType::File:        return L"shortcut";
        case ShortcutType::Hotkey:      return L"hotkey";
        case ShortcutType::Url:         return L"url";
        case ShortcutType::Command:     return L"command";
        case ShortcutType::Macro:       return L"macro";
        case ShortcutType::Batch:       return L"batch";
        case ShortcutType::BuiltinIcon: return L"builtinIcon";
        case ShortcutType::System:      return L"system";
        default:                        return L"shortcut";
        }
    }

    inline const wchar_t* ShortcutTypeDisplayName(ShortcutType type)
    {
        switch (type)
        {
        case ShortcutType::File:        return L"快捷方式";
        case ShortcutType::Hotkey:      return L"快捷键";
        case ShortcutType::Url:         return L"URL";
        case ShortcutType::Command:     return L"命令";
        case ShortcutType::Macro:       return L"宏";
        case ShortcutType::Batch:       return L"批量启动";
        case ShortcutType::BuiltinIcon: return L"内置图标";
        case ShortcutType::System:      return L"系统图标";
        default:                        return L"快捷方式";
        }
    }

    inline ShortcutType ShortcutTypeFromKey(const std::wstring& key)
    {
        if (key == L"hotkey") return ShortcutType::Hotkey;
        if (key == L"url") return ShortcutType::Url;
        if (key == L"command") return ShortcutType::Command;
        if (key == L"macro") return ShortcutType::Macro;
        if (key == L"batch") return ShortcutType::Batch;
        if (key == L"builtinIcon") return ShortcutType::BuiltinIcon;
        if (key == L"system") return ShortcutType::System;
        return ShortcutType::File;
    }

    inline const wchar_t* ShortcutTargetKindKey(ShortcutTargetKind kind)
    {
        switch (kind)
        {
        case ShortcutTargetKind::Link:   return L"link";
        case ShortcutTargetKind::Exe:    return L"exe";
        case ShortcutTargetKind::Folder: return L"folder";
        case ShortcutTargetKind::File:   return L"file";
        default:                         return L"unknown";
        }
    }

    inline ShortcutTargetKind ShortcutTargetKindFromKey(const std::wstring& key)
    {
        if (key == L"link") return ShortcutTargetKind::Link;
        if (key == L"exe") return ShortcutTargetKind::Exe;
        if (key == L"folder") return ShortcutTargetKind::Folder;
        if (key == L"file") return ShortcutTargetKind::File;
        return ShortcutTargetKind::Unknown;
    }

    inline const wchar_t* IconSourceKey(IconSource source)
    {
        switch (source)
        {
        case IconSource::CustomPath: return L"customPath";
        case IconSource::Builtin:    return L"builtin";
        default:                     return L"auto";
        }
    }

    inline IconSource IconSourceFromKey(const std::wstring& key)
    {
        if (key == L"customPath") return IconSource::CustomPath;
        if (key == L"builtin") return IconSource::Builtin;
        return IconSource::Auto;
    }
}
