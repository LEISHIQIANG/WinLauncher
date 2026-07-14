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
    return Get-Content -Encoding UTF8 -LiteralPath (Join-Path $repoRoot $RelativePath) -Raw
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
$commandExecSource = Read-RepoFile "WinLauncher\Services\CommandExecutionService.cpp"
Add-TestResult `
    -Name "Command capture opens live panel before work" `
    -Passed (
        $popupSource -match 'CommandPanelWindow::ShowLive' -and
        (
            ($popupSource -match 'ExecuteProcessStreaming' -and $popupSource -match 'ConfirmHighRiskCommand') -or
            ($commandExecSource -match 'ExecuteProcessStreaming' -and $popupSource -match 'ctx->userInteraction->ConfirmHighRiskCommand')
        )
    ) `
    -Detail "Long-running command execution must remain asynchronous and risk-gated"

Add-TestResult `
    -Name "Built-in reload is silent" `
    -Passed (
        $popupSource -match 'PopupCommandDispatcher::IsBuiltin\(item\.pluginId, item\.pluginCommandId, L"winlauncher\.reload"\)' -and
        $popupSource -match 'silent reload result' -and
        $popupSource -match ('ToastWindow::Show\(ok \? L"' + (-join [char[]]@(0x63D2, 0x4EF6, 0x5DF2, 0x91CD, 0x65B0, 0x52A0, 0x8F7D)) + '"') -and
        $popupSource -match 'ExecuteSlashCommand\(\s*L"", item\.pluginCommandId.*nullptr\)'
    ) `
    -Detail "The built-in reload slash command must refresh plugins without opening a command output panel"

$urlEditSource = Read-RepoFile "WinLauncher\Config\UrlEditForm.cpp"
$faviconFetcherSource = Read-RepoFile "WinLauncher\Services\FaviconFetcher.cpp"
$diagnosticSource = Read-RepoFile "WinLauncher\Services\DiagnosticService.cpp"
$migrationSource = Read-RepoFile "WinLauncher\Services\MigrationBackupService.cpp"
$folderWatcherSource = Read-RepoFile "WinLauncher\Services\FolderWatcher.cpp"
$fileSelectionSource = Read-RepoFile "WinLauncher\Services\FileSelectionService.cpp"
$pluginManagerSource = Read-RepoFile "WinLauncher\App\PluginManager.cpp"
$loggerSource = Read-RepoFile "WinLauncher\App\Logger.cpp"
$crashSource = Read-RepoFile "WinLauncher\App\CrashReporter.cpp"
$inputHookStopHeader = Read-RepoFile "WinLauncher\App\InputHookThreadStop.h"
$keyboardHookSource = Read-RepoFile "WinLauncher\KeyboardHook.cpp"
$mouseHookSource = Read-RepoFile "WinLauncher\MouseHook.cpp"
$macroServiceSource = Read-RepoFile "WinLauncher\Services\MacroService.cpp"
$macroEditFormSource = Read-RepoFile "WinLauncher\Config\MacroEditForm.cpp"
$batchLaunchSource = Read-RepoFile "WinLauncher\Services\BatchLaunchService.cpp"
$applicationSource = Read-RepoFile "WinLauncher\App\Application.cpp"
$popupHeader = Read-RepoFile "WinLauncher\PopupWindow.h"
$commandPanelSource = Read-RepoFile "WinLauncher\Config\CommandPanelWindow.cpp"
$commandPanelHeader = Read-RepoFile "WinLauncher\Config\CommandPanelWindow.h"

