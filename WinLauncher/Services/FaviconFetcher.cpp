/**
 * FaviconFetcher.cpp
 *
 * Multi-strategy favicon fetcher for WinLauncher.
 * See FaviconFetcher.h for the full strategy description.
 */

#define NOMINMAX
#include "FaviconFetcher.h"
#include "ConfigPath.h"

#include <windows.h>
#include <wininet.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <shellapi.h>

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

    // Common icon paths probed in order (mirrors _COMMON_ICON_PATHS)
    static const wchar_t* k_CommonIconPaths[] = {
        L"/favicon.png",
        L"/apple-touch-icon.png",
        L"/apple-touch-icon-precomposed.png",
        L"/favicon.ico",
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
        std::wstring lo = ToLower(href);
        if (lo.rfind(L"http://", 0) == 0 || lo.rfind(L"https://", 0) == 0)
            return href;
        // Relative path – join to origin
        if (href[0] == L'/') return ExtractOrigin(base) + href;
        // Relative to current directory – simplification: join to origin
        return ExtractOrigin(base) + L"/" + href;
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
    static std::wstring DownloadToTempFile(const std::wstring& url)
    {
        auto resp = HttpGet(url,
            L"image/png,image/svg+xml,image/x-icon,image/vnd.microsoft.icon,image/*;q=0.8,*/*;q=0.5",
            k_MaxIconBytes);
        if (!resp.ok || resp.body.empty()) return L"";

        // Skip HTML responses (some servers return 200 with error page)
        std::string ctLo = ToLowerA(resp.contentType);
        if (ctLo.find("html") != std::string::npos) return L"";

        wchar_t tmpDir[MAX_PATH] = {};
        GetTempPathW(MAX_PATH, tmpDir);
        wchar_t tmpFile[MAX_PATH] = {};
        GetTempFileNameW(tmpDir, L"wlf", 0, tmpFile);
        // rename to .ico so ShellExtract can handle it
        std::wstring icoPath = std::wstring(tmpFile) + L".ico";
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
        // WebP: RIFF????WEBP
        if (buf[0] == 'R' && buf[1] == 'I' && buf[2] == 'F' && buf[3] == 'F')
        {
            if (nRead >= 12 && buf[8] == 'W' && buf[9] == 'E' && buf[10] == 'B' && buf[11] == 'P')
                return true;
        }
        // BMP: BM
        if (buf[0] == 'B' && buf[1] == 'M') return true;

        // SVG check: must contain <svg and not be an HTML document
        std::string content(buf, nRead);
        if (content.find("<svg") != std::string::npos || content.find("<SVG") != std::string::npos)
        {
            // Reject HTML documents wrapping SVG or standard error pages
            if (content.find("<html") == std::string::npos && content.find("<HTML") == std::string::npos &&
                content.find("<body") == std::string::npos && content.find("<BODY") == std::string::npos)
            {
                return true;
            }
        }

        return false;
    }

    // Copy src to dest atomically (overwrite)
    static bool AtomicCopy(const std::wstring& src, const std::wstring& dest)
    {
        return CopyFileW(src.c_str(), dest.c_str(), FALSE) != 0;
    }

    // -----------------------------------------------------------
    // HTML icon-link parser
    // -----------------------------------------------------------
    struct IconCandidate
    {
        int          score = 0;
        std::wstring url;
    };

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

            std::string tag = lo.substr(tagStart, tagEnd - tagStart + 1);
            std::string tagOrig = html.substr(tagStart, tagEnd - tagStart + 1);
            pos = tagEnd + 1;

            // Must contain rel="...icon..."
            auto extractAttr = [&](const std::string& src, const std::string& attr) -> std::string
            {
                std::string key = attr + "=";
                size_t p = src.find(key);
                if (p == std::string::npos) return "";
                p += key.size();
                char q = 0;
                if (p < src.size() && (src[p] == '"' || src[p] == '\''))
                    q = src[p++];
                size_t end = q ? src.find(q, p) : src.find_first_of(" \t\r\n>", p);
                if (end == std::string::npos) end = src.size();
                return src.substr(p, end - p);
            };

            std::string rel  = extractAttr(tag, "rel");
            if (rel.find("icon") == std::string::npos) continue;

            std::string href = extractAttr(tagOrig, "href");  // case-preserved
            if (href.empty()) href = extractAttr(tag, "href");
            if (href.empty()) continue;

            std::wstring hrefW = Utf8ToWstr(href);
            std::wstring resolvedUrl = ResolveUrl(baseUrl, hrefW);
            if (resolvedUrl.empty()) continue;

            std::string loHref  = ToLowerA(href);
            std::string sizes   = extractAttr(tag, "sizes");
            std::string type    = extractAttr(tag, "type");

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

            std::string tag = lo.substr(tagStart, tagEnd - tagStart + 1);
            std::string tagOrig = html.substr(tagStart, tagEnd - tagStart + 1);
            pos = tagEnd + 1;

            auto extractAttr = [&](const std::string& src, const std::string& attr) -> std::string
            {
                std::string key = attr + "=";
                size_t p = src.find(key);
                if (p == std::string::npos) return "";
                p += key.size();
                char q = 0;
                if (p < src.size() && (src[p] == '"' || src[p] == '\''))
                    q = src[p++];
                size_t end = q ? src.find(q, p) : src.find_first_of(" \t\r\n>", p);
                if (end == std::string::npos) end = src.size();
                return src.substr(p, end - p);
            };

            std::string rel = extractAttr(tag, "rel");
            if (rel.find("manifest") == std::string::npos) continue;

            std::string href = extractAttr(tagOrig, "href");
            if (href.empty()) href = extractAttr(tag, "href");
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
        std::wstring cachePath = cacheDir + L"\\" + cacheKey + L".ico";

        if (!forceRefresh && PathFileExistsW(cachePath.c_str()) && IsValidIconFile(cachePath))
            return cachePath;

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
                using TmpResult = std::pair<int, std::wstring>;
                std::vector<std::future<TmpResult>> futures;
                int limit = std::min((int)candidates.size(), 3);
                for (int i = 0; i < limit; ++i)
                {
                    std::wstring cUrl = candidates[i].url;
                    futures.push_back(std::async(std::launch::async,
                        [i, cUrl]() -> TmpResult
                        {
                            return { i, DownloadToTempFile(cUrl) };
                        }));
                }
                std::vector<TmpResult> results(futures.size(), { -1, L"" });
                for (auto& f : futures)
                {
                    try { auto r = f.get(); results[r.first] = r; } catch (...) {}
                }
                bool found = false;
                for (auto& r : results)
                {
                    if (!r.second.empty() && IsValidIconFile(r.second))
                    {
                        AtomicCopy(r.second, cachePath);
                        found = true;
                        break;
                    }
                }
                for (auto& r : results)
                {
                    if (!r.second.empty()) DeleteFileW(r.second.c_str());
                }
                if (found && IsValidIconFile(cachePath)) return cachePath;
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
                    if (!iconUrls.empty())
                    {
                        using TmpResult = std::pair<int, std::wstring>;
                        std::vector<std::future<TmpResult>> futures;
                        int limit = std::min((int)iconUrls.size(), 3);
                        for (int i = 0; i < limit; ++i)
                        {
                            std::wstring iu = iconUrls[i];
                            futures.push_back(std::async(std::launch::async,
                                [i, iu]() -> TmpResult
                                {
                                    return { i, DownloadToTempFile(iu) };
                                }));
                        }
                        std::vector<TmpResult> results(futures.size(), { -1, L"" });
                        for (auto& f : futures)
                        {
                            try { auto r = f.get(); results[r.first] = r; } catch (...) {}
                        }
                        bool found = false;
                        for (auto& r : results)
                        {
                            if (!r.second.empty() && IsValidIconFile(r.second))
                            {
                                AtomicCopy(r.second, cachePath);
                                found = true;
                                break;
                            }
                        }
                        for (auto& r : results)
                        {
                            if (!r.second.empty()) DeleteFileW(r.second.c_str());
                        }
                        if (found && IsValidIconFile(cachePath)) return cachePath;
                    }
                }
            }
        }

        // 6. Strategy C: common well-known icon paths (probe in parallel with futures)
        std::wstring origin = ExtractOrigin(url);
        if (!origin.empty())
        {
            // Build the list of candidate URLs
            std::vector<std::wstring> commonUrls;
            for (int i = 0; k_CommonIconPaths[i] != nullptr; ++i)
                commonUrls.push_back(origin + k_CommonIconPaths[i]);

            // Launch parallel probes
            using TmpResult = std::pair<int /*index*/, std::wstring /*tmpPath*/>;
            std::vector<std::future<TmpResult>> futures;
            for (int i = 0; i < (int)commonUrls.size(); ++i)
            {
                std::wstring probeUrl = commonUrls[i];
                futures.push_back(std::async(std::launch::async,
                    [i, probeUrl]() -> TmpResult
                    {
                        std::wstring tmp = DownloadToTempFile(probeUrl);
                        return { i, tmp };
                    }));
            }

            // Collect results in original priority order
            std::vector<TmpResult> results(futures.size(), { -1, L"" });
            for (auto& f : futures)
            {
                try { auto r = f.get(); results[r.first] = r; }
                catch (...) {}
            }

            for (auto& r : results)
            {
                if (!r.second.empty() && IsValidIconFile(r.second))
                {
                    AtomicCopy(r.second, cachePath);
                    // Clean up all temp files
                    for (auto& r2 : results)
                        if (!r2.second.empty()) DeleteFileW(r2.second.c_str());

                    if (IsValidIconFile(cachePath)) return cachePath;
                    break;
                }
            }
            // Clean up any remaining temp files
            for (auto& r : results)
                if (!r.second.empty()) DeleteFileW(r.second.c_str());
        }

        // If we reach here: try returning old cached file (even if stale)
        if (PathFileExistsW(cachePath.c_str()) && IsValidIconFile(cachePath))
            return cachePath;

        return L"";
    }

} // namespace FaviconFetcher
