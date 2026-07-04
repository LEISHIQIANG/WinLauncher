param(
    [string]$Configuration = "Release",
    [string]$Platform = "x64",
    [string]$MsBuildPath = "msbuild"
)

$ErrorActionPreference = "Stop"

$sampleRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$project = Join-Path $sampleRoot "hello_world.vcxproj"
$distRoot = Join-Path $sampleRoot "dist"
$stagingRoot = Join-Path $distRoot "example.hello_world"
$packagePath = Join-Path $distRoot "example.hello_world.wlplugin"
$dllPath = Join-Path $sampleRoot "$Platform\$Configuration\hello_world.dll"

& $MsBuildPath $project /p:Configuration=$Configuration /p:Platform=$Platform
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

if (Test-Path -LiteralPath $stagingRoot) {
    Remove-Item -LiteralPath $stagingRoot -Recurse -Force
}
New-Item -ItemType Directory -Path $stagingRoot | Out-Null

Copy-Item -LiteralPath (Join-Path $sampleRoot "plugin.json") -Destination (Join-Path $stagingRoot "plugin.json")
Copy-Item -LiteralPath $dllPath -Destination (Join-Path $stagingRoot "hello_world.dll")

if (Test-Path -LiteralPath $packagePath) {
    Remove-Item -LiteralPath $packagePath -Force
}
Compress-Archive -Path (Join-Path $stagingRoot "*") -DestinationPath $packagePath -Force
Write-Host "Created $packagePath"

