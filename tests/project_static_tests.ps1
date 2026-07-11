param()

$ErrorActionPreference = "Stop"

$testRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = Split-Path -Parent $testRoot
$results = New-Object System.Collections.Generic.List[object]

function Add-TestResult {
    param(
        [string]$Name,
        [bool]$Passed,
        [string]$Detail
    )

    $results.Add([pscustomobject]@{
        Name = $Name
        Passed = $Passed
        Detail = $Detail
    }) | Out-Null
}

function Read-RepoFile {
    param([string]$RelativePath)
    return Get-Content -LiteralPath (Join-Path $repoRoot $RelativePath) -Raw
}

$updateHeader = Read-RepoFile "WinLauncher\Services\UpdateService.h"
Add-TestResult `
    -Name "Update mock is disabled" `
    -Passed ($updateHeader -match '(?m)^\s*#define\s+MOCK_UPDATE_SERVICE\s+0\s*$') `
    -Detail "MOCK_UPDATE_SERVICE must remain 0 outside explicit test builds"

$updateSource = Read-RepoFile "WinLauncher\Services\UpdateService.cpp"
Add-TestResult `
    -Name "Update download validates read and write completion" `
    -Passed (
        $updateSource -match 'bool\s+readOk\s*=\s*true' -and
        $updateSource -match 'bool\s+writeOk\s*=\s*true' -and
        $updateSource -match 'downloadSuccess\s*=\s*readOk\s*&&\s*writeOk\s*&&\s*\(contentLength\s*==\s*0\s*\|\|\s*totalBytesRead\s*==\s*contentLength\)' -and
        $updateSource -match 'DeleteFileW\(targetPath\.c_str\(\)\)'
    ) `
    -Detail "Partial or failed update downloads must not be treated as installable"

Add-TestResult `
    -Name "Automatic updates require an EXE asset and validate HTTP status" `
    -Passed (
        $updateSource -match 'name\.compare\(name\.size\(\) - 4, 4, L"\.exe"\)' -and
        $updateSource -notmatch 'name\.compare\(name\.size\(\) - 4, 4, L"\.zip"\)' -and
        $updateSource -match 'statusCode >= 200 && statusCode < 300' -and
        $updateSource -match 'cancellation->IsCancellationRequested\(\)' -and
        $updateSource -match 'if \(!downloadSuccess\)\s*DeleteFileW\(targetPath\.c_str\(\)\)'
    ) `
    -Detail "Only complete EXE releases may enter automatic replacement; failed, non-2xx, or cancelled downloads are removed"

Add-TestResult `
    -Name "Update replacement verifies the downloaded executable" `
    -Passed (
        $updateSource -match 'GetFileAttributesExW\(targetPath\.c_str\(\)' -and
        $updateSource -match 'replacement skipped because downloaded EXE is missing or empty'
    ) `
    -Detail "ApplyUpdate must not terminate the running app without a non-empty downloaded executable"

Add-TestResult `
    -Name "Update replacement uses PowerShell-safe literal paths" `
    -Passed (
        $updateSource -match 'EscapePowerShellSingleQuotedString' -and
        $updateSource -match 'Remove-Item\s+-LiteralPath' -and
        $updateSource -match 'Move-Item\s+-LiteralPath' -and
        $updateSource -match "escaped \+= L`"`'`'`""
    ) `
    -Detail "Updater replacement command must escape paths before embedding them in PowerShell"

$resourceText = Read-RepoFile "WinLauncher\resource.rc"
Add-TestResult `
    -Name "Executable resource uses central version macros" `
    -Passed (
        $resourceText -match '#include\s+"version\.h"' -and
        $resourceText -match 'FILEVERSION\s+WINLAUNCHER_VERSION_RC' -and
        $resourceText -match 'PRODUCTVERSION\s+WINLAUNCHER_VERSION_RC' -and
        $resourceText -match 'VALUE\s+"FileVersion",\s+WINLAUNCHER_VERSION_ASTR' -and
        $resourceText -match 'VALUE\s+"ProductVersion",\s+WINLAUNCHER_VERSION_ASTR'
    ) `
    -Detail "Release metadata should follow WinLauncher/version.h"

