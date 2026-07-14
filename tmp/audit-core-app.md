# WinLauncher Core Application Lifecycle -- Code Audit

**Date:** 2026-07-14
**Scope:** CORE APPLICATION LIFECYCLE CODE
**Files Audited:** 14 files (main.cpp, Application.h/cpp, AppContext.h, AppMessages.h, EventBus.h, Logger.h/cpp, BackgroundTaskService.h/cpp, UiDispatcher.h/cpp, CrashReporter.h/cpp, CallbackGuard.h)

---


## 1. main.cpp

**File:** `C:\Users\Administrator\Desktop\PLUG\WinLauncher\WinLauncher\main.cpp`

### Findings

| Category | Severity | Description |
|----------|----------|-------------|
| Error Handling | LOW | If the `Application` constructor throws an exception (e.g., from member initializers), the uncaught exception propagates through `WinMain`, which is non-standard but survivable -- Windows will terminate the process. The current constructor body is empty, so this is not a practical concern today. |
| Missing Edge Case | LOW | Unused parameters are unnamed (`HINSTANCE`, `LPSTR`, `int`), which is correct practice. No issues. |

**Verdict:** Clean. Minimal entry point with no substantive problems.

---

## 2. Application.h / Application.cpp

**Files:**
- `C:\Users\Administrator\Desktop\PLUG\WinLauncher\WinLauncher\App\Application.h`
- `C:\Users\Administrator\Desktop\PLUG\WinLauncher\WinLauncher\App\Application.cpp`

### 2.1 Memory Management

| Severity | Issue | Location | Detail |
|----------|-------|----------|--------|
| **HIGH** | **Dangling/use-after-free risk via raw pointer cast** | `Application.cpp:605` | `std::wstring* pId = reinterpret_cast<std::wstring*>(lParam);` -- lParam is cast to a `std::wstring*` in `HandleMessage` for `AppMessages::LaunchShortcutById`. The ownership and lifetime of this string are unknown to the handler. If the sender allocated on the stack (whose frame has returned) or the heap object was freed, this is a **use-after-free**. If heap-allocated and never freed, it is a **memory leak**. The sender-side contract must be audited separately and verified. A safer pattern would be `SendMessage` with `COPYDATASTRUCT` or a `shared_ptr` wrapper with explicit lifetime. |
| MEDIUM | Raw `new` without matching delete could leak | UiDispatcher linkage | The `Envelope*` allocated via `new` in `UiDispatcher::QueueRequest` is passed as `LPARAM` via `PostMessageW`. If the message is never dispatched (e.g., window destroyed before message pump drains it), the `Envelope` is leaked. `UiDispatcher::Shutdown()` attempts to drain pending messages, but if the window HWND is invalid at Shutdown time, the drain loop cannot reach those messages. |
| LOW | `m_uiWatchdogTask` lifetime during early return | `Application.cpp:67-68` | If `HandleHelperCommandLine()` returns true and `Run()` returns 0 early, `m_appCtx` was never created (line 78 never reached). `m_uiWatchdogTask` remains default-constructed (null) and its destructor is benign. No leak, but the asymmetry between early-return paths and the full-run path makes reasoning about cleanup harder. |

### 2.2 Thread Safety

| Severity | Issue | Location | Detail |
|----------|-------|----------|--------|
| MEDIUM | Non-atomic bool accessed across logical threads | `Application.h:46-47` | `m_popupPaused` and `m_mouseHookInstalled` are plain `bool` members, not `std::atomic<bool>`. They are accessed from `HandleMessage` (UI thread via message pump) and from `Shutdown()` (called from destructor, which could be on any thread). Since `Shutdown()` is always called after `MessageLoop()` returns (line 171-173 in `Run()`), these are on the same thread in the normal flow. However, the destructor path (if Run() is never called) runs on whatever thread destroys the Application object. If that is not the UI thread, there is a formal data race, though the practical risk is low because the values are monotonic (only set to true/cleared once). |
| LOW | Atomic heartbeat pattern is correct | `Application.h:48-52`, `Application.cpp:89-110` | The heartbeat watchdog correctly uses `std::atomic<ULONGLONG>` for `lastTick` and `std::atomic_bool` for `stopping`. The background task captures `shared_ptr<UiHeartbeatState>` by value, ensuring the state outlives the task. The timer writes on the UI thread; the watchdog reads from a background thread. This is a correct use of atomics for a single-producer, single-consumer flag. |