Add-TestResult `
    -Name "Favicon retrieval uses layered, privacy-scoped fallbacks" `
    -Passed (
        $faviconFetcherSource -match 'INTERNET_FLAG_NO_AUTO_REDIRECT' -and
        $faviconFetcherSource -match 'UrlCombineW' -and
        $faviconFetcherSource -match 'ParseHtmlIconLinks' -and
        $faviconFetcherSource -match 'ParseManifestIconUrls' -and
        $faviconFetcherSource -match 'ParseMetaImageUrls' -and
        $faviconFetcherSource -match 'k_CommonIconPaths' -and
        $faviconFetcherSource -match 'FetchPublicFallbackFavicon' -and
        $faviconFetcherSource -match 'ExtractHostname' -and
        $faviconFetcherSource -match 'domain=' -and
        $faviconFetcherSource -match 'CacheCandidateUrls' -and
        $faviconFetcherSource -match 'DetectIconExtension' -and
        $faviconFetcherSource -match 'StoreIconInCache' -and
        $faviconFetcherSource -match 'original alpha channel' -and
        $faviconFetcherSource -match 'GetFaviconLookupHosts' -and
        $faviconFetcherSource -match 'icons\.duckduckgo\.com/ip3/' -and
        $faviconFetcherSource -match 'HasOpaqueNeutralCanvas'
    ) `
    -Detail "Favicon fetching must try layered fallbacks while preserving supported source formats and alpha"

Add-TestResult `
    -Name "Favicon work stays inside the bounded background task service" `
    -Passed ($faviconFetcherSource -notmatch 'std::async\s*\(') `
    -Detail "Favicon probing must not create nested unmanaged async workers"

Add-TestResult `
    -Name "Diagnostics export metadata only" `
    -Passed ($diagnosticSource -match 'schemaVersion\\\":2' -and $diagnosticSource -notmatch 'debug-ring\.jsonl' -and $diagnosticSource -notmatch 'recent\.jsonl' -and $diagnosticSource -match 'ArchiveUtility::CompressDirectoryContents') `
    -Detail "Diagnostic ZIPs must not contain raw logs, paths, commands, or user content"

Add-TestResult `
    -Name "Migration validates ZIP paths before extraction and preserves merge semantics" `
    -Passed ($migrationSource -match 'ValidateMigrationZip\(zipPath' -and $migrationSource -match 'ArchiveUtility::ExpandArchive' -and $migrationSource -match (-join [char[]]@(0x672C, 0x673A, 0x989D, 0x5916, 0x6570, 0x636E, 0x5DF2, 0x4FDD, 0x7559)) -and $migrationSource -match 'kMaxMigrationUncompressedBytes') `
    -Detail "Migration imports must reject unsafe ZIPs before extraction and only merge supplied data"