$popupSource = Read-RepoFile "WinLauncher\PopupWindow.cpp"
Add-TestResult `
    -Name "Command capture opens live panel before work" `
    -Passed (
        $popupSource -match 'CommandPanelWindow::ShowLive' -and
        $popupSource -match 'ExecuteProcessStreaming' -and
        $popupSource -match 'ConfirmHighRiskCommand'
    ) `
    -Detail "Long-running command execution must remain asynchronous and risk-gated"

Add-TestResult `
    -Name "Built-in reload is silent" `
    -Passed (
        $popupSource -match 'item\.pluginCommandId == L"winlauncher\.reload"' -and
        $popupSource -match 'silent reload result' -and
        $popupSource -match 'ToastWindow::Show\(ok \? L"插件已重新加载"' -and
        $popupSource -match 'ExecuteSlashCommand\(\s*L"", item\.pluginCommandId.*nullptr\)'
    ) `
    -Detail "The built-in reload slash command must refresh plugins without opening a command output panel"

$urlEditSource = Read-RepoFile "WinLauncher\Config\UrlEditForm.cpp"
$fileSelectionSource = Read-RepoFile "WinLauncher\Services\FileSelectionService.cpp"
$pluginManagerSource = Read-RepoFile "WinLauncher\App\PluginManager.cpp"
$loggerSource = Read-RepoFile "WinLauncher\App\Logger.cpp"
$crashSource = Read-RepoFile "WinLauncher\App\CrashReporter.cpp"
$inputHookStopHeader = Read-RepoFile "WinLauncher\App\InputHookThreadStop.h"
$keyboardHookSource = Read-RepoFile "WinLauncher\KeyboardHook.cpp"
$mouseHookSource = Read-RepoFile "WinLauncher\MouseHook.cpp"
$macroServiceSource = Read-RepoFile "WinLauncher\Services\MacroService.cpp"
$batchLaunchSource = Read-RepoFile "WinLauncher\Services\BatchLaunchService.cpp"
$applicationSource = Read-RepoFile "WinLauncher\App\Application.cpp"
$popupHeader = Read-RepoFile "WinLauncher\PopupWindow.h"
$commandPanelSource = Read-RepoFile "WinLauncher\Config\CommandPanelWindow.cpp"
$commandPanelHeader = Read-RepoFile "WinLauncher\Config\CommandPanelWindow.h"
$popupWindowHeader = Read-RepoFile "WinLauncher\PopupWindow.h"
$configWindowSource = Read-RepoFile "WinLauncher\Config\ConfigWindow.cpp"
$commandVariableSource = Read-RepoFile "WinLauncher\Services\CommandVariableService.cpp"

Add-TestResult `
    -Name "Detached threads are isolated to bounded shutdown fallback" `
    -Passed (
        @(Get-ChildItem -LiteralPath (Join-Path $repoRoot "WinLauncher") -Recurse -Include *.cpp,*.h |
            Where-Object { $_.Name -ne "BackgroundTaskService.cpp" } |
            Where-Object { (Get-Content -LiteralPath $_.FullName -Raw) -cmatch '\.detach\s*\(' }).Count -eq 0
    ) `
    -Detail "Feature code must submit work through BackgroundTaskService instead of fire-and-forget threads"

Add-TestResult `
    -Name "Command panel cancels obsolete workers before reuse or teardown" `
    -Passed (
        $commandPanelHeader -match 'BackgroundTaskService::TaskHandle\s+m_workerTask' -and
        $commandPanelSource -match 'void\s+CommandPanelWindow::CancelWorker\s*\(' -and
        $commandPanelSource -match 'g_cmdPanelInstance->CancelWorker\(\)' -and
        $commandPanelSource -match 'case\s+WM_DESTROY:\s*\r?\n\s*CancelWorker\(\)' -and
        $commandPanelSource -match 'm_workerTask\.Cancel\(\)'
    ) `
    -Detail "Closing, replacing, or refreshing a panel must invalidate old worker output"

