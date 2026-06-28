#pragma once
/**
 * FaviconFetcher.h
 *
 * Multi-strategy favicon / URL icon fetcher for WinLauncher.
 *
 * Strategy order (mirroring the reference project):
 *   1. Fetch HTML of the target page, parse <link rel="icon"> tags (scored).
 *   2. Parse web-manifest links found in the HTML (<link rel="manifest">).
 *   3. Probe common well-known icon paths in parallel
 *      (/favicon.svg, /favicon.png, /apple-touch-icon.png, /favicon.ico …).
 *   4. Cache the result as a .ico file under
 *      %APPDATA%\WinLauncher\config\favicons\<sha1-of-url>.ico
 *
 * All network I/O uses WinInet and is safe to call from a worker thread.
 */

#include <windows.h>
#include <wininet.h>
#include <string>
#include <vector>

#pragma comment(lib, "wininet.lib")

namespace FaviconFetcher
{
    // -----------------------------------------------------------------------
    // Public API
    // -----------------------------------------------------------------------

    /**
     * Fetch the best available favicon for the given URL.
     *
     * @param url          The full URL whose favicon should be fetched.
     * @param forceRefresh If true, ignore any cached file and re-fetch.
     * @return             Absolute path to the cached .ico file, or "" on failure.
     *
     * Thread-safe – may be called from any thread.
     */
    std::wstring FetchFavicon(const std::wstring& url, bool forceRefresh = false);

    /**
     * Clear all cached favicons managed by this module.
     */
    void ClearCache();

    /**
     * Return the path to the favicon cache directory (creates it if needed).
     */
    std::wstring GetCacheDir();

} // namespace FaviconFetcher
