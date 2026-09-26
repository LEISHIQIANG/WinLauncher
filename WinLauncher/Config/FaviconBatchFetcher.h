#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d2d1.h>
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include "../App/BackgroundTaskService.h"
#include "../Model/ShortcutInfo.h"
#include "../ShortcutManager.h"

class IConfigWindow;
class IIconService;

class FaviconBatchFetcher
{
public:
    struct HostContext
    {
        IConfigWindow* owner = nullptr;
        RendPopupPage* pageData = nullptr;
        IIconService* iconService = nullptr;
        std::function<ID2D1Bitmap*(const RendShortcutInfo&)> createBitmap;
        std::function<std::vector<int>(const std::vector<int>&)> normalizeIndices;
    };

    FaviconBatchFetcher();
    ~FaviconBatchFetcher();

    void Cancel();
    void Detach();
    void StartFetch(const HostContext& host, const std::vector<int>& indices);

private:
    struct State;
    std::shared_ptr<State> m_state;
    std::vector<BackgroundTaskService::TaskHandle> m_tasks;
    uint64_t m_generation = 0;
    int m_pending = 0;
    int m_applied = 0;
    bool m_changed = false;
    bool m_historyRecorded = false;

    HostContext m_activeHost;

    void ApplyResult(uint64_t generation, int index, const std::wstring& shortcutId,
                     const std::wstring& url, const std::wstring& iconPath);
    void FinishFetch();
};