Add-TestResult `
    -Name "Folder watcher has per-folder backoff and recovery" `
    -Passed ($folderWatcherSource -match 'kRecoveryDelaysMs\[\] = \{ 5000, 15000, 60000 \}' -and $folderWatcherSource -match 'folder_unavailable' -and $folderWatcherSource -match 'folder_recovered') `
    -Detail "Missing sync folders must back off independently and recover without log storms"
$popupWindowHeader = Read-RepoFile "WinLauncher\PopupWindow.h"
$popupFileSelectionControllerSource = Read-RepoFile "WinLauncher\Popup\PopupFileSelectionController.cpp"
$popupIconRefreshControllerSource = Read-RepoFile "WinLauncher\Popup\PopupIconRefreshController.cpp"
$fileSelectionServiceSource = Read-RepoFile "WinLauncher\Services\FileSelectionService.cpp"
$configWindowSource = Read-RepoFile "WinLauncher\Config\ConfigWindow.cpp"
$configViewModelSource = Read-RepoFile "WinLauncher\ViewModel\ConfigViewModel.h"
$iniConfigHeader = Read-RepoFile "WinLauncher\Services\IniConfigRepository.h"
$iniConfigSource = Read-RepoFile "WinLauncher\Services\IniConfigRepository.cpp"
$pluginAbiSource = Read-RepoFile "WinLauncher\SDK\include\WinLauncher\WinLauncherPluginABI.h"
$networkPluginSource = Read-RepoFile "plugins\network_tools\network_tools.cpp"
$commandVariableSource = Read-RepoFile "WinLauncher\Services\CommandVariableService.cpp"
$glassWindowSource = Read-RepoFile "WinLauncher\GlassWindow.cpp"
$shadowWindowSource = Read-RepoFile "WinLauncher\ShadowWindow.cpp"
$shortcutDialogSource = Read-RepoFile "WinLauncher\Config\ShortcutDialog.cpp"
$confirmWindowSource = Read-RepoFile "WinLauncher\Config\ConfirmWindow.cpp"
$promptWindowSource = Read-RepoFile "WinLauncher\Config\PromptWindow.cpp"
$trayMenuSource = Read-RepoFile "WinLauncher\TrayMenuWindow.cpp"
$contextMenuSource = Read-RepoFile "WinLauncher\Config\ContextMenu.cpp"
$dropDownMenuSource = Read-RepoFile "WinLauncher\Config\DropDownMenu.cpp"
$shortcutPageSource = Read-RepoFile "WinLauncher\Config\ShortcutPage.cpp"
$shortcutPageHeader = Read-RepoFile "WinLauncher\Config\ShortcutPage.h"
$commandEditFormSource = Read-RepoFile "WinLauncher\Config\CommandEditForm.cpp"
$environmentDetectorSource = Read-RepoFile "WinLauncher\Services\EnvironmentDetector.cpp"
$pluginManagerSource = Read-RepoFile "WinLauncher\App\PluginManager.cpp"
$shortcutManagerSource = Read-RepoFile "WinLauncher\ShortcutManager.cpp"
$updateServiceSource = Read-RepoFile "WinLauncher\Services\UpdateService.cpp"
$fileToolsSource = Read-RepoFile "plugins\file_tools\file_tools.cpp"

Add-TestResult `
    -Name "Visible secondary windows retain shadows after deactivation" `
    -Passed (
        $glassWindowSource -match 'case\s+WM_ACTIVATE:[\s\S]{0,500}activationResult\s*=\s*DefWindowProcW\([\s\S]{0,300}SyncPosition\(IsWindowVisible\(hWnd\)\s*&&\s*!IsIconic\(hWnd\)\)[\s\S]{0,120}return\s+activationResult' -and
        $shadowWindowSource -match 'GetWindow\(m_hMainWnd,\s*GW_OWNER\)' -and
        $shadowWindowSource -match 'shadowOwner' -and
        $shadowWindowSource -match 'SWP_NOOWNERZORDER' -and
        $shortcutDialogSource -match 'case\s+WM_ACTIVATE:\s*GlassWindow::HandleMessage' -and
        $confirmWindowSource -match 'case\s+WM_ACTIVATE:\s*\{\s*GlassWindow::HandleMessage' -and
        $promptWindowSource -match 'case\s+WM_ACTIVATE:\s*GlassWindow::HandleMessage' -and
        $commandPanelSource -match 'case\s+WM_ACTIVATE:\s*\{\s*GlassWindow::HandleMessage' -and
        $popupSource -match 'case\s+WM_ACTIVATE:\s*\{\s*[\s\S]{0,400}GlassWindow::HandleMessage' -and
        $trayMenuSource -match 'case\s+WM_ACTIVATE:\s*GlassWindow::HandleMessage' -and
        $contextMenuSource -match 'case\s+WM_ACTIVATE:\s*GlassWindow::HandleMessage' -and
        $dropDownMenuSource -match 'case\s+WM_ACTIVATE:\s*GlassWindow::HandleMessage'
    ) `
    -Detail "Activation handlers must retain the shared shadow for visible dialogs whether focused or not"

Add-TestResult `
    -Name "Shortcut grid multi-selection has a safe batch favicon action" `
    -Passed (
        $shortcutPageSource -match 'preserveMultiSelection' -and
        $shortcutPageSource -match 'selectedIndices\.size\(\) > 1' -and
        $shortcutPageSource -match ('L"' + (-join [char[]]@(0x5220, 0x9664)) + '"') -and
        $shortcutPageSource -match ('L"' + (-join [char[]]@(0x83B7, 0x53D6, 0x56FE, 0x6807)) + '"') -and
        $shortcutPageSource -match 'FetchSelectedUrlFavicons' -and
        $shortcutPageSource -match 'Model::ShortcutType::Url' -and
        $shortcutPageSource -match 'Submit\(\s*L"config\.url_favicon\.batch"' -and
        $shortcutPageSource -match 'FaviconFetcher::FetchFavicon' -and
        $shortcutPageSource -match 'CancelBatchFaviconFetches' -and
        $shortcutPageHeader -match 'std::vector<BackgroundTaskService::TaskHandle>\s+m_batchFaviconTasks'
    ) `
    -Detail "Multi-select menus must omit edit actions and batch-fetch only URL icons through cancellable background tasks"

Add-TestResult `
    -Name "Command type selection uses versioned Python startup snapshots" `
    -Passed (
        $commandEditFormSource -match '#include "\.\./Services/EnvironmentDetector\.h"' -and
        $commandEditFormSource -match 'EnvironmentDetector::IsDetectionComplete\(\)' -and
        $commandEditFormSource -match 'EnvironmentDetector::GetPythonInterpreters\(\)' -and
        $commandEditFormSource -match 'L"python:" \+ interpreter\.version' -and
        $commandEditFormSource -match 'EnvironmentDetector::IsAvailable\(L"gitbash"\)' -and
        $commandEditFormSource -notmatch 'CheckPythonAvailable' -and
        $commandEditFormSource -notmatch 'CheckGitBashAvailable' -and
        $environmentDetectorSource -match 'L"py\.exe"' -and
        $environmentDetectorSource -match 'L"-0p"' -and
        $environmentDetectorSource -match 'SOFTWARE\\\\Python\\\\PythonCore' -and
        $environmentDetectorSource -match 'TryGetPythonInterpreter' -and
        (
            ($popupSource -match 'EnvironmentDetector::TryGetPythonInterpreter\(type, python\)' -and $popupSource -match 'GetPythonUnavailableMessage\(type\)') -or
            ($commandExecSource -match 'EnvironmentDetector::TryGetPythonInterpreter\(\w+(?:\.\w+)?, python\)' -and $commandExecSource -match 'GetPythonUnavailableMessage\(\w+(?:\.\w+)?\)')
        )
    ) `
    -Detail "Python command choices must be versioned startup-snapshot entries and execution must not fall back to another version"

