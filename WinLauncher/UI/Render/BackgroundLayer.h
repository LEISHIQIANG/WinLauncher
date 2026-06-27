#pragma once
#include "IRenderLayer.h"
#include "../../Config/UIStyle.h"
#include <d2d1helper.h>
#include <dwmapi.h>
#include <vector>

#pragma comment(lib, "dwmapi.lib")

class BackgroundLayer : public IRenderLayer
{
public:
    BackgroundLayer(HWND hWnd) : m_hWnd(hWnd) {}

    void ApplySystemBackdrop()
    {
        MARGINS m{ -1, -1, -1, -1 };
        DwmExtendFrameIntoClientArea(m_hWnd, &m);

        DWORD corner = 2;
        DwmSetWindowAttribute(m_hWnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));

        DWORD border = 0xFFFFFFFE;
        DwmSetWindowAttribute(m_hWnd, 34, &border, sizeof(border));

        int backdropType = 1;
        for (int attr : {38, 1029})
        {
            if (SUCCEEDED(DwmSetWindowAttribute(m_hWnd, attr, &backdropType, sizeof(backdropType))))
                break;
        }

        auto u = GetModuleHandleW(L"user32.dll");
        if (u)
        {
            using SWCAFn = BOOL(WINAPI*)(HWND, void*);
            auto fn = (SWCAFn)GetProcAddress(u, "SetWindowCompositionAttribute");
            if (fn)
            {
                struct AccentPolicy { int s, f; unsigned int g; int a; };
                struct WinCompAttr { int attr; void* data; size_t size; };
                AccentPolicy ap{ 2, 2, 0x00000000, 0 };
                WinCompAttr d{ 19, &ap, sizeof(ap) };
                fn(m_hWnd, &d);
            }
        }
    }

    virtual void Render(ID2D1HwndRenderTarget* rt, ID2D1DeviceContext* dc,
                        const D2D1_SIZE_F& size, float scale) override
    {
        if (m_dirty)
        {
            CaptureBackground();
            m_dirty = false;
        }

        rt->Clear(UIStyle::ThemeColor::WindowClear().d2d);

        if (!m_bgCap) return;

        if (dc)
        {
            ComPtr<ID2D1Effect> blur;
            ComPtr<ID2D1Effect> sat;
            if (SUCCEEDED(dc->CreateEffect(CLSID_D2D1GaussianBlur, &blur)) &&
                SUCCEEDED(dc->CreateEffect(CLSID_D2D1Saturation, &sat)))
            {
                blur->SetInput(0, m_bgCap.Get());
                blur->SetValue(D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION, 20.0f);
                blur->SetValue(D2D1_GAUSSIANBLUR_PROP_BORDER_MODE, D2D1_BORDER_MODE_HARD);

                ComPtr<ID2D1Image> blurOut;
                blur->GetOutput(&blurOut);
                if (blurOut)
                {
                    sat->SetInput(0, blurOut.Get());
                    sat->SetValue(D2D1_SATURATION_PROP_SATURATION, 2.5f);
                    dc->DrawImage(sat.Get());
                }
                return;
            }
            dc->DrawImage(m_bgCap.Get());
        }
        else
        {
            rt->DrawBitmap(m_bgCap.Get(), D2D1::RectF(0, 0, size.width, size.height));
        }
    }

    virtual void OnResize(const D2D1_SIZE_F& size) override
    {
        MarkDirty();
    }

    void MarkDirty()
    {
        m_dirty = true;
        if (m_bgCap)
        {
            D2D1_SIZE_U cur = m_bgCap->GetPixelSize();
            RECT wr; GetWindowRect(m_hWnd, &wr);
            int w = wr.right - wr.left;
            int h = wr.bottom - wr.top;
            if (cur.width != (UINT32)w || cur.height != (UINT32)h)
            {
                m_bgCap.Reset();
            }
        }
    }

private:
    void CaptureBackground()
    {
        RECT wr;
        GetWindowRect(m_hWnd, &wr);
        int w = wr.right - wr.left;
        int h = wr.bottom - wr.top;
        if (w <= 0 || h <= 0) return;

        HDC sdc = GetDC(nullptr);
        HDC mdc = CreateCompatibleDC(sdc);
        HBITMAP bmp = CreateCompatibleBitmap(sdc, w, h);
        SelectObject(mdc, bmp);
        BitBlt(mdc, 0, 0, w, h, sdc, wr.left, wr.top, SRCCOPY);

        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = w;
        bi.bmiHeader.biHeight = -h;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;

        std::vector<DWORD> pixbuf(w * h);
        GetDIBits(mdc, bmp, 0, h, pixbuf.data(), &bi, DIB_RGB_COLORS);
        for (int i = 0; i < w * h; i++)
            pixbuf[i] |= 0xFF000000;

        DeleteObject(bmp);
        DeleteDC(mdc);
        ReleaseDC(nullptr, sdc);

        if (m_bgCap)
        {
            m_bgCap->CopyFromMemory(nullptr, pixbuf.data(), w * 4);
        }
        else
        {
            float dx = 96.0f, dy = 96.0f;
            auto rt = GetD2DRenderTarget();
            if (rt) rt->GetDpi(&dx, &dy);

            D2D1_BITMAP_PROPERTIES props = D2D1::BitmapProperties(
                D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), dx, dy);

            auto d2dRt = GetD2DRenderTarget();
            if (d2dRt)
            {
                d2dRt->CreateBitmap(D2D1::SizeU(w, h), pixbuf.data(), w * 4, &props, &m_bgCap);
            }
        }
    }

    ID2D1HwndRenderTarget* GetD2DRenderTarget()
    {
        return m_rt;
    }

    HWND m_hWnd;
    ComPtr<ID2D1Bitmap> m_bgCap;
    ID2D1HwndRenderTarget* m_rt = nullptr;

public:
    void SetRenderTarget(ID2D1HwndRenderTarget* rt) { m_rt = rt; }
};
