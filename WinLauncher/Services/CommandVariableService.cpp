#define NOMINMAX
#include "CommandVariableService.h"
#include "../Config/PromptWindow.h"
#include <algorithm>
#include <wininet.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <shlwapi.h>

#pragma comment(lib, "wininet.lib")
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "shlwapi.lib")

namespace Services
{
    // Helper function to read text from Windows Clipboard
    static std::wstring GetClipboardText()
    {
        std::wstring text;
        if (OpenClipboard(nullptr))
        {
            HANDLE hData = GetClipboardData(CF_UNICODETEXT);
            if (hData)
            {
                wchar_t* pText = static_cast<wchar_t*>(GlobalLock(hData));
                if (pText)
                {
                    text = pText;
                    GlobalUnlock(hData);
                }
            }
            CloseClipboard();
        }
        return text;
    }

    // Helper to get local IPv4 address
    static std::wstring GetLocalLANIP()
    {
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
            return L"127.0.0.1";

        std::wstring ipStr = L"127.0.0.1";
        SOCKET sock = socket(AF_INET, SOCK_DGRAM, 0);
        if (sock != INVALID_SOCKET)
        {
            sockaddr_in loopback;
            loopback.sin_family = AF_INET;
            loopback.sin_addr.s_addr = inet_addr("8.8.8.8");
            loopback.sin_port = htons(80);

            if (connect(sock, reinterpret_cast<sockaddr*>(&loopback), sizeof(loopback)) == 0)
            {
                sockaddr_in name;
                int namelen = sizeof(name);
                if (getsockname(sock, reinterpret_cast<sockaddr*>(&name), &namelen) == 0)
                {
                    char ip[INET_ADDRSTRLEN];
                    if (inet_ntop(AF_INET, &name.sin_addr, ip, sizeof(ip)) != nullptr)
                    {
                        int len = MultiByteToWideChar(CP_ACP, 0, ip, -1, nullptr, 0);
                        if (len > 0)
                        {
                            ipStr.resize(len - 1);
                            MultiByteToWideChar(CP_ACP, 0, ip, -1, &ipStr[0], len - 1);
                        }
                    }
                }
            }
            closesocket(sock);
        }
        WSACleanup();
        return ipStr;
    }

    // Helper to fetch public WAN IPv4 address
    static std::wstring FetchPublicWANIP()
    {
        std::wstring ipStr = L"";
        HINTERNET hInternet = InternetOpenW(L"WinLauncher/1.0", INTERNET_OPEN_TYPE_DIRECT, nullptr, nullptr, 0);
        if (hInternet)
        {
            HINTERNET hUrl = InternetOpenUrlW(hInternet, L"https://api.ipify.org", nullptr, 0, INTERNET_FLAG_RELOAD, 0);
            if (hUrl)
            {
                char buffer[128];
                DWORD read = 0;
                std::string response;
                while (InternetReadFile(hUrl, buffer, sizeof(buffer) - 1, &read) && read > 0)
                {
                    buffer[read] = '\0';
                    response.append(buffer, read);
                }
                InternetCloseHandle(hUrl);

                if (!response.empty())
                {
                    int len = MultiByteToWideChar(CP_UTF8, 0, response.c_str(), -1, nullptr, 0);
                    if (len > 0)
                    {
                        ipStr.resize(len - 1);
                        MultiByteToWideChar(CP_UTF8, 0, response.c_str(), -1, &ipStr[0], len - 1);
                    }
                }
            }
            InternetCloseHandle(hInternet);
        }
        if (ipStr.empty()) ipStr = L"0.0.0.0";
        return ipStr;
    }

    // Helper to get formatted current date
    static std::wstring GetCurrentDateString()
    {
        SYSTEMTIME st;
        GetLocalTime(&st);
        wchar_t buf[64];
        swprintf_s(buf, L"%04d-%02d-%02d", st.wYear, st.wMonth, st.wDay);
        return buf;
    }

    // Helper to get formatted current time
    static std::wstring GetCurrentTimeString()
    {
        SYSTEMTIME st;
        GetLocalTime(&st);
        wchar_t buf[64];
        swprintf_s(buf, L"%02d:%02d:%02d", st.wHour, st.wMinute, st.wSecond);
        return buf;
    }