Add-TestResult `
    -Name "Glow and glass material frames use one DPI-aligned clean edge" `
    -Passed (
        $glassWindowSource -match 'struct\s+MaterialFrameGeometry' -and
        $glassWindowSource -match 'BuildMaterialFrameGeometry\(w, h, drawCornerRadius, borderOffset, borderWidth\)' -and
        $glassWindowSource -match 'RoundedRect\(materialFrame\.clipBounds, materialFrame\.clipRadius' -and
        $glassWindowSource -match 'RoundedRect\(\s*materialFrame\.borderBounds,\s*materialFrame\.borderRadius' -and
        $glassWindowSource -match 'const bool isDarkMaterial' -and
        $glassWindowSource -notmatch 'outerDarkBrush' -and
        $glassWindowSource -notmatch 'Outer thin dark border for desktop contrast'
    ) `
    -Detail "Shared glow and glass rendering must align clip and frame geometry and avoid a hard dark outer outline"

Add-TestResult `
    -Name "Detached threads are isolated to bounded shutdown fallback" `
    -Passed (
        @(Get-ChildItem -LiteralPath (Join-Path $repoRoot "WinLauncher") -Recurse -File -Include *.cpp,*.h |
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
        $popupIconRefreshControllerSource -match 'm_pending\s*=\s*false'
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
        (
            $popupSource -match (-join [char[]]@(0x79D2, 0x8D85, 0x65F6, 0x65F6, 0x95F4, 0xFF0C, 0x8FDB, 0x7A0B, 0x5DF2, 0x7EC8, 0x6B62)) -or
            $commandExecSource -match (-join [char[]]@(0x79D2, 0x8D85, 0x65F6, 0x65F6, 0x95F4, 0xFF0C, 0x8FDB, 0x7A0B, 0x5DF2, 0x7EC8, 0x6B62))
        )
    ) `
    -Detail "Invalid command timeout settings must use the 300-second default and clearly label terminated commands"

Add-TestResult `
    -Name "Popup lifecycle cancels pending file selection work" `
    -Passed (
        $popupHeader -match 'void\s+CancelFileSelectionQuery\(\)' -and
        $popupSource -match 'void\s+PopupWindow::CancelFileSelectionQuery\(\)' -and
        $popupSource -match 'm_fileSelection\.Cancel\(\)' -and
        $popupFileSelectionControllerSource -match 'request->Cancel\(\)' -and
        $popupSource -match 'KillTimer\(hWnd, FILE_SELECTION_TIMER_ID\)' -and
        ([regex]::Matches($popupSource, 'CancelFileSelectionQuery\(\);')).Count -ge 5
    ) `
    -Detail "Closing, destroying, replacing, and releasing popups must cancel obsolete Shell selection work"

Add-TestResult `
    -Name "File selection fallback completes with its original request context" `
    -Passed (
        $fileSelectionServiceSource -match 'const auto completeEmpty' -and
        $fileSelectionServiceSource -match 'm_result\.sourceHwnd = activeHwnd' -and
        $fileSelectionServiceSource -match 'm_result\.capturedTime = capturedTime' -and
        ([regex]::Matches($fileSelectionServiceSource, 'completeEmpty\(\);')).Count -ge 2
    ) `
    -Detail "An unavailable or saturated task service must finish selection capture without leaving the popup pending"

