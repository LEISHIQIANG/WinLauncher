# WinLauncher 插件系统优化计划

> 基于 QuickLauncher V1.6.3.7 插件体系经验，面向 WinLauncher 当前 C++17 / Win32 / Direct2D 自绘架构重新整理的可实施方案。插件以 DLL 形式编译，放置到 `%APPDATA%\WinLauncher\plugins\installed` 下对应目录后由主程序加载。

---

## 0. 文档定位

本文档不是一次性大重构清单，而是插件系统的产品、架构、接口、安全和实施路线图。它用于回答四个问题：

- WinLauncher 插件系统要先交付什么，哪些能力后置。
- 现有代码应从哪些稳定接入点扩展，避免破坏当前启动、弹窗和配置流程。
- 插件 API、权限、安装路径、状态存储和 UI 管理如何设计才便于长期维护。
- 每个阶段完成后如何验收，避免只完成类名拆分但没有可用闭环。

使用本文档时应把它当作后续实现的约束文档，而不是灵感清单：

- 每一轮开发必须先选择一个明确阶段（P0/P1/P2/P3/P4），不得跨阶段顺手实现后置能力。
- 每一轮开发必须写明本轮不做什么，尤其是隔离进程、在线仓库、插件 UI 扩展等容易扩大范围的能力。
- 每一轮开发必须从当前代码基线重新核对调用点，本文档中的行号只作为 2026-07-03 前后的定位参考。
- 每一轮开发必须保留 WinLauncher 的自绘窗口体系，不得用系统原生插件窗口替代现有 `GlassWindow` / `ConfigWindow` / `SettingsPage` 路线。
- 每一轮开发完成后必须能用用户可见行为验收，而不仅是类、文件或接口已经存在。
- 代码实施时还必须遵守仓库级要求：同步更新 `RELEASE_NOTES.md` 当前版本段落；本文档本次规划完善只修改计划书本身。

---

## 1. 总体目标

为 WinLauncher 建立一个可逐步演进的原生 DLL 插件系统，最终支持：

- 插件命令：第三方插件可注册可搜索、可执行的命令项。
- 插件搜索源：插件可在搜索模式中返回实时结果。
- 插件 / 命令：插件可注册斜杠（`/`）命令，在中键弹窗搜索框中键入 `/` 触发特殊命令模式；`/` 命令与普通搜索隔离，仅当输入以 `/` 开头时匹配，用于执行高级指令或实用功能。
- 生命周期事件：插件可接收弹窗显示/隐藏、快捷方式启动、配置变化等事件。
- Host API：插件可在受权限控制的前提下访问剪贴板、文件、网络、进程启动、日志、配置和应用信息。
- 插件管理：用户可在设置页中查看、启用、禁用、卸载和诊断插件。
- SDK 和生态：提供稳定头文件、模板、示例插件和开发文档。
- 隔离运行：未验证插件可在工作进程中运行，降低崩溃和恶意行为对主进程的影响。

### 1.1 设计原则

- 先做最小可运行闭环，再做生态能力。第一阶段必须能完成“编译 DLL → 放置到插件目录 → 发现插件 → 加载 DLL → 注册命令 → 搜索展示 → 执行命令 → 卸载清理”。
- DLL 是插件的唯一二进制分发形式。插件必须编译为 DLL，由主程序通过 `LoadLibrary` 加载；不支持脚本、解释型语言或独立 EXE 作为插件入口。
- 不破坏 WinLauncher 的自绘 UI 风格。插件管理和配置入口优先用现有 `SettingsPage` / `ConfigWindow` 自绘控件承载，早期不嵌入任意第三方 HWND。
- 运行时数据放用户目录，开发材料放仓库。插件安装、状态、日志、数据目录应位于 `%APPDATA%\WinLauncher` 下；SDK、示例和模板才放在源码仓库。
- DLL 边界要 ABI 稳定。公共 DLL 导出层不直接暴露 `std::wstring`、`std::vector`、C++ 虚表或 STL 容器；C++ 便利封装只作为 SDK 头文件包装层。
- `/` 命令与普通搜索维护两条独立匹配路径。`/` 命令只能通过 `/` 前缀触发匹配，普通搜索不能命中 `/` 命令，`/` 命令也不能干扰普通搜索结果排序。
- 主进程稳定性优先于插件能力。插件功能失败时可以降级、禁用、隔离或隐藏，但不能影响启动、弹窗显示、设置页打开、本地快捷方式执行和退出流程。
- 状态可解释。任何插件不可用状态都必须能回答“为什么不可用、上次什么时候失败、用户能做什么”。
- 用户授权优先。涉及文件、网络、进程、剪贴板、后台运行等能力时，即使早期只做进程内插件，也要从 UI 和 manifest 上保留权限表达。

### 1.2 最终成功标准

插件系统完成后，应满足以下用户侧和开发者侧标准：

| 角色 | 成功标准 |
|------|----------|
| 普通用户 | 能安装、启用、禁用、卸载插件；插件出错时主程序继续可用；插件权限和错误原因可见 |
| 插件开发者 | 能基于稳定 ABI、SDK 头文件、模板和示例插件完成开发；插件可声明命令、搜索源、权限和配置 |
| 项目维护者 | 能通过阶段验收清单判断改动是否完成；能定位插件加载、搜索、执行、卸载和隔离问题 |
| 发布负责人 | 能确认发布包不夹带本地开发插件、不破坏无插件启动路径、版本和 release notes 一致 |

### 1.3 架构红线

以下情况应视为偏离本计划，需要回退或重新设计：

- 插件命令被伪装成普通 `RendShortcutInfo`，导致来源、权限、执行路径不可区分。
- 第三方 DLL 直接拿到主程序内部对象指针、UI 控件指针或配置仓库对象。
- 公共 ABI 暴露 STL、异常、C++ 虚表、跨模块分配释放责任不清的对象。
- 插件安装包可以写入 `%APPDATA%\WinLauncher` 之外的路径。
- 插件搜索在 UI 线程同步等待不可控时长。
- 插件管理页使用原生系统窗口破坏 WinLauncher 自绘体验。
- 插件错误只能在调试器中看到，普通用户和维护者无法从日志或设置页判断原因。

---

## 2. 当前代码基线

当前 WinLauncher 已经有插件雏形，但还不是可用插件系统：