Add-TestResult `
    -Name "Popup icon refresh leaves Shell extraction off the UI message path" `
    -Passed (
        $popupWindowHeader -match 'BackgroundTaskService::TaskHandle\s+m_iconRefreshTask' -and
        $popupSource -match 'Submit\(L"popup\.icon_refresh"' -and
        $popupSource -match 'void\s+PopupWindow::CancelIconRefresh\s*\(' -and
        $popupSource -match 'void\s+PopupWindow::ApplyRefreshedIcons\s*\(' -and
        $popupSource -notmatch 'case\s+WM_USER_REFRESH_ICONS:[\s\S]{0,1800}ShortcutManager::RefreshShortcutIcon'
    ) `
    -Detail "The UI thread must apply completed icon results instead of extracting every Shell icon synchronously"

Add-TestResult `
    -Name "Popup icon work is limited to visible pages and cancelled when hidden" `
    -Passed (
        $popupSource -match 'distance\s*>\s*1\)\s*continue' -and
        $popupSource -match 'void\s+PopupWindow::HideSelf\s*\([\s\S]{0,900}CancelIconRefresh\(\)' -and
        $popupSource -match 'm_iconRefreshPending\s*=\s*false'
    ) `
    -Detail "First paint should defer off-screen bitmaps, and hidden popups must not keep refreshing icons"

Add-TestResult `
    -Name "Popup show reuses clean background caches and scene-safe page indices" `
    -Passed (
        $popupSource -match 'backgroundRefreshNeeded\s*=\s*this->m_bgCaptureDirty' -and
        $popupSource -match 'if\s*\(geometryChanged\)\s*\r?\n\s*SetWindowPos' -and
        $popupSource -match 'm_pageModelIndices\[i\]\s*==\s*modelCurrentPage' -and
        $popupSource -match 'PopupWindow perf: show_state'
    ) `
    -Detail "Repeated popup shows should preserve valid background caches and map filtered scene pages correctly"

Add-TestResult `
    -Name "Update tasks are cancelled before service teardown" `
    -Passed (
        $updateHeader -match 'void\s+Shutdown\(\)' -and
        $updateSource -match 'void\s+UpdateService::Shutdown\s*\(' -and
        $updateSource -match 'm_checkTask\.Cancel\(\)' -and
        $updateSource -match 'm_downloadTask\.Cancel\(\)' -and
        $applicationSource -match 'UpdateService::GetInstance\(\)\.Shutdown\(\)'
    ) `
    -Detail "Application shutdown must cancel update callbacks before background workers outlive UI services"

Add-TestResult `
    -Name "URL editor workers do not capture form instances" `
    -Passed ($urlEditSource -match 'url\.latency' -and $urlEditSource -match 'url\.favicon' -and $urlEditSource -notmatch 'std::thread\s*\(\s*\[this') `
    -Detail "Network workers must return through lifetime-checked UI dispatch"

Add-TestResult `
    -Name "File selection uses cancellable request state" `
    -Passed ($fileSelectionSource -match 'SelectionRequest' -and $fileSelectionSource -match 'Priority::Interactive' -and $fileSelectionSource -cnotmatch '\.detach\s*\(') `
    -Detail "Selection completion must not call PopupWindow from a worker thread"

Add-TestResult `
    -Name "Command variables use the real user config path and bounded WAN lookup" `
    -Passed (
        $commandVariableSource -match '#include "ConfigPath\.h"' -and
        $commandVariableSource -match 'ConfigPath::GetUserConfigDirectory\(\)' -and
        $commandVariableSource -match 'WAN_IP_TIMEOUT_MS = 3000' -and
        $commandVariableSource -match 'INTERNET_OPTION_CONNECT_TIMEOUT' -and
        $commandVariableSource -match 'INTERNET_OPTION_RECEIVE_TIMEOUT' -and
        $commandVariableSource -match 'InetPtonW\(AF_INET'
    ) `
    -Detail "config_dir must match user config storage and wan_ip must fail fast on invalid network responses"

Add-TestResult `
    -Name "Command timeout settings are bounded and reported" `
    -Passed (
        $popupSource -match 'configuredTimeout >= 1 && configuredTimeout <= 3600' -and
        $popupSource -match 'invalid timeout=.*using default 300 seconds' -and
        $popupSource -match '秒超时时间，进程已终止'
    ) `
    -Detail "Invalid command timeout settings must use the 300-second default and clearly label terminated commands"

