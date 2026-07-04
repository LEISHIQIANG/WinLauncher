#pragma once

#include <string>
#include <vector>

struct PluginCommandManifest
{
    std::wstring id;
    std::wstring title;
    std::wstring description;
};

struct PluginSlashCommandManifest
{
    std::wstring id;
    std::wstring command;
    std::wstring title;
    std::wstring description;
    std::wstring usage;
    std::wstring icon;
    std::vector<std::wstring> keywords;
    std::vector<std::wstring> aliases;
};

struct PluginSettingManifest
{
    std::wstring key;
    std::wstring type;
    std::wstring title;
    std::wstring defaultValue;
    int minValue = 0;
    int maxValue = 0;
    bool hasMin = false;
    bool hasMax = false;
};

struct PluginManifest
{
    std::wstring id;
    std::wstring name;
    std::wstring version;
    std::wstring description;
    std::wstring entry;
    std::wstring rootDirectory;
    std::wstring manifestPath;
    std::wstring entryPath;
    std::wstring minHostVersion;
    std::wstring targetHostVersion;
    unsigned int abiVersion = 1;
    std::vector<std::wstring> permissions;
    std::vector<PluginCommandManifest> commands;
    std::vector<PluginSlashCommandManifest> slashCommands;
    std::vector<PluginSettingManifest> settings;
};

class PluginManifestReader
{
public:
    static bool LoadFromFile(const std::wstring& manifestPath, PluginManifest& outManifest, std::wstring& error);

private:
    static bool IsValidPluginId(const std::wstring& id);
    static bool IsSafeRelativeDllPath(const std::wstring& path);
};
