#define NOMINMAX
#include "QuickLauncherImportFlow.h"
#include "../Services/QuickLauncherConfigImport.h"
#include "../ViewModel/ConfigViewModel.h"
#include "../App/AppContext.h"
#include "WaitWindow.h"
#include "ConfirmWindow.h"
#include <commdlg.h>
#include <string>
#include <vector>

namespace QuickLauncherImportFlow
{
    bool Run(HWND hwndOwner, AppContext* appCtx, const std::wstring& configDir, ConfigViewModel* viewModel, const std::function<void()>& onReload)
    {
        if (!viewModel)
            return false;

        // 1. Open file dialog to select JSON file
        wchar_t filePath[MAX_PATH] = {};
        OPENFILENAMEW ofn = {};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = hwndOwner;
        ofn.lpstrFilter = L"JSON文件 (*.json)\0*.json\0全部文件 (*.*)\0*.*\0\0";
        ofn.lpstrFile = filePath;
        ofn.nMaxFile = MAX_PATH;
        ofn.lpstrTitle = L"选择 QuickLauncher 配置文件 (data.json)";
        ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;

        if (!GetOpenFileNameW(&ofn))
            return false;

        // 2. Import with waiting spinner (JSON parsing + icon copying on background thread)
        QuickLauncherConfigImport importer;
        IConfigImportService::ImportResult result;

        WaitWindow::Show(hwndOwner, L"请稍候", L"正在导入 QuickLauncher 配置，请稍候...",
            [&]() {
                result = importer.Import(filePath, configDir);
            }, appCtx);

        // 3. Handle errors with project-style UI
        if (!result.success)
        {
            ConfirmWindow::Show(hwndOwner, L"导入失败", result.errorMsg.c_str(), appCtx);
            return false;
        }

        if (result.pages.empty() && !result.hasAutoStartSetting)
        {
            ConfirmWindow::Show(hwndOwner, L"导入结果",
                L"JSON 文件中未找到可转换的快捷方式、快捷键或 URL。", appCtx);
            return false;
        }

        // 4. Count shortcuts BEFORE the move
        int totalShortcuts = 0;
        for (const auto& p : result.pages)
            totalShortcuts += (int)p.shortcuts.size();
        int totalPages = (int)result.pages.size();

        // 5. Ask merge strategy BEFORE the second wait (user interaction on main thread)
        bool replaceExisting = false;
        auto& viewPages = viewModel->GetPages();
        bool hasExisting = false;
        for (const auto& p : viewPages)
        {
            if (!p.shortcuts.empty())
            {
                hasExisting = true;
                break;
            }
        }

        if (hasExisting)
        {
            replaceExisting = ConfirmWindow::Show(hwndOwner, L"导入配置",
                L"检测到当前已有快捷方式，是否要清除并替换为导入的配置？\n点“确定”替换，点“取消”保留并追加。",
                appCtx);
        }

        // 6. Apply merge, settings and save inside WaitWindow
        WaitWindow::Show(hwndOwner, L"请稍候", L"正在应用导入的配置，请稍候...",
            [&]() {
                if (appCtx && appCtx->configService)
                {
                    if (result.hasAutoStartSetting)
                        appCtx->configService->SetAutoStart(result.autoStart);
                    if (result.popupColumns > 0)
                        appCtx->configService->SetPopupColumns(result.popupColumns);
                    if (result.popupRows > 0)
                        appCtx->configService->SetPopupRows(result.popupRows);
                    if (result.dockHeight >= 0)
                        appCtx->configService->SetDockHeight(result.dockHeight);
                    if (result.popupIconSize > 0)
                        appCtx->configService->SetPopupIconSize(result.popupIconSize);
                    if (result.globalScalePercent > 0)
                        appCtx->configService->SetGlobalScalePercent(result.globalScalePercent);
                    if (result.theme >= 0)
                        appCtx->configService->SetTheme(result.theme);
                    if (result.sortMode >= 0)
                        appCtx->configService->SetSortMode(result.sortMode);
                    if (result.popupAlignMode >= 0)
                        appCtx->configService->SetPopupAlignMode(result.popupAlignMode);
                    if (result.hasHideTrayIcon)
                        appCtx->configService->SetHideTrayIcon(result.hideTrayIcon);
                    if (result.hasHardwareAcceleration)
                        appCtx->configService->SetHardwareAccelerationEnabled(result.hardwareAcceleration);
                    if (result.hasSearchMode)
                        appCtx->configService->SetSearchMode(result.searchMode);
                    if (result.hasPopupAutoClose)
                        appCtx->configService->SetPopupAutoClose(result.popupAutoClose);
                    if (result.hasPopupMultiOpenWhenPinned)
                        appCtx->configService->SetPopupMultiOpenWhenPinned(result.popupMultiOpenWhenPinned);
                    if (result.hoverLeaveDelay >= 0)
                        appCtx->configService->SetHoverLeaveDelay(result.hoverLeaveDelay);
                }

                if (hasExisting && replaceExisting)
                    viewPages = std::move(result.pages);
                else if (hasExisting)
                {
                    for (auto& page : result.pages)
                    {
                        bool found = false;
                        for (auto& existingPage : viewPages)
                        {
                            if (existingPage.name == page.name)
                            {
                                for (auto& sc : page.shortcuts)
                                    existingPage.shortcuts.push_back(std::move(sc));
                                found = true;
                                break;
                            }
                        }
                        if (!found)
                            viewPages.push_back(std::move(page));
                    }
                }
                else
                    viewPages = std::move(result.pages);

                bool hasDock = false;
                for (const auto& p : viewPages)
                    if (p.name == L"DOCK") { hasDock = true; break; }
                if (!hasDock)
                {
                    Model::PopupPage dockPage;
                    dockPage.name = L"DOCK";
                    viewPages.insert(viewPages.begin(), std::move(dockPage));
                }

                viewModel->SaveConfig();
            }, appCtx);

        // 7. Reload and apply imported runtime settings on the UI thread.
        if (onReload)
        {
            onReload();
        }

        // 8. Show result with project-style UI
        ConfirmWindow::Show(hwndOwner, L"导入成功",
            (std::wstring(L"导入完成！共 ") +
                std::to_wstring(totalPages) + L" 个分组，" +
                std::to_wstring(totalShortcuts) + L" 个快捷方式，已保存 " +
                std::to_wstring(result.copiedIcons) + L" 个自定义图标" +
                (result.skippedItems > 0 ? L"；" + std::to_wstring(result.skippedItems) + L" 个已禁用或暂不兼容的项目未导入。" : L"。")).c_str(),
            appCtx);
        return true;
    }
}
