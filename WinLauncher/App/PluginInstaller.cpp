#include "PluginInstaller.h"
#include "../Services/ConfigPath.h"
#include <Windows.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace
{
    constexpr size_t kMaxPluginPackageFiles = 200;
    constexpr unsigned long long kMaxPluginPackageBytes = 50ull * 1024ull * 1024ull;

    uint16_t ReadU16(const std::vector<unsigned char>& bytes, size_t offset)
    {
        if (offset + 2 > bytes.size()) return 0;
        return (uint16_t)(bytes[offset] | (bytes[offset + 1] << 8));
    }

    uint32_t ReadU32(const std::vector<unsigned char>& bytes, size_t offset)
    {
        if (offset + 4 > bytes.size()) return 0;
        return (uint32_t)bytes[offset] |
            ((uint32_t)bytes[offset + 1] << 8) |
            ((uint32_t)bytes[offset + 2] << 16) |
            ((uint32_t)bytes[offset + 3] << 24);
    }

    std::wstring Utf8ToWide(const char* data, int len)
    {
        int wideLen = MultiByteToWideChar(CP_UTF8, 0, data, len, nullptr, 0);
        if (wideLen <= 0) return L"";
        std::wstring out(wideLen, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, data, len, &out[0], wideLen);
        return out;
    }

    std::wstring AnsiToWide(const char* data, int len)
    {
        int wideLen = MultiByteToWideChar(CP_ACP, 0, data, len, nullptr, 0);
        if (wideLen <= 0) return L"";
        std::wstring out(wideLen, L'\0');
        MultiByteToWideChar(CP_ACP, 0, data, len, &out[0], wideLen);
        return out;
    }

    std::wstring JoinPath(const std::wstring& base, const std::wstring& name)
    {
        if (base.empty()) return name;
        if (base.back() == L'\\' || base.back() == L'/') return base + name;
        return base + L"\\" + name;
    }

    std::wstring PowerShellSingleQuoted(const std::wstring& value)
    {
        std::wstring out = L"'";
        for (wchar_t ch : value)
        {
            if (ch == L'\'')
                out += L"''";
            else
                out += ch;
        }
        out += L"'";
        return out;
    }
}

bool PluginInstaller::InstallPackage(const std::wstring& packagePath, std::wstring& installedPluginId, std::wstring& error)
{
    installedPluginId.clear();
    error.clear();

    std::vector<ZipEntry> entries;
    if (!ValidatePackage(packagePath, entries, error))
        return false;

    std::wstring tempDir = UniqueCachePath(L"install_extract");
    std::wstring backupDir;
    std::wstring targetDir;

    if (!ConfigPath::EnsureDirectoryExists(tempDir))
    {
        error = L"无法创建插件临时解压目录";
        return false;
    }

    if (!ExtractPackage(packagePath, tempDir, error))
    {
        std::wstring cleanupError;
        RemoveDirectoryTree(tempDir, cleanupError);
        return false;
    }

    PluginManifest manifest;
    std::wstring manifestError;
    if (!PluginManifestReader::LoadFromFile(JoinPath(tempDir, L"plugin.json"), manifest, manifestError))
    {
        error = L"插件包清单无效: " + manifestError;
        std::wstring cleanupError;
        RemoveDirectoryTree(tempDir, cleanupError);
        return false;
    }

    installedPluginId = manifest.id;
    targetDir = JoinPath(ConfigPath::PrepareUserPluginInstalledDirectory(), manifest.id);
    backupDir = UniqueCachePath(L"install_backup_" + manifest.id);

    if (DirectoryExists(targetDir))
    {
        if (!MoveDirectory(targetDir, backupDir, error))
        {
            std::wstring cleanupError;
            RemoveDirectoryTree(tempDir, cleanupError);
            return false;
        }
    }

    if (!MoveDirectory(tempDir, targetDir, error))
    {
        std::wstring rollbackError;
        if (!backupDir.empty() && DirectoryExists(backupDir))
            MoveDirectory(backupDir, targetDir, rollbackError);
        std::wstring cleanupError;
        RemoveDirectoryTree(tempDir, cleanupError);
        if (!rollbackError.empty())
            error += L"; 回滚失败: " + rollbackError;
        return false;
    }

    if (!backupDir.empty())
    {
        std::wstring cleanupError;
        RemoveDirectoryTree(backupDir, cleanupError);
    }
    return true;
}

