#define NOMINMAX
#include "ShortcutDialogController.h"
#include "ShortcutDialog.h"
#include "HotkeyDialog.h"
#include "UrlDialog.h"
#include "CommandDialog.h"
#include "MacroDialog.h"
#include "BatchLaunchDialog.h"
#include "BuiltinIconDialog.h"
#include "SystemIconDialog.h"
#include "../App/AppContext.h"
#include <string>
#include <vector>
#include <algorithm>

namespace ShortcutDialogController
{
    Model::IconSource ResolveEditedIconSource(const std::wstring& iconPath, const std::wstring& builtinIconId)
    {
        if (!iconPath.empty()) return Model::IconSource::CustomPath;
        return builtinIconId.empty() ? Model::IconSource::Auto : Model::IconSource::Builtin;
    }

    void ShowAddShortcut(const DialogHostContext& ctx)
    {
        if (!ctx.pageData || !ctx.owner || !ctx.hWnd) return;
        ShortcutDialogResult result;
        if (ShortcutDialog::Show(ctx.hWnd, L"添加快捷方式", result, nullptr, ctx.appCtx))
        {
            if (!result.targetPath.empty())
            {
                RendShortcutInfo sc;
                sc.name            = result.name;
                sc.targetPath      = result.targetPath;
                sc.arguments       = result.arguments;
                sc.iconPath        = result.iconPath;
                sc.runAsAdmin      = result.runAsAdmin;
                sc.iconInvertLight = result.iconInvertLight;
                sc.iconInvertDark  = result.iconInvertDark;
                sc.type            = Model::ShortcutType::File;
                sc.targetKind      = ShortcutManager::InferTargetKind(result.targetPath);
                sc.iconSource      = result.iconPath.empty() ? Model::IconSource::Auto : Model::IconSource::CustomPath;
                sc.hIcon           = ShortcutManager::GetShortcutIcon(sc, false, ctx.iconService);

                ctx.owner->RecordShortcutHistoryCheckpoint();
                ctx.pageData->shortcuts.push_back(sc);
                ID2D1Bitmap* bmp = ctx.createBitmap ? ctx.createBitmap(sc) : nullptr;
                ctx.pageData->iconBitmaps.push_back(bmp);

                if (ctx.notifyListChanged) ctx.notifyListChanged(false);
                ctx.owner->NotifyConfigChanged();
                InvalidateRect(ctx.hWnd, nullptr, FALSE);
            }
        }
    }

    void ShowAddHotkey(const DialogHostContext& ctx)
    {
        if (!ctx.pageData || !ctx.owner || !ctx.hWnd) return;
        HotkeyDialogResult result;
        if (HotkeyDialog::Show(ctx.hWnd, L"添加快捷键", result, nullptr, ctx.appCtx))
        {
            RendShortcutInfo sc;
            sc.name        = result.name;
            sc.targetPath  = result.hotkey;
            sc.arguments   = L"";
            sc.iconPath    = result.iconPath;
            sc.runAsAdmin  = result.afterClose;
            sc.type        = Model::ShortcutType::Hotkey;
            sc.iconInvertLight = result.iconInvertLight;
            sc.iconInvertDark  = result.iconInvertDark;
            sc.iconSource  = result.iconPath.empty() ? Model::IconSource::Auto : Model::IconSource::CustomPath;
            sc.hIcon       = ShortcutManager::GetShortcutIcon(sc, false, ctx.iconService);

            ctx.owner->RecordShortcutHistoryCheckpoint();
            ctx.pageData->shortcuts.push_back(sc);
            ID2D1Bitmap* bmp = ctx.createBitmap ? ctx.createBitmap(sc) : nullptr;
            ctx.pageData->iconBitmaps.push_back(bmp);

            if (ctx.notifyListChanged) ctx.notifyListChanged(false);
            ctx.owner->NotifyConfigChanged();
            InvalidateRect(ctx.hWnd, nullptr, FALSE);
        }
    }