### 2.3 Error Handling

| Severity | Issue | Location | Detail |
|----------|-------|----------|--------|
| MEDIUM | `InitializeProcess()` always returns `true` | `Application.cpp:176-213` | Regardless of whether DPI awareness configuration or CoInitializeEx fails, the function unconditionally returns `true`. This masks initialization failures. The caller has no way to distinguish between "everything initialized fine" and "COM failed but we soldiered on." Specific concerns: (a) If `CoInitializeEx` fails, `m_comInitialized` is `false`, which is correctly tracked. (b) However, if DPI awareness completely fails (all three fallback paths), the function still returns true. A warning log would be appropriate. (c) `GetModuleHandleW(L"user32.dll")` can theoretically return NULL; the function handles this gracefully. |
| LOW | `HandleHelperCommandLine` semantics unclear | `Application.cpp:215-218` | The function delegates to `AutoStartHelper::HandleCommandLine()`. The return value semantics are opaque: does `true` mean "command was handled, exit" or "command succeeded, continue"? This should be documented. |

### 2.4 Resource Leaks

| Severity | Issue | Location | Detail |
|----------|-------|----------|--------|
| LOW | `LoadIconW` for tray icon | `Application.cpp:432` | `nid.hIcon = LoadIconW(m_hInstance, MAKEINTRESOURCEW(IDI_APP_ICON))` -- According to MSDN, `LoadIcon` with a `MAKEINTRESOURCE` resource identifier returns a shared icon handle that does **not** need to be freed with `DestroyIcon`. This is correct usage for module resource icons. **No leak.** |
| LOW | `CreateFileW` for GPU crash marker | `Application.cpp:124-129` | The file handle is properly closed via `CloseHandle(hMarker)` before `DeleteFileW`. Correct. |

### 2.5 Architecture

| Severity | Issue | Location | Detail |
|----------|-------|----------|--------|
| **HIGH** | **God class with excessive responsibilities** | `Application.h` (entire class) | The `Application` class manages: window creation, tray icons, mouse hooks, keyboard hooks, heartbeat/watchdog, service initialization, configuration loading, popup display, config window display, tray menu, update lifecycle, restart logic, and the entire message routing/dispatch. This centralizes far too many concerns. Consider factoring into: `WindowManager`, `TrayManager`, `HookManager`, `LifecycleCoordinator`. |
| MEDIUM | Heavy work in message handler | `Application.cpp:603-664` | `LaunchShortcutById` performs full config reload (`LoadConfig()`) and linear shortcut search inside the window message handler. If config is large or I/O is slow, this blocks the message pump, causing UI stutter. The lookup should be cached or pre-indexed. |
| MEDIUM | Inline blocking work on UI thread | `Application.cpp:141` | `EnvironmentDetector::StartDetection()` is called inline on the UI thread. If this function performs any synchronous I/O or computation, it blocks `Run()` and delays window creation. |
| LOW | Plugins window opens config | `Application.cpp:675-677` | `AppMessages::ShowPluginsWindow` delegates to `ShowConfigWindow()`. This is a layering shortcut -- should route to a dedicated plugins view. |

### 2.6 Undefined Behavior

| Severity | Issue | Location | Detail |
|----------|-------|----------|--------|
| MEDIUM | `reinterpret_cast` of lParam to `std::wstring*` | `Application.cpp:605` | If `lParam` is not actually a valid pointer to a `std::wstring` object (strict aliasing violation, wrong type, or lifetime-expired), dereferencing it is **undefined behavior**. This depends entirely on sender correctness. The `if (pId)` check only guards against null, not type correctness. |

### 2.7 Performance

