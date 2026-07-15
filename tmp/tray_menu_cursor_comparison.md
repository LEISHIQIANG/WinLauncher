# 托盘右键菜单 vs 其他窗口 — 光标转圈差异分析

## 核心发现

**其他所有弹出窗口的 Show() 流程与 TrayMenuWindow 完全相同**，都执行 `CaptureBackground()` + `CompositeBackgroundToCache()` + `PrepareOpenTransitionFrame()` 之后才 `ShowWindow()`。所以差异不在于代码本身，而在于 **触发源和窗口生命周期**。

---

## 五类窗口对比

### 1. TrayMenuWindow — 唯一会转圈的

```
触发源: Shell 托盘图标 → AppMessages::TrayIcon (WM_APP+2, lParam=WM_RBUTTONUP)
窗口:   每次新建 → 销毁（无缓存）
流程:   new → Create → PrepareOpenTransitionFrame → CaptureBackground → CompositeBackgroundToCache → ShowWindow(SW_SHOW) → SetForegroundWindow → CaptureMouse(必败)
```

| 因素 | 值 |
|------|-----|
| 触发源 | **跨进程 — Shell (explorer.exe)** |
| 窗口生命周期 | **每次 new，然后 DestroyWindow** |
| m_bgFinal 缓存 | **永不命中 — 始终为 null** |
| ShowWindow flag | SW_SHOW（激活窗口） |
| SetForegroundWindow | 是 |
| CaptureMouse | 是（但必败） |

---

### 2. PopupWindow — 不转圈

```
触发源: 热键 / 鼠标手势（MouseHook）或双击托盘
窗口:   单例 — 创建一次，反复 Show/Hide
流程:   OnConfigChanged → EnsureD2D → [仅冷启动时] CaptureBackground → CompositeBackgroundToCache → PrepareOpenTransitionFrame → DoPaint → ShowWindow(SW_SHOWNOACTIVATE) → SetForegroundWindow
```

| 因素 | 值 |
|------|-----|
| 触发源 | **同进程** — MouseHook PostMessage 或双击托盘 |
| 窗口生命周期 | **单例 — 长期存活** |
| m_bgFinal 缓存 | **热启动命中** — 首次后跳过全部背景工作 |
| ShowWindow flag | SW_SHOWNOACTIVATE |
| SetForegroundWindow | 是 |
| CaptureMouse | **否** |

---

### 3. ContextMenu — 不转圈（即使流程完全相同！）

```
触发源: 配置窗口内右键（ConfigWindow UI）
窗口:   每次新建 → 销毁
流程:   new → Create → PrepareOpenTransitionFrame → CaptureBackground → CompositeBackgroundToCache → ShowWindow(SW_SHOW) → SetForegroundWindow → CaptureMouse(必败)
```

| 因素 | 值 |
|------|-----|
| 触发源 | **同进程** — ConfigWindow 的 WM_RBUTTONDOWN |
| 窗口生命周期 | 每次 new，然后 DestroyWindow |
| m_bgFinal 缓存 | 永不命中 |
| ShowWindow flag | SW_SHOW |
| SetForegroundWindow | 是 |
| CaptureMouse | 是（但必败） |

---

### 4. DropDownMenu — 不转圈（即使流程完全相同！）

```
触发源: 配置窗口内点击下拉按钮（ConfigWindow UI）
窗口:   每次新建 → 销毁
流程:   new → Create → PrepareOpenTransitionFrame → CaptureBackground → CompositeBackgroundToCache → ShowWindow(SW_SHOW) → SetForegroundWindow → CaptureMouse(必败)
```

与 ContextMenu **完全一致**，唯一区别是触发动作为左键点击而非右键。

---

### 5. ToastWindow — 不转圈

```
触发源: 同进程 — TogglePopupPause / RestartHook 等
窗口:   每次新建 → 销毁
流程:   Create → EnsureD2D → PrepareOpenTransitionFrame → CaptureBackground → CompositeBackgroundToCache → ShowWindow(SW_SHOWNOACTIVATE)
```

| 因素 | 值 |
|------|-----|
| 触发源 | **同进程** |
| ShowWindow flag | SW_SHOWNOACTIVATE |
| SetForegroundWindow | **否** |
| CaptureMouse | **否** |
| WS_EX_NOACTIVATE | **是** |

