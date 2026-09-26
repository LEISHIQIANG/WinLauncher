#include "SettingsPluginActions.h"
#include "SettingsPage.h"
#include "SettingsPresetMenus.h"
#include "IConfigWindow.h"
#include "ConfirmWindow.h"
#include "PromptWindow.h"
#include "../App/AppContext.h"
#include "../App/PluginManager.h"
#include "../Services/ConfigPath.h"
#include <commdlg.h>
#include <vector>

bool SettingsPluginActions::IsPluginPackagePath(const std::wstring& filePath)
{
    size_t dot = filePath.find_last_of(L'.');
    if (dot == std::wstring::npos)
        return false;
    std::wstring ext = SettingsPresetMenus::ToLowerCopy(filePath.substr(dot));
    return ext == L".wlplugin" || ext == L".zip";
}

bool SettingsPluginActions::InstallPluginPackageFromPath(IConfigWindow* owner, const std::wstring& filePath, bool showSuccessMessage, std::wstring* errorMessage)
{
    auto appCtx = owner ? owner->GetAppContext() : nullptr;
    if (!appCtx || !appCtx->pluginManager)
        return false;

    HWND hwnd = owner ? owner->GetWindowHWND() : nullptr;
    std::wstring message;
    if (!appCtx->pluginManager->InstallPackage(filePath, message))
    {
        if (errorMessage)
        {
            *errorMessage = message;
        }
        else
        {
            ConfirmWindow::Show(hwnd, L"插件安装失败", message.c_str(), appCtx, false);
        }
        return false;
    }

    if (showSuccessMessage)
        ConfirmWindow::Show(hwnd, L"插件安装完成", message.c_str(), appCtx, false);
    return true;
}

