#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl.h>
#include <string>
#include <cmath>
#include <algorithm>
#include <vector>
#include "../../Config/UIStyle.h"
#include "../../App/Logger.h"

using Microsoft::WRL::ComPtr;

class IconRenderer
{
public:
    static D2D1_RECT_F AlignToPixels(ID2D1RenderTarget* rt, float x, float y, float w, float h)
    {
        if (!rt)
            return D2D1::RectF(x, y, x + w, y + h);

        float dpiX = 96.0f;
        float dpiY = 96.0f;
        rt->GetDpi(&dpiX, &dpiY);
        float scaleX = dpiX / 96.0f;
        float scaleY = dpiY / 96.0f;

        float alignedX = std::round(x * scaleX) / scaleX;
        float alignedY = std::round(y * scaleY) / scaleY;
        float alignedR = std::round((x + w) * scaleX) / scaleX;
        float alignedB = std::round((y + h) * scaleY) / scaleY;

        return D2D1::RectF(alignedX, alignedY, alignedR, alignedB);
    }

    static int GetRecommendedBitmapSize(ID2D1RenderTarget* rt, float logicalSize)
    {
        if (logicalSize <= 0.0f)
            return 1;

        float dpiX = 96.0f;
        float dpiY = 96.0f;
        if (rt)
        {
            rt->GetDpi(&dpiX, &dpiY);
        }

        const float dpiScale = (std::max)(dpiX, dpiY) / 96.0f;
        const int dpiPixels = static_cast<int>(std::ceil(logicalSize * dpiScale));
        const int oversampledPixels = static_cast<int>(std::ceil(logicalSize * dpiScale * 2.0f));
        const int minPixels = static_cast<int>(std::ceil(logicalSize * 2.0f));
        int result = (std::max)({ dpiPixels, oversampledPixels, minPixels, 32 });
        result = (result + 1) & ~1;
        return (std::min)(result, 512);
    }