bool PluginInstaller::InspectPackage(const std::wstring& packagePath, PluginManifest& manifest, std::wstring& error)
{
    manifest = PluginManifest{};
    std::vector<ZipEntry> entries;
    if (!ValidatePackage(packagePath, entries, error))
        return false;

    std::wstring tempDir = UniqueCachePath(L"inspect_extract");
    if (!ConfigPath::EnsureDirectoryExists(tempDir))
    {
        error = L"无法创建插件检查目录";
        return false;
    }

    if (!ExtractPackage(packagePath, tempDir, error))
    {
        std::wstring cleanupError;
        RemoveDirectoryTree(tempDir, cleanupError);
        return false;
    }

    bool ok = PluginManifestReader::LoadFromFile(JoinPath(tempDir, L"plugin.json"), manifest, error);
    std::wstring cleanupError;
    RemoveDirectoryTree(tempDir, cleanupError);
    return ok;
}

bool PluginInstaller::UninstallPlugin(const PluginManifest& manifest, std::wstring& error)
{
    error.clear();
    if (manifest.id.empty() || manifest.rootDirectory.empty())
    {
        error = L"插件信息不完整，无法卸载";
        return false;
    }

    std::wstring backupDir = UniqueCachePath(L"uninstall_backup_" + manifest.id);
    if (!MoveDirectory(manifest.rootDirectory, backupDir, error))
        return false;

    std::wstring cleanupError;
    if (!RemoveDirectoryTree(backupDir, cleanupError))
    {
        error = L"插件目录已移出，但清理备份失败: " + cleanupError;
        return false;
    }
    return true;
}

bool PluginInstaller::ValidatePackage(const std::wstring& packagePath, std::vector<ZipEntry>& entries, std::wstring& error)
{
    entries.clear();
    error.clear();

    std::ifstream fs(packagePath, std::ios::binary | std::ios::ate);
    if (!fs)
    {
        error = L"无法打开插件包";
        return false;
    }

    std::streamoff fileSize = fs.tellg();
    if (fileSize <= 0 || (unsigned long long)fileSize > kMaxPluginPackageBytes + 1024ull * 1024ull)
    {
        error = L"插件包大小超过限制";
        return false;
    }

    fs.seekg(0, std::ios::beg);
    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(fs)), std::istreambuf_iterator<char>());
    if (bytes.size() < 22)
    {
        error = L"插件包不是有效 ZIP";
        return false;
    }

    size_t eocdOffset = std::wstring::npos;
    size_t searchStart = bytes.size() > 66000 ? bytes.size() - 66000 : 0;
    for (size_t i = bytes.size() - 22; i + 1 > searchStart; --i)
    {
        if (ReadU32(bytes, i) == 0x06054b50)
        {
            eocdOffset = i;
            break;
        }
        if (i == 0) break;
    }
    if (eocdOffset == std::wstring::npos)
    {
        error = L"插件包缺少 ZIP 中央目录";
        return false;
    }

    uint16_t entryCount = ReadU16(bytes, eocdOffset + 10);
    uint32_t centralSize = ReadU32(bytes, eocdOffset + 12);
    uint32_t centralOffset = ReadU32(bytes, eocdOffset + 16);
    if (entryCount == 0 || entryCount > kMaxPluginPackageFiles ||
        centralOffset >= bytes.size() || (unsigned long long)centralOffset + centralSize > bytes.size())
    {
        error = L"插件包中央目录无效或文件过多";
        return false;
    }

    bool hasRootManifest = false;
    unsigned long long totalUncompressed = 0;
    size_t offset = centralOffset;
    for (uint16_t i = 0; i < entryCount; ++i)
    {
        if (offset + 46 > bytes.size() || ReadU32(bytes, offset) != 0x02014b50)
        {
            error = L"插件包中央目录项无效";
            return false;
        }

        uint16_t flags = ReadU16(bytes, offset + 8);
        uint32_t uncompressedSize = ReadU32(bytes, offset + 24);
        uint16_t nameLen = ReadU16(bytes, offset + 28);
        uint16_t extraLen = ReadU16(bytes, offset + 30);
        uint16_t commentLen = ReadU16(bytes, offset + 32);
        if ((flags & 0x1) != 0)
        {
            error = L"不支持加密插件包";
            return false;
        }
        if (nameLen == 0 || offset + 46ull + nameLen + extraLen + commentLen > bytes.size())
        {
            error = L"插件包文件名无效";
            return false;
        }

        const char* nameData = reinterpret_cast<const char*>(&bytes[offset + 46]);
        std::wstring name = (flags & (1 << 11))
            ? Utf8ToWide(nameData, nameLen)
            : AnsiToWide(nameData, nameLen);
        std::replace(name.begin(), name.end(), L'/', L'\\');
        if (!IsSafePackagePath(name))
        {
            error = L"插件包包含不安全路径: " + name;
            return false;
        }

        bool directory = !name.empty() && name.back() == L'\\';
        if (!directory)
        {
            totalUncompressed += uncompressedSize;
            if (totalUncompressed > kMaxPluginPackageBytes)
            {
                error = L"插件包解压后大小超过 50MB";
                return false;
            }
        }
        if (name == L"plugin.json")
            hasRootManifest = true;

        entries.push_back(ZipEntry{ name, uncompressedSize, directory });
        offset += 46ull + nameLen + extraLen + commentLen;
    }

    if (!hasRootManifest)
    {
        error = L"插件包根目录缺少 plugin.json";
        return false;
    }
    return true;
}

