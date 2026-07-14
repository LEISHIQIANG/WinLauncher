param(
    [string]$Configuration = "Release",
    [string]$Platform = "x64",
    [string]$MsBuildPath = "",
    [switch]$SkipBuild,
    [switch]$BuildPlugins
)

$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = Split-Path -Parent $scriptDir

function Resolve-MSBuild {
    param([string]$ExplicitPath)

    if (-not [string]::IsNullOrWhiteSpace($ExplicitPath)) {
        if (-not (Test-Path -LiteralPath $ExplicitPath)) {
            throw "MSBuildPath does not exist: $ExplicitPath"
        }
        return (Resolve-Path -LiteralPath $ExplicitPath).Path
    }

    $repoDefault = "E:\Visual Studio 2026\MSBuild\Current\Bin\MSBuild.exe"
    if (Test-Path -LiteralPath $repoDefault) {
        return $repoDefault
    }

    $vswhereCandidates = @(
        "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe",
        "${env:ProgramFiles}\Microsoft Visual Studio\Installer\vswhere.exe"
    )
    foreach ($candidate in $vswhereCandidates) {
        if (Test-Path -LiteralPath $candidate) {
            $found = & $candidate -latest -requires Microsoft.Component.MSBuild -find "MSBuild\**\Bin\MSBuild.exe" | Select-Object -First 1
            if ($found -and (Test-Path -LiteralPath $found)) {
                return $found
            }
        }
    }

    $command = Get-Command msbuild.exe -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }

    throw "Unable to locate MSBuild.exe. Pass -MsBuildPath explicitly."
}

Push-Location $repoRoot
try {
    Write-Host "== Maintenance check =="
    & (Join-Path $repoRoot "scripts\maintenance_check.ps1")

    Write-Host "== Tests =="
    & (Join-Path $repoRoot "tests\run_tests.ps1")

    Write-Host "== Scenario regression =="
    & (Join-Path $repoRoot "tests\scenario_regression.ps1")

    $msbuild = $null
    if (-not $SkipBuild -or $BuildPlugins) {
        $msbuild = Resolve-MSBuild -ExplicitPath $MsBuildPath
        Write-Host "Using MSBuild: $msbuild"
    }

    if (-not $SkipBuild) {
        Write-Host "== Build WinLauncher $Configuration|$Platform =="
        & $msbuild (Join-Path $repoRoot "WinLauncher.sln") /p:Configuration=$Configuration /p:Platform=$Platform /m:1
        if ($LASTEXITCODE -ne 0) {
            exit $LASTEXITCODE
        }

        $exePath = Join-Path $repoRoot "$Platform\$Configuration\WinLauncher.exe"
        if (-not (Test-Path -LiteralPath $exePath)) {
            throw "Build succeeded but output executable is missing: $exePath"
        }

        $versionHeader = Get-Content -LiteralPath (Join-Path $repoRoot "WinLauncher\version.h") -Raw
        $parts = @{}
        foreach ($match in [regex]::Matches($versionHeader, '(?m)^\s*#define\s+WINLAUNCHER_VERSION_(MAJOR|MINOR|PATCH|BUILD)\s+(\d+)\s*$')) {
            $parts[$match.Groups[1].Value] = $match.Groups[2].Value
        }
        $expectedVersion = "$($parts.MAJOR).$($parts.MINOR).$($parts.PATCH).$($parts.BUILD)"
        $actualVersion = (Get-Item -LiteralPath $exePath).VersionInfo.ProductVersion
        if ($actualVersion -ne $expectedVersion) {
            throw "Executable version mismatch. Expected $expectedVersion, got $actualVersion"
        }
        Write-Host "Executable version verified: $actualVersion"

        $nativeProject = Join-Path $repoRoot "tests\native\WinLauncherNativeTests.vcxproj"
        Write-Host "== Build native stability tests $Configuration|$Platform =="
        & $msbuild $nativeProject /p:Configuration=$Configuration /p:Platform=$Platform /m:1
        if ($LASTEXITCODE -ne 0) {
            exit $LASTEXITCODE
        }

        $nativeTests = Join-Path $repoRoot "tests\native\$Platform\$Configuration\WinLauncherNativeTests.exe"
        if (-not (Test-Path -LiteralPath $nativeTests)) {
            throw "Native test executable is missing: $nativeTests"
        }
        Write-Host "== Native async/callback/crash tests =="
        & $nativeTests
        if ($LASTEXITCODE -ne 0) {
            exit $LASTEXITCODE
        }
    }

    if ($BuildPlugins) {
        Write-Host "== Build bundled plugins and SDK sample =="
        & (Join-Path $repoRoot "scripts\build_plugins.ps1") -Configuration $Configuration -Platform $Platform -MsBuildPath $msbuild -IncludeSamples
    }

    Write-Host "CI check passed."
}
finally {
    Pop-Location
}
