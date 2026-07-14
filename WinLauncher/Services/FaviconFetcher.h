#pragma once
/**
 * FaviconFetcher.h
 *
 * Multi-strategy favicon / URL icon fetcher for WinLauncher.
 *
 * Strategy order:
 *   1. Fetch HTML of the target page, parse and score <link rel="icon"> tags.
 *   2. Parse web-manifest links found in the HTML (<link rel="manifest">).
 *   3. Use branded Open Graph / Twitter image metadata only when no icon exists.
 *   4. Probe common well-known icon paths in parallel
 *      (/favicon.png, /apple-touch-icon.png, /favicon.ico, /icons/icon-192.png …).
 *   5. Query multiple public favicon indexes by exact hostname and base domain
 *      only (never the page path or query string), rejecting opaque white/black
 *      canvas results.
 *   6. Cache the original supported image format under
 *      %APPDATA%\WinLauncher\config\favicons\<sha1-of-url>.<format>.
 *      PNG and ICO alpha channels are preserved; no background is added.
 *
 * All network I/O runs inside the caller-owned BackgroundTaskService worker;
 * this module never creates nested fire-and-forget workers.
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
     * @return             Absolute path to the cached image file, or "" on failure.
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
