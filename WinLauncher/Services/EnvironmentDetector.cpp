#define NOMINMAX
#include "EnvironmentDetector.h"
#include <windows.h>
#include <algorithm>
#include <cctype>
#include <cwctype>
#include <set>
#include <vector>
#include "../App/BackgroundTaskService.h"

// Static members
std::vector<EnvironmentDetector::DetectEntry> EnvironmentDetector::s_detectList;
std::vector<EnvironmentDetector::PythonInterpreter> EnvironmentDetector::s_pythonInterpreters;
EnvironmentDetector::PythonInterpreter EnvironmentDetector::s_defaultPython;
std::mutex         EnvironmentDetector::s_mutex;
std::atomic<bool>  EnvironmentDetector::s_done{false};
std::atomic<bool>  EnvironmentDetector::s_started{false};

EnvironmentDetector::DetectEntry EnvironmentDetector::MakeEntry(const std::wstring& type, const std::wstring& exeName)
{
    return { type, exeName, false };
}

void EnvironmentDetector::StartDetection(const std::shared_ptr<BackgroundTaskService>& tasks)
{
    bool expected = false;
    if (!s_started.compare_exchange_strong(expected, true))
        return; // already started

    {
        std::lock_guard<std::mutex> lock(s_mutex);
        s_detectList.clear();
        s_detectList.push_back(MakeEntry(L"python", L"python.exe"));
        s_detectList.push_back(MakeEntry(L"gitbash", L"bash.exe"));
        s_pythonInterpreters.clear();
        s_defaultPython = {};
    }

    auto handle = tasks ? tasks->Submit(L"environment.detect", BackgroundTaskService::Priority::Normal,
        [](const std::shared_ptr<BackgroundTaskService::CancellationToken>& cancellation) {
            if (!cancellation->IsCancellationRequested()) RunDetection();
        }) : BackgroundTaskService::TaskHandle{};
    if (!handle)
        s_done = true;
}

bool EnvironmentDetector::IsAvailable(const std::wstring& type)
{
    if (type == L"cmd" || type == L"powershell")
        return true;

    std::lock_guard<std::mutex> lock(s_mutex);
    for (const auto& entry : s_detectList)
    {
        if (entry.type == type)
            return entry.available;
    }
    return false;
}

bool EnvironmentDetector::IsDetectionComplete()
{
    return s_done.load();
}

std::vector<EnvironmentDetector::PythonInterpreter> EnvironmentDetector::GetPythonInterpreters()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    return s_pythonInterpreters;
}

bool EnvironmentDetector::IsVersionedPythonCommandType(const std::wstring& commandType)
{
    static const std::wstring prefix = L"python:";
    if (commandType.rfind(prefix, 0) != 0)
        return false;

    const std::wstring version = commandType.substr(prefix.size());
    const size_t dot = version.find(L'.');
    if (dot == std::wstring::npos || dot == 0 || dot + 1 >= version.size() || version.find(L'.', dot + 1) != std::wstring::npos)
        return false;

    return std::all_of(version.begin(), version.begin() + dot, [](wchar_t ch) { return iswdigit(ch) != 0; }) &&
           std::all_of(version.begin() + dot + 1, version.end(), [](wchar_t ch) { return iswdigit(ch) != 0; });
}

std::wstring EnvironmentDetector::GetPythonDisplayName(const std::wstring& commandType)
{
    if (commandType == L"python")
        return L"Python（默认）";
    if (IsVersionedPythonCommandType(commandType))
        return L"Python" + commandType.substr(std::wstring(L"python:").size());
    return commandType;
}

bool EnvironmentDetector::TryGetPythonInterpreter(const std::wstring& commandType, PythonInterpreter& interpreter)
{
    std::lock_guard<std::mutex> lock(s_mutex);
    if (commandType == L"python")
    {
        if (s_defaultPython.executablePath.empty())
            return false;
        interpreter = s_defaultPython;
        return true;
    }

    if (!IsVersionedPythonCommandType(commandType))
        return false;

    const std::wstring version = commandType.substr(std::wstring(L"python:").size());
    for (const auto& candidate : s_pythonInterpreters)
    {
        if (candidate.version == version)
        {
            interpreter = candidate;
            return true;
        }
    }
    return false;
}

