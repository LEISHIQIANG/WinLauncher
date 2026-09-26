#include "SettingsAboutView.h"
#include "SettingsControlKit.h"
#include "SettingsPageLayout.h"
#include "UIStyle.h"
#include "../version.h"
#include <string>
#include <cwchar>

void SettingsAboutView::RenderAboutSection(
    ID2D1HwndRenderTarget* rt,
    IDWriteTextFormat* tfDefault,
    IDWriteTextFormat* tfTitle,
    ID2D1SolidColorBrush* tbNormal,
    ID2D1SolidColorBrush* tbMuted,
    D2D1_COLOR_F baseClr,
    bool hoveredOpenSourceUrl)
{
    if (!rt || !tfDefault || !tbNormal || !tbMuted) return;

    using namespace SettingsPageLayout;

    if (tfTitle)
        rt->DrawTextW(L"WinLauncher", 11, tfTitle, D2D1::RectF(160, 82, 510, 107), tbNormal);
    std::wstring verText = std::wstring(L"版本: v") + WINLAUNCHER_VERSION_WSTR;
    rt->DrawTextW(verText.c_str(), (UINT32)verText.size(), tfDefault, D2D1::RectF(160, 112, 510, 130), tbMuted);
    const wchar_t* tagline = L"原生 Windows 桌面启动器 · 快速、轻量、本地优先";
    rt->DrawTextW(tagline, (UINT32)wcslen(tagline), tfDefault,
        D2D1::RectF(160, 132, CONTENT_RIGHT, 150), tbMuted);

    SettingsControlKit::DrawInfoCard(rt, tfDefault, baseClr, tbNormal, tbMuted, TwoColumnRect(0, 164.0f, 68.0f), L"快速启动", L"通过鼠标手势或快捷键\n在光标处唤出快捷方式面板");
    SettingsControlKit::DrawInfoCard(rt, tfDefault, baseClr, tbNormal, tbMuted, TwoColumnRect(1, 164.0f, 68.0f), L"搜索与分类", L"分页管理常用项目，支持\n即时搜索、智能排序与场景筛选");
    SettingsControlKit::DrawInfoCard(rt, tfDefault, baseClr, tbNormal, tbMuted, TwoColumnRect(0, 242.0f, 68.0f), L"命令与自动化", L"运行自定义命令、批量启动与宏；\n可使用已选文件作为命令输入");
    SettingsControlKit::DrawInfoCard(rt, tfDefault, baseClr, tbNormal, tbMuted, TwoColumnRect(1, 242.0f, 68.0f), L"外观与扩展", L"可调主题、材质、布局与动画；\n支持 DLL 插件、/ 命令与搜索源");
    SettingsControlKit::DrawInfoCard(rt, tfDefault, baseClr, tbNormal, tbMuted, D2D1::RectF(CONTENT_LEFT, 320.0f, CONTENT_RIGHT, 400.0f), L"本地优先与诊断", L"配置、使用记录、日志和崩溃诊断均保留在本机，不会自动上传。\n可在“配置管理”中生成脱敏诊断包、创建备份或迁移到新设备。");

    const D2D1_RECT_F sourceLinkRect = AboutOpenSourceLinkRect();
    const D2D1_ROUNDED_RECT roundedSourceLink = D2D1::RoundedRect(sourceLinkRect, 5.0f, 5.0f);
    ID2D1SolidColorBrush* sourceBrush = nullptr;
    const D2D1_COLOR_F accent = UIStyle::ThemeColor::Accent().d2d;
    rt->CreateSolidColorBrush(D2D1::ColorF(accent.r, accent.g, accent.b, hoveredOpenSourceUrl ? 0.16f : 0.075f), &sourceBrush);
    if (sourceBrush)
    {
        rt->FillRoundedRectangle(roundedSourceLink, sourceBrush);
        sourceBrush->Release();
    }
    rt->CreateSolidColorBrush(D2D1::ColorF(accent.r, accent.g, accent.b, hoveredOpenSourceUrl ? 0.62f : 0.36f), &sourceBrush);
    if (sourceBrush)
    {
        rt->DrawRoundedRectangle(roundedSourceLink, sourceBrush, UIStyle::Metrics::ControlStroke());
        sourceBrush->Release();
    }
    const wchar_t* sourceLabel = L"开源地址  github.com/LEISHIQIANG/WinLauncher";
    rt->CreateSolidColorBrush(accent, &sourceBrush);
    if (sourceBrush)
    {
        tfDefault->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        rt->DrawTextW(sourceLabel, (UINT32)wcslen(sourceLabel), tfDefault, sourceLinkRect, sourceBrush);
        tfDefault->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        sourceBrush->Release();
    }
}
