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
