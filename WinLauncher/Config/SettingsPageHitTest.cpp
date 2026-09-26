#include "SettingsPage.h"
#include "SettingsPageLayout.h"
#include "IConfigWindow.h"
#include "UIStyle.h"
#include <vector>

// Hit-test routing for the settings page, split from SettingsPage.cpp so the
// click/hover geometry lives next to the layout constants. All functions are
// SettingsPage members moved verbatim; signatures are unchanged.

using namespace SettingsPageLayout;

bool SettingsPage::HitTestAppearance(POINT pt, int& settingIdx, int& buttonType)
{
    if (m_categoryIndex != 1) return false;
    for (int i = 0; i < 9; i++)
    {
        int col = i % 2;
        int row = i / 2;
        D2D1_RECT_F cardRect = TwoColumnRect(col, 90.0f + row * 42.0f);
        float ix = cardRect.left;
        float iy = 90.0f + row * 42.0f;
        float cy = iy + 16.0f;

        if (PointInRect(cardRect, pt))
        {
            settingIdx = i;
            if (pt.x >= ix + 83 && pt.x <= ix + 103 && pt.y >= cy - 10 && pt.y <= cy + 10)
            {
                buttonType = 1; // minus
            }
            else if (pt.x >= ix + 127 && pt.x <= ix + 147 && pt.y >= cy - 10 && pt.y <= cy + 10)
            {
                buttonType = 2; // plus
            }
            else
            {
                buttonType = 0; // card body
            }
            return true;
        }
    }
    return false;
}

bool SettingsPage::HitTestAutoStart(POINT pt)
{
    if (m_categoryIndex != 0) return false;
    return (pt.x >= 160 && pt.x <= 240 && pt.y >= 80 && pt.y <= 110);
}

bool SettingsPage::HitTestHideTrayIcon(POINT pt)
{
    if (m_categoryIndex != 0) return false;
    return (pt.x >= 245 && pt.x <= 325 && pt.y >= 80 && pt.y <= 110);
}

bool SettingsPage::HitTestHardwareAcceleration(POINT pt)
{
    if (m_categoryIndex != 0) return false;
    return (pt.x >= 330 && pt.x <= 410 && pt.y >= 80 && pt.y <= 110);
}

int SettingsPage::HitTestTrigger(POINT pt)
{
    if (m_categoryIndex != 2) return -1;
    for (int i = 0; i <= TRIGGER_PRESET_BUTTON; i++)
    {
        D2D1_RECT_F rect = TriggerButtonRect(i);
        if (pt.x >= rect.left && pt.x <= rect.right && pt.y >= rect.top && pt.y <= rect.bottom)
        {
            return i;
        }
    }
    return -1;
}

int SettingsPage::HitTestPopupAlignMode(POINT pt)
{
    if (m_categoryIndex != 2) return -1;
    for (int i = 0; i <= POPUP_ALIGN_PRESET_BUTTON; i++)
    {
        if (PointInRect(PopupAlignRect(i), pt))
            return i;
    }
    return -1;
}

int SettingsPage::HitTestPopupAutoClose(POINT pt)
{
    if (m_categoryIndex != 2) return -1;
    if (pt.y >= 256.0f && pt.y <= 284.0f)
    {
        if (PointInRect(PopupBehaviorRect(0, 256.0f), pt)) return 0;
        if (PointInRect(PopupBehaviorRect(1, 256.0f), pt)) return 1;
    }
    return -1;
}

int SettingsPage::HitTestPopupMultiOpenWhenPinned(POINT pt)
{
    if (m_categoryIndex != 2) return -1;
    if (pt.y >= 296.0f && pt.y <= 324.0f)
    {
        if (PointInRect(PopupBehaviorRect(0, 296.0f), pt)) return 0;
        if (PointInRect(PopupBehaviorRect(1, 296.0f), pt)) return 1;
    }
    return -1;
}

int SettingsPage::HitTestSortMode(POINT pt)
{
    if (m_categoryIndex != 2) return -1;
    if (pt.y >= 336.0f && pt.y <= 364.0f)
    {
        if (PointInRect(PopupBehaviorRect(0, 336.0f), pt)) return 0;
        if (PointInRect(PopupBehaviorRect(1, 336.0f), pt)) return 1;
    }
    return -1;
}

