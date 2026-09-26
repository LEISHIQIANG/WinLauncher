# WinLauncher 大文件拆解计划

> 制定日期：2026-09-26 · 状态：待执行
> 本计划是 `MAINTENANCE.md`「Refactoring workflow」的落地排期：小步、行为保持（behavior-preserving）、每片可验证、每片一个提交、可单独回滚。

## 1. 目标

- 把六个核心大文件降到 **1500 行以内**（理想 ≤1000），缩小稳定性问题的藏身面积，让触发/渲染/生命周期修复更容易定位与评审。
- 全程不改变任何用户可见行为、插件 ABI（`WLHostApiV1` 保持 append-only）、配置格式与消息约定。
- 每个切片是独立提交；出回归时单独 `git revert`，不影响其他切片。

## 2. 现状盘点（2026-09-26 快照）

| 文件 | 行数 | 主要职责 | 计划拆出 | 阶段后预计 |
|---|---|---|---|---|
| `WinLauncher/PopupWindow.cpp` | 4104 | 弹窗生命周期、渲染、输入、搜索、启动引擎 | ~1240 行 → `Popup/` 助手 | ~2850 |
| `WinLauncher/Config/SettingsPage.cpp` | 3370 | 设置页 6 分类绘制/hover/点击/hit-test | ~1950 行 | ~1400 |
| `WinLauncher/Config/ConfigWindow.cpp` | 2869 | 配置壳窗口 + `IConfigWindow` 门面 | ~1940 行 | ~930 |
| `WinLauncher/Config/ShortcutPage.cpp` | 2531 | 快捷方式网格：选择/拖拽/编辑/批量图标 | ~1710 行 | ~820 |
| `WinLauncher/App/PluginManager.cpp` | 2654 | 插件扫描/装载/宿主 API/搜索管线 | ~1190 行 | ~1460 |
| `WinLauncher/GlassWindow.cpp` | 2351 | 材质渲染管线、设备管理、显隐动画 | ~1400 行 | ~950 |

> ⚠️ 本文所有行号是制定计划时的快照，仅作定位参考；执行时以符号（函数/结构名）为准，动手前先重新确认边界。

## 3. 沿用的拆解模式（既有约定）

上一轮拆解（`Popup/PopupRenderHelper`、`SettingsTabHelper`、`PluginDllLoader` 等）已确立约定，本计划严格沿用：

1. **文件位置与命名**：助手放在所属模块目录，`Popup/Popup<域><角色>`、`Config/Settings<域>`、`App/Plugin<域>`、根目录 `Glass<域>`；PascalCase 类名与文件同名。
2. **两种形态**：
   - **无状态静态助手**（首选）：类内只有 public static 函数，或 namespace 自由函数；数据全部走参数；头文件显式声明"不知道 HWND/D2D/窗口状态"的契约。画笔等资源由调用方（窗口的 `GetOrCreateBrush`）创建后传入。
   - **有状态控制器**：小类由窗口**按值持有**（参照 `PopupFileSelectionController`、`PopupIconRefreshController`）：`Begin/Poll/Consume/Cancel` 接口 + `uint64_t m_generation` + `std::mutex` 做异步失效；窗口保留定时器与 UI 线程编排。
   - **成员函数第二翻译单元**（零风险纯物理拆分）：成员函数签名不变，仅把定义移到 `XxxWindowSettings.cpp` 等新 .cpp；头文件不动。
3. **头文件最小化**：`#pragma once`，最少 include；`IConfigWindow` 等耦合类型在头文件仅前置声明，.cpp 里再 include。
4. **防御式守卫**：每个助手函数开头 `if (!rt || !param) return;`。
5. **工程注册（同一切片内完成）**：
   - `WinLauncher.vcxproj`：`<ClCompile Include="Popup\X.cpp" />`（现有 Popup 块 ~L152–159）/ `<ClInclude Include="Popup\X.h" />`（~L268–277）；Config 文件同理（ClCompile ~L160–165、ClInclude ~L278–283）。
   - `WinLauncher.vcxproj.filters`：只挂到现有的 `源文件` / `头文件` 两个平铺 filter，不新建子 filter。
