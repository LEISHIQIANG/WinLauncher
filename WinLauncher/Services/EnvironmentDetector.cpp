#define NOMINMAX
#include "EnvironmentDetector.h"
#include <windows.h>
#include <thread>

// Static members
std::vector<EnvironmentDetector::DetectEntry> EnvironmentDetector::s_detectList;
std::mutex         EnvironmentDetector::s_mutex;
std::atomic<bool>  EnvironmentDetector::s_done{false};
std::atomic<bool>  EnvironmentDetector::s_started{false};

EnvironmentDetector::DetectEntry EnvironmentDetector::MakeEntry(const std::wstring& type, const std::wstring& exeName)
{
    return { type, exeName, false };
}

void EnvironmentDetector::StartDetection()
{
    bool expected = false;
    if (!s_started.compare_exchange_strong(expected, true))
        return; // already started

    // Build detect list
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        s_detectList.clear();
        s_detectList.push_back(MakeEntry(L"python",   L"python.exe"));
        s_detectList.push_back(MakeEntry(L"gitbash",  L"bash.exe"));
        // Extend here to add more executors in the future
    }

    // Run on a detached background thread — fire and forget
    std::thread(RunDetection).detach();
}

bool EnvironmentDetector::IsAvailable(const std::wstring& type)
{
    // Built-in Windows shells are always available
    if (type == L"cmd" || type == L"powershell")
        return true;

    std::lock_guard<std::mutex> lock(s_mutex);
    for (const auto& entry : s_detectList)
    {
        if (entry.type == type)
            return entry.available;
    }

    // Unknown type: not available
    return false;
}

bool EnvironmentDetector::IsDetectionComplete()
{
    return s_done.load();
}

// Helper: find an exe by SearchPathW in the system PATH
static bool FindExeInPath(const wchar_t* exeName, std::wstring& outPath)
{
    wchar_t foundPath[MAX_PATH]{};
    DWORD len = SearchPathW(nullptr, exeName, nullptr, MAX_PATH, foundPath, nullptr);
    if (len > 0 && len < MAX_PATH)
    {
        outPath = foundPath;
        return true;
    }
    return false;
}

// Helper: check if a file exists at the given path
static bool FileExists(const wchar_t* path)
{
    DWORD attr = GetFileAttributesW(path);
    return (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY));
}

// Helper: from a git.exe path like "E:\Git\cmd\git.exe", derive bash.exe paths
static bool FindBashFromGit(const std::wstring& gitPath)
{
    // gitPath example: E:\Git\cmd\git.exe  =>  root: E:\Git
    std::wstring root;
    size_t cmdPos = gitPath.rfind(L"\\cmd\\");
    if (cmdPos != std::wstring::npos)
        root = gitPath.substr(0, cmdPos);
    else
    {
        // fallback: go up two levels
        size_t lastSep = gitPath.rfind(L'\\');
        if (lastSep != std::wstring::npos)
            lastSep = gitPath.rfind(L'\\', lastSep - 1);
        if (lastSep != std::wstring::npos)
            root = gitPath.substr(0, lastSep);
    }
    if (root.empty())
        return false;

    return FileExists((root + L"\\bin\\bash.exe").c_str())
        || FileExists((root + L"\\usr\\bin\\bash.exe").c_str());
}

// Helper: check common install locations for an exe
static bool FindExeInCommonDirs(const wchar_t* relativePath)
{
    // relativePath e.g. L"Git\\usr\\bin\\bash.exe"
    static const wchar_t* s_roots[] = {
        nullptr,                    // %ProgramFiles%
        nullptr,                    // %ProgramFiles(x86)%
        nullptr,                    // %LocalAppData%\Programs
    };

    static wchar_t pf[MAX_PATH]{}, pf86[MAX_PATH]{}, lad[MAX_PATH]{};
    static bool inited = false;
    if (!inited)
    {
        GetEnvironmentVariableW(L"ProgramFiles", pf, MAX_PATH);
        GetEnvironmentVariableW(L"ProgramFiles(x86)", pf86, MAX_PATH);
        ExpandEnvironmentStringsW(L"%LocalAppData%\\Programs", lad, MAX_PATH);
        s_roots[0] = pf;
        s_roots[1] = pf86;
        s_roots[2] = lad;
        inited = true;
    }

    for (const auto* root : s_roots)
    {
        if (!root || !root[0]) continue;
        std::wstring full = std::wstring(root) + L"\\" + relativePath;
        if (FileExists(full.c_str()))
            return true;
    }
    return false;
}

void EnvironmentDetector::RunDetection()
{
    // Copy the list so we can work without holding the lock for the entire duration
    std::vector<DetectEntry> localList;
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        localList = s_detectList;
    }

    for (auto& entry : localList)
    {
        if (entry.type == L"gitbash")
        {
            // Strategy: find git.exe first (it IS in PATH), then derive bash location.
            // Also check common install dirs as fallback.
            std::wstring gitPath;
            bool found = false;

            if (FindExeInPath(L"git.exe", gitPath))
                found = FindBashFromGit(gitPath);

            if (!found)
                found = FindExeInCommonDirs(L"Git\\usr\\bin\\bash.exe")
                     || FindExeInCommonDirs(L"Git\\bin\\bash.exe");

            if (!found)
                found = FindExeInPath(L"bash.exe", gitPath); // last resort

            entry.available = found;
        }
        else
        {
            // Generic: SearchPathW
            std::wstring dummy;
            entry.available = FindExeInPath(entry.exeName.c_str(), dummy);
        }
    }

    // Write results back
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        s_detectList = std::move(localList);
    }

    s_done.store(true);
}
