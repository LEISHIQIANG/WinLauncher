#include <WinLauncher/WinLauncherPluginABI.h>
#include <cstddef>
#include <cwchar>
#include <algorithm>
#include <string>
#include <vector>

#include <windows.h>
#include <wincrypt.h>

#pragma comment(lib, "crypt32.lib")

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

    wstring StripQuotes(wstring value)
    {
        if (value.size() >= 2 && value.front() == L'"' && value.back() == L'"')
            return value.substr(1, value.size() - 2);
        return value;
    }

    vector<wstring> SelectedFilesFromContext(const WLSlashCommandContextV1* ctx)
    {
        vector<wstring> files;
        if (!ctx ||
            ctx->size < offsetof(WLSlashCommandContextV1, selectedFiles) + sizeof(ctx->selectedFiles) ||
            !ctx->selectedFiles ||
            !*ctx->selectedFiles)
        {
            return files;
        }

        wstring selected = ctx->selectedFiles;
        size_t start = 0;
        while (start < selected.size())
        {
            size_t end = selected.find_first_of(L"\r\n", start);
            wstring path = selected.substr(start, end == wstring::npos ? wstring::npos : end - start);
            if (!path.empty())
                files.push_back(StripQuotes(path));
            if (end == wstring::npos)
                break;
            start = selected.find_first_not_of(L"\r\n", end);
            if (start == wstring::npos)
                break;
        }
        return files;
    }

    wstring JoinSections(const vector<wstring>& sections)
    {
        wstring output;
        for (const auto& section : sections)
        {
            if (!output.empty())
                output += L"\n\n";
            output += section;
        }
        return output;
    }

    // ── File Info ───────────────────────────────────────────────────

    wstring FormatFileTime(const FILETIME& ft)
    {
        SYSTEMTIME st, lst;
        FileTimeToSystemTime(&ft, &st);
        SystemTimeToTzSpecificLocalTime(nullptr, &st, &lst);

        wchar_t buf[64]{};
        swprintf_s(buf, L"%04d-%02d-%02d %02d:%02d:%02d",
            lst.wYear, lst.wMonth, lst.wDay,
            lst.wHour, lst.wMinute, lst.wSecond);
        return buf;
    }

    wstring GetFileInfo(const wstring& path)
    {
        WIN32_FILE_ATTRIBUTE_DATA attr{};
        if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &attr))
            return L"Error: Cannot access file: " + path;

        LARGE_INTEGER size;
        size.LowPart = attr.nFileSizeLow;
        size.HighPart = attr.nFileSizeHigh;

        auto fmt = [](DWORDLONG b) -> wstring {
            const wchar_t* u[] = { L"B", L"KB", L"MB", L"GB", L"TB" };
            int i = 0; double v = (double)b;
            while (v >= 1024.0 && i < 4) { v /= 1024.0; ++i; }
            wchar_t buf[32]{}; swprintf_s(buf, L"%.2f %s", v, u[i]); return buf;
        };

        wchar_t buf[1024]{};
        swprintf_s(buf,
            L"File: %s\n"
            L"Size: %s (%lld bytes)\n"
            L"Created:  %s\n"
            L"Modified: %s\n"
            L"Accessed: %s\n",
            path.c_str(),
            fmt((DWORDLONG)size.QuadPart).c_str(), size.QuadPart,
            FormatFileTime(attr.ftCreationTime).c_str(),
            FormatFileTime(attr.ftLastWriteTime).c_str(),
            FormatFileTime(attr.ftLastAccessTime).c_str()
        );

        wstring result = buf;

        // Attributes
        result += L"Attributes:";
        if (attr.dwFileAttributes & FILE_ATTRIBUTE_READONLY)    result += L" ReadOnly";
        if (attr.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN)      result += L" Hidden";
        if (attr.dwFileAttributes & FILE_ATTRIBUTE_SYSTEM)      result += L" System";
        if (attr.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)   result += L" Directory";
        if (attr.dwFileAttributes & FILE_ATTRIBUTE_ARCHIVE)     result += L" Archive";
        if (attr.dwFileAttributes & FILE_ATTRIBUTE_COMPRESSED)  result += L" Compressed";
        if (attr.dwFileAttributes & FILE_ATTRIBUTE_ENCRYPTED)   result += L" Encrypted";

        return result;
    }

    // ── File Hash ───────────────────────────────────────────────────

    ALG_ID HashAlgorithmId(const wstring& algo)
    {
        if (algo == L"md5") return CALG_MD5;
        if (algo == L"sha1") return CALG_SHA1;
        return CALG_SHA_256;
    }

    const wchar_t* HashAlgorithmName(const wstring& algo)
    {
        if (algo == L"md5") return L"MD5";
        if (algo == L"sha1") return L"SHA-1";
        return L"SHA-256";
    }

    DWORD HashLength(ALG_ID aid)
    {
        if (aid == CALG_MD5) return 16;
        if (aid == CALG_SHA1) return 20;
        return 32;
    }

    string ToUtf8(const wstring& value)
    {
        if (value.empty())
            return {};
        int len = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), (int)value.size(), nullptr, 0, nullptr, nullptr);
        if (len <= 0)
            return {};
        string result((size_t)len, '\0');
        WideCharToMultiByte(CP_UTF8, 0, value.c_str(), (int)value.size(), result.data(), len, nullptr, nullptr);
        return result;
    }

    wstring BytesToHex(const vector<BYTE>& bytes)
    {
        wstring hex(bytes.size() * 2, L'\0');
        for (size_t i = 0; i < bytes.size(); ++i)
            swprintf_s(&hex[i * 2], 3, L"%02X", bytes[i]);
        return hex;
    }

    bool UpdateHashText(HCRYPTHASH hash, const wstring& text)
    {
        string utf8 = ToUtf8(text);
        return utf8.empty() || CryptHashData(hash, reinterpret_cast<const BYTE*>(utf8.data()), (DWORD)utf8.size(), 0) != 0;
    }

    bool UpdateHashFileContent(HCRYPTHASH hash, const wstring& path, wstring& error)
    {
        HANDLE hFile = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hFile == INVALID_HANDLE_VALUE)
        {
            error = L"Cannot open file: " + path;
            return false;
        }

        LARGE_INTEGER size{};
        if (!GetFileSizeEx(hFile, &size))
        {
            CloseHandle(hFile);
            error = L"Cannot read file size: " + path;
            return false;
        }
        if (size.QuadPart > 128LL * 1024LL * 1024LL)
        {
            CloseHandle(hFile);
            error = L"File too large (> 128 MB): " + path;
            return false;
        }

        vector<BYTE> buffer(64 * 1024);
        DWORD read = 0;
        while (ReadFile(hFile, buffer.data(), (DWORD)buffer.size(), &read, nullptr) && read > 0)
        {
            if (!CryptHashData(hash, buffer.data(), read, 0))
            {
                CloseHandle(hFile);
                error = L"Failed to hash file: " + path;
                return false;
            }
        }
        CloseHandle(hFile);
        return true;
    }

    bool ComputeFileDigest(const wstring& path, ALG_ID aid, vector<BYTE>& digest, wstring& error)
    {
        DWORD attrs = GetFileAttributesW(path.c_str());
        if (attrs == INVALID_FILE_ATTRIBUTES)
        {
            error = L"Cannot access file: " + path;
            return false;
        }
        if (attrs & FILE_ATTRIBUTE_DIRECTORY)
        {
            error = L"Path is a folder, not a file: " + path;
            return false;
        }

        HCRYPTPROV provider = 0;
        HCRYPTHASH hash = 0;
        if (!CryptAcquireContextW(&provider, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT))
        {
            error = L"CryptoAPI unavailable";
            return false;
        }
        if (!CryptCreateHash(provider, aid, 0, 0, &hash))
        {
            CryptReleaseContext(provider, 0);
            error = L"CryptoAPI hash unavailable";
            return false;
        }

        bool ok = UpdateHashFileContent(hash, path, error);
        if (ok)
        {
            DWORD len = HashLength(aid);
            digest.assign(len, 0);
            ok = CryptGetHashParam(hash, HP_HASHVAL, digest.data(), &len, 0) != 0;
            if (ok)
                digest.resize(len);
            else
                error = L"Failed to finalize hash";
        }

        CryptDestroyHash(hash);
        CryptReleaseContext(provider, 0);
        return ok;
    }

    bool IsDirectoryPath(const wstring& path)
    {
        DWORD attrs = GetFileAttributesW(path.c_str());
        return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
    }

    bool CollectDirectoryFiles(const wstring& root, const wstring& relativeDir, vector<pair<wstring, wstring>>& files, wstring& error)
    {
        if (files.size() > 5000)
        {
            error = L"Folder contains too many files (> 5000): " + root;
            return false;
        }

        wstring searchDir = relativeDir.empty() ? root : (root + L"\\" + relativeDir);
        WIN32_FIND_DATAW data{};
        HANDLE find = FindFirstFileW((searchDir + L"\\*").c_str(), &data);
        if (find == INVALID_HANDLE_VALUE)
        {
            error = L"Cannot enumerate folder: " + searchDir;
            return false;
        }

        bool ok = true;
        do
        {
            if (wcscmp(data.cFileName, L".") == 0 || wcscmp(data.cFileName, L"..") == 0)
                continue;
            if (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
                continue;

            wstring relative = relativeDir.empty() ? data.cFileName : (relativeDir + L"\\" + data.cFileName);
            wstring full = root + L"\\" + relative;
            if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            {
                if (!CollectDirectoryFiles(root, relative, files, error))
                {
                    ok = false;
                    break;
                }
            }
            else
            {
                files.emplace_back(relative, full);
            }
        } while (FindNextFileW(find, &data));

        FindClose(find);
        return ok;
    }

    bool ComputeDirectoryDigest(const wstring& path, ALG_ID aid, vector<BYTE>& digest, size_t& fileCount, wstring& error)
    {
        vector<pair<wstring, wstring>> files;
        if (!CollectDirectoryFiles(path, L"", files, error))
            return false;
        if (files.empty())
        {
            error = L"Folder has no files: " + path;
            return false;
        }

        sort(files.begin(), files.end(), [](const auto& left, const auto& right) {
            wstring a = left.first;
            wstring b = right.first;
            transform(a.begin(), a.end(), a.begin(), [](wchar_t c) { return (wchar_t)towlower(c); });
            transform(b.begin(), b.end(), b.begin(), [](wchar_t c) { return (wchar_t)towlower(c); });
            return a < b;
        });

        HCRYPTPROV provider = 0;
        HCRYPTHASH hash = 0;
        if (!CryptAcquireContextW(&provider, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT))
        {
            error = L"CryptoAPI unavailable";
            return false;
        }
        if (!CryptCreateHash(provider, aid, 0, 0, &hash))
        {
            CryptReleaseContext(provider, 0);
            error = L"CryptoAPI hash unavailable";
            return false;
        }

        bool ok = true;
        for (const auto& [relativePath, fullPath] : files)
        {
            wstring normalized = relativePath;
            replace(normalized.begin(), normalized.end(), L'\\', L'/');

            LARGE_INTEGER size{};
            HANDLE sizeFile = CreateFileW(fullPath.c_str(), GENERIC_READ, FILE_SHARE_READ,
                nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (sizeFile == INVALID_HANDLE_VALUE || !GetFileSizeEx(sizeFile, &size))
            {
                if (sizeFile != INVALID_HANDLE_VALUE)
                    CloseHandle(sizeFile);
                error = L"Cannot read file size: " + fullPath;
                ok = false;
                break;
            }
            CloseHandle(sizeFile);

            if (!UpdateHashText(hash, L"FILE\t" + normalized + L"\t" + to_wstring(size.QuadPart) + L"\n") ||
                !UpdateHashFileContent(hash, fullPath, error) ||
                !UpdateHashText(hash, L"\nEND_FILE\n"))
            {
                ok = false;
                break;
            }
        }

        if (ok)
        {
            DWORD len = HashLength(aid);
            digest.assign(len, 0);
            ok = CryptGetHashParam(hash, HP_HASHVAL, digest.data(), &len, 0) != 0;
            if (ok)
            {
                digest.resize(len);
                fileCount = files.size();
            }
            else
            {
                error = L"Failed to finalize folder hash";
            }
        }

        CryptDestroyHash(hash);
        CryptReleaseContext(provider, 0);
        return ok;
    }

    wstring GetFileHash(const wstring& path, const wstring& algo)
    {
        ALG_ID aid = HashAlgorithmId(algo);
        const wchar_t* aname = HashAlgorithmName(algo);

        if (IsDirectoryPath(path))
        {
            vector<BYTE> digest;
            size_t fileCount = 0;
            wstring error;
            if (!ComputeDirectoryDigest(path, aid, digest, fileCount, error))
                return L"Error: " + error;
            return path + L" (folder, " + to_wstring(fileCount) + L" files, " + aname + L"):\n" + BytesToHex(digest);
        }

        vector<BYTE> digest;
        wstring error;
        if (!ComputeFileDigest(path, aid, digest, error))
            return L"Error: " + error;
        return path + L" (" + aname + L"):\n" + BytesToHex(digest);
    }

    // ── Plugin ──────────────────────────────────────────────────────

    struct Plugin { const WLHostApiV1* host = nullptr; };

    bool WL_CALL OnLoad(void*)       { return true; }
    void WL_CALL OnUnload(void*)     {}
    bool WL_CALL ExecuteCommand(void*, const WLCommandContextV1*, WLStringResultV1*) { return true; }

    bool WL_CALL ExecuteSlashCommand(void* userData, const WLSlashCommandContextV1* ctx, WLStringResultV1* out)
    {
        (void)userData;
        if (!ctx || !out) return false;

        wstring cmd = ctx->command ? ctx->command : L"";
        wstring args = ctx->args ? ctx->args : L"";

        if (cmd == L"fileinfo")
        {
            if (args.empty())
            {
                auto selectedFiles = SelectedFilesFromContext(ctx);
                if (selectedFiles.empty())
                {
                    WriteResult(out, L"Usage: /fileinfo <file-path>\n"
                        L"Tip: select one or more files in Explorer or on the desktop before opening WinLauncher, then run /fileinfo.");
                    return true;
                }
                vector<wstring> sections;
                for (const auto& file : selectedFiles)
                    sections.push_back(GetFileInfo(file));
                WriteResult(out, JoinSections(sections));
                return true;
            }
            // Strip quotes
            wstring path = args;
            path = StripQuotes(path);
            WriteResult(out, GetFileInfo(path));
        }
        else if (cmd == L"filehash")
        {
            // Parse: [md5|sha1|sha256] <path>
            wstring algo = L"sha256";
            wstring path = L"";
            size_t sp = args.find(L' ');
            wstring first = sp == wstring::npos ? args : args.substr(0, sp);
            for (auto& c : first) c = (wchar_t)towlower(c);
            if (first == L"md5" || first == L"sha1" || first == L"sha256")
            {
                algo = first;
                if (sp != wstring::npos)
                    path = args.substr(sp + 1);
            }
            else
            {
                path = args;
            }
            path = StripQuotes(path);
            vector<wstring> paths;
            if (path.empty())
            {
                paths = SelectedFilesFromContext(ctx);
                if (paths.empty())
                {
                    WriteResult(out, L"Usage: /filehash [md5|sha1|sha256] <file-path>\n"
                        L"Tip: select one or more files in Explorer or on the desktop before opening WinLauncher, then run /filehash.");
                    return true;
                }
            }
            else
            {
                paths.push_back(path);
            }

            vector<wstring> sections;
            for (const auto& file : paths)
                sections.push_back(GetFileHash(file, algo));
            WriteResult(out, JoinSections(sections));
        }
        else
        {
            WriteResult(out, L"Unknown: " + cmd);
        }

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
