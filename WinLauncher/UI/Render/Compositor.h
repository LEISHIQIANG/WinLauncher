#pragma once
#include "IRenderLayer.h"
#include "../../App/Logger.h"
#include "BackgroundLayer.h"
#include "OverlayLayer.h"
#include <vector>
#include <memory>

class Compositor
{
public:
    Compositor() = default;

    void AddLayer(std::unique_ptr<IRenderLayer> layer)
    {
        m_layers.push_back(std::move(layer));
    }

    void Render(ID2D1HwndRenderTarget* rt, float scale)
    {
        if (!rt) return;
        double renderStartMs = PerfNowMs();
        D2D1_SIZE_F size = rt->GetSize();

        ID2D1DeviceContext* dc = nullptr;
        HRESULT dcHr = rt->QueryInterface(&dc);
        if (FAILED(dcHr))
        {
            static ULONGLONG s_lastDcLogTick = 0;
            if (Logger::ShouldLogEvery(s_lastDcLogTick, 5000))
            {
                LOG_G_WARNING_NODE(L"render.compositor", L"device_context_unavailable", L"hr=0x%08X hwnd=%p", dcHr, rt->GetHwnd());
            }
        }

        for (auto& layer : m_layers)
        {
            layer->Render(rt, dc, size, scale);
        }

        if (dc) dc->Release();

        double elapsedMs = PerfNowMs() - renderStartMs;
        static ULONGLONG s_lastRenderLogTick = 0;
        if (Logger::ShouldLogElapsed(s_lastRenderLogTick, elapsedMs, 16.0, 1000))
        {
            LOG_G_WARNING_NODE(
                L"render.compositor",
                L"render_slow",
                L"elapsedMs=%.2f thresholdMs=16.00 layers=%zu size=%.0fx%.0f scale=%.2f hwnd=%p",
                elapsedMs,
                m_layers.size(),
                size.width,
                size.height,
                scale,
                rt->GetHwnd());
        }
    }

    void OnResize(const D2D1_SIZE_F& size)
    {
        for (auto& layer : m_layers)
        {
            layer->OnResize(size);
        }
    }

    void MarkAllDirty()
    {
        for (auto& layer : m_layers)
        {
            layer->MarkDirty();
        }
    }

    bool NeedsRecreate() const { return m_needsRecreate; }
    void ClearRecreateFlag() { m_needsRecreate = false; }

    BackgroundLayer* GetBackgroundLayer()
    {
        for (auto& layer : m_layers)
        {
            auto* bg = dynamic_cast<BackgroundLayer*>(layer.get());
            if (bg) return bg;
        }
        return nullptr;
    }

private:
    static double PerfNowMs()
    {
        static double freq = 0.0;
        if (freq == 0.0)
        {
            LARGE_INTEGER li;
            QueryPerformanceFrequency(&li);
            freq = (double)li.QuadPart;
        }
        LARGE_INTEGER li;
        QueryPerformanceCounter(&li);
        return ((double)li.QuadPart * 1000.0) / freq;
    }

    std::vector<std::unique_ptr<IRenderLayer>> m_layers;
    bool m_needsRecreate = false;
};