6. **错误返回**：out-param `std::wstring* errorMessage`，不用异常。

## 4. 统一切片流程（每片必须全部完成）

1. **移动代码**：逐字搬运，只做机械化适配（include、namespace、参数化状态）；禁止顺手改逻辑、改名、调常量。
2. **注册工程**：vcxproj + .filters 同步加入新文件（见上节第 5 条）。
3. **构建**：`& "E:\Visual Studio 2026\MSBuild\Current\Bin\MSBuild.exe" WinLauncher.sln /p:Configuration=Release /p:Platform=x64 /m:1`
4. **检查**：`.\scripts\maintenance_check.ps1`；涉及脚本/CI 面时再跑 `.\scripts\ci_check.ps1`；建议同时跑 `tests\run_tests.ps1`（静态 + 原生测试）。
5. **手动冒烟**（按切片涉及面选做，阶段收尾必做全项）：
   - PopupWindow 阶段：唤出/隐藏、搜索（本地/拼音/斜杠）、数字键 1–9 直启、拖放文件到图标、翻页滚轮、中键触发、固定(pin)模式。
   - Config 阶段：设置打开/保存、每个分类切换、外观滑杆、插件安装/启停/卸载、撤销重做(Ctrl+Z/Y)、快捷方式增删改/拖拽排序/跨分类移动。
   - PluginManager 阶段：无插件目录/无效清单/禁用插件启动、插件命令执行、私有配置读写。
   - GlassWindow 阶段：材质切换（亚克力↔发光↔玻璃）、深色主题、首次显示阴影顺序、DPI 切换、设备丢失回退。
6. **更新 `RELEASE_NOTES.md`**：按 `AGENTS.md` 契约，合并进当前版本的"单体大文件解耦"条目，不重复造条目。
7. **提交**：Conventional Commits，格式 `refactor(<scope>): extract <目标> from <源文件>`，例如 `refactor(popup): extract PopupTimeZoneAction from PopupWindow`。

## 5. 阶段与切片明细

### 阶段 1：PopupWindow.cpp（主弹窗，最高优先级）