    // Helper to get application base folder path
    static std::wstring GetAppDir()
    {
        wchar_t path[MAX_PATH];
        GetModuleFileNameW(nullptr, path, MAX_PATH);
        std::wstring appPath(path);
        size_t lastSlash = appPath.find_last_of(L"\\/");
        if (lastSlash != std::wstring::npos)
            return appPath.substr(0, lastSlash);
        return L"";
    }

    // Helper to get config directory path
    static std::wstring GetConfigDir()
    {
        return GetAppDir() + L"\\config";
    }

    // Escapes and quotes an argument depending on shell type
    static std::wstring QuoteArgument(const std::wstring& value, const std::wstring& shellType)
    {
        if (shellType == L"gitbash")
        {
            std::wstring v = value;
            for (auto& c : v) { if (c == L'\\') c = L'/'; }
            std::wstring res = L"'";
            for (wchar_t c : v)
            {
                if (c == L'\'') res += L"'\\''";
                else res += c;
            }
            res += L"'";
            return res;
        }
        else if (shellType == L"powershell")
        {
            std::wstring res = L"'";
            for (wchar_t c : value)
            {
                if (c == L'\'') res += L"''";
                else res += c;
            }
            res += L"'";
            return res;
        }
        else
        {
            std::wstring res = L"\"";
            for (wchar_t c : value)
            {
                if (c == L'"') res += L"\\\"";
                else res += c;
            }
            res += L"\"";
            return res;
        }
    }

    // Parse options from a pipe-delimited string like "A|B|C"
    static std::vector<std::wstring> ParseChooseOptions(const std::wstring& optionsStr)
    {
        std::vector<std::wstring> options;
        size_t start = 0;
        while (start <= optionsStr.size())
        {
            size_t end = optionsStr.find(L'|', start);
            if (end == std::wstring::npos)
            {
                std::wstring opt = optionsStr.substr(start);
                while (!opt.empty() && opt.front() == L' ') opt.erase(opt.begin());
                while (!opt.empty() && opt.back() == L' ') opt.pop_back();
                if (!opt.empty()) options.push_back(opt);
                break;
            }
            std::wstring opt = optionsStr.substr(start, end - start);
            while (!opt.empty() && opt.front() == L' ') opt.erase(opt.begin());
            while (!opt.empty() && opt.back() == L' ') opt.pop_back();
            if (!opt.empty()) options.push_back(opt);
            start = end + 1;
        }
        return options;
    }

    // ──── ResolveInputs ────────────────────────────────────────────────