| 现状 | 影响 |
|------|------|
| `WinLauncher/App/PluginHost.h`（69 行，全部内联，无 .cpp）只有内存中的 `IPlugin` 列表和批量事件通知；缺少 `LoadPlugin` / `UnloadPlugin` 等单插件操作方法，仅提供 `UnloadAll` | 没有插件发现、清单解析、DLL 加载、权限校验、状态持久化；缺少单插件粒度的生命周期控制 |
| `PluginHost.cpp` 不存在，逻辑在头文件内 | 后续需要拆成可测试的管理器和运行时模块 |
| `AppContext`（`App/AppContext.h:15`）持有 `std::unique_ptr<PluginHost>` | 可作为替换为 `PluginManager` 的稳定入口 |
| `PopupViewModel`（`ViewModel/PopupViewModel.h:86-155`）已调用 `NotifyPopupShown/Hidden/ShortcutLaunched` | 事件接入点已存在，但事件没有载荷模型（当前仅传递 `ShortcutInfo` 给 `NotifyShortcutLaunched`） |
| `PopupWindow::UpdateSearch()`（`PopupWindow.cpp:1101`）直接遍历 `m_pages` 中的本地快捷方式填充 `m_searchResults`；搜索在文本变更时触发（`PopupWindow.cpp:2772,2837`） | 插件搜索需要在这里或其上层引入统一搜索结果模型 |
| 快捷方式执行集中在 `PopupWindow::ExecuteShortcut()`（`PopupWindow.cpp:4006`）和 `Application::LaunchShortcutById`（`Application.cpp:512`，通过 `WM_APP` 消息分发，也用于 `BatchLaunchService`） | 插件命令执行应避免伪装成普通快捷方式，建议使用独立 command kind |
| `ConfigPath`（`Services/ConfigPath.h`）已统一 `%APPDATA%\WinLauncher\config`，提供 `GetUserConfigDirectory()` / `PrepareUserConfigDirectory()` | 插件运行时目录应沿用同一用户数据根目录策略；`ConfigPath` 可直接扩展 `plugins` 子目录解析 |
| `EventBus`（`App/EventBus.h:23`）目前只支持 `std::function<void()>` 无参事件 | P0 可继续用显式通知；P1 再扩展为带载荷事件 |
| 已有轻量 JSON 解析辅助 `JsonImportHelper`（`Services/JsonImportHelper.h`，命名空间 `JsonImport`，递归下降解析器，无外部依赖，支持 UTF-8 BOM） | Manifest 可先复用或提取更严格的 JSON 读取层，避免新增重依赖 |
| `TrayMenuWindow.cpp:286` 在退出应用时直接调用 `s_ctx->pluginHost->UnloadAll()`（与 `Application.cpp:325-326` 构成两处卸载入口） | 替换为 `PluginManager` 时需同时修改两个调用点，确保退出和重启路径一致 |

### 2.1 基线判断

当前代码已经有“事件通知占位”，但缺少“插件系统闭环”。后续规划应按以下判断推进：

| 维度 | 当前成熟度 | 规划判断 |
|------|------------|----------|
| 生命周期 | 有 `PluginHost::UnloadAll()` 和若干 `Notify*`，但没有发现、加载、禁用、状态和错误模型 | P0 不应直接做 DLL 生态，应先替换为可管理的 `PluginManager` |
| 搜索执行 | 本地快捷方式搜索和执行路径集中在 `PopupWindow` | P1 要优先抽出统一搜索结果模型，否则插件命令会污染普通快捷方式 |
| 配置与路径 | `%APPDATA%\WinLauncher\config` 已统一 | 插件目录应扩展为 `%APPDATA%\WinLauncher\plugins`，不要混入 config 根目录或安装目录 |
| UI 承载 | `ConfigWindow` / `SettingsPage` 已经是项目主要设置入口 | 插件管理必须复用自绘设置页；P0 只做列表和状态，不做复杂插件配置 UI |
| JSON 能力 | 已有轻量 JSON 解析器 | P0 可复用或抽取，但 manifest 校验要比导入配置更严格 |
| 安全边界 | 进程内 DLL 无强隔离 | P1 只面向受信任插件；社区插件默认目标应等到 P3 worker |

### 2.2 规划假设

- 当前 WinLauncher 版本以 `WinLauncher/version.h` 为准，本文档核对时为 `0.5.1.7`。
- 当前项目目标是 C++17 / Win32 / Direct2D 自绘应用，不引入 Qt、.NET、Electron 或脚本运行时作为插件主路径。
- 当前用户配置和日志路径以 `ConfigPath` 统一管理，插件路径应由同一策略派生，而不是新增分散的路径拼接。
- 当前插件系统优先服务本地高信任扩展和后续官方插件；面向陌生第三方插件的安全承诺必须等隔离 worker 完成。
- 后续如果 WinLauncher 的主窗口、设置页、搜索模型发生大改，必须先更新本章基线，再继续执行阶段计划。

---

## 3. 范围与非目标

### 3.1 第一轮应纳入范围

- 插件清单格式和严格校验。
- 插件目录扫描和状态持久化。
- 插件管理的最小设置页入口：列表、启用/禁用、隔离状态、错误摘要。
- 插件目录和插件状态初始化流程。
- 错误记录、用户可见状态和重启后状态恢复。
- `PluginManager` 替换当前 `PluginHost` 的主入口，但可以先不加载第三方 DLL。

### 3.2 后置范围

- 插件 DLL 编译、放置和加载流程。
- 受信任进程内插件的命令注册、搜索展示、执行和卸载。
- 基础 Host API：应用版本、日志、插件数据目录、剪贴板读写、打开 URL/文件。
- SDK 头文件、Visual Studio 模板和一个示例插件 DLL。
- 未验证插件的隔离工作进程。
- 插件商店、在线更新和签名信任链。
- 自定义渲染层和复杂配置页扩展。
- 插件间依赖解析。
- 长驻后台插件和定时任务。

### 3.3 明确非目标

- 不把插件目录放到源码仓库或程序安装目录作为用户运行时安装位置。
- 不让插件直接修改主配置文件；插件只能写入自己的数据目录和配置命名空间。
- 不在早期允许插件任意嵌入原生窗口破坏 WinLauncher 自绘体验。
- 不承诺进程内第三方 DLL 的强安全隔离。

### 3.4 范围分层

为避免第一轮过大，插件系统按四层分开验收：

| 层级 | 内容 | 最早阶段 | 完成信号 |
|------|------|----------|----------|
| 管理层 | 目录、manifest、状态、错误、设置页列表 | P0 | 没有任何插件 DLL 时也能展示“无插件/无错误”的稳定状态 |
| 能力层 | ABI、Host API、命令注册、搜索合并、命令执行 | P1 | `hello_world` 插件能完整注册、搜索、执行、卸载 |
| 分发层 | `.wlplugin` 安装、更新、卸载、回滚、SDK 和示例 | P2 | 用户无需手工复制目录即可安装示例插件 |
| 隔离层 | worker、IPC、超时、崩溃恢复、隔离策略 | P3 | 插件崩溃不影响主进程，状态可见且可恢复 |

第一轮只完成管理层。若第一轮直接接入 DLL 加载，则必须把该轮拆成“P0 管理层”和“P1 进程内插件 MVP”两个独立提交或至少两个明确验收批次。

---

## 4. 目标架构

```
WinLauncher 主进程
    |
    +-- PluginManager
    |     +-- 扫描、加载、启用、禁用、卸载、隔离
    |     +-- 维护插件状态和错误计数
    |     +-- 对 PopupWindow / SettingsPage 提供查询与执行接口
    |
    +-- PluginCatalog
    |     +-- 扫描 %APPDATA%\WinLauncher\plugins\installed
    |     +-- 读取 plugin.json
    |
    +-- PluginManifest
    |     +-- schema 校验、版本校验、权限校验、路径校验
    |
    +-- PluginRuntime
    |     +-- 进程内 LoadLibrary 运行时
    |     +-- 后续扩展 IsolatedRuntime
    |
    +-- PluginCommandRegistry
    |     +-- 插件命令注册表
    |     +-- 搜索结果合并、排序、去重
    |
    +-- PluginHostAPI
          +-- 每插件独立实例
          +-- 权限检查、路径约束、日志、配置和系统能力代理

可选隔离工作进程
    |
    +-- WinLauncherPluginWorker.exe
          +-- 加载插件 DLL
          +-- 命名管道 IPC
          +-- RemoteHostAPI 代理
```

### 4.1 目录布局

运行时目录（基于现有 `ConfigPath::GetUserConfigDirectory()` → `%APPDATA%\WinLauncher\config` 扩展）：

```
%APPDATA%\WinLauncher\
├── config\
│   ├── launcher_config.ini
│   └── winlauncher.log
└── plugins\
    ├── installed\
    │   └── <plugin_id>\
    │       ├── plugin.json
    │       ├── <plugin>.dll
    │       ├── icon.png
    │       └── data\
    ├── packages\
    ├── state\
    │   ├── plugins_state.json
    │   └── plugin_errors.jsonl
    └── cache\
```

