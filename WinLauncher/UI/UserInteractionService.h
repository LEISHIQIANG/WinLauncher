#pragma once
#include "../Contracts/IUserInteractionService.h"

struct AppContext;

class UserInteractionService : public IUserInteractionService
{
public:
    explicit UserInteractionService(AppContext* ctx);
    virtual ~UserInteractionService() override = default;

    virtual bool ShowPrompt(HWND parent, const std::wstring& title, const wchar_t* prompt, std::wstring& value, const wchar_t* defaultText = nullptr) override;
    virtual bool ShowMultilinePrompt(HWND parent, const std::wstring& title, const wchar_t* prompt, std::wstring& value, const wchar_t* defaultText = nullptr) override;
    virtual bool ShowPasswordPrompt(HWND parent, const std::wstring& title, const wchar_t* prompt, std::wstring& value) override;
    virtual bool ShowChoosePrompt(HWND parent, const std::wstring& title, const wchar_t* prompt, const std::vector<std::wstring>& items, std::wstring& value) override;
    virtual bool ShowConfirm(HWND parent, const std::wstring& title, const wchar_t* message) override;
    virtual bool ConfirmHighRiskCommand(HWND parent, const std::wstring& commandText, const std::wstring& commandName) override;

private:
    AppContext* m_ctx;
};
