#pragma once

#include <string>

namespace ArchiveUtility
{
    bool CompressDirectoryContents(const std::wstring& sourceDirectory, const std::wstring& destinationZip,
        unsigned long timeoutMs, std::wstring& outError);
    bool ExpandArchive(const std::wstring& sourceZip, const std::wstring& destinationDirectory,
        unsigned long timeoutMs, std::wstring& outError);
}
