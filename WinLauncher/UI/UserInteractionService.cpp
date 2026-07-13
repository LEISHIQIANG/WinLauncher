#define NOMINMAX
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include "UserInteractionService.h"
#include "../App/AppContext.h"
#include "../Config/PromptWindow.h"
#include "../Config/ConfirmWindow.h"
#include <regex>

UserInteractionService::UserInteractionService(AppContext* ctx)
    : m_ctx(ctx)
{
}

bool UserInteractionService::ShowPrompt(HWND parent, const std::wstring& title, const wchar_t* prompt, std::wstring& value, const wchar_t* defaultText)
{
    return PromptWindow::Show(parent, title.c_str(), prompt, value, defaultText ? defaultText : L"", m_ctx);
}

bool UserInteractionService::ShowMultilinePrompt(HWND parent, const std::wstring& title, const wchar_t* prompt, std::wstring& value, const wchar_t* defaultText)
{
    return PromptWindow::ShowMultiline(parent, title.c_str(), prompt, value, defaultText ? defaultText : L"", m_ctx);
}

bool UserInteractionService::ShowPasswordPrompt(HWND parent, const std::wstring& title, const wchar_t* prompt, std::wstring& value)
{
    return PromptWindow::ShowPassword(parent, title.c_str(), prompt, value, m_ctx);
}

bool UserInteractionService::ShowChoosePrompt(HWND parent, const std::wstring& title, const wchar_t* prompt, const std::vector<std::wstring>& items, std::wstring& value)
{
    return PromptWindow::ShowChoose(parent, title.c_str(), prompt, items, value, m_ctx);
}

bool UserInteractionService::ShowConfirm(HWND parent, const std::wstring& title, const wchar_t* message)
{
    return PromptWindow::ShowConfirm(parent, title.c_str(), message, m_ctx);
}

struct RiskPattern
{
    std::wregex regex;
    const wchar_t* description;
};

bool UserInteractionService::ConfirmHighRiskCommand(HWND parent, const std::wstring& commandText, const std::wstring& commandName)
{
    static const RiskPattern riskPatterns[] = {
        // --- CMD 递归删除 ---
        { std::wregex(L"\\b(rmdir|rd)\\b.*\\s/s", std::regex_constants::icase), L"递归删除目录 (rmdir /s)" },
        { std::wregex(L"\\b(del|erase)\\b.*\\s/s", std::regex_constants::icase), L"递归删除文件 (del /s)" },
        { std::wregex(L"\\brm\\b.*-[a-zA-Z]*r", std::regex_constants::icase), L"递归删除 (rm -r)" },
        { std::wregex(L"\\b(remove-item|rm|ri)\\b.*-recurse", std::regex_constants::icase), L"PowerShell 递归删除" },
        { std::wregex(L"\\bRemove-Item\\b", std::regex_constants::icase), L"PowerShell 删除操作" },

        // --- 磁盘/系统操作 ---
        { std::wregex(L"\\bformat\\s+[a-zA-Z]:", std::regex_constants::icase), L"格式化磁盘 (format)" },
        { std::wregex(L"\\b(diskpart|bcdedit|bootrec)\\b", std::regex_constants::icase), L"磁盘/启动管理器操作" },
        { std::wregex(L"\\bvssadmin\\s+delete\\s+shadows\\b", std::regex_constants::icase), L"删除卷影副本 (vssadmin)" },
        { std::wregex(L"\\bcipher\\s+/w\\b", std::regex_constants::icase), L"覆写删除数据 (cipher /w)" },
        { std::wregex(L"\\bshutdown\\s+/[srplh]", std::regex_constants::icase), L"关机/重启/注销 (shutdown)" },

        // --- 注册表操作 ---
        { std::wregex(L"\\breg\\s+delete\\b", std::regex_constants::icase), L"注册表项删除 (reg delete)" },
        { std::wregex(L"\\breg\\s+add\\b", std::regex_constants::icase), L"注册表项添加 (reg add)" },
        { std::wregex(L"\\breg\\s+import\\b", std::regex_constants::icase), L"注册表文件导入 (reg import)" },

        // --- 持久化/后门 ---
        { std::wregex(L"\\bschtasks\\s+/create\\b", std::regex_constants::icase), L"创建计划任务 (schtasks)" },
        { std::wregex(L"\\bnet\\s+(user|localgroup)\\s+/add\\b", std::regex_constants::icase), L"创建用户/组 (net user)" },

        // --- 远程下载（常被滥用） ---
        { std::wregex(L"\\bcertutil\\s+-urlcache\\b", std::regex_constants::icase), L"certutil 下载文件" },
        { std::wregex(L"\\bbitsadmin\\s+/transfer\\b", std::regex_constants::icase), L"BITSAdmin 传输文件" },

        // --- 权限修改 ---
        { std::wregex(L"\\btakeown\\s+/[a-z]\\b", std::regex_constants::icase), L"获取文件所有权 (takeown)" },
        { std::wregex(L"\\bicacls\\s+\"?[a-zA-Z]:\\\\", std::regex_constants::icase), L"修改文件权限 (icacls)" },

        // --- PowerShell 高危操作 ---
        { std::wregex(L"\\bInvoke-Expression\\b", std::regex_constants::icase), L"PowerShell 代码执行 (IEX)" },
        { std::wregex(L"\\bInvoke-Command\\b", std::regex_constants::icase), L"PowerShell 远程执行" },
        { std::wregex(L"\\b(Stop|Restart)-Computer\\b", std::regex_constants::icase), L"PowerShell 关机/重启" },
        { std::wregex(L"\\bFormat-Volume\\b", std::regex_constants::icase), L"PowerShell 格式化卷" },
        { std::wregex(L"\\bClear-Content\\b", std::regex_constants::icase), L"PowerShell 清空文件内容" },
        { std::wregex(L"\\bAdd-MpPreference\\b", std::regex_constants::icase), L"修改 Windows Defender 排除项" },
        { std::wregex(L"\\bSet-MpPreference\\s+-DisableRealtimeMonitoring\\b", std::regex_constants::icase), L"禁用 Windows Defender 监控" },
        { std::wregex(L"\\bSet-ExecutionPolicy\\b", std::regex_constants::icase), L"修改 PowerShell 执行策略" },
        { std::wregex(L"\\bDownloadString\\b", std::regex_constants::icase), L"PowerShell 远程下载执行" },
        { std::wregex(L"\\bDownloadFile\\b", std::regex_constants::icase), L"PowerShell 远程下载文件" },
    };

    std::wstring matchedDesc;
    for (const auto& pattern : riskPatterns)
    {
        if (std::regex_search(commandText, pattern.regex))
        {
            matchedDesc = pattern.description;
            break;
        }
    }

    if (!matchedDesc.empty())
    {
        std::wstring promptText = L"检测到命令 [" + commandName + L"] 包含高风险操作：" + matchedDesc + L"\n\n";
        promptText += L"=== 命令内容 ===\n" + commandText + L"\n\n确定要运行该风险命令吗？";
        return ConfirmWindow::Show(parent, L"高风险命令提示", promptText.c_str(), m_ctx, true);
    }
    return true;
}
