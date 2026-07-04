@echo off
call "E:\Visual Studio 2026\VC\Auxiliary\Build\vcvars64.bat"
cd /d "C:\Users\Administrator\Desktop\WinLauncher\plugins\system_info"
"E:\Visual Studio 2026\MSBuild\Current\Bin\MSBuild.exe" system_info.vcxproj /p:Configuration=Release /p:Platform=x64
exit /b %ERRORLEVEL%
