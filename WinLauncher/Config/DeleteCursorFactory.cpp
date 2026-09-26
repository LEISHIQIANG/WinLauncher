#include "DeleteCursorFactory.h"
#include "../resource.h"
#include <vector>
#include <cmath>
#include <algorithm>

namespace DeleteCursorFactory
{
    HCURSOR CreateDeleteCursor()
    {
        static constexpr int kCursorSize = 32;
        std::vector<DWORD> pixels(kCursorSize * kCursorSize, 0);

        auto setPixel = [&pixels](int x, int y, BYTE a, BYTE r, BYTE g, BYTE b)
        {
            if (x < 0 || x >= kCursorSize || y < 0 || y >= kCursorSize) return;
            pixels[y * kCursorSize + x] = ((DWORD)a << 24) | ((DWORD)r << 16) | ((DWORD)g << 8) | (DWORD)b;
        };

        auto drawLine = [&setPixel](int x0, int y0, int x1, int y1, BYTE a, BYTE r, BYTE g, BYTE b)
        {
            int dx = std::abs(x1 - x0);
            int sx = x0 < x1 ? 1 : -1;
            int dy = -std::abs(y1 - y0);
            int sy = y0 < y1 ? 1 : -1;
            int err = dx + dy;
            while (true)
            {
                setPixel(x0, y0, a, r, g, b);
                if (x0 == x1 && y0 == y1) break;
                int e2 = 2 * err;
                if (e2 >= dy) { err += dy; x0 += sx; }
                if (e2 <= dx) { err += dx; y0 += sy; }
            }
        };

        const wchar_t* arrow[] = {
            L"X...............",
            L"XX..............",
            L"XWX.............",
            L"XWWX............",
            L"XWWWX...........",
            L"XWWWWX..........",
            L"XWWWWWX.........",
            L"XWWWWWWX........",
            L"XWWWWWWWX.......",
            L"XWWWWXXXXX......",
            L"XWWXWXX.........",
            L"XWX.XWX.........",
            L"XX..XWX.........",
            L"X....XWX........",
            L".....XWX........",
            L"......XX........",
        };
        for (int y = 0; y < 16; y++)
        {
            for (int x = 0; x < 16; x++)
            {
                wchar_t ch = arrow[y][x];
                if (ch == L'X') setPixel(x + 1, y + 1, 255, 18, 18, 18);
                else if (ch == L'W') setPixel(x + 1, y + 1, 255, 255, 255, 255);
            }
        }

        constexpr int badgeSize = 14;
        constexpr int badgeLeft = 17;
        constexpr int badgeTop = 15;
        HICON trashIcon = (HICON)LoadImageW(
            GetModuleHandleW(nullptr),
            MAKEINTRESOURCEW(IDI_DELETE_TRASH_ICON),
            IMAGE_ICON,
            badgeSize,
            badgeSize,
            LR_DEFAULTCOLOR);

        if (trashIcon)
        {
            BITMAPINFO badgeBmi{};
            badgeBmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            badgeBmi.bmiHeader.biWidth = badgeSize;
            badgeBmi.bmiHeader.biHeight = -badgeSize;
            badgeBmi.bmiHeader.biPlanes = 1;
            badgeBmi.bmiHeader.biBitCount = 32;
            badgeBmi.bmiHeader.biCompression = BI_RGB;

            void* badgeBitsRaw = nullptr;
            HBITMAP badgeBitmap = CreateDIBSection(nullptr, &badgeBmi, DIB_RGB_COLORS, &badgeBitsRaw, nullptr, 0);
            if (badgeBitmap && badgeBitsRaw)
            {
                HDC screenDc = GetDC(nullptr);
                HDC memDc = CreateCompatibleDC(screenDc);
                HGDIOBJ oldBitmap = SelectObject(memDc, badgeBitmap);
                DrawIconEx(memDc, 0, 0, trashIcon, badgeSize, badgeSize, 0, nullptr, DI_NORMAL);
                SelectObject(memDc, oldBitmap);
                DeleteDC(memDc);
                ReleaseDC(nullptr, screenDc);

                DWORD* badgeBits = static_cast<DWORD*>(badgeBitsRaw);
                for (int y = 0; y < badgeSize; y++)
                {
                    for (int x = 0; x < badgeSize; x++)
                    {
                        DWORD src = badgeBits[y * badgeSize + x];
                        BYTE srcB = (BYTE)(src & 0xFF);
                        BYTE srcG = (BYTE)((src >> 8) & 0xFF);
                        BYTE srcR = (BYTE)((src >> 16) & 0xFF);
                        BYTE srcA = (BYTE)((src >> 24) & 0xFF);
                        int luminance = (srcR * 30 + srcG * 59 + srcB * 11) / 100;
                        bool visible = srcA > 8 || luminance > 8;
                        if (!visible) continue;

                        BYTE alpha = srcA > 8 ? srcA : 255;
                        setPixel(badgeLeft + x + 1, badgeTop + y + 1, (BYTE)(alpha / 3), 0, 0, 0);

                        BYTE red = (BYTE)std::min(255, 150 + luminance / 2);
                        BYTE green = (BYTE)std::min(90, 12 + luminance / 6);
                        BYTE blue = (BYTE)std::min(90, 18 + luminance / 7);
                        setPixel(badgeLeft + x, badgeTop + y, alpha, red, green, blue);
                    }
                }
            }
            if (badgeBitmap) DeleteObject(badgeBitmap);
            DestroyIcon(trashIcon);
        }
        else
        {
            drawLine(20, 17, 26, 17, 255, 220, 38, 38);
            drawLine(18, 19, 28, 19, 255, 185, 28, 28);
            drawLine(19, 20, 19, 28, 255, 239, 68, 68);
            drawLine(27, 20, 27, 28, 255, 185, 28, 28);
            drawLine(19, 28, 27, 28, 255, 185, 28, 28);
        }

        BITMAPINFO bmi{};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = kCursorSize;
        bmi.bmiHeader.biHeight = -kCursorSize;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        void* colorBits = nullptr;
        HBITMAP colorBitmap = CreateDIBSection(nullptr, &bmi, DIB_RGB_COLORS, &colorBits, nullptr, 0);
        if (!colorBitmap || !colorBits)
        {
            if (colorBitmap) DeleteObject(colorBitmap);
            return nullptr;
        }
        memcpy(colorBits, pixels.data(), pixels.size() * sizeof(DWORD));

        std::vector<BYTE> maskBits((kCursorSize * kCursorSize + 7) / 8, 0);
        HBITMAP maskBitmap = CreateBitmap(kCursorSize, kCursorSize, 1, 1, maskBits.data());
        if (!maskBitmap)
        {
            DeleteObject(colorBitmap);
            return nullptr;
        }

        ICONINFO iconInfo{};
        iconInfo.fIcon = FALSE;
        iconInfo.xHotspot = 2;
        iconInfo.yHotspot = 2;
        iconInfo.hbmMask = maskBitmap;
        iconInfo.hbmColor = colorBitmap;

        HCURSOR cursor = CreateIconIndirect(&iconInfo);
        DeleteObject(maskBitmap);
        DeleteObject(colorBitmap);

        return cursor;
    }
}