    bool CommandVariableService::ResolveInputs(HWND parent, const std::wstring& commandText, std::map<std::wstring, std::wstring>& outInputs)
    {
        std::wstring text = commandText;
        size_t pos = 0;

        // We collect prompts deduplicated per variable type, but store per prompt key.
        struct VarPrompt {
            std::wstring type;   // "input", "password", "choose", "confirm"
            std::wstring key;    // dedup key (prompt text or options string)
            std::wstring prompt; // display prompt text
            std::vector<std::wstring> options; // for choose mode
        };
        std::vector<VarPrompt> varPrompts;

        while (true)
        {
            size_t start = text.find(L"{{", pos);
            if (start == std::wstring::npos)
                break;
            size_t end = text.find(L"}}", start);
            if (end == std::wstring::npos)
                break;

            std::wstring spec = text.substr(start + 2, end - (start + 2));
            pos = end + 2;

            while (!spec.empty() && spec.front() == L' ') spec.erase(spec.begin());
            while (!spec.empty() && spec.back() == L' ') spec.pop_back();

            std::wstring specLower = spec;
            for (auto& c : specLower) c = towlower(c);

            // --- input ---
            if (specLower == L"input" || specLower.rfind(L"input:", 0) == 0)
            {
                std::wstring baseKey = spec;
                if (baseKey.size() >= 2 && baseKey.substr(baseKey.size() - 2) == L":q")
                    baseKey = baseKey.substr(0, baseKey.size() - 2);

                std::wstring promptText = L"";
                if (baseKey.size() > 6 && baseKey.substr(0, 6) == L"input:")
                    promptText = baseKey.substr(6);

                while (!promptText.empty() && promptText.front() == L' ') promptText.erase(promptText.begin());
                while (!promptText.empty() && promptText.back() == L' ') promptText.pop_back();

                // Dedup by type + promptText
                bool found = false;
                for (auto& vp : varPrompts)
                {
                    if (vp.type == L"input" && vp.key == promptText) { found = true; break; }
                }
                if (!found)
                    varPrompts.push_back({ L"input", promptText, promptText, {} });
            }
            // --- password ---
            else if (specLower == L"password" || specLower.rfind(L"password:", 0) == 0)
            {
                std::wstring baseKey = spec;
                if (baseKey.size() >= 2 && baseKey.substr(baseKey.size() - 2) == L":q")
                    baseKey = baseKey.substr(0, baseKey.size() - 2);

                std::wstring promptText = L"";
                if (baseKey.size() > 9 && baseKey.substr(0, 9) == L"password:")
                    promptText = baseKey.substr(9);

                while (!promptText.empty() && promptText.front() == L' ') promptText.erase(promptText.begin());
                while (!promptText.empty() && promptText.back() == L' ') promptText.pop_back();

                bool found = false;
                for (auto& vp : varPrompts)
                {
                    if (vp.type == L"password" && vp.key == promptText) { found = true; break; }
                }
                if (!found)
                    varPrompts.push_back({ L"password", promptText, promptText, {} });
            }
            // --- choose ---
            else if (specLower.rfind(L"choose:", 0) == 0)
            {
                std::wstring optionsStr = spec.substr(7); // after "choose:"
                // strip :q suffix if present (handled by quote flag in ResolveVariables)
                if (optionsStr.size() >= 2 && optionsStr.substr(optionsStr.size() - 2) == L":q")
                    optionsStr = optionsStr.substr(0, optionsStr.size() - 2);
                while (!optionsStr.empty() && optionsStr.front() == L' ') optionsStr.erase(optionsStr.begin());

                std::vector<std::wstring> opts = ParseChooseOptions(optionsStr);
                if (opts.size() < 2) continue; // need at least 2 options

                // key is the full options string (for dedup)
                bool found = false;
                for (auto& vp : varPrompts)
                {
                    if (vp.type == L"choose" && vp.key == optionsStr) { found = true; break; }
                }
                if (!found)
                    varPrompts.push_back({ L"choose", optionsStr, L"\u8BF7\u9009\u62E9\u4E00\u9879", opts }); // "请选择一项"
            }
            // --- confirm ---
            else if (specLower == L"confirm" || specLower.rfind(L"confirm:", 0) == 0)
            {
                std::wstring baseKey = spec;
                if (baseKey.size() >= 2 && baseKey.substr(baseKey.size() - 2) == L":q")
                    baseKey = baseKey.substr(0, baseKey.size() - 2);

                std::wstring promptText = L"";
                if (baseKey.size() > 8 && baseKey.substr(0, 8) == L"confirm:")
                    promptText = baseKey.substr(8);

                while (!promptText.empty() && promptText.front() == L' ') promptText.erase(promptText.begin());
                while (!promptText.empty() && promptText.back() == L' ') promptText.pop_back();

                bool found = false;
                for (auto& vp : varPrompts)
                {
                    if (vp.type == L"confirm" && vp.key == promptText) { found = true; break; }
                }
                if (!found)
                    varPrompts.push_back({ L"confirm", promptText, promptText, {} });
            }
        }

        // Process each unique prompt
        for (const auto& vp : varPrompts)
        {
            if (vp.type == L"input")
            {
                std::wstring result;
                std::wstring displayPrompt = vp.prompt.empty() ? L"\u8BF7\u8F93\u5165\u8FD0\u884C\u65F6\u8F93\u5165\u5185\u5BB9:" : vp.prompt;
                if (!PromptWindow::Show(parent, L"\u8FD0\u884C\u65F6\u8F93\u5165", displayPrompt.c_str(), result, L"", nullptr))
                    return false;
                outInputs[L"input:" + vp.key] = result;
            }
            else if (vp.type == L"password")
            {
                std::wstring result;
                std::wstring displayPrompt = vp.prompt.empty() ? L"\u8BF7\u8F93\u5165\u5BC6\u7801:" : vp.prompt;
                if (!PromptWindow::ShowPassword(parent, L"\u5BC6\u7801\u8F93\u5165", displayPrompt.c_str(), result, nullptr))
                    return false;
                outInputs[L"password:" + vp.key] = result;
            }
            else if (vp.type == L"choose")
            {
                std::wstring result;
                if (!PromptWindow::ShowChoose(parent, L"\u8BF7\u9009\u62E9", L"\u8BF7\u9009\u62E9\u4E00\u9879:", vp.options, result, nullptr))
                    return false;
                outInputs[L"choose:" + vp.key] = result;
            }
            else if (vp.type == L"confirm")
            {
                std::wstring msg = vp.prompt.empty() ? L"\u786E\u5B9A\u7EE7\u7EED\u6267\u884C\u5417\uFF1F" : vp.prompt;
                if (!PromptWindow::ShowConfirm(parent, L"\u64CD\u4F5C\u786E\u8BA4", msg.c_str(), nullptr))
                    return false;
                // No value stored; existence in outInputs just signals "confirmed"
                outInputs[L"confirm:" + vp.key] = L"1";
            }
        }

        return true;
    }

