#pragma once
#include <string>
#include <vector>
#include <mutex>
#include <atomic>

/// <summary>
/// Standalone environment detection module.
/// Detects whether command executors (python, git bash, etc.) are available in the system PATH.
/// Runs detection on a background thread at app startup; results are queried via IsAvailable().
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
    // Start background detection. Safe to call multiple times (no-op after first).
    static void StartDetection();

    // Returns true if the given command type is available.
    // "cmd" and "powershell" always return true.
    // Other types require detection to be complete; returns false if still in progress.
    static bool IsAvailable(const std::wstring& type);

    // Returns true once the background detection thread has finished.
    static bool IsDetectionComplete();

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
    static std::mutex               s_mutex;
    static std::atomic<bool>        s_done;
    static std::atomic<bool>        s_started;
};
