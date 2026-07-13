#pragma once
#include <Windows.h>

class IWindowCoordinator
{
public:
    virtual ~IWindowCoordinator() = default;

    virtual bool IsPopupVisible() = 0;
    virtual bool IsConfigVisible() = 0;
    virtual void ShowPopup(HWND parent, POINT pt) = 0;
    virtual void ShowConfig(HWND parent, bool settingsMode) = 0;
    virtual void HidePopup() = 0;
    virtual void CloseConfig() = 0;
    virtual HWND GetPopupHWND() = 0;
    virtual HWND GetConfigHWND() = 0;
    virtual void DestroyWindows() = 0;
};
