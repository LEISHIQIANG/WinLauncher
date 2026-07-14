#include <WinLauncher/WinLauncherPluginABI.h>
#include <cstddef>
#include <cmath>
#include <ctime>
#include <cwchar>
#include <string>

#include <windows.h>

namespace
{
    using namespace std;

    void WriteResult(WLStringResultV1* out, const wstring& text)
    {
        if (!out) return;
        uint32_t n = (uint32_t)text.size() + 1;
        out->requiredLength = n;
        if (out->buffer && out->bufferLength >= n)
            wcscpy_s(out->buffer, out->bufferLength, text.c_str());
    }

    void CopyToClipboard(const WLHostApiV1* host, const wstring& text)
    {
        if (host && host->writeClipboardText)
            host->writeClipboardText(host->hostContext, text.c_str());
    }

    bool HasHostField(const WLHostApiV1* host, size_t fieldEnd)
    {
        return host && host->size >= fieldEnd;
    }

    wstring PromptText(const WLHostApiV1* host, const wchar_t* title, const wchar_t* prompt, const wchar_t* defaultText = L"")
    {
        if (!HasHostField(host, offsetof(WLHostApiV1, showInputDialog) + sizeof(host->showInputDialog)) || !host->showInputDialog)
            return L"";

        wstring result(4096, L'\0');
        WLStringResultV1 out{};
        out.size = sizeof(out);
        out.buffer = result.data();
        out.bufferLength = (uint32_t)result.size();
        if (!host->showInputDialog(host->hostContext, title, prompt, defaultText, &out))
            return L"";
        if (!result.empty() && result.back() == L'\0')
            result.pop_back();
        return result;
    }

    // ── Timestamp conversion ────────────────────────────────────────

