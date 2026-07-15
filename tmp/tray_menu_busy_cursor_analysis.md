# 托盘右键菜单光标繁忙转圈问题分析

## 问题现象

在系统托盘图标上右键时，鼠标光标出现繁忙/等待（转圈）状态。

---

## 调用链路

```
用户右键托盘图标
  → Shell 发送 AppMessages::TrayIcon (WM_APP+2)，lParam=WM_RBUTTONUP
    → Application::HandleMessage() [Application.cpp:742-745]
      → ShowTrayMenuAtCursor() [Application.cpp:496-501]
        → TrayMenuWindow::Show(pt) [TrayMenuWindow.cpp:47-99]
```

## 关键代码位置

| 文件 | 行号 | 内容 |
|------|------|------|
| `Application.cpp` | 742-745 | TrayIcon 消息分发 |
| `Application.cpp` | 496-501 | ShowTrayMenuAtCursor() |
| `TrayMenuWindow.cpp` | 47-99 | Show() - 菜单创建全流程 |
| `TrayMenuWindow.cpp` | 168-176 | CaptureMouse() |
| `GlassWindow.cpp` | 639-841 | CaptureBackground() |
| `GlassWindow.cpp` | 843-1160+ | CompositeBackgroundToCache() |
| `GlassWindow.cpp` | 1820-1829 | PrepareOpenTransitionFrame() |
| `ShadowWindow.cpp` | 198-338 | GenerateShadowBitmap() |

---

## 根因分析

问题由 **多个因素叠加** 导致，按重要性从高到低排列：

### 1. 主因：消息处理期间在 UI 线程上执行同步阻塞工作

`TrayMenuWindow::Show()` 在 `ShowWindow(SW_SHOW)` 之前（第96行），在 UI 线程上同步执行了以下所有操作：

#### a) `PrepareOpenTransitionFrame()` [第91行]
- 调用 `EnsureShadowForCurrentBounds(0.0f)` → 创建 ShadowWindow + `GenerateShadowBitmap()`
  - `GenerateShadowBitmap()` 执行 **CPU 端嵌套循环**，对每个像素进行 4x4 超采样圆角检测 [ShadowWindow.cpp:224-262]
  - 然后执行 **两次 1D 高斯模糊**（水平+垂直），每次都是 `O(width × height × radius)` 的嵌套循环 [ShadowWindow.cpp:280-326]
  - 最后创建 `CreateDIBSection` + `UpdateLayeredWindow`
- 调用 `ApplyVisibilityFrame(0.0f, 1.0f)` → `SetWindowLongPtr(WS_EX_LAYERED)` + `SetLayeredWindowAttributes`

#### b) `CaptureBackground()` [第92行]
- `GetDC(nullptr)` → 获取屏幕 DC
- `CreateCompatibleDC` + `CreateCompatibleBitmap` → GDI 位图创建 [GlassWindow.cpp:663-683]
- **`BitBlt`** → 屏幕捕获，可能因 GPU 管线同步而阻塞 [GlassWindow.cpp:693]
- **`GetDIBits`** → 将像素数据复制到 CPU 缓冲区 [GlassWindow.cpp:720]
- `m_rt->CreateBitmap()` → 将像素数据上传回 GPU 的 D2D 位图 [GlassWindow.cpp:803]
- 全部操作同步执行，且有性能监控日志（阈值 12ms）[GlassWindow.cpp:820-840]

#### c) `CompositeBackgroundToCache()` [第93行]
- `CreateCompatibleRenderTarget` → D2D 兼容渲染目标 [GlassWindow.cpp:874]
- 查询 `ID2D1DeviceContext` 接口 → GPU 上下文获取 [GlassWindow.cpp:954]
- 创建 D2D 效果：`CLSID_D2D1GaussianBlur` + `CLSID_D2D1Saturation` [GlassWindow.cpp:958,970]
- 多层合成渲染：
  - 模糊背景 + 饱和度调整 [GlassWindow.cpp:983-1005]
  - 对角高光渐变（径向渐变画刷）[GlassWindow.cpp:1007-1043]
  - 强调色发光（第二个径向渐变）[GlassWindow.cpp:1045-1079]
  - 底色着色层 [GlassWindow.cpp:1081-1097]
  - 边框渐变 [GlassWindow.cpp:1101-1139]
- 全部是同步 D2D GPU 操作

**所有上述操作都在 `Application::HandleMessage()` 处理 `WM_RBUTTONUP` 的调用栈内同步执行，窗口在这一切完成之后才显示。**

Shell 在此期间的视角：它向应用发送了托盘右键通知，正在等待返回。UI 线程正在执行 GDI + D2D + GPU 密集型工作。这会使应用看起来像"忙碌"状态。

### 2. 次因：SetCapture 必定失败

```cpp
// TrayMenuWindow.cpp:168-176
void TrayMenuWindow::CaptureMouse()
{
    HWND h = GetHWND();
    if (!h || !IsWindow(h)) return;
    SetCapture(h);                                          // ← 第174行
    m_mouseCaptured = (GetCapture() == h);                  // ← 结果：永远为 false
}
```

