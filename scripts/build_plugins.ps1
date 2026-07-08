param(
    [string]$Configuration = "Release",
    [string]$Platform = "x64",
    [string]$MsBuildPath = "msbuild",
    [string[]]$Plugin = @(),
    [switch]$IncludeSamples,
    [switch]$ContinueOnError
)

$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = Split-Path -Parent $scriptDir
$pluginsRoot = Join-Path $repoRoot "plugins"

if (-not (Test-Path -LiteralPath $pluginsRoot)) {
    throw "Missing plugins directory: $pluginsRoot"
}

$requested = @{}
foreach ($item in $Plugin) {
    if (-not [string]::IsNullOrWhiteSpace($item)) {
        $requested[$item.ToLowerInvariant()] = $true
    }
}

$pluginDirs = Get-ChildItem -LiteralPath $pluginsRoot -Directory | Sort-Object Name
$targets = New-Object System.Collections.Generic.List[object]

foreach ($dir in $pluginDirs) {
    $packageScript = Join-Path $dir.FullName "package.ps1"
    $manifestPath = Join-Path $dir.FullName "plugin.json"
    if (-not (Test-Path -LiteralPath $packageScript) -or -not (Test-Path -LiteralPath $manifestPath)) {
        continue
    }

    $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    $pluginId = [string]$manifest.id

    if ($requested.Count -gt 0 -and
        -not $requested.ContainsKey($dir.Name.ToLowerInvariant()) -and
        -not $requested.ContainsKey($pluginId.ToLowerInvariant())) {
        continue
    }

    $targets.Add([pscustomobject]@{
        Name = $dir.Name
        Id = $pluginId
        Script = $packageScript
    }) | Out-Null
}

if ($IncludeSamples) {
    $sampleRoot = Join-Path $repoRoot "SDK\samples\hello_world"
    $sampleScript = Join-Path $sampleRoot "package.ps1"
    $sampleManifest = Join-Path $sampleRoot "plugin.json"
    if (Test-Path -LiteralPath $sampleScript -and Test-Path -LiteralPath $sampleManifest) {
        $manifest = Get-Content -LiteralPath $sampleManifest -Raw | ConvertFrom-Json
        $pluginId = [string]$manifest.id
        if ($requested.Count -eq 0 -or
            $requested.ContainsKey("hello_world") -or
            $requested.ContainsKey($pluginId.ToLowerInvariant())) {
            $targets.Add([pscustomobject]@{
                Name = "SDK sample hello_world"
                Id = $pluginId
                Script = $sampleScript
            }) | Out-Null
        }
    }
}

if ($targets.Count -eq 0) {
    if ($requested.Count -gt 0) {
        throw "No matching plugin package scripts found for: $($Plugin -join ', ')"
    }
    throw "No plugin package scripts found."
}

$failures = New-Object System.Collections.Generic.List[string]

foreach ($target in $targets) {
    Write-Host "Building plugin $($target.Id) from $($target.Name)..."
    & $target.Script -Configuration $Configuration -Platform $Platform -MsBuildPath $MsBuildPath
    if ($LASTEXITCODE -ne 0) {
        $message = "Plugin $($target.Id) failed with exit code $LASTEXITCODE"
        $failures.Add($message) | Out-Null
        Write-Host "[FAIL] $message"
        if (-not $ContinueOnError) {
            exit $LASTEXITCODE
        }
    }
}

if ($failures.Count -gt 0) {
    Write-Host "Plugin build completed with failures:"
    $failures | ForEach-Object { Write-Host " - $_" }
    exit 1
}

Write-Host "Plugin build completed successfully for $($targets.Count) plugin(s)."