    static ComPtr<ID2D1Bitmap> HicontoD2D(ID2D1HwndRenderTarget* rt, HICON hIcon, int size = 48, bool invert = false)
    {
        if (!rt)
        {
            LOG_G_ERRA(L"HicontoD2D: rt is null");
            return nullptr;
        }
        if (size <= 0 || size > 512)
        {
            LOG_G_ERRA(L"HicontoD2D: invalid size=%d", size);
            return nullptr;
        }
        HICON hi = hIcon ? hIcon : LoadIcon(nullptr, IDI_APPLICATION);
        const int SZ = size;

        BITMAPINFO bmi{};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = SZ;
        bmi.bmiHeader.biHeight = -SZ;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        void* bits = nullptr;
        HDC cdc = CreateCompatibleDC(nullptr);
        if (!cdc)
        {
            LOG_G_ERRA(L"HicontoD2D: CreateCompatibleDC failed, size=%d, err=%d", SZ, GetLastError());
            return nullptr;
        }

        HBITMAP dib = CreateDIBSection(cdc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (!dib || !bits)
        {
            LOG_G_ERRA(L"HicontoD2D: CreateDIBSection failed, size=%d, dib=%p, bits=%p, err=%d",
                SZ, (void*)dib, bits, GetLastError());
            DeleteDC(cdc);
            return nullptr;
        }

        HGDIOBJ oldObj = SelectObject(cdc, dib);
        ZeroMemory(bits, SZ * SZ * 4);
        if (!DrawIconEx(cdc, 0, 0, hi, SZ, SZ, 0, nullptr, DI_NORMAL))
        {
            LOG_G_WORNING(L"HicontoD2D: DrawIconEx returned FALSE, hIcon=%p, size=%d, err=%d",
                (void*)hi, SZ, GetLastError());
        }
        GdiFlush();

        DWORD* p = (DWORD*)bits;

        // Check if GDI wrote any non-zero alpha (which indicates a 32-bit alpha icon)
        bool hasAlpha = false;
        for (int i = 0; i < SZ * SZ; i++)
        {
            if (((p[i] >> 24) & 0xFF) != 0)
            {
                hasAlpha = true;
                break;
            }
        }

        // If no alpha was written, reconstruct alpha channel using the AND mask stretched to SZ * SZ
        if (!hasAlpha)
        {
            HDC monoDC = CreateCompatibleDC(cdc);
            if (monoDC)
            {
                BITMAPINFO monoBmi{};
                monoBmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                monoBmi.bmiHeader.biWidth = SZ;
                monoBmi.bmiHeader.biHeight = -SZ; // top-down
                monoBmi.bmiHeader.biPlanes = 1;
                monoBmi.bmiHeader.biBitCount = 1; // 1-bit monochrome
                monoBmi.bmiHeader.biCompression = BI_RGB;

                void* monoBits = nullptr;
                HBITMAP monoDib = CreateDIBSection(monoDC, &monoBmi, DIB_RGB_COLORS, &monoBits, nullptr, 0);
                if (monoDib && monoBits)
                {
                    HGDIOBJ oldMono = SelectObject(monoDC, monoDib);
                    int monoRowWidth = ((SZ + 31) & ~31) / 8;
                    memset(monoBits, 0xFF, monoRowWidth * SZ);

                    if (DrawIconEx(monoDC, 0, 0, hi, SZ, SZ, 0, nullptr, DI_MASK))
                    {
                        GdiFlush();
                        BYTE* mBytes = (BYTE*)monoBits;
                        for (int y = 0; y < SZ; y++)
                        {
                            for (int x = 0; x < SZ; x++)
                            {
                                int byteIdx = y * monoRowWidth + (x / 8);
                                int bitIdx = 7 - (x % 8);
                                bool isTransparent = (mBytes[byteIdx] & (1 << bitIdx)) != 0;
                                int i = y * SZ + x;
                                if (isTransparent)
                                {
                                    p[i] = 0; // transparent black
                                }
                                else
                                {
                                    p[i] |= 0xFF000000; // set alpha to 255 (opaque)
                                }
                            }
                        }
                    }
                    SelectObject(monoDC, oldMono);
                    DeleteObject(monoDib);
                }
                DeleteDC(monoDC);
            }
        }

        bool sourceLooksStraightAlpha = false;
        for (int i = 0; i < SZ * SZ; i++)
        {
            BYTE a = (p[i] >> 24) & 0xFF;
            if (a == 0 || a == 0xFF)
                continue;
            BYTE r = (p[i] >> 16) & 0xFF;
            BYTE g = (p[i] >> 8) & 0xFF;
            BYTE b = p[i] & 0xFF;
            if (r > a || g > a || b > a)
            {
                sourceLooksStraightAlpha = true;
                break;
            }
        }

        for (int i = 0; i < SZ * SZ; i++)
        {
            BYTE a = (p[i] >> 24) & 0xFF;
            BYTE r = (p[i] >> 16) & 0xFF;
            BYTE g = (p[i] >> 8) & 0xFF;
            BYTE b = p[i] & 0xFF;

            if (invert)
            {
                if (a > 0)
                {
                    BYTE sr = sourceLooksStraightAlpha ? r : (BYTE)(std::min)(255, (int)r * 255 / (int)a);
                    BYTE sg = sourceLooksStraightAlpha ? g : (BYTE)(std::min)(255, (int)g * 255 / (int)a);
                    BYTE sb = sourceLooksStraightAlpha ? b : (BYTE)(std::min)(255, (int)b * 255 / (int)a);

                    sr = 255 - sr;
                    sg = 255 - sg;
                    sb = 255 - sb;

                    r = (BYTE)((sr * a) / 255);
                    g = (BYTE)((sg * a) / 255);
                    b = (BYTE)((sb * a) / 255);
                }
                else
                {
                    r = 0;
                    g = 0;
                    b = 0;
                }
            }
            else if (a == 0)
            {
                r = 0;
                g = 0;
                b = 0;
            }
            else if (sourceLooksStraightAlpha)
            {
                r = (BYTE)((r * a) / 255);
                g = (BYTE)((g * a) / 255);
                b = (BYTE)((b * a) / 255);
            }

            p[i] = (a << 24) | (r << 16) | (g << 8) | b;
        }

        D2D1_BITMAP_PROPERTIES props = D2D1::BitmapProperties(
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96.0f, 96.0f);
        ComPtr<ID2D1Bitmap> bmp;
        HRESULT hr = rt->CreateBitmap(D2D1::SizeU(SZ, SZ), bits, SZ * 4, &props, &bmp);
        if (FAILED(hr))
        {
            LOG_G_ERRA(L"HicontoD2D: CreateBitmap failed, size=%d, hr=0x%08X, err=%d",
                SZ, hr, GetLastError());
            SelectObject(cdc, oldObj);
            DeleteObject(dib);
            DeleteDC(cdc);
            return nullptr;
        }

        SelectObject(cdc, oldObj);
        DeleteObject(dib);
        DeleteDC(cdc);
        return bmp;
    }

    static ComPtr<ID2D1Bitmap> CreateDefaultIcon(ID2D1HwndRenderTarget* rt, IDWriteFactory* dwFactory, const std::wstring& name, int size = 96)
    {
        if (!rt) return nullptr;

        ComPtr<ID2D1BitmapRenderTarget> bmpRt;
        HRESULT hr = rt->CreateCompatibleRenderTarget(D2D1::SizeF(static_cast<float>(size), static_cast<float>(size)), &bmpRt);
        if (FAILED(hr)) return nullptr;

        bmpRt->BeginDraw();
        bmpRt->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));