| Severity | Issue | Location | Detail |
|----------|-------|----------|--------|
| MEDIUM | Busy-polling watchdog thread | `Application.cpp:94-110` | The UI watchdog task runs `Sleep(250)` in a tight loop on a dedicated background thread. A single thread is permanently occupied even when the UI is responsive. Consider signaling via a condition variable or timer-based polling with longer intervals. |
| LOW | Unnecessary copy of `RendShortcutInfo` | `Application.cpp:628-640` | The `LaunchShortcutById` handler copies 13 fields from `Model::ShortcutInfo` to `RendShortcutInfo` individually. A structured conversion function or aggregate initialization would be less error-prone and more maintainable. |

### 2.8 Shutdown Sequence Correctness

| Severity | Issue | Location | Detail |
|----------|-------|----------|--------|
| MEDIUM | Shutdown order is fragile | `Application.cpp:329-417` | The 90-line `Shutdown()` method has a highly specific ordering: (1) signal stopping, (2) cancel batch/macro, (3) uninstall keyboard hook, (4) uninstall mouse hook, (5) request plugin shutdown, (6) shutdown UI dispatcher, (7) shutdown update service, (8) shutdown background tasks, (9) destroy main window, (10) destroy child windows, (11) flush/free services, (12) end timer period, (13) uninitialize COM. If any step in this chain hangs or crashes, subsequent cleanup is skipped. The order is reasonable but any refactoring must preserve it. |
| LOW | `Shutdown()` called from destructor and mid-method | `Application.cpp:43, 172` | `Shutdown()` is called from both the destructor (cleanup on exception/early exit) and from the end of `Run()`. The early-return guard `if (!m_appCtx && !m_timerResolutionRaised && !m_comInitialized) return;` protects against double-cleanup, but the triple-negation logic is hard to reason about. |

---

## 3. AppContext.h

**File:** `C:\Users\Administrator\Desktop\PLUG\WinLauncher\WinLauncher\App\AppContext.h`

### Findings

| Category | Severity | Issue | Detail |
|----------|----------|-------|--------|
| Architecture | MEDIUM | Service Locator anti-pattern | `AppContext` is a bag of `shared_ptr` dependencies. While convenient, this pattern makes it difficult to track which components depend on which other components. Dependencies are implicit (any code with an `AppContext*` can access any service). Consider splitting into domain-specific contexts or using constructor injection for key services. |
| Memory Management | LOW | `DiagnosticService` holds raw `Logger*` | `DiagnosticService` constructor takes `logger.get()` -- a raw pointer. If the `DiagnosticService` were to survive the `Logger` (e.g., if the reference count pattern changes), this becomes dangling. Currently safe because `AppContext` destruction order follows member declaration order (reverse), and `diagnostics` is declared after `logger`. |
| Memory Management | INFORMATIONAL | Eager construction of all services | The `AppContext` constructor creates all 13 services immediately. If any service constructor performs I/O or heavy work, it delays app startup. The `ConfigPath::PrepareUserLogDirectory()` call in the `logger` initializer creates directories synchronously. |
| Thread Safety | LOW | `hMainWnd` and `hInstance` are raw HWND/HINSTANCE | These are non-atomic and non-synchronized. They are set once during initialization (before background threads start) and read throughout the application lifetime. This is safe under the current usage pattern but fragile if usage changes. |
| Dependencies | MEDIUM | `PluginManager` has 4 constructor dependencies | `PluginManager` depends on `EventBus`, `Logger`, `UiDispatcher`, `BackgroundTaskService` -- a wide interface. This suggests `PluginManager` may itself be a mini-god-class. |

---

## 4. AppMessages.h

**File:** `C:\Users\Administrator\Desktop\PLUG\WinLauncher\WinLauncher\App\AppMessages.h`

### Findings

**Verdict:** Clean. No issues found. All message IDs are well-defined `constexpr UINT` values starting from `WM_APP + 1`. The namespace scoping is appropriate. The gap between `WM_APP + 0x80` and `WM_APP + 0x90` leaves room for keyboard hook expansion.

---

## 5. EventBus.h

**File:** `C:\Users\Administrator\Desktop\PLUG\WinLauncher\WinLauncher\App\EventBus.h`