`SetCapture` **仅在鼠标按钮被按下时有效**。由于此函数在 `WM_RBUTTONUP` 已被处理后调用（右键已释放），`SetCapture` **无条件失败**。`m_mouseCaptured` 始终为 `false`。

后果：
- 没有鼠标捕获，无法通过 `WM_CAPTURECHANGED` 检测点击菜单外部
- 点击外部关闭依赖 `WM_ACTIVATE`（第282-286行），该机制有效但存在时序差异
- 没有捕获意味着光标状态完全由系统/默认处理器管理，在 shell 的内部状态和我们自定义窗口的显示之间可能存在不一致的转换

### 3. 三次因：对 WS_EX_TOPMOST 弹出窗口调用 SetForegroundWindow

```cpp
// TrayMenuWindow.cpp:96-97
ShowWindow(s_instance->GetHWND(), SW_SHOW);
SetForegroundWindow(s_instance->GetHWND());                // 第97行
```

对 `WS_EX_TOPMOST` 弹出窗口调用 `SetForegroundWindow` 时：
- Windows 必须刷新前台队列
- DWM 可能执行前台过渡动画
- 在此过渡期间，Windows 可能短暂显示 `IDC_APPSTARTING` 或 `IDC_WAIT` 光标

### 4. 四次因：Shell 的内部状态管理

当托盘图标收到 `WM_RBUTTONUP` 时，Shell 内部：
- 可能为标准的 `TrackPopupMenu` 模式设置等待光标
- 期望处理器显示弹出菜单或返回来清理光标状态
- 由于我们在此消息上不调用 `TrackPopupMenu`，Shell 的内部光标管理可能无法正确清理

---

## 问题时序图

```
时间轴 →

[WM_RBUTTONUP 到达]
    |
    ├─ ShowTrayMenuAtCursor()
    |     |
    |     ├─ new TrayMenuWindow()         // 对象构造
    |     ├─ Create()                     // Win32 窗口创建
    |     ├─ SetWindowDisplayAffinity()
    |     ├─ ApplySystemBackdrop()
    |     ├─ EnsureD2D()                  // D2D/DWrite 初始化
    |     ├─ PrepareOpenTransitionFrame()
    |     |     ├─ EnsureShadowForCurrentBounds()
    |     |     |     └─ GenerateShadowBitmap()  ← CPU 密集型 (掩码 + 模糊)
    |     |     └─ ApplyVisibilityFrame()
    |     ├─ CaptureBackground()          ← GDI BitBlt (可能 GPU 阻塞)
    |     |     └─ CreateBitmap (D2D)     ← GPU 上传
    |     ├─ CompositeBackgroundToCache()  ← D2D 效果 + 多层合成 (GPU)
    |     ├─ ShowWindow(SW_SHOW)          ← ★ 窗口在此处才首次显示
    |     ├─ SetForegroundWindow()        ← 前台切换 (DWM 过渡)
    |     └─ CaptureMouse()               ← 必定失败 (无按钮按下)
    |
    └─ 返回 ... Shell 收到 Notification 处理完成

[用户看到菜单出现]  ← 可能有数帧延迟
```

---

## 严重程度评估

| 因素 | 影响 | 可重现性 |
|------|------|----------|
| GDI BitBlt 屏幕捕获 | 通常快速（1-5ms），但 GPU 忙时可能显著变慢 | 某些系统上间歇性出现 |
| D2D 位图上传 | 通常快速（<1ms） | 始终 |
| D2D 效果创建（首次） | 中等（5-20ms），GPU 驱动程序编译着色器 | 首次显示时 |
| 阴影位图生成 | 中等（5-15ms），CPU 循环 | 首次显示时 |
| SetForegroundWindow 过渡 | 不定（0-50ms），取决于 DWM | 始终 |
| 失败的 SetCapture | 光标无法正确转换 | 始终 |

**最坏情况总计**：高 DPI（200%）下首次显示菜单延迟可达 50-150ms，操作系统可能将此解读为"应用程序繁忙"并显示等待光标。

---

## 解决方案方向（仅供参考，不实施）

1. **将繁重工作延迟到窗口显示之后**：让 `ShowWindow` 先执行，将 `CaptureBackground()` + `CompositeBackgroundToCache()` 移到 `WM_PAINT` 或 `PostMessage` 触发的工作中
2. **实现正确的 TrackPopupMenu 回退**：使用 `TrackPopupMenu` + `TPM_RETURNCMD` 配合 `WM_INITMENUPOPUP` 实现原生右键菜单，完全避免自定义弹出窗口的开销
3. **预创建/缓存**：在空闲时提前创建 D2D 资源和阴影位图，使 Show() 路径保持轻量
4. **替换 SetCapture 方案**：使用 `SetWindowsHookEx(WH_MOUSE_LL)` 进行点击外部检测，而非依赖必定失败的 `SetCapture`
5. **在 shell 需要时显式设置前景窗口**：在托盘图标处理中添加 `SetForegroundWindow(主窗口 HWND)`，作为一种众所周知的托盘菜单变通方案
