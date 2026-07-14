/**
 * FaviconFetcher.cpp
 *
 * Multi-strategy favicon fetcher for WinLauncher.
 * See FaviconFetcher.h for the full strategy description.
 */

#define NOMINMAX
#include "FaviconFetcher.h"
#include "ConfigPath.h"
#include "../App/BackgroundTaskService.h"

#include <windows.h>
#include <wininet.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <shellapi.h>
#include <gdiplus.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstring>
#include <functional>
#include <future>
#include <mutex>
#include <regex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "wininet.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "gdiplus.lib")

// ============================================================
// Internal helpers
// ============================================================

namespace
{
    // -----------------------------------------------------------
    // Constants matching the reference project
    // -----------------------------------------------------------
    static const DWORD  k_ConnectTimeout = 2000;   // ms
    static const DWORD  k_RecvTimeout    = 3000;   // ms
    static const size_t k_MaxHtmlBytes   = 512 * 1024;
    static const size_t k_MaxIconBytes   = 2 * 1024 * 1024;
    static const WCHAR* k_UserAgent      = L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) WinLauncher/1.0";
    static const wchar_t* k_CacheExtensions[] = { L".png", L".ico", L".gif", L".jpg", L".bmp", nullptr };

    // Common icon paths probed in order (mirrors _COMMON_ICON_PATHS)
    static const wchar_t* k_CommonIconPaths[] = {
        L"/favicon.png",
        L"/apple-touch-icon.png",
        L"/apple-touch-icon-precomposed.png",
        L"/favicon.ico",
        L"/favicon-32x32.png",
        L"/favicon-16x16.png",
        L"/icon.png",
        L"/icons/icon-192.png",
        L"/icons/icon-512.png",
        L"/images/favicons/favicon.png",
        L"/images/favicons/favicon.ico",
        nullptr
    };

    // -----------------------------------------------------------
    // String helpers
    // -----------------------------------------------------------
    static std::string  WstrToUtf8(const std::wstring& w);
    static std::wstring Utf8ToWstr(const std::string& s);
    static std::wstring ToLower(std::wstring s);
    static void         TrimW(std::wstring& s);
    static std::string  ToLowerA(std::string s);
    static void         TrimA(std::string& s);
    static bool         IsValidIconFile(const std::wstring& path);

    // -----------------------------------------------------------
    // URL helpers
    // -----------------------------------------------------------

    // Normalise: add https:// prefix if no scheme present
    static std::wstring NormalizeUrl(const std::wstring& raw)
    {
        std::wstring s = raw;
        TrimW(s);
        if (s.empty()) return L"";
        std::wstring lo = ToLower(s);
        if (lo.rfind(L"http://", 0) == 0 || lo.rfind(L"https://", 0) == 0)
            return s;
        return L"https://" + s;
    }

    // Extract origin (scheme + host) from a full URL
    // e.g. "https://www.example.com/path" -> "https://www.example.com"
    static std::wstring ExtractOrigin(const std::wstring& url)
    {
        // Find "://"
        size_t schemeEnd = url.find(L"://");
        if (schemeEnd == std::wstring::npos) return L"";
        schemeEnd += 3; // past "://"
        size_t pathStart = url.find(L'/', schemeEnd);
        if (pathStart == std::wstring::npos) return url;
        return url.substr(0, pathStart);
    }

    // Resolve a (possibly relative) href against a base URL.
    // Handles absolute http(s) URLs as-is; everything else is joined to origin.
    static std::wstring ResolveUrl(const std::wstring& base, const std::wstring& href)
    {
        if (href.empty()) return L"";
        std::vector<wchar_t> combined(8192, L'\0');
        DWORD combinedLength = static_cast<DWORD>(combined.size());
        if (FAILED(UrlCombineW(base.c_str(), href.c_str(), combined.data(), &combinedLength, 0)))
            return L"";

        std::wstring resolved(combined.data());
        std::wstring lo = ToLower(resolved);
        if (lo.rfind(L"http://", 0) != 0 && lo.rfind(L"https://", 0) != 0)
            return L"";
        return resolved;
    }

    // Return only the hostname.  The public favicon fallback deliberately
    // never receives the page path, query string, credentials, or port.
    static std::wstring ExtractHostname(const std::wstring& url)
    {
        size_t schemeEnd = url.find(L"://");
        if (schemeEnd == std::wstring::npos) return L"";
        size_t authorityStart = schemeEnd + 3;
        size_t authorityEnd = url.find_first_of(L"/?#", authorityStart);
        std::wstring authority = url.substr(authorityStart,
            authorityEnd == std::wstring::npos ? std::wstring::npos : authorityEnd - authorityStart);

        size_t userInfoEnd = authority.rfind(L'@');
        if (userInfoEnd != std::wstring::npos) authority.erase(0, userInfoEnd + 1);
        if (authority.empty()) return L"";

        // IPv6 literals are kept intact; for normal hosts remove an optional port.
        if (authority.front() == L'[')
        {
            size_t closeBracket = authority.find(L']');
            return closeBracket == std::wstring::npos ? L"" : authority.substr(0, closeBracket + 1);
        }
        size_t portStart = authority.rfind(L':');
        if (portStart != std::wstring::npos) authority.resize(portStart);
        return authority;
    }

    static std::wstring UrlEncodeQueryComponent(const std::wstring& value)
    {
        std::string utf8 = WstrToUtf8(value);
        std::wstring encoded;
        encoded.reserve(utf8.size() * 3);
        static const wchar_t kHex[] = L"0123456789ABCDEF";
        for (unsigned char c : utf8)
        {
            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                (c >= '0' && c <= '9') || c == '-' || c == '.' || c == '_' || c == '~')
            {
                encoded.push_back(static_cast<wchar_t>(c));
            }
            else
            {
                encoded.push_back(L'%');
                encoded.push_back(kHex[c >> 4]);
                encoded.push_back(kHex[c & 0x0F]);
            }
        }
        return encoded;
    }

    // Public favicon indexes are often keyed by the registrable site rather
    // than a protected subdomain.  Try the requested host first, then its
    // base domain without sending any URL path or query data.
    static std::vector<std::wstring> GetFaviconLookupHosts(const std::wstring& url)
    {
        std::vector<std::wstring> hosts;
        std::wstring hostname = ToLower(ExtractHostname(url));
        if (hostname.empty() || hostname.front() == L'[') return hosts;
        hosts.push_back(hostname);

        size_t lastDot = hostname.rfind(L'.');
        if (lastDot == std::wstring::npos || lastDot == 0) return hosts;
        size_t secondLastDot = hostname.rfind(L'.', lastDot - 1);
        if (secondLastDot == std::wstring::npos) return hosts;

        size_t baseStart = secondLastDot + 1;
        std::wstring topLevel = hostname.substr(lastDot + 1);
        std::wstring secondLevel = hostname.substr(secondLastDot + 1, lastDot - secondLastDot - 1);
        const wchar_t* countrySecondLevels[] = { L"ac", L"co", L"com", L"edu", L"gov", L"net", L"org", nullptr };
        bool useThreeLabels = topLevel.size() == 2;
        if (useThreeLabels)
        {
            useThreeLabels = false;
            for (int i = 0; countrySecondLevels[i] != nullptr; ++i)
            {
                if (secondLevel == countrySecondLevels[i]) { useThreeLabels = true; break; }
            }
        }
        if (useThreeLabels && secondLastDot > 0)
        {
            size_t thirdLastDot = hostname.rfind(L'.', secondLastDot - 1);
            baseStart = thirdLastDot == std::wstring::npos ? 0 : thirdLastDot + 1;
        }

        std::wstring baseDomain = hostname.substr(baseStart);
        if (baseDomain != hostname) hosts.push_back(baseDomain);
        return hosts;
    }

    // Simple SHA-1-like 40-char hex cache key via CryptHashData / Windows CNG.
    // Because we can't easily use OpenSSL, we use a FNV-1a hash (fast, good enough
    // for cache keys – collision risk is negligible for a local file cache).
    static std::wstring CacheKey(const std::wstring& url)
    {
        // FNV-1a 64-bit
        uint64_t hash = 14695981039346656037ULL;
        std::wstring lo = ToLower(url);
        TrimW(lo);
        for (wchar_t c : lo)
        {
            hash ^= static_cast<uint64_t>(c);
            hash *= 1099511628211ULL;
        }
        wchar_t buf[32];
        swprintf_s(buf, L"%016llx", (unsigned long long)hash);
        return std::wstring(buf);
    }

    // -----------------------------------------------------------
    // Cache directory
    // -----------------------------------------------------------
    static std::wstring GetCacheDirInternal()
    {
        std::wstring dir = ConfigPath::GetUserConfigDirectory() + L"\\favicons";
        ConfigPath::EnsureDirectoryExists(dir);
        return dir;
    }

    // -----------------------------------------------------------
    // WinInet request helper
    // -----------------------------------------------------------

    struct HttpResponse
    {
        bool        ok        = false;
        std::string body;         // raw bytes
        std::string contentType;
        int         statusCode = 0;
        std::wstring finalUrl;
    };

    static HttpResponse HttpGet(const std::wstring& url,
                                const std::wstring& acceptHeader = L"*/*",
                                size_t              maxBytes     = k_MaxHtmlBytes,
                                int                 redirectDepth = 0)
    {
        HttpResponse resp;
        if (redirectDepth > 5) return resp;

        HINTERNET hSession = InternetOpenW(
            k_UserAgent,
            INTERNET_OPEN_TYPE_PRECONFIG,
            nullptr, nullptr,
            INTERNET_FLAG_NO_UI);
        if (!hSession) return resp;

        auto closeSession = [&]{ InternetCloseHandle(hSession); };

        InternetSetOptionW(hSession, INTERNET_OPTION_CONNECT_TIMEOUT, (LPVOID)&k_ConnectTimeout, sizeof(DWORD));
        InternetSetOptionW(hSession, INTERNET_OPTION_RECEIVE_TIMEOUT, (LPVOID)&k_RecvTimeout,    sizeof(DWORD));

        // Build request headers
        std::wstring headers = L"Accept: " + acceptHeader + L"\r\n";

        HINTERNET hUrl = InternetOpenUrlW(
            hSession,
            url.c_str(),
            headers.c_str(), (DWORD)headers.size(),
            INTERNET_FLAG_NO_UI | INTERNET_FLAG_NO_CACHE_WRITE |
            INTERNET_FLAG_PRAGMA_NOCACHE | INTERNET_FLAG_RELOAD |
            INTERNET_FLAG_NO_AUTO_REDIRECT |
            INTERNET_FLAG_IGNORE_CERT_CN_INVALID | INTERNET_FLAG_IGNORE_CERT_DATE_INVALID,
            0);

        if (!hUrl) { closeSession(); return resp; }

        // Read status code
        DWORD statusCode = 0;
        DWORD bufLen = sizeof(DWORD);
        HttpQueryInfoW(hUrl, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER,
                       &statusCode, &bufLen, nullptr);
        resp.statusCode = (int)statusCode;

        // Manual redirect tracking (especially for protocol crossings like http -> https)
        if (statusCode == 301 || statusCode == 302 || statusCode == 303 || statusCode == 307 || statusCode == 308)
        {
            wchar_t locBuf[2048] = {};
            DWORD locLen = sizeof(locBuf);
            if (HttpQueryInfoW(hUrl, HTTP_QUERY_LOCATION, locBuf, &locLen, nullptr))
            {
                std::wstring redirectUrl = ResolveUrl(url, locBuf);
                InternetCloseHandle(hUrl);
                closeSession();
                return HttpGet(redirectUrl, acceptHeader, maxBytes, redirectDepth + 1);
            }
        }

        // Read Content-Type
        wchar_t ctBuf[256] = {};
        DWORD ctLen = sizeof(ctBuf);
        HttpQueryInfoW(hUrl, HTTP_QUERY_CONTENT_TYPE, ctBuf, &ctLen, nullptr);
        resp.contentType = WstrToUtf8(ctBuf);

        // Read body
        std::string body;
        body.reserve(64 * 1024);
        char buf[4096];
        DWORD bytesRead = 0;
        while (body.size() < maxBytes)
        {
            DWORD toRead = (DWORD)std::min((size_t)(sizeof(buf)), maxBytes - body.size());
            if (!InternetReadFile(hUrl, buf, toRead, &bytesRead) || bytesRead == 0)
                break;
            body.append(buf, bytesRead);
        }

        resp.ok   = ((statusCode >= 200 && statusCode < 300) || statusCode == 0);
        resp.body = std::move(body);
        resp.finalUrl = url;

        InternetCloseHandle(hUrl);
        closeSession();
        return resp;
    }

    // -----------------------------------------------------------
    // Download raw bytes to a temp file, return path
    // -----------------------------------------------------------
    static const wchar_t* DetectIconExtension(const std::string& bytes)
    {
        if (bytes.size() >= 8 && (BYTE)bytes[0] == 0x89 && (BYTE)bytes[1] == 0x50 &&
            (BYTE)bytes[2] == 0x4E && (BYTE)bytes[3] == 0x47)
            return L".png";
        if (bytes.size() >= 4 && (BYTE)bytes[0] == 0x00 && (BYTE)bytes[1] == 0x00 &&
            (BYTE)bytes[2] == 0x01 && (BYTE)bytes[3] == 0x00)
            return L".ico";
        if (bytes.size() >= 3 && bytes[0] == 'G' && bytes[1] == 'I' && bytes[2] == 'F')
            return L".gif";
        if (bytes.size() >= 2 && (BYTE)bytes[0] == 0xFF && (BYTE)bytes[1] == 0xD8)
            return L".jpg";
        if (bytes.size() >= 2 && bytes[0] == 'B' && bytes[1] == 'M')
            return L".bmp";
        return nullptr;
    }

    static std::wstring DownloadToTempFile(const std::wstring& url)
    {
        auto resp = HttpGet(url,
            L"image/png,image/svg+xml,image/x-icon,image/vnd.microsoft.icon,image/*;q=0.8,*/*;q=0.5",
            k_MaxIconBytes);
        if (!resp.ok || resp.body.empty()) return L"";

        // Skip HTML responses (some servers return 200 with error page)
        std::string ctLo = ToLowerA(resp.contentType);
        if (ctLo.find("html") != std::string::npos) return L"";

        const wchar_t* extension = DetectIconExtension(resp.body);
        // Keep only formats the native icon path can decode while retaining
        // their original alpha channel.  Do not rasterize onto a background.
        if (!extension) return L"";

        wchar_t tmpDir[MAX_PATH] = {};
        GetTempPathW(MAX_PATH, tmpDir);
        wchar_t tmpFile[MAX_PATH] = {};
        GetTempFileNameW(tmpDir, L"wlf", 0, tmpFile);
        // Preserve the source format in the filename so GDI+ keeps PNG/ICO
        // transparency instead of treating every payload as an ICO resource.
        std::wstring icoPath = std::wstring(tmpFile) + extension;
        DeleteFileW(tmpFile);

        // Write bytes
        HANDLE hFile = CreateFileW(icoPath.c_str(), GENERIC_WRITE, 0, nullptr,
                                   CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hFile == INVALID_HANDLE_VALUE) return L"";
        DWORD written = 0;
        BOOL writeResult = WriteFile(hFile, resp.body.data(), (DWORD)resp.body.size(), &written, nullptr);
        CloseHandle(hFile);

        if (writeResult && written > 0)
        {
            return icoPath;
        }
        else
        {
            DeleteFileW(icoPath.c_str());
            return L"";
        }
    }

    // Check if a downloaded file has content and is likely a valid image
    static bool IsValidIconFile(const std::wstring& path)
    {
        if (path.empty() || !PathFileExistsW(path.c_str())) return false;
        HANDLE hFile = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                                   nullptr, OPEN_EXISTING, 0, nullptr);
        if (hFile == INVALID_HANDLE_VALUE) return false;
        LARGE_INTEGER size{};
        GetFileSizeEx(hFile, &size);
        if (size.QuadPart < 16) { CloseHandle(hFile); return false; }

        // Read up to 1024 bytes to inspect magic and header content
        char buf[1024] = {};
        DWORD nRead = 0;
        ReadFile(hFile, buf, sizeof(buf) - 1, &nRead, nullptr);
        CloseHandle(hFile);

        if (nRead < 8) return false;

        // PNG: 89 50 4E 47
        if ((BYTE)buf[0] == 0x89 && (BYTE)buf[1] == 0x50) return true;
        // ICO: 00 00 01 00
        if ((BYTE)buf[0] == 0x00 && (BYTE)buf[1] == 0x00 &&
            (BYTE)buf[2] == 0x01 && (BYTE)buf[3] == 0x00) return true;
        // GIF
        if (buf[0] == 'G' && buf[1] == 'I' && buf[2] == 'F') return true;
        // JPEG: FF D8
        if ((BYTE)buf[0] == 0xFF && (BYTE)buf[1] == 0xD8) return true;
        // BMP: BM
        if (buf[0] == 'B' && buf[1] == 'M') return true;

        return false;
    }

    // Copy src to dest atomically (overwrite)
    static bool AtomicCopy(const std::wstring& src, const std::wstring& dest)
    {
        return CopyFileW(src.c_str(), dest.c_str(), FALSE) != 0;
    }

    static std::wstring GetCachedIconPath(const std::wstring& cacheBasePath)
    {
        for (int i = 0; k_CacheExtensions[i] != nullptr; ++i)
        {
            std::wstring path = cacheBasePath + k_CacheExtensions[i];
            if (PathFileExistsW(path.c_str()) && IsValidIconFile(path))
                return path;
        }
        return L"";
    }

    class FaviconGdiPlus
    {
    public:
        static void EnsureInitialized()
        {
            static FaviconGdiPlus instance;
        }

    private:
        FaviconGdiPlus()
        {
            Gdiplus::GdiplusStartupInput input;
            Gdiplus::GdiplusStartup(&m_token, &input, nullptr);
        }
        ~FaviconGdiPlus()
        {
            if (m_token) Gdiplus::GdiplusShutdown(m_token);
        }
        ULONG_PTR m_token = 0;
    };

    // Public favicon services sometimes place a small logo on a fully opaque
    // white or black tile.  This is not a source image with transparency, so
    // do not silently turn it into the user's launcher icon.
    static bool HasOpaqueNeutralCanvas(const std::wstring& path)
    {
        FaviconGdiPlus::EnsureInitialized();
        Gdiplus::Bitmap image(path.c_str());
        if (image.GetLastStatus() != Gdiplus::Ok || image.GetWidth() == 0 || image.GetHeight() == 0)
            return false;

        const UINT stepX = (std::max)(1u, image.GetWidth() / 64u);
        const UINT stepY = (std::max)(1u, image.GetHeight() / 64u);
        size_t sampled = 0;
        size_t transparent = 0;
        size_t neutral = 0;
        for (UINT y = 0; y < image.GetHeight(); y += stepY)
        {
            for (UINT x = 0; x < image.GetWidth(); x += stepX)
            {
                Gdiplus::Color color;
                if (image.GetPixel(x, y, &color) != Gdiplus::Ok) continue;
                ++sampled;
                if (color.GetAlpha() < 250)
                {
                    ++transparent;
                    continue;
                }

                const BYTE r = color.GetRed();
                const BYTE g = color.GetGreen();
                const BYTE b = color.GetBlue();
                bool isWhite = r >= 248 && g >= 248 && b >= 248;
                bool isBlack = r <= 7 && g <= 7 && b <= 7;
                if (isWhite || isBlack) ++neutral;
            }
        }
        return sampled > 0 && transparent == 0 && neutral * 2 > sampled;
    }

    static std::wstring StoreIconInCache(const std::wstring& sourcePath, const std::wstring& cacheBasePath)
    {
        if (!IsValidIconFile(sourcePath)) return L"";
        const wchar_t* extension = PathFindExtensionW(sourcePath.c_str());
        if (!extension || !*extension) return L"";

        std::wstring targetPath = cacheBasePath + extension;
        if (!AtomicCopy(sourcePath, targetPath) || !IsValidIconFile(targetPath)) return L"";

        for (int i = 0; k_CacheExtensions[i] != nullptr; ++i)
        {
            std::wstring stalePath = cacheBasePath + k_CacheExtensions[i];
            if (_wcsicmp(stalePath.c_str(), targetPath.c_str()) != 0)
                DeleteFileW(stalePath.c_str());
        }
        return targetPath;
    }

    // Some sites require JavaScript or a browser session before exposing their
    // favicon.  Fall back only after direct site retrieval has failed.  Google
    // receives the hostname only, never the complete URL entered by the user.
    static std::wstring FetchPublicFallbackFavicon(const std::wstring& url, const std::wstring& cacheBasePath)
    {
        std::vector<std::wstring> hosts = GetFaviconLookupHosts(url);
        for (const auto& hostname : hosts)
        {
            std::wstring escapedHostname = UrlEncodeQueryComponent(hostname);
            const std::wstring fallbackUrls[] = {
                L"https://icons.duckduckgo.com/ip3/" + escapedHostname + L".ico",
                L"https://www.google.com/s2/favicons?sz=128&domain=" + escapedHostname
            };
            for (const auto& fallbackUrl : fallbackUrls)
            {
                std::wstring tmpPath = DownloadToTempFile(fallbackUrl);
                if (tmpPath.empty()) continue;

                if (HasOpaqueNeutralCanvas(tmpPath))
                {
                    DeleteFileW(tmpPath.c_str());
                    continue;
                }

                std::wstring cachedPath = StoreIconInCache(tmpPath, cacheBasePath);
                DeleteFileW(tmpPath.c_str());
                if (!cachedPath.empty()) return cachedPath;
            }
        }
        return {};
    }

    // -----------------------------------------------------------
    // HTML icon-link parser
    // -----------------------------------------------------------
    struct IconCandidate
    {
        int          score = 0;
        std::wstring url;
    };

    static std::string ExtractHtmlAttribute(const std::string& tag, const std::string& attribute)
    {
        std::string lowerTag = ToLowerA(tag);
        std::string lowerAttribute = ToLowerA(attribute);
        size_t searchFrom = 0;
        while (true)
        {
            size_t attributeStart = lowerTag.find(lowerAttribute, searchFrom);
            if (attributeStart == std::string::npos) return "";
            bool startsAttribute = attributeStart == 0 ||
                isspace(static_cast<unsigned char>(lowerTag[attributeStart - 1])) ||
                lowerTag[attributeStart - 1] == '<';
            size_t valueStart = attributeStart + lowerAttribute.size();
            while (valueStart < lowerTag.size() && isspace(static_cast<unsigned char>(lowerTag[valueStart]))) ++valueStart;
            if (!startsAttribute || valueStart >= lowerTag.size() || lowerTag[valueStart] != '=')
            {
                searchFrom = attributeStart + lowerAttribute.size();
                continue;
            }

            ++valueStart;
            while (valueStart < tag.size() && isspace(static_cast<unsigned char>(tag[valueStart]))) ++valueStart;
            if (valueStart >= tag.size()) return "";
            char quote = tag[valueStart] == '"' || tag[valueStart] == '\'' ? tag[valueStart++] : 0;
            size_t valueEnd = quote ? tag.find(quote, valueStart) : tag.find_first_of(" \t\r\n>", valueStart);
            if (valueEnd == std::string::npos) valueEnd = tag.size();
            return tag.substr(valueStart, valueEnd - valueStart);
        }
    }

    static std::wstring CacheCandidateUrls(const std::vector<std::wstring>& urls,
                                           const std::wstring& cacheBasePath,
                                           int maxCandidates = 3)
    {
        int limit = std::min(static_cast<int>(urls.size()), maxCandidates);
        if (limit <= 0) return L"";

        using TmpResult = std::pair<int, std::wstring>;
        // FetchFavicon already runs on BackgroundTaskService for editor and
        // batch operations.  Do not create nested unowned async workers here:
        // a multi-select operation would otherwise multiply worker count.
        std::vector<TmpResult> results;
        results.reserve(limit);
        for (int i = 0; i < limit; ++i)
        {
            results.push_back({ i, DownloadToTempFile(urls[i]) });
        }

        for (const auto& result : results)
        {
            if (!result.second.empty())
            {
                std::wstring cachedPath = StoreIconInCache(result.second, cacheBasePath);
                if (!cachedPath.empty())
                {
                    for (const auto& cleanup : results)
                        if (!cleanup.second.empty()) DeleteFileW(cleanup.second.c_str());
                    return cachedPath;
                }
            }
        }
        for (const auto& result : results)
            if (!result.second.empty()) DeleteFileW(result.second.c_str());
        return L"";
    }

    static std::vector<IconCandidate> ParseHtmlIconLinks(const std::string& html,
                                                          const std::wstring& baseUrl)
    {
        std::vector<IconCandidate> candidates;

        // Case-insensitive search for <link ... > tags
        std::string lo = ToLowerA(html);
        size_t pos = 0;
        while (true)
        {
            size_t tagStart = lo.find("<link", pos);
            if (tagStart == std::string::npos) break;
            size_t tagEnd = lo.find('>', tagStart);
            if (tagEnd == std::string::npos) break;

            std::string tagOrig = html.substr(tagStart, tagEnd - tagStart + 1);
            pos = tagEnd + 1;

            // Must contain rel="...icon..."
            std::string rel = ToLowerA(ExtractHtmlAttribute(tagOrig, "rel"));
            if (rel.find("icon") == std::string::npos) continue;

            std::string href = ExtractHtmlAttribute(tagOrig, "href");
            if (href.empty()) continue;

            std::wstring hrefW = Utf8ToWstr(href);
            std::wstring resolvedUrl = ResolveUrl(baseUrl, hrefW);
            if (resolvedUrl.empty()) continue;

            std::string loHref  = ToLowerA(href);
            std::string sizes   = ExtractHtmlAttribute(tagOrig, "sizes");
            std::string type    = ToLowerA(ExtractHtmlAttribute(tagOrig, "type"));

            // Skip SVG files since the application cannot render them natively
            if (type.find("svg") != std::string::npos || loHref.find(".svg") != std::string::npos) continue;

            int score = 10;
            if (rel.find("apple-touch-icon") != std::string::npos) score += 20;
            if (rel.find("mask-icon")        != std::string::npos) score -= 12;
            if (type.find("png")  != std::string::npos || loHref.find(".png") != std::string::npos)  score += 12;
            if (type.find("webp") != std::string::npos || loHref.find(".webp") != std::string::npos) score += 10;
            if (type.find("ico")  != std::string::npos || loHref.find(".ico")  != std::string::npos) score += 3;

            // Parse sizes like "64x64", "any"
            if (!sizes.empty())
            {
                std::regex sizeRe("(\\d+)x(\\d+)");
                std::sregex_iterator it(sizes.begin(), sizes.end(), sizeRe), end;
                for (; it != end; ++it)
                {
                    int w = std::stoi((*it)[1].str());
                    int h = std::stoi((*it)[2].str());
                    score += std::min(w, h) / 16;
                }
            }

            candidates.push_back({ score, resolvedUrl });
        }

        // Sort descending by score
        std::sort(candidates.begin(), candidates.end(),
                  [](const IconCandidate& a, const IconCandidate& b){ return a.score > b.score; });

        // Deduplicate
        std::vector<IconCandidate> deduped;
        std::vector<std::wstring> seen;
        for (auto& c : candidates)
        {
            std::wstring lo2 = ToLower(c.url);
            bool dup = false;
            for (auto& s : seen) if (s == lo2) { dup = true; break; }
            if (!dup) { deduped.push_back(c); seen.push_back(lo2); }
        }
        return deduped;
    }

    // A small number of sites omit favicon links but publish a branded preview
    // image.  It is a lower-priority visual fallback, used only after favicon
    // and manifest candidates fail.
    static std::vector<std::wstring> ParseMetaImageUrls(const std::string& html,
                                                        const std::wstring& baseUrl)
    {
        std::vector<std::wstring> urls;
        std::string lo = ToLowerA(html);
        size_t pos = 0;
        while (true)
        {
            size_t tagStart = lo.find("<meta", pos);
            if (tagStart == std::string::npos) break;
            size_t tagEnd = lo.find('>', tagStart);
            if (tagEnd == std::string::npos) break;
            std::string tag = html.substr(tagStart, tagEnd - tagStart + 1);
            pos = tagEnd + 1;

            std::string kind = ToLowerA(ExtractHtmlAttribute(tag, "property"));
            if (kind.empty()) kind = ToLowerA(ExtractHtmlAttribute(tag, "name"));
            if (kind != "og:image" && kind != "og:image:url" && kind != "twitter:image" &&
                kind != "twitter:image:src" && kind != "msapplication-tileimage")
                continue;

            std::string content = ExtractHtmlAttribute(tag, "content");
            std::string contentLower = ToLowerA(content);
            if (content.empty() || contentLower.find(".svg") != std::string::npos) continue;
            std::wstring resolved = ResolveUrl(baseUrl, Utf8ToWstr(content));
            if (!resolved.empty()) urls.push_back(std::move(resolved));
        }

        std::vector<std::wstring> deduped;
        for (const auto& url : urls)
        {
            bool exists = false;
            std::wstring lowered = ToLower(url);
            for (const auto& known : deduped)
                if (ToLower(known) == lowered) { exists = true; break; }
            if (!exists) deduped.push_back(url);
        }
        return deduped;
    }

    // -----------------------------------------------------------
    // Simple JSON extractor: find "src" values in manifest "icons" array
    // -----------------------------------------------------------
    static std::vector<std::wstring> ParseManifestIconUrls(const std::string& json,
                                                             const std::wstring& manifestBase)
    {
        std::vector<std::wstring> urls;
        // Find "icons": [ ... ]
        std::string lo = ToLowerA(json);
        size_t iconsPos = lo.find("\"icons\"");
        if (iconsPos == std::string::npos) return urls;
        size_t arrStart = lo.find('[', iconsPos);
        if (arrStart == std::string::npos) return urls;
        size_t arrEnd = lo.find(']', arrStart);
        if (arrEnd == std::string::npos) arrEnd = json.size() - 1;

        // Within the array, extract "src" values (from original json for correct casing)
        std::string arr = json.substr(arrStart, arrEnd - arrStart + 1);
        size_t pos = 0;
        while (true)
        {
            size_t srcPos = arr.find("\"src\"", pos);
            if (srcPos == std::string::npos) break;
            srcPos += 5; // past "src"
            // skip whitespace and ':'
            while (srcPos < arr.size() && (arr[srcPos] == ' ' || arr[srcPos] == ':')) srcPos++;
            if (srcPos >= arr.size()) break;
            char q = arr[srcPos++];
            if (q != '"' && q != '\'') { pos = srcPos; continue; }
            size_t end = arr.find(q, srcPos);
            if (end == std::string::npos) break;
            std::string src = arr.substr(srcPos, end - srcPos);
            if (!src.empty())
            {
                std::string loSrc = ToLowerA(src);
                if (loSrc.find(".svg") == std::string::npos)
                {
                    std::wstring srcW = Utf8ToWstr(src);
                    urls.push_back(ResolveUrl(manifestBase, srcW));
                }
            }
            pos = end + 1;
        }
        return urls;
    }

    // Extract <link rel="manifest" href="..."> from HTML
    static std::wstring ParseManifestLink(const std::string& html, const std::wstring& baseUrl)
    {
        std::string lo = ToLowerA(html);
        size_t pos = 0;
        while (true)
        {
            size_t tagStart = lo.find("<link", pos);
            if (tagStart == std::string::npos) break;
            size_t tagEnd = lo.find('>', tagStart);
            if (tagEnd == std::string::npos) break;

            std::string tagOrig = html.substr(tagStart, tagEnd - tagStart + 1);
            pos = tagEnd + 1;

            std::string rel = ToLowerA(ExtractHtmlAttribute(tagOrig, "rel"));
            if (rel.find("manifest") == std::string::npos) continue;

            std::string href = ExtractHtmlAttribute(tagOrig, "href");
            if (href.empty()) continue;

            return ResolveUrl(baseUrl, Utf8ToWstr(href));
        }
        return L"";
    }

    // -----------------------------------------------------------
    // String utilities (implementation at the bottom)
    // -----------------------------------------------------------
    static std::string WstrToUtf8(const std::wstring& w)
    {
        if (w.empty()) return "";
        int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
        if (n <= 0) return "";
        std::string s(n - 1, '\0');
        WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, &s[0], n, nullptr, nullptr);
        return s;
    }

    static std::wstring Utf8ToWstr(const std::string& s)
    {
        if (s.empty()) return L"";
        int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
        if (n <= 0) return L"";
        std::wstring w(n - 1, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
        return w;
    }

    static std::wstring ToLower(std::wstring s)
    {
        for (wchar_t& c : s) c = (wchar_t)towlower(c);
        return s;
    }

    static std::string ToLowerA(std::string s)
    {
        for (char& c : s) c = (char)tolower((unsigned char)c);
        return s;
    }

    static void TrimW(std::wstring& s)
    {
        while (!s.empty() && iswspace(s.front())) s.erase(0, 1);
        while (!s.empty() && iswspace(s.back()))  s.pop_back();
    }

    static void TrimA(std::string& s)
    {
        while (!s.empty() && isspace((unsigned char)s.front())) s.erase(0, 1);
        while (!s.empty() && isspace((unsigned char)s.back()))  s.pop_back();
    }

} // namespace (anonymous)


