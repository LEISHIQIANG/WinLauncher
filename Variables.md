# WinLauncher 内置命令与 URL 变量系统说明文档

> **适用版本：** V1.6+  
> **核心模块：** `Services/CommandVariableService`  
> **源文件：** `WinLauncher/Services/CommandVariableService.cpp` (425 行)  
> **头文件：** `WinLauncher/Services/CommandVariableService.h`

---

## 目录

- [一、架构概览](#一架构概览)
- [二、支持的变量清单](#二支持的变量清单)
- [三、自动转义与参数引用修饰符 (`:q`)](#三自动转义与参数引用修饰符-q)
- [四、特殊占位符 `{{url}}`](#四特殊占位符-url)
- [五、内部处理流程详解](#五内部处理流程详解)
- [六、边界情况与错误处理](#六边界情况与错误处理)
- [七、线程安全说明](#七线程安全说明)
- [八、命令与 URL 使用示例](#八命令与-url-使用示例)

---

## 一、架构概览

启动一个 Command 或 URL 快捷方式时，系统按以下管线依次处理：

```
                    用户触发快捷方式
                          │
                          ▼
              ① 收集交互输入 (ResolveInputs)
              有 {{input}} → 弹输入框让用户填
              用户点取消 → 终止, 不执行
                          │
                          ▼
              ② 替换变量 (ResolveVariables)
              {{date}}→今天日期, {{clipboard}}→剪贴板内容, ...
              变量值按 shell 类型自动加引号转义
                          │
                          ▼
              ③ 高危命令检查 (仅 Command)  ←── URL 没有这一步
              匹配到 rm -rf / format 等高风险模式?
              ├─ 是 → 弹框:"检测到危险操作, 是否继续?"
              │       用户点否 → 终止, 不执行
              └─ 否 → 直接通过
                          │
                          ▼
              ④ 执行
              启动 cmd.exe / powershell.exe / 浏览器 ...
```

**两种快捷方式的差异：**

| 步骤 | Command | URL |
| :--- | :--- | :--- |
| ① 收集输入 | ✅ 有 `{{input}}` 才弹 | ✅ 同左 |
| ② 替换变量 | ✅ 按用户选的 shell 类型转义 | ✅ 固定用 CMD 风格转义 |
| ③ 高危检查 | ✅ 匹配到高风险模式就弹确认框 | ❌ 跳过 |
| ④ 执行 | `cmd /c ...` / `powershell ...` 等 | `ShellExecuteW` 打开浏览器 |

> **实现位置：** `PopupWindow.cpp`  
> `ExpandVariables()` (URL) — 第 2748–2772 行  
> `LaunchCommand()` (Command) — 第 3635–3671 行  
> `ConfirmHighRiskCommand()` — 第 3454 行起  
> `CommandVariableService::ResolveInputs()` — `CommandVariableService.cpp` 第 191–246 行  
> `CommandVariableService::ResolveVariables()` — `CommandVariableService.cpp` 第 249–424 行

---

## 二、支持的变量清单

> **重要：** 所有变量名均为 **大小写不敏感**（源码行 289–290：`specLower = spec; for(auto& c : specLower) c = towlower(c);`）。  
> 例如 `{{CLIPBOARD}}`、`{{Clipboard}}`、`{{clipboard}}` 三者等价。

### 1.1 系统状态类

| 变量占位符 | 内部实现 | 功能描述 | 示例输出 | 空值行为 |
| :--- | :--- | :--- | :--- | :--- |
| `{{clipboard}}` | `GetClipboardText()` 行 17–35 | 读取 Windows 剪贴板中的 `CF_UNICODETEXT` 文本内容。使用 `OpenClipboard()` + `GetClipboardData()` + `GlobalLock()` 标准 Win32 API。 | `http://example.com` | 剪贴板为空或非文本内容 → 空字符串 `""` |
| `{{date}}` | `GetCurrentDateString()` 行 114–121 | 通过 `GetLocalTime()` 获取当前系统本地时间，格式化为 `YYYY-MM-DD`。时间源为操作系统本地时钟。 | `2026-06-30` | 永远非空（系统时钟始终可用） |
| `{{time}}` | `GetCurrentTimeString()` 行 124–131 | 通过 `GetLocalTime()` 获取当前系统本地时间，格式化为 `HH:MM:SS`（24 小时制）。 | `23:40:15` | 永远非空（系统时钟始终可用） |
| `{{lan_ip}}` | `GetLocalLANIP()` 行 38–75 | 创建 UDP socket 向 `8.8.8.8:80` 发起 `connect()`（不实际发送数据），通过 `getsockname()` 提取本机实际使用的出口 IPv4 地址。每次 ResolveVariables 调用**仅执行一次**，结果缓存。 | `192.168.1.108` | `WSAStartup()` 失败 → `127.0.0.1`；其他失败 → `127.0.0.1` |
| `{{wan_ip}}` | `FetchPublicWANIP()` 行 78–111 | 通过 Windows `WinINet` API（`InternetOpenUrlW`）访问 `https://api.ipify.org` 获取本机公网 IPv4 地址。使用 UTF-8 解码响应。每次 ResolveVariables 调用**仅执行一次**，结果缓存。 | `203.0.113.50` | 网络不可达或 API 无响应 → `0.0.0.0` |

### 1.2 文件选择类

| 变量占位符 | 内部实现 | 功能描述 | 示例输出 | 空值行为 |
| :--- | :--- | :--- | :--- | :--- |
| `{{selected_file}}` | 行 306–308 | 获取文件资源管理器（或桌面）中当前选中的**第一个文件**的完整绝对路径。 | `C:\Users\Admin\Desktop\notes.txt` | 无选中文件 → 空字符串 `""` |
| `{{selected_file_name}}` | 行 310–317 | 获取选中第一个文件的**文件名**（含后缀），提取自最后一个 `\` 或 `/` 之后。 | `notes.txt` | 无选中文件 → 空字符串 `""` |
| `{{selected_file_dir}}` | 行 319–326 | 获取选中第一个文件所在的**父文件夹绝对路径**（最后一个 `\` 或 `/` 之前的部分）。**无尾部反斜杠**。 | `C:\Users\Admin\Desktop` | 无选中文件 → 空字符串 `""`；路径无分隔符 (根) → 空字符串 `""` |
| `{{selected_file_folder}}` | 行 319（别名） | **`selected_file_dir` 的完整等价别名**。两者行为完全一致，可互换使用。 | `C:\Users\Admin\Desktop` | 同 `selected_file_dir` |
| `{{selected_files}}` | 行 328–350 | 获取**所有选中文件**的绝对路径列表。取决于是否带 `:q` 修饰符，行为不同（详见下文）。 | 见下表 | 无选中文件 → 空字符串 `""` |

#### selected_files 行为详解

| 是否带 `:q` | 分隔符 | 行为 | 示例输出 (选中 3 个文件) |
| :--- | :--- | :--- | :--- |
| **不带 `:q`** | 换行符 `\n` | 每个文件路径原样拼接，换行分隔 | `C:\file1.txt`<br>`C:\folder\file2.doc`<br>`D:\file3.png` |
| **带 `:q`** | 空格 | 每个文件路径**先经 QuoteArgument() 按当前 shellType 转义**，再以空格拼接。**注意：此模式下不通过 value 字段输出，而是直接拼接到 result 中 (行 338: `result += filesStr; continue;`)** | `"C:\file1.txt" "C:\folder\file2.doc" "D:\file3.png"` (CMD 模式) |

### 1.3 应用路径类

| 变量占位符 | 内部实现 | 功能描述 | 示例输出 | 空值行为 |
| :--- | :--- | :--- | :--- | :--- |
| `{{app_dir}}` | `GetAppDir()` 行 134–143 | 通过 `GetModuleFileNameW()` 获取当前进程的可执行文件完整路径，截取最后一个 `\` 或 `/` 之前的父目录。**无尾部反斜杠**。 | `C:\Users\Admin\Desktop\WinLauncher` | 理论上不会为空 (exe 必有路径) |
| `{{config_dir}}` | `GetConfigDir()` 行 146–149 | `app_dir` + `\config`。即 `{app_dir}\config`。 | `C:\Users\Admin\Desktop\WinLauncher\config` | 同 `app_dir` + `\config` |

> **注意：** `config_dir` 仅拼接路径字符串，不检查目录是否真实存在。调用方需自行确保 `config` 子目录存在。

### 1.4 交互输入类

| 变量占位符 | 内部实现 | 功能描述 | 交互行为 |
| :--- | :--- | :--- | :--- |
| `{{input}}` | 行 386–405 | 运行时显示输入框，标题为 "运行时输入"，提示文字默认为 `"请输入运行时输入内容:"`。 | 用户确认 → 替换为用户输入值；用户取消 → **终止启动流程** |
| `{{input:提示文字}}` | 行 388–395 | 运行时显示输入框，标题为 "运行时输入"，提示文字为自定义内容（"提示文字" 部分）。支持 `:q` 后缀 (`{{input:提示文字:q}}`)，`:q` 被自动剥离后仅作为变量替换阶段的引号修饰符。 | 同上；同一 prompt 文字（含空提示）的多个 `{{input}}` 变量**共用一次输入**（去重） |
| `{{password}}` | 新增 | 密码输入框。字符以实心圆点 `●` 显示，输入文字不暴露。对话框标题 "密码输入"。 | 用户确认 → 替换为用户输入的密码原文（不落盘、不缓存）；用户取消 → 终止 |
| `{{password:提示文字}}` | 新增 | 带自定义提示文字的密码输入。提示默认为 `"请输入密码:"`。支持 `:q` 后缀。 | 同上；同一 prompt 的多个 `{{password}}` 共用一次输入（去重） |
| `{{choose:选项A\|选项B\|...}}` | 新增 | 下拉选项列表。选项以 `\|` 分隔，至少需要 2 个选项。对话框标题 "请选择"，提示文字 "请选择一项:"。用户点击 OR 键盘上下键选择，双击 OR 回车确认。支持 `:q` 后缀。 | 用户选择并确认 → 替换为选中项的文本；用户取消 → 终止 |
| `{{confirm}}` | 新增 | 确认关卡对话框。弹出 "确定继续执行吗？" 警告框，用户点确定则继续，点取消则终止。**始终替换为空字符串**，不污染命令文本。 | 用户确定 → 继续执行；用户取消 → 终止 |
| `{{confirm:提示信息}}` | 新增 | 带自定义信息的确认关卡。提示文字显示在对话框中（如 `"即将重启服务, 确定继续？"`）。 | 同上 |

> **交互去重规则：**  
> `ResolveInputs()` 阶段 1 对 `input`、`password`、`choose`、`confirm` 四类变量分别按 prompt/options 去重。  
> - 多个 `{{input:请输入用户名}}` → 只弹一次输入框  
> - 多个 `{{password:请输入数据库密码}}` → 只弹一次密码框  
> - 多个 `{{choose:生产\|测试\|开发}}` → 只弹一次选择框  
> - 多个 `{{confirm:危险操作确认}}` → 只弹一次确认框  
> 不同 prompt 或不同 options 组合的变量各自独立去重。

---

## 三、自动转义与参数引用修饰符 (`:q`)

### 2.1 基本机制

`:q` 修饰符附加在变量名末尾（如 `{{selected_file:q}}`），于 `ResolveVariables()` 中被检测 (行 294–298)：

```cpp
if (baseKey.size() >= 2 && baseKey.substr(baseKey.size() - 2) == L":q")
{
    quote = true;
    baseKey = baseKey.substr(0, baseKey.size() - 2);
}
```

`quote == true` 时，变量值被送入 `QuoteArgument(value, shellType)` (行 152–189) 进行 shell 特定转义。`baseKey` 剥离 `:q` 后继续正常查表匹配。

**适用范围：** 所有变量均支持 `:q` 修饰符，包括 `{{input:xxx:q}}`、`{{selected_files:q}}`、`{{clipboard:q}}` 等。

**`:q` 对 `{{input}}` 的特殊处理：**  
在 `ResolveInputs()` 阶段 1 中，`:q` 已从 prompt key 中剥离 (行 218–219, 394–395)，确保输入对话框的 prompt 不包含 `:q` 文本。但在阶段 2 中，变量的 `spec` 中的 `:q` 仍会触发 `quote = true`，对用户输入值进行 shell 转义。

### 2.2 Shell 类型详细转义规则

#### 2.2.1 CMD 模式 (`shellType == L"cmd"` 或默认)

**源码：** `CommandVariableService.cpp` 行 178–188

| 步骤 | 操作 | 说明 |
| :--- | :--- | :--- |
| 1 | 包裹双引号 | 整体用 `"..."` 括起 |
| 2 | 转义内部双引号 | 内部 `"` → `\"` |

**示例：**

| 原始值 | 转义结果 |
| :--- | :--- |
| `C:\My Files\doc.txt` | `"C:\My Files\doc.txt"` |
| `He said "Hello"` | `"He said \"Hello\""` |
| `a & b` | `"a & b"` (保护 `&` 不被 CMD 解释为命令连接符) |
| 空字符串 `""` | `""` (一对空引号) |

#### 2.2.2 PowerShell 模式 (`shellType == L"powershell"`)

**源码：** `CommandVariableService.cpp` 行 167–177

| 步骤 | 操作 | 说明 |
| :--- | :--- | :--- |
| 1 | 包裹单引号 | 整体用 `'...'` 括起 |
| 2 | 转义内部单引号 | 内部 `'` → `''` (PowerShell 标准转义) |

**示例：**

| 原始值 | 转义结果 |
| :--- | :--- |
| `C:\My Files\doc.txt` | `'C:\My Files\doc.txt'` |
| `Tony's file` | `'Tony''s file'` |
| `$env:PATH` | `'$env:PATH'` (保护 `$` 不被 PowerShell 展开) |

#### 2.2.3 GitBash 模式 (`shellType == L"gitbash"`)

**源码：** `CommandVariableService.cpp` 行 154–166

| 步骤 | 操作 | 说明 |
| :--- | :--- | :--- |
| 1 | 路径规范化 | 所有 `\` → `/`（Windows 路径转为 Unix 风格） |
| 2 | 包裹单引号 | 整体用 `'...'` 括起 |
| 3 | 转义内部单引号 | 内部 `'` → `'\''` (Bash/Shell 标准转义) |

**示例：**

| 原始值 | 转义结果 |
| :--- | :--- |
| `C:\Program Files\App` | `'C:/Program Files/App'` |
| `It's working` | `'It'\''s working'` |
| `D:\Tony's\data` | `'D:/Tony'\''s/data'` |

#### 2.2.4 URL 模式

**源码：** `PopupWindow.cpp` 行 2771 (`ExpandVariables` 函数)

URL 快捷方式通过 `ExpandVariables()` 调用 `ResolveVariables()` 时，**硬编码传入 `L"cmd"` 作为 shellType**。因此 URL 中的变量转义规则**等同于 CMD 模式**（双引号包裹 + `\"` 转义）。

> **设计意图：** URL 上下文中的变量值不需要 shell 执行语义，CMD 模式的双引号包裹足以保证 URL 参数安全性。

---

## 四、特殊占位符 `{{url}}`

### 3.1 适用范围

`{{url}}` **不是 CommandVariableService 变量系统中的一部分**，它是浏览器启动参数中专用的特殊占位符，仅在 URL 类型的快捷方式启动时生效。

**源码：** `PopupWindow.cpp` 行 2802–2811 (`LaunchUrl` 函数)

```cpp
size_t urlPos = args.find(L"{{url}}");
if (urlPos != std::wstring::npos)
{
    args.replace(urlPos, 7, url);   // 替换 {{url}} → 展开后的 URL
}
else
{
    if (!args.empty()) args += L" ";
    args += url;                     // 无 {{url}} 则追加到末尾
}
```

### 3.2 语义说明

| 场景 | 行为 |
| :--- | :--- |
| 浏览器参数含 `{{url}}` | `{{url}}` 被替换为 URL 字段的内容（已展开所有变量） |
| 浏览器参数不含 `{{url}}` | URL 自动追加到参数末尾（以空格分隔） |
| 浏览器参数为空 | 等价于不含 `{{url}}`，URL 被追加 |

### 3.3 注意点

- `{{url}}` **不支持** `:q` 修饰符 — 它是直接字符串查找替换，未经 `CommandVariableService` 处理。
- `{{url}}` 在浏览器参数中获得的值是 **已经过 `ExpandVariables()` 展开的 URL**（含 CMD 模式转义）。
- `{{url}}` 与 URL 字段中的 `{{input}}`、`{{clipboard}}` 等变量相互独立 — URL 字段先展开，然后 `{{url}}` 替换已展开的结果。

---

## 五、内部处理流程详解

### 4.1 阶段 1：输入收集 (`ResolveInputs`)

**函数签名：** `CommandVariableService::ResolveInputs(HWND, commandText, outInputs)` — 行 191–246

```
伪代码：
1. 初始化 pos=0, prompts=[]
2. while 找到 "{{" 在 pos 之后:
   a. 找到对应的 "}}"
   b. 提取 {{...}} 中间的 spec 字符串
   c. 去除 spec 首尾空格
   d. spec 转小写
   e. 如果 spec == "input" 或以 "input:" 开头:
      - 剥离 :q 后缀
      - 提取 prompt (":" 之后部分或空字符串)
      - 如果 prompt 不在 prompts 中, 加入
3. for 每个唯一的 prompt:
   a. 构造显示文字 (空 prompt → "请输入运行时输入内容:")
   b. 显示 PromptWindow (模态, 标题 "运行时输入")
   c. 如果用户取消 → 返回 false
   d. 存入 outInputs[prompt] = 用户输入
4. 返回 true
```

**关键细节：**

- 输入框是**模态窗口** (`PromptWindow::Show`)，阻塞调用线程直到用户确认或取消。
- **去重依据：** prompt 文字（包括空字符串）。多 `{{input}}` 只弹一次窗，多 `{{input:用户名}}` 也只弹一次。
- **取消传播：** 用户在任一输入框点取消 → `ResolveInputs` 返回 `false` → 调用方终止启动流程。

### 4.2 阶段 2：变量替换 (`ResolveVariables`)

**函数签名：** `CommandVariableService::ResolveVariables(commandText, shellType, selectedFiles, inputValues)` — 行 249–424

```
伪代码：
1. 初始化 result="", pos=0, hasCachedWANIP=false, hasCachedLANIP=false
2. while 找到 "{{" 在 pos 之后:
   a. 将 "{{" 之前的文字追加到 result
   b. 找到对应的 "}}"
   c. 提取 spec 字符串, 去除首尾空格
   d. spec 转小写 → specLower
   e. 检测 :q 后缀 → quote=true, baseKey=去掉:q 的名字
   f. 查表匹配 baseKey:
      - clipboard  → GetClipboardText()
      - selected_file → selectedFiles[0] 或 ""
      - selected_file_name → 提取文件名
      - selected_file_dir / selected_file_folder → 提取目录
      - selected_files → 拼接（带 :q 时各文件独立转义）
      - date → GetCurrentDateString()
      - time → GetCurrentTimeString()
      - app_dir → GetAppDir()
      - config_dir → GetConfigDir()
      - lan_ip → GetLocalLANIP() (首次) / cached (后续)
      - wan_ip → FetchPublicWANIP() (首次) / cached (后续)
      - input / input:xxx → inputValues[prompt]
      - 未知变量 → 原样输出 "{{spec}}"
   g. 如果 quote → QuoteArgument(value, shellType)
   h. 追加到 result
3. 将末尾剩余文字追加到 result
4. 返回 result
```

### 4.3 IP 地址缓存机制

**源码：** 行 260–263, 370–385

`lan_ip` 和 `wan_ip` 在**单次 `ResolveVariables()` 调用内**具有缓存：

```cpp
bool hasCachedWANIP = false;
std::wstring cachedWANIP = L"";
bool hasCachedLANIP = false;
std::wstring cachedLANIP = L"";
```

- 首次遇到 `{{lan_ip}}` → 调用 `GetLocalLANIP()` → 存缓存 + 设置 `hasCachedLANIP = true`
- 后续同一命令中的 `{{lan_ip}}` → 直接返回缓存值
- `{{wan_ip}}` 同理

**价值：** 避免同一命令中多次使用 `{{wan_ip}}` 时重复发起 HTTP 请求。

### 4.4 输入值的 key 机制

`ResolveInputs()` 输出 `std::map<std::wstring, std::wstring>`:
- **Key：** prompt 文字（空字符串表示裸 `{{input}}`，非空字符串如 `"请输入用户名"` 表示 `{{input:请输入用户名}}`）
- **Value：** 用户输入的文本

在 `ResolveVariables()` 中查找输入值 (行 397–405):

```cpp
auto it = inputValues.find(prompt);
if (it != inputValues.end())
    value = it->second;
else if (prompt.empty() && (it = inputValues.find(L"")) != inputValues.end())
    value = it->second;
```

---

## 六、边界情况与错误处理

### 5.1 空值行为汇总

| 变量 | 导致空值的条件 | 替换结果 |
| :--- | :--- | :--- |
| `{{clipboard}}` | 剪贴板为空 / 无非文本内容 / 无权限 | `""` (空字符串) |
| `{{selected_file}}` | 未选中任何文件 | `""` |
| `{{selected_file_name}}` | 未选中任何文件 | `""` |
| `{{selected_file_dir}}` | 未选中文件 / 文件在根目录 | `""` |
| `{{selected_files}}` | 未选中任何文件 | `""` |
| `{{lan_ip}}` | 网络栈不可用 (WSAStartup 失败) | `127.0.0.1` |
| `{{wan_ip}}` | 无网络连接 / ipify 不可达 | `0.0.0.0` |
| `{{input}}` | 用户输入空字符串并确认 | `""` |
| `{{date}}` / `{{time}}` | — | 永远非空 |

### 5.2 未知变量处理

**源码：** 行 407–411

```cpp
else
{
    result += L"{{" + spec + L"}}";
    continue;
}
```

不匹配任何已知变量的 `{{xxx}}` 将被**原样保留**，不做替换。这是**静默 pass-through** 策略，不报错、不警告。

### 5.3 语法异常处理

| 异常情况 | 处理行为 |
| :--- | :--- |
| `{{without_closing` (缺失 `}}`) | 从 `{{` 到字符串末尾的内容**原样追加**到 result (行 274–278) |
| `}}without_opening` (孤立 `}}`) | 原样保留，非变量分隔符 |
| `{{  var  }}` (变量名含首尾空格) | 空格被自动 trim (行 286–287)，`{{  date  }}` 等同于 `{{date}}` |
| `{{}}` (空变量名) | trim 后 spec 为空，落入 `else` 分支，原样输出 `{{}}` |
| 嵌套 `{{}}` | **不递归展开**。第一次替换后，不再对结果重新扫描。 |

### 5.4 文件选择有效期

**源码：** `PopupWindow.cpp` 行 59, 2756–2761

```cpp
static const double FILE_SELECTION_VALIDITY_DURATION = 5.0;
```

文件选择有 **5 秒有效期**。超过 5 秒前的选择将被视为过期：

| 条件 | 行为 |
| :--- | :--- |
| 选择在 5 秒内 | selectedFiles 正常传入 |
| 选择超过 5 秒 | selectedFiles 被清空，所有文件变量返回空字符串 |
| 选择状态为 `isPending` | 同样视为无效，不传入 |

这确保用户不会因为旧的、可能已不相关的文件选择而意外执行错误操作。

### 5.5 路径分隔符兼容性

**文件名提取** (`selected_file_name`) 和 **目录提取** (`selected_file_dir`) 同时支持 `\` 和 `/` 作为分隔符：

```cpp
size_t slash = firstFile.find_last_of(L"\\/");  // 行 315, 324
```

这意味着从 GitBash 路径（如 `/c/Users/file.txt`）获取的文件名和目录路径同样能正确解析。

### 5.6 `app_dir` 路径尾部斜杠

`GetAppDir()` 返回的路径 **不带尾部反斜杠**（行 139–141：`appPath.find_last_of(L"\\/")` + `substr(0, lastSlash)`）。  
拼接子路径时需注意自行添加分隔符（如 `GetConfigDir()` 的做法：`GetAppDir() + L"\\config"`）。

---

## 七、线程安全说明

| 组件 | 线程安全 | 说明 |
| :--- | :--- | :--- |
| `GetClipboardText()` | **需主线程** | `OpenClipboard(nullptr)` 要求调用线程有消息队列 |
| `GetLocalLANIP()` | 安全 | `WSAStartup()`/`socket()` 可跨线程 |
| `FetchPublicWANIP()` | 安全 | WinINet 可在任意线程调用 |
| `ResolveInputs()` | **需 UI 线程** | 弹出模态 PromptWindow，必须在 UI 线程 |
| `ResolveVariables()` | 安全 | 纯计算，无共享状态（除 IP 缓存是局部变量） |

**实际调用链：** 所有 `ResolveInputs()` 和 `ResolveVariables()` 调用均发生在 `PopupWindow` 的 UI 线程上下文中（响应快捷键触发），因此不会出现跨线程竞争。

---

## 八、命令与 URL 使用示例

### 7.1 运行命令（Command）

#### CMD — 基础变量打印

```cmd
echo 当前日期: {{date}} {{time}}
echo 局域网IP: {{lan_ip}}
echo 公网IP: {{wan_ip}}
echo 剪贴板: {{clipboard:q}}
```

#### PowerShell — 处理选中文件

```powershell
$file = {{selected_file:q}}
$dir  = {{selected_file_dir:q}}
$name = {{selected_file_name:q}}
Write-Host "文件: $name"
Write-Host "目录: $dir"
Get-Item -Path $file | Select-Object Name, Length, LastWriteTime
```

#### GitBash — 批量操作选中文件

```bash
# 对所有选中文件执行 wc 统计
wc -l {{selected_files:q}}

# 对选中文件进行归档
tar -czf "{{app_dir}}/backup_{{date}}.tar.gz" {{selected_files:q}}
```

#### Python — 交互式脚本

```python
import socket

host = "{{input:请输入目标主机IP}}"
port = int("{{input:请输入端口号}}")

print(f"Connecting to {host}:{port}...")
print(f"Local IP: {{lan_ip}}")
print(f"Script location: {{app_dir}}")
```

#### 混合使用 — 多种变量组合

```cmd
rem 归档当前选中文件到配置目录，以剪贴板内容命名
mkdir "{{config_dir}}\{{clipboard}}"
xcopy {{selected_files:q}} "{{config_dir}}\{{clipboard}}\" /Y
```

#### 环境切换 — `{{choose}}`

```cmd
rem 选择部署环境，一键 SSH 连接
ssh admin@{{choose:生产服务器192.168.1.10|测试服务器10.0.0.50|开发机localhost}}
```

```powershell
# 选择后端服务地址
$backend = {{choose:http://api.prod.com|http://api.staging.com:q}}
Invoke-RestMethod -Uri "$backend/health"
```

#### 密码场景 — `{{password}}`

```cmd
rem SSH 登录（密码不显示在屏幕上）
ssh admin@{{input:请输入服务器IP}} -p {{password:请输入SSH密码}}

rem MySQL 连接
mysql -u root -p{{password:请输入数据库密码:q}} -h {{lan_ip}}
```

```powershell
# 带密码的 API 调用
$pwd = {{password:请输入API密钥:q}}
$headers = @{ "Authorization" = "Bearer $pwd" }
Invoke-RestMethod -Uri "https://api.example.com/data" -Headers $headers
```

#### 安全确认 — `{{confirm}}`

```cmd
rem 重启服务前加确认关卡（{{confirm}} 替换为空，不污染命令）
{{confirm:即将重启 Nginx，确定继续？}}
net stop nginx && net start nginx
```

```bash
# 批量删除前确认
{{confirm:即将删除所有日志文件，确定继续？}}
rm -rf /var/log/app/*.log
```

```powershell
# 磁盘清理前确认
{{confirm:即将清空临时目录，确定继续？}}
Remove-Item -Path "$env:TEMP\*" -Recurse -Force
```

### 7.2 访问链接（URL）

#### 搜索引擎快捷搜索

| URL 字段 | 浏览器参数 (可选) |
| :--- | :--- |
| `https://www.google.com/search?q={{input:搜索内容}}` | — |
| `https://www.baidu.com/s?wd={{clipboard}}` | — |
| `https://github.com/search?q={{selected_file_name}}&type=code` | `{{url}} --new-window` |

#### 翻译工具集成

| URL 字段 | 说明 |
| :--- | :--- |
| `https://translate.google.com/?sl=auto&tl=zh-CN&text={{clipboard:q}}` | 翻译剪贴板内容 |
| `https://fanyi.baidu.com/#auto/zh/{{input:输入要翻译的文本}}` | 交互式翻译 |

#### 路径快速定位

| URL 字段 | 说明 |
| :--- | :--- |
| `file:///{{selected_file_dir}}` | 在浏览器中打开选中文件所在文件夹 |
| `https://www.google.com/maps?q={{input:输入地址}}` | 交互式地图搜索 |

### 7.3 浏览器参数中的 `{{url}}`

当使用自定义浏览器时，启动参数支持 `{{url}}` 占位符：

| 浏览器路径 | 启动参数 | 效果 |
| :--- | :--- | :--- |
| `C:\Program Files\Google\Chrome\Application\chrome.exe` | `{{url}} --new-window --incognito` | 以隐身模式打开 URL |
| `C:\Program Files\Mozilla Firefox\firefox.exe` | `-private-window {{url}}` | 以隐私窗口打开 URL |

