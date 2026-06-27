#pragma once
#include "IRenderLayer.h"
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
        D2D1_SIZE_F size = rt->GetSize();

        ID2D1DeviceContext* dc = nullptr;
        rt->QueryInterface(&dc);

        for (auto& layer : m_layers)
        {
            layer->Render(rt, dc, size, scale);
        }

        if (dc) dc->Release();
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
    std::vector<std::unique_ptr<IRenderLayer>> m_layers;
    bool m_needsRecreate = false;
};
