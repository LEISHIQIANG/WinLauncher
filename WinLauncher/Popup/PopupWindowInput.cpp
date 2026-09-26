// Input interaction routing for the popup window: the WM message dispatch
// table and every OnXxx branch handler (drag-drop, DPI, timers, mouse,
// keyboard), split from PopupWindow.cpp as one responsibility in one file.
// All functions are PopupWindow members moved verbatim; signatures and the
// class definition are unchanged.

#define NOMINMAX
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include "../PopupWindow.h"
#include "PopupShortcutLauncher.h"
#include "PopupWindowMessages.h"
#include "PopupClock.h"
#include "../UI/MouseCaptureController.h"
#include "../Config/UIStyle.h"
#include <windowsx.h>
#include <shellapi.h>
#include <imm.h>

static bool IsSelectionWithinValidity(double elapsedSeconds, int validitySeconds)
{
    return validitySeconds < 0 || elapsedSeconds < validitySeconds;
}

void PopupWindow::OnDropFiles(HWND hWnd, WPARAM wParam)
{
    HDROP hDrop = reinterpret_cast<HDROP>(wParam);
    POINT pt_px{};
    DragQueryPoint(hDrop, &pt_px);
    float scale = GetWindowScale(hWnd);
    POINT pt{ (int)(pt_px.x / scale), (int)(pt_px.y / scale) };

    UINT count = DragQueryFileW(hDrop, 0xFFFFFFFF, nullptr, 0);
    std::vector<std::wstring> droppedFiles;
    droppedFiles.reserve(count);
    for (UINT i = 0; i < count; ++i)
    {
        wchar_t szPath[MAX_PATH]{};
        if (DragQueryFileW(hDrop, i, szPath, MAX_PATH))
        {
            droppedFiles.push_back(szPath);
        }
    }
    DragFinish(hDrop);

    if (!droppedFiles.empty())
    {
        int dockHit = HitTestDock(pt);
        int hit = HitTest(pt);

        if (dockHit >= 0 && dockHit < (int)m_dockPage.shortcuts.size())
        {
            auto& sc = m_dockPage.shortcuts[dockHit];
            if (!m_pinned) HideSelf(PopupShortcutLauncher::HasLaunchAction(sc) && PopupShortcutLauncher::IsBackgroundExternalLaunch(sc));
            ExecuteShortcut(sc, hWnd, m_appCtx, droppedFiles);
        }
        else if (hit >= 0 && m_currentPage >= 0 && m_currentPage < (int)m_pages.size() &&
                 hit < (int)m_pages[m_currentPage].shortcuts.size())
        {
            auto& sc = m_pages[m_currentPage].shortcuts[hit];
            if (!m_pinned) HideSelf(PopupShortcutLauncher::HasLaunchAction(sc) && PopupShortcutLauncher::IsBackgroundExternalLaunch(sc));
            ExecuteShortcut(sc, hWnd, m_appCtx, droppedFiles);
        }
    }
}

