#include "ShortcutHistoryController.h"
#include "../ShortcutManager.h"
#include <d2d1.h>

namespace
{
    Model::ShortcutInfo ToSnapshotShortcut(const RendShortcutInfo& si)
    {
        Model::ShortcutInfo vs;
        vs.id = si.id;
        vs.name = si.name;
        vs.targetPath = si.targetPath;
        vs.arguments = si.arguments;
        vs.iconPath = si.iconPath;
        vs.runAsAdmin = si.runAsAdmin;
        vs.type = si.type;
        vs.targetKind = si.targetKind;
        vs.iconSource = si.iconSource;
        vs.builtinIconId = si.builtinIconId;
        vs.iconInvertLight = si.iconInvertLight;
        vs.iconInvertDark = si.iconInvertDark;
        return vs;
    }

    RendShortcutInfo ToRenderShortcut(const Model::ShortcutInfo& vs)
    {
        RendShortcutInfo si;
        si.id = vs.id;
        si.name = vs.name;
        si.targetPath = vs.targetPath;
        si.arguments = vs.arguments;
        si.iconPath = vs.iconPath;
        si.runAsAdmin = vs.runAsAdmin;
        si.type = vs.type;
        si.targetKind = vs.targetKind;
        si.iconSource = vs.iconSource;
        si.builtinIconId = vs.builtinIconId;
        si.iconInvertLight = vs.iconInvertLight;
        si.iconInvertDark = vs.iconInvertDark;
        si.hIcon = ShortcutManager::GetShortcutIcon(si);
        return si;
    }

    bool ShortcutEqual(const Model::ShortcutInfo& a, const Model::ShortcutInfo& b)
    {
        return a.id == b.id &&
            a.name == b.name &&
            a.targetPath == b.targetPath &&
            a.arguments == b.arguments &&
            a.iconPath == b.iconPath &&
            a.runAsAdmin == b.runAsAdmin &&
            a.type == b.type &&
            a.targetKind == b.targetKind &&
            a.iconSource == b.iconSource &&
            a.builtinIconId == b.builtinIconId &&
            a.iconInvertLight == b.iconInvertLight &&
            a.iconInvertDark == b.iconInvertDark;
    }

    bool ShortcutListEqual(const std::vector<Model::ShortcutInfo>& a, const std::vector<Model::ShortcutInfo>& b)
    {
        if (a.size() != b.size())
            return false;

        for (size_t i = 0; i < a.size(); i++)
        {
            if (!ShortcutEqual(a[i], b[i]))
                return false;
        }
        return true;
    }
}

ShortcutHistoryController::Snapshot ShortcutHistoryController::CaptureSnapshot(const std::vector<RendPopupPage>& pages, int currentCategory)
{
    Snapshot snapshot;
    snapshot.currentCategory = currentCategory;
    for (int pageIndex = 0; pageIndex < (int)pages.size(); pageIndex++)
    {
        const auto& page = pages[pageIndex];
        if (page.isSyncFolder)
            continue;

        PageState pageState;
        pageState.pageIndex = pageIndex;
        pageState.pageName = page.name;
        pageState.shortcuts.reserve(page.shortcuts.size());
        for (const auto& shortcut : page.shortcuts)
        {
            pageState.shortcuts.push_back(ToSnapshotShortcut(shortcut));
        }
        snapshot.pages.push_back(std::move(pageState));
    }
    return snapshot;
}

bool ShortcutHistoryController::SnapshotsEqual(const Snapshot& a, const Snapshot& b)
{
    if (a.currentCategory != b.currentCategory || a.pages.size() != b.pages.size())
        return false;

    for (size_t i = 0; i < a.pages.size(); i++)
    {
        if (a.pages[i].pageIndex != b.pages[i].pageIndex ||
            a.pages[i].pageName != b.pages[i].pageName ||
            !ShortcutListEqual(a.pages[i].shortcuts, b.pages[i].shortcuts))
        {
            return false;
        }
    }
    return true;
}

int ShortcutHistoryController::ResolvePageIndex(const PageState& pageState, const std::vector<RendPopupPage>& pages, std::vector<bool>& usedPages)
{
    if (pageState.pageIndex >= 0 && pageState.pageIndex < (int)pages.size() &&
        !usedPages[pageState.pageIndex] &&
        !pages[pageState.pageIndex].isSyncFolder &&
        pages[pageState.pageIndex].name == pageState.pageName)
    {
        usedPages[pageState.pageIndex] = true;
        return pageState.pageIndex;
    }

    for (int i = 0; i < (int)pages.size(); i++)
    {
        if (!usedPages[i] && !pages[i].isSyncFolder && pages[i].name == pageState.pageName)
        {
            usedPages[i] = true;
            return i;
        }
    }

    if (pageState.pageIndex >= 0 && pageState.pageIndex < (int)pages.size() &&
        !usedPages[pageState.pageIndex] &&
        !pages[pageState.pageIndex].isSyncFolder)
    {
        usedPages[pageState.pageIndex] = true;
        return pageState.pageIndex;
    }

    return -1;
}

