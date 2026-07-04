param([string]$Configuration="Release",[string]$Platform="x64",[string]$MsBuildPath="msbuild")
$ErrorActionPreference="Stop"
$root=Split-Path -Parent $MyInvocation.MyCommand.Path
$project=Join-Path $root "network_tools.vcxproj"
$dist=Join-Path $root "dist";$stage=Join-Path $dist "wl.network_tools";$pkg=Join-Path $dist "wl.network_tools.wlplugin"
& $MsBuildPath $project /p:Configuration=$Configuration /p:Platform=$Platform;if($LASTEXITCODE -ne 0){exit $LASTEXITCODE}
if(Test-Path $stage){Remove-Item $stage -Recurse -Force}
New-Item -ItemType Directory -Path $stage|Out-Null
Copy-Item (Join-Path $root "plugin.json") -Destination (Join-Path $stage "plugin.json")
Copy-Item (Join-Path $root "$Platform\$Configuration\network_tools.dll") -Destination (Join-Path $stage "network_tools.dll")
if(Test-Path $pkg){Remove-Item $pkg -Force}
Compress-Archive -Path (Join-Path $stage "*") -DestinationPath $pkg -Force
Write-Host "Created $pkg"
