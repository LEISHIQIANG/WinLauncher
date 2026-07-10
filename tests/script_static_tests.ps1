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

function Resolve-RepoPath {
    param([string]$RelativePath)
    return Join-Path $repoRoot $RelativePath
}

$requiredAutomationFiles = @(
    ".github\workflows\ci.yml",
    "scripts\ci_check.ps1",
    "scripts\maintenance_check.ps1",
    "scripts\build_plugins.ps1",
    "tests\run_tests.ps1",
    "tests\project_static_tests.ps1",
    "tests\script_static_tests.ps1",
    "tests\README.md",
    "tests\native\WinLauncherNativeTests.vcxproj",
    "tests\native\main.cpp"
)

foreach ($relativePath in $requiredAutomationFiles) {
    $path = Resolve-RepoPath $relativePath
    Add-TestResult "Automation file exists: $relativePath" (Test-Path -LiteralPath $path) $relativePath
}

$scriptFiles = @(
    "scripts\ci_check.ps1",
    "scripts\maintenance_check.ps1",
    "scripts\build_plugins.ps1",
    "tests\run_tests.ps1",
    "tests\project_static_tests.ps1",
    "tests\script_static_tests.ps1"
)

foreach ($relativePath in $scriptFiles) {
    $path = Resolve-RepoPath $relativePath
    if (-not (Test-Path -LiteralPath $path)) {
        continue
    }

    $tokens = $null
    $errors = $null
    [System.Management.Automation.Language.Parser]::ParseFile($path, [ref]$tokens, [ref]$errors) | Out-Null
    Add-TestResult "PowerShell syntax: $relativePath" ($errors.Count -eq 0) $(if ($errors.Count -eq 0) { "ok" } else { ($errors | ForEach-Object { $_.Message }) -join "; " })
}

$workflowPath = Resolve-RepoPath ".github\workflows\ci.yml"
if (Test-Path -LiteralPath $workflowPath) {
    $workflowText = Get-Content -LiteralPath $workflowPath -Raw
    Add-TestResult "CI runs on Windows" ($workflowText -match 'runs-on:\s*windows-latest') "windows-latest is required for MSBuild"
    Add-TestResult "CI calls source gate" ($workflowText -match '\\scripts\\ci_check\.ps1') "workflow should call scripts\ci_check.ps1"
    Add-TestResult "CI uses PowerShell" ($workflowText -match 'shell:\s*pwsh') "workflow should use pwsh"
}

$ciScriptPath = Resolve-RepoPath "scripts\ci_check.ps1"
if (Test-Path -LiteralPath $ciScriptPath) {
    $ciScript = Get-Content -LiteralPath $ciScriptPath -Raw
    Add-TestResult "CI script runs maintenance check" ($ciScript -match 'scripts\\maintenance_check\.ps1') "ci_check should run maintenance_check"
    Add-TestResult "CI script runs test suite" ($ciScript -match 'tests\\run_tests\.ps1') "ci_check should run tests\run_tests.ps1"
    Add-TestResult "CI script supports build skip" ($ciScript -match '\[switch\]\$SkipBuild') "SkipBuild keeps source-only validation possible"
    Add-TestResult "CI script supports plugin build opt-in" ($ciScript -match '\[switch\]\$BuildPlugins') "plugin builds should remain opt-in"
    Add-TestResult "CI runs native stability tests" ($ciScript -match 'WinLauncherNativeTests\.exe' -and $ciScript -match 'Native async/callback/crash tests') "Release validation must execute the native stability harness"
}

$failed = @($results | Where-Object { -not $_.Passed })
foreach ($result in $results) {
    $prefix = if ($result.Passed) { "[PASS]" } else { "[FAIL]" }
    Write-Host "$prefix $($result.Name) - $($result.Detail)"
}

if ($failed.Count -gt 0) {
    Write-Error "Script/CI static tests failed with $($failed.Count) issue(s)."
    exit 1
}

Write-Host "Script/CI static tests passed."
