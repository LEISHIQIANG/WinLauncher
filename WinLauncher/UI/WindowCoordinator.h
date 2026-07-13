#pragma once
#include "../Contracts/IWindowCoordinator.h"
#include <memory>

struct AppContext;

class WindowCoordinator : public IWindowCoordinator
{
public:
    explicit WindowCoordinator(AppContext* ctx);
    virtual ~WindowCoordinator() override = default;

    virtual bool IsPopupVisible() override;
    virtual bool IsConfigVisible() override;
    virtual void ShowPopup(HWND parent, POINT pt) override;
    virtual void ShowConfig(HWND parent, bool settingsMode) override;
    virtual void HidePopup() override;
    virtual void CloseConfig() override;
    virtual HWND GetPopupHWND() override;
    virtual HWND GetConfigHWND() override;
    virtual void DestroyWindows() override;

private:
    AppContext* m_ctx;
};
