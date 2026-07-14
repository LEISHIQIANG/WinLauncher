#include <WinLauncher/WinLauncherPluginABI.h>
#include <algorithm>
#include <cstddef>
#include <cwchar>
#include <cwctype>
#include <string>
#include <vector>

#include <windows.h>
#include <objbase.h>
#include <wincrypt.h>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "crypt32.lib")

namespace
{
    using namespace std;

    // Utilities

    wstring ToLower(wstring s)
    {
        transform(s.begin(), s.end(), s.begin(), [](wchar_t c) { return (wchar_t)towlower(c); });
        return s;
    }

    wstring ToUpper(wstring s)
    {
        transform(s.begin(), s.end(), s.begin(), [](wchar_t c) { return (wchar_t)towupper(c); });
        return s;
    }

    wstring Trim(wstring s)
    {
        size_t b = 0, e = s.size();
        while (b < e && iswspace(s[b])) ++b;
        while (e > b && iswspace(s[e - 1])) --e;
        return s.substr(b, e - b);
    }

    string WideToUtf8(const wstring& value)
    {
        if (value.empty()) return {};
        int len = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), (int)value.size(), nullptr, 0, nullptr, nullptr);
        if (len <= 0) return {};
        string result(len, '\0');
        WideCharToMultiByte(CP_UTF8, 0, value.c_str(), (int)value.size(), result.data(), len, nullptr, nullptr);
        return result;
    }

    wstring Utf8ToWide(const string& value)
    {
        if (value.empty()) return {};
        int len = MultiByteToWideChar(CP_UTF8, 0, value.data(), (int)value.size(), nullptr, 0);
        if (len <= 0) return {};
        wstring result(len, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, value.data(), (int)value.size(), result.data(), len);
        return result;
    }

    void WriteResult(WLStringResultV1* out, const wstring& text)
    {
        if (!out) return;
        uint32_t n = (uint32_t)text.size() + 1;
        out->requiredLength = n;
        if (out->buffer && out->bufferLength >= n)
            wcscpy_s(out->buffer, out->bufferLength, text.c_str());
    }

    wstring ReadFromClipboard(const WLHostApiV1* host)
    {
        if (!host || !host->readClipboardText) return L"";
        WLStringResultV1 r{};
        r.size = sizeof(r);
        r.buffer = nullptr;
        r.bufferLength = 0;
        if (!host->readClipboardText(host->hostContext, &r)) return L"";
        uint32_t len = r.requiredLength;
        if (len <= 1) return L"";
        wstring result(len - 1, L'\0');
        r.buffer = result.data();
        r.bufferLength = len;
        if (!host->readClipboardText(host->hostContext, &r)) return L"";
        return result;
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

    wstring GetTextInput(const WLHostApiV1* host, const wchar_t* title, const wchar_t* prompt)
    {
        wstring data = ReadFromClipboard(host);
        if (data.empty())
            data = PromptText(host, title, prompt);
        return data;
    }

    // Base64

    const wchar_t BASE64_CHARS[] = L"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    wstring Base64Encode(const string& data)
    {
        wstring r;
        r.reserve(((data.size() + 2) / 3) * 4);
        int val = 0, vb = -6;
        for (unsigned char c : data)
        {
            val = (val << 8) + c;
            vb += 8;
            while (vb >= 0) { r.push_back(BASE64_CHARS[(val >> vb) & 0x3F]); vb -= 6; }
        }
        if (vb > -6)
            r.push_back(BASE64_CHARS[((val << 8) >> (vb + 8)) & 0x3F]);
        while (r.size() % 4)
            r.push_back(L'=');
        return r;
    }

    string Base64Decode(const wstring& input)
    {
        string r;
        vector<int> T(256, -1);
        for (int i = 0; i < 64; ++i) T[(int)BASE64_CHARS[i]] = i;

        int val = 0, vb = -8;
        for (wchar_t c : input)
        {
            if (c == L'=') break;
            if (T[(int)c] == -1) continue;
            val = (val << 6) + T[(int)c];
            vb += 6;
            if (vb >= 0) { r.push_back((char)((val >> vb) & 0xFF)); vb -= 8; }
        }
        return r;
    }

    // UUID

    wstring GenerateUUID()
    {
        GUID g;
        if (FAILED(CoCreateGuid(&g))) return L"Error: CoCreateGuid failed";
        wchar_t buf[40]{};
        swprintf_s(buf, L"%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X",
            g.Data1, g.Data2, g.Data3,
            g.Data4[0], g.Data4[1], g.Data4[2], g.Data4[3],
            g.Data4[4], g.Data4[5], g.Data4[6], g.Data4[7]);
        return buf;
    }

    // Hash (MD5 / SHA-1 / SHA-256 via CryptoAPI)

    wstring ComputeHash(const string& data, ALG_ID alg, const wchar_t*& outName)
    {
        HCRYPTPROV hp = 0;
        HCRYPTHASH hh = 0;
        if (!CryptAcquireContextW(&hp, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT))
            return L"Error: CryptoAPI unavailable";

        DWORD hl = 32;
        if (alg == CALG_MD5)        { hl = 16; outName = L"MD5"; }
        else if (alg == CALG_SHA1)  { hl = 20; outName = L"SHA-1"; }
        else if (alg == CALG_SHA_256) { hl = 32; outName = L"SHA-256"; }

        BYTE hb[32]{};
        bool ok = CryptCreateHash(hp, alg, 0, 0, &hh)
            && CryptHashData(hh, (BYTE*)data.data(), (DWORD)data.size(), 0)
            && CryptGetHashParam(hh, HP_HASHVAL, hb, &hl, 0);

        CryptDestroyHash(hh);
        CryptReleaseContext(hp, 0);

        if (!ok) return L"Error: hash computation failed";

        wstring hex(hl * 2, L'\0');
        for (DWORD i = 0; i < hl; ++i)
            swprintf_s(&hex[i * 2], 3, L"%02X", hb[i]);
        return hex;
    }

    // Count

    wstring CountStats(const wstring& text)
    {
        int ch = 0, wd = 0, ln = 0;
        bool inWord = false;
        for (size_t i = 0; i < text.size(); ++i)
        {
            if (text[i] == L'\n') ++ln;
            if (!iswspace(text[i])) { ++ch; inWord = true; }
            else if (inWord) { ++wd; inWord = false; }
        }
        if (inWord) ++wd;
        if (!text.empty() && text.back() != L'\n') ++ln;

        wchar_t buf[256]{};
        swprintf_s(buf, L"Lines: %d  Words: %d  Characters: %d", ln, wd, ch);
        return buf;
    }

    // Plugin state struct

    struct Plugin
    {
        const WLHostApiV1* host = nullptr;
    };

    // Lifecycle

    bool WL_CALL OnLoad(void*)       { return true; }
    void WL_CALL OnUnload(void*)     {}
    bool WL_CALL ExecuteCommand(void*, const WLCommandContextV1*, WLStringResultV1*) { return true; }

    // Command handlers

    void DoBase64(const wstring& sub, Plugin* p, WLStringResultV1* out)
    {
        wstring mode = L"encode", data;
        size_t sp = sub.find(L' ');
        if (sp != wstring::npos) { mode = ToLower(Trim(sub.substr(0, sp))); data = Trim(sub.substr(sp + 1)); }
        else data = sub;

        if (data.empty()) data = GetTextInput(p->host, L"Base64", L"Enter text to encode or decode:");
        if (data.empty()) { WriteResult(out, L"Usage: /base64 [encode|decode] <text>\nDefault: encode"); return; }

        if (mode == L"encode" || mode == L"e")
        {
            string bytes = WideToUtf8(data);
            wstring enc = Base64Encode(bytes);
            CopyToClipboard(p->host, enc);
            WriteResult(out, L"Base64 Encoded (copied):\n" + enc);
        }
        else if (mode == L"decode" || mode == L"d")
        {
            string dec = Base64Decode(data);
            wstring wdec = Utf8ToWide(dec);
            CopyToClipboard(p->host, wdec);
            WriteResult(out, L"Base64 Decoded (copied):\n" + wdec);
        }
        else WriteResult(out, L"Usage: /base64 [encode|decode] <text>");
    }

    void DoUUID(Plugin* p, WLStringResultV1* out)
    {
        wstring uuid = GenerateUUID();
        CopyToClipboard(p->host, uuid);
        WriteResult(out, L"UUID (copied): " + uuid);
    }

    void DoHash(const wstring& sub, Plugin* p, WLStringResultV1* out)
    {
        wstring algo = L"sha256", data = sub;
        size_t sp = sub.find(L' ');
        if (sp != wstring::npos)
        {
            wstring first = ToLower(Trim(sub.substr(0, sp)));
            if (first == L"md5" || first == L"sha1" || first == L"sha256" || first == L"sha-256")
            {
                algo = first;
                data = Trim(sub.substr(sp + 1));
            }
        }
        if (data.empty()) data = GetTextInput(p->host, L"Compute Hash", L"Enter text to hash:");
        if (data.empty()) { WriteResult(out, L"Usage: /hash [md5|sha1|sha256] <text>\nDefault: SHA-256"); return; }

        ALG_ID aid = CALG_SHA_256;
        if (algo == L"md5")  aid = CALG_MD5;
        if (algo == L"sha1") aid = CALG_SHA1;

        const wchar_t* aname = L"SHA-256";
        string bytes = WideToUtf8(data);
        wstring hash = ComputeHash(bytes, aid, aname);

        CopyToClipboard(p->host, hash);
        WriteResult(out, wstring(aname) + L" (copied): " + hash);
    }

    void DoCase(const wstring& sub, Plugin* p, WLStringResultV1* out)
    {
        wstring mode, data;
        size_t sp = sub.find(L' ');
        if (sp != wstring::npos) { mode = ToLower(Trim(sub.substr(0, sp))); data = Trim(sub.substr(sp + 1)); }
        else { mode = ToLower(sub); data = GetTextInput(p->host, L"Case Conversion", L"Enter text to convert:"); }

        if (data.empty()) { WriteResult(out, L"Usage: /case upper|lower <text>"); return; }

        if (mode == L"upper" || mode == L"up" || mode == L"u")
        {
            wstring r = ToUpper(data);
            CopyToClipboard(p->host, r);
            WriteResult(out, L"UPPERCASE (copied):\n" + r);
        }
        else if (mode == L"lower" || mode == L"lo" || mode == L"l")
        {
            wstring r = ToLower(data);
            CopyToClipboard(p->host, r);
            WriteResult(out, L"lowercase (copied):\n" + r);
        }
        else WriteResult(out, L"Usage: /case upper|lower <text>");
    }

    void DoCount(const wstring& data, Plugin* p, WLStringResultV1* out)
    {
        wstring text = data;
        if (text.empty()) text = GetTextInput(p->host, L"Count Text", L"Enter text to count:");
        if (text.empty()) { WriteResult(out, L"Usage: /count <text>\nCounts lines, words, and characters."); return; }

        wstring stats = CountStats(text);
        CopyToClipboard(p->host, stats);
        WriteResult(out, stats + L"\n(copied)");
    }

    void DoReverse(const wstring& data, Plugin* p, WLStringResultV1* out)
    {
        wstring text = data;
        if (text.empty()) text = GetTextInput(p->host, L"Reverse Text", L"Enter text to reverse:");
        if (text.empty()) { WriteResult(out, L"Usage: /reverse <text>"); return; }

        wstring r(text.rbegin(), text.rend());
        CopyToClipboard(p->host, r);
        WriteResult(out, L"Reversed (copied):\n" + r);
    }

    // Slash command dispatch

    bool WL_CALL ExecuteSlashCommand(void* userData, const WLSlashCommandContextV1* ctx, WLStringResultV1* out)
    {
        auto* p = static_cast<Plugin*>(userData);
        if (!ctx || !out) return false;

        wstring cmd = ctx->command ? ctx->command : L"";
        wstring args = ctx->args ? ctx->args : L"";

        if (cmd == L"base64")      DoBase64(args, p, out);
        else if (cmd == L"uuid")   DoUUID(p, out);
        else if (cmd == L"hash")   DoHash(args, p, out);
        else if (cmd == L"case")   DoCase(args, p, out);
        else if (cmd == L"count")  DoCount(args, p, out);
        else if (cmd == L"reverse")DoReverse(args, p, out);
        else WriteResult(out, L"Unknown command: " + cmd);

        return true;
    }

    bool WL_CALL Search(void*, const WLSearchRequestV1*, WLSearchResponseV1*) { return true; }
}