void ShortcutHistoryController::RestorePage(RendPopupPage& page, const std::vector<Model::ShortcutInfo>& shortcuts)
{
    for (auto* bmp : page.iconBitmaps)
    {
        if (bmp) bmp->Release();
    }
    page.iconBitmaps.clear();
    ShortcutManager::FreeShortcuts(page.shortcuts);

    page.shortcuts.reserve(shortcuts.size());
    for (const auto& shortcut : shortcuts)
    {
        page.shortcuts.push_back(ToRenderShortcut(shortcut));
    }
}

void ShortcutHistoryController::RecordCheckpoint(const std::vector<RendPopupPage>& pages, int currentCategory, bool isSettingsMode)
{
    if (m_applying || isSettingsMode)
        return;

    Snapshot snapshot = CaptureSnapshot(pages, currentCategory);
    if (!m_undoHistory.empty() && SnapshotsEqual(m_undoHistory.back(), snapshot))
    {
        return;
    }

    m_undoHistory.push_back(std::move(snapshot));
    while (m_undoHistory.size() > SHORTCUT_HISTORY_LIMIT)
    {
        m_undoHistory.pop_front();
    }
    m_redoHistory.clear();
}

void ShortcutHistoryController::Clear()
{
    m_undoHistory.clear();
    m_redoHistory.clear();
}

bool ShortcutHistoryController::Undo(std::vector<RendPopupPage>& pages, int& currentCategory,
                                    const std::function<void()>& beforeRestore,
                                    const std::function<void()>& afterRestore)
{
    if (m_applying)
        return false;

    Snapshot current = CaptureSnapshot(pages, currentCategory);
    while (!m_undoHistory.empty() && SnapshotsEqual(m_undoHistory.back(), current))
    {
        m_undoHistory.pop_back();
    }

    if (m_undoHistory.empty())
        return false;

    Snapshot previous = std::move(m_undoHistory.back());
    m_undoHistory.pop_back();

    m_redoHistory.push_back(std::move(current));
    while (m_redoHistory.size() > SHORTCUT_HISTORY_LIMIT)
    {
        m_redoHistory.pop_front();
    }

    ApplySnapshot(previous, pages, currentCategory, beforeRestore, afterRestore);
    return true;
}

bool ShortcutHistoryController::Redo(std::vector<RendPopupPage>& pages, int& currentCategory,
                                    const std::function<void()>& beforeRestore,
                                    const std::function<void()>& afterRestore)
{
    if (m_applying)
        return false;

    Snapshot current = CaptureSnapshot(pages, currentCategory);
    while (!m_redoHistory.empty() && SnapshotsEqual(m_redoHistory.back(), current))
    {
        m_redoHistory.pop_back();
    }

    if (m_redoHistory.empty())
        return false;

    Snapshot next = std::move(m_redoHistory.back());
    m_redoHistory.pop_back();

    m_undoHistory.push_back(std::move(current));
    while (m_undoHistory.size() > SHORTCUT_HISTORY_LIMIT)
    {
        m_undoHistory.pop_front();
    }

    ApplySnapshot(next, pages, currentCategory, beforeRestore, afterRestore);
    return true;
}

void ShortcutHistoryController::ApplySnapshot(const Snapshot& snapshot, std::vector<RendPopupPage>& pages, int& currentCategory,
                                             const std::function<void()>& beforeRestore,
                                             const std::function<void()>& afterRestore)
{
    struct ApplyGuard
    {
        bool& flag;
        explicit ApplyGuard(bool& value) : flag(value) { flag = true; }
        ~ApplyGuard() { flag = false; }
    } guard(m_applying);

    if (beforeRestore)
    {
        beforeRestore();
    }

    std::vector<bool> usedPages(pages.size(), false);
    for (const auto& pageState : snapshot.pages)
    {
        int pageIndex = ResolvePageIndex(pageState, pages, usedPages);
        if (pageIndex >= 0 && pageIndex < (int)pages.size())
        {
            std::vector<Model::ShortcutInfo> currentShortcuts;
            currentShortcuts.reserve(pages[pageIndex].shortcuts.size());
            for (const auto& shortcut : pages[pageIndex].shortcuts)
            {
                currentShortcuts.push_back(ToSnapshotShortcut(shortcut));
            }
            if (ShortcutListEqual(currentShortcuts, pageState.shortcuts))
            {
                continue;
            }

            RestorePage(pages[pageIndex], pageState.shortcuts);
        }
    }

    currentCategory = snapshot.currentCategory;
    if (currentCategory >= (int)pages.size())
        currentCategory = (int)pages.size() - 1;
    if (currentCategory < 0 && !pages.empty())
        currentCategory = 0;

    if (afterRestore)
    {
        afterRestore();
    }
}
