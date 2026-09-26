#include "SettingsPresetMenus.h"
#include "SettingsPage.h"
#include "SettingsPageLayout.h"
#include "IConfigWindow.h"
#include "DropDownMenu.h"
#include "PromptWindow.h"
#include "../DpiHelper.h"
#include <algorithm>
#include <cwctype>

// Preset menu and blacklist editor flows, split from SettingsPage.cpp.
// Definitions are SettingsPage members moved verbatim.

using namespace SettingsPageLayout;

std::wstring SettingsPresetMenus::TriggerPresetLabel(int type)
{
    switch (type)
    {
    case 0: return L"鼠标中键";
    case 1: return L"鼠标侧键 4";
    case 2: return L"鼠标侧键 5";
    case 3: return L"Ctrl + 中键";
    case 4: return L"Shift + 中键";
    case 5: return L"Alt + 中键";
    case 6: return L"Ctrl + 侧键 4";
    case 7: return L"Ctrl + 侧键 5";
    default: return L"未知预设";
    }
}

std::wstring SettingsPresetMenus::PopupAlignPresetLabel(int mode)
{
    switch (mode)
    {
    case 0: return L"鼠标居中";
    case 1: return L"鼠标左上";
    case 2: return L"屏幕居中";
    case 3: return L"屏幕右下";
    case 4: return L"屏幕中下";
    case 5: return L"屏幕左下";
    case 6: return L"屏幕左上";
    case 7: return L"屏幕中上";
    case 8: return L"屏幕右上";
    case 9: return L"屏幕中左";
    case 10: return L"屏幕中右";
    default: return L"未知预设";
    }
}

std::wstring SettingsPresetMenus::ToLowerCopy(std::wstring value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) {
        return (wchar_t)towlower(ch);
    });
    return value;
}

static void TrimInPlace(std::wstring& value)
{
    while (!value.empty() && iswspace(value.back()))
        value.pop_back();
    size_t start = 0;
    while (start < value.size() && iswspace(value[start]))
        ++start;
    if (start > 0)
        value.erase(0, start);
}

std::vector<std::wstring> SettingsPresetMenus::ParseTriggerBlacklistInput(const std::wstring& input)
{
    std::vector<std::wstring> result;
    std::wstring current;
    auto pushCurrent = [&]()
    {
        TrimInPlace(current);
        if (!current.empty())
        {
            std::wstring lower = ToLowerCopy(current);
            bool exists = false;
            for (const auto& item : result)
            {
                if (ToLowerCopy(item) == lower)
                {
                    exists = true;
                    break;
                }
            }
            if (!exists)
                result.push_back(current);
        }
        current.clear();
    };

    for (wchar_t ch : input)
    {
        if (ch == L';' || ch == L',' || ch == L'\xFF1B' || ch == L'\xFF0C' || ch == L'\x3001' || ch == L'\r' || ch == L'\n')
            pushCurrent();
        else
            current.push_back(ch);
    }
    pushCurrent();
    return result;
}

std::wstring SettingsPresetMenus::JoinTriggerBlacklistInput(const std::vector<std::wstring>& items)
{
    std::wstring result;
    for (const auto& item : items)
    {
        if (!result.empty())
            result += L"\r\n";
        result += item;
    }
    return result;
}

std::wstring SettingsPresetMenus::TriggerBlacklistSummary(const std::vector<std::wstring>& items)
{
    if (items.empty())
        return L"未设置";

    return L"已设置 " + std::to_wstring(items.size()) + L" 项";
}

void SettingsPage::ShowTriggerPresetMenu()
{
    if (!m_owner) return;

    HWND hwnd = m_owner->GetWindowHWND();
    if (!hwnd) return;

    std::vector<DropDownMenu::Item> items;
    const int currentTrigger = m_owner->GetTriggerType();
    auto addPreset = [&](int type)
    {
        std::wstring label = SettingsPresetMenus::TriggerPresetLabel(type);
        if (type == currentTrigger)
            label = L"当前：" + label;

        items.push_back(DropDownMenu::Item{
            label,
            [this, type]()
            {
                if (!m_owner) return;
                if (m_owner->GetTriggerType() != type)
                    m_owner->SetTriggerType(type);
                m_owner->NotifyConfigChanged();
                HWND ownerHwnd = m_owner->GetWindowHWND();
                if (ownerHwnd)
                    InvalidateRect(ownerHwnd, nullptr, FALSE);
            },
            false
        });
    };

    for (int type = 3; type <= 7; ++type)
        addPreset(type);

    D2D1_RECT_F presetRect = TriggerButtonRect(TRIGGER_PRESET_BUTTON);
    POINT menuPt{ (int)presetRect.left, (int)(presetRect.bottom + 6.0f) };
    menuPt = DpiHelper::LogicalClientToScreen(hwnd, menuPt);
    DropDownMenu::Show(hwnd, menuPt, items, m_owner->GetAppContext(), presetRect.right - presetRect.left, true, 10.5f);
}

void SettingsPage::ShowPopupAlignPresetMenu()
{
    if (!m_owner) return;

    HWND hwnd = m_owner->GetWindowHWND();
    if (!hwnd) return;

    std::vector<DropDownMenu::Item> items;
    const int currentMode = m_owner->GetPopupAlignMode();
    for (int mode = POPUP_ALIGN_PRESET_BUTTON; mode <= POPUP_ALIGN_PRESET_LAST; ++mode)
    {
        std::wstring label = SettingsPresetMenus::PopupAlignPresetLabel(mode);
        if (mode == currentMode)
            label = L"当前：" + label;

        items.push_back(DropDownMenu::Item{
            label,
            [this, mode]()
            {
                if (!m_owner) return;
                if (m_owner->GetPopupAlignMode() != mode)
                    m_owner->SetPopupAlignMode(mode);
                m_owner->NotifyConfigChanged();
                HWND ownerHwnd = m_owner->GetWindowHWND();
                if (ownerHwnd)
                    InvalidateRect(ownerHwnd, nullptr, FALSE);
            },
            false
        });
    }

    D2D1_RECT_F presetRect = PopupAlignRect(POPUP_ALIGN_PRESET_BUTTON);
    POINT menuPt{ (int)presetRect.left, (int)(presetRect.bottom + 6.0f) };
    menuPt = DpiHelper::LogicalClientToScreen(hwnd, menuPt);
    DropDownMenu::Show(hwnd, menuPt, items, m_owner->GetAppContext(), presetRect.right - presetRect.left, true, 10.5f);
}

void SettingsPage::ShowTriggerBlacklistEditor()
{
    if (!m_owner) return;

    std::wstring input = SettingsPresetMenus::JoinTriggerBlacklistInput(m_owner->GetTriggerBlacklist());
    const wchar_t* prompt = L"忽略大小写，支持模糊匹配。\n每行一个，也可用中英文逗号/分号分隔。";
    if (PromptWindow::ShowMultiline(
        m_owner->GetWindowHWND(),
        L"触发黑名单",
        prompt,
        input,
        input.c_str(),
        m_owner->GetAppContext()))
    {
        m_owner->SetTriggerBlacklist(SettingsPresetMenus::ParseTriggerBlacklistInput(input));
        m_owner->NotifyConfigChanged();
        if (HWND hwnd = m_owner->GetWindowHWND())
            InvalidateRect(hwnd, nullptr, FALSE);
    }
}