源码和 SDK 目录：

```
WinLauncher/
├── WinLauncher/
│   ├── App/
│   │   ├── PluginManager.h/.cpp
│   │   ├── PluginCatalog.h/.cpp
│   │   ├── PluginManifest.h/.cpp
│   │   ├── PluginRuntime.h/.cpp
│   │   ├── PluginCommandRegistry.h/.cpp
│   │   ├── PluginHostAPI.h/.cpp
│   │   ├── PluginStateStore.h/.cpp
│   │   ├── PluginInstaller.h/.cpp
│   │   ├── PluginIPC.h/.cpp
│   │   └── PluginWorker.h/.cpp
│   └── SDK/
│       └── include/WinLauncher/
│           ├── WinLauncherPluginABI.h
│           └── WinLauncherPluginCpp.h
├── SDK/
│   ├── include/WinLauncher/
│   ├── templates/
│   ├── samples/
│   └── docs/PLUGIN_DEV.md
└── WinLauncherPluginWorker/
```

### 4.2 职责边界

| 模块 | 可以依赖 | 不应依赖 | 说明 |
|------|----------|----------|------|
| `PluginManager` | `PluginCatalog`、`PluginRuntime`、`PluginStateStore`、`PluginCommandRegistry`、`Logger` | `PopupWindow` 具体 UI 控件 | 作为插件系统门面，对 UI 暴露查询和命令执行接口 |
| `PluginCatalog` | `ConfigPath`、`PluginManifest`、文件系统 | `PluginRuntime`、UI、Host API | 只负责发现和读取，不决定是否加载 |
| `PluginManifest` | JSON 解析、版本工具、路径校验 | `PluginManager`、Windows 窗口对象 | 只做纯数据校验，便于单元测试 |
| `PluginRuntime` | ABI 头、`LoadLibrary`、`GetProcAddress`、Host API 创建 | `SettingsPage`、搜索排序 | 只负责插件实例生命周期 |
| `PluginCommandRegistry` | manifest 命令、运行时注册命令、排序策略 | `LoadLibrary`、文件解压 | 只维护命令和搜索源，不关心安装 |
| `PluginHostAPI` | 权限、路径、日志、配置、系统能力代理 | 主程序内部状态对象的可变引用 | 所有跨边界能力都在这里统一检查 |
| `SettingsPage` / `ConfigWindow` | `PluginManager` 只读列表和显式操作 API | 插件 DLL 函数表、plugin 私有数据结构 | UI 只负责展示和用户操作，不直接加载插件 |

### 4.3 主流程

启动流程：

```
Application::InitializeServices()
    -> ConfigPath 准备用户目录
    -> PluginManager::Initialize()
        -> PluginCatalog::ScanInstalled()
        -> PluginManifest::Validate()
        -> PluginStateStore::Load()
        -> 生成设置页可展示的插件状态
        -> P1 起再按启用状态加载受信任插件
```

搜索流程：

```
PopupWindow::UpdateSearch()
    -> 构造 query 和 search token
    -> 本地快捷方式搜索
    -> 如果 query 以 "/" 开头：PluginSlashCommandRegistry::Search()
    -> 否则：PluginCommandRegistry::Search() + P2 插件搜索源
    -> 合并为 LauncherSearchItem 列表
    -> UI 展示并保持来源信息
```

执行流程：

```
PopupWindow::ExecuteSelectedItem()
    -> LocalShortcut: 走现有 ExecuteShortcut()
    -> PluginCommand: PluginManager::ExecuteCommand()
    -> SlashCommand: PluginManager::ExecuteSlashCommand()
    -> 所有插件执行结果都记录状态、错误和用户可见反馈
```

退出流程：

```
Application::Shutdown() / TrayMenuWindow 退出
    -> 隐藏或释放自绘窗口
    -> PluginManager::Shutdown()
    -> 卸载插件、flush 状态和错误日志
    -> 释放配置与服务对象
```

### 4.4 数据所有权

- 主配置 `launcher_config.ini` 仍由现有配置服务负责，插件不能直接写入。
- 插件全局状态只写入 `%APPDATA%\WinLauncher\plugins\state`。
- 每个插件私有数据只写入 `%APPDATA%\WinLauncher\plugins\installed\<plugin_id>\data` 或后续迁移出的 `%APPDATA%\WinLauncher\plugins\data\<plugin_id>`。
- 插件包缓存只写入 `%APPDATA%\WinLauncher\plugins\packages` / `cache`。
- 主日志可以写插件摘要，插件详细错误进入 `plugin_errors.jsonl`，避免主日志被搜索源刷屏。

---

## 5. 插件 ABI 与 SDK

### 5.1 ABI 决策

公共 DLL 边界使用 C ABI：

- 导出函数使用 `extern "C"`。
- 使用 `uint32_t` 版本号做 ABI 协商。
- 字符串使用 UTF-16 指针和长度，避免跨模块分配释放不一致。
- 数组由调用方提供缓冲区或使用 Host 分配器回调释放。
- 插件实例使用 opaque handle，不跨 DLL 边界暴露 C++ 类实例。

C++ SDK 可提供 header-only 包装，把 C ABI 包成开发者更易用的 C++ 类，但包装层不作为二进制契约。

### 5.2 核心导出函数

```cpp
extern "C" __declspec(dllexport)
uint32_t WinLauncherPlugin_GetAbiVersion();

extern "C" __declspec(dllexport)
bool WinLauncherPlugin_Create(
    const WLHostApiV1* host,
    WLPluginInstanceV1** outInstance);

extern "C" __declspec(dllexport)
void WinLauncherPlugin_Destroy(WLPluginInstanceV1* instance);
```

### 5.3 插件实例函数表

```cpp
struct WLPluginInstanceV1
{
    void* userData;

    bool (*onLoad)(void* userData);
    void (*onUnload)(void* userData);
    bool (*executeCommand)(void* userData, const WLCommandContextV1* context, WLStringResultV1* outMessage);
    bool (*search)(void* userData, const WLSearchRequestV1* request, WLSearchResponseV1* outResponse);
    void (*onEvent)(void* userData, const WLEventV1* event);
};
```

### 5.4 版本兼容策略

- `abiVersion` 使用整数版本，例如 `1`。
- `minHostVersion` 和 `targetHostVersion` 使用语义版本字符串。
- 主程序只加载 `abiVersion` 兼容且 `minHostVersion <= 当前版本` 的插件。
- 新增字段采用结构体 `size` 字段兼容旧插件。
- 破坏性 ABI 变更必须新增 ABI 版本，不直接修改 v1 语义。

---

## 6. 插件清单

### 6.1 `plugin.json` 示例

```json
{
  "schemaVersion": 1,
  "id": "file_tools",
  "name": "File Tools",
  "version": "1.0.0",
  "author": "Author Name",
  "description": "Adds file related commands to WinLauncher.",
  "entry": "file_tools.dll",
  "abiVersion": 1,
  "minHostVersion": "0.5.1.7",
  "capabilities": ["commands", "search"],
  "permissions": ["clipboard.read", "open.file", "file.read"],
  "icon": "icon.png",
  "commands": [
    {
      "id": "file_tools.open_folder",
      "title": "Open Folder",
      "description": "Open a selected folder.",
      "keywords": ["folder", "open"]
    }
  ],
  "slashCommands": [
    {
      "id": "file_tools.hash",
      "command": "hash",
      "title": "Calculate Hash",
      "description": "Calculate file hash (MD5/SHA256).",
      "usage": "/hash <filepath>",
      "keywords": ["checksum", "md5", "sha256"],
      "aliases": ["md5", "sha256"]
    },
    {
      "id": "file_tools.encode",
      "command": "encode",
      "title": "Base64 Encode",
      "description": "Base64 encode clipboard text or input.",
      "usage": "/encode <text>",
      "keywords": ["base64", "encode"],
      "aliases": ["b64"]
    }
  ]
}
```