bool SettingsPage::HitTestTriggerBlacklist(POINT pt)
{
    if (m_categoryIndex != 2) return false;
    return PointInRect(TriggerBlacklistRect(), pt);
}

bool SettingsPage::HitTestHoverLeaveDelay(POINT pt, int& buttonType)
{
    if (m_categoryIndex != 2) return false;
    D2D1_RECT_F cardRect = TwoColumnRect(0, 386.0f);
    float ix = cardRect.left;
    float iy = 386.0f;
    float cy = iy + 16.0f;

    if (PointInRect(cardRect, pt))
    {
        if (pt.x >= ix + 83 && pt.x <= ix + 103 && pt.y >= cy - 10 && pt.y <= cy + 10)
            buttonType = 1;
        else if (pt.x >= ix + 127 && pt.x <= ix + 147 && pt.y >= cy - 10 && pt.y <= cy + 10)
            buttonType = 2;
        else
            buttonType = 0;
        return true;
    }
    return false;
}

int SettingsPage::HitTestTheme(POINT pt)
{
    if (m_categoryIndex != 0) return -1;
    if (pt.y >= 180.0f + SYSTEM_SETTINGS_CONTENT_OFFSET && pt.y <= 212.0f + SYSTEM_SETTINGS_CONTENT_OFFSET)
    {
        if (pt.x >= 160.0f && pt.x <= 325.0f) return 0; // Dark
        if (pt.x >= 345.0f && pt.x <= 510.0f) return 1; // Light
    }
    return -1;
}

int SettingsPage::HitTestThemeColor(POINT pt)
{
    if (m_categoryIndex != 0) return -1;
    if (pt.y < 241.0f + SYSTEM_SETTINGS_CONTENT_OFFSET || pt.y > 265.0f + SYSTEM_SETTINGS_CONTENT_OFFSET) return -1;

    const float swatchLeft = 160.0f;
    const float swatchRight = 510.0f;
    const float swatchSize = 18.0f;
    const float swatchStep = (swatchRight - swatchLeft - swatchSize) / (float)(UIStyle::ThemeColorPresetCount() - 1);

    for (int i = 0; i < UIStyle::ThemeColorPresetCount(); i++)
    {
        float x = swatchLeft + i * swatchStep;
        if (pt.x >= x - 3.0f && pt.x <= x + swatchSize + 3.0f)
        {
            return i;
        }
    }

    return -1;
}

int SettingsPage::HitTestWindowMode(POINT pt)
{
    if (m_categoryIndex != 0) return -1;
    if (pt.y >= 298.0f + SYSTEM_SETTINGS_CONTENT_OFFSET && pt.y <= 326.0f + SYSTEM_SETTINGS_CONTENT_OFFSET)
    {
        for (int i = 0; i < 3; i++)
        {
            float xStart = 160.0f + i * 120.0f;
            if (pt.x >= xStart && pt.x <= xStart + 110.0f)
            {
                return i;
            }
        }
    }
    return -1;
}

bool SettingsPage::HitTestOpenLogFile(POINT pt)
{
    if (m_categoryIndex != 3) return false;
    return PointInRect(TwoColumnRect(0, 226.0f), pt);
}

bool SettingsPage::HitTestConfigDirText(POINT pt)
{
    if (m_categoryIndex != 3) return false;
    return (pt.x >= CONTENT_LEFT + 20.0f && pt.x <= CONTENT_RIGHT - 20.0f && pt.y >= 112 && pt.y <= 146);
}

bool SettingsPage::HitTestOpenConfigHistoryDir(POINT pt)
{
    if (m_categoryIndex != 3) return false;
    return PointInRect(TwoColumnRect(1, 268.0f), pt);
}

bool SettingsPage::HitTestCreateConfigBackup(POINT pt)
{
    if (m_categoryIndex != 3) return false;
    return PointInRect(TwoColumnRect(1, 226.0f), pt);
}

bool SettingsPage::HitTestRestoreConfigBackup(POINT pt)
{
    if (m_categoryIndex != 3) return false;
    return PointInRect(TwoColumnRect(0, 268.0f), pt);
}