bool SettingsPluginActions::HandleLButtonDown(SettingsPage* page, IConfigWindow* owner, POINT pt, bool& repaint)
{
    if (!page || !owner) return false;
    HWND hwnd = owner->GetWindowHWND();
    auto appCtx = owner->GetAppContext();
    if (!appCtx || !appCtx->pluginManager)
        return false;

    if (page->HitTestPluginInstall(pt))
    {
        wchar_t filePath[MAX_PATH] = {};
        OPENFILENAMEW ofn = {};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = hwnd;
        ofn.lpstrFilter = L"WinLauncher 插件包 (*.wlplugin)\0*.wlplugin\0ZIP 包 (*.zip)\0*.zip\0所有文件\0*.*\0";
        ofn.lpstrFile = filePath;
        ofn.nMaxFile = MAX_PATH;
        ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
        ofn.lpstrTitle = L"安装插件包";

        if (GetOpenFileNameW(&ofn))
        {
            InstallPluginPackageFromPath(owner, filePath, false);
            repaint = true;
        }
        return true;
    }
    else if (page->HitTestPluginOpenDir(pt))
    {
        ConfigPath::PrepareUserPluginInstalledDirectory();
        ShellExecuteW(hwnd, L"open", ConfigPath::GetUserPluginInstalledDirectory().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        repaint = true;
        return true;
    }
    else if (page->HitTestPluginRefresh(pt))
    {
        appCtx->pluginManager->Rescan();
        repaint = true;
        return true;
    }
    else
    {
        auto plugins = appCtx->pluginManager->GetPlugins();
        int configIdx = page->HitTestPluginConfigure(pt);
        if (configIdx >= 0 && configIdx < (int)plugins.size())
        {
            const auto& plugin = plugins[configIdx];
            auto settings = appCtx->pluginManager->GetPluginSettings(plugin.id);
            if (settings.empty())
            {
                ConfirmWindow::Show(hwnd, L"插件配置", L"该插件没有声明可编辑配置项。", appCtx, false);
                return true;
            }

            PluginSettingInfo setting = settings.front();
            if (settings.size() > 1)
            {
                std::vector<std::wstring> options;
                options.reserve(settings.size());
                for (const auto& item : settings)
                    options.push_back(item.title + L" (" + item.key + L")");

                std::wstring selected;
                if (!PromptWindow::ShowChoose(hwnd, L"插件配置", L"选择要编辑的配置项:", options, selected, appCtx))
                    return true;

                for (size_t i = 0; i < options.size(); ++i)
                {
                    if (options[i] == selected)
                    {
                        setting = settings[i];
                        break;
                    }
                }
            }

            std::wstring newValue = setting.currentValue.empty() ? setting.defaultValue : setting.currentValue;
            bool accepted = false;
            if (setting.type == L"boolean")
            {
                std::wstring selected;
                std::vector<std::wstring> booleanOptions = { L"true", L"false" };
                accepted = PromptWindow::ShowChoose(hwnd, setting.title.c_str(), L"选择配置值:", booleanOptions, selected, appCtx);
                if (accepted)
                    newValue = selected;
            }
            else
            {
                std::wstring prompt = setting.title + L"\r\nKey: " + setting.key;
                if (setting.type == L"integer")
                {
                    if (setting.hasMin)
                        prompt += L"\r\nMin: " + std::to_wstring(setting.minValue);
                    if (setting.hasMax)
                        prompt += L"\r\nMax: " + std::to_wstring(setting.maxValue);
                }
                accepted = PromptWindow::Show(hwnd, L"插件配置", prompt.c_str(), newValue, newValue.c_str(), appCtx);
            }

            if (accepted)
            {
                if (!appCtx->pluginManager->SetPluginSettingValue(plugin.id, setting.key, newValue))
                {
                    ConfirmWindow::Show(hwnd, L"插件配置失败", L"配置值无效或无法写入插件私有配置。", appCtx, false);
                }
                repaint = true;
            }
            return true;
        }
        else
        {
            int uninstallIdx = page->HitTestPluginUninstall(pt);
            if (uninstallIdx >= 0 && uninstallIdx < (int)plugins.size())
            {
                const auto& plugin = plugins[uninstallIdx];
                std::wstring prompt = L"确定要卸载插件 \"" + plugin.name + L"\" 吗？";
                if (ConfirmWindow::Show(hwnd, L"卸载插件", prompt.c_str(), appCtx, true))
                {
                    std::wstring message;
                    if (!appCtx->pluginManager->UninstallPlugin(plugin.id, message))
                    {
                        ConfirmWindow::Show(hwnd, L"插件卸载失败", message.c_str(), appCtx, false);
                    }
                    repaint = true;
                }
                return true;
            }
            else
            {
                int idx = page->HitTestPluginToggle(pt);
                if (idx >= 0 && idx < (int)plugins.size())
                {
                    std::wstring message;
                    bool ok = appCtx->pluginManager->SetPluginEnabled(plugins[idx].id, !plugins[idx].enabled);
                    if (!ok)
                    {
                        message = L"插件状态切换失败，请查看插件错误状态和日志。";
                        ConfirmWindow::Show(hwnd, L"插件操作失败", message.c_str(), appCtx, false);
                    }
                    repaint = true;
                    return true;
                }
            }
        }
    }
    return false;
}

void SettingsPluginActions::HandleDropFiles(SettingsPage* page, IConfigWindow* owner, HDROP hDrop, bool& repaint)
{
    if (!page || !owner || !hDrop)
        return;

    auto appCtx = owner->GetAppContext();
    if (!appCtx || !appCtx->pluginManager)
        return;

    UINT fileCount = DragQueryFileW(hDrop, 0xFFFFFFFF, nullptr, 0);
    int installedCount = 0;
    int skippedCount = 0;
    std::wstring failedMessages;

    for (UINT i = 0; i < fileCount; ++i)
    {
        wchar_t filePath[MAX_PATH]{};
        if (!DragQueryFileW(hDrop, i, filePath, MAX_PATH))
            continue;

        if (!IsPluginPackagePath(filePath))
        {
            skippedCount++;
            continue;
        }

        std::wstring errorMessage;
        if (InstallPluginPackageFromPath(owner, filePath, false, &errorMessage))
        {
            installedCount++;
        }
        else
        {
            if (!failedMessages.empty())
                failedMessages += L"\r\n";
            failedMessages += filePath;
            if (!errorMessage.empty())
                failedMessages += L": " + errorMessage;
        }
    }

    HWND hwnd = owner->GetWindowHWND();
    if (!failedMessages.empty())
    {
        std::wstring message = L"以下插件包安装失败，请检查插件包格式或错误日志:\r\n" + failedMessages;
        ConfirmWindow::Show(hwnd, L"插件安装失败", message.c_str(), appCtx, false);
    }
    else if (installedCount > 0)
    {
        std::wstring message = installedCount == 1
            ? L"插件已安装。"
            : (L"已安装 " + std::to_wstring(installedCount) + L" 个插件。");
        if (skippedCount > 0)
            message += L"\r\n已忽略非插件包文件。";
        ConfirmWindow::Show(hwnd, L"插件安装完成", message.c_str(), appCtx, false);
    }
    else if (skippedCount > 0)
    {
        ConfirmWindow::Show(hwnd, L"未找到插件包", L"请拖入 .wlplugin 插件包文件。", appCtx, false);
    }

    if (installedCount > 0)
        repaint = true;
}
