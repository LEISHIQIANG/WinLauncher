#define NOMINMAX
#include "SceneSettingsWindow.h"
#include "DropDownMenu.h"
#include "UIStyle.h"
#include "../DpiHelper.h"
#include <windowsx.h>
#include <algorithm>

static const int SCENE_DLG_W = 460;
static const int SCENE_DLG_H = 420;
static const float Y_TITLE = 10.0f;
static const float Y_MODE = 46.0f;
static const float Y_LIST_TOP = 96.0f;
static const float Y_ITEM_TOP = 128.0f;
static const float Y_LIST_BOTTOM = 350.0f;
static const float Y_BUTTONS = 374.0f;
static const float COL_W = 190.0f;
static const float LEFT_X = 20.0f;
static const float RIGHT_X = 250.0f;
static const float ITEM_H = 28.0f;

static SceneSettingsWindow* g_sceneSettingsInstance = nullptr;

SceneSettingsWindow::SceneSettingsWindow(RendPopupPage* page, AppContext* ctx, std::function<void()> onChanged)
    : m_page(page)
    , m_onChanged(std::move(onChanged))
{
    m_appCtx = ctx;
    if (m_page)
    {
        m_sceneMode = m_page->sceneMode;
        m_selectedApps = m_page->sceneApps;
        for (const auto& app : m_page->sceneAvailableApps)
        {
            AddAvailableCandidate(AppScene::CandidateFromStoredApp(app));
        }
    }
    AppScene::AddCommonCandidates(m_availableApps);
    std::sort(m_availableApps.begin(), m_availableApps.end(), [](const AppScene::AppCandidate& a, const AppScene::AppCandidate& b) {
        return AppScene::ToLower(a.displayName) < AppScene::ToLower(b.displayName);
    });
}

SceneSettingsWindow::~SceneSettingsWindow()
{
}

