#include "MigrationBackupService.h"
#include "ConfigPath.h"
#include <Windows.h>
#include <filesystem>
#include <fstream>
#include <vector>
#include <algorithm>

namespace fs = std::filesystem;
static std::wstring Timestamp() { SYSTEMTIME s{}; GetLocalTime(&s); wchar_t b[32]{}; swprintf_s(b,L"%04u%02u%02u-%02u%02u%02u",s.wYear,s.wMonth,s.wDay,s.wHour,s.wMinute,s.wSecond); return b; }
static void PruneAutomaticRestoreBackups(const std::wstring& directory)
{
    std::error_code ec; std::vector<fs::directory_entry> entries;
    for (const auto& entry : fs::directory_iterator(directory, ec)) if (entry.is_regular_file(ec) && entry.path().extension() == L".zip") entries.push_back(entry);
    std::sort(entries.begin(), entries.end(), [&](const auto& a, const auto& b) { return a.last_write_time(ec) > b.last_write_time(ec); });
    for (size_t i = 3; i < entries.size(); ++i) fs::remove(entries[i].path(), ec);
}
bool MigrationBackupService::RunPowerShell(const std::wstring& script) { std::wstring c=L"powershell.exe -NoProfile -NonInteractive -Command \""+script+L"\""; std::vector<wchar_t> b(c.begin(),c.end()); b.push_back(0); STARTUPINFOW si{sizeof(si)}; PROCESS_INFORMATION pi{}; if(!CreateProcessW(nullptr,b.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&si,&pi)) return false; WaitForSingleObject(pi.hProcess,60000); DWORD code=1; GetExitCodeProcess(pi.hProcess,&code); CloseHandle(pi.hThread); CloseHandle(pi.hProcess); return code==0; }
MigrationResult MigrationBackupService::ExportToPath(const std::wstring& destPath) const
{
    const auto root=ConfigPath::GetUserDataDirectory(), stage=root+L"\\migration-stage-"+Timestamp();
    fs::create_directories(stage+L"\\config");
    std::error_code ec;
    const auto config=ConfigPath::GetUserConfigDirectory();
    if(fs::exists(config)) for(const auto& item:fs::recursive_directory_iterator(config,ec)) if(item.is_regular_file() && item.path().filename()!=L"winlauncher.log") { auto rel=fs::relative(item.path(),config,ec); if(!ec) { fs::create_directories((fs::path(stage+L"\\config")/rel).parent_path()); fs::copy_file(item.path(),fs::path(stage+L"\\config")/rel,fs::copy_options::overwrite_existing,ec); } }
    const auto pluginRoot=ConfigPath::GetUserPluginDirectory();
    if(fs::exists(pluginRoot+L"\\state")) fs::copy(pluginRoot+L"\\state",stage+L"\\plugins\\state",fs::copy_options::recursive|fs::copy_options::overwrite_existing,ec);
    const auto installed=pluginRoot+L"\\installed";
    if(fs::exists(installed)) for(const auto& plugin:fs::directory_iterator(installed,ec)) { auto data=plugin.path()/L"data"; if(fs::is_directory(data,ec)) fs::copy(data,fs::path(stage+L"\\plugins\\data")/plugin.path().filename(),fs::copy_options::recursive|fs::copy_options::overwrite_existing,ec); }
    const auto usage=root+L"\\usage_history.json"; if(fs::exists(usage)) fs::copy_file(usage,stage+L"\\usage_history.json",fs::copy_options::overwrite_existing,ec);
    std::wofstream(stage+L"\\manifest.json") << L"{\"schemaVersion\":1,\"createdUtc\":\""<<Timestamp()<<L"\",\"contents\":[\"config\",\"plugins/state\",\"plugins/data\",\"usage_history.json\"],\"pluginsIncluded\":false}";
    bool ok=RunPowerShell(L"Compress-Archive -Path '"+stage+L"\\*' -DestinationPath '"+destPath+L"' -Force"); fs::remove_all(stage,ec);
    return {ok,ok?L"迁移备份已创建":L"迁移备份压缩失败",{}};
}
MigrationResult MigrationBackupService::Export(const std::wstring& destPath) const
{
    return ExportToPath(destPath);
}
MigrationResult MigrationBackupService::Preflight(const std::wstring& zipPath) const
{
    if(zipPath.empty()||!fs::exists(zipPath)||fs::path(zipPath).extension()!=L".zip") return {false,L"请选择有效的迁移 ZIP 文件",{}};
    const auto tmp=ConfigPath::GetUserDataDirectory()+L"\\migration-check-"+Timestamp(); fs::create_directories(tmp); bool ok=RunPowerShell(L"Expand-Archive -LiteralPath '"+zipPath+L"' -DestinationPath '"+tmp+L"' -Force");
    bool valid=ok&&fs::exists(tmp+L"\\manifest.json"); std::error_code ec; if(valid) for(const auto& f:fs::recursive_directory_iterator(tmp,ec)) { auto rel=fs::relative(f.path(),tmp,ec).wstring(); if(rel.find(L"..")!=std::wstring::npos || (rel.rfind(L"config\\",0)!=0 && rel.rfind(L"plugins\\state\\",0)!=0 && rel.rfind(L"plugins\\data\\",0)!=0 && rel!=L"usage_history.json" && rel!=L"manifest.json")) valid=false; }
    fs::remove_all(tmp,ec); return {valid,valid?L"迁移包校验通过":L"迁移包无效、缺少清单或包含不允许的文件",{}};
}
MigrationResult MigrationBackupService::Restore(const std::wstring& zipPath) const
{
    auto check=Preflight(zipPath); if(!check.ok) return check;
    const auto root=ConfigPath::GetUserDataDirectory(), tmp=root+L"\\migration-restore-"+Timestamp(); fs::create_directories(tmp); if(!RunPowerShell(L"Expand-Archive -LiteralPath '"+zipPath+L"' -DestinationPath '"+tmp+L"' -Force")) { std::error_code cleanup; fs::remove_all(tmp,cleanup); return {false,L"无法解压迁移包",{}}; }
    const auto backupDir=ConfigPath::GetUserDataDirectory()+L"\\backups"; const auto autoBackupPath=backupDir+L"\\before-restore-"+Timestamp()+L".zip"; fs::create_directories(backupDir); ExportToPath(autoBackupPath); PruneAutomaticRestoreBackups(backupDir); std::error_code ec;
    if(fs::exists(tmp+L"\\config")) fs::copy(tmp+L"\\config",ConfigPath::GetUserConfigDirectory(),fs::copy_options::recursive|fs::copy_options::overwrite_existing,ec);
    if(fs::exists(tmp+L"\\plugins\\state")) fs::copy(tmp+L"\\plugins\\state",ConfigPath::GetUserPluginStateDirectory(),fs::copy_options::recursive|fs::copy_options::overwrite_existing,ec);
    const auto restoredData = fs::path(tmp + L"\\plugins\\data");
    const auto installed = fs::path(ConfigPath::GetUserPluginInstalledDirectory());
    if (fs::exists(restoredData)) for (const auto& plugin : fs::directory_iterator(restoredData, ec)) {
        const auto destination = installed / plugin.path().filename() / L"data";
        if (fs::is_directory(destination.parent_path(), ec)) fs::copy(plugin.path(), destination, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
    }
    if(fs::exists(tmp+L"\\usage_history.json")) fs::copy_file(tmp+L"\\usage_history.json",root+L"\\usage_history.json",fs::copy_options::overwrite_existing,ec);
    fs::remove_all(tmp,ec); return {true,L"迁移数据已恢复；如备份包含插件私有数据，请重新安装对应插件后使用",{}};
}