Add-TestResult `
    -Name "Command panel clears loading state when task submission fails" `
    -Passed (
        ([regex]::Matches($commandPanelSource, 'm_refreshRunning = false;')).Count -ge 2 -and
        ([regex]::Matches($commandPanelSource, 'm_workerGeneration = 0;')).Count -ge 2 -and
        ([regex]::Matches($commandPanelSource, (-join [char[]]@(0x540E, 0x53F0, 0x4EFB, 0x52A1, 0x7E41, 0x5FD9, 0xFF0C, 0x547D, 0x4EE4, 0x672A, 0x542F, 0x52A8)))).Count -ge 2
    ) `
    -Detail "A saturated task queue must not leave a reused or new command panel spinning indefinitely"

Add-TestResult `
    -Name "Configuration writes are immediate and observers reload only saved state" `
    -Passed (
        $iniConfigHeader -match 'class\s+IniConfigRepository\s+final\s*:\s*public\s+IConfigService' -and
        $iniConfigHeader -match 'std::unique_ptr<Impl>\s+m_impl' -and
        $iniConfigSource -match 'IniConfigDocument::Escape' -and
        $iniConfigSource -match 'IniConfigDocument::Unescape' -and
        $iniConfigSource -match 'ConfigFileStore::ReadUtf8' -and
        $iniConfigSource -match 'ConfigFileStore::AtomicWriteUtf8' -and
        $iniConfigSource -match 'ConfigFileStore::IsPathUnderDirectory' -and
        $iniConfigSource -notmatch 'static std::wstring EscapeValue' -and
        $iniConfigSource -notmatch 'static std::wstring ReadFile' -and
        $iniConfigSource -match 'WriteConfigContent\(content, pages\.size\(\)\);' -and
        $iniConfigSource -match 'std::lock_guard<std::mutex> writeLock\(m_configWriteMutex\)' -and
        $iniConfigSource -notmatch 'QueueConfigWrite' -and
        $iniConfigSource -notmatch 'ConfigSaveWorkerLoop' -and
        $configViewModelSource -match 'void\s+SaveConfig\(bool\s+publishConfigChanged\s*=\s*true\)' -and
        $configWindowSource -match 'SaveConfig\(false, true\);' -and
        $configWindowSource -match 'm_appCtx->eventBus->Publish\(EventType::ConfigChanged\);'
    ) `
    -Detail "Config state must reach disk before popup or settings observers reload it; only logs retain batching"

Add-TestResult `
    -Name "Plugin UI and shutdown use guarded lifetimes" `
    -Passed ($pluginManagerSource -match 'RequestShutdown' -and $pluginManagerSource -match 'm_activeExecutions' -and $pluginManagerSource -match 'm_uiDispatcher->InvokeSync' -and $pluginManagerSource -match 'm_uiDispatcher->Post' -and $pluginManagerSource -match 'IsCurrentTaskCancellationRequested') `
    -Detail "Plugin tasks must retain manager lifetime and marshal UI work"

Add-TestResult `
    -Name "Plugins use size-guarded cooperative unload and DNS cancellation" `
    -Passed (
        $pluginAbiSource -match 'requestShutdown' -and
        $pluginAbiSource -match 'isShutdownComplete' -and
        $pluginManagerSource -match 'SupportsCooperativeShutdown' -and
        $pluginManagerSource -match 'm_retiringPlugins' -and
        $pluginManagerSource -match 'Plugin retirement scheduled' -and
        $pluginManagerSource -match 'weak_from_this\(\)\.lock\(\)' -and
        $networkPluginSource -match 'DnsQueryEx' -and
        $networkPluginSource -match 'DnsCancelQuery' -and
        $networkPluginSource -notmatch '\.detach\s*\('
    ) `
    -Detail "Async plugins must retain their DLL until cancellation callbacks have completed"

