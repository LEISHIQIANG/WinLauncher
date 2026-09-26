#include "SettingsBackupView.h"
#include "IConfigWindow.h"
#include "SettingsControlKit.h"
#include "SettingsPageLayout.h"
#include "UIStyle.h"
#include "../Services/ConfigPath.h"
#include <string>

void SettingsBackupView::RenderBackupSection(
    ID2D1HwndRenderTarget* rt,
    IConfigWindow* owner,
    IDWriteTextFormat* tfDefault,
    ID2D1SolidColorBrush* tbNormal,
    ID2D1SolidColorBrush* tbMuted,
    D2D1_COLOR_F baseClr,
    const SettingsBackupHoverState& hover)
{
    if (!rt || !owner || !tfDefault) return;

    using namespace SettingsPageLayout;

    const D2D1_RECT_F pathCardRect = D2D1::RectF(CONTENT_LEFT, 82.0f, CONTENT_RIGHT, 154.0f);
    const D2D1_RECT_F dirLabelRect = D2D1::RectF(CONTENT_LEFT + 20.0f, 92.0f, CONTENT_RIGHT - 20.0f, 110.0f);
    const D2D1_RECT_F dirValueRect = D2D1::RectF(CONTENT_LEFT + 20.0f, 112.0f, CONTENT_RIGHT - 20.0f, 146.0f);
    const D2D1_RECT_F historyCardRect = D2D1::RectF(CONTENT_LEFT, 164.0f, CONTENT_RIGHT, 214.0f);
    const D2D1_RECT_F historyLabelRect = D2D1::RectF(CONTENT_LEFT + 20.0f, 172.0f, CONTENT_RIGHT - 20.0f, 190.0f);
    const D2D1_RECT_F historyValueRect = D2D1::RectF(CONTENT_LEFT + 20.0f, 192.0f, CONTENT_RIGHT - 20.0f, 210.0f);
    const D2D1_RECT_F openLogFileRect = TwoColumnRect(0, 226.0f);
    const D2D1_RECT_F backupRect = TwoColumnRect(1, 226.0f);
    const D2D1_RECT_F restoreRect = TwoColumnRect(0, 268.0f);
    const D2D1_RECT_F historyDirRect = TwoColumnRect(1, 268.0f);
    const D2D1_RECT_F diagnosticRect = TwoColumnRect(0, 310.0f);
    const D2D1_RECT_F exportMigrationRect = TwoColumnRect(1, 310.0f);
    const D2D1_RECT_F importMigrationRect = TwoColumnRect(0, 352.0f);
    const D2D1_RECT_F importJsonRect = TwoColumnRect(1, 352.0f);
    const D2D1_RECT_F clearUsageRect = TwoColumnRect(0, 394.0f);
    const D2D1_RECT_F clearCacheRect = TwoColumnRect(1, 394.0f);
    const D2D1_RECT_F clearConfigRect = TwoColumnRect(0, 436.0f);
    const D2D1_RECT_F clearHistoryRect = TwoColumnRect(1, 436.0f);

    ID2D1SolidColorBrush* cardBg = nullptr;
    rt->CreateSolidColorBrush(D2D1::ColorF(baseClr.r, baseClr.g, baseClr.b, 0.026f), &cardBg);
    ID2D1SolidColorBrush* cardBorder = nullptr;
    rt->CreateSolidColorBrush(D2D1::ColorF(baseClr.r, baseClr.g, baseClr.b, 0.065f), &cardBorder);

    D2D1_ROUNDED_RECT pathCard = D2D1::RoundedRect(pathCardRect, 6.0f, 6.0f);
    if (cardBg) rt->FillRoundedRectangle(pathCard, cardBg);
    if (cardBorder) rt->DrawRoundedRectangle(pathCard, cardBorder, UIStyle::Metrics::ControlStroke());
    D2D1_ROUNDED_RECT historyCard = D2D1::RoundedRect(historyCardRect, 6.0f, 6.0f);
    if (cardBg) rt->FillRoundedRectangle(historyCard, cardBg);
    if (cardBorder) rt->DrawRoundedRectangle(historyCard, cardBorder, UIStyle::Metrics::ControlStroke());

    if (tbNormal && tbMuted)
    {
        DWRITE_WORD_WRAPPING oldWrapping = tfDefault->GetWordWrapping();
        DWRITE_TEXT_ALIGNMENT oldAlignment = tfDefault->GetTextAlignment();
        tfDefault->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
        tfDefault->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);

        std::wstring dirLabel = L"配置文件夹";
        rt->DrawTextW(dirLabel.c_str(), (UINT32)dirLabel.size(), tfDefault, dirLabelRect, tbMuted);

        std::wstring configDir = ConfigPath::GetUserDataDirectory();
        ID2D1SolidColorBrush* textBrush = tbNormal;
        if (hover.configDirText)
        {
            rt->CreateSolidColorBrush(UIStyle::ThemeColor::Accent().d2d, &textBrush);
        }
        rt->DrawTextW(configDir.c_str(), (UINT32)configDir.size(), tfDefault, dirValueRect, textBrush);
        if (hover.configDirText && textBrush)
        {
            textBrush->Release();
        }

        tfDefault->SetWordWrapping(oldWrapping);
        tfDefault->SetTextAlignment(oldAlignment);
    }

    if (tbNormal && tbMuted)
    {
        DWRITE_TEXT_ALIGNMENT oldAlignment = tfDefault->GetTextAlignment();
        tfDefault->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        std::wstring historyLabel = L"配置历史";
        std::wstring historySummary = owner->GetConfigHistorySummary();
        rt->DrawTextW(historyLabel.c_str(), (UINT32)historyLabel.size(), tfDefault, historyLabelRect, tbMuted);
        rt->DrawTextW(historySummary.c_str(), (UINT32)historySummary.size(), tfDefault, historyValueRect, tbNormal);
        tfDefault->SetTextAlignment(oldAlignment);
    }

    SettingsControlKit::DrawActionButton(rt, tfDefault, baseClr, tbNormal, openLogFileRect, L"打开日志文件", hover.openLogFile, false);
    SettingsControlKit::DrawActionButton(rt, tfDefault, baseClr, tbNormal, backupRect, L"立即备份", hover.createConfigBackup, false);
    SettingsControlKit::DrawActionButton(rt, tfDefault, baseClr, tbNormal, restoreRect, L"回滚最近历史", hover.restoreConfigBackup, false);
    SettingsControlKit::DrawActionButton(rt, tfDefault, baseClr, tbNormal, historyDirRect, L"打开历史目录", hover.openConfigHistoryDir, false);
    SettingsControlKit::DrawActionButton(rt, tfDefault, baseClr, tbNormal, diagnosticRect, L"生成诊断包", hover.diagnosticPackage, false);
    SettingsControlKit::DrawActionButton(rt, tfDefault, baseClr, tbNormal, exportMigrationRect, L"导出迁移备份", hover.exportMigration, false);
    SettingsControlKit::DrawActionButton(rt, tfDefault, baseClr, tbNormal, importMigrationRect, L"导入迁移备份", hover.importMigration, false);
    SettingsControlKit::DrawActionButton(rt, tfDefault, baseClr, tbNormal, importJsonRect, L"导入 QuickLauncher", hover.importJson, false);
    SettingsControlKit::DrawActionButton(rt, tfDefault, baseClr, tbNormal, clearUsageRect, L"清除使用记录", hover.clearUsageHistory, true);
    SettingsControlKit::DrawActionButton(rt, tfDefault, baseClr, tbNormal, clearCacheRect, L"清理缓存", hover.clearCache, true);
    SettingsControlKit::DrawActionButton(rt, tfDefault, baseClr, tbNormal, clearConfigRect, L"清除配置", hover.clearConfig, true);
    SettingsControlKit::DrawActionButton(rt, tfDefault, baseClr, tbNormal, clearHistoryRect, L"清除历史", hover.clearConfigHistory, true);

    if (cardBg) cardBg->Release();
    if (cardBorder) cardBorder->Release();
}
