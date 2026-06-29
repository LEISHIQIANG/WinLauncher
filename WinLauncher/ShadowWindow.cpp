#include "ShadowWindow.h"
#include "DpiHelper.h"
#include <cmath>
#include <vector>
#include <algorithm>

static void RegisterShadowClass()
{
    static bool registered = false;
    if (registered) return;
    WNDCLASSEXW wc = { 0 };
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = L"WinLauncherShadow";
    if (RegisterClassExW(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS)
    {
        registered = true;
    }
}

static inline bool IsPointInRoundedRect(float x, float y, float rx, float ry, float rw, float rh, float r)
{
    float left = rx, top = ry, right = rx + rw, bottom = ry + rh;
    if (x < left || x > right || y < top || y > bottom) return false;
    
    // Check corners
    if (x < left + r && y < top + r) {
        float dx = x - (left + r);
        float dy = y - (top + r);
        return (dx * dx + dy * dy <= r * r);
    }
    if (x > right - r && y < top + r) {
        float dx = x - (right - r);
        float dy = y - (top + r);
        return (dx * dx + dy * dy <= r * r);
    }
    if (x < left + r && y > bottom - r) {
        float dx = x - (left + r);
        float dy = y - (bottom - r);
        return (dx * dx + dy * dy <= r * r);
    }
    if (x > right - r && y > bottom - r) {
        float dx = x - (right - r);
        float dy = y - (bottom - r);
        return (dx * dx + dy * dy <= r * r);
    }
    return true;
}

ShadowWindow::ShadowWindow(HWND hMainWnd)
    : m_hMainWnd(hMainWnd)
{
    CreateShadowWindow();
}

ShadowWindow::~ShadowWindow()
{
    Destroy();
}

void ShadowWindow::SetSettings(const ShadowSettings& settings)
{
    m_settings = settings;
    // Invalidate cache to force recreation on next update
    m_cachedWidth = 0;
    m_cachedHeight = 0;
}

void ShadowWindow::Destroy()
{
    if (m_hShadowWnd)
    {
        DestroyWindow(m_hShadowWnd);
        m_hShadowWnd = nullptr;
    }
    if (m_hBitmap)
    {
        DeleteObject(m_hBitmap);
        m_hBitmap = nullptr;
    }
}

void ShadowWindow::CreateShadowWindow()
{
    RegisterShadowClass();

    m_hShadowWnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
        L"WinLauncherShadow",
        L"",
        WS_POPUP,
        CW_USEDEFAULT, CW_USEDEFAULT, 100, 100,
        nullptr, // No owner - managed Z-order manually
        nullptr,
        GetModuleHandle(nullptr),
        nullptr
    );
}

void ShadowWindow::SyncPosition(bool mainVisible)
{
    if (!m_hShadowWnd || !m_hMainWnd) return;

    bool show = mainVisible && IsWindowVisible(m_hMainWnd) && !IsIconic(m_hMainWnd);
    
    RECT wr;
    GetWindowRect(m_hMainWnd, &wr);
    int mainW = wr.right - wr.left;
    int mainH = wr.bottom - wr.top;
    
    float scale = DpiHelper::GetWindowScale(m_hMainWnd);
    int margin = (int)(m_settings.margin * scale);
    int offsetX = (int)(m_settings.offsetX * scale);
    int offsetY = (int)(m_settings.offsetY * scale);

    int shadowX = wr.left - margin + offsetX;
    int shadowY = wr.top - margin + offsetY;
    int shadowW = mainW + margin * 2;
    int shadowH = mainH + margin * 2;

    UINT flags = SWP_NOACTIVATE | SWP_NOSENDCHANGING;
    if (show)
    {
        flags |= SWP_SHOWWINDOW;
    }
    else
    {
        flags |= SWP_HIDEWINDOW;
    }
    
    // Set Z-order directly behind m_hMainWnd
    SetWindowPos(m_hShadowWnd, m_hMainWnd, shadowX, shadowY, shadowW, shadowH, flags);
}