### 6.2 必填字段

| 字段 | 规则 |
|------|------|
| `schemaVersion` | 当前为 `1` |
| `id` | 小写字母、数字、下划线、连字符；必须与目录名一致 |
| `name` | 面向用户展示，不能为空 |
| `version` | 语义版本，至少 `major.minor.patch` |
| `entry` | 插件 DLL 相对路径，不允许绝对路径和目录穿越 |
| `abiVersion` | 必须是主程序支持的 ABI 版本 |
| `minHostVersion` | 不得高于当前 WinLauncher 版本 |
| `capabilities` | 必须来自允许列表 |
| `permissions` | 必须来自允许列表 |

### 6.3 命令 ID 规则

- 命令 ID 必须是 `<plugin_id>.<command_name>`。
- `<command_name>` 只允许小写字母、数字、下划线、连字符和点。
- 不允许插件注册不属于自己命名空间的命令。
- 本地快捷方式、内置命令、插件命令在搜索结果里必须保留来源信息，避免同名误执行。

---

## 7. 权限模型

### 7.1 权限列表

| 权限 | Host API 能力 | 默认阶段 |
|------|---------------|----------|
| `app.info` | 读取版本、主题、语言等应用信息 | P0 |
| `log.write` | 写入插件日志 | P0 |
| `plugin.config.read` | 读取插件私有配置 | P0 |
| `plugin.config.write` | 写入插件私有配置 | P0 |
| `clipboard.read` | 读取剪贴板文本 | P1 |
| `clipboard.write` | 写入剪贴板文本 | P1 |
| `open.url` | 打开 URL | P1 |
| `open.file` | 打开文件或文件夹 | P1 |
| `file.read` | 读取用户选择或授权路径 | P1 |
| `file.write` | 写入插件数据目录 | P1 |
| `network.request` | 发起 HTTP 请求 | P2 |
| `process.run` | 执行外部进程 | P2 |
| `ui.settings` | 声明式配置页 | P2 |
| `background.worker` | 长驻后台任务 | P3 |

### 7.2 权限边界

进程内插件可以绕过 Host API 直接调用 Win32 API，因此：

- P1 进程内插件只面向内置插件、开发者本地插件或用户明确信任的插件。
- 权限校验用于控制 Host API 和 UI 提示，不作为强沙箱。
- 社区插件默认目标应是 P3 隔离工作进程。

### 7.3 Host API 无权限行为

| 方法 | 无权限行为 |
|------|------------|
| `ReadClipboard` | 返回空结果和 `permissionDenied` 错误码 |
| `WriteClipboard` | 返回 false |
| `ReadTextFile` | 返回空结果和错误码 |
| `WriteTextFile` | 返回 false |
| `OpenUrl` / `OpenFile` | 返回 false |
| `HttpGet` | 返回错误码，不发起请求 |
| `RunProcess` | 返回错误码，不启动进程 |
| `GetPluginConfig` | 返回默认值 |
| `SetPluginConfig` | 返回 false |

---

## 8. 命令与搜索集成

### 8.1 统一搜索结果模型

新增 `PluginSearchItem` 或更通用的 `LauncherSearchItem`，用于区分结果来源：

```cpp
enum class SearchItemKind
{
    LocalShortcut,
    DockShortcut,
    PluginCommand,
    PluginSearchResult,
    SlashCommand          // / 命令，仅当查询以 / 开头时匹配
};
```

每条结果至少包含：

- `kind`
- `pluginId`
- `commandId`
- `title`
- `subtitle`
- `score`
- `icon`
- `originalPageIndex`
- `originalShortcutIndex`

### 8.2 与 `PopupWindow::UpdateSearch()` 的关系

当前 `UpdateSearch()`（`PopupWindow.cpp:1101`）只搜索本地页面（`m_pages`）和 Dock（`m_dockPage`），在文本变更（`PopupWindow.cpp:2772` on `WM_KEYDOWN`，`PopupWindow.cpp:2837` on `WM_CHAR`）和搜索模式切换（`PopupWindow.cpp:2711`）时触发。插件系统引入后建议拆为：

```
PopupWindow::UpdateSearch()
    |
    +-- BuildLocalShortcutResults(query)
    +-- PluginManager::Search(query, budget)
    +-- MergeAndSortResults()
    +-- Update selected item
```

这样可以保留现有本地搜索行为，同时把插件结果纳入同一展示和执行管线。

### 8.3 搜索性能预算

- 单个进程内插件搜索建议预算：50ms。
- 总搜索预算建议：150ms。
- 超时插件本轮丢弃结果并记录 warning。
- 隔离模式下搜索应使用异步请求，返回结果按最新 query token 合并，避免旧查询覆盖新结果。

### 8.4 命令执行路径

插件命令不应伪装为普通 `RendShortcutInfo` 文件项。当前 `PopupWindow::LaunchShortcut()`（`PopupWindow.cpp:4006`）是统一的本地快捷方式执行入口，也接收 `Application::LaunchShortcutById`（`Application.cpp:512`，通过 `AppMessages::LaunchShortcutById` 消息分发）的调用。建议执行入口拆分：

```
PopupWindow
    |
    +-- SearchItemKind::LocalShortcut -> ExecuteShortcut()
    +-- SearchItemKind::PluginCommand -> PluginManager::ExecuteCommand(pluginId, commandId, context)
    +-- SearchItemKind::SlashCommand  -> PluginManager::ExecuteSlashCommand(pluginId, commandId, rawArgs)
```

普通命令和 `/` 命令走不同执行入口，因为 `/` 命令携带用户键入的参数文本（如 `/hash C:\file.txt` 中 `C:\file.txt` 是参数），需要解析后传入。

执行上下文包含：

- 原始搜索输入。
- 命令参数文本。
- 当前剪贴板文本（仅有权限时填充）。
- 最近捕获的选中文件。
- 当前主题、窗口模式、DPI scale 等只读上下文。

### 8.5 `/` 命令（Slash Commands）

#### 8.5.1 概念

`/` 命令是一套与普通搜索隔离的特殊命令系统，由用户在搜索框中键入 `/` 触发。其设计要点：

- **两套独立系统**：`/` 命令图标与普通图标在展示、匹配、执行上完全分离。
- **前缀强制匹配**：普通文本输入不会命中 `/` 命令；必须键入 `/` 开头才会进入 `/` 命令匹配模式。例如输入 `hash` 不会匹配 `/hash`，输入 `/hash` 才会。
- **用途定位**：高级指令、实用工具、系统操作、开发辅助等不适合作为普通快捷方式的场景。
- **参数传递**：`/` 命令天然支持参数，用户键入 `/hash C:\file.txt` 时，命令名为 `hash`，参数为 `C:\file.txt`。

#### 8.5.2 内置 / 命令

WinLauncher 自带一批内置 `/` 命令。这些命令目前存在于"内置图标窗口"中、需要用户手动添加到快捷方式面板。迁移到 `/` 命令系统后，它们变为搜索框即用模式：

| 命令 | 功能 |
|------|------|
| `/settings` / `/config` | 打开设置窗口 |
| `/reload` | 重新加载配置和插件 |
| `/theme` | 切换主题 |
| `/about` | 显示关于信息 |
| `/help` | 显示帮助 |
| `/clip` | 查看/管理剪贴板历史 |
| `/calc` | 计算器表达式求值 |
| `/color` | 取色或颜色格式转换 |
| `/base64` | Base64 编解码 |
| `/uuid` | 生成 UUID/GUID |
| `/date` | 显示/格式化当前日期时间 |
| `/json` | JSON 格式化/压缩/校验 |
| `/shortcut` | 创建快捷方式向导 |

