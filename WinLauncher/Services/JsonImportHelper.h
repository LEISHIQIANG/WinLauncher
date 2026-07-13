#pragma once
#include <string>
#include <vector>
#include <fstream>
#include <Windows.h>
#include <wincrypt.h>
#include <algorithm>
#pragma comment(lib, "Crypt32.lib")
#include "../Model/ShortcutInfo.h"
#include "ConfigPath.h"

namespace JsonImport
{

    struct JsonValue
    {
        enum Type { Null, Bool, Number, String, Array, Object };
        Type type = Null;

        bool boolValue = false;
        double numberValue = 0;
        std::wstring stringValue;
        std::vector<JsonValue> arrayValue;
        std::vector<std::pair<std::wstring, JsonValue>> objectValue;

        const JsonValue* Get(const std::wstring& key) const
        {
            for (const auto& [k, v] : objectValue)
                if (k == key) return &v;
            return nullptr;
        }

        const JsonValue* operator[](size_t i) const
        {
            if (i < arrayValue.size()) return &arrayValue[i];
            return nullptr;
        }

        size_t Size() const { return arrayValue.size(); }

        std::wstring GetString(const std::wstring& key, const std::wstring& def = L"") const
        {
            auto* v = Get(key);
            if (v && v->type == String) return v->stringValue;
            return def;
        }

        double GetNumber(const std::wstring& key, double def = 0) const
        {
            auto* v = Get(key);
            if (v && v->type == Number) return v->numberValue;
            return def;
        }

        int GetInt(const std::wstring& key, int def = 0) const
        {
            return (int)GetNumber(key, (double)def);
        }

        bool GetBool(const std::wstring& key, bool def = false) const
        {
            auto* v = Get(key);
            if (v)
            {
                if (v->type == Bool) return v->boolValue;
                if (v->type == Number) return v->numberValue != 0;
            }
            return def;
        }
    };

    static void SkipWhitespace(const wchar_t*& p)
    {
        while (*p == L' ' || *p == L'\t' || *p == L'\r' || *p == L'\n')
            p++;
    }

    static JsonValue ParseString(const wchar_t*& p)
    {
        JsonValue v;
        v.type = JsonValue::String;
        if (*p != L'"') return v;
        p++;
        while (*p && *p != L'"')
        {
            if (*p == L'\\')
            {
                p++;
                switch (*p)
                {
                case L'"': v.stringValue += L'"'; break;
                case L'\\': v.stringValue += L'\\'; break;
                case L'/': v.stringValue += L'/'; break;
                case L'b': v.stringValue += L'\b'; break;
                case L'f': v.stringValue += L'\f'; break;
                case L'n': v.stringValue += L'\n'; break;
                case L'r': v.stringValue += L'\r'; break;
                case L't': v.stringValue += L'\t'; break;
                case L'u':
                {
                    wchar_t hex[5] = {};
                    for (int i = 0; i < 4 && p[1 + i]; i++)
                        hex[i] = p[1 + i];
                    v.stringValue += (wchar_t)wcstol(hex, nullptr, 16);
                    p += 4;
                    break;
                }
                default:
                    v.stringValue += L'\\';
                    v.stringValue += *p;
                    break;
                }
            }
            else
            {
                v.stringValue += *p;
            }
            p++;
        }
        if (*p == L'"') p++;
        return v;
    }

    static JsonValue ParseNumber(const wchar_t*& p)
    {
        JsonValue v;
        v.type = JsonValue::Number;
        const wchar_t* start = p;
        if (*p == L'-') p++;
        while (*p >= L'0' && *p <= L'9') p++;
        if (*p == L'.')
        {
            p++;
            while (*p >= L'0' && *p <= L'9') p++;
        }
        if (*p == L'e' || *p == L'E')
        {
            p++;
            if (*p == L'+' || *p == L'-') p++;
            while (*p >= L'0' && *p <= L'9') p++;
        }
        std::wstring numStr(start, p - start);
        v.numberValue = wcstod(numStr.c_str(), nullptr);
        return v;
    }

    static JsonValue ParseValue(const wchar_t*& p);

