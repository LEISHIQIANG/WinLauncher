# 托盘右键菜单光标转圈 — 修复方案分析

## 约束条件

- ✅ 保持现有 UI 样式（D2D 自绘 GlassWindow 弹出菜单）
- ✅ 不改用 Win32 TrackPopupMenu
- ✅ 最小化修改范围

## 根本矛盾

```
Shell 发送 WM_RBUTTONUP → 等待返回 → 期间光标状态被 Shell 锁定
                              ↓
                    我们的代码同步阻塞（背景捕获 + 合成）
                              ↓
                    Shell 长时间收不到返回 → 保持等待光标
```

**解决思路：让 Shell 尽快得到返回，光标清理后，再做繁重工作。**

---

## 方案 1：PostMessage 延迟触发 ⭐ 推荐

**思路**：在 `Application::HandleMessage` 中，收到 `TrayIcon + WM_RBUTTONUP` 时不直接调用 `ShowTrayMenuAtCursor()`，而是 `PostMessage` 一个内部消息，立即 return。Shell 收到 return 后清理光标。下一轮消息循环中，内部消息触发菜单显示。

### 涉及文件与修改点

| 文件 | 修改 |
|------|------|
| `AppMessages.h` | 新增 `constexpr UINT ShowTrayMenu = WM_APP + 0x99;` |
| `Application.cpp:742-745` | `ShowTrayMenuAtCursor()` 改为 `PostMessageW(hWnd, AppMessages::ShowTrayMenu, 0, 0)` |
| `Application.cpp` HandleMessage | 新增 `case AppMessages::ShowTrayMenu: ShowTrayMenuAtCursor(); return 0;` |

### 影响评估

| 维度 | 评价 |
|------|------|
| UI 样式 | ✅ 完全不变 |
| 代码改动量 | ⭐ 最小（3 处，约 5 行） |
| 菜单响应延迟 | 增加 1 帧（~16ms），肉眼不可感知 |
| 副作用风险 | ⭐ 最低。PostMessage 是本进程标准操作 |
| 首次/后续表现 | 始终一致，每次都走同一路径 |

### 原理

```
之前：
  Shell → TrayIcon/RBUTTONUP → ShowTrayMenuAtCursor() [阻塞 50-150ms] → return 0
  光标: Shell 等待 150ms → 转圈

之后：
  Shell → TrayIcon/RBUTTONUP → PostMessage(ShowTrayMenu) → return 0 [<1ms]
  光标: Shell 立即得到 return → 正常箭头
  下一帧 → ShowTrayMenu → ShowTrayMenuAtCursor() [阻塞 50-150ms]
  光标: 托盘菜单窗口出现后 GlassWindow::WM_SETCURSOR → IDC_ARROW
```

### 为什么不会"闪一下"

菜单窗口创建时 `PrepareOpenTransitionFrame` 已将透明度设为 0（不可见），`ShowWindow(SW_SHOW)` 后才通过动画逐渐显示。因此在 `ShowTrayMenuAtCursor()` 的阻塞期间，用户看不到任何异常。

---

## 方案 2：ShowWindow 提前，背景工作后移

**思路**：在 `TrayMenuWindow::Show()` 中，先 `ShowWindow`，再通过 `PostMessage` 触发 `CaptureBackground` + `CompositeBackgroundToCache` + `InvalidateRect`。

### 涉及文件与修改点

| 文件 | 修改 |
|------|------|
| `TrayMenuWindow.cpp:91-98` | 将 CaptureBackground/CompositeBackgroundToCache 移到 ShowWindow 之后，通过 PostMessage 触发 |
| `TrayMenuWindow.cpp` HandleMessage | 新增自定义消息处理，执行背景捕获 + 合成 + InvalidateRect |

### 影响评估

| 维度 | 评价 |
|------|------|
| UI 样式 | ⚠️ 首次可能看到 1 帧无背景的"裸窗口"（仅有 Unreal 风格边框） |
| 代码改动量 | 中等（~15 行） |
| 副作用风险 | 中等。D2D 渲染时序变复杂 |

