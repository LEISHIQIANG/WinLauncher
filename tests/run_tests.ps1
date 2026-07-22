param()

$ErrorActionPreference = "Stop"

$testRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = Split-Path -Parent $testRoot

Push-Location $repoRoot
try {
    Write-Host "== Project static tests =="
    & (Join-Path $testRoot "project_static_tests.ps1")

    Write-Host "== Script and CI static tests =="
    & (Join-Path $testRoot "script_static_tests.ps1")

    $nativeCandidates = @(
        (Join-Path $testRoot "native\x64\Release\WinLauncherNativeTests.exe"),
        (Join-Path $testRoot "native\x64\Debug\WinLauncherNativeTests.exe")
    )
    foreach ($candidate in $nativeCandidates) {
        if (Test-Path -LiteralPath $candidate) {
            Write-Host "== Native stability tests ($candidate) =="
            & $candidate
            if ($LASTEXITCODE -ne 0) {
                exit $LASTEXITCODE
            }
            break
        }
    }

    Write-Host "All tests passed."
}
finally {
    Pop-Location
}