    void ShowAddUrl(const DialogHostContext& ctx)
    {
        if (!ctx.pageData || !ctx.owner || !ctx.hWnd) return;
        UrlDialogResult result;
        if (UrlDialog::Show(ctx.hWnd, L"添加打开网址", result, nullptr, ctx.appCtx))
        {
            RendShortcutInfo sc;
            sc.name        = result.name;
            sc.targetPath  = result.url;
            sc.arguments   = result.browserPath + L"|||" + result.browserArgs;
            sc.iconPath    = result.iconPath;
            sc.runAsAdmin  = false;
            sc.type        = Model::ShortcutType::Url;
            sc.iconInvertLight = result.iconInvertLight;
            sc.iconInvertDark  = result.iconInvertDark;
            sc.iconSource  = result.iconPath.empty() ? Model::IconSource::Auto : Model::IconSource::CustomPath;
            sc.hIcon       = ShortcutManager::GetShortcutIcon(sc, false, ctx.iconService);

            ctx.owner->RecordShortcutHistoryCheckpoint();
            ctx.pageData->shortcuts.push_back(sc);
            ID2D1Bitmap* bmp = ctx.createBitmap ? ctx.createBitmap(sc) : nullptr;
            ctx.pageData->iconBitmaps.push_back(bmp);

            if (ctx.notifyListChanged) ctx.notifyListChanged(false);
            ctx.owner->NotifyConfigChanged();
            InvalidateRect(ctx.hWnd, nullptr, FALSE);
        }
    }

    void ShowAddCommand(const DialogHostContext& ctx)
    {
        if (!ctx.pageData || !ctx.owner || !ctx.hWnd) return;
        CommandDialogResult result;
        if (CommandDialog::Show(ctx.hWnd, L"添加运行命令", result, nullptr, ctx.appCtx))
        {
            RendShortcutInfo sc;
            sc.name        = result.name;
            sc.targetPath  = result.command;
            sc.arguments   = result.commandType + L"||||||" +
                             std::to_wstring((result.showWindow && !result.captureOutput) ? 1 : 0) + L"|||" +
                             std::to_wstring(result.captureOutput ? 1 : 0) + L"|||" + 
                             std::to_wstring(result.timeoutSeconds) + L"|||" + 
                             std::to_wstring(result.maxChars);
            sc.iconPath    = result.iconPath;
            sc.runAsAdmin  = result.runAsAdmin;
            sc.type        = Model::ShortcutType::Command;
            sc.iconInvertLight = result.iconInvertLight;
            sc.iconInvertDark  = result.iconInvertDark;
            sc.iconSource  = result.iconPath.empty() ? Model::IconSource::Auto : Model::IconSource::CustomPath;
            sc.hIcon       = ShortcutManager::GetShortcutIcon(sc, false, ctx.iconService);

            ctx.owner->RecordShortcutHistoryCheckpoint();
            ctx.pageData->shortcuts.push_back(sc);
            ID2D1Bitmap* bmp = ctx.createBitmap ? ctx.createBitmap(sc) : nullptr;
            ctx.pageData->iconBitmaps.push_back(bmp);

            if (ctx.notifyListChanged) ctx.notifyListChanged(false);
            ctx.owner->NotifyConfigChanged();
            InvalidateRect(ctx.hWnd, nullptr, FALSE);
        }
    }

    void ShowAddMacro(const DialogHostContext& ctx)
    {
        if (!ctx.pageData || !ctx.owner || !ctx.hWnd) return;
        MacroDialogResult result;
        if (MacroDialog::Show(ctx.hWnd, L"添加宏", result, nullptr, ctx.appCtx))
        {
            RendShortcutInfo sc;
            sc.name        = result.name;
            sc.targetPath  = L"";
            sc.arguments   = result.arguments;
            sc.iconPath    = result.iconPath;
            sc.runAsAdmin  = false;
            sc.type        = Model::ShortcutType::Macro;
            sc.iconInvertLight = result.iconInvertLight;
            sc.iconInvertDark  = result.iconInvertDark;
            sc.iconSource  = result.iconPath.empty() ? Model::IconSource::Auto : Model::IconSource::CustomPath;
            sc.hIcon       = ShortcutManager::GetShortcutIcon(sc, false, ctx.iconService);

            ctx.owner->RecordShortcutHistoryCheckpoint();
            ctx.pageData->shortcuts.push_back(sc);
            ID2D1Bitmap* bmp = ctx.createBitmap ? ctx.createBitmap(sc) : nullptr;
            ctx.pageData->iconBitmaps.push_back(bmp);

            if (ctx.notifyListChanged) ctx.notifyListChanged(false);
            ctx.owner->NotifyConfigChanged();
            InvalidateRect(ctx.hWnd, nullptr, FALSE);
        }
    }

