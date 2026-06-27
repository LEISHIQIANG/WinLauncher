#pragma once
#include <memory>
#include <vector>
#include <string>
#include <Windows.h>
#include "../Model/ShortcutInfo.h"
#include "../Services/IConfigService.h"
#include "../App/AppContext.h"

using Model::PopupPage;
using Model::ShortcutInfo;

class PopupViewModel
{
public:
    explicit PopupViewModel(AppContext* ctx)
        : m_ctx(ctx)
    {
        ReloadPages();
    }

    void ReloadPages()
    {
        if (m_ctx && m_ctx->configService)
        {
            auto allPages = m_ctx->configService->LoadConfig();
            m_pages.clear();
            m_dockPage = PopupPage{};
            m_dockPage.name = L"DOCK";
            for (auto& page : allPages)
            {
                if (page.name == L"DOCK")
                    m_dockPage = std::move(page);
                else
                    m_pages.push_back(std::move(page));
            }
        }

        if (m_currentPage >= (int)m_pages.size())
            m_currentPage = 0;
        if (m_currentPage < 0 && !m_pages.empty())
            m_currentPage = 0;

        m_scrollPosition = (float)m_currentPage;
        m_scrollVelocity = 0.0f;
    }

    const std::vector<PopupPage>& GetPages() const { return m_pages; }
    std::vector<PopupPage>& GetPages() { return m_pages; }

    const PopupPage& GetDockPage() const { return m_dockPage; }
    PopupPage& GetDockPage() { return m_dockPage; }

    int GetCurrentPage() const { return m_currentPage; }
    float GetScrollPosition() const { return m_scrollPosition; }
    float GetScrollVelocity() const { return m_scrollVelocity; }

    bool IsPinned() const { return m_pinned; }
    void TogglePin() { m_pinned = !m_pinned; }
    void SetPinned(bool p) { m_pinned = p; }

    bool IsAnimating() const { return m_animating; }

    bool SwitchToPage(int newPage)
    {
        if (m_pages.size() <= 1) return false;
        if (newPage < 0 || newPage >= (int)m_pages.size()) return false;
        if (newPage == m_currentPage) return false;

        m_currentPage = newPage;

        if (!m_animating)
        {
            m_animating = true;
            m_animLastTick = GetTickCount64();
        }
        return true;
    }

    void NotifyShortcutLaunched(int pageIndex, int shortcutIndex)
    {
        if (pageIndex < 0 || pageIndex >= (int)m_pages.size()) return;
        auto& page = m_pages[pageIndex];
        if (shortcutIndex < 0 || shortcutIndex >= (int)page.shortcuts.size()) return;
        if (m_ctx && m_ctx->pluginHost)
            m_ctx->pluginHost->NotifyShortcutLaunched(page.shortcuts[shortcutIndex]);
    }

    bool UpdateAnimation()
    {
        if (!m_animating) return false;

        ULONGLONG now = GetTickCount64();
        float dt = (float)(now - m_animLastTick) / 1000.0f;
        m_animLastTick = now;

        if (dt > 0.1f) dt = 0.1f;
        if (dt <= 0.0f) dt = 0.016f;

        float target = (float)m_currentPage;
        float error = target - m_scrollPosition;

        float stiffness = 200.0f;
        float damping = 22.0f;

        float force = error * stiffness - m_scrollVelocity * damping;
        m_scrollVelocity += force * dt;
        m_scrollPosition += m_scrollVelocity * dt;

        if (std::abs(target - m_scrollPosition) < 0.002f && std::abs(m_scrollVelocity) < 0.05f)
        {
            m_scrollPosition = target;
            m_scrollVelocity = 0.0f;
            m_animating = false;
        }
        return true;
    }

    void ResetScroll()
    {
        m_scrollPosition = (float)m_currentPage;
        m_scrollVelocity = 0.0f;
        m_animating = false;
    }

    void NotifyPopupShown()
    {
        if (m_ctx && m_ctx->pluginHost)
            m_ctx->pluginHost->NotifyPopupShown();
    }

    void NotifyPopupHidden()
    {
        if (m_ctx && m_ctx->pluginHost)
            m_ctx->pluginHost->NotifyPopupHidden();
    }

private:
    AppContext* m_ctx;
    std::vector<PopupPage> m_pages;
    PopupPage m_dockPage;
    int m_currentPage = 0;
    bool m_pinned = false;

    bool m_animating = false;
    ULONGLONG m_animLastTick = 0;
    float m_scrollPosition = 0.0f;
    float m_scrollVelocity = 0.0f;
};
