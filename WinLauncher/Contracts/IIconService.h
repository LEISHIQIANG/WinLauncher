#pragma once
#include <Windows.h>
#include <d2d1.h>
#include <string>

struct ID2D1HwndRenderTarget;

class IIconService
{
public:
    virtual ~IIconService() = default;

    virtual HICON GetIcon(const std::wstring& targetPath) = 0;
    // Cache-only lookup returning an owned CopyIcon duplicate (the caller
    // must DestroyIcon); never extracts and never blocks on the filesystem.
    // Returns nullptr on a miss. Non-pure so existing implementations keep
    // compiling; the shared SystemIconService overrides both helpers below.
    virtual HICON GetIconCopy(const std::wstring&) { return nullptr; }
    // Caches a copy of sourceIcon under targetPath without taking ownership.
    virtual void StoreIconCopy(const std::wstring&, HICON, bool /*fromResource*/ = false) {}
    virtual ID2D1Bitmap* GetOrCreateBitmap(ID2D1HwndRenderTarget* rt, const std::wstring& targetPath, int size = 48, bool invert = false) = 0;
    virtual ID2D1Bitmap* IconToBitmap(ID2D1HwndRenderTarget* rt, HICON hIcon, int size = 48, bool invert = false) = 0;
    virtual void ReleaseBitmap(ID2D1Bitmap* bmp) = 0;
    virtual void ClearCache() = 0;
};
