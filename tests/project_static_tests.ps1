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

$urlEditSource = Read-RepoFile "WinLauncher\Config\UrlEditForm.cpp"
$fileSelectionSource = Read-RepoFile "WinLauncher\Services\FileSelectionService.cpp"
$pluginManagerSource = Read-RepoFile "WinLauncher\App\PluginManager.cpp"
$loggerSource = Read-RepoFile "WinLauncher\App\Logger.cpp"
$crashSource = Read-RepoFile "WinLauncher\App\CrashReporter.cpp"

Add-TestResult `
    -Name "Detached threads are isolated to bounded shutdown fallback" `
    -Passed (
        @(Get-ChildItem -LiteralPath (Join-Path $repoRoot "WinLauncher") -Recurse -Include *.cpp,*.h |
            Where-Object { $_.Name -ne "BackgroundTaskService.cpp" } |
            Where-Object { (Get-Content -LiteralPath $_.FullName -Raw) -cmatch '\.detach\s*\(' }).Count -eq 0
    ) `
    -Detail "Feature code must submit work through BackgroundTaskService instead of fire-and-forget threads"

Add-TestResult `
    -Name "URL editor workers do not capture form instances" `
    -Passed ($urlEditSource -match 'url\.latency' -and $urlEditSource -match 'url\.favicon' -and $urlEditSource -notmatch 'std::thread\s*\(\s*\[this') `
    -Detail "Network workers must return through lifetime-checked UI dispatch"

Add-TestResult `
    -Name "File selection uses cancellable request state" `
    -Passed ($fileSelectionSource -match 'SelectionRequest' -and $fileSelectionSource -match 'Priority::Interactive' -and $fileSelectionSource -cnotmatch '\.detach\s*\(') `
    -Detail "Selection completion must not call PopupWindow from a worker thread"

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