    void ShowAddBatch(const DialogHostContext& ctx)
    {
        if (!ctx.pageData || !ctx.owner || !ctx.hWnd) return;
        BatchLaunchDialogResult result;
        if (BatchLaunchDialog::Show(ctx.hWnd, L"添加批量启动", result, nullptr, ctx.appCtx))
        {
            RendShortcutInfo sc;
            sc.name        = result.name;
            sc.targetPath  = L"";
            sc.arguments   = result.arguments;
            sc.iconPath    = result.iconPath;
            sc.runAsAdmin  = false;
            sc.type        = Model::ShortcutType::Batch;
            sc.iconInvertLight = result.iconInvertLight;
            sc.iconInvertDark  = result.iconInvertDark;
            sc.iconSource  = result.iconPath.empty() ? Model::IconSource::Auto : Model::IconSource::CustomPath;
            sc.hIcon       = ShortcutManager::GetShortcutIcon(sc, false, ctx.iconService);

            ctx.owner->RecordShortcutHistoryCheckpoint();
            ctx.pageData->shortcuts.push_back(sc);
            ID2D1Bitmap* bmp = ctx.createBitmap ? ctx.createBitmap(sc) : nullptr;
            ctx.pageData->iconBitmaps.push_back(bmp);

            if (ctx.notifyListChanged) ctx.notifyListChanged(false);
            ctx.owner->NotifyConfigChanged();
            InvalidateRect(ctx.hWnd, nullptr, FALSE);
        }
    }

    void ShowBuiltinIcon(const DialogHostContext& ctx)
    {
        if (!ctx.pageData || !ctx.owner || !ctx.hWnd) return;
        std::vector<RendShortcutInfo> results;
        if (BuiltinIconDialog::Show(ctx.hWnd, results, ctx.appCtx))
        {
            ctx.owner->RecordShortcutHistoryCheckpoint();
            for (auto& sc : results)
            {
                sc.hIcon = ShortcutManager::GetShortcutIcon(sc, false, ctx.iconService);

                ctx.pageData->shortcuts.push_back(sc);
                ID2D1Bitmap* bmp = ctx.createBitmap ? ctx.createBitmap(sc) : nullptr;
                ctx.pageData->iconBitmaps.push_back(bmp);
            }

            if (ctx.notifyListChanged) ctx.notifyListChanged(false);
            ctx.owner->NotifyConfigChanged();
            InvalidateRect(ctx.hWnd, nullptr, FALSE);
        }
    }

