#pragma once

#include "../ShortcutManager.h"
#include <string>
#include <unordered_map>
#include <vector>

// Owns the HICON copies remembered across popup page rebuilds so a refresh can
// reuse icons that were already loaded. Single owner, UI thread only.
// Key() is the stable shortcut identity that background icon-refresh jobs
// carry to detect results that no longer match the shortcut they were
// started for.
class PopupIconCache
{
public:
    static std::wstring Key(const RendShortcutInfo& shortcut);

    void Remember(const RendShortcutInfo& shortcut);
    HICON Copy(const RendShortcutInfo& shortcut) const;
    void Preserve(const std::vector<RendPopupPage>& pages, const RendPopupPage& dockPage);
    void Clear();

private:
    std::unordered_map<std::wstring, HICON> m_loaded;
};