Add-TestResult `
    -Name "Popup lifecycle cancels pending file selection work" `
    -Passed (
        $popupHeader -match 'void\s+CancelFileSelectionQuery\(\)' -and
        $popupSource -match 'void\s+PopupWindow::CancelFileSelectionQuery\(\)' -and
        $popupSource -match 'm_selectionRequest->Cancel\(\)' -and
        $popupSource -match 'KillTimer\(hWnd, FILE_SELECTION_TIMER_ID\)' -and
        ([regex]::Matches($popupSource, 'CancelFileSelectionQuery\(\);')).Count -ge 5
    ) `
    -Detail "Closing, destroying, replacing, and releasing popups must cancel obsolete Shell selection work"

Add-TestResult `
    -Name "Command panel clears loading state when task submission fails" `
    -Passed (
        ([regex]::Matches($commandPanelSource, 'm_refreshRunning = false;')).Count -ge 2 -and
        ([regex]::Matches($commandPanelSource, 'm_workerGeneration = 0;')).Count -ge 2 -and
        ([regex]::Matches($commandPanelSource, '后台任务繁忙，命令未启动')).Count -ge 2
    ) `
    -Detail "A saturated task queue must not leave a reused or new command panel spinning indefinitely"

Add-TestResult `
    -Name "Config file operations cancel pending delayed saves" `
    -Passed (
        ([regex]::Matches($configWindowSource, 'KillTimer\(hwnd, CONFIG_SAVE_TIMER_ID\)')).Count -ge 3 -and
        $configWindowSource -match 'RestoreConfigBackup\(' -and
        $configWindowSource -match 'ClearConfig\('
    ) `
    -Detail "Restoring or clearing config must not be overwritten by an older debounced save"

Add-TestResult `
    -Name "Plugin UI and shutdown use guarded lifetimes" `
    -Passed ($pluginManagerSource -match 'RequestShutdown' -and $pluginManagerSource -match 'm_activeExecutions' -and $pluginManagerSource -match 'm_uiDispatcher->InvokeSync' -and $pluginManagerSource -match 'm_uiDispatcher->Post' -and $pluginManagerSource -match 'IsCurrentTaskCancellationRequested') `
    -Detail "Plugin tasks must retain manager lifetime and marshal UI work"

Add-TestResult `
    -Name "Crash reporting is independent from normal logger locks" `
    -Passed ($crashSource -match 'MiniDumpWriteDump' -and $crashSource -match 'MiniDumpWithThreadInfo' -and $crashSource -match 'MiniDumpWithUnloadedModules' -and $loggerSource -notmatch 'SetUnhandledExceptionFilter\(') `
    -Detail "Unhandled exceptions must use the dedicated dump thread, not the normal log mutex"

Add-TestResult `
    -Name "Normal logging uses a bounded async queue" `
    -Passed ($loggerSource -match 'MaxPendingLogEntries\s*=\s*4096' -and $loggerSource -match 'm_pendingLogs' -and $loggerSource -match 'milliseconds\(100\)') `
    -Detail "UI callers must not flush the log file on every entry"

Add-TestResult `
    -Name "Input hook threads use one bounded shutdown primitive" `
    -Passed (
        $inputHookStopHeader -match 'PostThreadMessageW\(threadId, WM_QUIT' -and
        $inputHookStopHeader -match 'GetTickCount64\(\) \+ timeoutMs' -and
        $inputHookStopHeader -match 'WaitForSingleObject\(thread, remainingMs\)' -and
        $inputHookStopHeader -match 'cleanupBeforeForce\(\)' -and
        $inputHookStopHeader -match 'TerminateThread\(thread, 0\)' -and
        $keyboardHookSource -match 'InputHookThreadStop::RequestStopAndClose' -and
        $mouseHookSource -match 'InputHookThreadStop::RequestStopAndClose' -and
        $macroServiceSource -match 'InputHookThreadStop::RequestStopAndClose' -and
        $keyboardHookSource -notmatch 'TerminateThread\(' -and
        $mouseHookSource -notmatch 'TerminateThread\(' -and
        $macroServiceSource -notmatch 'TerminateThread\('
    ) `
    -Detail "Keyboard, mouse, and macro hook threads must quit cooperatively before the explicit timeout fallback"

Add-TestResult `
    -Name "Macro hook shutdown clears stale UI and input state" `
    -Passed (
        $macroServiceSource -match 's_hNotifyWnd\.store\(nullptr\)' -and
        $macroServiceSource -match 's_ignoreMouseUntilReleased\.store\(false\)'
    ) `
    -Detail "Failed or stopped recording must not retain a closed notify window or stale mouse state"