LRESULT PopupWindow::OnDpiChanged(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    m_iconPresenter.ClearBrushCache();
    RECT* const prcNewWindow = (RECT*)lParam;
    if (prcNewWindow)
    {
        float newDpiScale = UIStyle::Scaling::EffectiveScaleFactor(LOWORD(wParam) / 96.0f);

        const PopupLayout::WindowMetrics metrics = ComputeWindowMetrics();
        int w = metrics.width;
        int h = metrics.height;

        int w_px = (int)(w * newDpiScale);
        int h_px = (int)(h * newDpiScale);

        POINT ptRef = { prcNewWindow->left, prcNewWindow->top };
        HMONITOR hm = MonitorFromPoint(ptRef, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi{ sizeof(mi) };
        GetMonitorInfoW(hm, &mi);
        RECT wa = mi.rcWork;

        int currentX = prcNewWindow->left;
        int currentY = prcNewWindow->top;

        if (currentX + w_px > wa.right) currentX = wa.right - w_px;
        if (currentY + h_px > wa.bottom) currentY = wa.bottom - h_px;
        if (currentX < wa.left) currentX = wa.left;
        if (currentY < wa.top) currentY = wa.top;

        prcNewWindow->left = currentX;
        prcNewWindow->top = currentY;
        prcNewWindow->right = currentX + w_px;
        prcNewWindow->bottom = currentY + h_px;
    }

    LRESULT res = GlassWindow::HandleMessage(hWnd, uMsg, wParam, lParam);
    if (EnsureD2D())
    {
        EnsureIcons();
    }
    return res;
}

bool PopupWindow::OnTimer(HWND hWnd, WPARAM wParam)
{
    if (wParam == PopupWindowMessages::IconFlashTimer)
    {
        if (m_iconPresenter.OnFlashTimerTick())
        {
            KillTimer(hWnd, PopupWindowMessages::IconFlashTimer);
        }
        InvalidateRect(hWnd, nullptr, FALSE);
        return true;
    }
    if (wParam == PopupWindowMessages::IconProgressTimer)
    {
        auto state = m_iconRefresh.Current();
        if (m_iconRefresh.IsRefreshing() && state)
            ApplyRefreshedIcons(m_iconRefresh.WaitForCompletion(state, 0));
        else
            KillTimer(hWnd, PopupWindowMessages::IconProgressTimer);
        return true;
    }
    if (wParam == PopupWindowMessages::FileSelectionTimerId)
    {
        PollFileSelectionQuery();
        return true;
    }
    if (wParam == PopupWindowMessages::ClickCloseTimerId)
    {
        KillTimer(hWnd, PopupWindowMessages::ClickCloseTimerId);
        bool autoClose = !m_appCtx || !m_appCtx->configService || m_appCtx->configService->GetPopupAutoClose();
        if (!autoClose && !m_pinned && m_pressedShortcutKind == PressedShortcutKind::None)
        {
            ClearCapturedFileSelection();
            HideSelf();
        }
        return true;
    }
    if (wParam == PopupWindowMessages::AutoHideTimerId)
    {
        if (m_searchActive)
        {
            m_searchTextBox.BlinkCaret();
            InvalidateRect(hWnd, nullptr, FALSE);
        }

        bool autoClose = !m_appCtx || !m_appCtx->configService || m_appCtx->configService->GetPopupAutoClose();
        if (m_pinned) return true;
        if (m_pressedShortcutKind != PressedShortcutKind::None) return true;

        // Grace period: do not close within 500ms of showing the popup
        if (PopupClock::NowSeconds() - m_showTimeSeconds < 0.5)
        {
            return true;
        }

        POINT pt; GetCursorPos(&pt); ScreenToClient(hWnd, &pt);
        RECT cr; GetClientRect(hWnd, &cr);
        bool outside = pt.x < 0 || pt.y < 0 || pt.x >= cr.right || pt.y >= cr.bottom;
        if (!autoClose)
        {
            if (outside)
            {
                bool mousePressed =
                    (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0 ||
                    (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0 ||
                    (GetAsyncKeyState(VK_MBUTTON) & 0x8000) != 0 ||
                    (GetAsyncKeyState(VK_XBUTTON1) & 0x8000) != 0 ||
                    (GetAsyncKeyState(VK_XBUTTON2) & 0x8000) != 0;
                if (mousePressed)
                {
                    SetTimer(hWnd, PopupWindowMessages::ClickCloseTimerId, 50, nullptr);
                }
            }
            return true;
        }
        if (outside)
        {
            bool imeActive = false;
            HIMC hIMC = ImmGetContext(hWnd);
            if (hIMC)
            {
                DWORD dwSize = ImmGetCandidateListW(hIMC, 0, nullptr, 0);
                if (dwSize > 0)
                {
                    imeActive = true;
                }
                else
                {
                    LONG compLen = ImmGetCompositionStringW(hIMC, GCS_COMPSTR, nullptr, 0);
                    if (compLen > 0)
                    {
                        imeActive = true;
                    }
                }
                ImmReleaseContext(hWnd, hIMC);
            }

            if (!imeActive)
            {
                ClearCapturedFileSelection();
                HideSelf();
            }
        }
        return true;
    }
    if (wParam == PopupWindowMessages::PageAnimationTimerId)
    {
        StepPageAnimationFrame(hWnd);
        return true;
    }
    if (wParam == PopupWindowMessages::TimelineAnimationTimerId)
    {
        double now = PopupClock::NowSeconds();
        double elapsed = 0.0;
        std::vector<std::wstring> selectionPreview;
        const bool hasSelection = m_fileSelection.Peek(now, -1, selectionPreview, &elapsed);
        const bool isEmpty = !hasSelection;

        const int validitySeconds = GetFileSelectionValiditySeconds();
        const bool selectionExpired = !IsSelectionWithinValidity(elapsed, validitySeconds);
        if (selectionExpired || isEmpty || validitySeconds < 0)
        {
            KillTimer(hWnd, PopupWindowMessages::TimelineAnimationTimerId);
            if (selectionExpired) m_fileSelection.ExpireIfNeeded(now, validitySeconds);
        }
        InvalidateRect(hWnd, nullptr, FALSE);
        return true;
    }
    if (wParam == PopupWindowMessages::PluginSearchTimerId)
    {
        if (!m_searchActive || m_searchQuery.empty() || !m_appCtx || !m_appCtx->pluginManager)
        {
            KillTimer(hWnd, PopupWindowMessages::PluginSearchTimerId);
            return true;
        }

        bool wasRunning = m_appCtx->pluginManager->IsSearchRunning(m_searchQuery);
        UpdateSearch();
        InvalidateRect(hWnd, nullptr, FALSE);
        if (!wasRunning && !m_appCtx->pluginManager->IsSearchRunning(m_searchQuery))
            KillTimer(hWnd, PopupWindowMessages::PluginSearchTimerId);
        return true;
    }
    return false;
}

void PopupWindow::OnMouseMove(HWND hWnd, LPARAM lParam)
{
    POINT pt_px{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
    RECT cr; GetClientRect(hWnd, &cr);
    if (pt_px.x < 0 || pt_px.y < 0 || pt_px.x >= cr.right || pt_px.y >= cr.bottom)
    {
        if (m_pressedShortcutKind != PressedShortcutKind::None)
        {
            m_hovered = -1;
            m_hoveredDock = -1;
            InvalidateRect(hWnd, nullptr, FALSE);
            return;
        }
        bool autoClose = !m_appCtx || !m_appCtx->configService || m_appCtx->configService->GetPopupAutoClose();
        if (autoClose && !m_pinned)
        {
            UINT delay = (UINT)(m_appCtx && m_appCtx->configService ? m_appCtx->configService->GetHoverLeaveDelay() : 200);
            if (delay == 0) delay = 1;
            SetTimer(hWnd, PopupWindowMessages::AutoHideTimerId, delay, nullptr);
        }
        else if (!autoClose && !m_pinned)
        {
            SetTimer(hWnd, PopupWindowMessages::AutoHideTimerId, 50, nullptr);
        }
        return;
    }

    float scale = GetWindowScale(hWnd);
    POINT pt{ (int)(pt_px.x / scale), (int)(pt_px.y / scale) };
    StartAutoHideTimer();

    if (m_searchActive)
    {
        bool repaint = false;
        m_searchTextBox.OnMouseMove(hWnd, pt, scale, repaint);
        if (repaint) InvalidateRect(hWnd, nullptr, FALSE);
    }

    // Handle tab hover
    int newHoveredTab = -1;
    if (!m_searchActive && pt.y >= GetWndPadding() && pt.y <= GetWndPadding() + GetHeaderLayout().controlHeight)
    {
        int numPages = (int)m_pages.size();
        if (numPages > 0)
        {
            int wndPad = GetWndPadding();
            float totalWidth = (cr.right / scale) - wndPad * 2;
            float tabWidth = totalWidth / numPages;

            int hoveredTab = (int)((pt.x - wndPad) / tabWidth);
            if (hoveredTab >= 0 && hoveredTab < numPages)
            {
                newHoveredTab = hoveredTab;
            }
        }
    }

    if (newHoveredTab != m_hoveredTab)
    {
        m_hoveredTab = newHoveredTab;
        InvalidateRect(hWnd, nullptr, FALSE);
    }

    int h = HitTest(pt);
    if (h != m_hovered) { m_hovered = h; InvalidateRect(hWnd, nullptr, FALSE); }

    // Handle dock hover
    int newHoveredDock = HitTestDock(pt);
    if (newHoveredDock != m_hoveredDock)
    {
        m_hoveredDock = newHoveredDock;
        InvalidateRect(hWnd, nullptr, FALSE);
    }

    // Use TME_LEAVE for more responsive hide
    if (!m_trackMouse)
    {
        TRACKMOUSEEVENT tme{ sizeof(tme) };
        tme.dwFlags = TME_LEAVE;
        tme.hwndTrack = hWnd;
        TrackMouseEvent(&tme);
        m_trackMouse = true;
    }
}

void PopupWindow::OnLButtonDown(HWND hWnd, LPARAM lParam)
{
    POINT pt_px{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
    float scale = GetWindowScale(hWnd);
    POINT pt{ (int)(pt_px.x / scale), (int)(pt_px.y / scale) };

    // Handle search box click when search is active
    if (m_searchActive && pt.y >= GetWndPadding() && pt.y <= GetWndPadding() + GetHeaderLayout().controlHeight)
    {
        RECT cr; GetClientRect(hWnd, &cr);
        float w = (float)cr.right / scale;
        int wndPad = GetWndPadding();
        if (pt.x >= wndPad && pt.x <= w - wndPad)
        {
            m_searchTextBox.SetFocus(true);
            bool repaint = false;
            m_searchTextBox.OnLButtonDown(hWnd, pt, scale, repaint);
            if (repaint) InvalidateRect(hWnd, nullptr, FALSE);
            return;
        }
    }
    else if (m_searchActive)
    {
        m_searchTextBox.SetFocus(false);
    }

    // Handle tab click
    if (!m_searchActive && pt.y >= GetWndPadding() && pt.y <= GetWndPadding() + GetHeaderLayout().controlHeight)
    {
        int numPages = (int)m_pages.size();
        if (numPages > 0)
        {
            int wndPad = GetWndPadding();
            RECT cr; GetClientRect(hWnd, &cr);
            float totalWidth = (cr.right / scale) - wndPad * 2;
            float tabWidth = totalWidth / numPages;

            int clickedTab = (int)((pt.x - wndPad) / tabWidth);
            if (clickedTab >= 0 && clickedTab < numPages)
            {
                if (clickedTab != m_currentPage)
                {
                    m_wheel.Reset(clickedTab);
                    m_currentPage = clickedTab;
                    m_hovered = -1;
                    if (m_viewModel) m_viewModel->SetCurrentPage(ToModelPageIndex(clickedTab));

                    if (!m_animating)
                    {
                        StartPageAnimationLoop();
                    }
                    InvalidateRect(hWnd, nullptr, FALSE);
                }
                return;
            }
        }
    }

    int hit = HitTest(pt);
    if (m_searchActive && !m_searchQuery.empty())
    {
        if (hit >= 0 && hit < (int)m_searchResults.size())
        {
            m_pressedShortcutKind = PressedShortcutKind::SearchResult;
            m_pressedShortcutIndex = hit;
            m_pressedShortcutPage = -1;
            MouseCaptureController::CaptureGesture(hWnd);
            InvalidateRect(hWnd, nullptr, FALSE);
        }
        else if (m_pinned)
        {
            ResetPressedShortcut();
            MouseCaptureController::ReleaseCurrent(L"popup_window_move");
            SendMessageW(hWnd, WM_SYSCOMMAND, SC_MOVE | HTCAPTION, 0);
        }
    }
    else
    {
        // Handle dock click (check before normal hit test so dock takes priority)
        int dockHit = HitTestDock(pt);
        if (dockHit >= 0 && dockHit < (int)m_dockPage.shortcuts.size())
        {
            m_pressedShortcutKind = PressedShortcutKind::Dock;
            m_pressedShortcutIndex = dockHit;
            m_pressedShortcutPage = -1;
            MouseCaptureController::CaptureGesture(hWnd);
            InvalidateRect(hWnd, nullptr, FALSE);
            return;
        }

        if (hit >= 0 && hit < (int)m_pages[m_currentPage].shortcuts.size())
        {
            m_pressedShortcutKind = PressedShortcutKind::Page;
            m_pressedShortcutIndex = hit;
            m_pressedShortcutPage = m_currentPage;
            MouseCaptureController::CaptureGesture(hWnd);
            InvalidateRect(hWnd, nullptr, FALSE);
        }
        else if (m_pinned)
        {
            ResetPressedShortcut();
            MouseCaptureController::ReleaseCurrent(L"popup_window_move");
            SendMessageW(hWnd, WM_SYSCOMMAND, SC_MOVE | HTCAPTION, 0);
        }
    }
}

void PopupWindow::OnLButtonDblClk(HWND hWnd, LPARAM lParam)
{
    POINT pt_px{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
    float scale = GetWindowScale(hWnd);
    POINT pt{ (int)(pt_px.x / scale), (int)(pt_px.y / scale) };

    // Handle search box double click
    if (m_searchActive && pt.y >= GetWndPadding() && pt.y <= GetWndPadding() + GetHeaderLayout().controlHeight)
    {
        RECT cr; GetClientRect(hWnd, &cr);
        float w = (float)cr.right / scale;
        int wndPad = GetWndPadding();
        if (pt.x >= wndPad && pt.x <= w - wndPad)
        {
            bool repaint = false;
            m_searchTextBox.OnLButtonDblClk(hWnd, pt, scale, repaint);
            if (repaint) InvalidateRect(hWnd, nullptr, FALSE);
            return;
        }
    }

    // On blank area double-click → refresh icons
    if (!m_searchActive || m_searchQuery.empty())
    {
        bool onTab = pt.y >= GetWndPadding() && pt.y <= GetWndPadding() + GetHeaderLayout().controlHeight;
        int dockHit = HitTestDock(pt);
        int hit = HitTest(pt);
        bool onShortcut = hit >= 0 && hit < (int)m_pages[m_currentPage].shortcuts.size();
        bool onDock = dockHit >= 0 && dockHit < (int)m_dockPage.shortcuts.size();

        if (!onTab && !onShortcut && !onDock)
        {
            RefreshIcons();
            return;
        }
    }
}

void PopupWindow::OnLButtonUp(HWND hWnd, LPARAM lParam)
{
    POINT pt_px{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
    float scale = GetWindowScale(hWnd);
    POINT pt{ (int)(pt_px.x / scale), (int)(pt_px.y / scale) };

    if (m_pressedShortcutKind != PressedShortcutKind::None)
    {
        PressedShortcutKind pressedKind = m_pressedShortcutKind;
        int pressedIndex = m_pressedShortcutIndex;
        int pressedPage = m_pressedShortcutPage;
        if (GetCapture() == hWnd)
        {
            MouseCaptureController::Complete(hWnd);
        }
        ResetPressedShortcut();

        if (pressedKind == PressedShortcutKind::SearchResult && m_searchActive && !m_searchQuery.empty())
        {
            int hit = HitTest(pt);
            if (hit == pressedIndex && hit >= 0 && hit < (int)m_searchResults.size())
            {
                if (!m_pinned) HideSelf(PopupShortcutLauncher::IsBackgroundExternalLaunch(m_searchResults[hit].shortcut));
                ExecuteSearchResult(hit);
            }
        }
        else if (pressedKind == PressedShortcutKind::Dock)
        {
            int dockHit = HitTestDock(pt);
            if (dockHit == pressedIndex && dockHit >= 0 && dockHit < (int)m_dockPage.shortcuts.size())
            {
                auto& sc = m_dockPage.shortcuts[dockHit];
                if (!m_pinned) HideSelf(PopupShortcutLauncher::HasLaunchAction(sc) && PopupShortcutLauncher::IsBackgroundExternalLaunch(sc));
                if (PopupShortcutLauncher::HasLaunchAction(sc))
                {
                    LOG_G_INFO(L"PopupWindow::LButtonUp: launching dock shortcut %s (Target=%s)", sc.name.c_str(), sc.targetPath.c_str());
                    RecordShortcutUsage(sc);
                    LaunchShortcut(sc);
                    if (m_pinned)
                        ApplyShortcutSortMode();
                }
            }
        }
        else if (pressedKind == PressedShortcutKind::Page)
        {
            int hit = HitTest(pt);
            if (pressedPage == m_currentPage &&
                hit == pressedIndex &&
                pressedPage >= 0 &&
                pressedPage < (int)m_pages.size() &&
                hit >= 0 &&
                hit < (int)m_pages[pressedPage].shortcuts.size())
            {
                auto& sc = m_pages[pressedPage].shortcuts[hit];
                if (!m_pinned) HideSelf(PopupShortcutLauncher::HasLaunchAction(sc) && PopupShortcutLauncher::IsBackgroundExternalLaunch(sc));
                if (PopupShortcutLauncher::HasLaunchAction(sc))
                {
                    LOG_G_INFO(L"PopupWindow::LButtonUp: launching shortcut %s (Target=%s)", sc.name.c_str(), sc.targetPath.c_str());
                    RecordShortcutUsage(sc);
                    LaunchShortcut(sc);
                }
                if (m_viewModel)
                    m_viewModel->NotifyShortcutLaunched(ToModelPageIndex(pressedPage), hit);
                if (m_pinned)
                    ApplyShortcutSortMode();
            }
        }

        InvalidateRect(hWnd, nullptr, FALSE);
        return;
    }

    if (m_searchActive)
    {
        bool repaint = false;
        m_searchTextBox.OnLButtonUp(hWnd, pt, scale, repaint);
        if (repaint) InvalidateRect(hWnd, nullptr, FALSE);
    }
}

void PopupWindow::OnKeyDown(HWND hWnd, WPARAM wParam, LPARAM lParam)
{
    if (wParam == VK_ESCAPE)
    {
        if (m_searchActive)
        {
            m_searchActive = false;
            m_searchTextBox.SetText(L"");
            m_searchTextBox.SetFocus(false);
            m_searchQuery.clear();
            m_searchResults.clear();
            if (m_appCtx && m_appCtx->configService)
            {
                m_appCtx->configService->SetSearchMode(false);
                SavePopupConfig();
            }
            InvalidateRect(hWnd, nullptr, FALSE);
        }
        else
        {
            HideSelf();
        }
    }
    else if (wParam == VK_TAB)
    {
        m_searchActive = !m_searchActive;
        if (!m_searchActive)
        {
            m_searchTextBox.SetText(L"");
            m_searchTextBox.SetFocus(false);
            m_searchQuery.clear();
            m_searchResults.clear();
        }
        else
        {
            m_searchTextBox.SetFocus(true);
            UpdateSearch();
        }
        if (m_appCtx && m_appCtx->configService)
        {
            m_appCtx->configService->SetSearchMode(m_searchActive);
            SavePopupConfig();
        }
        InvalidateRect(hWnd, nullptr, FALSE);
    }
    else if (m_searchActive)
    {
        if (wParam == VK_RETURN)
        {
            if (m_selectedSearchResult >= 0 && m_selectedSearchResult < (int)m_searchResults.size())
            {
                const auto& result = m_searchResults[m_selectedSearchResult];
                if (!m_pinned) HideSelf(PopupShortcutLauncher::IsBackgroundExternalLaunch(result.shortcut));
                ExecuteSearchResult(m_selectedSearchResult);
            }
        }
        else if (wParam == VK_UP)
        {
            if (!m_searchResults.empty())
            {
                if (m_selectedSearchResult < 0)
                    m_selectedSearchResult = (int)m_searchResults.size() - 1;
                else
                    m_selectedSearchResult = (m_selectedSearchResult - 1 + (int)m_searchResults.size()) % (int)m_searchResults.size();
                InvalidateRect(hWnd, nullptr, FALSE);
            }
        }
        else if (wParam == VK_DOWN)
        {
            if (!m_searchResults.empty())
            {
                if (m_selectedSearchResult < 0)
                    m_selectedSearchResult = 0;
                else
                    m_selectedSearchResult = (m_selectedSearchResult + 1) % (int)m_searchResults.size();
                InvalidateRect(hWnd, nullptr, FALSE);
            }
        }
        else
        {
            bool repaint = false;
            std::wstring oldText = m_searchTextBox.GetText();
            m_searchTextBox.OnKeyDown(hWnd, wParam, lParam, repaint);
            if (m_searchTextBox.GetText() != oldText)
            {
                m_searchQuery = m_searchTextBox.GetText();
                UpdateSearch();
                repaint = true;
            }
            if (repaint) InvalidateRect(hWnd, nullptr, FALSE);
            return;
        }
    }
    else if ((!m_searchActive || m_searchTextBox.IsEmpty()) && wParam >= '1' && wParam <= '9')
    {
        int targetIdx = static_cast<int>(wParam - '1');
        if (m_currentPage >= 0 && m_currentPage < static_cast<int>(m_pages.size()) &&
            targetIdx < static_cast<int>(m_pages[m_currentPage].shortcuts.size()))
        {
            auto& sc = m_pages[m_currentPage].shortcuts[targetIdx];
            if (!m_pinned) HideSelf(PopupShortcutLauncher::HasLaunchAction(sc) && PopupShortcutLauncher::IsBackgroundExternalLaunch(sc));
            if (PopupShortcutLauncher::HasLaunchAction(sc))
            {
                LOG_G_INFO(L"PopupWindow::WM_KEYDOWN: quick launching shortcut %s (Index=%d)", sc.name.c_str(), targetIdx);
                RecordShortcutUsage(sc);
                LaunchShortcut(sc);
            }
            if (m_viewModel)
                m_viewModel->NotifyShortcutLaunched(ToModelPageIndex(m_currentPage), targetIdx);
            if (m_pinned)
                ApplyShortcutSortMode();
            return;
        }
    }
    else if (wParam == VK_LEFT)
    {
        if (m_pages.size() > 1)
        {
            int targetPage = (m_currentPage - 1 + (int)m_pages.size()) % (int)m_pages.size();
            if (targetPage != m_currentPage)
            {
                m_wheel.Reset(targetPage);
                m_currentPage = targetPage;
                m_hovered = -1;
                if (m_viewModel) m_viewModel->SetCurrentPage(ToModelPageIndex(targetPage));

                if (!m_animating)
                {
                    StartPageAnimationLoop();
                }
                InvalidateRect(hWnd, nullptr, FALSE);
            }
        }
    }
    else if (wParam == VK_RIGHT)
    {
        if (m_pages.size() > 1)
        {
            int targetPage = (m_currentPage + 1) % (int)m_pages.size();
            if (targetPage != m_currentPage)
            {
                m_wheel.Reset(targetPage);
                m_currentPage = targetPage;
                m_hovered = -1;
                if (m_viewModel) m_viewModel->SetCurrentPage(ToModelPageIndex(targetPage));

                if (!m_animating)
                {
                    StartPageAnimationLoop();
                }
                InvalidateRect(hWnd, nullptr, FALSE);
            }
        }
    }
}

void PopupWindow::OnChar(HWND hWnd, WPARAM wParam)
{
    if (wParam >= '1' && wParam <= '9' && !m_searchActive && m_searchQuery.empty())
    {
        return;
    }

    if (wParam >= 32 && !m_searchActive)
    {
        // Activate search mode temporarily and immediately repaint so the
        // search box appears before the character is processed.
        // NOTE: do NOT persist this activation to config – the next popup
        // open should still show the category tab bar as before.
        m_searchActive = true;
        m_searchTextBox.SetFocus(true);
        m_searchTextBox.SetText(L"");
        m_searchQuery.clear();
        m_searchResults.clear();
        InvalidateRect(hWnd, nullptr, FALSE);
    }

    if (m_searchActive)
    {
        bool repaint = false;
        std::wstring oldText = m_searchTextBox.GetText();
        m_searchTextBox.OnChar(hWnd, wParam, repaint);
        if (m_searchTextBox.GetText() != oldText)
        {
            m_searchQuery = m_searchTextBox.GetText();
            UpdateSearch();
            repaint = true;
        }
        if (repaint) InvalidateRect(hWnd, nullptr, FALSE);
    }
}

LRESULT PopupWindow::HandleMessage(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_DROPFILES:
        OnDropFiles(hWnd, wParam);
        return 0;

    case WM_DPICHANGED:
        return OnDpiChanged(hWnd, uMsg, wParam, lParam);

    case WM_IME_STARTCOMPOSITION:
    case WM_IME_COMPOSITION:
    case WM_IME_ENDCOMPOSITION:
    {
        // If the user starts typing Chinese/Japanese/Korean via IME while the
        // popup is showing the category tab bar, switch to search mode now so
        // the composition string appears inside the search box.
        // Do NOT save this to config – next popup open should still show tabs.
        if (!m_searchActive && uMsg == WM_IME_STARTCOMPOSITION)
        {
            m_searchActive = true;
            m_searchTextBox.SetFocus(true);
            m_searchTextBox.SetText(L"");
            m_searchQuery.clear();
            m_searchResults.clear();
            InvalidateRect(hWnd, nullptr, FALSE);
        }

        if (m_searchActive)
        {
            bool repaint = false;
            if (m_searchTextBox.HandleImeMessage(hWnd, uMsg, wParam, lParam, repaint))
            {
                if (repaint) InvalidateRect(hWnd, nullptr, FALSE);
                return 0;
            }
        }
        break;
    }

    case WM_TIMER:
        if (OnTimer(hWnd, wParam)) return 0;
        break;

    case WM_ACTIVATE:
    {
        // The popup can remain open while inactive (for example when it is
        // pinned or auto-close is disabled).  Always forward activation to
        // GlassWindow so its companion shadow stays directly behind it.
        GlassWindow::HandleMessage(hWnd, uMsg, wParam, lParam);
        if (LOWORD(wParam) == WA_INACTIVE)
        {
            bool autoClose = !m_appCtx || !m_appCtx->configService || m_appCtx->configService->GetPopupAutoClose();
            if (!autoClose && !m_pinned && m_pressedShortcutKind == PressedShortcutKind::None)
            {
                SetTimer(hWnd, PopupWindowMessages::ClickCloseTimerId, 100, nullptr);
            }
        }
        break;
    }

    case PopupWindowMessages::Animate:
    {
        StepPageAnimationFrame(hWnd);
        return 0;
    }

    case PopupWindowMessages::SelectionUpdated:
    {
        SetTimer(hWnd, PopupWindowMessages::TimelineAnimationTimerId, PopupWindowMessages::TimelineAnimationFrameMs, nullptr);
        InvalidateRect(hWnd, nullptr, FALSE);
        return 0;
    }

    case PopupWindowMessages::RefreshIcons:
    {
        if (!m_iconRefresh.IsRefreshing()) return 0;
        OnIconPreloadCompleted(m_iconRefresh.Current());
        return 0;
    }

    case WM_MOUSEMOVE:
        OnMouseMove(hWnd, lParam);
        return 0;

    case WM_MOUSELEAVE:
    {
        m_trackMouse = false;
        m_hoveredTab = -1;
        m_hoveredDock = -1;
        bool autoClose = !m_appCtx || !m_appCtx->configService || m_appCtx->configService->GetPopupAutoClose();
        if (autoClose && !m_pinned)
        {
            UINT delay = (UINT)(m_appCtx && m_appCtx->configService ? m_appCtx->configService->GetHoverLeaveDelay() : 200);
            if (delay == 0) delay = 1;
            SetTimer(hWnd, PopupWindowMessages::AutoHideTimerId, delay, nullptr);
        }
        else if (!autoClose && !m_pinned)
        {
            SetTimer(hWnd, PopupWindowMessages::AutoHideTimerId, 50, nullptr);
        }
        return 0;
    }

    case WM_MOUSEWHEEL:
    {
        if (m_searchActive && !m_searchQuery.empty()) return 0;
        if (m_pages.size() <= 1) return 0;
        if (m_wheel.Wheel(GET_WHEEL_DELTA_WPARAM(wParam), m_scrollPosition))
        {
            m_currentPage = PopupWheelState::Page(m_wheel.target, static_cast<int>(m_pages.size()));
            m_hovered = -1;
            if (m_viewModel) m_viewModel->SetCurrentPage(ToModelPageIndex(m_currentPage));
            if (!m_animating) StartPageAnimationLoop();
            InvalidateRect(hWnd, nullptr, FALSE);
        }
        return 0;
    }

    case WM_LBUTTONDOWN:
        OnLButtonDown(hWnd, lParam);
        return 0;

    case WM_LBUTTONDBLCLK:
        OnLButtonDblClk(hWnd, lParam);
        return 0;

    case WM_LBUTTONUP:
        OnLButtonUp(hWnd, lParam);
        return 0;

    case WM_RBUTTONDOWN:
        m_pinned = !m_pinned;
        if (m_pinned)
        {
            SetWindowPos(hWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        }
        InvalidateRect(hWnd, nullptr, FALSE);
        return 0;

    case WM_MBUTTONDOWN:
        HideSelf();
        return 0;

    case WM_CAPTURECHANGED:
        MouseCaptureController::OnCaptureChanged(hWnd, reinterpret_cast<HWND>(lParam));
        m_trackMouse = false;
        m_hovered = -1;
        if (!m_pinned && m_pressedShortcutKind == PressedShortcutKind::None)
        {
            // Grace period: do not close within 500ms of showing the popup
            if (PopupClock::NowSeconds() - m_showTimeSeconds < 0.5)
            {
                ResetPressedShortcut();
                return 0;
            }

            POINT pt; GetCursorPos(&pt); ScreenToClient(hWnd, &pt);
            RECT cr; GetClientRect(hWnd, &cr);
            if (pt.x < 0 || pt.y < 0 || pt.x >= cr.right || pt.y >= cr.bottom)
            {
                StopAutoHideTimer();
                ShowWindow(hWnd, SW_HIDE);
                if (m_viewModel) m_viewModel->NotifyPopupHidden();
            }
        }
        ResetPressedShortcut();
        return 0;

    case WM_KEYDOWN:
        OnKeyDown(hWnd, wParam, lParam);
        return 0;

    case WM_CHAR:
        OnChar(hWnd, wParam);
        return 0;

    case WM_DESTROY:
        KillTimer(hWnd, PopupWindowMessages::PageAnimationTimerId);
        KillTimer(hWnd, PopupWindowMessages::PluginSearchTimerId);
        GlassWindow::HandleMessage(hWnd, uMsg, wParam, lParam);
        // s_instance is managed by Release()
        return 0;
    }

    return GlassWindow::HandleMessage(hWnd, uMsg, wParam, lParam);
}