内置 `/` 命令由主程序启动时在 `PluginSlashCommandRegistry` 中注册，不需要对应插件 DLL。

#### 8.5.3 插件注册 / 命令

插件可以通过两种方式注册 `/` 命令：

1. **清单声明（推荐）**：在 `plugin.json` 的 `slashCommands` 数组中声明。主程序加载插件时自动注册，无需额外代码。
2. **运行时注册**：插件在 `onLoad` 阶段通过 Host API 的 `RegisterSlashCommand` 方法动态注册。适用于命令列表需要根据运行时条件变化的情况。

清单声明的 `/` 命令字段：

| 字段 | 必填 | 说明 |
|------|------|------|
| `id` | 是 | 全局唯一 ID，格式 `<plugin_id>.<command>`，规则同普通命令 |
| `command` | 是 | `/` 后面的命令名称（不含 `/` 前缀），如 `hash` 表示 `/hash`。只能包含小写字母、数字、连字符 |
| `title` | 是 | 面向用户展示的标题 |
| `description` | 否 | 命令描述，显示在 `/` 命令列表中 |
| `usage` | 否 | 用法提示，如 `/hash <filepath>`，在 UI 中作为辅助信息显示 |
| `keywords` | 否 | 匹配关键词列表，当用户搜索 `/` 命令时扩展匹配范围 |
| `aliases` | 否 | 命令别名列表（不含 `/` 前缀），如 `["md5", "sha256"]` 表示 `/md5` 和 `/sha256` 也触发同一命令 |
| `icon` | 否 | 命令图标的相对路径 |

#### 8.5.4 搜索匹配逻辑

```
用户输入 → PopupWindow::UpdateSearch()
    |
    +-- 输入以 "/" 开头？
    |   是 → 进入 SlashCommand 匹配模式
    |   |    |
    |   |    +-- 去除首字符 "/" 得到 query
    |   |    +-- 遍历注册表中所有 / 命令
    |   |    +-- 匹配字段：command、keywords、aliases、title
    |   |    +-- 返回匹配结果列表（SearchItemKind::SlashCommand）
    |   |    +-- 结果中显示 usage 信息做辅助提示
    |   |
    |   否 → 走普通搜索流程（不包含 / 命令）
```

匹配规则：

- `query` 是去除 `/` 后用户已输入的部分。例如输入 `/ha`，query = `ha`，应匹配 `command=hash`。
- `/` 必须是输入的第一个字符，中间出现的不视为斜杠命令前缀。
- 当用户只输入 `/` 时，展示所有可用 `/` 命令的概览列表。
- `/` 命令结果不参与普通搜索结果排序，在弹窗中以独立分组展示（如通过分隔线、不同底色或图标风格区分）。

#### 8.5.5 执行逻辑

```
PopupWindow::ExecuteSelectedItem()
    |
    +-- SearchItemKind::SlashCommand
        |
        +-- 解析参数：用户输入中命令名之后的部分
        |   "/hash C:\file.txt" → command="hash", args="C:\file.txt"
        |   "/hash"             → command="hash", args=""
        |
        +-- PluginManager::ExecuteSlashCommand(pluginId, commandId, args)
            |
            +-- 查找命令注册表中的对应条目
            +-- 构造 WLSlashCommandContext（含 args、rawInput、clipboard、selectedFiles）
            +-- 如果是插件命令 → 调用插件实例的 ExecuteSlashCommand
            +-- 如果是内置命令 → 调用内置处理函数
            +-- 返回执行结果（成功/失败、输出文本等）
```

---

## 9. 生命周期与事件

### 9.1 P0 事件来源

先复用现有调用点：

| 事件 | 当前接入点 | 插件事件 |
|------|------------|----------|
| 弹窗显示 | `PopupViewModel::NotifyPopupShown()`（`ViewModel/PopupViewModel.h:145`）→ `pluginHost->NotifyPopupShown()` | `popup.shown` |
| 弹窗隐藏 | `PopupViewModel::NotifyPopupHidden()`（`ViewModel/PopupViewModel.h:151`）→ `pluginHost->NotifyPopupHidden()` | `popup.hidden` |
| 快捷方式启动 | `PopupViewModel::NotifyShortcutLaunched(pageIndex, shortcutIndex)`（`ViewModel/PopupViewModel.h:86`）→ `pluginHost->NotifyShortcutLaunched(shortcut)` | `shortcut.launched` |
| 应用退出 | `Application::Shutdown()`（`App/Application.cpp:323-326`）→ `pluginHost->UnloadAll()`；`TrayMenuWindow` 退出菜单（`TrayMenuWindow.cpp:286`）→ `pluginHost->UnloadAll()` | `app.shutdown` |
| 配置变化 | `ConfigWindow` 保存配置后 / `EventBus::ConfigChanged`（`App/EventBus.h`） | `config.changed` |

### 9.2 事件模型演进

- P0：`PluginManager` 保持显式通知方法，事件载荷最小化。
- P1：扩展 `EventBus` 支持 typed payload 或为插件建立独立 `PluginEvent` 模型。
- P2：隔离模式下事件通过 IPC 广播，带事件序号和超时保护。

---

## 10. 状态、错误与隔离

### 10.1 状态文件

`%APPDATA%\WinLauncher\plugins\state\plugins_state.json`

```json
{
  "file_tools": {
    "enabled": true,
    "loadMode": "inProcess",
    "quarantinedUntil": "",
    "failureCount": 0,
    "lastError": "",
    "lastLoadedAt": "2026-07-02T10:30:00Z"
  }
}
```

### 10.2 错误日志

`%APPDATA%\WinLauncher\plugins\state\plugin_errors.jsonl`

每行记录：

- 时间。
- 插件 ID 和版本。
- 阶段：manifest / load / search / execute / unload / ipc。
- 错误码和简短消息。
- 是否触发隔离。

### 10.3 隔离策略

- 清单错误：不加载，显示为 invalid。
- 加载失败：记录失败次数，可由用户重试。
- 连续 3 次加载失败：自动禁用。
- 连续 5 次运行失败：隔离 300 秒。
- 用户手动禁用优先于自动恢复。
- 插件更新版本后可清除部分错误计数，但保留历史日志。

---

## 11. 插件安装与打包

### 11.1 `.wlplugin` 包格式

`.wlplugin` 是 ZIP 包，内含编译好的插件 DLL：

```
file_tools.wlplugin
├── plugin.json
├── file_tools.dll
├── icon.png
└── data\
```

### 11.2 安装校验

安装器必须检查：

- ZIP 内所有路径都是相对路径。
- 不允许 `..`、绝对路径、盘符路径、UNC 路径。
- 文件总数默认不超过 200。
- 解压后总大小默认不超过 50MB。
- `plugin.json` 位于根目录。
- `entry` 指向包内存在的 DLL。
- `id` 与目标安装目录一致。
- 若目标目录已存在，先备份再替换，失败时回滚。

### 11.3 更新策略

- 相同 `id` 高版本包视为更新。
- 降级需要用户确认。
- 更新前先调用旧插件卸载。
- 更新后重新扫描并加载。
- 更新失败时恢复旧版本目录和状态。

---

## 12. 设置页与用户体验

### 12.1 插件管理页

在现有自绘设置窗口中新增“插件”分类，至少显示：

- 插件名称、版本、作者。
- 启用状态。
- 权限摘要。
- 加载模式：受信任进程内 / 隔离进程。
- 错误状态和最近错误。
- 操作：启用、禁用、卸载、打开插件目录、查看日志。

