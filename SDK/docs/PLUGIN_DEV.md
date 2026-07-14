# WinLauncher 插件开发者 README

WinLauncher 插件是原生 Windows DLL，通过稳定 C ABI 与主程序通信。插件需要编译为 x64 DLL，随 `plugin.json` 一起打包为 `.wlplugin`，或直接放入用户插件目录后加载。

运行时安装目录：

```text
%APPDATA%\WinLauncher\plugins\installed\<plugin_id>\
```

每个插件目录至少包含：

- `plugin.json`
- `plugin.json.entry` 指向的 DLL
- 可选 `data\` 私有数据目录

## ABI 约定

公共 ABI 头文件：

```text
WinLauncher/SDK/include/WinLauncher/WinLauncherPluginABI.h
```

C++ 便利封装：

```text
WinLauncher/SDK/include/WinLauncher/WinLauncherPluginCpp.h
```

插件必须导出三个函数：

```cpp
WL_EXPORT uint32_t WL_CALL WinLauncherPlugin_GetAbiVersion();
WL_EXPORT bool WL_CALL WinLauncherPlugin_Create(
    const WLHostApiV1* host,
    WLPluginInstanceV1** outInstance);
WL_EXPORT void WL_CALL WinLauncherPlugin_Destroy(WLPluginInstanceV1* instance);
```

ABI 规则：

- 当前 ABI 版本为 `WINLAUNCHER_PLUGIN_ABI_VERSION == 1`。
- 导出函数使用 `extern "C"` 和 `__stdcall`。
- 公共 ABI 不允许暴露 STL、C++ 虚表、异常对象、智能指针或主程序内部对象。
- 所有结构体第一个字段都是 `size`，新增字段只追加在结构体末尾。
- 插件调用扩展 Host API 前必须检查 `host->size` 是否覆盖对应函数指针。
- 插件分配的 `WLPluginInstanceV1` 由插件在 `WinLauncherPlugin_Destroy` 中释放。

## 插件实例

`WLPluginInstanceV1` 支持以下回调：

| 回调 | 用途 |
|---|---|
| `onLoad` | 插件加载后初始化资源，可注册运行时命令 |
| `onUnload` | 卸载前释放资源 |
| `executeCommand` | 执行普通插件命令 |
| `executeSlashCommand` | 执行 `/` 命令 |
| `onPopupShown` | 中键弹窗显示事件 |
| `onPopupHidden` | 中键弹窗隐藏事件 |
| `requestShutdown` | 可选：主程序请求插件取消仍在运行的异步工作 |
| `isShutdownComplete` | 可选：仅当返回 `true` 后主程序才会调用 `onUnload`、销毁实例并卸载 DLL |
| `search` | 动态普通搜索源，运行在后台搜索线程 |

建议：

- 回调中不要抛出 C++ 异常穿过 ABI 边界。
- 如果插件自行启动线程、异步 DNS/网络请求或其他会在卸载后继续执行的工作，请同时实现 `requestShutdown` 与 `isShutdownComplete`。收到停止请求后应取消工作；只有所有回调和插件代码都已返回时才报告完成。旧插件可以把两个字段设为 `nullptr`，主程序会按原有立即卸载规则处理。
- 搜索回调要短、确定、可取消，返回数量不要超过 `request->maxResults`。
- 插件 UI、文件、网络、进程等能力优先使用 Host API，不要绕过权限模型。

`WLSlashCommandContextV1.selectedFiles` 会提供中键弹窗打开时 WinLauncher 捕获到的资源管理器/桌面选中文件，多个路径以换行分隔。插件可以按命令能力选择只使用第一个文件，或遍历所有选中文件；没有选中文件时该字段为空字符串。

## Host API

`WLHostApiV1` 由主程序在 `WinLauncherPlugin_Create` 时传入。基础和扩展接口如下。

### 命令和日志

| 接口 | 权限 | 说明 |
|---|---|---|
| `registerCommand` | 无需权限 | 注册普通命令 |
| `registerSlashCommand` | 无需权限 | 注册 `/` 命令 |
| `log` | `log.write` 默认可用 | 写入主程序插件日志 |

### 应用信息和插件数据

| 接口 | 权限 | 说明 |
|---|---|---|
| `getDataDirectory` | 默认可用 | 获取插件私有 `data` 目录 |
| `getAppVersion` | `app.info` 默认可用 | 获取 WinLauncher 版本 |
| `getScreenInfo` | `app.info` 默认可用 | 获取主屏分辨率、DPI 和主题 |

### 剪贴板、打开 URL/文件、插件文件

| 接口 | 权限 | 说明 |
|---|---|---|
| `readClipboardText` | `clipboard.read` | 读取文本剪贴板 |
| `writeClipboardText` | `clipboard.write` | 写入文本剪贴板 |
| `openUrl` | `open.url` | 打开 `http://` 或 `https://` URL |
| `openFile` | `open.file` | 交给系统打开文件 |
| `readTextFile` | `file.read` | 读取插件 `data` 下的相对路径文本文件 |
| `writeTextFile` | `file.write` | 写入插件 `data` 下的相对路径文本文件 |

