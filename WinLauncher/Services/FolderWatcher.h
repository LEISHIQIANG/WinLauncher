#pragma once
#include <Windows.h>
#include <vector>
#include <string>
#include <memory>

class FolderWatcher
{
public:
    FolderWatcher();
    ~FolderWatcher();

    void UpdateFolders(const std::vector<std::wstring>& folders, HWND hWndNotify, UINT msgNotify);
    void Start();
    void Stop();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