### 12.2 插件配置

早期不建议允许插件提供任意 HWND。推荐先做声明式配置：

```json
{
  "settings": [
    {
      "key": "max_results",
      "type": "integer",
      "title": "Max Results",
      "default": 10,
      "min": 1,
      "max": 50
    },
    {
      "key": "include_hidden_files",
      "type": "boolean",
      "title": "Include Hidden Files",
      "default": false
    }
  ]
}
```

WinLauncher 自己渲染这些控件，保证样式、缩放、主题和输入行为一致。

---

## 13. 分阶段实施计划

### 13.0 阶段门槛

| 阶段 | 进入条件 | 退出条件 | 不允许夹带 |
|------|----------|----------|------------|
| P0 | 当前 `PluginHost` 调用点已重新核对；确认设置页可承载插件列表 | 无插件、无效 manifest、禁用状态、错误状态都可稳定展示 | DLL 加载、SDK、worker、在线仓库 |
| P1 | P0 状态和错误模型稳定；设置页能解释插件启用状态 | 示例 DLL 完成注册、搜索、执行、卸载闭环 | `.wlplugin` 安装器、社区插件默认开放、复杂 Host API |
| P2 | P1 的 ABI 和命令模型没有阻塞问题 | 用户可安装示例包，开发者可按 SDK 文档开发基础插件 | worker 安全承诺、插件商店 |
| P3 | P1/P2 暴露的稳定 ABI 已确定；IPC 协议可版本化 | 插件崩溃/超时不影响主进程，隔离状态可见 | 在线生态、后台常驻任务大扩展 |
| P4 | 隔离模型和安装模型稳定 | 形成可维护的插件生态能力 | 绕过签名/信任/权限模型的快速入口 |

阶段推进原则：

- P0 是基础设施，不追求“好玩”，只追求“可管理、可解释、可回滚”。
- P1 是最小能力闭环，只允许一个受信任示例插件证明 ABI 和搜索执行路径正确。
- P2 才开始考虑第三方开发者体验和插件包安装体验。
- P3 才能对陌生第三方插件给出较强的稳定性承诺。
- P4 不应在 P0-P3 未稳定时提前做，否则会把生态问题压到不稳定的底层架构上。

### P0：主机侧基础与清单闭环

目标：不急于加载第三方 DLL，先把插件目录、清单、状态、错误和管理模型打通。

任务：

1. 新增 `PluginManifest`，解析并校验 `plugin.json`。
2. 新增 `PluginCatalog`，扫描 `%APPDATA%\WinLauncher\plugins\installed`。
3. 新增 `PluginStateStore`，读写启用、禁用、隔离和错误状态。
4. 新增 `PluginManager`，替换当前轻量 `PluginHost` 的职责入口。
5. `AppContext` 改持有 `PluginManager`。
6. 设置页显示插件列表和错误状态。
7. 写入插件状态和错误日志。

验收：

- 放入无效 manifest 时不会崩溃，设置页能显示错误。
- 禁用状态重启后仍然保留。
- 没有插件时启动流程和弹窗行为不变。
- 退出路径只调用一次统一插件 shutdown，不出现重复卸载或空指针。
- `plugin_errors.jsonl` 能记录 manifest、路径、版本、权限等 P0 错误。
- `RELEASE_NOTES.md` 记录“新增插件管理基础设施/插件计划落地准备”这类用户可理解的变化。

P0 暂停点：

- 如果设置页列表还无法稳定展示插件状态，不进入 P1。
- 如果 `PluginManager` 替换后本地弹窗、设置页或退出流程有回归，不进入 P1。
- 如果 manifest 校验规则还依赖 UI 层判断，不进入 P1。

### P1：受信任进程内命令插件 MVP

目标：完成第一个可执行插件闭环。

任务：

1. 新增 ABI v1 头文件和 C++ SDK 包装。
2. 实现 `PluginRuntime` 的进程内加载。
3. 实现 `PluginHostAPI` 的基础能力：版本、日志、插件数据目录、插件配置。
4. 新增 `PluginCommandRegistry`。
5. 改造 `PopupWindow::UpdateSearch()`，合并本地快捷方式和插件命令。
6. 改造搜索结果执行路径，支持 `PluginCommand`。
7. 提供 `hello_world` 示例插件。

验收：

- 示例插件能被扫描、启用、显示在搜索结果、执行并写日志。
- 禁用插件后命令从搜索结果中消失。
- 插件卸载后 DLL 句柄释放，命令注册清理。
- 插件执行失败不影响本地快捷方式执行。
- 普通搜索和 `/` 命令搜索互不污染。
- 插件命令执行失败时，用户能看到简短失败反馈，维护者能在日志中看到原因。
- 删除插件目录或替换 DLL 后，重新扫描行为可预测，不留下悬空命令。

P1 暂停点：

- 如果搜索结果仍以 `RendShortcutInfo` 承载插件命令，不进入 P2。
- 如果 ABI 仍暴露 C++ 对象或跨 DLL 分配释放责任不清，不进入 P2。
- 如果示例插件需要修改主程序内部对象才能工作，不进入 P2。

### P2：SDK、安装器与基础能力扩展

目标：让第三方开发者可以较稳定地开发和安装插件。

任务：

1. 完成 `SDK/docs/PLUGIN_DEV.md`。
2. 提供 Visual Studio 插件模板。
3. 实现 `.wlplugin` 安装、更新、卸载和回滚。
4. Host API 增加剪贴板、打开 URL/文件、受限文件读写。
5. 支持插件搜索源，加入搜索预算和超时日志。
6. 支持声明式插件配置。

验收：

- 用户可从 `.wlplugin` 安装示例插件。
- 插件权限在设置页可见。
- 搜索源慢响应不会卡住弹窗输入。
- 插件配置变更只写入插件私有配置。
- 安装失败能回滚到旧版本或干净状态。
- `.wlplugin` 包路径穿越、绝对路径、过大包、缺失 entry 都能被拒绝并显示原因。
- SDK 示例和实际主程序 ABI 匹配，不出现示例能编译但无法加载的情况。

P2 暂停点：

- 如果安装器不能回滚，不开放更新功能。
- 如果 Host API 权限提示和实际权限检查不一致，不扩大权限列表。
- 如果搜索源仍然可能阻塞 UI 线程，不允许引入网络或文件系统重搜索源。

### P3：隔离工作进程

目标：将社区插件默认迁移到进程外运行。

任务：

1. 新增 `WinLauncherPluginWorker.exe`。
2. 实现命名管道 IPC。
3. 实现 RemoteHostAPI。
4. 实现 worker 健康检查、超时、崩溃重启、熔断。
5. 支持隔离模式下的搜索和命令执行。

验收：

- worker 崩溃不会带崩主进程。
- 超时插件会被终止或隔离。
- 主进程重启后隔离状态保留。
- worker IPC 版本不匹配时能拒绝加载并显示可理解错误。
- 主进程退出时 worker 能被有序回收；异常残留进程不会影响下一次启动。
- 隔离模式和进程内模式在设置页中有清楚区分。

P3 暂停点：

- 如果 worker 崩溃后主进程状态不一致，不进入 P4。
- 如果 IPC 没有超时和取消机制，不允许把搜索源默认放到 worker。
- 如果 worker 日志和主进程日志无法关联同一次插件操作，不进入生态扩展。

### P4：生态能力

目标：完善插件生态和长期扩展能力。

任务：

1. 插件签名和信任提示。
2. 内置插件 bundle。
3. 插件在线仓库或导入源。
4. 插件更新提醒。
5. 后台任务和更细粒度事件订阅。
6. 受控自定义 UI 或高级渲染扩展。