bool SettingsPage::HitTestClearConfig(POINT pt)
{
    if (m_categoryIndex != 3) return false;
    return PointInRect(TwoColumnRect(0, 436.0f), pt);
}

bool SettingsPage::HitTestClearConfigHistory(POINT pt)
{
    if (m_categoryIndex != 3) return false;
    return PointInRect(TwoColumnRect(1, 436.0f), pt);
}

bool SettingsPage::HitTestImportJson(POINT pt)
{
    if (m_categoryIndex != 3) return false;
    return PointInRect(TwoColumnRect(1, 352.0f), pt);
}

bool SettingsPage::HitTestDiagnosticPackage(POINT pt) { return m_categoryIndex == 3 && PointInRect(TwoColumnRect(0, 310.0f), pt); }
bool SettingsPage::HitTestExportMigration(POINT pt) { return m_categoryIndex == 3 && PointInRect(TwoColumnRect(1, 310.0f), pt); }
bool SettingsPage::HitTestImportMigration(POINT pt) { return m_categoryIndex == 3 && PointInRect(TwoColumnRect(0, 352.0f), pt); }
bool SettingsPage::HitTestClearUsageHistory(POINT pt) { return m_categoryIndex == 3 && PointInRect(TwoColumnRect(0, 394.0f), pt); }
bool SettingsPage::HitTestClearCache(POINT pt) { return m_categoryIndex == 3 && PointInRect(TwoColumnRect(1, 394.0f), pt); }

bool SettingsPage::HitTestOpenSourceUrl(POINT pt)
{
    return m_categoryIndex == 5 && PointInRect(AboutOpenSourceLinkRect(), pt);
}

bool SettingsPage::HitTestThemeDetails(POINT pt, int& settingIdx, int& buttonType)
{
    if (m_categoryIndex != 0) return false;
    int currentWindowMode = m_owner->GetWindowMode();
    if (currentWindowMode != 0 && currentWindowMode != 1 && currentWindowMode != 2) return false;

    std::vector<int> activeIndices = { 1, 2, 3, 4, 5 };
    for (int i = 0; i < (int)activeIndices.size(); i++)
    {
        int col = i % 2;
        int row = i / 2;
        D2D1_RECT_F cardRect = TwoColumnRect(col, 360.0f + SYSTEM_SETTINGS_CONTENT_OFFSET + row * 38.0f);
        float ix = cardRect.left;
        float iy = 360.0f + SYSTEM_SETTINGS_CONTENT_OFFSET + row * 38.0f;
        float cy = iy + 16.0f;

        if (PointInRect(cardRect, pt))
        {
            settingIdx = activeIndices[i];
            if (pt.x >= ix + 83 && pt.x <= ix + 103 && pt.y >= cy - 10 && pt.y <= cy + 10)
            {
                buttonType = 1; // minus
            }
            else if (pt.x >= ix + 127 && pt.x <= ix + 147 && pt.y >= cy - 10 && pt.y <= cy + 10)
            {
                buttonType = 2; // plus
            }
            else
            {
                buttonType = 0; // card body
            }
            return true;
        }
    }
    return false;
}

bool SettingsPage::HitTestAnimationToggle(POINT pt)
{
    if (m_categoryIndex != 0) return false;
    return (pt.x >= 415 && pt.x <= 505 && pt.y >= 80 && pt.y <= 110);
}

bool SettingsPage::HitTestFileSelectionValidity(POINT pt, int& buttonType)
{
    if (m_categoryIndex != 2) return false;
    D2D1_RECT_F cardRect = TwoColumnRect(1, 386.0f);
    float ix = cardRect.left;
    float iy = 386.0f;
    float cy = iy + 16.0f;

    if (PointInRect(cardRect, pt))
    {
        if (pt.x >= ix + 83 && pt.x <= ix + 103 && pt.y >= cy - 10 && pt.y <= cy + 10)
        {
            buttonType = 1; // minus
        }
        else if (pt.x >= ix + 127 && pt.x <= ix + 147 && pt.y >= cy - 10 && pt.y <= cy + 10)
        {
            buttonType = 2; // plus
        }
        else
        {
            buttonType = 0; // body
        }
        return true;
    }
    return false;
}