Add-TestResult `
    -Name "Crash reporting is independent from normal logger locks" `
    -Passed ($crashSource -match 'MiniDumpWriteDump' -and $crashSource -match 'MiniDumpWithThreadInfo' -and $crashSource -match 'MiniDumpWithUnloadedModules' -and $loggerSource -notmatch 'SetUnhandledExceptionFilter\(') `
    -Detail "Unhandled exceptions must use the dedicated dump thread, not the normal log mutex"

Add-TestResult `
    -Name "Normal logging uses a bounded JSONL batch queue" `
    -Passed ($loggerSource -match 'kMaxPendingLogEntries\s*=\s*4096' -and $loggerSource -match 'm_pendingLogs' -and $loggerSource -match 'kFlushBytes\s*=\s*64 \* 1024' -and $loggerSource -match 'std::chrono::seconds\(1\)' -and $loggerSource -match 'jsonl') `
    -Detail "INFO and WARN records must use a bounded batch queue while DEBUG stays in the in-memory ring"

Add-TestResult `
    -Name "Input hook threads use one bounded shutdown primitive" `
    -Passed (
        $inputHookStopHeader -match 'PostThreadMessageW\(threadId, WM_QUIT' -and
        $inputHookStopHeader -match 'GetTickCount64\(\) \+ timeoutMs' -and
        $inputHookStopHeader -match 'WaitForSingleObject\(thread, remainingMs\)' -and
        $inputHookStopHeader -match 'bool\s+timedOut' -and
        $inputHookStopHeader -match 'bool\s+ReapIfExited' -and
        $inputHookStopHeader -notmatch 'TerminateThread\(' -and
        $keyboardHookSource -match 'InputHookThreadStop::RequestStop' -and
        $mouseHookSource -match 'InputHookThreadStop::RequestStop' -and
        $macroServiceSource -match 'InputHookThreadStop::RequestStop' -and
        $keyboardHookSource -notmatch 'TerminateThread\(' -and
        $mouseHookSource -notmatch 'TerminateThread\(' -and
        $macroServiceSource -notmatch 'TerminateThread\('
    ) `
    -Detail "Keyboard, mouse, and macro hook threads must quit cooperatively and retain timed-out handles for safe reaping"

Add-TestResult `
    -Name "Paused popup triggers pass all input through and coalesce pending requests" `
    -Passed (
        $mouseHookSource -match 'void\s+MouseHook::SetTriggerEnabled\s*\(' -and
        $mouseHookSource -match 's_suppressButtonUpMask\.store\(0' -and
        $mouseHookSource -match 's_popupRequestPending\.compare_exchange_strong' -and
        $mouseHookSource -match 'void\s+MouseHook::AcknowledgePopupRequest\s*\(' -and
        $applicationSource -match 'MouseHook::SetTriggerEnabled\(!m_popupPaused\)' -and
        $applicationSource -match 'MouseHook::AcknowledgePopupRequest\(\)'
    ) `
    -Detail "Paused triggers must not consume mouse input, while active high-frequency presses keep one pending UI request"

