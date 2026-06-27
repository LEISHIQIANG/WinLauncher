#pragma once
#include <Windows.h>
#include <d2d1.h>
#include <wrl.h>

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
};