bool SceneSettingsWindow::Show(HWND parent, RendPopupPage* page, AppContext* ctx, std::function<void()> onChanged)
{
    if (g_sceneSettingsInstance || !page)
        return false;

    SceneSettingsWindow* win = new SceneSettingsWindow(page, ctx, std::move(onChanged));

    HMONITOR hm = parent
        ? MonitorFromWindow(parent, MONITOR_DEFAULTTONEAREST)
        : ([] {
              POINT pt;
              GetCursorPos(&pt);
              return MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
          }());

    float scale = DpiHelper::GetDpiScaleForMonitor(hm);
    int wPx = (int)(SCENE_DLG_W * scale);
    int hPx = (int)(SCENE_DLG_H * scale);

    int x = 0;
    int y = 0;
    if (parent && IsWindowVisible(parent))
    {
        RECT pr{};
        GetWindowRect(parent, &pr);
        x = pr.left + (pr.right - pr.left - wPx) / 2;
        y = pr.top + (pr.bottom - pr.top - hPx) / 2;
    }
    else
    {
        MONITORINFO mi{ sizeof(mi) };
        GetMonitorInfoW(hm, &mi);
        RECT wa = mi.rcWork;
        x = wa.left + (wa.right - wa.left - wPx) / 2;
        y = wa.top + (wa.bottom - wa.top - hPx) / 2;
    }

    if (parent)
        EnableWindow(parent, FALSE);

    win->Create(L"", WS_POPUP, WS_EX_TOOLWINDOW | WS_EX_TOPMOST, x, y, wPx, hPx, parent);
    if (!win->GetHWND())
    {
        if (parent)
            EnableWindow(parent, TRUE);
        delete win;
        return false;
    }

    SetWindowDisplayAffinitySafe(win->GetHWND());
    win->ApplySystemBackdrop();
    win->EnsureD2D();

    ShowWindow(win->GetHWND(), SW_SHOW);
    UpdateWindow(win->GetHWND());
    SetForegroundWindow(win->GetHWND());
    SetFocus(win->GetHWND());

    g_sceneSettingsInstance = win;

    MSG msg;
    HWND hWnd = win->GetHWND();
    while (IsWindow(hWnd) && GetMessageW(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (IsWindow(hWnd))
        DestroyWindow(hWnd);

    if (msg.message == WM_QUIT)
        PostQuitMessage((int)msg.wParam);

    bool ok = win->m_okPressed;
    if (ok && win->m_page)
    {
        win->m_page->sceneMode = win->m_sceneMode;
        win->m_page->sceneApps = win->m_selectedApps;
        win->m_page->sceneAvailableApps = win->BuildAvailableAppList();
        if (win->m_onChanged)
            win->m_onChanged();
    }

    g_sceneSettingsInstance = nullptr;
    delete win;

    if (parent)
    {
        EnableWindow(parent, TRUE);
        SetForegroundWindow(parent);
    }

    return ok;
}

void SceneSettingsWindow::EnsureFonts()
{
    if (m_tfTitle)
        return;

    UIStyle::Typography::CreateTextFormat(GetDWFactory(), &m_tfTitle, 13.0f, DWRITE_FONT_WEIGHT_MEDIUM, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    UIStyle::Typography::CreateTextFormat(GetDWFactory(), &m_tfLabel, 11.0f, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    UIStyle::Typography::CreateTextFormat(GetDWFactory(), &m_tfItem, 11.0f, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    UIStyle::Typography::CreateTextFormat(GetDWFactory(), &m_tfSmall, 9.0f, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    UIStyle::Typography::CreateTextFormat(GetDWFactory(), &m_tfButton, 10.0f, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
}

void SceneSettingsWindow::OnPaintContent(ID2D1HwndRenderTarget* rt)
{
    EnsureFonts();

    auto textBrush = GetOrCreateBrush(UIStyle::ThemeColor::TextNormal().d2d);
    auto mutedBrush = GetOrCreateBrush(UIStyle::ThemeColor::TextMuted().d2d);
    if (textBrush && m_tfTitle)
    {
        std::wstring title = L"场景设置";
        if (m_page && !m_page->name.empty())
            title += L" - " + m_page->name;
        rt->DrawTextW(title.c_str(), (UINT32)title.size(), m_tfTitle.Get(),
                      D2D1::RectF(20.0f, Y_TITLE, SCENE_DLG_W - 60.0f, Y_TITLE + 24.0f), textBrush.Get());
    }

    DrawCloseButton(rt);

    D2D1_COLOR_F sepClr = UIStyle::ThemeColor::TextNormal().d2d;
    sepClr.a = 0.10f;
    auto sepBrush = GetOrCreateBrush(sepClr);
    if (sepBrush)
    {
        rt->DrawLine(D2D1::Point2F(20.0f, 36.0f), D2D1::Point2F(SCENE_DLG_W - 20.0f, 36.0f), sepBrush.Get(), UIStyle::Metrics::ControlStroke());
        rt->DrawLine(D2D1::Point2F(20.0f, Y_BUTTONS - 12.0f), D2D1::Point2F(SCENE_DLG_W - 20.0f, Y_BUTTONS - 12.0f), sepBrush.Get(), UIStyle::Metrics::ControlStroke());
    }

    if (mutedBrush && m_tfLabel)
    {
        rt->DrawTextW(L"类型", 2, m_tfLabel.Get(), D2D1::RectF(20.0f, Y_MODE, 58.0f, Y_MODE + 28.0f), mutedBrush.Get());
        rt->DrawTextW(L"选定的应用", 5, m_tfLabel.Get(), D2D1::RectF(LEFT_X, Y_LIST_TOP, LEFT_X + COL_W, Y_LIST_TOP + 24.0f), mutedBrush.Get());
        rt->DrawTextW(L"应用场景可选", 6, m_tfLabel.Get(), D2D1::RectF(RIGHT_X, Y_LIST_TOP, RIGHT_X + COL_W, Y_LIST_TOP + 24.0f), mutedBrush.Get());
    }

    DrawModeDropDown(rt);
    DrawButton(rt, L"新增场景", D2D1::RectF(SCENE_DLG_W - 112.0f, Y_MODE, SCENE_DLG_W - 20.0f, Y_MODE + 28.0f), m_hoveredAddScene, false);
    DrawList(rt, ListSide::Selected);
    DrawList(rt, ListSide::Available);

    DrawButton(rt, L"确定", D2D1::RectF(SCENE_DLG_W - 176.0f, Y_BUTTONS, SCENE_DLG_W - 96.0f, Y_BUTTONS + 26.0f), m_hoveredOk, true);
    DrawButton(rt, L"取消", D2D1::RectF(SCENE_DLG_W - 88.0f, Y_BUTTONS, SCENE_DLG_W - 20.0f, Y_BUTTONS + 26.0f), m_hoveredCancel, false);
}

void SceneSettingsWindow::DrawCloseButton(ID2D1HwndRenderTarget* rt)
{
    float cx = SCENE_DLG_W - 25.0f;
    float cy = Y_TITLE + 10.0f;
    D2D1_COLOR_F clr = m_hoveredClose ? UIStyle::ThemeColor::DangerRed().d2d : UIStyle::ThemeColor::TextMuted().d2d;
    auto brush = GetOrCreateBrush(clr);
    if (brush)
    {
        rt->DrawLine(D2D1::Point2F(cx - 5, cy - 5), D2D1::Point2F(cx + 5, cy + 5), brush.Get(), 1.5f);
        rt->DrawLine(D2D1::Point2F(cx + 5, cy - 5), D2D1::Point2F(cx - 5, cy + 5), brush.Get(), 1.5f);
    }
}

void SceneSettingsWindow::DrawModeDropDown(ID2D1HwndRenderTarget* rt)
{
    D2D1_RECT_F rect = D2D1::RectF(62.0f, Y_MODE, 172.0f, Y_MODE + 28.0f);
    D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(rect, 5.0f, 5.0f);

    auto bg = GetOrCreateBrush(m_hoveredMode ? UIStyle::ThemeColor::ButtonBgHover().d2d : UIStyle::ThemeColor::ButtonBgNormal().d2d);
    auto border = GetOrCreateBrush(m_hoveredMode ? UIStyle::ThemeColor::ButtonBorderHover().d2d : UIStyle::ThemeColor::ButtonBorderNormal().d2d);
    if (bg) rt->FillRoundedRectangle(rr, bg.Get());
    if (border) rt->DrawRoundedRectangle(rr, border.Get(), UIStyle::Metrics::ControlStroke());

    std::wstring modeText = m_sceneMode == Model::PageSceneMode::Blacklist ? L"黑名单模式" : L"白名单模式";
    auto textBrush = GetOrCreateBrush(UIStyle::ThemeColor::TextNormal().d2d);
    if (textBrush && m_tfItem)
    {
        rt->DrawTextW(modeText.c_str(), (UINT32)modeText.size(), m_tfItem.Get(),
                      D2D1::RectF(rect.left + 10.0f, rect.top, rect.right - 30.0f, rect.bottom), textBrush.Get());
    }

    auto arrowBrush = GetOrCreateBrush(UIStyle::ThemeColor::TextMuted().d2d);
    if (arrowBrush)
    {
        float ax = rect.right - 18.0f;
        float ay = rect.top + 13.0f;
        rt->DrawLine(D2D1::Point2F(ax - 4.0f, ay - 2.0f), D2D1::Point2F(ax, ay + 3.0f), arrowBrush.Get(), 1.2f);
        rt->DrawLine(D2D1::Point2F(ax, ay + 3.0f), D2D1::Point2F(ax + 4.0f, ay - 2.0f), arrowBrush.Get(), 1.2f);
    }
}

void SceneSettingsWindow::DrawList(ID2D1HwndRenderTarget* rt, ListSide side)
{
    float x = side == ListSide::Selected ? LEFT_X : RIGHT_X;
    int scroll = side == ListSide::Selected ? m_selectedScroll : m_availableScroll;
    D2D1_RECT_F panel = D2D1::RectF(x, Y_ITEM_TOP - 8.0f, x + COL_W, Y_LIST_BOTTOM);
    D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(panel, 6.0f, 6.0f);

    auto bg = GetOrCreateBrush(UIStyle::ThemeColor::ButtonBgNormal().d2d);
    auto border = GetOrCreateBrush(UIStyle::ThemeColor::ButtonBorderNormal().d2d);
    if (bg) rt->FillRoundedRectangle(rr, bg.Get());
    if (border) rt->DrawRoundedRectangle(rr, border.Get(), UIStyle::Metrics::ControlStroke());

    int visibleCount = (int)((Y_LIST_BOTTOM - Y_ITEM_TOP) / ITEM_H);
    int totalCount = side == ListSide::Selected ? (int)m_selectedApps.size() : (int)m_availableApps.size();
    int end = (std::min)(totalCount, scroll + visibleCount);

    for (int i = scroll; i < end; ++i)
    {
        float rowY = Y_ITEM_TOP + (i - scroll) * ITEM_H;
        D2D1_RECT_F row = D2D1::RectF(x + 8.0f, rowY, x + COL_W - 8.0f, rowY + ITEM_H - 4.0f);
        bool hovered = (side == m_hoveredSide && i == m_hoveredItem);
        D2D1_ROUNDED_RECT rowRr = D2D1::RoundedRect(row, 4.0f, 4.0f);

        auto rowBg = GetOrCreateBrush(hovered ? UIStyle::ThemeColor::ButtonBgHover().d2d : D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.0f));
        if (rowBg) rt->FillRoundedRectangle(rowRr, rowBg.Get());

        std::wstring name;
        bool alreadySelected = false;
        if (side == ListSide::Selected)
        {
            name = SelectedDisplayName(m_selectedApps[i]);
        }
        else
        {
            const auto& app = m_availableApps[i];
            name = app.displayName.empty() ? AppScene::FriendlyNameForExe(app.exeName) : app.displayName;
            alreadySelected = IsSelected(app.exePath.empty() ? app.exeName : app.exePath);
        }

        D2D1_COLOR_F nameClr = UIStyle::ThemeColor::TextNormal().d2d;
        if (alreadySelected)
        {
            nameClr.a = 0.45f;
        }

        auto nameBrush = GetOrCreateBrush(nameClr);
        if (nameBrush && m_tfItem)
        {
            rt->DrawTextW(name.c_str(), (UINT32)name.size(), m_tfItem.Get(),
                          D2D1::RectF(row.left + 10.0f, row.top, row.right - 10.0f, row.bottom), nameBrush.Get());
        }
    }
}

void SceneSettingsWindow::DrawButton(ID2D1HwndRenderTarget* rt, const wchar_t* text, const D2D1_RECT_F& rect, bool hovered, bool accent)
{
    D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(rect, 4.0f, 4.0f);
    if (accent)
    {
        D2D1_COLOR_F bgClr = UIStyle::ThemeColor::Accent().d2d;
        bgClr.a = hovered ? 0.85f : 0.70f;
        auto bg = GetOrCreateBrush(bgClr);
        auto tb = GetOrCreateBrush(UIStyle::ThemeColor::TextOnAccent().d2d);
        if (bg) rt->FillRoundedRectangle(rr, bg.Get());
        if (tb && m_tfButton) rt->DrawTextW(text, (UINT32)wcslen(text), m_tfButton.Get(), rect, tb.Get());
    }
    else
    {
        auto bg = GetOrCreateBrush(hovered ? UIStyle::ThemeColor::ButtonBgHover().d2d : UIStyle::ThemeColor::ButtonBgNormal().d2d);
        auto border = GetOrCreateBrush(hovered ? UIStyle::ThemeColor::ButtonBorderHover().d2d : UIStyle::ThemeColor::ButtonBorderNormal().d2d);
        auto tb = GetOrCreateBrush(UIStyle::ThemeColor::TextNormal().d2d);
        if (bg) rt->FillRoundedRectangle(rr, bg.Get());
        if (border) rt->DrawRoundedRectangle(rr, border.Get(), UIStyle::Metrics::ControlStroke());
        if (tb && m_tfButton) rt->DrawTextW(text, (UINT32)wcslen(text), m_tfButton.Get(), rect, tb.Get());
    }
}

void SceneSettingsWindow::ShowModeMenu()
{
    HWND hWnd = GetHWND();
    if (!hWnd)
        return;

    POINT pt{ 62, (int)(Y_MODE + 30.0f) };
    float scale = DpiHelper::GetWindowScale(hWnd);
    POINT screenPt{ (int)(pt.x * scale), (int)(pt.y * scale) };
    ClientToScreen(hWnd, &screenPt);

    SceneSettingsWindow* self = this;
    std::vector<DropDownMenu::Item> items;
    items.push_back({ L"白名单模式", [self]() {
        self->m_sceneMode = Model::PageSceneMode::Whitelist;
        InvalidateRect(self->GetHWND(), nullptr, FALSE);
    } });
    items.push_back({ L"黑名单模式", [self]() {
        self->m_sceneMode = Model::PageSceneMode::Blacklist;
        InvalidateRect(self->GetHWND(), nullptr, FALSE);
    } });
    DropDownMenu::Show(hWnd, screenPt, items, m_appCtx, 110.0f);
}

bool SceneSettingsWindow::HitTestRect(POINT pt, const D2D1_RECT_F& rect) const
{
    return pt.x >= rect.left && pt.x <= rect.right && pt.y >= rect.top && pt.y <= rect.bottom;
}

bool SceneSettingsWindow::HitTestCloseButton(POINT pt) const
{
    return pt.x >= SCENE_DLG_W - 35 && pt.x <= SCENE_DLG_W - 15 && pt.y >= Y_TITLE && pt.y <= Y_TITLE + 20;
}

bool SceneSettingsWindow::HitTestModeDropDown(POINT pt) const
{
    return HitTestRect(pt, D2D1::RectF(62.0f, Y_MODE, 172.0f, Y_MODE + 28.0f));
}

bool SceneSettingsWindow::HitTestAddSceneButton(POINT pt) const
{
    return HitTestRect(pt, D2D1::RectF(SCENE_DLG_W - 112.0f, Y_MODE, SCENE_DLG_W - 20.0f, Y_MODE + 28.0f));
}

bool SceneSettingsWindow::HitTestOkButton(POINT pt) const
{
    return HitTestRect(pt, D2D1::RectF(SCENE_DLG_W - 176.0f, Y_BUTTONS, SCENE_DLG_W - 96.0f, Y_BUTTONS + 26.0f));
}

bool SceneSettingsWindow::HitTestCancelButton(POINT pt) const
{
    return HitTestRect(pt, D2D1::RectF(SCENE_DLG_W - 88.0f, Y_BUTTONS, SCENE_DLG_W - 20.0f, Y_BUTTONS + 26.0f));
}

bool SceneSettingsWindow::HitTestList(POINT pt, ListSide side) const
{
    float x = side == ListSide::Selected ? LEFT_X : RIGHT_X;
    return pt.x >= x && pt.x <= x + COL_W && pt.y >= Y_ITEM_TOP && pt.y <= Y_LIST_BOTTOM;
}

int SceneSettingsWindow::HitTestListItem(POINT pt, ListSide side) const
{
    if (!HitTestList(pt, side))
        return -1;

    int scroll = side == ListSide::Selected ? m_selectedScroll : m_availableScroll;
    int index = scroll + (int)((pt.y - Y_ITEM_TOP) / ITEM_H);
    int count = side == ListSide::Selected ? (int)m_selectedApps.size() : (int)m_availableApps.size();
    return (index >= 0 && index < count) ? index : -1;
}

int SceneSettingsWindow::MaxScroll(ListSide side) const
{
    int count = side == ListSide::Selected ? (int)m_selectedApps.size() : (int)m_availableApps.size();
    int visibleCount = (int)((Y_LIST_BOTTOM - Y_ITEM_TOP) / ITEM_H);
    int maxScroll = count - visibleCount;
    return maxScroll > 0 ? maxScroll : 0;
}

void SceneSettingsWindow::ClampScrolls()
{
    m_selectedScroll = (std::max)(0, (std::min)(m_selectedScroll, MaxScroll(ListSide::Selected)));
    m_availableScroll = (std::max)(0, (std::min)(m_availableScroll, MaxScroll(ListSide::Available)));
}

void SceneSettingsWindow::AddAvailableCandidate(const AppScene::AppCandidate& app)
{
    std::wstring key = app.exeName.empty() ? app.exePath : app.exeName;
    if (key.empty() || AppScene::IsDuplicateCandidate(m_availableApps, app.exePath, app.exeName))
        return;

    AppScene::AppCandidate candidate = app;
    if (candidate.displayName.empty())
        candidate.displayName = AppScene::FriendlyNameForExe(candidate.exeName.empty() ? AppScene::FileNameOf(candidate.exePath) : candidate.exeName);
    m_availableApps.push_back(std::move(candidate));
}

std::vector<std::wstring> SceneSettingsWindow::BuildAvailableAppList() const
{
    std::vector<std::wstring> apps;
    for (const auto& app : m_availableApps)
    {
        std::wstring stored = app.exeName.empty() ? app.exePath : app.exeName;
        if (stored.empty())
            continue;
        bool exists = false;
        for (const auto& saved : apps)
        {
            if (AppScene::ToLower(saved) == AppScene::ToLower(stored))
            {
                exists = true;
                break;
            }
        }
        if (!exists)
            apps.push_back(std::move(stored));
    }
    return apps;
}

void SceneSettingsWindow::CaptureAvailableScenes()
{
    auto capturedApps = AppScene::CollectForegroundApps();
    for (const auto& app : capturedApps)
    {
        AddAvailableCandidate(app);
    }
    std::sort(m_availableApps.begin(), m_availableApps.end(), [](const AppScene::AppCandidate& a, const AppScene::AppCandidate& b) {
        return AppScene::ToLower(a.displayName) < AppScene::ToLower(b.displayName);
    });
    ClampScrolls();
}

bool SceneSettingsWindow::IsSelected(const std::wstring& exePath) const
{
    std::wstring target = AppScene::ToLower(exePath);
    std::wstring targetName = AppScene::ToLower(AppScene::FileNameOf(exePath));
    for (const auto& app : m_selectedApps)
    {
        std::wstring stored = AppScene::ToLower(app);
        if (stored == target || stored == targetName)
            return true;
    }
    return false;
}

void SceneSettingsWindow::AddSelectedApp(const AppScene::AppCandidate& app)
{
    std::wstring stored = app.exeName.empty() ? app.exePath : app.exeName;
    if (stored.empty() || IsSelected(stored) || (!app.exePath.empty() && IsSelected(app.exePath)))
        return;

    m_selectedApps.push_back(stored);
    ClampScrolls();
}

void SceneSettingsWindow::RemoveSelectedApp(int index)
{
    if (index < 0 || index >= (int)m_selectedApps.size())
        return;

    m_selectedApps.erase(m_selectedApps.begin() + index);
    ClampScrolls();
}

std::wstring SceneSettingsWindow::SelectedDisplayName(const std::wstring& app) const
{
    std::wstring name = AppScene::FileNameOf(app);
    if (name.empty())
        name = app;
    return AppScene::FriendlyNameForExe(name);
}

LRESULT SceneSettingsWindow::HandleMessage(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_DESTROY:
        PostThreadMessageW(GetCurrentThreadId(), WM_NULL, 0, 0);
        return GlassWindow::HandleMessage(hWnd, uMsg, wParam, lParam);

    case WM_LBUTTONDOWN:
    {
        POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        float scale = DpiHelper::GetWindowScale(hWnd);
        pt.x = (int)(pt.x / scale);
        pt.y = (int)(pt.y / scale);

        if (HitTestCloseButton(pt) || HitTestCancelButton(pt))
        {
            PostMessageW(hWnd, WM_CLOSE, 0, 0);
            return 0;
        }
        if (HitTestOkButton(pt))
        {
            m_okPressed = true;
            PostMessageW(hWnd, WM_CLOSE, 0, 0);
            return 0;
        }
        if (HitTestModeDropDown(pt))
        {
            ShowModeMenu();
            return 0;
        }
        if (HitTestAddSceneButton(pt))
        {
            CaptureAvailableScenes();
            InvalidateRect(hWnd, nullptr, FALSE);
            return 0;
        }
        if (pt.y < 36.0f)
        {
            SetFocus(hWnd);
            ReleaseCapture();
            SendMessageW(hWnd, WM_SYSCOMMAND, SC_MOVE | HTCAPTION, 0);
            return 0;
        }
        return 0;
    }

    case WM_LBUTTONDBLCLK:
    {
        POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        float scale = DpiHelper::GetWindowScale(hWnd);
        pt.x = (int)(pt.x / scale);
        pt.y = (int)(pt.y / scale);

        int selectedHit = HitTestListItem(pt, ListSide::Selected);
        if (selectedHit >= 0)
        {
            RemoveSelectedApp(selectedHit);
            InvalidateRect(hWnd, nullptr, FALSE);
            return 0;
        }

        int availableHit = HitTestListItem(pt, ListSide::Available);
        if (availableHit >= 0 && availableHit < (int)m_availableApps.size())
        {
            AddSelectedApp(m_availableApps[availableHit]);
            InvalidateRect(hWnd, nullptr, FALSE);
            return 0;
        }
        return 0;
    }

    case WM_MOUSEMOVE:
    {
        POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        float scale = DpiHelper::GetWindowScale(hWnd);
        pt.x = (int)(pt.x / scale);
        pt.y = (int)(pt.y / scale);

        if (!m_trackMouse)
        {
            TRACKMOUSEEVENT tme{ sizeof(tme) };
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hWnd;
            TrackMouseEvent(&tme);
            m_trackMouse = true;
        }

        bool repaint = false;
        auto updateBool = [&](bool& state, bool value) {
            if (state != value)
            {
                state = value;
                repaint = true;
            }
        };

        updateBool(m_hoveredClose, HitTestCloseButton(pt));
        updateBool(m_hoveredMode, HitTestModeDropDown(pt));
        updateBool(m_hoveredAddScene, HitTestAddSceneButton(pt));
        updateBool(m_hoveredOk, HitTestOkButton(pt));
        updateBool(m_hoveredCancel, HitTestCancelButton(pt));

        ListSide side = ListSide::None;
        int item = -1;
        if (HitTestList(pt, ListSide::Selected))
        {
            side = ListSide::Selected;
            item = HitTestListItem(pt, side);
        }
        else if (HitTestList(pt, ListSide::Available))
        {
            side = ListSide::Available;
            item = HitTestListItem(pt, side);
        }

        if (side != m_hoveredSide || item != m_hoveredItem)
        {
            m_hoveredSide = side;
            m_hoveredItem = item;
            repaint = true;
        }

        if (repaint)
            InvalidateRect(hWnd, nullptr, FALSE);
        return 0;
    }

    case WM_MOUSELEAVE:
        m_trackMouse = false;
        m_hoveredSide = ListSide::None;
        m_hoveredItem = -1;
        m_hoveredMode = false;
        m_hoveredAddScene = false;
        m_hoveredOk = false;
        m_hoveredCancel = false;
        m_hoveredClose = false;
        InvalidateRect(hWnd, nullptr, FALSE);
        return 0;

    case WM_MOUSEWHEEL:
    {
        POINT screenPt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        POINT pt = screenPt;
        ScreenToClient(hWnd, &pt);
        float scale = DpiHelper::GetWindowScale(hWnd);
        pt.x = (int)(pt.x / scale);
        pt.y = (int)(pt.y / scale);

        int delta = GET_WHEEL_DELTA_WPARAM(wParam);
        int step = delta > 0 ? -1 : 1;
        bool handled = false;
        if (HitTestList(pt, ListSide::Selected))
        {
            m_selectedScroll += step;
            handled = true;
        }
        else if (HitTestList(pt, ListSide::Available))
        {
            m_availableScroll += step;
            handled = true;
        }
        if (handled)
        {
            ClampScrolls();
            InvalidateRect(hWnd, nullptr, FALSE);
            return 0;
        }
        break;
    }

    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE)
        {
            PostMessageW(hWnd, WM_CLOSE, 0, 0);
            return 0;
        }
        if (wParam == VK_RETURN)
        {
            m_okPressed = true;
            PostMessageW(hWnd, WM_CLOSE, 0, 0);
            return 0;
        }
        break;
    }

    return GlassWindow::HandleMessage(hWnd, uMsg, wParam, lParam);
}