Add-TestResult `
    -Name "Macro playback interruption stays non-blocking and ignores injected input" `
    -Passed (
        $macroServiceSource -match 'RequestInterruptFromKeyboard' -and
        $macroServiceSource -match 'RequestInterruptFromMouse' -and
        $macroServiceSource -match 'LLKHF_INJECTED' -and
        $macroServiceSource -match 'LLMHF_INJECTED' -and
        $macroServiceSource -match 'MacroSendInputSignature' -and
        $keyboardHookSource -match 'MacroPlayer::RequestInterruptFromKeyboard' -and
        $mouseHookSource -match 'MacroPlayer::RequestInterruptFromMouse'
    ) `
    -Detail "Physical input may request playback cancellation without blocking a low-level hook callback"

Add-TestResult `
    -Name "Macro recording batches high-frequency preview updates by session" `
    -Passed (
        $macroServiceSource -match 'SetTimer\(nullptr, 0, 33, UiUpdateTimerCallback\)' -and
        $macroServiceSource -match 's_uiUpdateSession' -and
        $macroServiceSource -match 'scheduledSession != CurrentSession\(\)' -and
        $macroEditFormSource -match 'm_recordingSession' -and
        $macroEditFormSource -match 'wParam != m_recordingSession'
    ) `
    -Detail "Drag recording must throttle preview messages and reject an obsolete recording session"

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
$pluginRetirementIndex = $applicationSource.IndexOf('m_appCtx->pluginManager->Shutdown()')
$backgroundShutdownIndex = $applicationSource.IndexOf('m_appCtx->backgroundTasks->Shutdown(std::chrono::milliseconds(1500))')
$destroyMainWindowIndex = $applicationSource.IndexOf('DestroyWindow(m_hMainWnd)')
Add-TestResult `
    -Name "Application shuts down async work before destroying windows" `
    -Passed (
        $pluginShutdownIndex -ge 0 -and
        $dispatcherShutdownIndex -gt $pluginShutdownIndex -and
        $pluginRetirementIndex -gt $dispatcherShutdownIndex -and
        $backgroundShutdownIndex -gt $pluginRetirementIndex -and
        $destroyMainWindowIndex -gt $backgroundShutdownIndex
    ) `
    -Detail "Plugin submissions, UI dispatch, and background tasks must stop before window teardown"

Add-TestResult `
    -Name "Single-instance Mutex guard is implemented" `
    -Passed (
        $applicationSource -match 'CreateMutexW\(' -and
        $applicationSource -match 'ERROR_ALREADY_EXISTS' -and
        $applicationSource -match 'ReleaseMutex\(m_hSingleInstanceMutex\)' -and
        $applicationSource -match 'CloseHandle\(m_hSingleInstanceMutex\)'
    ) `
    -Detail "Process startup must check named Mutex to guard against concurrent startup race conditions"

Add-TestResult `
    -Name "Hook watchdog auto-recovery is wired" `
    -Passed (
        $applicationSource -match 'PostMessageW\(hWnd, AppMessages::RestartHook' -and
        $applicationSource -match 'shouldRecoverHooks\s*=\s*false'
    ) `
    -Detail "The UI watchdog must trigger a hook restart when the UI thread becomes responsive again"

Add-TestResult `
    -Name "Direct2D device-loss recovery fallback is implemented" `
    -Passed (
        $glassWindowSource -match 'm_consecutiveDeviceLossCount\+\+' -and
        ($glassWindowSource -match 'gpu_crash_recovery\.marker' -or
         $glassWindowSource -match 'GetGpuCrashMarkerPath()') -and
        $glassWindowSource -match 'm_consecutiveDeviceLossCount\s*=\s*0'
    ) `
    -Detail "D2D drawing target recreation must monitor consecutive failures and write the recovery marker"

Add-TestResult `
    -Name "Configuration and plugin writes are atomic" `
    -Passed (
        $shortcutManagerSource -match 'MoveFileExW\(tempPath\.c_str\(\), path\.c_str\(\), MOVEFILE_REPLACE_EXISTING' -and
        $pluginManagerSource -match 'MoveFileExW\(tempPath\.c_str\(\), path\.c_str\(\), MOVEFILE_REPLACE_EXISTING'
    ) `
    -Detail "All file writes of user shortcuts and plugin settings must write to .tmp and swap atomically"

Add-TestResult `
    -Name "Update service download is atomic and has connection timeouts" `
    -Passed (
        $updateServiceSource -match 'INTERNET_OPTION_CONNECT_TIMEOUT' -and
        $updateServiceSource -match 'INTERNET_OPTION_RECEIVE_TIMEOUT' -and
        $updateServiceSource -match 'MoveFileExW\(tempTargetPath\.c_str\(\), targetPath\.c_str\(\), MOVEFILE_REPLACE_EXISTING'
    ) `
    -Detail "The update service download must configure WinINet timeouts and write to target path atomically"

Add-TestResult `
    -Name "File Tools plugin uses permissive file sharing" `
    -Passed (
        $fileToolsSource -match 'CreateFileW\(path\.c_str\(\),\s*GENERIC_READ,\s*FILE_SHARE_READ\s*\|\s*FILE_SHARE_WRITE\s*\|\s*FILE_SHARE_DELETE' -and
        $fileToolsSource -match 'CreateFileW\(fullPath\.c_str\(\),\s*GENERIC_READ,\s*FILE_SHARE_READ\s*\|\s*FILE_SHARE_WRITE\s*\|\s*FILE_SHARE_DELETE'
    ) `
    -Detail "The File Tools hash and size operations must open files with read, write, and delete sharing"

Add-TestResult `
    -Name "Popup show grace period is implemented" `
    -Passed (
        $popupSource -match 'GetTimeInSeconds\(\)\s*-\s*m_showTimeSeconds\s*<\s*0\.5'
    ) `
    -Detail "PopupWindow must ignore auto-close triggers within 500ms grace period to prevent cold-startup close race conditions"

