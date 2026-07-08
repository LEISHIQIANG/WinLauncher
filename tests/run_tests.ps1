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

    Write-Host "All tests passed."
}
finally {
    Pop-Location
}
