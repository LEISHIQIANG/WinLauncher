#include "MigrationBackupService.h"
#include "ArchiveUtility.h"
#include "ConfigPath.h"

#include <Windows.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <vector>

namespace fs = std::filesystem;

namespace
{
    constexpr size_t kMaxMigrationEntries = 10000;
    constexpr unsigned long long kMaxMigrationUncompressedBytes = 512ull * 1024ull * 1024ull;

    uint16_t ReadU16(const std::vector<unsigned char>& bytes, size_t offset)
    {
        return offset + 2 <= bytes.size() ? static_cast<uint16_t>(bytes[offset] | (bytes[offset + 1] << 8)) : 0;
    }

    uint32_t ReadU32(const std::vector<unsigned char>& bytes, size_t offset)
    {
        return offset + 4 <= bytes.size() ? static_cast<uint32_t>(bytes[offset]) |
            (static_cast<uint32_t>(bytes[offset + 1]) << 8) |
            (static_cast<uint32_t>(bytes[offset + 2]) << 16) |
            (static_cast<uint32_t>(bytes[offset + 3]) << 24) : 0;
    }

    std::wstring Timestamp()
    {
        SYSTEMTIME now{};
        GetLocalTime(&now);
        wchar_t value[32]{};
        swprintf_s(value, L"%04u%02u%02u-%02u%02u%02u", now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond);
        return value;
    }

    bool IsAllowedArchivePath(std::string path)
    {
        std::replace(path.begin(), path.end(), '/', '\\');
        if (path.empty() || path.front() == '\\' || path.find(':') != std::string::npos || path.find('\0') != std::string::npos)
            return false;
        size_t start = 0;
        while (start < path.size())
        {
            const size_t end = path.find('\\', start);
            if (path.substr(start, end == std::string::npos ? std::string::npos : end - start) == "..")
                return false;
            start = end == std::string::npos ? path.size() : end + 1;
        }
        return path == "manifest.json" || path == "usage_history.json" ||
            path.rfind("config\\", 0) == 0 || path.rfind("plugins\\state\\", 0) == 0 ||
            path.rfind("plugins\\data\\", 0) == 0 || path == "config" || path == "plugins" ||
            path == "plugins\\state" || path == "plugins\\data";
    }