$projectPath = Join-Path $repoRoot "WinLauncher\WinLauncher.vcxproj"
$filtersPath = Join-Path $repoRoot "WinLauncher\WinLauncher.vcxproj.filters"
$projectXml = [xml](Get-Content -Encoding UTF8 -LiteralPath $projectPath -Raw)
$filtersXml = [xml](Get-Content -Encoding UTF8 -LiteralPath $filtersPath -Raw)
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

# --- P3-A 新增稳定性断言 ---

# GPU 崩溃恢复标记路径必须通过 ConfigPath::GetGpuCrashMarkerPath() 统一获取，
# 不得使用 GetEnvironmentVariableW("APPDATA",...) 手动拼接。
$glassWindowSource = Read-RepoFile "WinLauncher\GlassWindow.cpp"
$applicationSource = Read-RepoFile "WinLauncher\App\Application.cpp"
Add-TestResult `
    -Name "GPU crash marker path uses ConfigPath (GlassWindow compositor)" `
    -Passed ($glassWindowSource -match 'ConfigPath::GetGpuCrashMarkerPath\(\)' -and
             ($glassWindowSource -notmatch 'GetEnvironmentVariableW\(L"APPDATA"[^)]*\)\s*\)\s*\{[\s\S]{0,200}gpu_crash_recovery')) `
    -Detail "GlassWindow compositor-path GPU marker must use ConfigPath::GetGpuCrashMarkerPath()"

Add-TestResult `
    -Name "GPU crash marker path uses ConfigPath (GlassWindow legacy)" `
    -Passed ($glassWindowSource -match 'ConfigPath::GetGpuCrashMarkerPath\(\)') `
    -Detail "GlassWindow legacy-path GPU marker must use ConfigPath::GetGpuCrashMarkerPath()"

Add-TestResult `
    -Name "GPU crash marker path uses ConfigPath (Application startup)" `
    -Passed ($applicationSource -match 'ConfigPath::GetGpuCrashMarkerPath\(\)' -and
             $applicationSource -notmatch 'GetEnvironmentVariableW\(L"APPDATA"[^)]*gpu_crash') `
    -Detail "Application::Run GPU marker detection must use ConfigPath::GetGpuCrashMarkerPath()"

# RestartApp 定时器必须使用 AppMessages::RestartAppTimerId 命名常量，不得包含裸 0xDEAD
Add-TestResult `
    -Name "RestartApp timer uses named constant, no magic 0xDEAD" `
    -Passed ($applicationSource -match 'AppMessages::RestartAppTimerId' -and
             $applicationSource -notmatch 'SetTimer\s*\([^,]+,\s*0xDEAD\s*,') `
    -Detail "SetTimer in RestartApp must use AppMessages::RestartAppTimerId"

# AppMessages.h 必须声明 RestartAppTimerId 常量
$appMessagesSource = Read-RepoFile "WinLauncher\App\AppMessages.h"
Add-TestResult `
    -Name "AppMessages.h declares RestartAppTimerId" `
    -Passed ($appMessagesSource -match 'RestartAppTimerId') `
    -Detail "AppMessages.h must define RestartAppTimerId for timer ID registration"

# FaviconFetcher Strategy C 必须包含后台任务取消检查
# Check using the pre-loaded $faviconFetcherSource variable
$hasCancellation = $false
if ($faviconFetcherSource) {
    $hasCancellation = $faviconFetcherSource.Contains("IsCurrentTaskCancellationRequested")
} else {
    Write-Host "WARNING: faviconFetcherSource is null!"
}
Add-TestResult `
    -Name "FaviconFetcher Strategy C checks cancellation before each download" `
    -Passed ($hasCancellation) `
    -Detail "FaviconFetcher common-icon-paths loop must check BackgroundTaskService::IsCurrentTaskCancellationRequested()"

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
