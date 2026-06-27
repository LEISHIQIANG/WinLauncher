#pragma once
#include <d2d1.h>
#include <d2d1_1.h>
#include <wrl.h>

using Microsoft::WRL::ComPtr;

class IRenderLayer
{
public:
    virtual ~IRenderLayer() = default;

    virtual void Render(ID2D1HwndRenderTarget* rt, ID2D1DeviceContext* dc,
                        const D2D1_SIZE_F& size, float scale) = 0;

    virtual void OnResize(const D2D1_SIZE_F& size) = 0;
    virtual void MarkDirty() { m_dirty = true; }
    virtual bool IsDirty() const { return m_dirty; }
    virtual void ClearDirty() { m_dirty = false; }

protected:
    bool m_dirty = true;
};
