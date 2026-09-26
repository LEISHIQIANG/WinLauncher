#pragma once

#include "../ShortcutManager.h"
#include <functional>
#include <string>
#include <vector>

struct AppContext;

// Launch engine for popup shortcuts: hotkey replay, URL, command, built-in
// system actions, macros and batch launches. PopupWindow keeps the trigger
// side (click routing, file-selection capture, hide and usage bookkeeping)
// and supplies the window-side state through LaunchContext. Everything runs
// on the caller's thread except hotkey replay and captured command output,
// which are submitted to background tasks exactly as before.
namespace PopupShortcutLauncher
{
    // Window-side state the engine cannot own.
    struct LaunchContext
    {
        std::vector<std::wstring> selectedFiles;                       // selection consumed at trigger time
        std::function<std::vector<std::wstring>()> peekLiveSelection;  // live (still valid) selection for {{...}} expansion
        bool popupWillHide = false;                                    // source popup closes after this launch
        HWND restoreForegroundWnd = nullptr;                           // macro "after close" foreground restore target
    };

    bool HasLaunchAction(const RendShortcutInfo& shortcut);
    bool IsBackgroundExternalLaunch(const RendShortcutInfo& shortcut);
    bool Execute(const RendShortcutInfo& sc, HWND parent, AppContext* ctx, const LaunchContext& launch);
    void LaunchFromPopup(const RendShortcutInfo& sc, HWND popupHwnd, AppContext* ctx,
        const std::vector<std::wstring>& consumedFiles, bool selectionConsumed, const LaunchContext& launch);
}