文件读写限制：

- 路径必须是相对路径。
- 禁止盘符、UNC、绝对路径和 `..`。
- 读写范围限制在插件私有 `data` 目录内。

### 插件配置

| 接口 | 权限 | 说明 |
|---|---|---|
| `getPluginConfig` | `plugin.config.read` 默认可用 | 读取插件私有配置 |
| `setPluginConfig` | `plugin.config.write` | 写入插件私有配置 |

配置文件存储在插件 `data\config.json`。设置项也可以声明在 `plugin.json.settings`，由设置页展示和编辑。

### 输入和文件选择

| 接口 | 权限 | 说明 |
|---|---|---|
| `showInputDialog` | `ui.input` 默认可用 | 文本输入框 |
| `showPasswordDialog` | `ui.input` 默认可用 | 密码输入框 |
| `showChooseDialog` | `ui.input` 默认可用 | 从换行分隔的选项中选择 |
| `showConfirmDialog` | `ui.input` 默认可用 | 确认/取消 |
| `showFilePicker` | `ui.filepick` 默认可用 | 文件或文件夹选择器，支持多选 |

注意：返回字符串使用 `WLStringResultV1`。输入类接口会实际弹窗，插件应直接提供足够缓冲区，避免把第一次长度探测当成交互调用。

### 通知、加载、进度和结果回显

| 接口 | 权限 | 说明 |
|---|---|---|
| `showNotificationToaster` | `ui.notify` 默认可用 | 非阻塞提示 |
| `showMessageBox` | `ui.notify` 默认可用 | 模态消息框 |
| `showBalloonTip` | `ui.notify` 默认可用 | 托盘/通知后备提示 |
| `showLoadingDialog` | `ui.notify` 默认可用 | 创建加载状态句柄 |
| `updateLoadingMessage` | `ui.notify` 默认可用 | 更新加载提示 |
| `hideLoadingDialog` | `ui.notify` 默认可用 | 关闭加载状态 |
| `showProgressDialog` | `ui.notify` 默认可用 | 创建进度状态句柄 |
| `updateProgress` | `ui.notify` 默认可用 | 更新进度 |
| `hideProgressDialog` | `ui.notify` 默认可用 | 关闭进度状态 |
| `isDialogCancelled` | `ui.notify` 默认可用 | 查询用户是否取消 |
| `showResultInPanel` | `ui.notify` 默认可用 | 显示命令结果 |

当前实现提供轻量后备显示，后续主程序可升级为专用自绘面板而不改变 ABI。

### 网络和进程

| 接口 | 权限 | 说明 |
|---|---|---|
| `httpRequest` | `network.request` | 发起 HTTP/HTTPS 请求 |
| `runProcess` | `process.run` | 在插件数据目录范围内启动进程，可捕获输出 |

网络限制：

- 仅支持 `http` 和 `https`。
- 方法限制为常见 HTTP 方法。
- 单次响应有大小保护。

进程限制：

- `workingDir` 为空时使用插件 `data` 目录。
- 非空 `workingDir` 必须位于插件 `data` 目录下。
- 使用 `timeoutMs` 限制长时间运行的进程。
- 不要把用户输入直接拼接成命令行；需要执行前先确认或校验。

## 字符串返回协议

返回字符串统一使用 `WLStringResultV1`：

```cpp
struct WLStringResultV1
{
    uint32_t size;
    wchar_t* buffer;
    uint32_t bufferLength;
    uint32_t requiredLength;
};
```

普通只读接口可以两阶段调用：

1. `buffer = nullptr` 获取 `requiredLength`
2. 分配缓冲区后再次调用

会弹窗的交互接口建议一次性传入足够缓冲区，例如 4096 或 32768 个 `wchar_t`。

## Manifest

最小 `plugin.json`：

```json
{
  "id": "wl.example",
  "name": "Example Plugin",
  "version": "1.0.0",
  "description": "Example plugin.",
  "entry": "example.dll",
  "abiVersion": 1,
  "permissions": ["app.info", "log.write"],
  "category": "Tools",
  "commands": [],
  "slashCommands": [
    {
      "id": "wl.example.hello",
      "command": "hello",
      "title": "Hello",
      "description": "Say hello.",
      "usage": "/hello [name]",
      "keywords": ["sample"],
      "aliases": ["hi"]
    }
  ],
  "settings": [
    {
      "key": "greeting",
      "type": "string",
      "title": "Greeting",
      "default": "Hello"
    }
  ]
}
```

字段规则：

- `id` 必须全局唯一，建议使用 `wl.<name>` 或反向域名。
- 命令 ID 必须以 `<plugin_id>.` 开头。
- `/` 命令的 `command` 不包含 `/`，只允许小写字母、数字和连字符。
- `/` 命令的 `icon` 可使用插件包内的安全相对 `.ico` 路径，例如 `icons/my-command.ico`。
- `entry` 必须是安全相对 DLL 路径，不能包含 `..`、盘符、UNC 或绝对路径。
- `settings` 支持 `string`、`integer`、`boolean`。

## 权限清单

