#pragma once
#include <string>
#include <vector>
struct MigrationResult { bool ok = false; std::wstring message; std::vector<std::wstring> warnings; };
class MigrationBackupService
{
public:
    MigrationResult Export(const std::wstring& destPath) const;
    MigrationResult Preflight(const std::wstring& zipPath) const;
    MigrationResult Restore(const std::wstring& zipPath) const;
private:
    MigrationResult ExportToPath(const std::wstring& destPath) const;
};