| 切片 | 新文件 | 移出内容（快照行号） | 行数 | 风险 |
|---|---|---|---|---|
| P1-1 | `Popup/PopupTimeZoneAction` | 时区切换静态函数组 269–394（零窗口状态，`s_running` 原子一并移走） | ~126 | 极低 |
| P1-2 | `Popup/PopupShortcutSorter` | `SortPageByUsage` 144–184（注意位图并行重排守卫 152–153 原样搬） | ~45 | 极低 |
| P1-3 | `Popup/PopupIconCache` | `PopupIconCacheKey` 235–253 + `PreserveLoadedIcons/CopyCachedIcon/RememberLoadedIcon/ClearLoadedIconCache` 586–638，按值持有类替换 `m_loadedIconCache`；保持"先换 hIcon 再 Remember"的顺序（2296–2312） | ~90 | 低 |
| P1-4 | 扩展 `Popup/PopupLayout` | 窗口尺寸/停靠几何三处重复：818–828、1059–1141、2837–2846 → `ComputeWindowMetrics` + `ResolveAlignPosition`；保留 900px 上限与 alignMode=3 语义差异 | ~60 | 低 |
| P1-5 | `Popup/PopupShortcutLauncher` | 启动引擎：88–142、3686–3953（`ParseVirtualKey/SimulateHotkey/ExpandVariables/LaunchUrl/LaunchCommand`）+ `LaunchShortcut/ExecuteShortcut` 3955–4071。**要点**：`ExpandVariables` 改为显式参数（选中文件+有效期）传入，删除 `friend` 声明（PopupWindow.h:204）；宏分支读 `m_pinned/m_restoreForegroundWnd` 改为调用方构造 `PopupLaunchHost{waitForClose, restoreWnd}`；`PopupWindow::ExecuteShortcut` 保留为一行转发（公共 API） | ~570 | 低-中 |
| P1-6 | 扩展 `Popup/PopupRenderHelper` | 网格卡片/图标/标签三重循环去重：`DrawPage` 2377–2430、`DrawDock` 2548–2598、`DrawSearchResults` 1916–2027。画笔仍由调用方创建；选中/hover 配色按参数传入**不做数值统一**；图标闪烁遮罩保留在类内包装 | ~200 | 中 |
| P1-7 | （类内重构，0 行净移出） | `HandleMessage` 2780–3682 的 WM_TIMER/WM_MOUSEMOVE/WM_LBUTTONDOWN/WM_LBUTTONUP/WM_KEYDOWN 等分支体拆为私有 `OnTimer/OnMouseMove/...` 成员方法；共享 Timer ID（60–66、80–81）与 `WM_USER` 码（73–75）先收进 `Popup/PopupWindowTimers.h`，为后续 TU 拆分铺路 | 0 | 低 |
| P1-8 | `Popup/PopupScrollAnimator` | 翻页弹簧积分器 2630–2778（`StartPageAnimationLoop/StepPageAnimationFrame`），拥有 `m_animating/m_animLastTime/m_scrollPosition/m_scrollVelocity`；窗口保留定时器、`m_currentPage` 同步与 `InvalidateRect` | ~150 | 中-高 |

### 阶段 2：SettingsPage.cpp

| 切片 | 新文件 | 移出内容（快照行号） | 行数 | 风险 |
|---|---|---|---|---|
| S1 | `Config/SettingsPageLayout.h` + `Config/SettingsPageHitTest.cpp` | 布局矩形工厂/标签表 62–168 → Layout 头文件（inline 自由函数）；hit-test 43 个方法 2964–3370 → 保持成员身份、仅移到第二 TU，零调用点变化 | ~500 | 极低 |
| S2 | `Config/SettingsControlKit`（或并入 `SettingsTabHelper`） | `OnPaint` 内 8 个绘制 lambda 提升为静态函数：`drawInlineCheckbox` 573、`drawSegmentButton` 1314、`drawStepperCard` 1353、`drawStepButton` 1392、`drawActionButton` 1653、`drawSmallButton` 1735、`drawInfoCard` 1890 + 两段重复滑杆卡 612–694/695–810 去重 | ~400 | 低 |
| S3 | 充实 `SettingsPluginView` 空壳 + `Config/SettingsPluginActions` | 插件分类全套：绘制 1705–1870、hover 2200–2223、点击 2695–2832、拖放安装 2864–2962、hit-test 3315–3370；交互经 `IConfigWindow*`，hover 标志以 in/out 参数保持原行为 | ~440 | 低-中 |
| S4 | `Config/SettingsPresetMenus` | 预设菜单与黑名单编辑器：标签表 137–168、文本工具 170–218、`ShowTriggerPresetMenu` 279–317、`ShowPopupAlignPresetMenu` 319–354、`ShowTriggerBlacklistEditor` 356–375 | ~210 | 低 |
| S5 | 充实 `SettingsBackupView` 空壳 + 新 `Config/SettingsAboutView` | 配置管理分类渲染 1577–1704、关于分类渲染 1871–1951（hover 标志指针传入） | ~210 | 低 |
| S6 | `Config/SettingsSelectionAnimator` | 选择高亮动画：`SelectionVisual` 结构 + `GetSelectionRect` 256–277、`DrawSelectionHighlight` 377–400、`UpdateAnimation` 402–444，按值持有类，`IsAnimating` 转发 | ~190 | 中 |