    static JsonValue ParseObject(const wchar_t*& p)
    {
        JsonValue v;
        v.type = JsonValue::Object;
        p++;
        SkipWhitespace(p);
        if (*p == L'}') { p++; return v; }
        while (*p)
        {
            SkipWhitespace(p);
            auto key = ParseString(p);
            SkipWhitespace(p);
            if (*p == L':') p++;
            SkipWhitespace(p);
            auto val = ParseValue(p);
            v.objectValue.emplace_back(key.stringValue, std::move(val));
            SkipWhitespace(p);
            if (*p == L',') p++;
            else if (*p == L'}') { p++; break; }
        }
        return v;
    }

    static JsonValue ParseArray(const wchar_t*& p)
    {
        JsonValue v;
        v.type = JsonValue::Array;
        p++;
        SkipWhitespace(p);
        if (*p == L']') { p++; return v; }
        while (*p)
        {
            SkipWhitespace(p);
            v.arrayValue.push_back(ParseValue(p));
            SkipWhitespace(p);
            if (*p == L',') p++;
            else if (*p == L']') { p++; break; }
        }
        return v;
    }

    static JsonValue ParseValue(const wchar_t*& p)
    {
        SkipWhitespace(p);
        switch (*p)
        {
        case L'{': return ParseObject(p);
        case L'[': return ParseArray(p);
        case L'"': return ParseString(p);
        case L't':
            if (wcsncmp(p, L"true", 4) == 0) { p += 4; JsonValue v; v.type = JsonValue::Bool; v.boolValue = true; return v; }
            break;
        case L'f':
            if (wcsncmp(p, L"false", 5) == 0) { p += 5; JsonValue v; v.type = JsonValue::Bool; v.boolValue = false; return v; }
            break;
        case L'n':
            if (wcsncmp(p, L"null", 4) == 0) { p += 4; return JsonValue(); }
            break;
        default:
            if (*p == L'-' || (*p >= L'0' && *p <= L'9'))
                return ParseNumber(p);
            break;
        }
        return JsonValue();
    }

    static JsonValue ParseJson(const std::wstring& content)
    {
        if (content.empty()) return JsonValue();
        const wchar_t* p = content.c_str();
        return ParseValue(p);
    }

    static JsonValue ParseJsonFile(const std::wstring& path)
    {
        std::ifstream fs(path, std::ios::binary);
        if (!fs) return JsonValue();
        std::string bytes((std::istreambuf_iterator<char>(fs)), std::istreambuf_iterator<char>());
        if (bytes.empty()) return JsonValue();

        // Skip UTF-8 BOM
        const char* data = bytes.data();
        size_t len = bytes.size();
        if (len >= 3 && (unsigned char)data[0] == 0xEF && (unsigned char)data[1] == 0xBB && (unsigned char)data[2] == 0xBF)
        {
            data += 3;
            len -= 3;
        }

        int wideLen = MultiByteToWideChar(CP_UTF8, 0, data, (int)len, nullptr, 0);
        if (wideLen <= 0) return JsonValue();
        std::wstring wstr(wideLen, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, data, (int)len, &wstr[0], wideLen);
        return ParseJson(wstr);
    }

    static void NormalizePath(std::wstring& path)
    {
        for (auto& ch : path)
        {
            if (ch == L'/')
                ch = L'\\';
        }
    }

    static bool IsImageFile(const std::wstring& path)
    {
        size_t dot = path.rfind(L'.');
        if (dot == std::wstring::npos) return false;
        std::wstring ext = path.substr(dot);
        for (auto& ch : ext) ch = towlower(ch);
        return ext == L".ico" || ext == L".png" || ext == L".bmp" ||
               ext == L".jpg" || ext == L".jpeg" || ext == L".gif" ||
               ext == L".svg" || ext == L".tiff" || ext == L".tif" ||
               ext == L".webp" || ext == L".cur" || ext == L".dib";
    }

    static std::wstring GetIconFileName(const std::wstring& iconPath, const std::wstring& itemName)
    {
        size_t dot = iconPath.rfind(L'.');
        if (dot != std::wstring::npos)
        {
            std::wstring ext = iconPath.substr(dot);
            std::wstring safeName = itemName;
            for (auto& ch : safeName)
            {
                if (ch == L'/' || ch == L'\\' || ch == L':' || ch == L'*' || ch == L'?' || ch == L'"' || ch == L'<' || ch == L'>' || ch == L'|')
                    ch = L'_';
            }
            // Several QuickLauncher entries can have the same display name.
            // Keep their copied icons separate rather than silently overwriting one.
            unsigned long hash = 2166136261u;
            for (wchar_t ch : iconPath)
                hash = (hash ^ static_cast<unsigned long>(towlower(ch))) * 16777619u;
            return L"imported_icons\\" + safeName + L"_" + std::to_wstring(hash) + ext;
        }
        return L"";
    }

