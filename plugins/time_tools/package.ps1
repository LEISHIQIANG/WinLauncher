param([string]$Configuration="Release",[string]$Platform="x64",[string]$MsBuildPath="msbuild")
$ErrorActionPreference="Stop"
$root=Split-Path -Parent $MyInvocation.MyCommand.Path
$project=Join-Path $root "time_tools.vcxproj"
$dist=Join-Path $root "dist";$stage=Join-Path $dist "wl.time_tools";$pkg=Join-Path $dist "wl.time_tools.wlplugin"
& $MsBuildPath $project /p:Configuration=$Configuration /p:Platform=$Platform;if($LASTEXITCODE -ne 0){exit $LASTEXITCODE}
if(Test-Path $stage){Remove-Item $stage -Recurse -Force}
New-Item -ItemType Directory -Path $stage|Out-Null
Copy-Item (Join-Path $root "plugin.json") -Destination (Join-Path $stage "plugin.json")
Copy-Item (Join-Path $root "$Platform\$Configuration\time_tools.dll") -Destination (Join-Path $stage "time_tools.dll")
if(Test-Path (Join-Path $root "icons")){Copy-Item (Join-Path $root "icons") -Destination (Join-Path $stage "icons") -Recurse}
if(Test-Path $pkg){Remove-Item $pkg -Force}
$zipPkg="$pkg.zip"
if(Test-Path $zipPkg){Remove-Item $zipPkg -Force}
Compress-Archive -Path (Join-Path $stage "*") -DestinationPath $zipPkg -Force
Move-Item -Path $zipPkg -Destination $pkg -Force
Write-Host "Created $pkg"
