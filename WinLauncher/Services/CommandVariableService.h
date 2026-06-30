#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef _WINSOCK_DEPRECATED_NO_WARNINGS
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <Windows.h>
#include <string>
#include <vector>
#include <map>

namespace Services
{
    class CommandVariableService
    {
    public:
        // Scans the command text for unique {{input}} or {{input:prompt}} variables and asks user for input
        static bool ResolveInputs(HWND parent, const std::wstring& commandText, std::map<std::wstring, std::wstring>& outInputs);

        // Resolves all variables in commandText and returns the expanded string
        static std::wstring ResolveVariables(
            const std::wstring& commandText,
            const std::wstring& shellType,
            const std::vector<std::wstring>& selectedFiles,
            const std::map<std::wstring, std::wstring>& inputValues
        );
    };
}
