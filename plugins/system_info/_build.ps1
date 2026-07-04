$ErrorActionPreference = "Stop"
$msbuild = "E:\Visual Studio 2026\MSBuild\Current\Bin\MSBuild.exe"
$project = "C:\Users\Administrator\Desktop\WinLauncher\plugins\system_info\system_info.vcxproj"
& $msbuild $project /p:Configuration=Release /p:Platform=x64
exit $LASTEXITCODE