// ============================================================
// Public API implementation
// ============================================================

namespace FaviconFetcher
{
    std::wstring GetCacheDir()
    {
        return GetCacheDirInternal();
    }

    void ClearCache()
    {
        std::wstring dir = GetCacheDirInternal();
        WIN32_FIND_DATAW fd{};
        HANDLE hFind = FindFirstFileW((dir + L"\\*").c_str(), &fd);
        if (hFind == INVALID_HANDLE_VALUE) return;
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            DeleteFileW((dir + L"\\" + fd.cFileName).c_str());
        } while (FindNextFileW(hFind, &fd));
        FindClose(hFind);
    }

    std::wstring FetchFavicon(const std::wstring& rawUrl, bool forceRefresh)
    {
        // 1. Normalize URL
        std::wstring url = NormalizeUrl(rawUrl);
        if (url.empty()) return L"";

        // 2. Check cache
        std::wstring cacheDir = GetCacheDirInternal();
        std::wstring cacheKey  = CacheKey(url);
        std::wstring cacheBasePath = cacheDir + L"\\" + cacheKey;
        std::wstring cachedPath = GetCachedIconPath(cacheBasePath);

        if (!forceRefresh && !cachedPath.empty())
            return cachedPath;

        // 3. Fetch HTML
        std::string  html;
        std::wstring finalUrl = url;
        {
            auto resp = HttpGet(url,
                L"text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8",
                k_MaxHtmlBytes);
            if (resp.ok && !resp.body.empty())
            {
                std::string ctLo = ToLowerA(resp.contentType);
                if (ctLo.find("html") != std::string::npos ||
                    resp.contentType.empty())
                {
                    html = std::move(resp.body);
                }
                finalUrl = resp.finalUrl;
            }
        }

        // 4. Strategy A: parse <link rel="icon"> from HTML
        if (!html.empty())
        {
            auto candidates = ParseHtmlIconLinks(html, finalUrl);
            if (!candidates.empty())
            {
                std::vector<std::wstring> iconUrls;
                iconUrls.reserve(candidates.size());
                for (const auto& candidate : candidates) iconUrls.push_back(candidate.url);
                std::wstring fetchedPath = CacheCandidateUrls(iconUrls, cacheBasePath);
                if (!fetchedPath.empty()) return fetchedPath;
            }

            // 5. Strategy B: web manifest icons
            std::wstring manifestUrl = ParseManifestLink(html, finalUrl);
            if (!manifestUrl.empty())
            {
                auto mResp = HttpGet(manifestUrl,
                    L"application/manifest+json,application/json,*/*;q=0.5",
                    128 * 1024);
                if (mResp.ok && !mResp.body.empty())
                {
                    auto iconUrls = ParseManifestIconUrls(mResp.body, manifestUrl);
                    std::wstring fetchedPath = CacheCandidateUrls(iconUrls, cacheBasePath);
                    if (!fetchedPath.empty()) return fetchedPath;
                }
            }

            // 6. Some pages only expose a branded Open Graph or Twitter image.
            // Use it after normal icons, so a wide social card never displaces a
            // genuine favicon.
            std::wstring fetchedPath = CacheCandidateUrls(ParseMetaImageUrls(html, finalUrl), cacheBasePath, 2);
            if (!fetchedPath.empty()) return fetchedPath;
        }

        // 7. Strategy C: common well-known icon paths.  This stays within the
        // caller-owned background task instead of opening nested async work.
        std::wstring origin = ExtractOrigin(url);
        if (!origin.empty())
        {
            // Build the list of candidate URLs
            std::vector<std::wstring> commonUrls;
            for (int i = 0; k_CommonIconPaths[i] != nullptr; ++i)
                commonUrls.push_back(origin + k_CommonIconPaths[i]);

            using TmpResult = std::pair<int /*index*/, std::wstring /*tmpPath*/>;
            std::vector<TmpResult> results;
            results.reserve(commonUrls.size());
            for (int i = 0; i < (int)commonUrls.size(); ++i)
            {
                // 检查后台任务是否已被取消（如弹窗关闭、编辑器替换），避免阻塞线程
                if (BackgroundTaskService::IsCurrentTaskCancellationRequested())
                    break;
                results.push_back({ i, DownloadToTempFile(commonUrls[i]) });
            }

            for (auto& r : results)
            {
                if (!r.second.empty() && IsValidIconFile(r.second))
                {
                    std::wstring fetchedPath = StoreIconInCache(r.second, cacheBasePath);
                    // Clean up all temp files
                    for (auto& r2 : results)
                        if (!r2.second.empty()) DeleteFileW(r2.second.c_str());

                    if (!fetchedPath.empty()) return fetchedPath;
                    break;
                }
            }
            // Clean up any remaining temp files
            for (auto& r : results)
                if (!r.second.empty()) DeleteFileW(r.second.c_str());
        }

        // 8. A browser-protected site can still have a publicly available
        // favicon.  This is intentionally last so ordinary sites never need
        // a third-party lookup.  Both exact-host and base-domain indexes are
        // tried, and opaque neutral canvas results are rejected.
        if (std::wstring fetchedPath = FetchPublicFallbackFavicon(url, cacheBasePath); !fetchedPath.empty())
            return fetchedPath;

        // If we reach here: try returning old cached file (even if stale)
        if (!cachedPath.empty())
            return cachedPath;

        return L"";
    }

} // namespace FaviconFetcher
