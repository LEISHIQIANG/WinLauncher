#pragma once
#include <d2d1.h>
#include <dwrite.h>

class IConfigWindow;

struct SettingsBackupHoverState
{
    bool openLogFile = false;
    bool createConfigBackup = false;
    bool restoreConfigBackup = false;
    bool openConfigHistoryDir = false;
    bool diagnosticPackage = false;
    bool exportMigration = false;
    bool importMigration = false;
    bool importJson = false;
    bool clearUsageHistory = false;
    bool clearCache = false;
    bool clearConfig = false;
    bool clearConfigHistory = false;
    bool configDirText = false;
};

class SettingsBackupView
{
public:
    static void RenderBackupSection(
        ID2D1HwndRenderTarget* rt,
        IConfigWindow* owner,
        IDWriteTextFormat* tfDefault,
        ID2D1SolidColorBrush* tbNormal,
        ID2D1SolidColorBrush* tbMuted,
        D2D1_COLOR_F baseClr,
        const SettingsBackupHoverState& hover);
};
