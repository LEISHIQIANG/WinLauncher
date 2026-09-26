#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d2d1.h>
#include <string>
#include <vector>
#include <functional>
#include "IConfigWindow.h"
#include "../ShortcutManager.h"
#include "../Contracts/IIconService.h"

struct AppContext;

namespace ShortcutDialogController
{
    struct DialogHostContext
    {
        HWND hWnd = nullptr;
        AppContext* appCtx = nullptr;
        RendPopupPage* pageData = nullptr;
        IConfigWindow* owner = nullptr;
        IIconService* iconService = nullptr;
        std::function<ID2D1Bitmap*(const RendShortcutInfo&)> createBitmap;
        std::function<void(bool)> notifyListChanged;
    };

    Model::IconSource ResolveEditedIconSource(const std::wstring& iconPath, const std::wstring& builtinIconId);

    void ShowAddShortcut(const DialogHostContext& ctx);
    void ShowAddHotkey(const DialogHostContext& ctx);
    void ShowAddUrl(const DialogHostContext& ctx);
    void ShowAddCommand(const DialogHostContext& ctx);
    void ShowAddMacro(const DialogHostContext& ctx);
    void ShowAddBatch(const DialogHostContext& ctx);
    void ShowBuiltinIcon(const DialogHostContext& ctx);
    void EditShortcut(const DialogHostContext& ctx, int index, bool& repaint);
}