        float hue = 0.0f;
        if (!name.empty())
        {
            size_t hashVal = std::hash<std::wstring>{}(name);
            hue = static_cast<float>(hashVal % 360);
        }
        else
        {
            hue = static_cast<float>(rand() % 360);
        }

        D2D1_COLOR_F bgColor = HSLToRGB(hue, 0.65f, 0.55f);

        ComPtr<ID2D1SolidColorBrush> bgBrush;
        bmpRt->CreateSolidColorBrush(bgColor, &bgBrush);

        float padding = static_cast<float>(size) * 0.02f;
        D2D1_RECT_F rect = D2D1::RectF(padding, padding, size - padding, size - padding);
        float radius = static_cast<float>(size) * 0.15f;
        D2D1_ROUNDED_RECT roundedRect = D2D1::RoundedRect(rect, radius, radius);

        if (bgBrush)
        {
            bmpRt->FillRoundedRectangle(roundedRect, bgBrush.Get());
        }

        std::wstring text = L"";
        if (name.length() >= 2)
        {
            text = name.substr(0, 2);
        }
        else if (name.length() == 1)
        {
            text = name;
        }
        else
        {
            text = L"??";
        }

        ComPtr<IDWriteFactory> localDwFactory = dwFactory;
        if (!localDwFactory)
        {
            DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(localDwFactory.GetAddressOf()));
        }

        if (localDwFactory)
        {
            ComPtr<IDWriteTextFormat> textFormat;
            float fontSize = static_cast<float>(size) * 0.35f;
            UIStyle::Typography::CreateTextFormat(
                localDwFactory.Get(),
                &textFormat,
                fontSize,
                DWRITE_FONT_WEIGHT_SEMI_BOLD,
                DWRITE_TEXT_ALIGNMENT_CENTER,
                DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

            if (textFormat)
            {
                ComPtr<ID2D1SolidColorBrush> textBrush;
                bmpRt->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White), &textBrush);

                if (textBrush)
                {
                    bmpRt->DrawTextW(
                        text.c_str(),
                        static_cast<UINT32>(text.length()),
                        textFormat.Get(),
                        rect,
                        textBrush.Get()
                    );
                }
            }
        }

        bmpRt->EndDraw();

        ComPtr<ID2D1Bitmap> bitmap;
        bmpRt->GetBitmap(&bitmap);
        return bitmap;
    }

private:
    static D2D1_COLOR_F HSLToRGB(float h, float s, float l)
    {
        float c = (1.0f - fabsf(2.0f * l - 1.0f)) * s;
        float x = c * (1.0f - fabsf(fmodf(h / 60.0f, 2.0f) - 1.0f));
        float m = l - c / 2.0f;

        float r = 0, g = 0, b = 0;
        if (h >= 0 && h < 60) { r = c; g = x; b = 0; }
        else if (h >= 60 && h < 120) { r = x; g = c; b = 0; }
        else if (h >= 120 && h < 180) { r = 0; g = c; b = x; }
        else if (h >= 180 && h < 240) { r = 0; g = x; b = c; }
        else if (h >= 240 && h < 300) { r = x; g = 0; b = c; }
        else if (h >= 300 && h < 360) { r = c; g = 0; b = x; }

        return D2D1::ColorF(r + m, g + m, b + m, 1.0f);
    }
};
