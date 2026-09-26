#pragma once
#include "../ShortcutManager.h"
#include "../Model/ShortcutInfo.h"
#include <vector>
#include <string>
#include <deque>
#include <functional>

class ShortcutHistoryController
{
public:
    static constexpr size_t SHORTCUT_HISTORY_LIMIT = 5;

    struct PageState
    {
        int pageIndex = -1;
        std::wstring pageName;
        std::vector<Model::ShortcutInfo> shortcuts;
    };

    struct Snapshot
    {
        std::vector<PageState> pages;
        int currentCategory = 0;
    };

    void RecordCheckpoint(const std::vector<RendPopupPage>& pages, int currentCategory, bool isSettingsMode);
    bool Undo(std::vector<RendPopupPage>& pages, int& currentCategory,
              const std::function<void()>& beforeRestore = nullptr,
              const std::function<void()>& afterRestore = nullptr);
    bool Redo(std::vector<RendPopupPage>& pages, int& currentCategory,
              const std::function<void()>& beforeRestore = nullptr,
              const std::function<void()>& afterRestore = nullptr);
    void Clear();
    bool IsApplying() const { return m_applying; }

    static Snapshot CaptureSnapshot(const std::vector<RendPopupPage>& pages, int currentCategory);
    static bool SnapshotsEqual(const Snapshot& a, const Snapshot& b);
    static void RestorePage(RendPopupPage& page, const std::vector<Model::ShortcutInfo>& shortcuts);
    static int ResolvePageIndex(const PageState& pageState, const std::vector<RendPopupPage>& pages, std::vector<bool>& usedPages);

private:
    void ApplySnapshot(const Snapshot& snapshot, std::vector<RendPopupPage>& pages, int& currentCategory,
                       const std::function<void()>& beforeRestore,
                       const std::function<void()>& afterRestore);

    std::deque<Snapshot> m_undoHistory;
    std::deque<Snapshot> m_redoHistory;
    bool m_applying = false;
};
