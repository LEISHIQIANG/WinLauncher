#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <WinLauncher/WinLauncherPluginABI.h>
#include <cstddef>
#include <cwchar>
#include <algorithm>
#include <atomic>
#include <future>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <winhttp.h>
#include <windns.h>
#include <iphlpapi.h>
#include <icmpapi.h>

#pragma comment(lib, "dnsapi.lib")
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

    bool AppendPanel(const WLHostApiV1* host, const wstring& text)
    {
        return HasHostField(host, offsetof(WLHostApiV1, appendResultToPanel) + sizeof(host->appendResultToPanel)) &&
            host->appendResultToPanel &&
            host->appendResultToPanel(host->hostContext, text.c_str());
    }

    bool ToUtf8Z(const wstring& text, string& out)
    {
        out.clear();
        if (text.empty())
            return false;
        int len = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, nullptr, 0, nullptr, nullptr);
        if (len <= 1)
            return false;
        out.assign((size_t)len, '\0');
        return WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, out.data(), len, nullptr, nullptr) > 0;
    }

    bool ToWideZ(const char* text, wstring& out)
    {
        out.clear();
        if (!text || !*text)
            return false;
        int len = MultiByteToWideChar(CP_UTF8, 0, text, -1, nullptr, 0);
        if (len <= 1)
            return false;
        vector<wchar_t> buffer((size_t)len);
        if (MultiByteToWideChar(CP_UTF8, 0, text, -1, buffer.data(), len) <= 0)
            return false;
        out.assign(buffer.data());
        return true;
    }

    wstring ToWideUtf8(const string& text)
    {
        if (text.empty())
            return L"";
        int len = MultiByteToWideChar(CP_UTF8, 0, text.data(), (int)text.size(), nullptr, 0);
        if (len <= 0)
            return L"";
        wstring out((size_t)len, L'\0');
        if (MultiByteToWideChar(CP_UTF8, 0, text.data(), (int)text.size(), out.data(), len) <= 0)
            return L"";
        return out;
    }

    wstring Trim(const wstring& value)
    {
        size_t first = value.find_first_not_of(L" \t\r\n");
        if (first == wstring::npos)
            return L"";
        size_t last = value.find_last_not_of(L" \t\r\n");
        return value.substr(first, last - first + 1);
    }

    vector<wstring> Split(const wstring& value, wchar_t delimiter)
    {
        vector<wstring> parts;
        size_t start = 0;
        while (start <= value.size())
        {
            size_t pos = value.find(delimiter, start);
            if (pos == wstring::npos)
            {
                parts.push_back(value.substr(start));
                break;
            }
            parts.push_back(value.substr(start, pos - start));
            start = pos + 1;
        }
        return parts;
    }

    wstring PadRight(const wstring& value, size_t width)
    {
        if (value.size() >= width)
            return value + L" ";
        return value + wstring(width - value.size() + 1, L' ');
    }

    bool AddrInfoToIpString(const addrinfo* info, wstring& family, wstring& ip)
    {
        family.clear();
        ip.clear();
        if (!info || !info->ai_addr)
            return false;

        char buffer[INET6_ADDRSTRLEN]{};
        const void* addr = nullptr;
        if (info->ai_family == AF_INET)
        {
            if (info->ai_addrlen < sizeof(sockaddr_in))
                return false;
            addr = &reinterpret_cast<const sockaddr_in*>(info->ai_addr)->sin_addr;
            family = L"IPv4";
        }
        else if (info->ai_family == AF_INET6)
        {
            if (info->ai_addrlen < sizeof(sockaddr_in6))
                return false;
            addr = &reinterpret_cast<const sockaddr_in6*>(info->ai_addr)->sin6_addr;
            family = L"IPv6";
        }
        else
        {
            return false;
        }

        if (!inet_ntop(info->ai_family, addr, buffer, sizeof(buffer)))
            return false;
        return ToWideZ(buffer, ip);
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
        result.assign(result.c_str());
        return result;
    }

    wstring HttpGet(const wstring& url);

    // ── Ping ────────────────────────────────────────────────────────

    wstring DoPing(const wstring& target, const WLHostApiV1* host)
    {
        if (target.empty()) return L"Usage: /ping <hostname-or-IP>";

        string mb;
        if (!ToUtf8Z(target, mb))
            return L"Error: Invalid host name: " + target;

        // Resolve hostname to an IPv4 address because IcmpSendEcho is IPv4-only.
        ULONG ipAddr = INADDR_NONE;
        in_addr parsed{};
        if (inet_pton(AF_INET, mb.c_str(), &parsed) == 1)
        {
            ipAddr = parsed.S_un.S_addr;
        }
        else
        {
            addrinfo hints{}, *result = nullptr;
            hints.ai_family = AF_INET;
            if (getaddrinfo(mb.c_str(), nullptr, &hints, &result) != 0 || !result)
                return L"Error: Could not resolve host: " + target;
            bool ok = result->ai_addr && result->ai_addrlen >= sizeof(sockaddr_in);
            if (ok)
                ipAddr = reinterpret_cast<sockaddr_in*>(result->ai_addr)->sin_addr.S_un.S_addr;
            freeaddrinfo(result);
            if (!ok || ipAddr == INADDR_NONE)
                return L"Error: Could not resolve host: " + target;
        }

        // Open ICMP handle
        HANDLE hIcmp = IcmpCreateFile();
        if (hIcmp == INVALID_HANDLE_VALUE)
            return L"Error: IcmpCreateFile failed (requires admin?)";

        wstring result;
        in_addr ia;
        ia.S_un.S_addr = ipAddr;
        char ipBuf[INET_ADDRSTRLEN]{};
        wstring ipText = target;
        if (inet_ntop(AF_INET, &ia, ipBuf, sizeof(ipBuf)))
        {
            wstring convertedIp;
            if (ToWideZ(ipBuf, convertedIp))
                ipText = convertedIp;
        }

        result = L"Pinging " + target + L" [" + ipText + L"]\n";
        bool streaming = AppendPanel(host, result);

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
            if (streaming)
                AppendPanel(host, wstring(line) + L"\n");

            if (i < 3) Sleep(500);
        }

        IcmpCloseHandle(hIcmp);

        // Stats
        int loss = sent ? ((sent - recv) * 100 / sent) : 100;
        wchar_t stats[256]{};
        swprintf_s(stats, L"\nPing statistics: Sent=%d, Received=%d, Lost=%d (%d%% loss)",
            sent, recv, sent - recv, loss);
        result += stats;
        if (streaming)
        {
            AppendPanel(host, stats);
            AppendPanel(host, L"\n");
            return L"Ping completed.";
        }

        return result;
    }

    // ── DNS leak test ───────────────────────────────────────────────

    struct DnsLeakRecord
    {
        wstring ip;
        wstring countryCode;
        wstring country;
        wstring provider;
        wstring type;
    };

    wstring FormatDnsLeakHeader()
    {
        return L"  " + PadRight(L"#", 3) + PadRight(L"IP", 17) + L"Country\n" +
            L"  " + wstring(49, L'-') + L"\n";
    }

    wstring FormatDnsLeakRow(size_t index, const DnsLeakRecord& row)
    {
        return L"  " +
            PadRight(to_wstring(index) + L".", 3) +
            PadRight(row.ip, 17) +
            (row.country.empty() ? L"-" : row.country) +
            L"\n";
    }

    // DnsQueryEx keeps both the cancellation handle and result storage owned by
    // the session until every completion callback has returned. The host can
    // therefore retain this plugin DLL safely while cancellation drains.
    class DnsLeakProbeSession
    {
    public:
        bool Start(const wstring& id)
        {
            lock_guard<recursive_mutex> lock(m_mutex);
            if (!m_requests.empty() && m_completed != m_requests.size())
                return false;
            m_cancelRequested = false;
            m_requests.clear();
            m_completed = 0;
            for (int index = 1; index <= 10; ++index)
            {
                auto item = make_unique<Request>();
                item->owner = this;
                item->host = to_wstring(index) + L"." + id + L".bash.ws";
                item->request.Version = DNS_QUERY_REQUEST_VERSION1;
                item->request.QueryName = item->host.c_str();
                item->request.QueryType = DNS_TYPE_A;
                item->request.QueryOptions = DNS_QUERY_BYPASS_CACHE | DNS_QUERY_NO_HOSTS_FILE;
                item->request.pQueryCompletionCallback = &OnCompleted;
                item->request.pQueryContext = item.get();
                item->result.Version = DNS_QUERY_RESULTS_VERSION1;
                Request* raw = item.get();
                m_requests.push_back(std::move(item));

                const DNS_STATUS status = DnsQueryEx(&raw->request, &raw->result, &raw->cancel);
                if (status != DNS_REQUEST_PENDING)
                    CompleteLocked(raw);
            }
            return true;
        }

        void Cancel()
        {
            lock_guard<recursive_mutex> lock(m_mutex);
            if (m_cancelRequested) return;
            m_cancelRequested = true;
            for (const auto& item : m_requests)
            {
                if (!item->completed)
                    DnsCancelQuery(&item->cancel);
            }
        }

        bool IsComplete() const
        {
            lock_guard<recursive_mutex> lock(m_mutex);
            return m_completed == m_requests.size();
        }

    private:
        struct Request
        {
            DnsLeakProbeSession* owner = nullptr;
            wstring host;
            DNS_QUERY_REQUEST request{};
            DNS_QUERY_RESULT result{};
            DNS_QUERY_CANCEL cancel{};
            bool completed = false;
        };

        static void WINAPI OnCompleted(void* context, DNS_QUERY_RESULT*)
        {
            auto* item = static_cast<Request*>(context);
            if (!item || !item->owner) return;
            lock_guard<recursive_mutex> lock(item->owner->m_mutex);
            item->owner->CompleteLocked(item);
        }

        void CompleteLocked(Request* item)
        {
            if (!item || item->completed) return;
            item->completed = true;
            if (item->result.pQueryRecords)
            {
                DnsRecordListFree(item->result.pQueryRecords, DnsFreeRecordList);
                item->result.pQueryRecords = nullptr;
            }
            ++m_completed;
        }

        mutable recursive_mutex m_mutex;
        vector<unique_ptr<Request>> m_requests;
        size_t m_completed = 0;
        bool m_cancelRequested = false;
    };

    vector<DnsLeakRecord> ParseDnsLeakRows(const wstring& text)
    {
        vector<DnsLeakRecord> records;
        size_t start = 0;
        while (start < text.size())
        {
            size_t end = text.find(L'\n', start);
            wstring line = Trim(text.substr(start, end == wstring::npos ? wstring::npos : end - start));
            start = end == wstring::npos ? text.size() : end + 1;
            if (line.empty())
                continue;

            vector<wstring> fields = Split(line, L'|');
            if (fields.size() < 5)
                continue;

            DnsLeakRecord record;
            record.ip = Trim(fields[0]);
            record.countryCode = Trim(fields[1]);
            record.country = Trim(fields[2]);
            record.provider = Trim(fields[3]);
            record.type = Trim(fields[4]);
            records.push_back(std::move(record));
        }
        return records;
    }

    wstring DoDNSLeakTest(const WLHostApiV1* host, const shared_ptr<DnsLeakProbeSession>& session)
    {
        wstring id = HttpGet(L"https://bash.ws/id");
        id = Trim(id);
        if (id.empty())
            return L"DNS Leak Test failed: could not create a bash.ws test session.";

        bool streaming = AppendPanel(host,
            L"DNS Leak Test (bash.ws)\n"
            L"Session: " + id + L"\n\n"
            L"Detecting DNS servers...\n\n");

        if (!session)
            return L"DNS Leak Test failed: probe session is unavailable.";
        if (!session->Start(id))
            return L"DNS Leak Test is already running. Please wait for it to finish.";

        vector<DnsLeakRecord> rows;
        vector<DnsLeakRecord> streamedDnsRows;
        set<wstring> streamedDns;
        bool streamedPublicIp = false;
        bool streamedHeader = false;
        DWORD startTick = GetTickCount();
        DWORD lastDnsCount = 0;
        DWORD stablePolls = 0;
        while (GetTickCount() - startTick < 7000)
        {
            wstring rowsText = HttpGet(L"https://bash.ws/dnsleak/test/" + id + L"?txt");
            vector<DnsLeakRecord> polledRows = ParseDnsLeakRows(rowsText);
            DWORD dnsCount = 0;
            bool hasConclusion = false;
            for (const auto& row : polledRows)
            {
                if (row.type == L"dns")
                    ++dnsCount;
                else if (row.type == L"conclusion")
                    hasConclusion = true;
            }

            if (!polledRows.empty())
                rows = std::move(polledRows);

            if (streaming)
            {
                for (const auto& row : rows)
                {
                    if (!streamedPublicIp && row.type == L"ip")
                    {
                        wstring line = L"Your public IP:\n  " + row.ip;
                        if (!row.country.empty())
                            line += L"  [" + row.country + L"]";
                        line += L"\n\n";
                        AppendPanel(host, line);
                        streamedPublicIp = true;
                    }
                    else if (row.type == L"dns" && streamedDns.insert(row.ip).second)
                    {
                        if (!streamedHeader)
                        {
                            AppendPanel(host, L"Detected DNS servers:\n" + FormatDnsLeakHeader());
                            streamedHeader = true;
                        }
                        streamedDnsRows.push_back(row);
                        AppendPanel(host, FormatDnsLeakRow(streamedDnsRows.size(), row));
                    }
                }
            }

            if (dnsCount > 0 && dnsCount == lastDnsCount)
                ++stablePolls;
            else
                stablePolls = 0;
            lastDnsCount = dnsCount;

            if (dnsCount > 0 && (hasConclusion || stablePolls >= 2))
                break;
            Sleep(700);
        }

        if (rows.empty())
            return L"DNS Leak Test failed: no result was returned for session " + id + L".";

        vector<DnsLeakRecord> dnsRows;
        vector<DnsLeakRecord> ipRows;
        wstring conclusion;
        set<wstring> seenDns;
        for (const auto& row : rows)
        {
            if (row.type == L"dns" && seenDns.insert(row.ip).second)
                dnsRows.push_back(row);
            else if (row.type == L"ip")
                ipRows.push_back(row);
            else if (row.type == L"conclusion" && !row.ip.empty())
                conclusion = row.ip;
        }

        sort(dnsRows.begin(), dnsRows.end(), [](const DnsLeakRecord& left, const DnsLeakRecord& right) {
            if (left.country != right.country)
                return left.country < right.country;
            if (left.provider != right.provider)
                return left.provider < right.provider;
            return left.ip < right.ip;
        });

        wstring output = L"DNS Leak Test (bash.ws)\n";
        output += L"Session: " + id + L"\n\n";

        if (!ipRows.empty())
        {
            const auto& row = ipRows.front();
            output += L"Your public IP:\n";
            output += L"  " + row.ip;
            if (!row.country.empty())
                output += L"  [" + row.country + L"]";
            output += L"\n\n";
        }

        output += L"Detected DNS servers: " + to_wstring(dnsRows.size()) + L"\n";
        if (dnsRows.empty())
        {
            output += L"  No DNS servers were detected. Try running the test again.\n";
        }
        else
        {
            output += FormatDnsLeakHeader();
            for (size_t i = 0; i < dnsRows.size(); ++i)
                output += FormatDnsLeakRow(i + 1, dnsRows[i]);
        }

        if (!conclusion.empty())
            output += L"\nService conclusion: " + conclusion + L"\n";
        output += L"\nNote: \"may be leaking\" means the detected DNS resolvers do not clearly match the current public network or expected VPN/proxy path. Public DNS, secure DNS, proxy/TUN routing, and resolver anycast can also trigger this warning.";
        if (streaming)
        {
            wstring summary;
            if (!conclusion.empty())
                summary += L"\nService conclusion: " + conclusion + L"\n";
            summary += L"\nDone. Detected " + to_wstring(dnsRows.size()) + L" DNS server(s).\n";
            AppendPanel(host, summary);
            return L"DNS leak test completed.";
        }
        return output;
    }

    // ── Public IP helper via WinHTTP ──────────────────────────────────

    wstring HttpGet(const wstring& url)
    {
        URL_COMPONENTS uc{};
        uc.dwStructSize = sizeof(uc);
        wchar_t host[256]{}, path[1024]{L"/"}, extra[512]{};
        uc.lpszHostName = host; uc.dwHostNameLength = 256;
        uc.lpszUrlPath = path; uc.dwUrlPathLength = 1024;
        uc.lpszExtraInfo = extra; uc.dwExtraInfoLength = 512;

        if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc)) return L"";

        HINTERNET hSession = WinHttpOpen(L"WinLauncher/1.0",
            WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!hSession) return L"";

        WinHttpSetTimeouts(hSession, 3000, 5000, 5000, 5000);

        HINTERNET hConnect = WinHttpConnect(hSession, host, uc.nPort, 0);
        if (!hConnect) { WinHttpCloseHandle(hSession); return L""; }

        DWORD flags = (uc.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
        wstring objectName = wstring(path) + extra;
        HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", objectName.c_str(), nullptr,
            WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
        if (!hRequest) { WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return L""; }

        bool ok = WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
            WINHTTP_NO_REQUEST_DATA, 0, 0, 0) != 0
            && WinHttpReceiveResponse(hRequest, nullptr) != 0;

        if (!ok) { WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return L""; }

        string bytes;
        DWORD avail = 0;
        while (WinHttpQueryDataAvailable(hRequest, &avail) && avail > 0)
        {
            DWORD read = 0;
            vector<char> buf((size_t)avail + 1);
            if (!WinHttpReadData(hRequest, buf.data(), avail, &read)) break;
            bytes.append(buf.data(), buf.data() + read);
        }

        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);

        wstring result = ToWideUtf8(bytes);
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
        shared_ptr<DnsLeakProbeSession> dnsSession = make_shared<DnsLeakProbeSession>();
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

    void WL_CALL RequestShutdown(void* userData)
    {
        auto* p = static_cast<Plugin*>(userData);
        if (p && p->dnsSession)
            p->dnsSession->Cancel();
    }

    bool WL_CALL IsShutdownComplete(void* userData)
    {
        auto* p = static_cast<Plugin*>(userData);
        return !p || !p->dnsSession || p->dnsSession->IsComplete();
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
            WriteResult(out, DoPing(args, p->host));
        }
        else if (cmd == L"dns")
        {
            WriteResult(out, DoDNSLeakTest(p->host, p->dnsSession));
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
    i->requestShutdown = &RequestShutdown; i->isShutdownComplete = &IsShutdownComplete;
    *outInstance = i;
    return true;
}
WL_EXPORT void WL_CALL WinLauncherPlugin_Destroy(WLPluginInstanceV1* i)
{
    if (!i) return;
    delete static_cast<Plugin*>(i->userData);
    delete i;
}