P4 验收：

- 插件签名、来源、信任等级和权限提示在 UI 上形成一致语言。
- 在线导入源失败不会影响本地已安装插件。
- 内置插件、官方插件、社区插件、开发者本地插件有清晰区分。
- 后台任务可被用户发现、暂停、禁用和诊断。

### 13.1 每轮实施模板

以后每次按本文档实施时，建议按同一模板推进：

1. 选择阶段和本轮目标。
2. 重新核对相关当前代码调用点。
3. 写明本轮不做的后置能力。
4. 实现最小闭环。
5. 做无插件、坏插件、禁用插件、正常插件四类验证。
6. 更新 `RELEASE_NOTES.md` 当前版本段落。
7. 记录还没进入下一阶段的原因或下一阶段入口条件。

---

## 14. 代码改动清单

### 14.1 优先新增

| 文件 | 职责 |
|------|------|
| `WinLauncher/App/PluginManager.h/.cpp` | 插件系统门面和生命周期管理 |
| `WinLauncher/App/PluginManifest.h/.cpp` | manifest 解析、字段校验、版本校验 |
| `WinLauncher/App/PluginCatalog.h/.cpp` | 插件目录扫描 |
| `WinLauncher/App/PluginStateStore.h/.cpp` | 状态和错误持久化 |
| `WinLauncher/App/PluginRuntime.h/.cpp` | 进程内 DLL 加载 |
| `WinLauncher/App/PluginCommandRegistry.h/.cpp` | 插件命令和搜索源注册表 |
| `WinLauncher/App/PluginSlashCommandRegistry.h/.cpp` | / 命令专用注册、匹配、查找和执行调度；区分内置 / 命令和插件 / 命令 |
| `WinLauncher/App/PluginHostAPI.h/.cpp` | Host API 实现和权限检查；新增 RegisterSlashCommand 运行时注册方法 |
| `WinLauncher/SDK/include/WinLauncher/WinLauncherPluginABI.h` | 稳定 C ABI；新增 / 命令相关 ABI 类型和函数表 |
| `WinLauncher/SDK/include/WinLauncher/WinLauncherPluginCpp.h` | C++ 便利封装 |

### 14.2 需要修改