### 阶段 3：ConfigWindow.cpp

| 切片 | 新文件 | 移出内容（快照行号） | 行数 | 风险 |
|---|---|---|---|---|
| C1 | `Config/ConfigWindowSettings.cpp`（第二 TU） | 属性转发门面（~60 个 2–8 行委托）1616–2112，纯物理移动，头文件不动 | ~497 | 极低 |
| C2 | `Config/ShortcutHistoryController` | 撤销/重做：645–855 + 映射/比较工具 38–119 + 头文件快照结构与 deque；`LoadConfig/SaveConfig/StartAnimation` 编排留在窗口，控制器管数据与 `m_applyingShortcutHistory` 守卫 | ~330 | 低 |
| C3 | `Config/ConfigMaintenanceOps` | 维护门面：日志/诊断包/迁移备份/清缓存 1153–1425 + `CleanupUserDataDirectory` 121–254；各操作后的 `ReloadAfterConfigFileOperation`/`NotifyConfigChanged` 顺序保持在窗口方法内 | ~410 | 低 |
| C4 | `Config/QuickLauncherImportFlow` | `ImportJsonConfig` 1427–1578（QuickLauncher 导入向导），窗口保留对话框与重载编排 | ~155 | 低 |
| C5 | `Config/IconBackfillCoordinator` | 图标后台回填：405–590 + `IconBackfillResult/State` 结构；按值持有 + generation；析构与 `LoadConfig/ClearPages` 的失效点原样保留，UI 线程应用回调留在窗口 | ~300 | 中 |
| C6 | `Config/ConfigChromeRenderer` | 壳渲染：标题/关闭/设置/添加按钮 + 更新 pill 2286–2425、2760–2808；pill 几何三处重复先**逐常量核对一致**再合一（72/98/110/126/138） | ~250 | 中-低 |

### 阶段 4：ShortcutPage.cpp

| 切片 | 新文件 | 移出内容（快照行号） | 行数 | 风险 |
|---|---|---|---|---|
| K1 | `Config/DeleteCursorFactory` | `GetDeleteCursor` 1811–1975（166 行手绘像素光标），函数级 static 缓存或沿用 `m_deleteCursor` | ~166 | 极低 |
| K2 | `Config/ShortcutDialogController` | 新增对话框族 `ShowAdd*Dialog` 148–357 + `EditShortcut` 2271–2519 + `ResolveEditedIconSource` 38–42；模态对话框纯搬迁，字段 diff 块逐字移动 | ~460 | 低 |
| K3 | 扩展 `ShortcutGridViewHelper` + 笔刷缓存小类 | 卡片绘制循环 452–560 + `GetOrCreateBrush/GetOrCreateBitmapBrush` 1283–1324；`EnsureIcons`/`UpdateTheme` 的失效点行为保持 | ~150 | 低 |
| K4 | `Config/ShortcutSelectionModel` | 选择/删除谓词与索引变换：1394–1580、1726–1787；数据留在页面，只移纯函数 | ~320 | 低-中 |
| K5 | `Config/FaviconBatchFetcher` | 批量网址图标：`BatchFaviconState` 44–92 + 1582–1724；generation + 取消检查（`IsCurrentTaskCancellationRequested`）原样；历史记录标志与 `RecordShortcutHistoryCheckpoint` 的顺序保持 | ~235 | 中 |
| K6 | `Config/ShortcutDragController` | 拖拽排序/跨分类移动/删除区：`OnLButtonUp` 主体 828–969 + 1977–2131；按值持有控制器 + 回调进页面；与 `MouseCaptureController` 手势、`m_shortcutStates` 动画目标的耦合最重，**最后做** | ~380 | 高 |

### 阶段 5：PluginManager.cpp

