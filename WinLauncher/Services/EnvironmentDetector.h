#pragma once
#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <memory>

class BackgroundTaskService;

/// <summary>
/// Standalone environment detection module.
/// Detects whether optional command executors (Python, Git Bash, etc.) are runnable.
/// Runs once through BackgroundTaskService at app startup. Command editors query the
/// completed snapshot rather than repeating PATH and filesystem scans on the UI thread.
///
/// Usage:
///   EnvironmentDetector::StartDetection();        // once at startup
///   bool ok = EnvironmentDetector::IsAvailable(L"python");  // thread-safe
///   bool done = EnvironmentDetector::IsDetectionComplete();
///
/// Extensible: add new executors by adding entries to s_detectList.
/// </summary>
class EnvironmentDetector
{
public:
    struct PythonInterpreter
    {
        // Major.minor, for example L"3.12".
        std::wstring version;
        // Direct executable path. Never the Windows Store app-execution alias.
        std::wstring executablePath;
    };

    // Start background detection. Safe to call multiple times (no-op after first).
    static void StartDetection(const std::shared_ptr<BackgroundTaskService>& tasks);

    // Returns true when the completed snapshot contains a runnable command type.
    // cmd and powershell are built-in Windows options; optional types return false
    // until the detection task completes.
    static bool IsAvailable(const std::wstring& type);

    // Returns true once the background detection thread has finished.
    static bool IsDetectionComplete();

    // Returns the Python installations found during the startup probe, ordered from
    // newest to oldest. The returned snapshot is safe to use from UI and worker threads.
    static std::vector<PythonInterpreter> GetPythonInterpreters();

    // Resolves a persisted Python command type. "python" retains legacy default-Python
    // behavior; "python:<major.minor>" resolves only that discovered version.
    static bool TryGetPythonInterpreter(const std::wstring& commandType, PythonInterpreter& interpreter);

    static bool IsVersionedPythonCommandType(const std::wstring& commandType);
    static std::wstring GetPythonDisplayName(const std::wstring& commandType);

private:
    struct DetectEntry
    {
        std::wstring type;       // e.g. L"python"
        std::wstring exeName;    // e.g. L"python.exe"
        bool         available = false;
    };

    static DetectEntry MakeEntry(const std::wstring& type, const std::wstring& exeName);

    static void RunDetection();

    static std::vector<DetectEntry> s_detectList;
    static std::vector<PythonInterpreter> s_pythonInterpreters;
    static PythonInterpreter             s_defaultPython;
    static std::mutex               s_mutex;
    static std::atomic<bool>        s_done;
    static std::atomic<bool>        s_started;
};