bool SettingsPage::HitTestAnimationDurationSlider(POINT pt)
{
    if (m_categoryIndex != 0) return false;
    return pt.x >= (int)(GLOBAL_SCALE_TRACK_LEFT - 8.0f) &&
        pt.x <= (int)(GLOBAL_SCALE_TRACK_RIGHT + 8.0f) &&
        pt.y >= (int)(ANIMATION_DURATION_TRACK_Y - 12.0f) &&
        pt.y <= (int)(ANIMATION_DURATION_TRACK_Y + 12.0f);
}

bool SettingsPage::HitTestAnimationDurationApply(POINT pt)
{
    if (m_categoryIndex != 0) return false;
    return pt.x >= (int)GLOBAL_SCALE_APPLY_LEFT &&
        pt.x <= (int)GLOBAL_SCALE_APPLY_RIGHT &&
        pt.y >= (int)ANIMATION_DURATION_APPLY_TOP &&
        pt.y <= (int)ANIMATION_DURATION_APPLY_BOTTOM;
}

bool SettingsPage::HitTestGlobalScaleSlider(POINT pt)
{
    if (m_categoryIndex != 0) return false;
    return pt.x >= (int)(GLOBAL_SCALE_TRACK_LEFT - 8.0f) &&
        pt.x <= (int)(GLOBAL_SCALE_TRACK_RIGHT + 8.0f) &&
        pt.y >= (int)(GLOBAL_SCALE_TRACK_Y - 12.0f) &&
        pt.y <= (int)(GLOBAL_SCALE_TRACK_Y + 12.0f);
}

bool SettingsPage::HitTestGlobalScaleApply(POINT pt)
{
    if (m_categoryIndex != 0) return false;
    return pt.x >= (int)GLOBAL_SCALE_APPLY_LEFT &&
        pt.x <= (int)GLOBAL_SCALE_APPLY_RIGHT &&
        pt.y >= (int)GLOBAL_SCALE_APPLY_TOP &&
        pt.y <= (int)GLOBAL_SCALE_APPLY_BOTTOM;
}

bool SettingsPage::HitTestPluginInstall(POINT pt)
{
    if (m_categoryIndex != 4) return false;
    return pt.x >= 348 && pt.x <= 396 && pt.y >= 96 && pt.y <= 122;
}

bool SettingsPage::HitTestPluginOpenDir(POINT pt)
{
    if (m_categoryIndex != 4) return false;
    return pt.x >= 402 && pt.x <= 450 && pt.y >= 96 && pt.y <= 122;
}

bool SettingsPage::HitTestPluginRefresh(POINT pt)
{
    if (m_categoryIndex != 4) return false;
    return pt.x >= 456 && pt.x <= 504 && pt.y >= 96 && pt.y <= 122;
}

int SettingsPage::HitTestPluginConfigure(POINT pt)
{
    if (m_categoryIndex != 4) return -1;
    if (pt.x < 346 || pt.x > 392) return -1;
    for (int i = 0; i < 6; ++i)
    {
        float top = 152.0f + (float)i * 48.0f;
        if (pt.y >= (int)(top + 8.0f) && pt.y <= (int)(top + 30.0f))
            return i;
    }
    return -1;
}

int SettingsPage::HitTestPluginToggle(POINT pt)
{
    if (m_categoryIndex != 4) return -1;
    if (pt.x < 398 || pt.x > 450) return -1;
    for (int i = 0; i < 6; ++i)
    {
        float top = 152.0f + (float)i * 48.0f;
        if (pt.y >= (int)(top + 8.0f) && pt.y <= (int)(top + 30.0f))
            return i;
    }
    return -1;
}

int SettingsPage::HitTestPluginUninstall(POINT pt)
{
    if (m_categoryIndex != 4) return -1;
    if (pt.x < 456 || pt.x > 502) return -1;
    for (int i = 0; i < 6; ++i)
    {
        float top = 152.0f + (float)i * 48.0f;
        if (pt.y >= (int)(top + 8.0f) && pt.y <= (int)(top + 30.0f))
            return i;
    }
    return -1;
}
