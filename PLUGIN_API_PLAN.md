# WinLauncher 插件 API 接口计划书

> 本文档面向插件开发者，完整描述 WinLauncher 插件系统当前已有的接口能力及后续演进方向。  
> 当前状态以 `WLHostApiV1` (ABI v1) 为准，扩展接口已追加在结构体末尾，插件应通过 `size` 字段判断 Host 是否支持对应函数。

---

## 目录

- [一、架构约定](#一架构约定)
- [二、插件生命周期入口](#二插件生命周期入口)
- [三、当前 Host API (WLHostApiV1)](#三当前-host-api-wlhostapiv1)
- [四、已实现扩展接口](#四已实现扩展接口)
  - [4.1 输入交互类](#41-输入交互类)
  - [4.2 文件选择类](#42-文件选择类)
  - [4.3 通知弹窗类](#43-通知弹窗类)
    - [4.3.1 消息通知](#431-消息通知)
    - [4.3.2 加载与进度弹窗](#432-加载与进度弹窗)
    - [4.3.3 结果回显面板](#433-结果回显面板)
  - [4.4 网络类](#44-网络类)
  - [4.5 进程执行类](#45-进程执行类)
  - [4.6 UI 设置类](#46-ui-设置类)
- [五、权限模型](#五权限模型)
- [六、字符串结果协议](#六字符串结果协议)
- [七、搜索与 Slash 命令](#七搜索与-slash-命令)
- [八、插件清单 manifest](#八插件清单-manifest)
- [九、版本兼容策略](#九版本兼容策略)

---

## 一、架构约定

### 1.1 技术基线

- **语言**: C++17，插件 DLL 通过纯 C ABI 导出
- **平台**: Windows 10+ x64
- **UI 框架**: 主程序使用 Direct2D/DWrite 自绘；插件可使用系统原生 HWND 弹窗或主程序提供的 Host API 对话框
- **字符集**: UTF-16 LE (`wchar_t`)
- **调用约定**: `__stdcall`（宏 `WL_CALL`）

### 1.2 命名规则

| 类别 | 前缀 | 示例 |
|------|------|------|
| 结构体 | `WL` + PascalCase + `V1` | `WLHostApiV1` |
| 函数指针 | camelCase | `readClipboardText` |
| 导出函数 | `WinLauncherPlugin_` | `WinLauncherPlugin_Create` |
| 命令 ID | `plugin_id.command_name` | `wl.text_tools.base64` |

### 1.3 数据传递规则

- **字符串**: 始终 UTF-16、以 `const wchar_t*` 传递，调用方管理生命周期
- **输出字符串**: 使用两阶段 `WLStringResultV1` 协议（先探长度、再填充）
- **不暴露 C++ 对象**: 公共 ABI 不暴露 `std::string`、`std::vector`、虚表
- **结构体版本化**: 所有结构体第一个字段为 `uint32_t size`，实现运行时版本检测

---

## 二、插件生命周期入口

每个插件 DLL 必须导出以下三个函数：

```cpp
// 返回当前编译的 ABI 版本号
WL_EXPORT uint32_t WL_CALL WinLauncherPlugin_GetAbiVersion();

// 创建插件实例
// host: 主程序传入的 Host API 函数表（插件保存此指针）
// outInstance: 插件分配并填充的实例表
// 返回 true 表示成功
WL_EXPORT bool WL_CALL WinLauncherPlugin_Create(
    const WLHostApiV1* host,
    WLPluginInstanceV1** outInstance);

// 销毁插件实例，插件自行释放所有资源
WL_EXPORT void WL_CALL WinLauncherPlugin_Destroy(
    WLPluginInstanceV1* instance);
```

### 插件实例函数表 (WLPluginInstanceV1)

```cpp
struct WLPluginInstanceV1
{
    uint32_t size;                    // sizeof(WLPluginInstanceV1)
    void* userData;                   // 插件私有数据指针

    bool (WL_CALL* onLoad)(void* userData);
    void (WL_CALL* onUnload)(void* userData);

    // 执行普通命令（非 / 前缀）
    bool (WL_CALL* executeCommand)(void* userData,
        const WLCommandContextV1* context,
        WLStringResultV1* outMessage);

    // 执行 / 命令
    bool (WL_CALL* executeSlashCommand)(void* userData,
        const WLSlashCommandContextV1* context,
        WLStringResultV1* outMessage);

    // 弹窗显示/隐藏事件
    void (WL_CALL* onPopupShown)(void* userData);
    void (WL_CALL* onPopupHidden)(void* userData);

    // 动态搜索源（可选）
    bool (WL_CALL* search)(void* userData,
        const WLSearchRequestV1* request,
        WLSearchResponseV1* response);
};
```

### 生命周期时序

```
WinLauncherPlugin_Create()
    ↓
onLoad()                          ← 命令注册（registerCommand / registerSlashCommand）
    ↓
[用户交互: search / execute / popup 事件]
    ↓
onUnload()                        ← 清理资源
    ↓
WinLauncherPlugin_Destroy()
```

---

## 三、当前 Host API (WLHostApiV1)

插件通过 `Create` 函数获得 `WLHostApiV1` 指针，可调用以下方法：

```cpp
struct WLHostApiV1
{
    uint32_t size;
    void* hostContext;

    // ── 命令注册 ────────────────────────────────────────
    bool (WL_CALL* registerCommand)(void*, const WLCommandDescriptorV1*);
    bool (WL_CALL* registerSlashCommand)(void*, const WLSlashCommandDescriptorV1*);

    // ── 日志 ────────────────────────────────────────────
    // 权限: log.write（默认启用）
    void (WL_CALL* log)(void*, const wchar_t* message);

    // ── 信息查询 ────────────────────────────────────────
    // 权限: app.info（默认启用）
    bool (WL_CALL* getDataDirectory)(void*, wchar_t* buffer, uint32_t len, uint32_t* required);
    bool (WL_CALL* getAppVersion)(void*, wchar_t* buffer, uint32_t len, uint32_t* required);

    // ── 剪贴板 ──────────────────────────────────────────
    // 权限: clipboard.read / clipboard.write
    bool (WL_CALL* readClipboardText)(void*, WLStringResultV1* outText);
    bool (WL_CALL* writeClipboardText)(void*, const wchar_t* text);

    // ── URL/文件打开 ────────────────────────────────────
    // 权限: open.url / open.file
    bool (WL_CALL* openUrl)(void*, const wchar_t* url);
    bool (WL_CALL* openFile)(void*, const wchar_t* path);

    // ── 文件读写（相对于插件数据目录）────────────────────
    // 权限: file.read / file.write
    bool (WL_CALL* readTextFile)(void*, const wchar_t* relativePath, WLStringResultV1* outText);
    bool (WL_CALL* writeTextFile)(void*, const wchar_t* relativePath, const wchar_t* text);

    // ── 插件配置 ────────────────────────────────────────
    // 权限: plugin.config.read / plugin.config.write
    bool (WL_CALL* getPluginConfig)(void*, const wchar_t* key, const wchar_t* defaultVal, WLStringResultV1* outVal);
    bool (WL_CALL* setPluginConfig)(void*, const wchar_t* key, const wchar_t* value);

    // ── UI / 文件选择 / 通知 / 网络 / 进程 / 屏幕信息扩展 ─────────────
    // 这些函数追加在结构体末尾，插件调用前应检查 host->size。
    bool (WL_CALL* showInputDialog)(void*, const wchar_t* title, const wchar_t* prompt, const wchar_t* defaultText, WLStringResultV1* outText);
    bool (WL_CALL* showPasswordDialog)(void*, const wchar_t* title, const wchar_t* prompt, WLStringResultV1* outText);
    bool (WL_CALL* showChooseDialog)(void*, const wchar_t* title, const wchar_t* prompt, const wchar_t* options, WLStringResultV1* outSelected);
    bool (WL_CALL* showConfirmDialog)(void*, const wchar_t* title, const wchar_t* message);
    bool (WL_CALL* showFilePicker)(void*, const wchar_t* title, bool multiSelect, const wchar_t* filterPattern, bool onlyFolders, WLStringResultV1* outPaths);
    bool (WL_CALL* showNotificationToaster)(void*, const wchar_t* title, const wchar_t* message, const wchar_t* type, uint32_t durationMs);
    bool (WL_CALL* showMessageBox)(void*, const wchar_t* title, const wchar_t* message, const wchar_t* iconType, const wchar_t* buttons, WLStringResultV1* outResult);
    bool (WL_CALL* showBalloonTip)(void*, const wchar_t* title, const wchar_t* message, const wchar_t* iconType, uint32_t durationMs);
    bool (WL_CALL* showLoadingDialog)(void*, const wchar_t* message, bool cancelable, uint64_t* outHandle);
    bool (WL_CALL* updateLoadingMessage)(void*, uint64_t handle, const wchar_t* newMessage);
    bool (WL_CALL* hideLoadingDialog)(void*, uint64_t handle);
    bool (WL_CALL* showProgressDialog)(void*, const wchar_t* title, const wchar_t* message, uint64_t total, bool cancelable, uint64_t* outHandle);
    bool (WL_CALL* updateProgress)(void*, uint64_t handle, uint64_t current, const wchar_t* statusMessage);
    bool (WL_CALL* hideProgressDialog)(void*, uint64_t handle);
    bool (WL_CALL* isDialogCancelled)(void*, uint64_t handle, bool* outCancelled);
    bool (WL_CALL* showResultInPanel)(void*, const wchar_t* title, const wchar_t* content, const wchar_t* contentType);
    bool (WL_CALL* httpRequest)(void*, const wchar_t* method, const wchar_t* url, const wchar_t* headers, const wchar_t* body, uint32_t timeoutMs, WLStringResultV1* outResponse);
    bool (WL_CALL* runProcess)(void*, const wchar_t* command, const wchar_t* workingDir, bool captureOutput, uint32_t timeoutMs, WLStringResultV1* outOutput, uint32_t* outExitCode);
    bool (WL_CALL* getScreenInfo)(void*, uint32_t* outWidth, uint32_t* outHeight, uint32_t* outDpi, WLStringResultV1* outTheme);
};
```

### 当前接口覆盖情况

| 接口 | 当前状态 | 权限 | 插件使用情况 |
|------|---------|------|------------|
| `registerCommand` | ✅ 已实现 | — | 清单声明为主 |
| `registerSlashCommand` | ✅ 已实现 | — | 清单声明为主 |
| `log` | ✅ 已实现 | `log.write` | hello_world 示例 |
| `getDataDirectory` | ✅ 已实现 | — | 未使用 |
| `getAppVersion` | ✅ 已实现 | `app.info` | 未使用 |
| `readClipboardText` | ✅ 已实现 | `clipboard.read` | text_tools |
| `writeClipboardText` | ✅ 已实现 | `clipboard.write` | 5 个插件使用 |
| `openUrl` | ✅ 已实现 | `open.url` | 未使用 |
| `openFile` | ✅ 已实现 | `open.file` | 未使用 |
| `readTextFile` | ✅ 已实现 | `file.read` | 未使用 |
| `writeTextFile` | ✅ 已实现 | `file.write` | 未使用 |
| `getPluginConfig` | ✅ 已实现 | `plugin.config.read` | hello_world 示例 |
| `setPluginConfig` | ✅ 已实现 | `plugin.config.write` | 未使用 |
| `showInputDialog` | ✅ 已实现 | `ui.input` | text_tools / time_tools / network_tools |
| `showPasswordDialog` | ✅ 已实现 | `ui.input` | SDK 可用 |
| `showChooseDialog` | ✅ 已实现 | `ui.input` | SDK 可用 |
| `showConfirmDialog` | ✅ 已实现 | `ui.input` | SDK 可用 |
| `showFilePicker` | ✅ 已实现 | `ui.filepick` | SDK 可用 |
| `showNotificationToaster` | ✅ 已实现 | `ui.notify` | SDK 可用 |
| `showMessageBox` | ✅ 已实现 | `ui.notify` | SDK 可用 |
| `showBalloonTip` | ✅ 已实现 | `ui.notify` | 复用通知后备 |
| `showLoadingDialog` / `updateLoadingMessage` / `hideLoadingDialog` | ✅ 已实现 | `ui.notify` | SDK 可用 |
| `showProgressDialog` / `updateProgress` / `hideProgressDialog` / `isDialogCancelled` | ✅ 已实现 | `ui.notify` | SDK 可用 |
| `showResultInPanel` | ✅ 已实现 | `ui.notify` | 消息框后备 |
| `httpRequest` | ✅ 已实现 | `network.request` | SDK 可用 |
| `runProcess` | ✅ 已实现 | `process.run` | SDK 可用 |
| `getScreenInfo` | ✅ 已实现 | `app.info` | SDK 可用 |

---

## 四、已实现扩展接口

以下接口已通过 `WLHostApiV1` 末尾追加函数指针提供。  
所有扩展接口均遵循现有权限模型，无权限时返回错误码或空值；插件调用前必须通过 `host->size` 检查函数指针是否存在。

> 当前 UI 后备说明：输入、密码、选择和确认接口使用主程序自绘 PromptWindow；文件选择使用系统文件选择器；toast、加载、进度和结果回显目前提供轻量后备显示，后续可升级为专用自绘面板而不改变 ABI。

### 4.1 输入交互类

对应 Variables.md 中的 `{{input}}`、`{{password}}`、`{{choose}}`、`{{confirm}}`。

#### showInputDialog

弹出文本输入对话框。

```cpp
// 弹出模态文本输入框
// title:      窗口标题
// prompt:     提示文字（显示在输入框上方）
// defaultText: 默认填充文本，可为空
// outText:    用户输入的文字（用户取消时为空字符串）
// 返回: true 表示获取到输入, false 表示用户取消或无权限
bool (WL_CALL* showInputDialog)(
    void* hostContext,
    const wchar_t* title,
    const wchar_t* prompt,
    const wchar_t* defaultText,
    WLStringResultV1* outText);
```

**权限**: `ui.input`（默认启用）

**典型场景**:
```
/text_tools /base64 → 弹出 "Enter text to encode" → 返回编码结果
/network_tools /ping → 弹出 "Enter hostname" → 返回 ping 结果
```

#### showPasswordDialog

弹出密码输入框（字符显示为 `●`）。

```cpp
// 弹出密码输入框
// title / prompt: 同上
// outText:    用户输入的密码原文（不落盘，不缓存）
// 返回: true = 获取到输入, false = 取消
bool (WL_CALL* showPasswordDialog)(
    void* hostContext,
    const wchar_t* title,
    const wchar_t* prompt,
    WLStringResultV1* outText);
```

**权限**: `ui.input`（默认启用）

#### showChooseDialog

弹出下拉选项列表。

```cpp
// 弹出选项选择框
// title:      窗口标题
// prompt:     提示文字
// options:    以 '\n' 分隔的选项列表
// outSelected: 用户选中的选项文本
// 返回: true = 选中, false = 取消
bool (WL_CALL* showChooseDialog)(
    void* hostContext,
    const wchar_t* title,
    const wchar_t* prompt,
    const wchar_t* options,
    WLStringResultV1* outSelected);
```

**权限**: `ui.input`（默认启用）

#### showConfirmDialog

弹出确认/取消对话框。

```cpp
// 弹出确认对话框
// title:    窗口标题
// message:  提示消息
// 返回: true = 用户确认, false = 用户取消
bool (WL_CALL* showConfirmDialog)(
    void* hostContext,
    const wchar_t* title,
    const wchar_t* message);
```

**权限**: `ui.input`（默认启用）

---

### 4.2 文件选择类

对应 Variables.md 中的 `{{selected_file}}`、`{{selected_files}}`。

#### showFilePicker

弹出文件选择对话框。

```cpp
// 弹出文件选择器
// title:         窗口标题
// multiSelect:   true = 允许多选
// filterPattern: 文件类型过滤，如 "*.txt;*.md" 或 "" 表示所有文件
// onlyFolders:   true = 选择文件夹而非文件
// outPaths:      选中的路径（多个路径以 '\n' 分隔；多选为空时为空字符串）
// 返回: true = 选中, false = 取消
bool (WL_CALL* showFilePicker)(
    void* hostContext,
    const wchar_t* title,
    bool multiSelect,
    const wchar_t* filterPattern,
    bool onlyFolders,
    WLStringResultV1* outPaths);
```

**权限**: `ui.filepick`（默认启用）

**典型场景**:
```
/file_tools /filehash → 使用 WinLauncher 捕获到的选中文件 → 计算哈希
```

---

### 4.3 通知弹窗类

通知弹窗类接口覆盖插件运行时的各种用户可见反馈场景：
消息通知（toast / balloon / 消息框）、加载/进度弹窗（不可取消 / 可取消）、结果回显面板。

#### 4.3.1 消息通知

##### showNotificationToaster

弹出非阻塞通知 toast（右上角滑入，自动消失）。

```cpp
// 弹出非阻塞通知 toast
// title:      通知标题
// message:    通知内容
// type:       "info" | "success" | "warning" | "error"
// durationMs: 显示毫秒数，0 = 默认 3000ms
// 返回: true = 显示成功
bool (WL_CALL* showNotificationToaster)(
    void* hostContext,
    const wchar_t* title,
    const wchar_t* message,
    const wchar_t* type,
    uint32_t durationMs);
```

**权限**: `ui.notify`（默认启用）

**典型场景**:
```
插件搜索源索引完成 → "索引完成，共 120 条结果" (success, 3000ms)
网络请求失败 → "网络连接超时，请检查网络" (error, 5000ms)
```

##### showMessageBox

弹出模态消息框（阻塞，等待用户点击按钮后返回）。

```cpp
// 弹出模态消息框
// title:    窗口标题
// message:  消息内容（支持 '\n' 换行）
// iconType: "none" | "info" | "warning" | "error" | "question"
// buttons:  "ok" | "okcancel" | "yesno" | "yesnocancel" | "retrycancel" | "abortretryignore"
// outResult: 用户点击的按钮文本 "ok" | "cancel" | "yes" | "no" | "retry" | "abort" | "ignore"
// 返回: true = 用户已响应, false = 错误
bool (WL_CALL* showMessageBox)(
    void* hostContext,
    const wchar_t* title,
    const wchar_t* message,
    const wchar_t* iconType,
    const wchar_t* buttons,
    WLStringResultV1* outResult);
```

**权限**: `ui.notify`（默认启用）

**典型场景**:
```
删除文件前确认 → "确定要删除 3 个文件吗？" (warning, yesno)
操作失败重试 → "文件读取失败，是否重试？" (error, retrycancel)
```

##### showBalloonTip

弹出系统托盘气泡提示（仅在 WinLauncher 有托盘图标时有效）。

```cpp
// 弹出系统托盘气泡提示
// title:   气泡标题
// message: 气泡内容
// iconType: "none" | "info" | "warning" | "error"
// durationMs: 显示毫秒数，0 = 系统默认（约 10s）
// 返回: true = 成功入队, false = 无托盘或失败
bool (WL_CALL* showBalloonTip)(
    void* hostContext,
    const wchar_t* title,
    const wchar_t* message,
    const wchar_t* iconType,
    uint32_t durationMs);
```

**权限**: `ui.notify`（默认启用）

#### 4.3.2 加载与进度弹窗

##### showLoadingDialog

弹出不确定进度（旋转指示器）的加载弹窗。

```cpp
// 弹出加载弹窗（不确定进度）
// message:   加载提示文字
// cancelable: true = 显示取消按钮，用户可点击取消
// outHandle: 返回弹窗句柄（用于后续关闭或更新）
// 返回: true = 弹窗已显示
bool (WL_CALL* showLoadingDialog)(
    void* hostContext,
    const wchar_t* message,
    bool cancelable,
    uint64_t* outHandle);
```

##### updateLoadingMessage

```cpp
// 更新加载弹窗的提示文字
// handle:     showLoadingDialog 返回的句柄
// newMessage: 新的提示文字
// 返回: true = 更新成功
bool (WL_CALL* updateLoadingMessage)(
    void* hostContext,
    uint64_t handle,
    const wchar_t* newMessage);
```

##### hideLoadingDialog

```cpp
// 关闭加载弹窗
// handle: showLoadingDialog 返回的句柄
// 返回: true = 关闭成功
bool (WL_CALL* hideLoadingDialog)(
    void* hostContext,
    uint64_t handle);
```

##### showProgressDialog

弹出确定进度（百分比进度条 + 状态文字）的进度弹窗。

```cpp
// 弹出进度弹窗（确定进度）
// title:      弹窗标题
// message:    初始提示文字
// total:      总工作量（如文件总数、字节总数），必须 >= 1
// cancelable: true = 显示取消按钮
// outHandle:  返回弹窗句柄
// 返回: true = 弹窗已显示
bool (WL_CALL* showProgressDialog)(
    void* hostContext,
    const wchar_t* title,
    const wchar_t* message,
    uint64_t total,
    bool cancelable,
    uint64_t* outHandle);
```

##### updateProgress

```cpp
// 更新进度弹窗的当前进度
// handle:        showProgressDialog 返回的句柄
// current:       当前已完成的工作量（0 ~ total）
// statusMessage: 新的状态文字（如 "正在处理: file_42.txt (128 KB)"），可为 nullptr 表示不更新
// 返回: true = 更新成功
bool (WL_CALL* updateProgress)(
    void* hostContext,
    uint64_t handle,
    uint64_t current,
    const wchar_t* statusMessage);
```

##### hideProgressDialog

```cpp
// 关闭进度弹窗
// handle: showProgressDialog 返回的句柄
// 返回: true = 关闭成功
bool (WL_CALL* hideProgressDialog)(
    void* hostContext,
    uint64_t handle);
```

##### isDialogCancelled

```cpp
// 检查加载/进度弹窗是否被用户取消
// handle:       showLoadingDialog 或 showProgressDialog 返回的句柄
// outCancelled: true = 用户已点击取消按钮
// 返回: true = 查询成功
bool (WL_CALL* isDialogCancelled)(
    void* hostContext,
    uint64_t handle,
    bool* outCancelled);
```

**权限**: 以上所有加载/进度接口均为 `ui.notify`（默认启用）

**不确定进度使用模式**:
```cpp
uint64_t handle = 0;
host->showLoadingDialog(host->hostContext, L"正在连接服务器...", true, &handle);

// 在后台线程中执行耗时操作
host->updateLoadingMessage(host->hostContext, handle, L"正在下载数据...");
DownloadData();

host->updateLoadingMessage(host->hostContext, handle, L"正在解析...");
ParseData();

// 定期检查取消
bool cancelled = false;
host->isDialogCancelled(host->hostContext, handle, &cancelled);
if (cancelled) goto cleanup;

host->hideLoadingDialog(host->hostContext, handle);
```

**确定进度使用模式**:
```cpp
uint64_t handle = 0;
host->showProgressDialog(host->hostContext, L"批量处理文件",
    L"准备中...", files.size(), true, &handle);

for (size_t i = 0; i < files.size(); ++i)
{
    // 检查用户是否取消
    bool cancelled = false;
    host->isDialogCancelled(host->hostContext, handle, &cancelled);
    if (cancelled) break;

    // 更新进度和状态
    wchar_t status[256];
    swprintf_s(status, L"正在处理: %s (%zu/%zu)",
        files[i].c_str(), i + 1, files.size());
    host->updateProgress(host->hostContext, handle, i + 1, status);

    ProcessFile(files[i]);
}

host->hideProgressDialog(host->hostContext, handle);
```

#### 4.3.3 结果回显面板

##### showResultInPanel

将命令执行结果回显到主窗口的结果面板。

```cpp
// 将格式化的结果内容回显到主窗口的结果面板
// title:       结果面板标题（如 "File Hash Result"）
// content:     结果内容（支持 '\n' 换行的纯文本）
// contentType: "text" | "json" | "markdown"
// 返回: true = 回显成功
bool (WL_CALL* showResultInPanel)(
    void* hostContext,
    const wchar_t* title,
    const wchar_t* content,
    const wchar_t* contentType);
```

**权限**: `ui.notify`（默认启用）

---

### 4.4 网络类

#### httpRequest

发起 HTTP/HTTPS 请求。

```cpp
// 发起 HTTP 请求
// method:  "GET" | "POST" | "PUT" | "DELETE" | ...
// url:     请求地址
// headers: 请求头，每行一个 "Key: Value"，以 '\n' 分隔
// body:    请求体（GET 为空）
// timeoutMs: 超时毫秒数（0 = 默认 30s）
// outResponse: 响应内容（原始文本）
// 返回: true = 请求完成（含 HTTP 错误码）, false = 网络错误或权限拒绝
bool (WL_CALL* httpRequest)(
    void* hostContext,
    const wchar_t* method,
    const wchar_t* url,
    const wchar_t* headers,
    const wchar_t* body,
    uint32_t timeoutMs,
    WLStringResultV1* outResponse);
```

**权限**: `network.request`

**说明**: 当前 `network_tools` 插件直接调用 WinHTTP 和 Winsock API，实际上是绕过了 Host API 权限控制。引入 `network.request` 权限后，Host API 可统一管理所有网络请求并在设置页展示。

---

### 4.5 进程执行类

#### runProcess

执行外部进程。

```cpp
// 执行外部进程
// command:  完整命令行（含参数）
// workingDir: 工作目录（空 = 插件数据目录）
// captureOutput: true = 捕获 stdout/stderr
// timeoutMs: 超时毫秒数（0 = 不限）
// outOutput: 进程输出（仅当 captureOutput=true 时填充）
// outExitCode: 进程退出码
// 返回: true = 进程成功启动, false = 失败
bool (WL_CALL* runProcess)(
    void* hostContext,
    const wchar_t* command,
    const wchar_t* workingDir,
    bool captureOutput,
    uint32_t timeoutMs,
    WLStringResultV1* outOutput,
    uint32_t* outExitCode);
```

**权限**: `process.run`

**安全**: 命令执行前需经高风险命令检测（同 Variables.md 中的 `{{confirm}}` 机制），危险操作弹确认框。

---

### 4.6 UI 设置类

#### getScreenInfo

获取显示设备信息。

```cpp
// 获取当前屏幕信息
// outWidth:   屏幕宽度（像素）
// outHeight:  屏幕高度（像素）
// outDpi:     当前 DPI
// outTheme:   "light" | "dark"
// 返回: true = 成功
bool (WL_CALL* getScreenInfo)(
    void* hostContext,
    uint32_t* outWidth,
    uint32_t* outHeight,
    uint32_t* outDpi,
    WLStringResultV1* outTheme);
```

**权限**: `app.info`（默认启用）

---

## 五、权限模型

### 5.1 权限清单

| 权限 | 级别 | 涉及接口 | 说明 |
|------|------|----------|------|
| `app.info` | 基础 | `getAppVersion`, `getDataDirectory`, `getScreenInfo` | 默认启用 |
| `log.write` | 基础 | `log` | 默认启用 |
| `plugin.config.read` | 基础 | `getPluginConfig` | 默认启用 |
| `plugin.config.write` | 基础 | `setPluginConfig` | 需用户确认 |
| `clipboard.read` | 中级 | `readClipboardText` | 需用户确认 |
| `clipboard.write` | 中级 | `writeClipboardText` | 需用户确认 |
| `open.url` | 中级 | `openUrl` | 需用户确认 |
| `open.file` | 中级 | `openFile` | 需用户确认 |
| `file.read` | 中级 | `readTextFile` | 需用户确认 |
| `file.write` | 中级 | `writeTextFile` | 需用户确认 |
| `ui.input` | 基础 | `showInput/Pwd/Choose/Confirm` | 默认启用 |
| `ui.filepick` | 基础 | `showFilePicker` | 默认启用 |
| `ui.notify` | 基础 | `showNotification`, `showMessageBox` | 默认启用 |
| `network.request` | 高级 | `httpRequest` | 需用户确认 |
| `process.run` | 高级 | `runProcess` | 需用户确认 |

### 5.2 权限级别说明

| 级别 | 默认行为 | 典型场景 |
|------|----------|----------|
| 基础 | 自动授予 | 日志、配置、应用信息、UI 交互 |
| 中级 | 首次使用弹确认 | 剪贴板、文件读写、打开链接 |
| 高级 | 安装时明确授权 | 网络请求、进程执行 |

### 5.3 无权限行为

| 方法 | 无权限返回 |
|------|-----------|
| `readClipboardText` | `outText` 为空，返回 `false` |
| `writeClipboardText` | 返回 `false` |
| `readTextFile` | `outText` 为空，返回 `false` |
| `writeTextFile` | 返回 `false` |
| `openUrl` / `openFile` | 返回 `false` |
| `httpRequest` | `outResponse` 为空，返回 `false` |
| `runProcess` | 返回 `false`，不启动进程 |
| `getPluginConfig` | 返回 `defaultValue` |
| `setPluginConfig` | 返回 `false` |

---

## 六、字符串结果协议

所有返回字符串的接口使用统一的 `WLStringResultV1` 协议：

```cpp
struct WLStringResultV1
{
    uint32_t size;           // sizeof(WLStringResultV1)
    wchar_t* buffer;         // 调用方提供的缓冲区（nullptr = 探长度）
    uint32_t bufferLength;   // 缓冲区大小（wchar_t 单位）
    uint32_t requiredLength; // 被调方填充：需要的最小长度（含 '\0'）
};
```

### 两阶段调用模式

```cpp
// 第一阶段：探长度
WLStringResultV1 r{};
r.size = sizeof(r);
r.buffer = nullptr;
r.bufferLength = 0;
host->getAppVersion(host->hostContext, &r);

// 第二阶段：分配缓冲区并获取
uint32_t len = r.requiredLength;
std::wstring result(len - 1, L'\0');
r.buffer = result.data();
r.bufferLength = len;
host->getAppVersion(host->hostContext, &r);
```

### SDK 便捷封装

```cpp
std::wstring Host::GetAppVersion()
{
    WLStringResultV1 r{}; r.size = sizeof(r);
    if (!api_->getAppVersion(ctx_, &r)) return L"";
    std::wstring result(r.requiredLength - 1, L'\0');
    r.buffer = result.data(); r.bufferLength = r.requiredLength;
    api_->getAppVersion(ctx_, &r);
    return result;
}
```

---

## 七、搜索与 Slash 命令

### 7.1 命令注册方式

**清单声明（推荐）**:

```json
{
  "slashCommands": [
    {
      "id": "wl.myplugin.mycmd",
      "command": "mycmd",
      "title": "My Command",
      "description": "Does something useful.",
      "usage": "/mycmd <arg>",
      "keywords": ["keyword1"],
      "aliases": ["mc"]
    }
  ]
}
```

**运行时注册**:

```cpp
WLSlashCommandDescriptorV1 cmd{};
cmd.size = sizeof(cmd);
cmd.id = L"wl.myplugin.mycmd";
cmd.command = L"mycmd";
cmd.title = L"My Command";
// ...
host->registerSlashCommand(host->hostContext, &cmd);
```

### 7.2 / 命令执行上下文

```cpp
struct WLSlashCommandContextV1
{
    uint32_t size;
    const wchar_t* commandId;    // 完整 ID，如 "wl.myplugin.mycmd"
    const wchar_t* command;      // 命令名，如 "mycmd"
    const wchar_t* args;         // 命令后的参数文本
    const wchar_t* rawInput;     // 完整用户输入，含 "/"
    const wchar_t* selectedFiles; // WinLauncher 捕获到的选中文件，多个路径以 '\n' 分隔
};
```

`selectedFiles` 可为空。插件可按命令能力取第一个文件，或遍历所有路径处理多文件。

### 7.3 搜索请求/响应

```cpp
struct WLSearchRequestV1
{
    uint32_t size;
    const wchar_t* query;        // 当前搜索词
    bool slashMode;              // true = 用户输入以 / 开头
    uint32_t maxResults;         // 最多返回条数
};

struct WLSearchResultV1
{
    uint32_t size;
    const wchar_t* id;
    const wchar_t* title;
    const wchar_t* description;
    const wchar_t* commandId;
    int32_t score;               // 排序分数（越高越靠前）
};

struct WLSearchResponseV1
{
    uint32_t size;
    void* hostContext;
    bool (WL_CALL* addResult)(void*, const WLSearchResultV1*);
};
```

---

## 八、插件清单 manifest

### 8.1 `plugin.json` 完整字段

```json
{
  "schemaVersion": 1,
  "id": "wl.my_plugin",
  "name": "My Plugin",
  "version": "1.0.0",
  "author": "Author Name",
  "description": "Plugin description.",
  "entry": "my_plugin.dll",
  "abiVersion": 1,
  "minHostVersion": "0.5.1.7",
  "category": "Tools",
  "permissions": [
    "clipboard.read",
    "clipboard.write",
    "ui.input"
  ],
  "icon": "icon.png",
  "commands": [
    {
      "id": "wl.my_plugin.cmd1",
      "title": "Command 1",
      "description": "Description",
      "keywords": ["kw1", "kw2"]
    }
  ],
  "slashCommands": [
    {
      "id": "wl.my_plugin.scmd1",
      "command": "scmd1",
      "title": "Slash Command",
      "description": "Description",
      "usage": "/scmd1 <arg>",
      "keywords": ["kw1"],
      "aliases": ["sc1"],
      "icon": "cmd_icon.png"
    }
  ],
  "settings": [
    {
      "key": "max_results",
      "type": "integer",
      "title": "Max Results",
      "description": "Maximum search results to return.",
      "default": 10,
      "min": 1,
      "max": 50
    },
    {
      "key": "enable_feature",
      "type": "boolean",
      "title": "Enable Feature",
      "default": true
    },
    {
      "key": "api_endpoint",
      "type": "string",
      "title": "API Endpoint",
      "default": "https://api.example.com"
    }
  ]
}
```

### 8.2 字段规则

| 字段 | 必填 | 规则 |
|------|------|------|
| `schemaVersion` | 是 | 固定为 `1` |
| `id` | 是 | `小写字母` + `数字` + `_` + `.`，必须与目录名一致 |
| `name` | 是 | 面向用户展示 |
| `version` | 是 | 语义版本 `major.minor.patch` |
| `entry` | 是 | 相对路径 DLL，禁止 `..` / 绝对路径 / UNC |
| `abiVersion` | 是 | 当前为 `1` |
| `minHostVersion` | 是 | 不得高于当前 WinLauncher 版本 |
| `permissions` | 是 | 仅限允许列表中的值 |
| `category` | 否 | 分组标签，如 `"Text"` / `"File"` / `"Network"` |
| `commands[].id` | 是 | 格式 `plugin_id.command_name` |
| `slashCommands[].id` | 是 | 同上 |
| `slashCommands[].command` | 是 | 不含 `/` 前缀，仅小写字母+连字符 |
| `slashCommands[].aliases` | 否 | 别名列表，不含 `/` 前缀 |

---

## 九、版本兼容策略

### 9.1 ABI 版本

- 当前 ABI 版本: `1`（常量 `WINLAUNCHER_PLUGIN_ABI_VERSION`）
- 新增非破坏性字段: 附加在结构体末尾，`size` 字段标识
- 新增破坏性变更: 发布 ABI v2，主程序同时支持 v1 和 v2 插件

### 9.2 `size` 字段兼容

```cpp
// 主程序检查插件传入的结构体版本
if (ctx->size >= offsetof(WLSlashCommandContextV1, newField) + sizeof(newField))
{
    // 使用 newField
}
else
{
    // newField 不存在，使用默认行为
}
```

### 9.3 新增 Host API 的兼容方式

**推荐方案**: 在 `WLHostApiV1` 结构体末尾追加新函数指针，通过 `size` 字段判断：

```cpp
// 插件检查 Host API 是否支持新接口
if (host->size >= sizeof(WLHostApiV1))
{
    // 包含所有字段，新接口可用
}
// 否则使用已有接口或直接 Win32 API
```

---


## 附录 A：接口优先级汇总

| 优先级 | 接口组 | 接口列表 |
|--------|--------|----------|
| P0 (当前) | 基础能力 | `log`, `getDataDirectory`, `getAppVersion`, `getPluginConfig`, `setPluginConfig`, `registerCommand`, `registerSlashCommand` |
| P0 (当前) | 剪贴板 | `readClipboardText`, `writeClipboardText` |
| P0 (当前) | 文件/URL | `openUrl`, `openFile`, `readTextFile`, `writeTextFile` |
| P1 (当前) | 输入交互 | `showInputDialog`, `showPasswordDialog`, `showChooseDialog`, `showConfirmDialog` |
| P1 (当前) | 文件选择 | `showFilePicker` |
| P1 (当前) | 通知提示 | `showNotificationToaster`, `showMessageBox`, `showBalloonTip` |
| P1 (当前) | 加载弹窗 | `showLoadingDialog`, `updateLoadingMessage`, `hideLoadingDialog` |
| P1 (当前) | 进度弹窗 | `showProgressDialog`, `updateProgress`, `hideProgressDialog`, `isDialogCancelled` |
| P1 (当前) | 结果回显 | `showResultInPanel` |
| P2 (当前) | 网络 | `httpRequest` |
| P2 (当前) | 进程 | `runProcess` |
| P2 (当前) | 显示信息 | `getScreenInfo` |

---

## 附录 B：影响到的内置插件

| 插件 | 当前缺失 | 对接接口 | 对接方式 |
|------|----------|----------|----------|
| `text_tools` | 文本输入 | `showInputDialog` | 无参数且剪贴板为空时弹输入框 |
| `file_tools` | 选中文件上下文 | `WLSlashCommandContextV1.selectedFiles` | `/fileinfo`、`/filehash` 可直接处理资源管理器/桌面已选文件 |
| `time_tools` | 数值输入 | `showInputDialog` | `/countdown` 无参数时弹输入框 |
| `network_tools` | 文本输入 | `showInputDialog` | `/ping`、`/dns`、`/port` 无参数时弹输入框 |
| `color_tools` | 颜色选择 | 自绘 (无需 Host) | ✅ 已实现 |
| `system_info` | 屏幕信息 | `getScreenInfo` | `/screen` 显示主屏分辨率、DPI 和主题 |

---

> **文档版本**: 1.2  
> **创建日期**: 2026-07-04  
> **更新日期**: 2026-07-04  
> **适用 WinLauncher 版本**: 0.5.1.7+  
> **维护规则**: 每新增或修改接口时，更新本文档对应章节并递增版本号。  
> **v1.2 更新**: 将输入交互、文件选择、通知、加载/进度、结果回显、网络请求、进程执行和屏幕信息接口落到 `WLHostApiV1` 末尾扩展，并同步内置插件对接状态。
