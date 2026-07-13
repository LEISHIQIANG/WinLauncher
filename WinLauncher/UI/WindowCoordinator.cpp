#include "WindowCoordinator.h"
#include "../App/AppContext.h"
#include "../PopupWindow.h"
#include "../Config/ConfigWindow.h"

WindowCoordinator::WindowCoordinator(AppContext* ctx)
    : m_ctx(ctx)
{
}

bool WindowCoordinator::IsPopupVisible()
{
    return PopupWindow::IsVisible();
}

bool WindowCoordinator::IsConfigVisible()
{
    return ConfigWindow::IsVisible();
}

void WindowCoordinator::ShowPopup(HWND parent, POINT pt)
{
    PopupWindow::Show(parent, pt);
}

void WindowCoordinator::ShowConfig(HWND parent, bool settingsMode)
{
    if (settingsMode)
    {
        ConfigWindow::ShowSettings(parent, m_ctx);
    }
    else
    {
        ConfigWindow::ShowConfig(parent, m_ctx);
    }
}

void WindowCoordinator::HidePopup()
{
    PopupWindow::Hide();
}

void WindowCoordinator::CloseConfig()
{
    ConfigWindow::Hide();
}

HWND WindowCoordinator::GetPopupHWND()
{
    return PopupWindow::GetHWNDStatic();
}

HWND WindowCoordinator::GetConfigHWND()
{
    return ConfigWindow::GetHWNDStatic();
}

void WindowCoordinator::DestroyWindows()
{
    PopupWindow::Release();
    ConfigWindow::Release(true);
}