    static bool IsAbsolutePath(const std::wstring& path)
    {
        return path.size() >= 3 && ((path[0] >= L'A' && path[0] <= L'Z') || (path[0] >= L'a' && path[0] <= L'z')) &&
               path[1] == L':' && (path[2] == L'\\' || path[2] == L'/');
    }

    static std::wstring GetParentDirectory(const std::wstring& path)
    {
        const size_t pos = path.find_last_of(L"\\/");
        return pos == std::wstring::npos ? L"" : path.substr(0, pos);
    }

    static std::wstring ResolveRelativePath(const std::wstring& path, const std::wstring& sourceConfigFile)
    {
        if (path.empty() || IsAbsolutePath(path) || path.rfind(L"\\\\", 0) == 0)
            return path;
        const std::wstring parent = GetParentDirectory(sourceConfigFile);
        return parent.empty() ? path : parent + L"\\" + path;
    }

    static std::wstring CopyIconToConfigDir(const std::wstring& sourcePath, const std::wstring& configDir, const std::wstring& itemName)
    {
        if (sourcePath.empty()) return L"";

        std::wstring iconsDir = configDir + L"\\imported_icons";
        ConfigPath::EnsureDirectoryExists(iconsDir);

        std::wstring destFileName = GetIconFileName(sourcePath, itemName);
        if (destFileName.empty()) return L"";

        std::wstring destPath = configDir + L"\\" + destFileName;

        if (CopyFileW(sourcePath.c_str(), destPath.c_str(), FALSE))
        {
            return destPath;
        }

        return sourcePath;
    }

    static std::wstring CopyEmbeddedIconToConfigDir(const std::wstring& iconData, const std::wstring& configDir,
                                                    const std::wstring& itemName)
    {
        if (iconData.empty() || iconData.size() > 10 * 1024 * 1024 || configDir.empty())
            return L"";

        std::wstring payload = iconData;
        std::wstring extension = L".png";
        if (iconData.rfind(L"data:", 0) == 0)
        {
            const size_t comma = iconData.find(L',');
            if (comma == std::wstring::npos) return L"";
            const std::wstring header = iconData.substr(5, comma - 5);
            if (header.find(L"image/jpeg") != std::wstring::npos) extension = L".jpg";
            else if (header.find(L"image/gif") != std::wstring::npos) extension = L".gif";
            else if (header.find(L"image/bmp") != std::wstring::npos) extension = L".bmp";
            else if (header.find(L"image/svg") != std::wstring::npos) extension = L".svg";
            else if (header.find(L"image/x-icon") != std::wstring::npos || header.find(L"image/vnd.microsoft.icon") != std::wstring::npos) extension = L".ico";
            else if (header.find(L"image/webp") != std::wstring::npos) extension = L".webp";
            payload = iconData.substr(comma + 1);
        }

        DWORD byteCount = 0;
        if (!CryptStringToBinaryW(payload.c_str(), static_cast<DWORD>(payload.size()), CRYPT_STRING_BASE64_ANY,
                                  nullptr, &byteCount, nullptr, nullptr) || byteCount == 0 || byteCount > 5 * 1024 * 1024)
            return L"";
        std::vector<BYTE> bytes(byteCount);
        if (!CryptStringToBinaryW(payload.c_str(), static_cast<DWORD>(payload.size()), CRYPT_STRING_BASE64_ANY,
                                  bytes.data(), &byteCount, nullptr, nullptr))
            return L"";

        ConfigPath::EnsureDirectoryExists(configDir + L"\\imported_icons");
        std::wstring virtualSource = itemName + extension;
        std::wstring relative = GetIconFileName(virtualSource, itemName);
        if (relative.empty()) return L"";
        const std::wstring destination = configDir + L"\\" + relative;
        HANDLE file = CreateFileW(destination.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) return L"";
        DWORD written = 0;
        const bool ok = WriteFile(file, bytes.data(), byteCount, &written, nullptr) && written == byteCount;
        CloseHandle(file);
        if (!ok)
        {
            DeleteFileW(destination.c_str());
            return L"";
        }
        return destination;
    }

} // namespace JsonImport