static std::wstring ToLower(std::wstring value)
{
    std::transform(value.begin(), value.end(), value.begin(), towlower);
    return value;
}

static bool IsUsableExecutablePath(const std::wstring& path)
{
    if (path.empty())
        return false;

    DWORD attrs = GetFileAttributesW(path.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_DIRECTORY))
        return false;

    return ToLower(path).find(L"\\microsoft\\windowsapps\\") == std::wstring::npos;
}

static bool FindExeInPath(const wchar_t* exeName, std::wstring& outPath)
{
    wchar_t foundPath[MAX_PATH]{};
    DWORD len = SearchPathW(nullptr, exeName, nullptr, MAX_PATH, foundPath, nullptr);
    if (len > 0 && len < MAX_PATH)
    {
        outPath = foundPath;
        return IsUsableExecutablePath(outPath);
    }
    return false;
}

static bool FileExists(const wchar_t* path)
{
    return IsUsableExecutablePath(path);
}

static std::wstring GetCanonicalPath(const std::wstring& path)
{
    wchar_t fullPath[MAX_PATH]{};
    DWORD len = GetFullPathNameW(path.c_str(), MAX_PATH, fullPath, nullptr);
    return (len > 0 && len < MAX_PATH) ? std::wstring(fullPath) : path;
}

static void AddPythonCandidate(std::vector<std::wstring>& candidates, std::set<std::wstring>& seen, const std::wstring& path)
{
    if (!IsUsableExecutablePath(path))
        return;

    const std::wstring canonical = GetCanonicalPath(path);
    if (seen.insert(ToLower(canonical)).second)
        candidates.push_back(canonical);
}