### 5.1 Thread Safety

| Severity | Issue | Location | Detail |
|----------|-------|----------|--------|
| MEDIUM | Token refetch is not guaranteed atomic across lock release/reacquire | `EventBus.h:68-82` | `Publish()` collects tokens under lock, releases the lock, then re-acquires the lock to fetch each handler by token. Between the two lock acquisitions, a concurrent `Unsubscribe` could remove the handler. This is handled correctly (the token lookup returns nothing and the iteration continues), but the **same token could be reused** by a new `Subscribe` call between the two critical sections if `m_nextToken` wraps. In practice this requires 2^64 subscriptions, which is impossible, so the risk is null. |
| MEDIUM | Callbacks invoked under no lock | `EventBus.h:80-81` | `CallbackGuard::Invoke` runs handler callbacks outside any mutex. This is intentional to prevent deadlock (handler re-entering Publish or Subscribe). Handlers must be re-entrant safe and must not rely on EventBus state during invocation. This is a design contract that should be documented. |
| LOW | Mutex is mutable and used correctly | `EventBus.h:96` | The `mutable std::mutex m_mutex` correctly allows locking in const methods like `Publish` (if it were const). Currently `Publish` is non-const, so `mutable` is unnecessary but harmless. |

### 5.2 Error Handling

| Severity | Issue | Location | Detail |
|----------|-------|----------|--------|
| LOW | Null logger silently ignored | `EventBus.h:28-30, 81` | If `m_logger` is null (default constructor), `CallbackGuard::Invoke(m_logger.get(), ...)` passes a null `Logger*`. Inside `CallbackGuard::Invoke`, the `LOG_ERROR_NODE` macro checks `if (logger)` before use, so this is safe. The null logger case means handler exceptions will not be logged. |

### 5.3 Architecture

| Severity | Issue | Location | Detail |
|----------|-------|----------|--------|
| LOW | Identity-based unsubscription requires token storage | `EventBus.h:41-57` | Subscribers must store the returned `Token` to unsubscribe later. If a token is lost, the subscription persists for the lifetime of the `EventBus`. No `UnsubscribeAll` per-token is provided. This is a known pattern but can lead to "zombie subscriptions." |

---

## 6. Logger.h / Logger.cpp

**Files:**
- `C:\Users\Administrator\Desktop\PLUG\WinLauncher\WinLauncher\App\Logger.h`
- `C:\Users\Administrator\Desktop\PLUG\WinLauncher\WinLauncher\App\Logger.cpp`

### 6.1 Thread Safety

| Severity | Issue | Location | Detail |
|----------|-------|----------|--------|
| MEDIUM | `s_defaultLogger` is a non-atomic raw pointer | `Logger.h:75, Logger.cpp:10` | The static `s_defaultLogger` pointer is read and written without any synchronization. In the current architecture, it is set once during `AppContext` construction (before background threads start) and only reset during destruction (after threads stop). This is safe *today* but fragile if the usage pattern changes. |
| MEDIUM | `GetInstanceRef()` singleton same concern | `Logger.cpp:173` | `static Logger* instance = nullptr` -- same non-atomic access pattern. Used by the unhandled exception filter to flush logs during a crash. If multiple `Logger` instances are ever created concurrently, this is a data race. |
| LOW | `m_cleanupMutex` vs `m_mutex` dual-lock pattern | `Logger.cpp:227, 290` | `LogV` holds `m_cleanupMutex` while pushing to the queue. `WritePending` drains the queue under `m_cleanupMutex`, then writes to file under `m_mutex`. This two-phase locking is correct but adds complexity: the log file could be rotated between when pending logs are swapped and when they are written. This is handled because rotation re-opens the file under `m_mutex`, and the write also holds `m_mutex`, so they are serialized. |

### 6.2 Error Handling

