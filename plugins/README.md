# WinLauncher 内置插件维护说明

本目录存放 WinLauncher 当前随仓库维护的原生 DLL 插件源码。每个插件都是独立 Visual Studio DLL 项目，并通过 `package.ps1` 生成 `.wlplugin` 安装包。

## 插件列表

| 目录 | 插件 ID | 主要命令 |
|---|---|---|
| `text_tools` | `wl.text_tools` | `/base64`, `/uuid`, `/hash`, `/case`, `/count`, `/reverse` |
| `file_tools` | `wl.file_tools` | `/fileinfo`, `/filehash` |
| `time_tools` | `wl.time_tools` | `/timestamp`, `/countdown`, `/worldclock` |
| `network_tools` | `wl.network_tools` | `/ping`, `/dns`, `/ip`, `/port` |
| `system_info` | `wl.system_info` | `/sysinfo` |

## 构建全部插件

```powershell
.\scripts\build_plugins.ps1 -Configuration Release -Platform x64 -MsBuildPath "E:\Visual Studio 2026\MSBuild\Current\Bin\MSBuild.exe"
```

每个脚本会：

- 编译 Release x64 DLL
- 复制 `plugin.json`
- 复制 `icons\` 图标目录（如果存在）
- 复制 DLL 到 `dist\<plugin_id>\`
- 生成 `dist\<plugin_id>.wlplugin`

## 维护要求

- 修改插件源码后必须重新编译并重新打包对应 `.wlplugin`。
- 修改 `plugin.json` 后必须同步 `dist\<plugin_id>\plugin.json`，或直接重新运行 `package.ps1`。
- 权限只声明实际使用的 Host API。
- 命令 ID 必须使用 `<plugin_id>.<command>` 命名空间。
- 缺参数的用户交互优先使用 Host API，例如 `showInputDialog` 和 `showFilePicker`。
- 网络、进程、文件、剪贴板等能力必须走 Host API 或在 manifest 中明确声明权限。
- 打包脚本应先生成临时 `.zip`，再重命名为 `.wlplugin`，避免 PowerShell 压缩命令对自定义扩展名的兼容性差异。
- 生成的 `.wlplugin` 只能包含运行所需文件，例如 `plugin.json`、DLL 和图标；不得包含源码、工程文件、PDB、OBJ、日志或脚本。
- 项目级插件 API 和开发规范见 `SDK/docs/PLUGIN_DEV.md`。

## 维护检查

在修改插件源码、manifest、SDK、命令执行或发布包前后，运行：

```powershell
.\scripts\maintenance_check.ps1
```

该脚本会检查当前版本发布说明、核心维护文档、内置插件 manifest，以及已有 `.wlplugin` 包是否只包含运行时文件。

## 安装验证

可在 WinLauncher 设置页安装生成的 `.wlplugin`，也可手动解压到：

```text
%APPDATA%\WinLauncher\plugins\installed\<plugin_id>\
```

安装后使用设置页刷新插件列表，确认插件状态为已加载，再在中键弹窗中输入 `/` 检查命令是否出现。