Add-TestResult `
    -Name "Batch launch cancels before UI teardown and bounds UI calls" `
    -Passed (
        $batchLaunchSource -match 's_cancelRequested' -and
        $batchLaunchSource -match 'SendMessageTimeoutW' -and
        $batchLaunchSource -match 'SMTO_ABORTIFHUNG \| SMTO_BLOCK' -and
        $batchLaunchSource -match 'WaitForSingleObject\(thread, 2500\)' -and
        $batchLaunchSource -notmatch 'LRESULT res = SendMessageW' -and
        $applicationSource -match 'BatchLaunchService::Cancel\(\);'
    ) `
    -Detail "Batch work must stop before UI destruction and never wait indefinitely for the main window"

Add-TestResult `
    -Name "Macro playback retains a live worker handle after cancellation timeout" `
    -Passed (
        $macroServiceSource -match 'const DWORD wait = WaitForSingleObject\(thread, 2000\)' -and
        $macroServiceSource -match 'if \(wait == WAIT_OBJECT_0\)' -and
        $macroServiceSource -match 'retaining worker handle until it exits' -and
        $applicationSource -match 'MacroPlayer::Cancel\(\);'
    ) `
    -Detail "A timed-out macro worker must block a second input injector and be cancelled before UI teardown"

$pluginShutdownIndex = $applicationSource.IndexOf('m_appCtx->pluginManager->RequestShutdown()')
$dispatcherShutdownIndex = $applicationSource.IndexOf('m_appCtx->uiDispatcher->Shutdown()')
$backgroundShutdownIndex = $applicationSource.IndexOf('m_appCtx->backgroundTasks->Shutdown(std::chrono::milliseconds(1500))')
$destroyMainWindowIndex = $applicationSource.IndexOf('DestroyWindow(m_hMainWnd)')
Add-TestResult `
    -Name "Application shuts down async work before destroying windows" `
    -Passed (
        $pluginShutdownIndex -ge 0 -and
        $dispatcherShutdownIndex -gt $pluginShutdownIndex -and
        $backgroundShutdownIndex -gt $dispatcherShutdownIndex -and
        $destroyMainWindowIndex -gt $backgroundShutdownIndex
    ) `
    -Detail "Plugin submissions, UI dispatch, and background tasks must stop before window teardown"

$projectPath = Join-Path $repoRoot "WinLauncher\WinLauncher.vcxproj"
$filtersPath = Join-Path $repoRoot "WinLauncher\WinLauncher.vcxproj.filters"
$projectXml = [xml](Get-Content -LiteralPath $projectPath)
$filtersXml = [xml](Get-Content -LiteralPath $filtersPath)
$projectNs = New-Object System.Xml.XmlNamespaceManager($projectXml.NameTable)
$projectNs.AddNamespace("msb", "http://schemas.microsoft.com/developer/msbuild/2003")
$filtersNs = New-Object System.Xml.XmlNamespaceManager($filtersXml.NameTable)
$filtersNs.AddNamespace("msb", "http://schemas.microsoft.com/developer/msbuild/2003")

foreach ($kind in @("ClCompile", "ClInclude", "ResourceCompile", "Manifest")) {
    $projectItems = @($projectXml.SelectNodes("//msb:$kind", $projectNs) | ForEach-Object { $_.Include } | Sort-Object -Unique)
    $filterItems = @($filtersXml.SelectNodes("//msb:$kind", $filtersNs) | ForEach-Object { $_.Include } | Sort-Object -Unique)
    $missing = @($projectItems | Where-Object { $_ -notin $filterItems })
    Add-TestResult `
        -Name "Project filters include all $kind entries" `
        -Passed ($missing.Count -eq 0) `
        -Detail $(if ($missing.Count -eq 0) { "$($projectItems.Count) entries" } else { $missing -join ", " })
}

$failed = @($results | Where-Object { -not $_.Passed })
foreach ($result in $results) {
    $prefix = if ($result.Passed) { "[PASS]" } else { "[FAIL]" }
    Write-Host "$prefix $($result.Name) - $($result.Detail)"
}

if ($failed.Count -gt 0) {
    Write-Error "Static project tests failed with $($failed.Count) issue(s)."
    exit 1
}

Write-Host "Static project tests passed."