static std::wstring DecodeOutput(const std::string& bytes)
{
    if (bytes.empty())
        return L"";

    int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
    UINT codePage = CP_UTF8;
    if (length <= 0)
    {
        codePage = CP_OEMCP;
        length = MultiByteToWideChar(codePage, 0, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
    }
    if (length <= 0)
        return L"";

    std::wstring text(length, L'\0');
    MultiByteToWideChar(codePage, codePage == CP_UTF8 ? MB_ERR_INVALID_CHARS : 0, bytes.data(), static_cast<int>(bytes.size()), text.data(), length);
    return text;
}

static bool RunProcessAndCapture(const std::wstring& executable, const std::wstring& arguments, std::wstring& output)
{
    SECURITY_ATTRIBUTES security{ sizeof(security), nullptr, TRUE };
    HANDLE readPipe = nullptr;
    HANDLE writePipe = nullptr;
    if (!CreatePipe(&readPipe, &writePipe, &security, 0))
        return false;
    SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW startup{ sizeof(startup) };
    startup.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
    startup.wShowWindow = SW_HIDE;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = writePipe;
    startup.hStdError = writePipe;

    PROCESS_INFORMATION process{};
    std::wstring commandLine = L"\"" + executable + L"\"";
    if (!arguments.empty())
        commandLine += L" " + arguments;
    std::vector<wchar_t> mutableCommand(commandLine.begin(), commandLine.end());
    mutableCommand.push_back(L'\0');

    const bool created = CreateProcessW(nullptr, mutableCommand.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
        nullptr, nullptr, &startup, &process) != FALSE;
    CloseHandle(writePipe);
    if (!created)
    {
        CloseHandle(readPipe);
        return false;
    }

    std::string bytes;
    char buffer[512];
    const ULONGLONG startedAt = GetTickCount64();
    bool timedOut = false;
    for (;;)
    {
        DWORD available = 0;
        if (PeekNamedPipe(readPipe, nullptr, 0, nullptr, &available, nullptr) && available > 0)
        {
            DWORD read = 0;
            const DWORD requested = std::min<DWORD>(available, static_cast<DWORD>(sizeof(buffer)));
            if (ReadFile(readPipe, buffer, requested, &read, nullptr) && read > 0)
                bytes.append(buffer, read);
            continue;
        }

        if (WaitForSingleObject(process.hProcess, 0) == WAIT_OBJECT_0)
            break;
        if (GetTickCount64() - startedAt >= 5000)
        {
            timedOut = true;
            TerminateProcess(process.hProcess, ERROR_TIMEOUT);
            WaitForSingleObject(process.hProcess, 1000);
            break;
        }
        Sleep(10);
    }

    DWORD read = 0;
    while (ReadFile(readPipe, buffer, sizeof(buffer), &read, nullptr) && read > 0)
        bytes.append(buffer, read);
    CloseHandle(readPipe);

    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD exitCode = 1;
    GetExitCodeProcess(process.hProcess, &exitCode);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    output = DecodeOutput(bytes);
    return !timedOut && exitCode == 0;
}

static std::wstring ExtractMajorMinorVersion(const std::wstring& output)
{
    for (size_t i = 0; i < output.size(); ++i)
    {
        if (!iswdigit(output[i]))
            continue;
        const size_t majorStart = i;
        while (i < output.size() && iswdigit(output[i])) ++i;
        if (i >= output.size() || output[i] != L'.')
            continue;
        const size_t minorStart = ++i;
        while (i < output.size() && iswdigit(output[i])) ++i;
        if (minorStart != i)
            return output.substr(majorStart, i - majorStart);
    }
    return L"";
}

static std::wstring DetectPythonVersion(const std::wstring& executable)
{
    std::wstring output;
    const std::wstring args = L"-c \"import sys; print(str(sys.version_info.major)+'.'+str(sys.version_info.minor))\"";
    return RunProcessAndCapture(executable, args, output) ? ExtractMajorMinorVersion(output) : L"";
}

static void CollectPythonLauncherCandidates(std::vector<std::wstring>& candidates, std::set<std::wstring>& seen, std::wstring& launcherDefaultPath)
{
    std::wstring launcherPath;
    if (!FindExeInPath(L"py.exe", launcherPath))
        return;

    std::wstring output;
    if (!RunProcessAndCapture(launcherPath, L"-0p", output))
        return;

    size_t lineStart = 0;
    while (lineStart < output.size())
    {
        const size_t lineEnd = output.find_first_of(L"\r\n", lineStart);
        const std::wstring line = output.substr(lineStart, lineEnd == std::wstring::npos ? std::wstring::npos : lineEnd - lineStart);
        const std::wstring lower = ToLower(line);
        const size_t exeEnd = lower.find(L".exe");
        if (exeEnd != std::wstring::npos)
        {
            for (size_t i = 0; i + 2 < line.size(); ++i)
            {
                if (iswalpha(line[i]) && line[i + 1] == L':' && (line[i + 2] == L'\\' || line[i + 2] == L'/'))
                {
                    const std::wstring candidatePath = line.substr(i, exeEnd + 4 - i);
                    AddPythonCandidate(candidates, seen, candidatePath);
                    if (line.find(L'*') != std::wstring::npos && IsUsableExecutablePath(candidatePath))
                        launcherDefaultPath = GetCanonicalPath(candidatePath);
                    break;
                }
            }
        }
        if (lineEnd == std::wstring::npos)
            break;
        lineStart = lineEnd + 1;
        if (lineStart < output.size() && output[lineStart] == L'\n') ++lineStart;
    }
}

static void CollectRegistryPythonCandidates(HKEY root, REGSAM view, std::vector<std::wstring>& candidates, std::set<std::wstring>& seen)
{
    HKEY pythonCore = nullptr;
    if (RegOpenKeyExW(root, L"SOFTWARE\\Python\\PythonCore", 0, KEY_READ | view, &pythonCore) != ERROR_SUCCESS)
        return;

    for (DWORD index = 0;; ++index)
    {
        wchar_t versionName[256]{};
        DWORD versionNameLength = static_cast<DWORD>(sizeof(versionName) / sizeof(versionName[0]));
        if (RegEnumKeyExW(pythonCore, index, versionName, &versionNameLength, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS)
            break;

        HKEY installPathKey = nullptr;
        const std::wstring keyPath = std::wstring(versionName, versionNameLength) + L"\\InstallPath";
        if (RegOpenKeyExW(pythonCore, keyPath.c_str(), 0, KEY_READ | view, &installPathKey) != ERROR_SUCCESS)
            continue;

        wchar_t installPath[MAX_PATH]{};
        DWORD bytes = sizeof(installPath);
        if (RegQueryValueExW(installPathKey, nullptr, nullptr, nullptr, reinterpret_cast<LPBYTE>(installPath), &bytes) == ERROR_SUCCESS)
        {
            std::wstring executable = installPath;
            if (ToLower(executable).size() < 10 || ToLower(executable).rfind(L"python.exe") != executable.size() - 10)
            {
                if (!executable.empty() && executable.back() != L'\\') executable += L"\\";
                executable += L"python.exe";
            }
            AddPythonCandidate(candidates, seen, executable);
        }
        RegCloseKey(installPathKey);
    }
    RegCloseKey(pythonCore);
}

static void CollectPathPythonCandidates(std::vector<std::wstring>& candidates, std::set<std::wstring>& seen)
{
    const DWORD chars = GetEnvironmentVariableW(L"PATH", nullptr, 0);
    if (chars == 0)
        return;
    std::wstring path(chars, L'\0');
    GetEnvironmentVariableW(L"PATH", path.data(), chars);
    if (!path.empty() && path.back() == L'\0') path.pop_back();

    size_t begin = 0;
    while (begin <= path.size())
    {
        const size_t end = path.find(L';', begin);
        std::wstring directory = path.substr(begin, end == std::wstring::npos ? std::wstring::npos : end - begin);
        if (directory.size() >= 2 && directory.front() == L'"' && directory.back() == L'"')
            directory = directory.substr(1, directory.size() - 2);
        if (!directory.empty())
            AddPythonCandidate(candidates, seen, directory + L"\\python.exe");
        if (end == std::wstring::npos)
            break;
        begin = end + 1;
    }
}

static bool FindBashFromGit(const std::wstring& gitPath)
{
    std::wstring root;
    size_t cmdPos = gitPath.rfind(L"\\cmd\\");
    if (cmdPos != std::wstring::npos)
        root = gitPath.substr(0, cmdPos);
    else
    {
        size_t lastSep = gitPath.rfind(L'\\');
        if (lastSep != std::wstring::npos)
            lastSep = gitPath.rfind(L'\\', lastSep - 1);
        if (lastSep != std::wstring::npos)
            root = gitPath.substr(0, lastSep);
    }
    return !root.empty() && (FileExists((root + L"\\bin\\bash.exe").c_str()) || FileExists((root + L"\\usr\\bin\\bash.exe").c_str()));
}

static bool FindExeInCommonDirs(const wchar_t* relativePath)
{
    static const wchar_t* roots[] = { nullptr, nullptr, nullptr };
    static wchar_t pf[MAX_PATH]{}, pf86[MAX_PATH]{}, localPrograms[MAX_PATH]{};
    static bool initialized = false;
    if (!initialized)
    {
        GetEnvironmentVariableW(L"ProgramFiles", pf, MAX_PATH);
        GetEnvironmentVariableW(L"ProgramFiles(x86)", pf86, MAX_PATH);
        ExpandEnvironmentStringsW(L"%LocalAppData%\\Programs", localPrograms, MAX_PATH);
        roots[0] = pf;
        roots[1] = pf86;
        roots[2] = localPrograms;
        initialized = true;
    }
    for (const auto* root : roots)
    {
        if (root && root[0] && FileExists((std::wstring(root) + L"\\" + relativePath).c_str()))
            return true;
    }
    return false;
}

static bool IsNewerPythonVersion(const EnvironmentDetector::PythonInterpreter& left, const EnvironmentDetector::PythonInterpreter& right)
{
    const size_t leftDot = left.version.find(L'.');
    const size_t rightDot = right.version.find(L'.');
    const int leftMajor = std::stoi(left.version.substr(0, leftDot));
    const int leftMinor = std::stoi(left.version.substr(leftDot + 1));
    const int rightMajor = std::stoi(right.version.substr(0, rightDot));
    const int rightMinor = std::stoi(right.version.substr(rightDot + 1));
    return leftMajor != rightMajor ? leftMajor > rightMajor : leftMinor > rightMinor;
}

void EnvironmentDetector::RunDetection()
{
    std::vector<DetectEntry> localList;
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        localList = s_detectList;
    }

    std::vector<PythonInterpreter> pythonInterpreters;
    PythonInterpreter defaultPython;
    std::vector<std::wstring> pythonCandidates;
    std::set<std::wstring> seenCandidates;
    std::wstring legacyDefaultPath;
    if (FindExeInPath(L"python.exe", legacyDefaultPath))
        AddPythonCandidate(pythonCandidates, seenCandidates, legacyDefaultPath);
    const std::wstring canonicalDefault = legacyDefaultPath.empty() ? L"" : ToLower(GetCanonicalPath(legacyDefaultPath));
    std::wstring launcherDefaultPath;
    CollectPythonLauncherCandidates(pythonCandidates, seenCandidates, launcherDefaultPath);
    const std::wstring canonicalLauncherDefault = launcherDefaultPath.empty() ? L"" : ToLower(launcherDefaultPath);
    CollectRegistryPythonCandidates(HKEY_CURRENT_USER, 0, pythonCandidates, seenCandidates);
    CollectRegistryPythonCandidates(HKEY_LOCAL_MACHINE, KEY_WOW64_64KEY, pythonCandidates, seenCandidates);
    CollectRegistryPythonCandidates(HKEY_LOCAL_MACHINE, KEY_WOW64_32KEY, pythonCandidates, seenCandidates);
    CollectPathPythonCandidates(pythonCandidates, seenCandidates);

    std::set<std::wstring> seenVersions;
    for (const auto& candidatePath : pythonCandidates)
    {
        const std::wstring version = DetectPythonVersion(candidatePath);
        if (version.empty())
            continue;
        PythonInterpreter candidate{ version, candidatePath };
        const std::wstring canonicalCandidate = ToLower(candidatePath);
        if (canonicalCandidate == canonicalDefault || (canonicalDefault.empty() && canonicalCandidate == canonicalLauncherDefault))
            defaultPython = candidate;
        if (seenVersions.insert(version).second)
            pythonInterpreters.push_back(candidate);
    }
    std::sort(pythonInterpreters.begin(), pythonInterpreters.end(), IsNewerPythonVersion);
    if (defaultPython.executablePath.empty() && !pythonInterpreters.empty())
        defaultPython = pythonInterpreters.front();

    for (auto& entry : localList)
    {
        if (entry.type == L"gitbash")
        {
            std::wstring gitPath;
            bool found = FindExeInPath(L"git.exe", gitPath) && FindBashFromGit(gitPath);
            if (!found)
                found = FindExeInCommonDirs(L"Git\\usr\\bin\\bash.exe") || FindExeInCommonDirs(L"Git\\bin\\bash.exe");
            if (!found)
                found = FindExeInPath(L"bash.exe", gitPath);
            entry.available = found;
        }
        else if (entry.type == L"python")
        {
            entry.available = !pythonInterpreters.empty();
        }
        else
        {
            std::wstring executablePath;
            entry.available = FindExeInPath(entry.exeName.c_str(), executablePath);
        }
    }

    {
        std::lock_guard<std::mutex> lock(s_mutex);
        s_detectList = std::move(localList);
        s_pythonInterpreters = std::move(pythonInterpreters);
        s_defaultPython = std::move(defaultPython);
    }
    s_done.store(true);
}