| 切片 | 新文件 | 移出内容（快照行号） | 行数 | 风险 |
|---|---|---|---|---|
| M1 | `App/PluginManagerHostApi.cpp`（第二 TU，可选再拆 `...HostApiIo.cpp`） | 全部 `Host*` 实现 1953–2628（~40 个静态成员函数，签名不变）+ 其专属匿名工具（消息框/文件对话框/utf8/thread-local panel 等）。可把 `HostHttpRequest/HostRunProcess/HostGetScreenInfo` + `#pragma comment(lib,"winhttp.lib")` 隔离到 Io TU。`LoadPlugin` 内的派发表装配 1274–1312 不动 | ~700 | 极低 |
| M2 | `App/PluginConfigStore` | 插件私有配置：`IsSafePluginConfigKey/PluginConfigPath/Read/WritePluginConfigValue` 1760–1848 + JSON 工具；原子写顺序保持；`PluginStateStore` 的重复 `ToUtf8/EscapeJsonString` **本切片不合并**（留后续治理） | ~110 | 低 |
| M3 | `App/PluginTextUtil` | 无状态字符串/斜杠命令工具：107–181、228–287；`PluginManifest/PluginInstaller/SettingsPage` 各自的 `JoinPath` 副本不动 | ~130 | 极低 |
| M4 | `App/PluginDialogRegistry` | loading/progress 对话框注册表：1850–1879 + 头文件状态 308–310；句柄从 1 单调递增保持 | ~55 | 低 |
| M5 | `Services/PluginProcessRunner` | `HostRunProcess` 主体 2506–2597（去掉权限前置）+ `WaitForProcessWithTimeout`；1MB 上限/25ms 轮询/超时 kill 逐字保持；路径安全策略留在 PluginManager（信任边界不移动） | ~95 | 低-中 |
| M6 | `Services/PluginHttpClient` | `HostHttpRequest` 主体 2409–2504 + `IsHttpMethodAllowed`；URL 缓冲尺寸、30s 默认超时、4MB 上限、两处取消检查、UA 字符串 `WinLauncherPlugin/1.0` 逐字节保持；winhttp pragma 随迁 | ~100 | 低-中 |

**明确暂缓**：搜索管线（683–807、1169–1245、2630–2654）与生命周期核心耦合过深（`Rescan/Shutdown` 均以其为门禁），本轮不拆。

### 阶段 6：GlassWindow.cpp（近期改动最密集，放最后）

| 切片 | 新文件 | 移出内容（快照行号） | 行数 | 风险 |
|---|---|---|---|---|
| G1 | `GlassBackgroundCapture` | `CaptureBackground` 677–893 + `m_bgCap/m_pixbuf/错误节流日志` 状态；"失败不得提升旧合成结果"的契约（1240–1254 编排留在窗口）保持 | ~230 | 低 |
| G2 | `GlassThemeTransition` | 主题过渡：2266–2351 + 头文件 163–170 状态；与 0x889 定时器共享的合并 tick 先留在 `HandleMessage` | ~90 | 低-中 |
| G3 | `GlassBackdrop` | `SetAccent` 32–45 + `ApplySystemBackdrop` 278–406 + 圆角/区域 222–276 + `m_lastAppliedAccentState`。⚠️ 这是最近一次修复的重灾区：accent 过渡恢复与 383–387 的 `SWP_FRAMECHANGED` 收尾顺序敏感，整体逐字搬移反而便于集中评审 | ~150 | 中 |
| G4 | `GlassMaterialComposite` | 材质合成管线：`MaterialFrameGeometry` 176–208 + `CompositeBackgroundToCache` 895–1238 + 效果缓存状态；缓存键 `(w,h,radius)`、深色边缘、层 push/pop 配对原样；设备丢失与 DPI 变化路径必须路由进 `Reset()` | ~380 | 中-高 |
| G5 | `GlassRenderDevice` | 设备/资源生命周期：`EnsureD2D/ReleaseD2D/ResetBackgroundResources` 408–587 + 笔刷缓存 73–107；`ResetBackgroundResources` 内部释放顺序逐项保持；设备丢失策略（GPU 崩溃标记 + 全局 SW 降级）两处重复可合一（行为等价） | ~250 | 中 |
| G6 | `GlassVisibilityAnimator` | 显隐/首帧 reveal 动画：1990–2264 + 头文件 147–159 状态。⚠️ `m_revealFirstFrameBarrier` 被 5 处读取、完成回调可能中途销毁 HWND（1825–1828 守卫）、与主题过渡共享 0x889；需要 Host 接口回指，**全计划最后做** | ~300 | 高 |

