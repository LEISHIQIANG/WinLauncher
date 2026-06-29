#pragma once
#include "IIconService.h"
#include "../resource.h"
#include "../App/Logger.h"
#include <commoncontrols.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <unordered_map>
#include "../UI/Controls/IconRenderer.h"

#pragma comment(lib, "shlwapi.lib")

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
        if (targetPath.empty())
        {
            LOG_G_WORNING(L"GetIcon: empty targetPath");
            return nullptr;
        }

        auto it = m_hiconCache.find(targetPath);
        if (it != m_hiconCache.end())
            return it->second;

        HICON hIcon = nullptr;
        DWORD attr = GetFileAttributesW(targetPath.c_str());
        bool isDir = (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY));

        if (attr == INVALID_FILE_ATTRIBUTES)
        {
            LOG_G_WORNING(L"GetIcon: GetFileAttributesW failed, path=%s, err=%d",
                targetPath.c_str(), GetLastError());
        }

        if (isDir)
        {
            hIcon = (HICON)LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(102), IMAGE_ICON, 256, 256, LR_DEFAULTCOLOR);
            if (hIcon)
            {
                LOG_G_DEBUG(L"GetIcon: loaded builtin folder icon for dir=%s", targetPath.c_str());
                m_hiconCache[targetPath] = hIcon;
                return hIcon;
            }
            LOG_G_WORNING(L"GetIcon: LoadImage folder icon failed for dir=%s, err=%d",
                targetPath.c_str(), GetLastError());
        }

        const wchar_t* ext = PathFindExtensionW(targetPath.c_str());
        bool isExeOrDllOrIco = false;
        if (ext && *ext)
        {
            isExeOrDllOrIco = (_wcsicmp(ext, L".exe") == 0 ||
                               _wcsicmp(ext, L".dll") == 0 ||
                               _wcsicmp(ext, L".ico") == 0);
        }

        if (isExeOrDllOrIco)
        {
            UINT extracted = PrivateExtractIconsW(targetPath.c_str(), 0, 256, 256, &hIcon, nullptr, 1, LR_DEFAULTCOLOR);
            if (extracted > 0 && hIcon)
            {
                m_hiconCache[targetPath] = hIcon;
                return hIcon;
            }
        }

        IImageList* sysImgList = nullptr;
        const int sizes[] = { SHIL_JUMBO, SHIL_EXTRALARGE, SHIL_LARGE };
        for (int size : sizes)
        {
            if (SUCCEEDED(SHGetImageList(size, IID_PPV_ARGS(&sysImgList))))
            {
                LOG_G_DEBUG(L"GetIcon: SHGetImageList succeeded, size=%d", size);
                break;
            }
        }

        if (sysImgList)
        {
            SHFILEINFOW sfi{};
            if (!SHGetFileInfoW(targetPath.c_str(), 0, &sfi, sizeof(sfi), SHGFI_SYSICONINDEX))
            {
                LOG_G_WORNING(L"GetIcon: SHGetFileInfoW(SYSICONINDEX) failed, path=%s, err=%d",
                    targetPath.c_str(), GetLastError());
            }
            if (sysImgList->GetIcon(sfi.iIcon, ILD_NORMAL, &hIcon) != S_OK)
            {
                LOG_G_WORNING(L"GetIcon: IImageList::GetIcon failed, iIcon=%d, path=%s",
                    sfi.iIcon, targetPath.c_str());
                hIcon = nullptr;
            }
            sysImgList->Release();
        }
        else
        {
            LOG_G_WORNING(L"GetIcon: SHGetImageList failed for all sizes, path=%s", targetPath.c_str());
        }

        if (!hIcon)
        {
            SHFILEINFOW sfi{};
            if (SHGetFileInfoW(targetPath.c_str(), 0, &sfi, sizeof(sfi), SHGFI_ICON | SHGFI_LARGEICON))
            {
                LOG_G_DEBUG(L"GetIcon: SHGetFileInfoW(SHGFI_ICON) succeeded, path=%s", targetPath.c_str());
                hIcon = sfi.hIcon;
            }
            else
            {
                LOG_G_WORNING(L"GetIcon: all icon extraction methods failed, path=%s, err=%d",
                    targetPath.c_str(), GetLastError());
            }
        }

        if (hIcon)
        {
            m_hiconCache[targetPath] = hIcon;
        }
        else
        {
            LOG_G_WORNING(L"GetIcon: returning nullptr for path=%s", targetPath.c_str());
        }
        return hIcon;
    }

    virtual ID2D1Bitmap* GetOrCreateBitmap(ID2D1HwndRenderTarget* rt, const std::wstring& targetPath, int size, bool invert) override
    {
        if (!rt)
        {
            LOG_G_ERRA(L"GetOrCreateBitmap: rt is null, path=%s, size=%d", targetPath.c_str(), size);
            return nullptr;
        }

        auto key = targetPath + L"@" + std::to_wstring(size) + L"@" + std::to_wstring(invert ? 1 : 0);
        auto it = m_bmpCache.find(key);
        if (it != m_bmpCache.end())
        {
            it->second->AddRef();
            return it->second.Get();
        }

        HICON hIcon = GetIcon(targetPath);
        if (!hIcon)
        {
            LOG_G_WORNING(L"GetOrCreateBitmap: GetIcon returned null, path=%s, size=%d, using fallback",
                targetPath.c_str(), size);
        }
        auto bmp = IconRenderer::HicontoD2D(rt, hIcon, size, invert);
        if (bmp)
        {
            m_bmpCache[key] = bmp;
            bmp->AddRef();
            return bmp.Get();
        }
        LOG_G_ERRA(L"GetOrCreateBitmap: HicontoD2D failed, path=%s, hIcon=%p, size=%d",
            targetPath.c_str(), (void*)hIcon, size);
        return nullptr;
    }

    virtual ID2D1Bitmap* IconToBitmap(ID2D1HwndRenderTarget* rt, HICON hIcon, int size, bool invert) override
    {
        if (!rt)
        {
            LOG_G_ERRA(L"IconToBitmap: rt is null, hIcon=%p, size=%d", (void*)hIcon, size);
            return nullptr;
        }
        if (!hIcon)
        {
            LOG_G_DEBUG(L"IconToBitmap: hIcon is null, using fallback IDI_APPLICATION, size=%d", size);
        }
        auto bmp = IconRenderer::HicontoD2D(rt, hIcon, size, invert);
        if (bmp)
        {
            bmp->AddRef();
            return bmp.Get();
        }
        LOG_G_ERRA(L"IconToBitmap: HicontoD2D returned null, hIcon=%p, size=%d", (void*)hIcon, size);
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
