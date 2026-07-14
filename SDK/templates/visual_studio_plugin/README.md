# WinLauncher Visual Studio Plugin Template

This template creates a native C++ DLL plugin project for WinLauncher.

## Use In This Repository

1. Copy this folder to a working plugin project folder.
2. Open `WinLauncherPlugin.vcxproj` in Visual Studio.
3. Set the `WinLauncherSdkInclude` MSBuild property if the project is outside this repository.
4. Edit `plugin.json` so `id`, command IDs, and slash command IDs use your plugin namespace.
5. Build `x64|Release` and copy the DLL plus `plugin.json` into:

`%APPDATA%\WinLauncher\plugins\installed\<plugin_id>`

The project defaults to `x64` and C++17. The public ABI header path can be supplied with:

`/p:WinLauncherSdkInclude=C:\path\to\WinLauncher\WinLauncher\SDK\include`

If the plugin starts asynchronous work, implement the optional `requestShutdown` and
`isShutdownComplete` instance callbacks. The host retains the DLL until completion;
simple synchronous plugins should explicitly set both callbacks to `nullptr`.
