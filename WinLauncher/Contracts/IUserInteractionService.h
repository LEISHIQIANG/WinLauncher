#pragma once
#include <string>
#include <vector>
#include <Windows.h>

class IUserInteractionService
{
public:
    virtual ~IUserInteractionService() = default;

    virtual bool ShowPrompt(HWND parent, const std::wstring& title, const wchar_t* prompt, std::wstring& value, const wchar_t* defaultText = nullptr) = 0;
    virtual bool ShowMultilinePrompt(HWND parent, const std::wstring& title, const wchar_t* prompt, std::wstring& value, const wchar_t* defaultText = nullptr) = 0;
    virtual bool ShowPasswordPrompt(HWND parent, const std::wstring& title, const wchar_t* prompt, std::wstring& value) = 0;
    virtual bool ShowChoosePrompt(HWND parent, const std::wstring& title, const wchar_t* prompt, const std::vector<std::wstring>& items, std::wstring& value) = 0;
    virtual bool ShowConfirm(HWND parent, const std::wstring& title, const wchar_t* message) = 0;
    virtual bool ConfirmHighRiskCommand(HWND parent, const std::wstring& commandText, const std::wstring& commandName) = 0;
};
