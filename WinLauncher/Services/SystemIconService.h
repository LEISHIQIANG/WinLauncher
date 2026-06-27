#pragma once
#include "IIconService.h"
#include "../resource.h"
#include <commoncontrols.h>
#include <shlobj.h>
#include <unordered_map>
#include "../UI/Controls/IconRenderer.h"

class SystemIconService : public IIconService
{
public:
    SystemIconService() = default;

    virtual ~SystemIconService()
    {
        ClearCache();
    }

    virtual HICON GetIcon(const std::wstring& targetPath) override
    {
        if (targetPath.empty()) return nullptr;

        auto it = m_hiconCache.find(targetPath);
        if (it != m_hiconCache.end())
            return it->second;

        HICON hIcon = nullptr;
        bool isDir = false;
        DWORD attr = GetFileAttributesW(targetPath.c_str());
        isDir = (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY));

        if (isDir)
        {
            hIcon = (HICON)LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(102), IMAGE_ICON, 48, 48, LR_DEFAULTCOLOR);
            if (hIcon)
            {
                m_hiconCache[targetPath] = hIcon;
                return hIcon;
            }
        }

        IImageList* sysImgList = nullptr;
        const int sizes[] = { SHIL_EXTRALARGE, SHIL_JUMBO, SHIL_LARGE };
        for (int size : sizes)
        {
            if (SUCCEEDED(SHGetImageList(size, IID_PPV_ARGS(&sysImgList))))
                break;
        }

        if (sysImgList)
        {
            SHFILEINFOW sfi{};
            SHGetFileInfoW(targetPath.c_str(), 0, &sfi, sizeof(sfi), SHGFI_SYSICONINDEX);
            if (sysImgList->GetIcon(sfi.iIcon, ILD_NORMAL, &hIcon) != S_OK)
                hIcon = nullptr;
            sysImgList->Release();
        }

        if (!hIcon)
        {
            SHFILEINFOW sfi{};
            if (SHGetFileInfoW(targetPath.c_str(), 0, &sfi, sizeof(sfi), SHGFI_ICON | SHGFI_LARGEICON))
                hIcon = sfi.hIcon;
        }

        if (hIcon)
        {
            m_hiconCache[targetPath] = hIcon;
        }
        return hIcon;
    }

    virtual ID2D1Bitmap* GetOrCreateBitmap(ID2D1HwndRenderTarget* rt, const std::wstring& targetPath, int size) override
    {
        if (!rt) return nullptr;

        auto key = targetPath + L"@" + std::to_wstring(size);
        auto it = m_bmpCache.find(key);
        if (it != m_bmpCache.end())
        {
            it->second->AddRef();
            return it->second.Get();
        }

        HICON hIcon = GetIcon(targetPath);
        auto bmp = IconRenderer::HicontoD2D(rt, hIcon, size);
        if (bmp)
        {
            m_bmpCache[key] = bmp;
            bmp->AddRef();
            return bmp.Get();
        }
        return nullptr;
    }

    virtual ID2D1Bitmap* IconToBitmap(ID2D1HwndRenderTarget* rt, HICON hIcon, int size) override
    {
        if (!rt) return nullptr;
        // hIcon may be null — HicontoD2D handles that by falling back to IDI_APPLICATION
        auto bmp = IconRenderer::HicontoD2D(rt, hIcon, size);
        if (bmp)
        {
            bmp->AddRef();
            return bmp.Get();
        }
        return nullptr;
    }

    virtual void ReleaseBitmap(ID2D1Bitmap* bmp) override
    {
        if (bmp) bmp->Release();
    }

    virtual void ClearCache() override
    {
        for (auto& pair : m_hiconCache)
        {
            if (pair.second) DestroyIcon(pair.second);
        }
        m_hiconCache.clear();
        m_bmpCache.clear();
    }

private:
    std::unordered_map<std::wstring, HICON> m_hiconCache;
    std::unordered_map<std::wstring, ComPtr<ID2D1Bitmap>> m_bmpCache;
};
