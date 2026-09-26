#include "PopupIconCache.h"

std::wstring PopupIconCache::Key(const RendShortcutInfo& shortcut)
{
    constexpr wchar_t Separator = L'\x1f';
    std::wstring key;
    key.reserve(shortcut.id.size() + shortcut.targetPath.size() +
        shortcut.iconPath.size() + shortcut.builtinIconId.size() + 32);
    key.append(shortcut.id);
    key.push_back(Separator);
    key.append(shortcut.targetPath);
    key.push_back(Separator);
    key.append(shortcut.iconPath);
    key.push_back(Separator);
    key.append(shortcut.builtinIconId);
    key.push_back(Separator);
    key.append(std::to_wstring(static_cast<int>(shortcut.type)));
    key.push_back(Separator);
    key.append(std::to_wstring(static_cast<int>(shortcut.iconSource)));
    return key;
}

void PopupIconCache::Remember(const RendShortcutInfo& shortcut)
{
    if (!shortcut.hIcon)
        return;

    HICON copy = CopyIcon(shortcut.hIcon);
    if (!copy)
        return;

    const std::wstring key = Key(shortcut);
    auto found = m_loaded.find(key);
    if (found != m_loaded.end())
    {
        if (found->second)
            DestroyIcon(found->second);
        found->second = copy;
        return;
    }

    constexpr size_t MaximumRememberedIcons = 512;
    if (m_loaded.size() >= MaximumRememberedIcons)
    {
        auto oldest = m_loaded.begin();
        if (oldest->second)
            DestroyIcon(oldest->second);
        m_loaded.erase(oldest);
    }
    m_loaded.emplace(key, copy);
}

HICON PopupIconCache::Copy(const RendShortcutInfo& shortcut) const
{
    const auto found = m_loaded.find(Key(shortcut));
    return found != m_loaded.end() && found->second
        ? CopyIcon(found->second)
        : nullptr;
}

void PopupIconCache::Preserve(const std::vector<RendPopupPage>& pages, const RendPopupPage& dockPage)
{
    for (const auto& page : pages)
        for (const auto& shortcut : page.shortcuts)
            Remember(shortcut);
    for (const auto& shortcut : dockPage.shortcuts)
        Remember(shortcut);
}

void PopupIconCache::Clear()
{
    for (auto& entry : m_loaded)
        if (entry.second) DestroyIcon(entry.second);
    m_loaded.clear();
}