void ShadowWindow::UpdateShadow(int mainWidth, int mainHeight, float physicalCornerRadius, float scale)
{
    if (!m_hShadowWnd || !m_hMainWnd) return;

    if (m_cachedWidth == mainWidth &&
        m_cachedHeight == mainHeight &&
        m_cachedCornerRadius == physicalCornerRadius &&
        m_cachedScale == scale &&
        m_hBitmap != nullptr)
    {
        SyncPosition(true);
        return;
    }

    m_cachedWidth = mainWidth;
    m_cachedHeight = mainHeight;
    m_cachedCornerRadius = physicalCornerRadius;
    m_cachedScale = scale;

    int margin = (int)(m_settings.margin * scale);
    int radius = (int)(m_settings.blurRadius * scale);
    int offsetX = (int)(m_settings.offsetX * scale);
    int offsetY = (int)(m_settings.offsetY * scale);
    float scaledCornerRadius = physicalCornerRadius;

    GenerateShadowBitmap(mainWidth, mainHeight, radius, margin, offsetX, offsetY, scaledCornerRadius, m_settings.opacity, m_settings.color);
    SyncPosition(true);
}

void ShadowWindow::GenerateShadowBitmap(int w, int h, int radius, int margin, int offsetX, int offsetY, float cornerRadius, float opacity, COLORREF color)
{
    int shadowWidth = w + margin * 2;
    int shadowHeight = h + margin * 2;

    if (shadowWidth <= 0 || shadowHeight <= 0) return;

    // 1. Generate Alpha Mask for Rounded Rectangle
    std::vector<BYTE> mask(shadowWidth * shadowHeight, 0);

    float rectX = (float)margin;
    float rectY = (float)margin;
    float rectW = (float)w;
    float rectH = (float)h;

    // Clamp corner radius to fit within shape dimensions
    float maxRadius = (std::min)(rectW, rectH) / 2.0f;
    if (cornerRadius > maxRadius) cornerRadius = maxRadius;
    if (cornerRadius < 0.0f) cornerRadius = 0.0f;

    float left = rectX;
    float top = rectY;
    float right = rectX + rectW;
    float bottom = rectY + rectH;
    float r = cornerRadius;

    for (int py = 0; py < shadowHeight; ++py)
    {
        int rowOffset = py * shadowWidth;
        for (int px = 0; px < shadowWidth; ++px)
        {
            // Skip check if out of bounds of the bounding box
            if (px < left || px >= right || py < top || py >= bottom)
            {
                mask[rowOffset + px] = 0;
                continue;
            }

            // Check fast-path inner rectangles (non-corner regions)
            bool insideHorizontal = (px >= left + r && px < right - r && py >= top && py < bottom);
            bool insideVertical = (px >= left && px < right && py >= top + r && py < bottom - r);

            if (insideHorizontal || insideVertical)
            {
                mask[rowOffset + px] = 255;
                continue;
            }

            // Perform 4x4 super-sampling for border and corner pixels
            int insideCount = 0;
            for (int j = 0; j < 4; ++j)
            {
                float sy = (float)py + (j + 0.5f) / 4.0f;
                for (int i = 0; i < 4; ++i)
                {
                    float sx = (float)px + (i + 0.5f) / 4.0f;
                    if (IsPointInRoundedRect(sx, sy, left, top, rectW, rectH, r))
                    {
                        insideCount++;
                    }
                }
            }
            mask[rowOffset + px] = (BYTE)((insideCount * 255) / 16);
        }
    }

    // 2. Compute 1D Gaussian Kernel
    std::vector<float> kernel(radius * 2 + 1);
    float sigma = radius / 3.0f;
    if (sigma < 0.1f) sigma = 0.1f;
    float sum = 0.0f;
    for (int i = -radius; i <= radius; ++i)
    {
        float val = expf(-(i * i) / (2.0f * sigma * sigma));
        kernel[i + radius] = val;
        sum += val;
    }
    for (int i = 0; i <= radius * 2; ++i)
    {
        kernel[i] /= sum;
    }

    // 3. Horizontal 1D Blur
    std::vector<float> temp(shadowWidth * shadowHeight, 0.0f);
    for (int y = 0; y < shadowHeight; ++y)
    {
        int rowOffset = y * shadowWidth;
        for (int x = 0; x < shadowWidth; ++x)
        {
            float val = 0.0f;
            for (int k = -radius; k <= radius; ++k)
            {
                int sampleX = x + k;
                if (sampleX < 0) sampleX = 0;
                else if (sampleX >= shadowWidth) sampleX = shadowWidth - 1;
                val += mask[rowOffset + sampleX] * kernel[k + radius];
            }
            temp[rowOffset + x] = val;
        }
    }

    // 4. Vertical 1D Blur & Premultiplied BGRA Output
    std::vector<DWORD> pixels(shadowWidth * shadowHeight);
    BYTE rColor = GetRValue(color);
    BYTE gColor = GetGValue(color);
    BYTE bColor = GetBValue(color);

    for (int y = 0; y < shadowHeight; ++y)
    {
        int rowOffset = y * shadowWidth;
        for (int x = 0; x < shadowWidth; ++x)
        {
            float val = 0.0f;
            for (int k = -radius; k <= radius; ++k)
            {
                int sampleY = y + k;
                if (sampleY < 0) sampleY = 0;
                else if (sampleY >= shadowHeight) sampleY = shadowHeight - 1;
                val += temp[sampleY * shadowWidth + x] * kernel[k + radius];
            }

            BYTE finalAlpha = (BYTE)(val * opacity);
            BYTE rOut = (BYTE)((int)rColor * finalAlpha / 255);
            BYTE gOut = (BYTE)((int)gColor * finalAlpha / 255);
            BYTE bOut = (BYTE)((int)bColor * finalAlpha / 255);

            pixels[rowOffset + x] = (finalAlpha << 24) | (rOut << 16) | (gOut << 8) | bOut;
        }
    }

    // 5. Create 32-bit DIBSection and update layered window
    if (m_hBitmap)
    {
        DeleteObject(m_hBitmap);
        m_hBitmap = nullptr;
    }

    BITMAPINFO bmi = { 0 };
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = shadowWidth;
    bmi.bmiHeader.biHeight = -shadowHeight; // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* pvBits = nullptr;
    HDC hdcScreen = GetDC(nullptr);
    m_hBitmap = CreateDIBSection(hdcScreen, &bmi, DIB_RGB_COLORS, &pvBits, nullptr, 0);
    
    if (m_hBitmap && pvBits)
    {
        memcpy(pvBits, pixels.data(), shadowWidth * shadowHeight * sizeof(DWORD));
        
        HDC hdcMem = CreateCompatibleDC(hdcScreen);
        HGDIOBJ hOld = SelectObject(hdcMem, m_hBitmap);

        POINT ptSrc = { 0, 0 };
        SIZE sizeDst = { shadowWidth, shadowHeight };
        
        RECT wr;
        GetWindowRect(m_hMainWnd, &wr);
        int actualMargin = margin;
        POINT ptDst = { wr.left - actualMargin + offsetX, wr.top - actualMargin + offsetY };

        BLENDFUNCTION blend = { 0 };
        blend.BlendOp = AC_SRC_OVER;
        blend.BlendFlags = 0;
        blend.SourceConstantAlpha = 255;
        blend.AlphaFormat = AC_SRC_ALPHA;

        UpdateLayeredWindow(m_hShadowWnd, hdcScreen, &ptDst, &sizeDst, hdcMem, &ptSrc, 0, &blend, ULW_ALPHA);

        SelectObject(hdcMem, hOld);
        DeleteDC(hdcMem);
    }
    ReleaseDC(nullptr, hdcScreen);
}

