# WinLauncher 内置命令与 URL 变量系统说明文档

本项目支持在 **运行命令（Command）** 和 **访问链接（URL）** 的内容与参数中嵌入动态命令变量。在快捷方式启动时，系统会自动捕获当时的环境、所选文件、剪贴板内容等，并支持交互式询问用户输入，完成变量的高级替换与智能转义。

---

## 一、 支持的变量清单

| 变量占位符 | 功能描述 | 示例输出 |
| :--- | :--- | :--- |
| `{{clipboard}}` | 捕获当前系统的剪贴板文本内容。 | `http://example.com` |
| `{{selected_file}}` | 获取当前在文件资源管理器中选中的**第一个文件**的完整绝对路径。 | `C:\Users\Admin\Desktop\notes.txt` |
| `{{selected_file_name}}` | 获取选中的第一个文件的**文件名（带后缀）**。 | `notes.txt` |
| `{{selected_file_dir}}` | 获取选中的第一个文件所在的**父文件夹绝对路径**。 | `C:\Users\Admin\Desktop` |
| `{{selected_files}}` | 获取所有选中的**文件绝对路径列表**（默认以换行符分隔，配合 `:q` 可在命令行自动分词）。 | `C:\file1.txt`<br>`C:\file2.txt` |
| `{{date}}` | 获取当前系统日期（格式：`YYYY-MM-DD`）。 | `2026-06-30` |
| `{{time}}` | 获取当前系统时间（格式：`HH:MM:SS`）。 | `20:05:42` |
| `{{app_dir}}` | 获取本软件的可执行文件所在的根目录绝对路径。 | `C:\Users\Admin\Desktop\WinLauncher` |
| `{{config_dir}}` | 获取本软件的配置数据目录的绝对路径。 | `C:\Users\Admin\Desktop\WinLauncher\config` |
| `{{lan_ip}}` | 获取本机的局域网内网出口 IPv4 地址（通过探测公共网关智能获取）。 | `192.168.1.108` |
| `{{wan_ip}}` | 访问公网 IP 接口，获取本机的公网出口公网 IPv4 地址。 | `203.0.113.50` |
| `{{input}}` | 运行时交互输入。启动时会弹窗提示用户输入内容，并将其替换至对应位置。 | （用户在弹窗中输入的值） |
| `{{input:提示文字}}` | 带自定义提示文字的运行时交互输入。会弹出输入框，标题显示为“提示文字”。 | （用户在输入框中输入的值） |

---

## 二、 自动转义与参数引用修饰符 (`:q`)

为了防止路径中的空格、特殊字符或剪贴板文本破坏命令行或 URL 的参数结构，我们提供了 **`:q` 修饰符**（例如 `{{selected_file:q}}`）。

系统会根据**命令的运行类型（Shell 类型）**，自动采用对应的转义和引用规则：

1. **CMD 模式（或默认模式）**
   - 规则：将变量内容包裹在双引号 `"` 中，如果内容内部含有双引号，会以反斜杠进行转义 `\"`。
   - 示例：`C:\My Files` 替换为 `"C:\My Files"`

2. **PowerShell 模式**
   - 规则：将变量内容包裹在 PowerShell 的单引号 `'` 中，内部含有的单引号会加倍为两个单引号 `''`。
   - 示例：`D:\Tony's File` 替换为 `'D:\Tony''s File'`

3. **GitBash 模式**
   - 规则：将变量内容中的所有反斜杠 `\` 转换为正斜杠 `/`，然后整体用单引号 `'` 包裹，内部单引号转义为 `'\''`。
   - 示例：`C:\Program Files` 替换为 `'C:/Program Files'`

4. **URL 模式**
   - URL 的目标路径及浏览器参数中同样支持使用上述所有变量（如 `{{clipboard}}`, `{{input:请输入搜索词}}` 等）。URL 变量替换时，默认采用 Windows 标准转义以保证安全。

---

## 三、 命令与 URL 使用示例

### 1. 运行命令（Command）中的使用

- **在 CMD 中打印剪贴板内容与局域网 IP**
  ```cmd
  echo "当前局域网IP是: {{lan_ip}}"
  echo "当前剪贴板内容是: {{clipboard:q}}"
  ```

- **在 PowerShell 中处理选中的文件**
  ```powershell
  $filePath = {{selected_file:q}}
  Write-Host "正在处理文件: $filePath"
  Get-Content -Path $filePath
  ```

- **在 GitBash 中对所有选中的文件进行打包**
  ```bash
  tar -czf backup.tar.gz {{selected_files:q}}
  ```

- **利用交互式输入 `{{input}}` 运行 Python 脚本**
  ```python
  # 运行前会弹出两个输入框询问用户
  username = "{{input:请输入用户名}}"
  ip_addr = "{{input:请输入目标IP}}"
  print(f"Connecting to {username}@{ip_addr}...")
  ```

### 2. 访问链接（URL）中的使用

- **快捷用搜索引擎检索剪贴板内容**
  - URL 设置为：`https://www.google.com/search?q={{clipboard}}`

- **快捷翻译选中的文本或用户交互输入**
  - URL 设置为：`https://translate.google.com/?sl=auto&tl=zh-CN&text={{input:请输入翻译文本}}`
