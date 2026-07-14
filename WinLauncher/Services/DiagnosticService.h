#pragma once
#include <string>
class Logger;
class DiagnosticService
{
public:
    explicit DiagnosticService(Logger* logger);
    bool CreatePackage(const std::wstring& destPath, std::wstring& outError) const;
    std::wstring GetDirectory() const;
private:
    Logger* m_logger;
};
