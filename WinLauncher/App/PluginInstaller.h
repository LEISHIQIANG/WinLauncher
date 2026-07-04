#pragma once

#include "PluginManifest.h"
#include <string>
#include <vector>

class PluginInstaller
{
public:
    static bool InspectPackage(const std::wstring& packagePath, PluginManifest& manifest, std::wstring& error);
    static bool InstallPackage(const std::wstring& packagePath, std::wstring& installedPluginId, std::wstring& error);
    static bool UninstallPlugin(const PluginManifest& manifest, std::wstring& error);

private:
    struct ZipEntry
    {
        std::wstring name;
        unsigned long long uncompressedSize = 0;
        bool directory = false;
    };

    static bool ValidatePackage(const std::wstring& packagePath, std::vector<ZipEntry>& entries, std::wstring& error);
    static bool ExtractPackage(const std::wstring& packagePath, const std::wstring& destinationDir, std::wstring& error);
    static bool IsSafePackagePath(const std::wstring& path);
    static bool DirectoryExists(const std::wstring& path);
    static bool FileExists(const std::wstring& path);
    static bool MoveDirectory(const std::wstring& from, const std::wstring& to, std::wstring& error);
    static bool RemoveDirectoryTree(const std::wstring& path, std::wstring& error);
    static std::wstring UniqueCachePath(const std::wstring& prefix);
};