## 6. 红线（明确不做的事）

1. **不把 `PluginDllLoader` 接入 `LoadPlugin`**：两者语义已分歧（LoadLibrary 标志、ABI 导出严格度、卸载顺序 `onUnload` 缺失），接通等于行为变更；维持现状，另立项处理。
2. **不动 `WLHostApiV1` 与 SDK ABI**：M1 切片只移动函数定义，派发表装配与函数地址不变。
3. **不合并重复实现**（`ToUtf8`/`JoinPath`/设备丢失块等）：合并是行为变更风险点，只在切片注记，留独立小切片处理。
4. **不夹带行为/性能/视觉优化**：与拆解混在同一提交会让回归无法二分。
5. **GlassWindow 高风险区附加验证**：G3/G6 落地后必须手动过材质切换边线、玻璃深色主题、首帧阴影顺序、DPI 切换四项。

## 7. 风险与回滚

- 每片一个提交 → 回归时 `git revert <slice-commit>`，不牵连。
- 拆解期间若主线出现功能性修复，优先让修复先行，本计划切片在其后 rebase/续作；GlassWindow 阶段尤其如此。
- 大范围移动后 `git blame` 会断链：每个切片提交信息中记录"自 `<源文件>` 原样迁出"，便于追溯。

## 8. 完成标准

- 六个文件全部 ≤1500 行（理想 ≤1000），无超过 500 行的单个函数。
- 全部切片通过：Release x64 构建、`maintenance_check.ps1`、`tests\run_tests.ps1`、对应阶段的手动冒烟清单。
- 无残留：旧文件、vcxproj/filters、测试、文档中不残留对已移出符号的失效引用。
- `RELEASE_NOTES.md` 当前版本小节合并记录本轮拆解（用户可读口径）。

## 9. 进度跟踪

- [x] 阶段 1 PopupWindow：P1-1 ✅ P1-2 ✅ P1-3 ✅ P1-4 ✅ P1-5 ✅ P1-6 ✅ P1-7 ✅ P1-8 ✅（4104 → 3403 行）
- [x] 阶段 2 SettingsPage：S1 ✅ S2 ✅ S3 ✅ S4 ✅ S5 ✅ S6 ✅（3370 → 1919 行）
- [ ] 阶段 3 ConfigWindow：C1 ✅ C2 ✅ C3 ✅ C4 ☐ C5 ☐ C6 ☐
- [ ] 阶段 4 ShortcutPage：K1 ☐ K2 ☐ K3 ☐ K4 ☐ K5 ☐ K6 ☐
- [ ] 阶段 5 PluginManager：M1 ☐ M2 ☐ M3 ☐ M4 ☐ M5 ☐ M6 ☐
- [ ] 阶段 6 GlassWindow：G1 ☐ G2 ☐ G3 ☐ G4 ☐ G5 ☐ G6 ☐

> 执行注记（2026-09-26）：P1-1~P1-5 已按计划落地，每片独立提交并通过 Release 构建、`maintenance_check.ps1` 与 `tests\run_tests.ps1`。
> 期间同步更新了 `tests\project_static_tests.ps1` 中盯源码位置的断言（指向新的 Popup/ 文件）。
> 本机 MSBuild 实际路径为 `C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe`（MAINTENANCE.md 中 E 盘路径失效）。
