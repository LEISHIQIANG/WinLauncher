#include "DiagnosticService.h"
#include "ConfigPath.h"
#include "../App/Logger.h"
#include "../version.h"
#include <Windows.h>
#include <fstream>
#include <filesystem>
#include <vector>
#include <deque>

DiagnosticService::DiagnosticService(Logger* logger) : m_logger(logger) {}
std::wstring DiagnosticService::GetDirectory() const { auto p=ConfigPath::GetUserDataDirectory()+L"\\diagnostics"; ConfigPath::EnsureDirectoryExists(p); return p; }
std::wstring DiagnosticService::Sanitize(const std::wstring& value) { std::wstring result=value; wchar_t user[MAX_PATH]{}; GetEnvironmentVariableW(L"USERPROFILE",user,MAX_PATH); if (*user) { size_t p; while((p=result.find(user))!=std::wstring::npos) result.replace(p,wcslen(user),L"<user-path>"); } return result; }
bool DiagnosticService::CreatePackage(const std::wstring& destPath, std::wstring& outError) const
{
    wchar_t stamp[32]{}; SYSTEMTIME st{}; GetLocalTime(&st); swprintf_s(stamp,L"%04u%02u%02u-%02u%02u%02u",st.wYear,st.wMonth,st.wDay,st.wHour,st.wMinute,st.wSecond);
    const std::wstring root=GetDirectory(), staging=root+L"\\package-"+stamp, json=staging+L"\\diagnostic.json";
    ConfigPath::EnsureDirectoryExists(staging);
    auto cleanup = [&]() { std::error_code ignored; std::filesystem::remove_all(staging, ignored); };
    std::wofstream manifest(json); if (!manifest) { cleanup(); outError=L"无法创建诊断文件"; return false; }
    SYSTEM_INFO systemInfo{}; GetNativeSystemInfo(&systemInfo);
    manifest << L"{\"schemaVersion\":1,\"app\":{\"version\":\"" << WINLAUNCHER_VERSION_WSTR << L"\"},\"environment\":{\"architecture\":" << systemInfo.wProcessorArchitecture << L"},\"recentEvents\":[],\"crashes\":[],\"plugins\":[],\"sanitization\":\"paths and user content removed\"}"; manifest.close();
    const auto log=ConfigPath::GetUserLogDirectory()+L"\\current.jsonl";
    if (std::filesystem::exists(log))
    {
        std::wifstream in(log); std::deque<std::wstring> recent; std::wstring line;
        while (std::getline(in, line)) { recent.push_back(Sanitize(line)); if (recent.size() > 2000) recent.pop_front(); }
        std::wofstream out(staging+L"\\recent.jsonl"); for (const auto& entry : recent) out << entry << L"\n";
    }
    if (m_logger)
    {
        std::ofstream debug(staging+L"\\debug-ring.jsonl", std::ios::binary | std::ios::trunc);
        for (const auto& entry : m_logger->GetRecentDebugJsonLines()) debug << entry;
    }
    std::wstring command=L"powershell.exe -NoProfile -NonInteractive -Command \"Compress-Archive -Path '"+staging+L"\\*' -DestinationPath '"+destPath+L"' -Force\"";
    STARTUPINFOW si{sizeof(si)}; PROCESS_INFORMATION pi{}; std::vector<wchar_t> cmd(command.begin(),command.end()); cmd.push_back(0);
    if (!CreateProcessW(nullptr,cmd.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&si,&pi)) { cleanup(); outError=L"无法启动本地压缩工具"; return false; }
    WaitForSingleObject(pi.hProcess,30000); DWORD code=1; GetExitCodeProcess(pi.hProcess,&code); CloseHandle(pi.hThread); CloseHandle(pi.hProcess); cleanup();
    if(code!=0 || !std::filesystem::exists(destPath)) { outError=L"诊断包压缩失败"; return false; } LOG_INFO(m_logger,L"Diagnostic package created: %s",destPath.c_str()); return true;
}