| 权限 | 默认 | 说明 |
|---|---:|---|
| `app.info` | 是 | 应用版本、屏幕信息 |
| `log.write` | 是 | 插件日志 |
| `plugin.config.read` | 是 | 读插件配置 |
| `plugin.config.write` | 否 | 写插件配置 |
| `clipboard.read` | 否 | 读剪贴板 |
| `clipboard.write` | 否 | 写剪贴板 |
| `open.url` | 否 | 打开 URL |
| `open.file` | 否 | 打开文件 |
| `file.read` | 否 | 读插件私有文件 |
| `file.write` | 否 | 写插件私有文件 |
| `ui.input` | 是 | 输入、密码、选择、确认 |
| `ui.filepick` | 是 | 文件/文件夹选择 |
| `ui.notify` | 是 | 通知、消息框、进度、结果回显 |
| `network.request` | 否 | 网络请求 |
| `process.run` | 否 | 启动进程 |

开发规范：

- 只声明实际使用的权限。
- 高风险能力要给出清晰的用户可见命令说明。
- 网络和进程能力不要在搜索回调中执行。
- 插件不得修改主程序配置、安装目录或其他插件目录。

## 搜索和命令

WinLauncher 有两套搜索入口：

- 普通搜索：查询不以 `/` 开头，匹配快捷方式、普通插件命令和动态搜索结果。
- Slash 搜索：查询以 `/` 开头，只匹配内置和插件 `/` 命令。

普通命令：

- 在 `plugin.json.commands` 声明，或运行时调用 `registerCommand`。
- 执行入口是 `executeCommand`。

Slash 命令：

- 在 `plugin.json.slashCommands` 声明，或运行时调用 `registerSlashCommand`。
- 执行入口是 `executeSlashCommand`。
- `WLSlashCommandContextV1.args` 是命令名后的参数文本。
- `WLSlashCommandContextV1.selectedFiles` 是 WinLauncher 捕获到的选中文件，多个路径以换行分隔；只支持单文件的命令可以取第一项，支持多文件的命令可以遍历全部。
- 插件 `/` 命令的图标由 `plugin.json` 的 `icon` 字段独立配置，图标文件应随插件一起放在插件目录内并打包。

动态搜索：

- 设置 `WLPluginInstanceV1::search`。
- 通过 `WLSearchResponseV1::addResult` 返回结果。
- 结果的 `commandId` 必须属于当前插件命名空间。

## 构建

插件项目使用 Visual Studio/MSBuild：

```powershell
& "E:\Visual Studio 2026\MSBuild\Current\Bin\MSBuild.exe" plugin.vcxproj /p:Configuration=Release /p:Platform=x64
```

仓库提供模板：

```text
SDK/templates/visual_studio_plugin
```

仓库提供示例：

```text
SDK/samples/hello_world
```

如果插件项目在仓库外，配置 SDK include 路径：

```powershell
msbuild WinLauncherPlugin.vcxproj /p:Configuration=Release /p:Platform=x64 /p:WinLauncherSdkInclude=C:\path\to\WinLauncher\WinLauncher\SDK\include
```

## 打包

`.wlplugin` 是 ZIP 包，根目录必须包含 `plugin.json` 和 DLL：

```text
wl.example.wlplugin
├── plugin.json
└── example.dll
```

内置插件目录均提供 `package.ps1`：

```powershell
.\package.ps1 -MsBuildPath "E:\Visual Studio 2026\MSBuild\Current\Bin\MSBuild.exe"
```

从仓库根目录做发布候选验证时，可以统一构建随附插件和 SDK 示例：

```powershell
.\scripts\build_plugins.ps1 -Configuration Release -Platform x64 -MsBuildPath "E:\Visual Studio 2026\MSBuild\Current\Bin\MSBuild.exe" -IncludeSamples
```

打包安全规则：

- 包内不能有绝对路径、UNC 路径或 `..`。
- 包内文件数量不能超过主程序限制。
- 解压后总体积不能超过主程序限制。
- 不要把中间文件、PDB、OBJ、源码或用户数据放进 `.wlplugin`。

## 当前内置插件

| 插件 | 说明 | 已接入 Host API |
|---|---|---|
| `wl.text_tools` | Base64、UUID、哈希、大小写、计数、反转 | 剪贴板、输入框 |
| `wl.file_tools` | 文件信息、文件/文件夹哈希 | 选中文件上下文 |
| `wl.time_tools` | 时间戳、倒计时、世界时钟 | 剪贴板、输入框 |
| `wl.network_tools` | Ping、DNS、IP、端口检查 | 输入框、网络权限 |
| `wl.system_info` | 系统信息、屏幕信息 | 应用信息 |

## 发布前检查

提交插件变更前至少检查：

- `plugin.json` 可被主程序校验通过。
- DLL 导出三个必需函数。
- 命令 ID 全部在插件命名空间内。
- 只声明实际使用的权限。
- Release x64 DLL 已重新编译。
- `.wlplugin` 已重新打包，包内只有运行所需文件。
- `RELEASE_NOTES.md` 已同步实际变化；如本次涉及 SDK 接口，请同步更新本开发文档和示例。