| Severity | Issue | Location | Detail |
|----------|-------|----------|--------|
| MEDIUM | `_vsnwprintf` failure silently drops the message | `Logger.cpp:183-189` | `_vsnwprintf(nullptr, 0, format, copy)` returns the number of characters that would be written. If it returns `-1` (encoding error or null format string), the message is silently dropped with no diagnostic. Consider logging a fallback "corrupted log message" entry to aid debugging. |
| LOW | File open failure not reported | `Logger.cpp:136` | If `m_file.open(...)` fails, all subsequent log writes silently go to `OutputDebugStringW` only (line 225). The file write at line 292 returns early if `!m_file.is_open()`. Consider logging to `OutputDebugStringW` that the log file could not be opened. |
| LOW | `WideCharToMultiByte` failure in `ToUtf8` | `Logger.cpp:44-49` | If the first `WideCharToMultiByte` call returns 0 (encoding error), an empty `std::string` is returned. The subsequent call is skipped. This silently drops non-representable characters rather than using replacement characters. |
| LOW | `SHCreateDirectoryExW` failure ignored | `Logger.cpp:38-41` | `EnsureDirectory` calls `SHCreateDirectoryExW` and ignores the return value. If directory creation fails, `m_file.open()` will fail silently. |

### 6.3 Performance

| Severity | Issue | Location | Detail |
|----------|-------|----------|--------|
| MEDIUM | Full JSON record construction on every log call | `Logger.cpp:212-224` | Every call to `LogV` builds a complete JSON record with: UTC timestamp, Unicode escaping, ANSI-to-wide conversion of file/function names, session ID conversion via `FromAnsi` (which uses the ACP codepage), and UTF-8 conversion. This is ~10+ heap allocations per log entry. For high-frequency logging (e.g., debug tracing), this could become a measurable bottleneck. |
| LOW | `SanitizeMessage` does O(n) scanning | `Logger.cpp:87-101` | Every log message is scanned for `USERPROFILE` path occurrences. This is quadratic in the worst case (repeated replacements). In practice, log messages are short, so the impact is minimal. |
| LOW | `OutputDebugStringW` on every log call | `Logger.cpp:225` | Debug output is always written even in release builds. This is fine for development but consumes kernel resources. Consider a compile-time flag. |

### 6.4 Resource Management

| Severity | Issue | Location | Detail |
|----------|-------|----------|--------|
| LOW | `m_cleanupThread` properly joined | `Logger.cpp:149` | The clean-up thread is joined in the destructor with the correct signal-and-wait pattern. |
| LOW | Log rotation re-opens without flushing old handle | `Logger.cpp:303-321` | `RotateIfNeededLocked` calls `m_file.close()` then either re-opens on failure or opens new file. The close-before-open pattern is correct. However, if `MoveFileExW` succeeds but `m_file.open()` fails, the log file has been moved and the new file creation failed, resulting in a gap. The application continues with `OutputDebugStringW` only output. |

### 6.5 Dead Code

| Severity | Issue | Location | Detail |
|----------|-------|----------|--------|
| LOW | `UnhandledCrashHandler` is dead code | `Logger.h:65, Logger.cpp:261` | The method `UnhandledCrashHandler` always returns `EXCEPTION_CONTINUE_SEARCH` and is never installed via `SetUnhandledExceptionFilter`. The field `m_prevFilter` is never populated. This appears to be vestigial code that was superseded by `CrashReporter`. |
| LOW | Typo in enum name "ERRA" and "WORNING" | `Logger.h:24-25, 29` | The log level enum uses "ERRA" (should be "ERROR") and "WORNING" (should be "WARNING"). These are deliberate aliases for backward compatibility (aliases `LError` and `LWarn` exist). The typos are propagated throughout the codebase via macros. This is a known pattern but is confusing for new developers. |

### 6.6 String Encoding

| Severity | Issue | Location | Detail |
|----------|-------|----------|--------|
| MEDIUM | `FromAnsi` uses CP_ACP (system ANSI codepage) | `Logger.cpp:55, 60` | The session ID, file names, and function names are converted from "ANSI" using `CP_ACP`, which depends on the system locale. Source file names in MSVC are typically encoded in the system's ANSI codepage, so this usually works. However, if source files use UTF-8 encoding and `/utf-8` compiler flag is set, this conversion will mangle non-ASCII characters in file paths. |