void ShadowWindow::SetOpacity(float factor)
{
    if (!m_hShadowWnd || !m_hBitmap) return;

    HDC hdcScreen = GetDC(nullptr);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    HGDIOBJ hOld = SelectObject(hdcMem, m_hBitmap);

    float scale = m_cachedScale;
    int margin = (int)(m_settings.margin * scale);
    int offsetX = (int)(m_settings.offsetX * scale);
    int offsetY = (int)(m_settings.offsetY * scale);
    int shadowWidth = m_cachedWidth + margin * 2;
    int shadowHeight = m_cachedHeight + margin * 2;

    POINT ptSrc = { 0, 0 };
    SIZE sizeDst = { shadowWidth, shadowHeight };

    RECT wr;
    GetWindowRect(m_hMainWnd, &wr);
    POINT ptDst = { wr.left - margin + offsetX, wr.top - margin + offsetY };

    BLENDFUNCTION blend = { 0 };
    blend.BlendOp = AC_SRC_OVER;
    blend.BlendFlags = 0;
    // Set custom opacity multiplier
    blend.SourceConstantAlpha = (BYTE)(factor * 255.0f);
    blend.AlphaFormat = AC_SRC_ALPHA;

    UpdateLayeredWindow(m_hShadowWnd, hdcScreen, &ptDst, &sizeDst, hdcMem, &ptSrc, 0, &blend, ULW_ALPHA);

    SelectObject(hdcMem, hOld);
    DeleteDC(hdcMem);
    ReleaseDC(nullptr, hdcScreen);
}