// Exported entry points

WL_EXPORT uint32_t WL_CALL WinLauncherPlugin_GetAbiVersion()
{
    return WINLAUNCHER_PLUGIN_ABI_VERSION;
}

WL_EXPORT bool WL_CALL WinLauncherPlugin_Create(const WLHostApiV1* host, WLPluginInstanceV1** outInstance)
{
    if (!host || !outInstance) return false;

    auto* plugin = new Plugin();
    plugin->host = host;

    auto* inst = new WLPluginInstanceV1();
    inst->size = sizeof(WLPluginInstanceV1);
    inst->userData = plugin;
    inst->onLoad = &OnLoad;
    inst->onUnload = &OnUnload;
    inst->executeCommand = &ExecuteCommand;
    inst->executeSlashCommand = &ExecuteSlashCommand;
    inst->onPopupShown = nullptr;
    inst->onPopupHidden = nullptr;
    inst->search = &Search;
    inst->requestShutdown = nullptr;
    inst->isShutdownComplete = nullptr;

    *outInstance = inst;
    return true;
}

WL_EXPORT void WL_CALL WinLauncherPlugin_Destroy(WLPluginInstanceV1* instance)
{
    if (!instance) return;
    delete static_cast<Plugin*>(instance->userData);
    delete instance;
}
