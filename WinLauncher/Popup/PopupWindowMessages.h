#pragma once
#include <Windows.h>

// 弹窗窗口的定时器 ID、私有 WM_USER 消息码与帧节奏常量的唯一定义，
// 供窗口的编排、输入与绘制翻译单元共享（AppMessages.h 的弹窗侧对应物）。
namespace PopupWindowMessages
{
    // 内部定时器 ID
    constexpr UINT_PTR AutoHideTimerId = 1;           // 自动隐藏轮询 / 光标闪烁
    constexpr UINT_PTR PageAnimationTimerId = 2;      // 翻页弹簧动画
    constexpr UINT_PTR TimelineAnimationTimerId = 3;  // 选中文件时间线
    constexpr UINT_PTR ClickCloseTimerId = 4;         // 点击外部延迟关闭
    constexpr UINT_PTR PluginSearchTimerId = 5;       // 插件搜索刷新
    constexpr UINT_PTR FileSelectionTimerId = 6;      // 选中文件捕获轮询
    constexpr UINT_PTR IconProgressTimer = 0xA713;    // 图标后台加载进度
    constexpr UINT_PTR IconFlashTimer = 0xA714;       // 双击空白图标闪烁反馈

    // 帧节奏与刷新间隔
    constexpr UINT PageAnimationFrameMs = 8;
    constexpr UINT TimelineAnimationFrameMs = 16;
    constexpr UINT PluginSearchRefreshMs = 120;

    // 私有消息（WM_USER 区间，分配前先查 AppMessages.h 与此表避免冲突）
    constexpr UINT Animate = WM_USER + 100;
    constexpr UINT RefreshIcons = WM_USER + 101;
    constexpr UINT SelectionUpdated = WM_USER + 102;

    // 性能阈值与默认值
    constexpr double SlowShowMs = 24.0;
    constexpr double SlowFrameMs = 16.0;
    constexpr double SlowIconRefreshMs = 40.0;
    constexpr int DefaultFileSelectionValiditySeconds = 15;
}