| 文件 | 修改方向 |
|------|----------|
| `WinLauncher/App/PluginHost.h` | 收敛或替换为 `PluginManager`，避免继续承载完整系统；加载逻辑改为 `LoadLibrary` + `GetProcAddress` 方式。当前 69 行全内联，无 `LoadPlugin`/`UnloadPlugin` 单插件方法，重构时需暴露单插件粒度的启停 API |
| `WinLauncher/App/AppContext.h`（`:15`） | 将 `std::unique_ptr<PluginHost> pluginHost` 替换为 `std::unique_ptr<PluginManager> pluginManager`，并传入 logger/config/app context |
| `WinLauncher/App/Application.cpp`（`:325-326`） | 启动时（`InitializeServices`）扫描插件目录；退出时（`Shutdown`）将 `pluginHost->UnloadAll()` 替换为 `pluginManager->Shutdown()`；注意 shutdown 顺序：插件卸载应在 `PopupWindow::Release()` 之后、`configService.reset()` 之前 |
| `WinLauncher/ViewModel/PopupViewModel.h`（`:86-155`） | 三处 `m_ctx->pluginHost->Notify*` 调用改指向 `m_ctx->pluginManager` |
| `WinLauncher/PopupWindow.cpp`（`:1101,2772,2837`） | `UpdateSearch()` 搜索结果模型和插件命令执行入口；`LaunchShortcut()`（`:4006`）路径分流；新增 `/` 命令匹配分支和参数解析 |
| `WinLauncher/TrayMenuWindow.cpp`（`:286`） | "退出应用"菜单项的 `s_ctx->pluginHost->UnloadAll()` 替换为 `s_ctx->pluginManager->Shutdown()`，确保退出和主程序 shutdown 路径一致 |
| `WinLauncher/Config/SettingsPage.h/.cpp` | 插件管理页入口和交互（`SettingsPage` 当前已承载全局设置，插件分类可复用其自绘控件） |
| `WinLauncher/Config/ConfigWindow.h/.cpp` | 插件管理页路由、安装/卸载操作（`ConfigWindow` 已有 category 侧边栏，可新增"插件"分类） |
| `WinLauncher/WinLauncher.vcxproj` | 纳入新增源文件和 SDK 头文件；添加 `SDK\include\WinLauncher\` 到附加包含目录 |
| `WinLauncher/WinLauncher.vcxproj.filters` | 增加 `App\Plugin` 和 `SDK` 筛选器分组 |

---

## 15. QuickLauncher 对照

| QuickLauncher 能力 | WinLauncher 方案 | 阶段 |
|-------------------|------------------|------|
| `PluginInfo` / manifest 模型 | `PluginManifest` + `plugin.json` | P0 |
| 插件目录扫描 | `PluginCatalog` 扫描 `%APPDATA%\WinLauncher\plugins\installed` | P0 |
| 状态存储 | `PluginStateStore` JSON / JSONL | P0 |
| Host API | `PluginHostAPI` + 权限检查 | P1 |
| 注册命令 | `PluginCommandRegistry` | P1 |
| 搜索源 | `PluginManager::Search()` 合并结果 | P2 |
| `.qlzip` 包 | `.wlplugin` ZIP 包 | P2 |
| 插件管理 UI | 自绘设置页插件分类 | P2 |
| 隔离运行 | `WinLauncherPluginWorker.exe` + 命名管道 | P3 |
| Worker 熔断 | 失败计数、超时、隔离状态 | P3 |
| 沙箱限制 | Host API 权限 + 进程隔离 | P3 |
| 插件商店 | 在线仓库 / 信任链 | P4 |

---

## 16. 风险与缓解

| 风险 | 缓解 |
|------|------|
| 进程内 DLL 崩溃导致主进程崩溃 | P1 只允许受信任插件；P3 默认隔离运行 |
| ABI 不稳定导致插件升级后不可用 | C ABI + `abiVersion` + 结构体 `size` 字段 |
| 搜索源阻塞弹窗输入 | 搜索预算、异步 token、超时丢弃 |
| 插件污染用户配置 | 插件只能写私有数据目录和私有配置命名空间 |
| 插件 UI 破坏自绘风格 | 先做声明式配置，由 WinLauncher 渲染 |
| 插件安装包目录穿越 | 安装前校验 ZIP 路径，先解压临时目录再原子替换 |
| 错误难以定位 | `plugin_errors.jsonl` + 设置页最近错误 + 主日志摘要 |
| `PluginHost` 替换为 `PluginManager` 时破坏现有调用方 | `PopupViewModel`（3 处）、`Application::Shutdown`（1 处）、`TrayMenuWindow`（1 处）共 5 处调用点，需统一 API 命名后一次性更新；建议先在 `PluginManager` 上保留兼容的 `Notify*` 方法，逐步迁移 |
| `PluginHost` 缺少单插件粒度的 `LoadPlugin`/`UnloadPlugin`，现有 `UnloadAll` 无法选择性卸载 | P0 在 `PluginManager` 中从零设计细粒度 API，不受旧接口约束 |
| 第一轮范围过大导致半成品不可验收 | P0 严格限制为 manifest、状态、错误、设置页列表；DLL 加载进入 P1 |
| 插件状态和主配置耦合过深 | 插件状态独立 JSON/JSONL，主配置只保留必要的全局开关 |
| `/` 命令和普通搜索结果混排导致误执行 | 独立 `SearchItemKind::SlashCommand` 和独立注册表，只有 `/` 前缀进入匹配 |
| 示例插件被误打进正式发布包 | 发布验证检查测试插件、SDK 示例和本地路径是否进入运行时目录 |

---

## 17. 验证清单

每个阶段完成前至少验证：

- 无插件目录时启动、弹窗、设置页行为不变。
- 无效 manifest 不会阻塞主程序启动。
- 禁用、启用、隔离状态重启后保持一致。
- 插件搜索不会让弹窗输入卡顿。
- 插件命令执行失败不会影响普通快捷方式。
- 卸载插件后搜索结果和命令注册被清理。
- 发布包不包含测试插件或本地开发路径，除非明确作为示例发布。
- `RELEASE_NOTES.md` 已按当前版本更新。

### 17.1 分层验证矩阵

| 类别 | 验证项 | 适用阶段 |
|------|--------|----------|
| 启动 | `%APPDATA%\WinLauncher\plugins` 不存在时自动处理；启动时间无明显回退 | P0+ |
| 启动 | `plugins\installed` 为空时设置页显示空状态，不报错 | P0+ |
| Manifest | 缺失 `plugin.json`、JSON 语法错误、缺失必填字段、版本过高、权限未知都能被标记 invalid | P0+ |
| Manifest | `entry` 为绝对路径、`..`、UNC、非 DLL、目录不存在时拒绝 | P0+ |
| 状态 | 启用、禁用、失败计数、隔离状态重启后保持一致 | P0+ |
| UI | 插件管理页在浅色/深色、不同全局缩放下不溢出、不使用系统窗口 | P0+ |
| 搜索 | 普通搜索不出现 `/` 命令；输入 `/` 时只出现 `/` 命令 | P1+ |
| 搜索 | 插件命令和本地快捷方式同名时仍能区分来源 | P1+ |
| 执行 | 插件命令失败、超时或返回错误时，本地快捷方式仍可执行 | P1+ |
| 卸载 | 禁用或卸载插件后搜索结果、注册命令、DLL 句柄和状态都清理 | P1+ |
| 安装 | `.wlplugin` 更新失败能恢复旧版本目录和状态 | P2+ |
| 权限 | 未声明权限的 Host API 调用返回明确错误，不执行副作用 | P2+ |
| 隔离 | worker 崩溃、卡死、IPC 版本不匹配都不会拖垮主进程 | P3+ |
| 发布 | Release 包不包含开发机绝对路径、测试插件、临时包缓存 | P2+ |

### 17.2 建议验证命令与检查方式

具体命令应按当时仓库状态调整，但至少覆盖：

- 静态检查：搜索新增 `pluginHost` 残留调用点，确认统一迁移到 `pluginManager`。
- 编译检查：使用项目现有 Visual Studio / MSBuild 路线验证 Release x64；如遇 `LNK1104`，先确认是否运行中的 `WinLauncher.exe` 锁定输出。
- 文档检查：确认 `PLUGIN_SYSTEM_PLAN.md`、SDK 文档和 `RELEASE_NOTES.md` 对同一阶段描述一致。
- 运行检查：手动或脚本构造空插件目录、无效 manifest、禁用插件、正常示例插件四种状态。
- 日志检查：确认 `%APPDATA%\WinLauncher\config\winlauncher.log` 和 `plugins\state\plugin_errors.jsonl` 中能关联同一次插件操作。

### 17.3 回滚策略

- P0 回滚：保留原 `PluginHost` 兼容外观或最小适配层，必要时让 `PluginManager` 空实现，确保无插件路径完全恢复。
- P1 回滚：通过状态文件禁用所有插件，保留本地快捷方式搜索和执行路径不依赖插件注册表。
- P2 回滚：安装器更新失败必须恢复旧插件目录；新增 Host API 权限可以在 manifest 校验层临时拒绝。
- P3 回滚：隔离 worker 失败时退回“插件不可用/需用户重试”，不得自动降级到不受控进程内运行陌生插件。
- P4 回滚：在线仓库、更新提醒、后台任务必须可单独关闭，不影响已安装本地插件。

---

## 18. 开放问题

- 是否需要为内置插件和第三方插件使用不同的签名或信任等级。
- 是否允许企业用户配置插件白名单目录。
- 是否需要把插件搜索结果纳入使用频率排序。
- 是否需要兼容 QuickLauncher 现有插件包格式，或只提供迁移模板。
- 内置 `/` 命令是作为硬编码注册在主程序中，还是允许通过内置插件 bundle 提供。
- `/` 命令是否需要支持子命令（如 `/file hash`、`/file encode`）还是保持扁平结构。
- `/` 命令的执行结果是否要支持输出文本回显到搜索框或显示在弹窗中。
- 隔离 worker 是否需要支持多插件共享进程，还是每插件一个进程。
- DLL 插件加载失败时的详细错误码（`LoadLibrary` / `GetProcAddress` / DllMain 异常）是否需要向用户展示。
- `TrayMenuWindow.cpp:286` 的 `UnloadAll()` 调用是在"退出应用"菜单项中触发，替换为 `PluginManager` 后是否应在 P0 就统一为 `pluginManager->Shutdown()`（先卸载插件再 `PostQuitMessage`），还是保持简单替换接口名。
- `PluginHost` 当前仅 69 行全内联代码，替换为 `PluginManager` 后是否保留 `PluginHost.h` 作为兼容层（typedef 或 using 别名）以避免一次性修改 5 处调用点，还是直接全部替换。

### 18.1 决策优先级

| 必须决策时间 | 问题 | 建议默认 |
|--------------|------|----------|
| P0 开始前 | `PluginHost.h` 是保留兼容层还是直接替换 | 直接替换调用点，但 `PluginManager` 保留 `Notify*` 兼容方法，减少 UI 调用方改动 |
| P0 开始前 | 退出路径是否统一为 `PluginManager::Shutdown()` | 统一，避免 `Application` 和 `TrayMenuWindow` 两套卸载语义 |
| P1 开始前 | `/` 命令是否支持子命令 | 先保持扁平结构，等 P2/P3 后再评估子命令 |
| P1 开始前 | DLL 加载错误是否向用户展示详细码 | 设置页展示简短原因，详细 Win32 错误码写入日志 |
| P2 开始前 | 是否兼容 QuickLauncher 包格式 | 不直接兼容 `.qlzip`，只提供迁移文档或转换模板 |
| P3 开始前 | worker 是每插件一个进程还是多插件共享 | 默认每插件一个 worker，后续再优化共享池 |
| P4 开始前 | 内置插件与第三方插件信任等级 | 至少区分内置、官方签名、用户本地、社区来源四类 |

---

## 19. 文档元信息

- 文档版本：1.5
- 创建日期：2026-07-01
- 更新日期：2026-07-03
- 参考项目：QuickLauncher V1.6.3.7 Plugin System
- 当前 WinLauncher 版本：0.5.1.7
- 修订记录：
  - v1.5（2026-07-03）：作为后续项目优化计划书重新完善——补充文档使用约束、最终成功标准、架构红线、当前基线判断、规划假设、范围分层、职责边界、主流程、数据所有权、阶段门槛、暂停点、每轮实施模板、分层验证矩阵、回滚策略和开放问题决策优先级；明确 P0 只做插件管理基础设施，P1 再进入受信任 DLL 插件 MVP。
  - v1.4（2026-07-02）：对照 WinLauncher 实际代码库逐项核实并完善——补充 `PluginHost`（69 行全内联、无单插件 API）、`TrayMenuWindow.cpp:286` 调用点、`PopupWindow::UpdateSearch` / `LaunchShortcut` / `Application::LaunchShortcutById` / `JsonImportHelper` 精确文件路径和行号引用；新增两处风险和两个开放问题；修正文件路径通配符格式
