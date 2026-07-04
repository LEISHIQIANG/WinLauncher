param(
    [string]$Configuration = "Release",
    [string]$Platform = "x64",
    [string]$MsBuildPath = "msbuild"
)

$ErrorActionPreference = "Stop"
$pluginRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$project = Join-Path $pluginRoot "text_tools.vcxproj"
$distRoot = Join-Path $pluginRoot "dist"
$stagingRoot = Join-Path $distRoot "wl.text_tools"
$packagePath = Join-Path $distRoot "wl.text_tools.wlplugin"
$dllPath = Join-Path $pluginRoot "$Platform\$Configuration\text_tools.dll"

Write-Host "Building text_tools..."
& $MsBuildPath $project /p:Configuration=$Configuration /p:Platform=$Platform
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

if (Test-Path -LiteralPath $stagingRoot) { Remove-Item -LiteralPath $stagingRoot -Recurse -Force }
New-Item -ItemType Directory -Path $stagingRoot | Out-Null

Copy-Item -LiteralPath (Join-Path $pluginRoot "plugin.json") -Destination (Join-Path $stagingRoot "plugin.json")
Copy-Item -LiteralPath $dllPath -Destination (Join-Path $stagingRoot "text_tools.dll")

if (Test-Path -LiteralPath $packagePath) { Remove-Item -LiteralPath $packagePath -Force }
Compress-Archive -Path (Join-Path $stagingRoot "*") -DestinationPath $packagePath -Force
Write-Host "Created $packagePath"
