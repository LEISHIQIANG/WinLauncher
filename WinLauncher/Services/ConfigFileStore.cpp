#include "ConfigFileStore.h"
#include <Windows.h>
#include <fstream>

std::wstring ConfigFileStore::ReadUtf8(const std::wstring& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) return L"";
    const std::string bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (bytes.empty()) return L"";
    const int length = MultiByteToWideChar(CP_UTF8, 0, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
    if (length <= 0) return L"";
    std::wstring result(length, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, bytes.data(), static_cast<int>(bytes.size()), result.data(), length);
    return result;
}

bool ConfigFileStore::AtomicWriteUtf8(const std::wstring& path, const std::wstring& content, bool& changed)
{
    changed = ReadUtf8(path) != content;
    if (!changed) return true;
    const int length = WideCharToMultiByte(CP_UTF8, 0, content.data(), static_cast<int>(content.size()), nullptr, 0, nullptr, nullptr);
    if (length < 0) return false;
    std::string bytes(length, '\0');
    if (length > 0) WideCharToMultiByte(CP_UTF8, 0, content.data(), static_cast<int>(content.size()), bytes.data(), length, nullptr, nullptr);
    const std::wstring temporary = path + L".tmp";
    DeleteFileW(temporary.c_str());
    std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
    if (!file) return false;
    file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    file.close();
    if (file.fail()) { DeleteFileW(temporary.c_str()); return false; }
    if (MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return true;
    DeleteFileW(temporary.c_str());
    return false;
}

bool ConfigFileStore::IsPathUnderDirectory(const std::wstring& directory, const std::wstring& path)
{
    const DWORD directoryLength = GetFullPathNameW(directory.c_str(), 0, nullptr, nullptr);
    const DWORD pathLength = GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);
    if (directoryLength == 0 || pathLength == 0) return false;
    std::wstring fullDirectory(directoryLength, L'\0'), fullPath(pathLength, L'\0');
    fullDirectory.resize(GetFullPathNameW(directory.c_str(), directoryLength, fullDirectory.data(), nullptr));
    fullPath.resize(GetFullPathNameW(path.c_str(), pathLength, fullPath.data(), nullptr));
    while (!fullDirectory.empty() && (fullDirectory.back() == L'\\' || fullDirectory.back() == L'/')) fullDirectory.pop_back();
    fullDirectory += L"\\";
    if (fullPath.size() < fullDirectory.size()) return false;
    return CompareStringOrdinal(fullPath.c_str(), static_cast<int>(fullDirectory.size()), fullDirectory.c_str(), static_cast<int>(fullDirectory.size()), TRUE) == CSTR_EQUAL;
}
