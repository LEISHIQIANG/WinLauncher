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

class ConfigViewModel
{
public:
    explicit ConfigViewModel(AppContext* ctx)
        : m_ctx(ctx)
    {
        ReloadConfig();
    }

    void ReloadConfig()
    {
        if (m_ctx && m_ctx->configService)
        {
            m_pages = m_ctx->configService->LoadConfig();
        }

        if (m_currentCategory >= (int)m_pages.size())
            m_currentCategory = (int)m_pages.size() - 1;
        if (m_currentCategory < 0 && !m_pages.empty())
            m_currentCategory = 0;
    }

    void SaveConfig()
    {
        if (m_ctx && m_ctx->configService)
        {
            m_ctx->configService->SaveConfig(m_pages);
        }
        if (m_ctx && m_ctx->eventBus)
        {
            m_ctx->eventBus->Publish(EventType::ConfigChanged);
        }
    }

    std::vector<PopupPage>& GetPages() { return m_pages; }
    const std::vector<PopupPage>& GetPages() const { return m_pages; }

    PopupPage* GetCurrentPageData()
    {
        if (m_currentCategory >= 0 && m_currentCategory < (int)m_pages.size())
            return &m_pages[m_currentCategory];
        return nullptr;
    }

    int GetCurrentCategory() const { return m_currentCategory; }

    void SetCurrentCategory(int index)
    {
        if (index >= 0 && index < (int)m_pages.size())
        {
            m_currentCategory = index;
        }
    }

    size_t GetCategoryCount() const { return m_pages.size(); }
    std::wstring GetCategoryName(size_t index) const
    {
        if (index < m_pages.size()) return m_pages[index].name;
        return L"";
    }

    void AddCategory(const std::wstring& name)
    {
        if (name.empty()) return;
        PopupPage page;
        page.name = name;
        m_pages.push_back(std::move(page));
        m_currentCategory = (int)m_pages.size() - 1;
        SaveConfig();
    }

    void DeleteCategory(int index)
    {
        if (index < 0 || index >= (int)m_pages.size()) return;
        m_pages.erase(m_pages.begin() + index);

        if (m_currentCategory >= (int)m_pages.size())
            m_currentCategory = (int)m_pages.size() - 1;
        if (m_currentCategory < 0 && !m_pages.empty())
            m_currentCategory = 0;

        SaveConfig();
    }

    void AddShortcut(const ShortcutInfo& sc)
    {
        if (m_currentCategory >= 0 && m_currentCategory < (int)m_pages.size())
        {
            m_pages[m_currentCategory].shortcuts.push_back(sc);
            SaveConfig();
        }
    }

    void DeleteShortcut(int pageIndex, int shortcutIndex)
    {
        if (pageIndex < 0 || pageIndex >= (int)m_pages.size()) return;
        auto& shortcuts = m_pages[pageIndex].shortcuts;
        if (shortcutIndex < 0 || shortcutIndex >= (int)shortcuts.size()) return;
        shortcuts.erase(shortcuts.begin() + shortcutIndex);
        SaveConfig();
    }

    void MoveShortcut(int pageIndex, int from, int to)
    {
        if (pageIndex < 0 || pageIndex >= (int)m_pages.size()) return;
        auto& shortcuts = m_pages[pageIndex].shortcuts;
        if (from < 0 || from >= (int)shortcuts.size()) return;
        if (to < 0 || to >= (int)shortcuts.size()) return;
        if (from == to) return;

        auto sc = std::move(shortcuts[from]);
        shortcuts.erase(shortcuts.begin() + from);
        shortcuts.insert(shortcuts.begin() + to, std::move(sc));
        SaveConfig();
    }

private:
    AppContext* m_ctx;
    std::vector<PopupPage> m_pages;
    int m_currentCategory = 0;
};