---

## 差异矩阵

| | TrayMenuWindow | PopupWindow | ContextMenu | DropDownMenu | ToastWindow |
|---|---|---|---|---|---|
| 触发源 | **跨进程 (Shell)** | 同进程 | 同进程 | 同进程 | 同进程 |
| 窗口复用 | 否 | **是（单例）** | 否 | 否 | 否 |
| 冷启动背景工作 | **每次** | 仅首次 | 每次 | 每次 | 每次 |
| ShowWindow flag | SW_SHOW | SW_SHOWNOACTIVATE | SW_SHOW | SW_SHOW | SW_SHOWNOACTIVATE |
| SetForegroundWindow | 是 | 是 | 是 | 是 | 否 |
| CaptureMouse | **是（但必败）** | 否 | 是（但必败） | 是（但必败） | 否 |
| 光标转圈 | **✗ 有** | ✓ 无 | ✓ 无 | ✓ 无 | ✓ 无 |

---

## 结论：真正的原因只有一个

### 触发源跨进程 — Shell 的光标状态管理

**ContextMenu 和 DropDownMenu 与 TrayMenuWindow 的代码流程完全一致**（同样的创建→背景捕获→合成→ShowWindow→SetForegroundWindow→CaptureMouse），但它们不转圈。

唯一的区别是 **谁触发的**：

| 窗口 | 触发消息来源 | 发送者进程 |
|------|-------------|-----------|
| **TrayMenuWindow** | `AppMessages::TrayIcon` (WM_APP+2) | **Shell (explorer.exe)** |
| ContextMenu | `WM_RBUTTONDOWN` | ConfigWindow (自身) |
| DropDownMenu | `WM_LBUTTONDOWN` | ConfigWindow (自身) |
| PopupWindow | `AppMessages::ShowPopup` | MouseHook (自身线程) |
| ToastWindow | 直接调用 | Application (自身) |

**具体机制：**

1. 用户在托盘上右键 → Shell 的托盘 WndProc 捕获 `WM_RBUTTONUP`
2. Shell 通过已注册的 `uCallbackMessage`（`AppMessages::TrayIcon`）将消息发送到我们的窗口
3. Shell 的内部处理中，**在发送消息前后会管理光标状态** — 可能设置 `IDC_ARROW`、`IDC_APPSTARTING`，或进入某种等待状态
4. 我们的消息处理器收到消息后，执行 `TrayMenuWindow::Show()` 全套同步流程
5. 在整个 `CaptureBackground + CompositeBackgroundToCache + PrepareOpenTransitionFrame` 阻塞期间，**UI 线程没有返回给 Shell**，Shell 的光标管理无法完成过渡
6. Shell 看到发送的消息长时间未返回，保持等待光标状态

**对比：** 当 ContextMenu/DropDownMenu 从 ConfigWindow 触发时，消息在同一进程内流转，Windows 消息泵正常运转，光标在应用自己的窗口上下文中被正确管理，不存在跨进程等待。

### 次要因素

虽然主因是跨进程触发，但以下因素会加重问题：

1. **TrayMenuWindow 每次新建（无缓存）** — 背景捕获/合成工作每次都要执行，延长了阻塞时间。PopupWindow 是单例，热启动跳过全部背景工作。
2. **CaptureMouse 必败** — `SetCapture` 在 `WM_RBUTTONUP` 后调用必然失败，光标捕获状态异常。
3. **SW_SHOW vs SW_SHOWNOACTIVATE** — `SW_SHOW` 会触发额外的窗口激活过渡。

---

## 修正结论（与上一轮分析的差异）

上一轮分析将主因归结为"UI 线程同步阻塞工作"，但这不够精确——因为 **所有其他窗口都做同样的阻塞工作，却不会转圈**。

真正的根因是：**Shell 跨进程托盘消息处理中的光标状态管理 + UI 线程同步阻塞** 的组合效应。单独任何一个都不会导致转圈（ContextMenu 有同步阻塞但不跨进程，不转圈；PopWindow 跨进程触发但是单例缓存不阻塞，也不转圈）。