    bool ValidateMigrationZip(const std::wstring& zipPath, std::wstring& error)
    {
        std::ifstream file(zipPath, std::ios::binary);
        if (!file) { error = L"无法读取迁移 ZIP 文件"; return false; }
        std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(file)), {});
        if (bytes.size() < 22) { error = L"迁移 ZIP 文件无效"; return false; }

        const size_t searchStart = bytes.size() > 65557 ? bytes.size() - 65557 : 0;
        size_t eocd = bytes.size();
        for (size_t i = bytes.size() - 22;; --i)
        {
            if (ReadU32(bytes, i) == 0x06054b50) { eocd = i; break; }
            if (i == searchStart) break;
        }
        if (eocd == bytes.size()) { error = L"迁移 ZIP 缺少目录"; return false; }
        const uint16_t count = ReadU16(bytes, eocd + 10);
        const uint32_t centralSize = ReadU32(bytes, eocd + 12);
        size_t offset = ReadU32(bytes, eocd + 16);
        if (count == 0 || count == 0xFFFF || count > kMaxMigrationEntries || offset + centralSize > bytes.size())
        {
            error = L"迁移 ZIP 条目数量或目录无效";
            return false;
        }

        unsigned long long totalUncompressed = 0;
        bool hasManifest = false;
        for (uint16_t i = 0; i < count; ++i)
        {
            if (ReadU32(bytes, offset) != 0x02014b50 || offset + 46 > bytes.size())
            {
                error = L"迁移 ZIP 目录条目无效";
                return false;
            }
            const uint16_t flags = ReadU16(bytes, offset + 8);
            const uint32_t uncompressed = ReadU32(bytes, offset + 24);
            const uint16_t nameLength = ReadU16(bytes, offset + 28);
            const uint16_t extraLength = ReadU16(bytes, offset + 30);
            const uint16_t commentLength = ReadU16(bytes, offset + 32);
            const uint32_t externalAttributes = ReadU32(bytes, offset + 38);
            if (flags & 0x0001 || offset + 46ull + nameLength + extraLength + commentLength > bytes.size())
            {
                error = L"迁移 ZIP 包含不支持或损坏的条目";
                return false;
            }
            const std::string name(reinterpret_cast<const char*>(&bytes[offset + 46]), nameLength);
            const unsigned fileType = (externalAttributes >> 16) & 0170000;
            if (fileType == 0120000 || !IsAllowedArchivePath(name))
            {
                error = L"迁移 ZIP 包含不允许的路径或链接";
                return false;
            }
            if (name == "manifest.json") hasManifest = true;
            totalUncompressed += uncompressed;
            if (totalUncompressed > kMaxMigrationUncompressedBytes)
            {
                error = L"迁移 ZIP 解压后大小超过限制";
                return false;
            }
            offset += 46ull + nameLength + extraLength + commentLength;
        }
        if (!hasManifest) error = L"迁移 ZIP 缺少清单文件";
        return hasManifest;
    }

    bool ValidateExtractedManifest(const fs::path& directory)
    {
        std::ifstream file(directory / L"manifest.json", std::ios::binary);
        const std::string contents((std::istreambuf_iterator<char>(file)), {});
        return !contents.empty() && contents.find("\"schemaVersion\":1") != std::string::npos &&
            contents.find("\"pluginsIncluded\":false") != std::string::npos;
    }

    bool CopyTreeMerge(const fs::path& source, const fs::path& destination, std::vector<std::wstring>& warnings)
    {
        std::error_code ec;
        for (fs::recursive_directory_iterator it(source, fs::directory_options::skip_permission_denied, ec), end; it != end; it.increment(ec))
        {
            if (ec) { warnings.push_back(L"读取迁移文件失败"); ec.clear(); continue; }
            if (!it->is_regular_file(ec)) continue;
            if (it->path().filename() == L"winlauncher.log") continue;
            const fs::path relative = fs::relative(it->path(), source, ec);
            if (ec) { warnings.push_back(L"计算迁移文件位置失败"); ec.clear(); continue; }
            fs::create_directories((destination / relative).parent_path(), ec);
            if (ec) { warnings.push_back(L"无法创建迁移目标目录"); ec.clear(); continue; }
            fs::copy_file(it->path(), destination / relative, fs::copy_options::overwrite_existing, ec);
            if (ec) { warnings.push_back(L"无法合并部分迁移数据"); ec.clear(); }
        }
        return warnings.empty();
    }

    void PruneAutomaticRestoreBackups(const std::wstring& directory)
    {
        std::error_code ec;
        std::vector<fs::directory_entry> entries;
        for (const auto& entry : fs::directory_iterator(directory, ec))
            if (entry.is_regular_file(ec) && entry.path().extension() == L".zip") entries.push_back(entry);
        std::sort(entries.begin(), entries.end(), [&](const auto& a, const auto& b) { return a.last_write_time(ec) > b.last_write_time(ec); });
        for (size_t i = 3; i < entries.size(); ++i) fs::remove(entries[i].path(), ec);
    }
}

MigrationResult MigrationBackupService::ExportToPath(const std::wstring& destPath) const
{
    const fs::path root = ConfigPath::GetUserDataDirectory();
    const fs::path stage = root / (L"migration-stage-" + Timestamp());
    std::error_code ec;
    fs::create_directories(stage / L"config", ec);
    std::vector<std::wstring> warnings;
    const fs::path config = ConfigPath::GetUserConfigDirectory();
    if (fs::exists(config, ec)) CopyTreeMerge(config, stage / L"config", warnings);
    const fs::path pluginRoot = ConfigPath::GetUserPluginDirectory();
    if (fs::exists(pluginRoot / L"state", ec)) CopyTreeMerge(pluginRoot / L"state", stage / L"plugins\\state", warnings);
    const fs::path installed = pluginRoot / L"installed";
    if (fs::exists(installed, ec)) for (const auto& plugin : fs::directory_iterator(installed, ec))
        if (fs::is_directory(plugin.path() / L"data", ec)) CopyTreeMerge(plugin.path() / L"data", stage / L"plugins\\data" / plugin.path().filename(), warnings);
    const fs::path usage = root / L"usage_history.json";
    if (fs::exists(usage, ec)) fs::copy_file(usage, stage / L"usage_history.json", fs::copy_options::overwrite_existing, ec);
    if (ec) warnings.push_back(L"无法复制部分使用记录");
    std::wofstream(stage / L"manifest.json") << L"{\"schemaVersion\":1,\"createdUtc\":\"" << Timestamp()
        << L"\",\"contents\":[\"config\",\"plugins/state\",\"plugins/data\",\"usage_history.json\"],\"pluginsIncluded\":false}";

    std::wstring error;
    const bool ok = warnings.empty() && ArchiveUtility::CompressDirectoryContents(stage.wstring(), destPath, 60000, error);
    fs::remove_all(stage, ec);
    return { ok, ok ? L"迁移备份已创建" : (error.empty() ? L"迁移备份未完成" : error), warnings };
}