### 不推荐原因

背景捕获需要窗口已定位且 DPI 已设置，ShowWindow 之后再设置 DPI 可能导致 D2D 位图尺寸不匹配。更关键的是，菜单首次出现时可能短暂显示缺少模糊背景的窗口，视觉上不完美。

---

## 方案 3：TrayMenuWindow 单例复用（参考 PopupWindow）

**思路**：TrayMenuWindow 不每次新建/销毁，改为单例模式，Hide 时隐藏而非 DestroyWindow。

### 涉及文件与修改点

| 文件 | 修改 |
|------|------|
| `TrayMenuWindow.h` | Hide() 改为 ShowWindow(SW_HIDE)，Release() 才 DestroyWindow |
| `TrayMenuWindow.cpp:47-53` | Show() 中若 s_instance 存在，只调整位置 + ShowWindow |
| `TrayMenuWindow.cpp:101-123` | Hide() 改为隐藏而非销毁 |
| `Application.cpp:158,397` | 生命周期调整 |

### 影响评估

| 维度 | 评价 |
|------|------|
| UI 样式 | ✅ 完全不变 |
| 代码改动量 | 大（~30 行） |
| 首次打开 | ⚠️ 首次仍有转圈问题（冷启动），如果用户第一次右键就遇到转圈，体验无改善 |
| 副作用风险 | 中等。窗口一直存活在内存中，隐藏时仍占用 D2D 资源 |
| DPI 变化 | ⚠️ 需要额外处理：窗口从显示器 A 移动到显示器 B 时，DPI 可能不同，需要重建 D2D 资源 |

### 不推荐原因

首次打开问题未解决。且需要处理 DPI 变化、背景缓存失效等额外情况，增加了代码复杂度。

---

## 方案 4：Application 层 PostMessage（与方案 1 本质相同，但无新消息 ID）

**思路**：不新增 AppMessages 常量，而是注册一个自定义 Windows 消息。

### 涉及文件

| 文件 | 修改 |
|------|------|
| `Application.cpp` | `static UINT s_uShowTrayMenu = RegisterWindowMessageW(L"WinLauncherShowTrayMenu")` |
| `Application.cpp:742-745` | PostMessage 使用 s_uShowTrayMenu |

### 评价

与方案 1 等价，但多了 `RegisterWindowMessage` 的开销，且不如 `AppMessages` 统一管理清晰。不推荐。

---

## 方案对比总结

| | 方案 1 PostMessage | 方案 2 提前 Show | 方案 3 单例复用 | 方案 4 RegMsg |
|---|---|---|---|---|
| UI 样式保持 | ✅ | ⚠️ 可能闪裸窗 | ✅ | ✅ |
| 首次打开修复 | ✅ | ✅ | ❌ 仍有转圈 | ✅ |
| 代码改动量 | ⭐ 最小 | 中等 | 大 | 少但不优雅 |
| 副作用风险 | ⭐ 最低 | 中等 | 中等 | 低 |
| 推荐度 | ⭐⭐⭐ | ⭐⭐ | ⭐ | ⭐ |

---

## 推荐：方案 1 — PostMessage 延迟触发

### 为什么

1. **改动最小** — 仅 3 处，约 5 行代码
2. **完全不影响 UI** — 菜单外观、动画、交互逻辑零变化
3. **从根本上解决问题** — Shell 立即得到 return、清理光标、然后菜单才做繁重工作
4. **延迟不可感知** — 1 帧（~16ms）的 PostMessage 延迟，菜单本身的动画 + 渲染远大于此
5. **无副作用** — PostMessage 是本进程异步操作，不引入线程问题，不改变窗口生命周期

### 具体修改点

```
AppMessages.h       — 加一行: constexpr UINT ShowTrayMenu = WM_APP + 0x99;
Application.cpp:742 — 改一行: PostMessageW(hWnd, AppMessages::ShowTrayMenu, 0, 0);
Application.cpp      — 加四行: case AppMessages::ShowTrayMenu: ShowTrayMenuAtCursor(); return 0;
```
