# Hello World Plugin Sample

This sample builds a native WinLauncher plugin DLL and packages it as a `.wlplugin` archive.

Build the DLL:

```powershell
msbuild hello_world.vcxproj /p:Configuration=Release /p:Platform=x64
```

Build and package:

```powershell
.\package.ps1
```

The package is written to `dist\example.hello_world.wlplugin`. Install it from the WinLauncher plugin settings page.