    void EditShortcut(const DialogHostContext& ctx, int index, bool& repaint)
    {
        if (!ctx.pageData || index < 0 || index >= (int)ctx.pageData->shortcuts.size() || !ctx.owner || !ctx.hWnd) return;

        RendShortcutInfo& sc = ctx.pageData->shortcuts[index];
        RendShortcutInfo originalSc = sc;
        bool changed = false;

        if (sc.type == Model::ShortcutType::Hotkey)
        {
            HotkeyDialog::InitParams init;
            init.name = sc.name;
            init.hotkey = sc.targetPath;
            init.iconPath = sc.iconPath;
            init.afterClose = sc.runAsAdmin;
            init.iconInvertLight = sc.iconInvertLight;
            init.iconInvertDark = sc.iconInvertDark;

            HotkeyDialogResult result;
            if (HotkeyDialog::Show(ctx.hWnd, L"编辑快捷键", result, &init, ctx.appCtx))
            {
                if (sc.name != result.name) { sc.name = result.name; changed = true; }
                if (sc.targetPath != result.hotkey) { sc.targetPath = result.hotkey; changed = true; }
                if (sc.iconPath != result.iconPath) { sc.iconPath = result.iconPath; changed = true; }
                Model::IconSource newIconSource = ResolveEditedIconSource(result.iconPath, sc.builtinIconId);
                if (sc.iconSource != newIconSource) { sc.iconSource = newIconSource; changed = true; }
                if (sc.runAsAdmin != result.afterClose) { sc.runAsAdmin = result.afterClose; changed = true; }
                if (sc.iconInvertLight != result.iconInvertLight) { sc.iconInvertLight = result.iconInvertLight; changed = true; }
                if (sc.iconInvertDark != result.iconInvertDark) { sc.iconInvertDark = result.iconInvertDark; changed = true; }
            }
        }
        else if (sc.type == Model::ShortcutType::Url)
        {
            UrlDialog::InitParams init;
            init.name = sc.name;
            init.url = sc.targetPath;
            
            std::wstring browserPath, browserArgs;
            size_t sep = sc.arguments.find(L"|||");
            if (sep != std::wstring::npos)
            {
                browserPath = sc.arguments.substr(0, sep);
                browserArgs = sc.arguments.substr(sep + 3);
            }
            else
            {
                browserPath = sc.arguments;
            }
            init.browserPath = browserPath;
            init.browserArgs = browserArgs;
            init.iconPath = sc.iconPath;
            init.iconInvertLight = sc.iconInvertLight;
            init.iconInvertDark = sc.iconInvertDark;

            UrlDialogResult result;
            if (UrlDialog::Show(ctx.hWnd, L"编辑打开网址", result, &init, ctx.appCtx))
            {
                if (sc.name != result.name) { sc.name = result.name; changed = true; }
                if (sc.targetPath != result.url) { sc.targetPath = result.url; changed = true; }
                
                std::wstring newArgs = result.browserPath + L"|||" + result.browserArgs;
                if (sc.arguments != newArgs) { sc.arguments = newArgs; changed = true; }
                
                if (sc.iconPath != result.iconPath) { sc.iconPath = result.iconPath; changed = true; }
                Model::IconSource newIconSource = ResolveEditedIconSource(result.iconPath, sc.builtinIconId);
                if (sc.iconSource != newIconSource) { sc.iconSource = newIconSource; changed = true; }
                if (sc.iconInvertLight != result.iconInvertLight) { sc.iconInvertLight = result.iconInvertLight; changed = true; }
                if (sc.iconInvertDark != result.iconInvertDark) { sc.iconInvertDark = result.iconInvertDark; changed = true; }
            }
        }
        else if (sc.type == Model::ShortcutType::Command)
        {
            CommandDialog::InitParams init;
            init.name = sc.name;
            init.command = sc.targetPath;
            init.iconPath = sc.iconPath;
            init.runAsAdmin = sc.runAsAdmin;
            init.iconInvertLight = sc.iconInvertLight;
            init.iconInvertDark = sc.iconInvertDark;

            std::vector<std::wstring> segments;
            std::wstring s = sc.arguments;
            size_t pos = 0;
            while ((pos = s.find(L"|||")) != std::wstring::npos)
            {
                segments.push_back(s.substr(0, pos));
                s.erase(0, pos + 3);
            }
            segments.push_back(s);
            
            std::wstring type = L"cmd";
            bool showWindow = false, captureOutput = false;
            int timeout = 300, maxChars = 50000;
            
            if (segments.size() > 0) type = segments[0];
            if (segments.size() > 2) showWindow = (segments[2] == L"1");
            if (segments.size() > 3) captureOutput = (segments[3] == L"1");
            if (captureOutput) showWindow = false;
            if (segments.size() > 4) { try { timeout = std::stoi(segments[4]); } catch(...) {} }
            if (segments.size() > 5) { try { maxChars = std::stoi(segments[5]); } catch(...) {} }

            init.commandType = type;
            init.showWindow = showWindow;
            init.captureOutput = captureOutput;
            init.timeoutSeconds = timeout;
            init.maxChars = maxChars;

            CommandDialogResult result;
            if (CommandDialog::Show(ctx.hWnd, L"编辑运行命令", result, &init, ctx.appCtx))
            {
                if (sc.name != result.name) { sc.name = result.name; changed = true; }
                if (sc.targetPath != result.command) { sc.targetPath = result.command; changed = true; }
                if (sc.iconPath != result.iconPath) { sc.iconPath = result.iconPath; changed = true; }
                Model::IconSource newIconSource = ResolveEditedIconSource(result.iconPath, sc.builtinIconId);
                if (sc.iconSource != newIconSource) { sc.iconSource = newIconSource; changed = true; }
                if (sc.runAsAdmin != result.runAsAdmin) { sc.runAsAdmin = result.runAsAdmin; changed = true; }
                if (sc.iconInvertLight != result.iconInvertLight) { sc.iconInvertLight = result.iconInvertLight; changed = true; }
                if (sc.iconInvertDark != result.iconInvertDark) { sc.iconInvertDark = result.iconInvertDark; changed = true; }
                
                std::wstring newArgs = result.commandType + L"||||||" +
                                       std::to_wstring((result.showWindow && !result.captureOutput) ? 1 : 0) + L"|||" +
                                       std::to_wstring(result.captureOutput ? 1 : 0) + L"|||" + 
                                       std::to_wstring(result.timeoutSeconds) + L"|||" + 
                                       std::to_wstring(result.maxChars);
                if (sc.arguments != newArgs) { sc.arguments = newArgs; changed = true; }
            }
        }
        else if (sc.type == Model::ShortcutType::System)
        {
            SystemIconDialog::InitParams init;
            init.name = sc.name;
            init.targetPath = sc.targetPath;
            init.iconPath = sc.iconPath;
            init.targetKind = sc.targetKind;
            init.iconSource = sc.iconSource;
            init.builtinIconId = sc.builtinIconId;
            init.iconInvertLight = sc.iconInvertLight;
            init.iconInvertDark = sc.iconInvertDark;

            SystemIconDialogResult result;
            if (SystemIconDialog::Show(ctx.hWnd, L"编辑系统图标", result, &init, ctx.appCtx))
            {
                if (sc.name != result.name) { sc.name = result.name; changed = true; }
                if (sc.iconPath != result.iconPath) { sc.iconPath = result.iconPath; changed = true; }
                Model::IconSource newIconSource = ResolveEditedIconSource(result.iconPath, sc.builtinIconId);
                if (sc.iconSource != newIconSource) { sc.iconSource = newIconSource; changed = true; }
                if (sc.iconInvertLight != result.iconInvertLight) { sc.iconInvertLight = result.iconInvertLight; changed = true; }
                if (sc.iconInvertDark != result.iconInvertDark) { sc.iconInvertDark = result.iconInvertDark; changed = true; }
            }
        }
        else if (sc.type == Model::ShortcutType::Macro)
        {
            MacroDialog::InitParams init;
            init.name = sc.name;
            init.arguments = sc.arguments;
            init.iconPath = sc.iconPath;
            init.iconInvertLight = sc.iconInvertLight;
            init.iconInvertDark = sc.iconInvertDark;

            MacroDialogResult result;
            if (MacroDialog::Show(ctx.hWnd, L"编辑宏脚本", result, &init, ctx.appCtx))
            {
                if (sc.name != result.name) { sc.name = result.name; changed = true; }
                if (sc.arguments != result.arguments) { sc.arguments = result.arguments; changed = true; }
                if (sc.iconPath != result.iconPath) { sc.iconPath = result.iconPath; changed = true; }
                Model::IconSource newIconSource = ResolveEditedIconSource(result.iconPath, sc.builtinIconId);
                if (sc.iconSource != newIconSource) { sc.iconSource = newIconSource; changed = true; }
                if (sc.iconInvertLight != result.iconInvertLight) { sc.iconInvertLight = result.iconInvertLight; changed = true; }
                if (sc.iconInvertDark != result.iconInvertDark) { sc.iconInvertDark = result.iconInvertDark; changed = true; }
            }
        }
        else if (sc.type == Model::ShortcutType::Batch)
        {
            BatchLaunchDialog::InitParams init;
            init.name = sc.name;
            init.arguments = sc.arguments;
            init.iconPath = sc.iconPath;
            init.iconInvertLight = sc.iconInvertLight;
            init.iconInvertDark = sc.iconInvertDark;

            BatchLaunchDialogResult result;
            if (BatchLaunchDialog::Show(ctx.hWnd, L"编辑批量启动队列", result, &init, ctx.appCtx))
            {
                if (sc.name != result.name) { sc.name = result.name; changed = true; }
                if (sc.arguments != result.arguments) { sc.arguments = result.arguments; changed = true; }
                if (sc.iconPath != result.iconPath) { sc.iconPath = result.iconPath; changed = true; }
                Model::IconSource newIconSource = ResolveEditedIconSource(result.iconPath, sc.builtinIconId);
                if (sc.iconSource != newIconSource) { sc.iconSource = newIconSource; changed = true; }
                if (sc.iconInvertLight != result.iconInvertLight) { sc.iconInvertLight = result.iconInvertLight; changed = true; }
                if (sc.iconInvertDark != result.iconInvertDark) { sc.iconInvertDark = result.iconInvertDark; changed = true; }
            }
        }
        else
        {
            ShortcutDialog::InitParams init;
            init.name            = sc.name;
            init.targetPath      = sc.targetPath;
            init.arguments       = sc.arguments;
            init.iconPath        = sc.iconPath;
            init.runAsAdmin      = sc.runAsAdmin;
            init.iconInvertLight = sc.iconInvertLight;
            init.iconInvertDark  = sc.iconInvertDark;

            ShortcutDialogResult result;
            if (ShortcutDialog::Show(ctx.hWnd, L"编辑快捷方式", result, &init, ctx.appCtx))
            {
                if (sc.name != result.name) { sc.name = result.name; changed = true; }
                if (sc.arguments != result.arguments) { sc.arguments = result.arguments; changed = true; }
                if (sc.iconPath != result.iconPath) { sc.iconPath = result.iconPath; changed = true; }
                Model::IconSource newIconSource = ResolveEditedIconSource(result.iconPath, sc.builtinIconId);
                if (sc.iconSource != newIconSource) { sc.iconSource = newIconSource; changed = true; }
                if (sc.runAsAdmin != result.runAsAdmin) { sc.runAsAdmin = result.runAsAdmin; changed = true; }
                if (sc.targetPath != result.targetPath) { sc.targetPath = result.targetPath; changed = true; }
                Model::ShortcutTargetKind newTargetKind = ShortcutManager::InferTargetKind(result.targetPath);
                if (sc.targetKind != newTargetKind) { sc.targetKind = newTargetKind; changed = true; }
                if (sc.iconInvertLight != result.iconInvertLight) { sc.iconInvertLight = result.iconInvertLight; changed = true; }
                if (sc.iconInvertDark != result.iconInvertDark) { sc.iconInvertDark = result.iconInvertDark; changed = true; }
            }
        }

        if (changed)
        {
            HICON currentIcon = sc.hIcon;
            RendShortcutInfo changedSc = sc;
            sc = originalSc;
            sc.hIcon = currentIcon;
            ctx.owner->RecordShortcutHistoryCheckpoint();
            sc = changedSc;
            sc.hIcon = currentIcon;

            if (sc.hIcon) { DestroyIcon(sc.hIcon); sc.hIcon = nullptr; }
            sc.hIcon = ShortcutManager::GetShortcutIcon(sc, false, ctx.iconService);

            if (index < (int)ctx.pageData->iconBitmaps.size() && ctx.pageData->iconBitmaps[index])
            {
                ctx.pageData->iconBitmaps[index]->Release();
                ctx.pageData->iconBitmaps[index] = nullptr;
            }
            if (index < (int)ctx.pageData->iconBitmaps.size())
            {
                ctx.pageData->iconBitmaps[index] = ctx.createBitmap ? ctx.createBitmap(sc) : nullptr;
            }

            ctx.owner->NotifyConfigChanged();
            repaint = true;
        }
    }
}
