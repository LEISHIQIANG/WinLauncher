#include "SettingsPluginView.h"
#include "IConfigWindow.h"
#include "SettingsControlKit.h"
#include "SettingsPageLayout.h"
#include "../App/AppContext.h"
#include "../App/PluginManager.h"
#include "../Services/ConfigPath.h"
#include "UIStyle.h"
#include <algorithm>
#include <string>
#include <vector>

void SettingsPluginView::RenderPluginSection(
    ID2D1HwndRenderTarget* rt,
    IConfigWindow* owner,
    IDWriteTextFormat* tfDefault,
    ID2D1SolidColorBrush* tbNormal,
    ID2D1SolidColorBrush* tbMuted,
    D2D1_COLOR_F baseClr,
    const SettingsPluginHoverState& hover)
{
    if (!rt || !owner || !tfDefault) return;

    using namespace SettingsPageLayout;

    ID2D1SolidColorBrush* cardBg = nullptr;
    rt->CreateSolidColorBrush(D2D1::ColorF(baseClr.r, baseClr.g, baseClr.b, 0.026f), &cardBg);
    ID2D1SolidColorBrush* cardBorder = nullptr;
    rt->CreateSolidColorBrush(D2D1::ColorF(baseClr.r, baseClr.g, baseClr.b, 0.08f), &cardBorder);

    auto appCtx = owner->GetAppContext();
    auto plugins = (appCtx && appCtx->pluginManager) ? appCtx->pluginManager->GetPlugins() : std::vector<PluginInfo>{};

    D2D1_RECT_F rootRect = D2D1::RectF(160.0f, 82.0f, CONTENT_RIGHT, 132.0f);
    D2D1_ROUNDED_RECT rootRounded = D2D1::RoundedRect(rootRect, 6.0f, 6.0f);
    if (cardBg) rt->FillRoundedRectangle(rootRounded, cardBg);
    if (cardBorder) rt->DrawRoundedRectangle(rootRounded, cardBorder, UIStyle::Metrics::ControlStroke());

    if (tbNormal && tbMuted)
    {
        rt->DrawTextW(L"安装目录", 4, tfDefault, D2D1::RectF(172, 92, 250, 112), tbMuted);
        std::wstring dirText = ConfigPath::GetUserPluginInstalledDirectory();
        rt->DrawTextW(dirText.c_str(), (UINT32)dirText.size(), tfDefault, D2D1::RectF(172, 112, 340, 130), tbNormal);
        std::wstring dropHint = L"可将 .wlplugin 文件直接拖入此页面安装";
        rt->DrawTextW(dropHint.c_str(), (UINT32)dropHint.size(), tfDefault, D2D1::RectF(172, 134, CONTENT_RIGHT, 150), tbMuted);
    }

    SettingsControlKit::DrawSmallButton(rt, tfDefault, baseClr, tbNormal, D2D1::RectF(348.0f, 96.0f, 396.0f, 122.0f), L"安装", hover.install, true);
    SettingsControlKit::DrawSmallButton(rt, tfDefault, baseClr, tbNormal, D2D1::RectF(402.0f, 96.0f, 450.0f, 122.0f), L"打开", hover.openDir, false);
    SettingsControlKit::DrawSmallButton(rt, tfDefault, baseClr, tbNormal, D2D1::RectF(456.0f, 96.0f, 504.0f, 122.0f), L"刷新", hover.refresh, false);

    if (plugins.empty())
    {
        if (tbMuted)
        {
            std::wstring emptyText = L"暂无已安装插件。将包含 plugin.json 和 DLL 的插件目录放入 installed 后刷新即可显示。";
            rt->DrawTextW(emptyText.c_str(), (UINT32)emptyText.size(),
                tfDefault, D2D1::RectF(160, 158, CONTENT_RIGHT, 210), tbMuted);
        }
    }
    else
    {
        size_t visibleCount = (std::min)(plugins.size(), (size_t)6);
        for (size_t i = 0; i < visibleCount; ++i)
        {
            const auto& plugin = plugins[i];
            float top = 152.0f + (float)i * 48.0f;
            D2D1_RECT_F rowRect = D2D1::RectF(160.0f, top, CONTENT_RIGHT, top + 38.0f);
            D2D1_ROUNDED_RECT rowRounded = D2D1::RoundedRect(rowRect, 6.0f, 6.0f);
            bool hovered = ((int)i == hover.configure) || ((int)i == hover.toggle) || ((int)i == hover.uninstall);

            ID2D1SolidColorBrush* rowBg = nullptr;
            rt->CreateSolidColorBrush(D2D1::ColorF(baseClr.r, baseClr.g, baseClr.b, hovered ? 0.06f : 0.022f), &rowBg);
            if (rowBg)
            {
                rt->FillRoundedRectangle(rowRounded, rowBg);
                rowBg->Release();
            }
            if (cardBorder) rt->DrawRoundedRectangle(rowRounded, cardBorder, UIStyle::Metrics::ControlStroke());

            if (tbNormal && tbMuted)
            {
                std::wstring title = plugin.name + (plugin.version.empty() ? L"" : (L"  v" + plugin.version));
                rt->DrawTextW(title.c_str(), (UINT32)title.size(), tfDefault, D2D1::RectF(172, top + 4, 340, top + 22), tbNormal);

                std::wstring status = plugin.statusText;
                if (!plugin.lastError.empty())
                    status += L" - " + plugin.lastError;
                else if (!plugin.permissionSummary.empty())
                    status += L" - " + plugin.permissionSummary;
                if (plugin.settingCount > 0)
                    status += L" - 配置项 " + std::to_wstring(plugin.settingCount);
                if (status.empty())
                    status = plugin.enabled ? L"已启用" : L"已禁用";
                rt->DrawTextW(status.c_str(), (UINT32)status.size(), tfDefault, D2D1::RectF(172, top + 22, 340, top + 39), tbMuted);
            }

            if (plugin.settingCount > 0)
            {
                D2D1_RECT_F configRect = D2D1::RectF(346.0f, top + 8.0f, 392.0f, top + 30.0f);
                D2D1_COLOR_F configBg = baseClr;
                configBg.a = ((int)i == hover.configure) ? 0.08f : 0.04f;
                ID2D1SolidColorBrush* configBgBrush = nullptr;
                rt->CreateSolidColorBrush(configBg, &configBgBrush);
                if (configBgBrush)
                {
                    rt->FillRoundedRectangle(D2D1::RoundedRect(configRect, 5.0f, 5.0f), configBgBrush);
                    configBgBrush->Release();
                }
                if (tbNormal)
                {
                    DWRITE_TEXT_ALIGNMENT old = tfDefault->GetTextAlignment();
                    tfDefault->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                    rt->DrawTextW(L"配置", 2, tfDefault, configRect, tbNormal);
                    tfDefault->SetTextAlignment(old);
                }
            }

            D2D1_RECT_F toggleRect = D2D1::RectF(398.0f, top + 8.0f, 450.0f, top + 30.0f);
            D2D1_COLOR_F toggleBg = plugin.enabled ? UIStyle::ThemeColor::Accent().d2d : baseClr;
            toggleBg.a = plugin.enabled ? (((int)i == hover.toggle) ? 0.32f : 0.22f) : (((int)i == hover.toggle) ? 0.08f : 0.04f);
            ID2D1SolidColorBrush* toggleBgBrush = nullptr;
            rt->CreateSolidColorBrush(toggleBg, &toggleBgBrush);
            if (toggleBgBrush)
            {
                rt->FillRoundedRectangle(D2D1::RoundedRect(toggleRect, 5.0f, 5.0f), toggleBgBrush);
                toggleBgBrush->Release();
            }
            if (tbNormal)
            {
                const wchar_t* label = plugin.enabled ? L"禁用" : L"启用";
                DWRITE_TEXT_ALIGNMENT old = tfDefault->GetTextAlignment();
                tfDefault->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                rt->DrawTextW(label, 2, tfDefault, toggleRect, tbNormal);
                tfDefault->SetTextAlignment(old);
            }

            D2D1_RECT_F uninstallRect = D2D1::RectF(456.0f, top + 8.0f, 502.0f, top + 30.0f);
            D2D1_COLOR_F removeBg = UIStyle::ThemeColor::DangerRed().d2d;
            removeBg.a = ((int)i == hover.uninstall) ? 0.24f : 0.12f;
            ID2D1SolidColorBrush* removeBgBrush = nullptr;
            rt->CreateSolidColorBrush(removeBg, &removeBgBrush);
            if (removeBgBrush)
            {
                rt->FillRoundedRectangle(D2D1::RoundedRect(uninstallRect, 5.0f, 5.0f), removeBgBrush);
                removeBgBrush->Release();
            }
            if (tbNormal)
            {
                DWRITE_TEXT_ALIGNMENT old = tfDefault->GetTextAlignment();
                tfDefault->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                rt->DrawTextW(L"卸载", 2, tfDefault, uninstallRect, tbNormal);
                tfDefault->SetTextAlignment(old);
            }
        }
    }

    if (cardBg) cardBg->Release();
    if (cardBorder) cardBorder->Release();
}
