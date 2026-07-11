param()
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$required = @(
    'WinLauncher/Services/DiagnosticService.cpp',
    'WinLauncher/Services/UsageHistoryStore.cpp',
    'WinLauncher/Services/MigrationBackupService.cpp',
    'WinLauncher/PopupWindow.cpp'
)
foreach ($path in $required) {
    if (-not (Test-Path (Join-Path $root $path))) { throw "Scenario regression prerequisite missing: $path" }
}
$popup = Get-Content (Join-Path $root 'WinLauncher/PopupWindow.cpp') -Raw
if ($popup -notmatch 'RecordAccepted' -or $popup -notmatch 'prefixScore') { throw 'Search ranking regression: local usage ordering is not wired.' }
$migration = Get-Content (Join-Path $root 'WinLauncher/Services/MigrationBackupService.cpp') -Raw
if ($migration -notmatch 'Preflight' -or $migration -notmatch 'manifest.json' -or $migration -notmatch 'pluginsIncluded') { throw 'Migration safety regression: preflight/manifest boundary missing.' }
$diagnostics = Get-Content (Join-Path $root 'WinLauncher/Services/DiagnosticService.cpp') -Raw
if ($diagnostics -notmatch 'Sanitize' -or $diagnostics -notmatch 'Compress-Archive' -or $diagnostics -match 'MiniDump') { throw 'Diagnostic privacy regression.' }
Write-Host 'Scenario regression checks passed.'