    wstring DoTimestamp(const wstring& args, const WLHostApiV1* host)
    {
        // If no args, output current timestamps
        if (args.empty())
        {
            __time64_t now = _time64(nullptr);
            SYSTEMTIME st;
            GetLocalTime(&st);
            FILETIME ft;
            SystemTimeToFileTime(&st, &ft);
            ULARGE_INTEGER uli;
            uli.LowPart = ft.dwLowDateTime;
            uli.HighPart = ft.dwHighDateTime;
            __time64_t ms = (uli.QuadPart - 116444736000000000ULL) / 10000;

            wchar_t buf[512]{};
            wchar_t dateStr[64]{};
            swprintf_s(dateStr, L"%04d-%02d-%02d %02d:%02d:%02d",
                st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

            swprintf_s(buf,
                L"Current time (local):\n"
                L"  %s\n\n"
                L"Timestamp (seconds):  %lld\n"
                L"Timestamp (milliseconds): %lld\n\n"
                L"Usage: /timestamp <number>\n"
                L"  Converts a Unix timestamp to human-readable format.\n"
                L"  10 digits = seconds, 13 digits = milliseconds.",
                dateStr, (long long)now, (long long)ms);

            CopyToClipboard(host, to_wstring(now));
            return wstring(buf) + L"\n\nSecond timestamp copied to clipboard.";
        }

        // Parse input: convert timestamp to date
        long long ts = _wtoi64(args.c_str());
        if (ts <= 0)
            return L"Error: invalid timestamp. Must be a positive number.";

        // Auto-detect seconds vs milliseconds (13 digits ~= millis)
        if (ts > 1e12) ts /= 1000;

        // Convert to local time
        time_t t = (time_t)ts;
        struct tm ltm;
        localtime_s(&ltm, &t);

        wchar_t dateStr[64]{};
        wcsftime(dateStr, sizeof(dateStr) / sizeof(wchar_t), L"%Y-%m-%d %H:%M:%S", &ltm);

        wchar_t buf[512]{};
        swprintf_s(buf, L"Timestamp:  %lld\nLocal time: %s", ts, dateStr);

        CopyToClipboard(host, wstring(dateStr));
        return wstring(buf) + L"\n\nDate string copied to clipboard.";
    }

    // ── Countdown ───────────────────────────────────────────────────

    wstring DoCountdown(wstring args, const WLHostApiV1* host)
    {
        if (args.empty())
            args = PromptText(host, L"Countdown", L"Enter seconds and optional message:", L"300 tea timer");
        if (args.empty())
            return L"Usage: /countdown <seconds> [message]\n"
                L"  e.g. /countdown 30\n"
                L"  e.g. /countdown 300 tea timer";

        // Parse seconds
        size_t sp = args.find(L' ');
        wstring secStr = (sp != wstring::npos) ? args.substr(0, sp) : args;
        wstring msg = (sp != wstring::npos) ? args.substr(sp + 1) : L"";

        int seconds = _wtoi(secStr.c_str());
        if (seconds <= 0 || seconds > 86400)
            return L"Error: seconds must be between 1 and 86400.";

        // Calculate target time
        __time64_t now = _time64(nullptr);
        __time64_t target = now + seconds;
        struct tm ttm;
        localtime_s(&ttm, &target);

        wchar_t targetStr[32]{};
        wcsftime(targetStr, sizeof(targetStr) / sizeof(wchar_t), L"%H:%M:%S", &ttm);

        wchar_t buf[256]{};
        if (!msg.empty())
            swprintf_s(buf, L"Countdown started!\n"
                           L"  Duration: %d seconds\n"
                           L"  Message: %s\n"
                           L"  Target time: %s\n\n"
                           L"(Note: In-plugin countdown is not yet implemented.\n"
                           L" The target time is shown above.)",
                       seconds, msg.c_str(), targetStr);
        else
            swprintf_s(buf, L"Countdown started!\n"
                           L"  Duration: %d seconds\n"
                           L"  Target time: %s\n\n"
                           L"(Note: In-plugin countdown is not yet implemented.\n"
                           L" The target time is shown above.)",
                       seconds, targetStr);

        return buf;
    }

    // ── World Clock ─────────────────────────────────────────────────

    struct TZEntry { const wchar_t* city; int offset; }; // offset in hours from UTC

    TZEntry TZ_DATA[] = {
        { L"UTC",             0 },
        { L"San Francisco",  -7 },
        { L"New York",       -4 },
        { L"London",          1 },
        { L"Paris/Berlin",    2 },
        { L"Moscow",          3 },
        { L"Dubai",           4 },
        { L"Beijing",         8 },
        { L"Tokyo",           9 },
        { L"Sydney",         10 },
        { L"Auckland",       12 },
    };

    wstring DoWorldClock()
    {
        __time64_t now = _time64(nullptr);

        wstring result = L"World Clock:\n\n";
        for (const auto& tz : TZ_DATA)
        {
            __time64_t local = now + tz.offset * 3600;
            struct tm ltm;
            gmtime_s(&ltm, &local);
            wchar_t timeStr[16]{};
            wcsftime(timeStr, sizeof(timeStr) / sizeof(wchar_t), L"%H:%M", &ltm);

            wchar_t line[128]{};
            const wchar_t* offsetSign = tz.offset >= 0 ? L"+" : L"";
            swprintf_s(line, L"  %-16s  %s  (UTC%s%d)\n",
                tz.city, timeStr, offsetSign, tz.offset);
            result += line;
        }

        result += L"\nNote: Offsets do not account for DST.";

        return result;
    }

    struct Plugin { const WLHostApiV1* host = nullptr; };

    bool WL_CALL OnLoad(void*)       { return true; }
    void WL_CALL OnUnload(void*)     {}
    bool WL_CALL ExecuteCommand(void*, const WLCommandContextV1*, WLStringResultV1*) { return true; }

    bool WL_CALL ExecuteSlashCommand(void* userData, const WLSlashCommandContextV1* ctx, WLStringResultV1* out)
    {
        auto* p = static_cast<Plugin*>(userData);
        if (!ctx || !out) return false;
        wstring cmd = ctx->command ? ctx->command : L"";
        wstring args = ctx->args ? ctx->args : L"";

        if (cmd == L"timestamp")
            WriteResult(out, DoTimestamp(args, p ? p->host : nullptr));
        else if (cmd == L"countdown")
            WriteResult(out, DoCountdown(args, p ? p->host : nullptr));
        else if (cmd == L"worldclock")
            WriteResult(out, DoWorldClock());
        else
            WriteResult(out, L"Unknown: " + cmd);

        return true;
    }

    bool WL_CALL Search(void*, const WLSearchRequestV1*, WLSearchResponseV1*) { return true; }
}

WL_EXPORT uint32_t WL_CALL WinLauncherPlugin_GetAbiVersion() { return WINLAUNCHER_PLUGIN_ABI_VERSION; }
WL_EXPORT bool WL_CALL WinLauncherPlugin_Create(const WLHostApiV1* host, WLPluginInstanceV1** outInstance)
{
    if (!host || !outInstance) return false;
    auto* p = new Plugin(); p->host = host;
    auto* i = new WLPluginInstanceV1();
    i->size = sizeof(WLPluginInstanceV1);
    i->userData = p;
    i->onLoad = &OnLoad; i->onUnload = &OnUnload;
    i->executeCommand = &ExecuteCommand; i->executeSlashCommand = &ExecuteSlashCommand;
    i->onPopupShown = nullptr; i->onPopupHidden = nullptr; i->search = &Search;
    i->requestShutdown = nullptr; i->isShutdownComplete = nullptr;
    *outInstance = i;
    return true;
}
WL_EXPORT void WL_CALL WinLauncherPlugin_Destroy(WLPluginInstanceV1* i)
{
    if (!i) return;
    delete static_cast<Plugin*>(i->userData);
    delete i;
}