bool PluginInstaller::ExtractPackage(const std::wstring& packagePath, const std::wstring& destinationDir, std::wstring& error)
{
    std::wstring cacheDir = ConfigPath::PrepareUserPluginCacheDirectory();
    std::wstring zipCopy = UniqueCachePath(L"package") + L".zip";
    if (!CopyFileW(packagePath.c_str(), zipCopy.c_str(), FALSE))
    {
        error = L"无法复制插件包到临时 ZIP: " + std::to_wstring(GetLastError());
        return false;
    }

    std::wstring command =
        L"powershell.exe -NoProfile -ExecutionPolicy Bypass -Command \"$ErrorActionPreference='Stop'; Expand-Archive -LiteralPath " +
        PowerShellSingleQuoted(zipCopy) +
        L" -DestinationPath " +
        PowerShellSingleQuoted(destinationDir) +
        L" -Force\"";

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};
    std::wstring mutableCommand = command;
    BOOL ok = CreateProcessW(nullptr, &mutableCommand[0], nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    if (!ok)
    {
        DeleteFileW(zipCopy.c_str());
        error = L"无法启动 ZIP 解压进程: " + std::to_wstring(GetLastError());
        return false;
    }

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    DeleteFileW(zipCopy.c_str());

    if (exitCode != 0)
    {
        error = L"插件包解压失败，退出码: " + std::to_wstring(exitCode);
        return false;
    }
    return true;
}

bool PluginInstaller::IsSafePackagePath(const std::wstring& path)
{
    if (path.empty()) return false;
    if (path.find(L":") != std::wstring::npos) return false;
    if (path.rfind(L"\\\\", 0) == 0) return false;
    if (path.front() == L'\\' || path.front() == L'/') return false;

    size_t start = 0;
    while (start < path.size())
    {
        size_t end = path.find(L'\\', start);
        std::wstring part = path.substr(start, end == std::wstring::npos ? std::wstring::npos : end - start);
        if (part == L"..")
            return false;
        start = (end == std::wstring::npos) ? path.size() : end + 1;
    }
    return true;
}

bool PluginInstaller::DirectoryExists(const std::wstring& path)
{
    DWORD attrs = GetFileAttributesW(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

bool PluginInstaller::FileExists(const std::wstring& path)
{
    DWORD attrs = GetFileAttributesW(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

bool PluginInstaller::MoveDirectory(const std::wstring& from, const std::wstring& to, std::wstring& error)
{
    std::error_code ec;
    std::filesystem::rename(std::filesystem::path(from), std::filesystem::path(to), ec);
    if (ec)
    {
        error = L"移动目录失败: " + from + L" -> " + to;
        return false;
    }
    return true;
}

bool PluginInstaller::RemoveDirectoryTree(const std::wstring& path, std::wstring& error)
{
    if (path.empty() || !DirectoryExists(path))
        return true;

    std::error_code ec;
    std::filesystem::remove_all(std::filesystem::path(path), ec);
    if (ec)
    {
        error = L"删除目录失败: " + path;
        return false;
    }
    return true;
}

std::wstring PluginInstaller::UniqueCachePath(const std::wstring& prefix)
{
    static std::atomic<uint32_t> s_sequence{ 0 };
    std::wstring cacheDir = ConfigPath::PrepareUserPluginCacheDirectory();
    SYSTEMTIME st{};
    GetLocalTime(&st);
    wchar_t buf[128]{};
    swprintf_s(buf, L"%s_%04u%02u%02u_%02u%02u%02u_%03u_%lu_%u",
        prefix.c_str(),
        st.wYear, st.wMonth, st.wDay,
        st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
        GetCurrentProcessId(),
        s_sequence.fetch_add(1));
    return JoinPath(cacheDir, buf);
}