    // ──── ResolveVariables ─────────────────────────────────────────────

    std::wstring CommandVariableService::ResolveVariables(
        const std::wstring& commandText,
        const std::wstring& shellType,
        const std::vector<std::wstring>& selectedFiles,
        const std::map<std::wstring, std::wstring>& inputValues
    )
    {
        std::wstring result = L"";
        size_t lastPos = 0;
        size_t pos = 0;

        bool hasCachedWANIP = false;
        std::wstring cachedWANIP = L"";
        bool hasCachedLANIP = false;
        std::wstring cachedLANIP = L"";

        while (true)
        {
            size_t start = commandText.find(L"{{", pos);
            if (start == std::wstring::npos)
            {
                result += commandText.substr(lastPos);
                break;
            }
            size_t end = commandText.find(L"}}", start);
            if (end == std::wstring::npos)
            {
                result += commandText.substr(lastPos);
                break;
            }

            result += commandText.substr(lastPos, start - lastPos);

            std::wstring spec = commandText.substr(start + 2, end - (start + 2));
            pos = end + 2;
            lastPos = pos;

            while (!spec.empty() && spec.front() == L' ') spec.erase(spec.begin());
            while (!spec.empty() && spec.back() == L' ') spec.pop_back();

            std::wstring specLower = spec;
            for (auto& c : specLower) c = towlower(c);

            bool quote = false;
            std::wstring baseKey = specLower;
            if (baseKey.size() >= 2 && baseKey.substr(baseKey.size() - 2) == L":q")
            {
                quote = true;
                baseKey = baseKey.substr(0, baseKey.size() - 2);
            }

            std::wstring value = L"";

            if (baseKey == L"clipboard")
            {
                value = GetClipboardText();
            }
            else if (baseKey == L"selected_file")
            {
                value = selectedFiles.empty() ? L"" : selectedFiles[0];
            }
            else if (baseKey == L"selected_file_name")
            {
                if (!selectedFiles.empty())
                {
                    std::wstring firstFile = selectedFiles[0];
                    size_t slash = firstFile.find_last_of(L"\\/");
                    value = (slash == std::wstring::npos) ? firstFile : firstFile.substr(slash + 1);
                }
            }
            else if (baseKey == L"selected_file_dir" || baseKey == L"selected_file_folder")
            {
                if (!selectedFiles.empty())
                {
                    std::wstring firstFile = selectedFiles[0];
                    size_t slash = firstFile.find_last_of(L"\\/");
                    value = (slash == std::wstring::npos) ? L"" : firstFile.substr(0, slash);
                }
            }
            else if (baseKey == L"selected_files")
            {
                if (quote)
                {
                    std::wstring filesStr = L"";
                    for (size_t i = 0; i < selectedFiles.size(); i++)
                    {
                        if (i > 0) filesStr += L" ";
                        filesStr += QuoteArgument(selectedFiles[i], shellType);
                    }
                    result += filesStr;
                    continue;
                }
                else
                {
                    std::wstring filesStr = L"";
                    for (size_t i = 0; i < selectedFiles.size(); i++)
                    {
                        if (i > 0) filesStr += L"\n";
                        filesStr += selectedFiles[i];
                    }
                    value = filesStr;
                }
            }
            else if (baseKey == L"date")
            {
                value = GetCurrentDateString();
            }
            else if (baseKey == L"time")
            {
                value = GetCurrentTimeString();
            }
            else if (baseKey == L"app_dir")
            {
                value = GetAppDir();
            }
            else if (baseKey == L"config_dir")
            {
                value = GetConfigDir();
            }
            else if (baseKey == L"lan_ip")
            {
                if (!hasCachedLANIP)
                {
                    cachedLANIP = GetLocalLANIP();
                    hasCachedLANIP = true;
                }
                value = cachedLANIP;
            }
            else if (baseKey == L"wan_ip")
            {
                if (!hasCachedWANIP)
                {
                    cachedWANIP = FetchPublicWANIP();
                    hasCachedWANIP = true;
                }
                value = cachedWANIP;
            }
            else if (baseKey == L"input" || baseKey.rfind(L"input:", 0) == 0)
            {
                std::wstring prompt = L"";
                if (baseKey.size() > 6 && baseKey.substr(0, 6) == L"input:")
                    prompt = spec.substr(6);

                while (!prompt.empty() && prompt.front() == L' ') prompt.erase(prompt.begin());
                while (!prompt.empty() && prompt.back() == L' ') prompt.pop_back();
                if (prompt.size() >= 2 && prompt.substr(prompt.size() - 2) == L":q")
                    prompt = prompt.substr(0, prompt.size() - 2);

                std::wstring lookupKey = L"input:" + prompt;
                auto it = inputValues.find(lookupKey);
                if (it != inputValues.end())
                    value = it->second;
                else if (prompt.empty())
                {
                    it = inputValues.find(L"input:");
                    if (it != inputValues.end()) value = it->second;
                }
            }
            else if (baseKey == L"password" || baseKey.rfind(L"password:", 0) == 0)
            {
                std::wstring prompt = L"";
                if (baseKey.size() > 9 && baseKey.substr(0, 9) == L"password:")
                    prompt = spec.substr(9);

                while (!prompt.empty() && prompt.front() == L' ') prompt.erase(prompt.begin());
                while (!prompt.empty() && prompt.back() == L' ') prompt.pop_back();
                if (prompt.size() >= 2 && prompt.substr(prompt.size() - 2) == L":q")
                    prompt = prompt.substr(0, prompt.size() - 2);

                std::wstring lookupKey = L"password:" + prompt;
                auto it = inputValues.find(lookupKey);
                if (it != inputValues.end())
                    value = it->second;
            }
            else if (baseKey.rfind(L"choose:", 0) == 0)
            {
                // Build the key the same way ResolveInputs stored it
                std::wstring optionsStr = spec.substr(7); // after "choose:"
                // strip :q suffix (mirrors ResolveInputs)
                if (optionsStr.size() >= 2 && optionsStr.substr(optionsStr.size() - 2) == L":q")
                    optionsStr = optionsStr.substr(0, optionsStr.size() - 2);
                while (!optionsStr.empty() && optionsStr.front() == L' ') optionsStr.erase(optionsStr.begin());

                std::wstring lookupKey = L"choose:" + optionsStr;
                auto it = inputValues.find(lookupKey);
                if (it != inputValues.end())
                    value = it->second;
            }
            else if (baseKey == L"confirm" || baseKey.rfind(L"confirm:", 0) == 0)
            {
                // confirm: always replaced with empty string (already confirmed in ResolveInputs)
                value = L"";
            }
            else
            {
                result += L"{{" + spec + L"}}";
                continue;
            }

            if (quote)
            {
                result += QuoteArgument(value, shellType);
            }
            else
            {
                result += value;
            }
        }

        return result;
    }
}
