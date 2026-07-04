#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <WinLauncher/WinLauncherPluginABI.h>
#include <cstddef>
#include <cwchar>
#include <string>
#include <vector>

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <winhttp.h>
#include <iphlpapi.h>
#include <icmpapi.h>

#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "winhttp.lib")

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

    // ── Ping ────────────────────────────────────────────────────────

    wstring DoPing(const wstring& target)
    {
        if (target.empty()) return L"Usage: /ping <hostname-or-IP>";

        // Resolve hostname to IP
        ULONG ipAddr = inet_addr(nullptr);
        {
            int len = WideCharToMultiByte(CP_UTF8, 0, target.c_str(), -1, nullptr, 0, nullptr, nullptr);
            string mb(len, '\0');
            WideCharToMultiByte(CP_UTF8, 0, target.c_str(), -1, mb.data(), len, nullptr, nullptr);
            ipAddr = inet_addr(mb.c_str());
        }

        if (ipAddr == INADDR_NONE)
        {
            addrinfo hints{}, *result = nullptr;
            hints.ai_family = AF_INET;
            int len = WideCharToMultiByte(CP_UTF8, 0, target.c_str(), -1, nullptr, 0, nullptr, nullptr);
            string mb(len, '\0');
            WideCharToMultiByte(CP_UTF8, 0, target.c_str(), -1, mb.data(), len, nullptr, nullptr);
            if (getaddrinfo(mb.c_str(), nullptr, &hints, &result) != 0 || !result)
                return L"Error: Could not resolve host: " + target;
            ipAddr = ((sockaddr_in*)result->ai_addr)->sin_addr.S_un.S_addr;
            freeaddrinfo(result);
        }

        // Open ICMP handle
        HANDLE hIcmp = IcmpCreateFile();
        if (hIcmp == INVALID_HANDLE_VALUE)
            return L"Error: IcmpCreateFile failed (requires admin?)";

        wstring result;
        wchar_t ipStr[16]{};
        in_addr ia;
        ia.S_un.S_addr = ipAddr;
        WCHAR* ipw = nullptr;
        {
            char ipBuf[16]{};
            inet_ntop(AF_INET, &ia, ipBuf, sizeof(ipBuf));
            int wlen = MultiByteToWideChar(CP_UTF8, 0, ipBuf, -1, nullptr, 0);
            ipw = new WCHAR[wlen];
            MultiByteToWideChar(CP_UTF8, 0, ipBuf, -1, ipw, wlen);
        }

        result = L"Pinging " + target + L" [" + wstring(ipw) + L"]\n";
        delete[] ipw;

        // Send 4 pings
        int sent = 0, recv = 0;
        DWORD times[4]{};
        const DWORD timeout = 3000;

        for (int i = 0; i < 4; ++i)
        {
            char sendData[] = "WinLauncher Ping";
            DWORD replySize = sizeof(ICMP_ECHO_REPLY) + sizeof(sendData) + 8;
            vector<BYTE> replyBuf(replySize);
            DWORD dwRet = IcmpSendEcho(hIcmp, ipAddr, sendData, sizeof(sendData),
                nullptr, replyBuf.data(), replySize, timeout);

            ICMP_ECHO_REPLY* echo = (ICMP_ECHO_REPLY*)replyBuf.data();
            sent++;

            wchar_t line[128]{};
            if (dwRet != 0 && echo->Status == IP_SUCCESS)
            {
                recv++;
                times[i] = echo->RoundTripTime;
                swprintf_s(line, L"  Reply from %s: bytes=%lu time=%lums TTL=%u",
                    target.c_str(), echo->DataSize, echo->RoundTripTime, echo->Options.Ttl);
            }
            else
            {
                times[i] = 0;
                wstring statusStr;
                if (echo->Status == IP_REQ_TIMED_OUT) statusStr = L"Request timed out";
                else if (echo->Status == IP_DEST_HOST_UNREACHABLE) statusStr = L"Host unreachable";
                else statusStr = L"Failed";
                swprintf_s(line, L"  %s", statusStr.c_str());
            }
            result += line;
            result += L"\n";

            if (i < 3) Sleep(500);
        }

        IcmpCloseHandle(hIcmp);

        // Stats
        int loss = sent ? ((sent - recv) * 100 / sent) : 100;
        wchar_t stats[256]{};
        swprintf_s(stats, L"\nPing statistics: Sent=%d, Received=%d, Lost=%d (%d%% loss)",
            sent, recv, sent - recv, loss);
        result += stats;

        return result;
    }

    // ── DNS ─────────────────────────────────────────────────────────

    wstring DoDNS(const wstring& host)
    {
        if (host.empty()) return L"Usage: /dns <hostname>";

        addrinfo hints{}, *result = nullptr;
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;

        int len = WideCharToMultiByte(CP_UTF8, 0, host.c_str(), -1, nullptr, 0, nullptr, nullptr);
        string mb(len, '\0');
        WideCharToMultiByte(CP_UTF8, 0, host.c_str(), -1, mb.data(), len, nullptr, nullptr);

        if (getaddrinfo(mb.c_str(), nullptr, &hints, &result) != 0)
            return L"Error: Could not resolve: " + host;

        wstring output = L"DNS lookup for: " + host + L"\n";

        for (addrinfo* ptr = result; ptr; ptr = ptr->ai_next)
        {
            wchar_t ipStr[64]{};
            void* addr;
            const wchar_t* family;

            if (ptr->ai_family == AF_INET)
            {
                addr = &((sockaddr_in*)ptr->ai_addr)->sin_addr;
                family = L"IPv4";
                char buf4[16]{};
                inet_ntop(ptr->ai_family, addr, buf4, sizeof(buf4));
                MultiByteToWideChar(CP_UTF8, 0, buf4, -1, ipStr, 64);
            }
            else if (ptr->ai_family == AF_INET6)
            {
                addr = &((sockaddr_in6*)ptr->ai_addr)->sin6_addr;
                family = L"IPv6";
                char buf6[64]{};
                inet_ntop(ptr->ai_family, addr, buf6, sizeof(buf6));
                MultiByteToWideChar(CP_UTF8, 0, buf6, -1, ipStr, 64);
            }
            else { continue; }

            wchar_t line[128]{};
            swprintf_s(line, L"  %-6s %s\n", family, ipStr);
            output += line;
        }

        freeaddrinfo(result);
        return output;
    }

    // ── Public IP helper via WinHTTP ──────────────────────────────────

    wstring HttpGet(const wstring& url)
    {
        URL_COMPONENTS uc{};
        uc.dwStructSize = sizeof(uc);
        wchar_t host[256]{}, path[1024]{L"/"};
        uc.lpszHostName = host; uc.dwHostNameLength = 256;
        uc.lpszUrlPath = path; uc.dwUrlPathLength = 1024;

        if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc)) return L"";

        HINTERNET hSession = WinHttpOpen(L"WinLauncher/1.0",
            WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!hSession) return L"";

        WinHttpSetTimeouts(hSession, 3000, 5000, 5000, 5000);

        HINTERNET hConnect = WinHttpConnect(hSession, host, uc.nPort, 0);
        if (!hConnect) { WinHttpCloseHandle(hSession); return L""; }

        DWORD flags = (uc.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
        HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path, nullptr,
            WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
        if (!hRequest) { WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return L""; }

        bool ok = WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
            WINHTTP_NO_REQUEST_DATA, 0, 0, 0) != 0
            && WinHttpReceiveResponse(hRequest, nullptr) != 0;

        if (!ok) { WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return L""; }

        wstring result;
        DWORD avail = 0;
        while (WinHttpQueryDataAvailable(hRequest, &avail) && avail > 0)
        {
            DWORD read = 0;
            vector<char> buf((size_t)avail + 1);
            if (!WinHttpReadData(hRequest, buf.data(), avail, &read)) break;
            result.append(buf.begin(), buf.begin() + read);
        }

        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);

        // Trim whitespace/line endings from IP-only responses
        while (!result.empty() && (result.back() == L'\n' || result.back() == L'\r' || result.back() == L' '))
            result.pop_back();
        return result;
    }

    // ── Local IP ────────────────────────────────────────────────────

    wstring DoLocalIP()
    {
        wstring result = L"Local IP Addresses:\n";

        ULONG bufSize = 15000;
        vector<BYTE> buf(bufSize);
        PIP_ADAPTER_ADDRESSES adapters = (PIP_ADAPTER_ADDRESSES)buf.data();
        ULONG ret = GetAdaptersAddresses(AF_UNSPEC,
            GAA_FLAG_INCLUDE_PREFIX | GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST,
            nullptr, adapters, &bufSize);

        if (ret == ERROR_BUFFER_OVERFLOW)
        {
            buf.resize(bufSize);
            adapters = (PIP_ADAPTER_ADDRESSES)buf.data();
            ret = GetAdaptersAddresses(AF_UNSPEC,
                GAA_FLAG_INCLUDE_PREFIX | GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST,
                nullptr, adapters, &bufSize);
        }

        if (ret != NO_ERROR) return L"Error: GetAdaptersAddresses failed";

        for (PIP_ADAPTER_ADDRESSES a = adapters; a; a = a->Next)
        {
            if (a->OperStatus != IfOperStatusUp) continue;
            for (PIP_ADAPTER_UNICAST_ADDRESS addr = a->FirstUnicastAddress; addr; addr = addr->Next)
            {
                wchar_t ipStr[64]{};
                sockaddr* sa = addr->Address.lpSockaddr;
                if (sa->sa_family == AF_INET)
                {
                    char buf4[16]{};
                    inet_ntop(AF_INET, &((sockaddr_in*)sa)->sin_addr, buf4, sizeof(buf4));
                    MultiByteToWideChar(CP_UTF8, 0, buf4, -1, ipStr, 64);
                }
                else if (sa->sa_family == AF_INET6)
                {
                    char buf6[64]{};
                    inet_ntop(AF_INET6, &((sockaddr_in6*)sa)->sin6_addr, buf6, sizeof(buf6));
                    MultiByteToWideChar(CP_UTF8, 0, buf6, -1, ipStr, 64);
                }
                else continue;

                wchar_t line[256]{};
                swprintf_s(line, L"  %-6s %s  (%s)\n",
                    (sa->sa_family == AF_INET ? L"IPv4" : L"IPv6"), ipStr, a->FriendlyName);
                result += line;
            }
        }

        if (result == L"Local IP Addresses:\n")
            result = L"No active network adapters found.";

        // Query public IPs
        result += L"\nPublic IP Addresses:\n";
        wstring ip4 = HttpGet(L"https://api4.ipify.org");
        result += L"  IPv4    " + (ip4.empty() ? L"(unavailable)" : ip4) + L"\n";

        wstring ip6 = HttpGet(L"https://api6.ipify.org");
        result += L"  IPv6    " + (ip6.empty() ? L"(unavailable / no IPv6 from ISP)" : ip6) + L"\n";

        return result;
    }

    // ── Port check ──────────────────────────────────────────────────

    wstring DoPortCheck(const wstring& args)
    {
        if (args.empty())
            return L"Usage: /port <host> <port>\n  e.g. /port google.com 443";

        size_t sp = args.find_last_of(L' ');
        if (sp == wstring::npos)
            return L"Usage: /port <host> <port>\n  e.g. /port google.com 443";

        wstring host = args.substr(0, sp);
        int port = _wtoi(args.substr(sp + 1).c_str());
        if (port <= 0 || port > 65535)
            return L"Error: Invalid port number";

        // Convert to multibyte
        int len = WideCharToMultiByte(CP_UTF8, 0, host.c_str(), -1, nullptr, 0, nullptr, nullptr);
        string mb(len, '\0');
        WideCharToMultiByte(CP_UTF8, 0, host.c_str(), -1, mb.data(), len, nullptr, nullptr);

        // Resolve
        addrinfo hints{}, *result = nullptr;
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        if (getaddrinfo(mb.c_str(), nullptr, &hints, &result) != 0 || !result)
            return L"Error: Could not resolve: " + host;

        SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (sock == INVALID_SOCKET) { freeaddrinfo(result); return L"Error: socket() failed"; }

        sockaddr_in* sa = (sockaddr_in*)result->ai_addr;
        sa->sin_port = htons((u_short)port);

        // Non-blocking connect with timeout
        u_long mode = 1;
        ioctlsocket(sock, FIONBIO, &mode);

        connect(sock, (sockaddr*)sa, sizeof(*sa));

        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(sock, &fds);
        timeval tv;
        tv.tv_sec = 5;
        tv.tv_usec = 0;

        wstring output;
        if (select(0, nullptr, &fds, nullptr, &tv) > 0)
        {
            output = L"Port " + to_wstring(port) + L" on " + host + L" is OPEN \u2705";
        }
        else
        {
            output = L"Port " + to_wstring(port) + L" on " + host + L" is CLOSED or unreachable \u274C";
        }

        closesocket(sock);
        freeaddrinfo(result);
        return output;
    }

    struct Plugin
    {
        const WLHostApiV1* host = nullptr;
        bool winsockStarted = false;
    };

    bool WL_CALL OnLoad(void* userData)
    {
        auto* p = static_cast<Plugin*>(userData);
        if (!p)
            return false;

        WSADATA wsaData{};
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
            return false;
        p->winsockStarted = true;
        return true;
    }

    void WL_CALL OnUnload(void* userData)
    {
        auto* p = static_cast<Plugin*>(userData);
        if (p && p->winsockStarted)
        {
            WSACleanup();
            p->winsockStarted = false;
        }
    }
    bool WL_CALL ExecuteCommand(void*, const WLCommandContextV1*, WLStringResultV1*) { return true; }

    bool WL_CALL ExecuteSlashCommand(void* userData, const WLSlashCommandContextV1* ctx, WLStringResultV1* out)
    {
        auto* p = static_cast<Plugin*>(userData);
        if (!ctx || !out) return false;
        wstring cmd = ctx->command ? ctx->command : L"";
        wstring args = ctx->args ? ctx->args : L"";

        if (cmd == L"ping")
        {
            if (args.empty())
                args = PromptText(p->host, L"Ping Host", L"Enter hostname or IP address:", L"google.com");
            WriteResult(out, DoPing(args));
        }
        else if (cmd == L"dns")
        {
            if (args.empty())
                args = PromptText(p->host, L"DNS Lookup", L"Enter hostname:", L"example.com");
            WriteResult(out, DoDNS(args));
        }
        else if (cmd == L"ip")         WriteResult(out, DoLocalIP());
        else if (cmd == L"port")
        {
            if (args.empty())
                args = PromptText(p->host, L"Port Check", L"Enter host and port:", L"google.com 443");
            WriteResult(out, DoPortCheck(args));
        }
        else WriteResult(out, L"Unknown: " + cmd);

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
    *outInstance = i;
    return true;
}
WL_EXPORT void WL_CALL WinLauncherPlugin_Destroy(WLPluginInstanceV1* i)
{
    if (!i) return;
    delete static_cast<Plugin*>(i->userData);
    delete i;
}
