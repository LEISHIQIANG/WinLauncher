#pragma once
#include <Windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl.h>
#include <string>
#include <cmath>
#include "../../Config/UIStyle.h"

using Microsoft::WRL::ComPtr;

class IconRenderer
{
public:
    static ComPtr<ID2D1Bitmap> HicontoD2D(ID2D1HwndRenderTarget* rt, HICON hIcon, int size = 48)
    {
        if (!rt) return nullptr;
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
        HBITMAP dib = CreateDIBSection(cdc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
        SelectObject(cdc, dib);
        DrawIconEx(cdc, 0, 0, hi, SZ, SZ, 0, nullptr, DI_NORMAL);
        GdiFlush();

        DWORD* p = (DWORD*)bits;
        for (int i = 0; i < SZ * SZ; i++)
        {
            BYTE a = p[i] >> 24;
            if (a > 0 && a < 255)
            {
                p[i] = (a << 24)
                    | ((((p[i] >> 16) & 0xFF) * a / 255) << 16)
                    | ((((p[i] >> 8) & 0xFF) * a / 255) << 8)
                    | ((p[i] & 0xFF) * a / 255);
            }
        }

        D2D1_BITMAP_PROPERTIES props = D2D1::BitmapProperties(
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96.0f, 96.0f);
        ComPtr<ID2D1Bitmap> bmp;
        rt->CreateBitmap(D2D1::SizeU(SZ, SZ), bits, SZ * 4, &props, &bmp);

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
