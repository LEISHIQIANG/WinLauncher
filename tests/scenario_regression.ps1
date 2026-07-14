param()
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$required = @(
    'WinLauncher/Services/DiagnosticService.cpp',
    'WinLauncher/Services/UsageHistoryStore.cpp',
    'WinLauncher/Services/MigrationBackupService.cpp',
    'WinLauncher/PopupWindow.cpp',
    'WinLauncher/Popup/PopupSearchModel.cpp'
)
foreach ($path in $required) {
    if (-not (Test-Path (Join-Path $root $path))) { throw "Scenario regression prerequisite missing: $path" }
}
$popup = Get-Content (Join-Path $root 'WinLauncher/PopupWindow.cpp') -Raw
$popupSearchService = Get-Content (Join-Path $root 'WinLauncher/Popup/PopupSearchService.cpp') -Raw
$popupSearchModel = Get-Content (Join-Path $root 'WinLauncher/Popup/PopupSearchModel.cpp') -Raw
if ($popup -notmatch 'RecordAccepted' -or
    $popupSearchService -notmatch 'PopupSearchModel::SortKey' -or
    $popupSearchModel -notmatch 'prefixScore' -or
    $popupSearchModel -notmatch '-static_cast<int64_t>\(usage\.launchCount\)') {
    throw 'Search ranking regression: local usage ordering is not wired.'
}
$migration = Get-Content (Join-Path $root 'WinLauncher/Services/MigrationBackupService.cpp') -Raw
if ($migration -notmatch 'Preflight' -or $migration -notmatch 'manifest.json' -or $migration -notmatch 'pluginsIncluded') { throw 'Migration safety regression: preflight/manifest boundary missing.' }
$diagnostics = Get-Content (Join-Path $root 'WinLauncher/Services/DiagnosticService.cpp') -Raw
$archiveUtility = Get-Content (Join-Path $root 'WinLauncher/Services/ArchiveUtility.cpp') -Raw
if ($diagnostics -notmatch 'schemaVersion\\":2' -or
    $diagnostics -match 'debug-ring\.jsonl|recent\.jsonl|MiniDump' -or
    $diagnostics -notmatch 'ArchiveUtility::CompressDirectoryContents' -or
    $archiveUtility -notmatch 'PowerShellSingleQuoted|WaitForSingleObject|file_size') { throw 'Diagnostic privacy regression.' }
Write-Host 'Scenario regression checks passed.'
