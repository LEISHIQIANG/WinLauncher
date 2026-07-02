# WinLauncher / WinLauncher 桌面启动器

[中文](#中文) | [English](#english)

---


<a id="中文"></a>

## 中文

**WinLauncher** 是一个原生 Windows 桌面应用启动器 —— 极速、轻量、零依赖。完全基于 Win32 API + Direct2D 渲染构建，常驻系统托盘，通过鼠标手势或键盘快捷键在光标处弹出精美的毛玻璃快捷方式面板。可以把它看作你桌面上的个人命令面板。

> 版本 **0.5.1.7** | C++17 | MSVC v143 | Windows 10+

---

### 功能特性

#### 核心启动器
- **弹出面板** — 可分页的快捷方式图标网格，通过可配置的鼠标手势触发
- **双击 Alt 触发** — 快速双击 Alt 键，通过全局键盘钩子显示/隐藏启动器
- **搜索模式** — 输入即时筛选快捷方式
- **底部停靠栏** — 常用快捷方式固定在弹窗底部，始终可见
- **固定屏幕** — 弹窗失去焦点时仍保持可见

#### 7 种快捷方式类型
| 类型 | 说明 |
|---|---|
| **文件** | 启动文件、文件夹、可执行程序（自动解析 `.lnk` 快捷方式） |
| **热键** | 模拟键盘输入 / 按键序列 |
| **网址** | 在默认浏览器中打开网址 |
| **命令** | 运行任意命令（支持 stdin 管道输入） |
| **宏** | 录制并回放鼠标/键盘宏序列 |
| **批量启动** | 按顺序执行多个快捷方式，可配置延迟 |
| **系统图标** | 通过注册键启动系统已知的可执行程序 |

#### 运行时变量系统
命令/URL 快捷方式支持 **12 种运行时变量**，在启动时动态替换：

| 变量 | 说明 |
|---|---|
| `{{clipboard}}` | 当前剪贴板文本内容 |
| `{{date}}` / `{{time}}` | 当前系统日期/时间 |
| `{{lan_ip}}` / `{{wan_ip}}` | 局域网/公网 IP 地址（WAN 有缓存） |
| `{{selected_file}}` / `{{selected_files}}` | 资源管理器中选中的文件路径 |
| `{{selected_file_name}}` / `{{selected_file_dir}}` / `{{selected_file_folder}}` | 选中文件的名称/所在目录/上级目录 |
| `{{app_dir}}` / `{{config_dir}}` | 应用目录/配置目录 |
| `{{input}}` / `{{input:提示}}` | 运行时弹出输入框收集用户文本 |
| `{{password}}` / `{{password:提示}}` | 运行时弹出密码输入框（`●` 掩码） |
| `{{choose:选项1\|选项2\|...}}` | 运行时弹出下拉选择列表 |
| `{{confirm}}` / `{{confirm:消息}}` | 运行时弹出确认对话框，取消则中止 |
| `{{url}}` | 命令中的网址占位符，替换为展开后的 URL |
| `:q` 修饰符 | shell 感知的自动转义（CMD/PowerShell/GitBash） |

#### 视觉效果与主题
- **毛玻璃 / Acrylic 效果** — 背景模糊、光泽效果、圆角，通过 Direct2D 实现
- **深色/浅色模式**，支持玻璃/亚克力窗口变体
- **6 种材料预设** — `dark`、`light`、`acrylicDark`、`acrylicLight`、`glassDark`、`glassLight`，含 Glass 1:1 精确匹配
- **12 种主题色彩预设**，每个主题支持 6 维 HSL 颜色调校
- **平滑主题过渡**，带缓动函数
- **可配置网格布局** — 列数、行数、图标大小、间距、圆角、内边距
- **UI 缩放** — 80% 至 250%，以 10% 步进
- **逐显示器 DPI 感知** (PMv2)
- **基于物理的弹簧动画**，用于打开/关闭过渡

#### 架构与可扩展性
- **类 MVVM 分层架构** — Model / Services / ViewModel / UI
- **依赖注入**，通过 `AppContext` 管理 `EventBus`、`Logger`、`PluginHost`
- **插件系统** — 运行时插件加载，带生命周期钩子
- **事件总线** — 发布/订阅消息，解耦组件通信
- **文件夹同步** — 自动从指定文件夹加载快捷方式，通过 `ReadDirectoryChangesW` 实时监控
- **场景页面** — 每个页面可配置白名单/黑名单应用列表，仅在前台应用匹配时显示该页面
- **命令变量服务** — 命令/URL 中嵌入运行时变量（剪贴板、选中文件、输入框等），支持交互式收集
- **命令输出面板** — 执行命令时弹出实时 stdout/stderr 输出窗口，支持复制与清空
- **配置备份与历史** — 自动创建带时间戳的配置备份，支持还原操作与历史记录浏览
- **快捷操作撤销/重做** — 快捷键 CRUD 操作的撤销/重做栈
- **异步文件选择** — 后台线程捕获资源管理器选中文件，避免 UI 阻塞

#### 生产级功能
- **系统托盘** — 最小化到托盘，单击或快捷键恢复
- **开机自启** — 通过 Windows 任务计划程序注册（兼容管理员/标准用户账户）
- **权限管理** — 以管理员身份 (UAC) 启动目标，或按需降权
- **高危险命令检测** — 自动检测 `rm -rf`、`format` 等危险命令并弹出确认
- **GPU 崩溃防护** — Direct2D 硬件加速崩溃时自动回退到软件渲染
- **鼠标钩子防抖** — 鼠标钩子事件防抖逻辑，防止误触
- **自动更新** — 检查 GitHub Releases 获取新版本
- **线程安全日志** — 基于文件的日志记录，可配置日志级别
- **配置导入** — 支持从 JSON 或 QuickLauncher (Python) 格式导入
- **Toast 通知** — 临时屏幕消息提示

---

### 架构

```
main.cpp  ──►  Application  ──►  AppContext（依赖注入容器）
                                    │
       ┌────────────────────────────┼────────────────────────────┐
       │                            │                            │
    Model 层                    Services 层                  ViewModel 层
    ├─ ShortcutInfo            ├─ IConfigService             ├─ PopupViewModel
    └─ AppearanceSettings      ├─ IIconService               └─ ConfigViewModel
                               ├─ FolderWatcher
                               ├─ MacroService                     │
                               ├─ BatchLaunchService          ┌────┴────┐
                               ├─ UpdateService               │         │
                               ├─ PrivilegeLaunchService   UI 层   渲染层
                               ├─ CommandVariableService   ├─ Popup   ├─ Compositor
                               ├─ AppSceneMatcher          ├─ Config  ├─ BgLayer
                               ├─ FileSelectionService     └─ Tray    └─ Overlay
                               └─ ConfigPath                 Menu
                                                             │
 窗口继承体系                                        Config 子窗口
 BaseWindow ──► GlassWindow ──► PopupWindow          ├─ CommandPanelWindow
                     │                               ├─ SceneSettingsWindow
                     ├─► ConfigWindow                └─ 各类编辑表单 (x8)
                     └─► TrayMenuWindow
```

#### 核心设计模式

| 模式 | 应用 |
|---|---|
| **MVVM** | Model（数据）→ ViewModel（状态/逻辑）→ View（渲染） |
| **DI / 组合根** | `AppContext` 在启动时装配 services、event bus、logger |
| **基于接口设计** | `IConfigService`、`IIconService`、`IPlugin`、`IControl`、`IRenderLayer`、`IConfigWindow` |
| **事件驱动** | `EventBus` 类型化发布/订阅：`ConfigChanged`、`ShortcutLaunched`、`PopupShown`、`PopupHidden`、`ThemeChanged`、`UiScaleChanged`、`BackgroundStyleChanged`、`AppQuit` |
| **后台线程** | MouseHook、KeyboardHook、MacroRecorder、FolderWatcher 各自独立线程 |
| **窗口消息传递** | 通过 `WM_APP+N` 自定义消息实现跨线程通信 |

---

### 环境要求

- **Windows 10** 或更高版本（x64 或 x86）
- **Visual Studio 2022** (17.0+)，需包含：
  - "使用 C++ 的桌面开发" 工作负载
  - Windows 10 SDK (10.0+)
  - MSVC v143 工具集

无需任何外部库，无需 vcpkg，无需 CMake —— 一切来自 Windows SDK。

---

### 构建

```bash
# 方式一：Visual Studio IDE
#   打开 WinLauncher.sln
#   选择 Release | x64
#   生成 → 生成解决方案

# 方式二：MSBuild 命令行
msbuild WinLauncher.sln /p:Configuration=Release /p:Platform=x64

# 输出: x64/Release/WinLauncher.exe
```

可用配置：

| 配置 | 平台 | 运行时库 |
|---|---|---|
| Debug | x64 | /MTd |
| Debug | Win32 | /MTd |
| Release | x64 | /MT |
| Release | Win32 | /MT |

可执行文件完全自包含 —— 静态链接 CRT，无需安装任何运行时组件。

---

### 使用说明

1. **启动** `WinLauncher.exe` —— 自动最小化到系统托盘
2. **触发弹窗**，使用配置的鼠标手势（默认：左上角）或双击 Alt 键
3. **点击快捷方式** 即可启动
4. **右键托盘图标** 打开上下文菜单 → **"设置"** 进入配置界面
5. **添加快捷方式**，拖放文件/文件夹到设置窗口，或点击"添加"

#### 弹窗快捷键

| 按键 | 操作 |
|---|---|
| `Esc` | 关闭弹窗 |
| `Tab` | 切换搜索栏 |
| `←` / `→` | 切换页面 |
| `Ctrl` + `Tab` | 切换页面（备用） |
| `Ctrl` + `←` / `Ctrl` + `→` | 快速翻页 |

#### 托盘菜单

| 项目 | 操作 |
|---|---|
| 设置 | 打开设置窗口 |
| 暂停 | 暂时禁用弹窗触发 |
| 检查更新 | 检查 GitHub 新版本 |
| 退出 | 退出应用程序 |

---

### 配置说明

配置文件以 UTF-8 INI 格式存储在：

```
%APPDATA%\WinLauncher\config\launcher_config.ini
```

#### 可配置项（通过设置界面）

**触发方式**
- 鼠标手势类型（角落、边缘、按钮）
- 触发灵敏度
- 弹窗位置偏移
- 弹窗对齐模式（相对于光标/触发点）
- 弹窗自动关闭（鼠标离开后延迟关闭）
- 悬停离开延迟（毫秒）
- 固定时允许多开弹窗

**网格布局**
- 列数 (1–12)
- 行数 (1–8)
- 图标大小 (32–256 px)
- 间距、圆角、内边距

**主题**
- 模式：深色 / 浅色
- 材料预设：6 种（`dark`、`light`、`acrylicDark`、`acrylicLight`、`glassDark`、`glassLight`）
- 色彩预设：12 种主题色彩
- 调校：色相、模糊度、不透明度、高亮、亮度、饱和度
- 动画：时长、弹簧刚度
- 硬件加速开关（含崩溃自动回退）

**UI 缩放**
- 80% – 250%，以 10% 步进调整

**系统**
- 开机自启（任务计划程序）
- 自动更新检查频率
- 隐藏托盘图标
- 配置备份与历史管理
- 排序模式（页面内快捷方式排序）

---

### 变量系统

命令与 URL 快捷方式支持运行时变量替换，参见 [`Variables.md`](Variables.md) 完整文档。

支持的变量类别：

| 类别 | 变量 |
|---|---|
| **系统状态** | `{{clipboard}}` `{{date}}` `{{time}}` `{{lan_ip}}` `{{wan_ip}}` |
| **文件选择** | `{{selected_file}}` `{{selected_file_name}}` `{{selected_file_dir}}` `{{selected_file_folder}}` `{{selected_files}}` |
| **应用路径** | `{{app_dir}}` `{{config_dir}}` |
| **交互输入** | `{{input}}` `{{password}}` `{{choose}}` `{{confirm}}` |
| **URL 占位** | `{{url}}` — 在浏览器参数中替换展开后的 URL |
| **引用修饰** | `:q` — shell 感知自动转义（CMD/PowerShell/GitBash） |

---

### 项目结构

```
WinLauncher/
+-- WinLauncher.sln                       # Visual Studio 解决方案
+-- assets/                               # 应用图标 (.ico)
\-- WinLauncher/
    +-- main.cpp                          # WinMain 入口
    +-- resource.h / resource.rc          # Windows 资源
    +-- version.h                         # 版本宏 (0.5.1.7)
    +-- WinLauncher.exe.manifest          # 应用清单 (ComCtl6, Win10)
    |
    +-- App/                              # 应用生命周期
    |   +-- Application.h/.cpp            # 初始化、生命周期、消息循环
    |   +-- AppContext.h                  # 依赖注入容器
    |   +-- AppMessages.h                 # 自定义 WM_APP+N 消息
    |   +-- EventBus.h                    # 发布/订阅事件系统
    |   +-- Logger.h/.cpp                 # 线程安全文件日志
    |   \-- PluginHost.h                  # 插件系统 (IPlugin)
    |
    +-- Model/                            # 数据模型
    |   +-- ShortcutInfo.h                # ShortcutInfo、PopupPage、枚举
    |   \-- AppearanceSettings.h          # 主题与外观配置
    |
    +-- Services/                         # 业务逻辑层
    |   +-- IConfigService.h              # 配置服务接口
    |   +-- IIconService.h                # 图标提取接口
    |   +-- IniConfigRepository.h         # INI 文件配置存储
    |   +-- SystemIconService.h           # 文件/系统图标提取
    |   +-- FolderWatcher.h/.cpp          # ReadDirectoryChangesW 监控
    |   +-- SyncFolderService.h/.cpp      # 文件夹快捷方式加载
    |   +-- FaviconFetcher.h/.cpp         # 网站图标获取
    |   +-- PrivilegeLaunchService.h/.cpp # UAC 提权/降权
    |   +-- EnvironmentDetector.h/.cpp    # Python、Git Bash 检测
    |   +-- FileSelectionService.h/.cpp   # 资源管理器文件捕获
    |   +-- MacroService.h/.cpp           # 宏录制与回放
    |   +-- BatchLaunchService.h/.cpp     # 顺序批量启动
    |   +-- UpdateService.h/.cpp          # GitHub 自动更新
    |   +-- IConfigImportService.h        # 配置导入接口
    |   +-- JsonImportHelper.h            # JSON 配置导入
    |   +-- QuickLauncherConfigImport.h   # QuickLauncher 导入
    |   +-- CommandVariableService.h/.cpp # 运行时变量替换 ({{clipboard}} 等)
    |   +-- AppSceneMatcher.h             # 前台应用匹配与场景识别
    |   \-- ConfigPath.h                  # 配置目录路径解析
    |
    +-- ViewModel/                        # MVVM 视图模型
    |   +-- PopupViewModel.h              # 弹窗状态、页面切换、动画
    |   \-- ConfigViewModel.h             # 配置管理、CRUD
    |
    +-- UI/
    |   +-- Controls/
    |   |   +-- IControl.h                # 控件接口
    |   |   +-- Button.h                  # 按钮控件
    |   |   \-- IconRenderer.h            # 图标渲染辅助
    |   \-- Render/
    |       +-- IRenderLayer.h            # 渲染层接口
    |       +-- BackgroundLayer.h         # 背景渲染
    |       +-- OverlayLayer.h            # 叠加层渲染
    |       \-- Compositor.h              # 图层合成器
    |
    +-- Config/                           # 设置与管理界面
    |   +-- ConfigWindow.h/.cpp           # 主设置窗口
    |   +-- ConfigPage.h                  # 配置页抽象基类
    |   +-- IConfigWindow.h               # 配置窗口接口
    |   +-- CategoryList.h/.cpp           # 分类侧边栏
    |   +-- ShortcutPage.h/.cpp           # 快捷方式管理
    |   +-- SettingsPage.h/.cpp           # 通用设置
    |   +-- SceneSettingsWindow.h/.cpp    # 场景页面应用可见性配置
    |   +-- CommandPanelWindow.h/.cpp     # 命令实时输出面板
    |   +-- ContextMenu.h/.cpp            # 右键菜单
    |   +-- DropDownMenu.h/.cpp           # 下拉菜单
    |   +-- UIStyle.h                     # 主题系统、颜色、字体排版
    |   +-- TextBox.h/.cpp                # 自定义文本输入
    |   +-- ShortcutDialog.h/.cpp         # 添加/编辑快捷方式对话框
    |   +-- ShortcutEditForm.h/.cpp       # 快捷方式编辑表单
    |   +-- HotkeyDialog.h/.cpp           # 热键编辑对话框
    |   +-- HotkeyEditForm.h/.cpp         # 热键编辑表单
    |   +-- UrlDialog.h/.cpp              # URL 编辑对话框
    |   +-- UrlEditForm.h/.cpp            # URL 编辑表单
    |   +-- CommandDialog.h/.cpp          # 命令编辑对话框
    |   +-- CommandEditForm.h/.cpp        # 命令编辑表单
    |   +-- MacroDialog.h/.cpp            # 宏编辑对话框
    |   +-- MacroEditForm.h/.cpp          # 宏编辑表单
    |   +-- BatchLaunchDialog.h/.cpp      # 批量启动对话框
    |   +-- BatchLaunchEditForm.h/.cpp    # 批量启动表单
    |   +-- BuiltinIconDialog.h/.cpp      # 内置图标选择器
    |   +-- SystemIconDialog.h/.cpp       # 系统图标选择器
    |   +-- SystemIconEditForm.h/.cpp     # 系统图标编辑
    |   +-- PromptWindow.h/.cpp           # 提示对话框
    |   +-- ConfirmWindow.h/.cpp          # 确认对话框
    |   \-- WaitWindow.h/.cpp             # 等待/进度对话框
    |
    +-- BaseWindow.h                      # 抽象 Win32 窗口基类
    +-- GlassWindow.h/.cpp                # D2D 毛玻璃窗口
    +-- ShadowWindow.h/.cpp               # 投影窗口 (GDI 模糊)
    +-- PopupWindow.h/.cpp                # 主启动器弹窗
    +-- TrayMenuWindow.h/.cpp             # 系统托盘菜单
    +-- ToastWindow.h/.cpp                # Toast 通知
    +-- KeyboardHook.h/.cpp               # WH_KEYBOARD_LL 钩子
    +-- MouseHook.h/.cpp                  # WH_MOUSE_LL 钩子
    +-- ShortcutManager.h/.cpp            # 遗留快捷方式门面
    +-- AutoStartHelper.h                 # 任务计划程序开机自启
    +-- DpiHelper.h                       # 逐显示器 DPI 辅助
    \-- InputFocusGuard.h                 # 文本输入上下文保护
```

---

### 技术栈

| 类别 | 技术 |
|---|---|
| 语言 | C++17 |
| 编译器 | MSVC v143 (Visual Studio 2022) |
| 构建系统 | MSBuild (.vcxproj) |
| 图形 | Direct2D (D2D1) |
| 文字 | DirectWrite (DWrite) |
| 图像 | Windows Imaging Component (WIC) |
| 桌面 | Win32 Window API、DWM（桌面窗口管理器） |
| 输入 | WH_MOUSE_LL / WH_KEYBOARD_LL 钩子 |
| 计划任务 | Task Scheduler COM API (ITaskService) |
| 链接 | 静态 CRT —— 零运行时依赖 |

---

### 依赖

**零第三方依赖。** 仅使用 Windows SDK 库：

- `comctl32.lib` — 通用控件 v6
- `winmm.lib` — 多媒体（计时、回放时序）
- `shlwapi.lib` — Shell 轻量工具
- `advapi32.lib` — 高级 Windows API（注册表、安全）
- `taskschd.lib` — 任务计划程序
- `ole32.lib` — COM 运行时
- `shell32.lib` — Shell API（文件操作、快捷方式）
- `wtsapi32.lib` — 终端服务（会话检测）

---

### 许可证

本项目为个人工具。除非另有说明，保留所有权利。

---

### 致谢

WinLauncher 是从零开始的 C++ 重写项目，受 QuickLauncher Python 项目 (v1.6.3.6) 启发。它用裸机 Win32 替代了 Python/Electron 层的抽象，以开发效率换取运行时性能、零安装包体积和小于 10MB 的静态二进制文件。

<a id="english"></a>

## English

**WinLauncher** is a native Windows desktop application launcher — fast, lightweight, zero-dependency. Built entirely with Win32 API + Direct2D rendering, it stays in your system tray and pops up a sleek, frosted-glass shortcut panel at your cursor via mouse gesture or keyboard shortcut. Think of it as a personal command palette for your desktop.

> Version **0.5.1.7** | C++17 | MSVC v143 | Windows 10+

---

### Features

#### Core Launcher
- **Popup panel** — a paginated grid of customizable shortcut icons, triggered by configurable mouse gestures
- **Double-Alt trigger** — press Alt twice quickly to show/hide the launcher via global keyboard hook
- **Search mode** — type to filter shortcuts in real time
- **Dock bar** — a persistent row of frequently-used shortcuts pinned at the bottom of the popup
- **Pin to screen** — keep the popup visible even when it loses focus

#### 7 Shortcut Types
| Type | Description |
|---|---|
| **File** | Launch files, folders, executables (auto-resolves `.lnk` shortcuts) |
| **Hotkey** | Simulate keyboard input / key sequences |
| **URL** | Open websites in the default browser |
| **Command** | Run arbitrary commands (supports stdin piping) |
| **Macro** | Record & replay mouse/keyboard macro sequences |
| **Batch Launch** | Execute multiple shortcuts sequentially with configurable delays |
| **System Icon** | Launch system-known executables by registered key |

#### Runtime Variable System
Command/URL shortcuts support **12 runtime variables** resolved at launch:

| Variable | Description |
|---|---|
| `{{clipboard}}` | Current clipboard text content |
| `{{date}}` / `{{time}}` | Current system date/time |
| `{{lan_ip}}` / `{{wan_ip}}` | LAN / WAN IP address (WAN caching) |
| `{{selected_file}}` / `{{selected_files}}` | File(s) selected in Explorer |
| `{{selected_file_name}}` / `{{selected_file_dir}}` / `{{selected_file_folder}}` | Selected file name/directory/parent folder |
| `{{app_dir}}` / `{{config_dir}}` | App directory / config directory |
| `{{input}}` / `{{input:prompt}}` | Modal text input dialog at runtime |
| `{{password}}` / `{{password:prompt}}` | Modal password input (masked with `●`) |
| `{{choose:opt1\|opt2\|...}}` | Dropdown selection list at runtime |
| `{{confirm}}` / `{{confirm:message}}` | Confirmation gate, aborts on cancel |
| `{{url}}` | URL placeholder for custom browser parameters |
| `:q` modifier | Shell-aware auto quoting (CMD/PowerShell/GitBash) |

#### Visual & Theming
- **Frosted glass / Acrylic** — background blur, sheen effect, rounded corners via Direct2D
- **Dark & Light modes** with glass/acrylic window variants
- **6 material presets** — `dark`, `light`, `acrylicDark`, `acrylicLight`, `glassDark`, `glassLight` (incl. Glass 1:1)
- **12 theme color presets** with 6-dimensional HSL color tuning per theme
- **Smooth theme transitions** with easing functions
- **Configurable grid layout** — columns, rows, icon size, gap, corner radius, padding
- **UI scaling** — 80% to 250% in 10% steps
- **Per-monitor DPI awareness** (PMv2)
- **Physics-based spring animation** for open/close transitions

#### Architecture & Extensibility
- **MVVM-like layered architecture** — Model / Services / ViewModel / UI
- **Dependency injection** via `AppContext` with `EventBus`, `Logger`, `PluginHost`
- **Plugin system** — runtime plugin loading with lifecycle hooks
- **Event bus** — pub/sub messaging for decoupled component communication
- **Folder sync** — auto-load shortcuts from designated folders with live `ReadDirectoryChangesW` monitoring
- **Scene pages** — per-page whitelist/blacklist app visibility, shows page only when foreground app matches
- **Command variable service** — embed runtime variables (clipboard, selected files, input dialogs, etc.) in commands/URLs
- **Command output panel** — real-time stdout/stderr display window with copy & clear
- **Config backup & history** — timestamped backups with restore and history browsing
- **Shortcut undo/redo** — undo/redo stack for shortcut CRUD operations
- **Async file selection** — background thread file capture from Explorer, no UI blocking

#### Production-Ready Features
- **System tray** — minimize to tray, restore with click or hotkey
- **Auto-start** — register via Windows Task Scheduler (handles admin/standard user accounts)
- **Privilege management** — launch targets as admin (UAC) or de-elevate when needed
- **High-risk command detection** — auto-detects dangerous commands (`rm -rf`, `format`, etc.) with confirmation
- **GPU crash guard** — automatic fallback to software rendering when D2D crashes
- **Mouse hook debounce** — debounce logic preventing spurious trigger events
- **Auto-update** — check GitHub Releases for new versions
- **Thread-safe logging** — file-based logger with configurable levels
- **Config import** — import from JSON or QuickLauncher (Python) format
- **Toast notifications** — transient on-screen messages

---

### Architecture

```
main.cpp  ──►  Application  ──►  AppContext (DI Container)
                                    │
       ┌────────────────────────────┼────────────────────────────┐
       │                            │                            │
    Model Layer               Services Layer                 ViewModel Layer
    ├─ ShortcutInfo           ├─ IConfigService              ├─ PopupViewModel
    └─ AppearanceSettings     ├─ IIconService                └─ ConfigViewModel
                              ├─ FolderWatcher
                              ├─ MacroService                     │
                              ├─ BatchLaunchService          ┌────┴────┐
                              ├─ UpdateService               │         │
                              ├─ PrivilegeLaunchService   UI Layer   Render
                              ├─ CommandVariableService   ├─ Popup   ├─ Compositor
                              ├─ AppSceneMatcher          ├─ Config  ├─ BgLayer
                              ├─ FileSelectionService     └─ Tray    └─ Overlay
                              └─ ConfigPath                 Menu
                                                             │
UI Window Hierarchy                                  Config Sub-Windows
BaseWindow ──► GlassWindow ──► PopupWindow            ├─ CommandPanelWindow
                     │                                ├─ SceneSettingsWindow
                     ├─► ConfigWindow                 └─ Edit forms (x8)
                     └─► TrayMenuWindow
```

#### Key Design Patterns

| Pattern | Usage |
|---|---|
| **MVVM** | Model (data) → ViewModel (state/logic) → View (rendering) |
| **DI / Composition Root** | `AppContext` wires services, event bus, logger at startup |
| **Interface-based** | `IConfigService`, `IIconService`, `IPlugin`, `IControl`, `IRenderLayer`, `IConfigWindow` |
| **Event-Driven** | `EventBus` with typed pub/sub for `ConfigChanged`, `ShortcutLaunched`, `PopupShown`, `PopupHidden`, `ThemeChanged`, `UiScaleChanged`, `BackgroundStyleChanged`, `AppQuit` |
| **Background Threads** | Dedicated threads for MouseHook, KeyboardHook, MacroRecorder, FolderWatcher |
| **Window Message Passing** | Cross-thread communication via `WM_APP+N` custom messages |

---

### Prerequisites

- **Windows 10** or later (x64 or x86)
- **Visual Studio 2022** (17.0+) with:
  - "Desktop development with C++" workload
  - Windows 10 SDK (10.0+)
  - MSVC v143 toolset

No external libraries, no vcpkg, no CMake — everything comes from the Windows SDK.

---

### Build

```bash
# Option 1: Visual Studio IDE
#   Open WinLauncher.sln
#   Select Release | x64
#   Build → Build Solution

# Option 2: MSBuild command line
msbuild WinLauncher.sln /p:Configuration=Release /p:Platform=x64

# Output: x64/Release/WinLauncher.exe
```

Available configurations:

| Configuration | Platform | RTL |
|---|---|---|
| Debug | x64 | /MTd |
| Debug | Win32 | /MTd |
| Release | x64 | /MT |
| Release | Win32 | /MT |

The executable is self-contained — statically linked CRT, no runtime redistributables needed.

---

### Usage

1. **Launch** `WinLauncher.exe` — it minimizes to the system tray
2. **Trigger the popup** using your configured mouse gesture (default: top-left corner) or double-tap Alt
3. **Click a shortcut** to launch it
4. **Right-click the tray icon** for the context menu → **"Config"** to open settings
5. **Add shortcuts** by drag-and-dropping files/folders into the config window, or clicking "Add"

#### Keyboard Shortcuts (in Popup)

| Key | Action |
|---|---|
| `Esc` | Close popup |
| `Tab` | Toggle search bar |
| `←` / `→` | Switch pages |
| `Ctrl` + `Tab` | Switch pages (alt) |
| `Ctrl` + `←` / `Ctrl` + `→` | Quick page flip |

#### Tray Menu

| Item | Action |
|---|---|
| Config | Open settings window |
| Pause | Temporarily disable popup trigger |
| Check for Updates | Check GitHub for new version |
| Exit | Quit the application |

---

### Configuration

Configuration is stored as UTF-8 INI at:

```
%APPDATA%\WinLauncher\config\launcher_config.ini
```

#### Configurable Settings (via Settings UI)

**Trigger**
- Mouse gesture type (corner, edge, button)
- Trigger sensitivity
- Popup position offset
- Popup align mode (relative to cursor/trigger point)
- Auto-close on mouse leave (configurable delay)
- Hover leave delay (ms)
- Multi-open when pinned

**Grid Layout**
- Columns (1–12)
- Rows (1–8)
- Icon size (32–256 px)
- Gap, corner radius, padding

**Theme**
- Mode: Dark / Light
- Material presets: 6 (`dark`, `light`, `acrylicDark`, `acrylicLight`, `glassDark`, `glassLight`)
- Color presets: 12 theme colors
- Tuning: hue, blur, opacity, highlight, brightness, saturation
- Animation: duration, spring stiffness
- Hardware acceleration toggle (auto-fallback on crash)

**UI Scaling**
- 80% – 250% in 10% increments

**System**
- Auto-start (Task Scheduler)
- Auto-update check frequency
- Hide tray icon
- Config backup & history management
- Sort mode (per-page shortcut ordering)

---

### Variable System

Command and URL shortcuts support runtime variable substitution. See [`Variables.md`](Variables.md) for complete documentation.

Supported variable categories:

| Category | Variables |
|---|---|
| **System State** | `{{clipboard}}` `{{date}}` `{{time}}` `{{lan_ip}}` `{{wan_ip}}` |
| **File Selection** | `{{selected_file}}` `{{selected_file_name}}` `{{selected_file_dir}}` `{{selected_file_folder}}` `{{selected_files}}` |
| **App Paths** | `{{app_dir}}` `{{config_dir}}` |
| **Interactive** | `{{input}}` `{{password}}` `{{choose}}` `{{confirm}}` |
| **URL Placeholder** | `{{url}}` — replaced with expanded URL in browser params |
| **Quote Modifier** | `:q` — shell-aware auto-escaping (CMD/PowerShell/GitBash) |

---

### Project Structure

```
WinLauncher/
+-- WinLauncher.sln                       # Visual Studio solution
+-- assets/                               # App icons (.ico)
\-- WinLauncher/
    +-- main.cpp                          # WinMain entry point
    +-- resource.h / resource.rc          # Windows resources
    +-- version.h                         # Version macros (0.5.1.7)
    +-- WinLauncher.exe.manifest          # App manifest (ComCtl6, Win10)
    |
    +-- App/                              # Application lifecycle
    |   +-- Application.h/.cpp            # Init, lifecycle, message loop
    |   +-- AppContext.h                  # DI container
    |   +-- AppMessages.h                 # Custom WM_APP+N messages
    |   +-- EventBus.h                    # Pub/sub event system
    |   +-- Logger.h/.cpp                 # Thread-safe file logger
    |   \-- PluginHost.h                  # Plugin system (IPlugin)
    |
    +-- Model/                            # Data models
    |   +-- ShortcutInfo.h                # ShortcutInfo, PopupPage, enums
    |   \-- AppearanceSettings.h          # Theme & appearance config
    |
    +-- Services/                         # Business logic layer
    |   +-- IConfigService.h              # Config service interface
    |   +-- IIconService.h                # Icon extraction interface
    |   +-- IniConfigRepository.h         # INI file config store
    |   +-- SystemIconService.h           # File/system icon extraction
    |   +-- FolderWatcher.h/.cpp          # ReadDirectoryChangesW monitor
    |   +-- SyncFolderService.h/.cpp      # Folder shortcut loading
    |   +-- FaviconFetcher.h/.cpp         # Website favicon fetcher
    |   +-- PrivilegeLaunchService.h/.cpp # UAC admin/de-elevation
    |   +-- EnvironmentDetector.h/.cpp    # Python, Git Bash detection
    |   +-- FileSelectionService.h/.cpp   # Explorer file capture
    |   +-- MacroService.h/.cpp           # Macro record & playback
    |   +-- BatchLaunchService.h/.cpp     # Sequential batch launch
    |   +-- UpdateService.h/.cpp          # GitHub auto-update
    |   +-- IConfigImportService.h        # Config import interface
    |   +-- JsonImportHelper.h            # JSON config import
    |   +-- QuickLauncherConfigImport.h   # QuickLauncher import
    |   +-- CommandVariableService.h/.cpp # Runtime variable substitution ({{clipboard}}, etc.)
    |   +-- AppSceneMatcher.h             # Foreground app matching & scene detection
    |   \-- ConfigPath.h                  # Config directory path resolution
    |
    +-- ViewModel/                        # MVVM ViewModels
    |   +-- PopupViewModel.h              # Popup state, page switching, animation
    |   \-- ConfigViewModel.h             # Config management, CRUD
    |
    +-- UI/
    |   +-- Controls/
    |   |   +-- IControl.h                # Control interface
    |   |   +-- Button.h                  # Button control
    |   |   \-- IconRenderer.h            # Icon rendering helper
    |   \-- Render/
    |       +-- IRenderLayer.h            # Render layer interface
    |       +-- BackgroundLayer.h         # Background rendering
    |       +-- OverlayLayer.h            # Overlay rendering
    |       \-- Compositor.h              # Layer compositor
    |
    +-- Config/                           # Settings & management UI
    |   +-- ConfigWindow.h/.cpp           # Main settings window
    |   +-- ConfigPage.h                  # Config page abstract base class
    |   +-- IConfigWindow.h               # Config window interface
    |   +-- CategoryList.h/.cpp           # Category sidebar
    |   +-- ShortcutPage.h/.cpp           # Shortcut management
    |   +-- SettingsPage.h/.cpp           # General settings
    |   +-- SceneSettingsWindow.h/.cpp    # Scene page app visibility config
    |   +-- CommandPanelWindow.h/.cpp     # Real-time command output panel
    |   +-- ContextMenu.h/.cpp            # Right-click menu
    |   +-- DropDownMenu.h/.cpp           # Dropdown menu
    |   +-- UIStyle.h                     # Theme system, colors, typography
    |   +-- TextBox.h/.cpp                # Custom text input
    |   +-- ShortcutDialog.h/.cpp         # Add/edit shortcut dialog
    |   +-- ShortcutEditForm.h/.cpp       # Shortcut editing form
    |   +-- HotkeyDialog.h/.cpp           # Hotkey editing dialog
    |   +-- HotkeyEditForm.h/.cpp         # Hotkey editing form
    |   +-- UrlDialog.h/.cpp              # URL editing dialog
    |   +-- UrlEditForm.h/.cpp            # URL editing form
    |   +-- CommandDialog.h/.cpp          # Command editing dialog
    |   +-- CommandEditForm.h/.cpp        # Command editing form
    |   +-- MacroDialog.h/.cpp            # Macro editing dialog
    |   +-- MacroEditForm.h/.cpp          # Macro editing form
    |   +-- BatchLaunchDialog.h/.cpp      # Batch launch dialog
    |   +-- BatchLaunchEditForm.h/.cpp    # Batch launch form
    |   +-- BuiltinIconDialog.h/.cpp      # Built-in icon picker
    |   +-- SystemIconDialog.h/.cpp       # System icon picker
    |   +-- SystemIconEditForm.h/.cpp     # System icon editing
    |   +-- PromptWindow.h/.cpp           # Prompt dialog
    |   +-- ConfirmWindow.h/.cpp          # Confirmation dialog
    |   \-- WaitWindow.h/.cpp             # Wait/progress dialog
    |
    +-- BaseWindow.h                      # Abstract Win32 window base
    +-- GlassWindow.h/.cpp                # D2D frosted glass window
    +-- ShadowWindow.h/.cpp               # Drop shadow (GDI blur)
    +-- PopupWindow.h/.cpp                # Main launcher popup
    +-- TrayMenuWindow.h/.cpp             # System tray menu
    +-- ToastWindow.h/.cpp                # Toast notifications
    +-- KeyboardHook.h/.cpp               # WH_KEYBOARD_LL hook
    +-- MouseHook.h/.cpp                  # WH_MOUSE_LL hook
    +-- ShortcutManager.h/.cpp            # Legacy shortcut facade
    +-- AutoStartHelper.h                 # Task Scheduler auto-start
    +-- DpiHelper.h                       # Per-monitor DPI helpers
    \-- InputFocusGuard.h                 # Text input context guard
```

---

### Technology Stack

| Category | Technology |
|---|---|
| Language | C++17 |
| Compiler | MSVC v143 (Visual Studio 2022) |
| Build System | MSBuild (.vcxproj) |
| Graphics | Direct2D (D2D1) |
| Text | DirectWrite (DWrite) |
| Imaging | Windows Imaging Component (WIC) |
| Desktop | Win32 Window API, DWM (Desktop Window Manager) |
| Input | WH_MOUSE_LL / WH_KEYBOARD_LL hooks |
| Scheduling | Task Scheduler COM API (ITaskService) |
| Linking | Static CRT — zero runtime dependencies |

---

### Dependencies

**Zero third-party dependencies.** Only Windows SDK libraries:

- `comctl32.lib` — Common Controls v6
- `winmm.lib` — Multimedia (time, playback timing)
- `shlwapi.lib` — Shell Lightweight Utility
- `advapi32.lib` — Advanced Windows API (registry, security)
- `taskschd.lib` — Task Scheduler
- `ole32.lib` — COM runtime
- `shell32.lib` — Shell API (file operations, shortcuts)
- `wtsapi32.lib` — Terminal Services (session detection)

---

### License

This project is a personal tool. All rights reserved unless otherwise specified.

---

### Acknowledgements

WinLauncher is a from-scratch C++ rewrite, inspired by the QuickLauncher Python project (v1.6.3.6). It replaces Python/Electron-level abstractions with bare-metal Win32, trading development velocity for runtime performance, zero install footprint, and a sub-10MB static binary.

---