MigrationResult MigrationBackupService::Export(const std::wstring& destPath) const { return ExportToPath(destPath); }

MigrationResult MigrationBackupService::Preflight(const std::wstring& zipPath) const
{
    if (zipPath.empty() || !fs::exists(zipPath) || fs::path(zipPath).extension() != L".zip")
        return { false, L"请选择有效的迁移 ZIP 文件", {} };
    std::wstring error;
    if (!ValidateMigrationZip(zipPath, error)) return { false, error, {} };
    return { true, L"迁移包预检通过", {} };
}

MigrationResult MigrationBackupService::Restore(const std::wstring& zipPath) const
{
    auto preflight = Preflight(zipPath);
    if (!preflight.ok) return preflight;

    const fs::path root = ConfigPath::GetUserDataDirectory();
    const fs::path stage = root / (L"migration-restore-" + Timestamp());
    std::error_code ec;
    fs::create_directories(stage, ec);
    std::wstring error;
    if (ec || !ArchiveUtility::ExpandArchive(zipPath, stage.wstring(), 60000, error) || !ValidateExtractedManifest(stage))
    {
        fs::remove_all(stage, ec);
        return { false, error.empty() ? L"迁移包清单无效或解压失败" : error, {} };
    }

    const fs::path backupDir = root / L"backups";
    fs::create_directories(backupDir, ec);
    const auto automaticBackup = ExportToPath((backupDir / (L"before-restore-" + Timestamp() + L".zip")).wstring());
    if (!automaticBackup.ok)
    {
        fs::remove_all(stage, ec);
        return { false, L"恢复前保护备份失败：" + automaticBackup.message, automaticBackup.warnings };
    }
    PruneAutomaticRestoreBackups(backupDir.wstring());

    std::vector<std::wstring> warnings;
    if (fs::exists(stage / L"config", ec)) CopyTreeMerge(stage / L"config", ConfigPath::GetUserConfigDirectory(), warnings);
    if (fs::exists(stage / L"plugins\\state", ec)) CopyTreeMerge(stage / L"plugins\\state", ConfigPath::GetUserPluginStateDirectory(), warnings);
    const fs::path restoredData = stage / L"plugins\\data";
    const fs::path installed = ConfigPath::GetUserPluginInstalledDirectory();
    if (fs::exists(restoredData, ec)) for (const auto& plugin : fs::directory_iterator(restoredData, ec))
    {
        const fs::path target = installed / plugin.path().filename() / L"data";
        if (fs::is_directory(target.parent_path(), ec)) CopyTreeMerge(plugin.path(), target, warnings);
        else warnings.push_back(L"已跳过未安装插件的私有数据");
    }
    if (fs::exists(stage / L"usage_history.json", ec))
    {
        fs::copy_file(stage / L"usage_history.json", root / L"usage_history.json", fs::copy_options::overwrite_existing, ec);
        if (ec) { warnings.push_back(L"无法合并使用记录"); ec.clear(); }
    }
    fs::remove_all(stage, ec);
    if (!warnings.empty()) return { false, L"迁移数据仅部分恢复，未重新加载配置", warnings };
    return { true, L"迁移数据已合并恢复；本机额外数据已保留", {} };
}
