#pragma once

// ============================================================
//  WinLauncher 版本号管理中心
//  修改以下宏即可统一更新整个项目的版本号
// ============================================================

#define WINLAUNCHER_VERSION_MAJOR    0
#define WINLAUNCHER_VERSION_MINOR    5
#define WINLAUNCHER_VERSION_PATCH    1
#define WINLAUNCHER_VERSION_BUILD    5

// -- 下面的内容通常无需手动修改 --

// 用于 .rc 资源文件的逗号分隔格式
#define WINLAUNCHER_VERSION_RC       WINLAUNCHER_VERSION_MAJOR, WINLAUNCHER_VERSION_MINOR, WINLAUNCHER_VERSION_PATCH, WINLAUNCHER_VERSION_BUILD

// 辅助宏：将数字转为字符串
#define WINLAUNCHER_VER_STRINGIFY(x)        WINLAUNCHER_VER_STRINGIFY2(x)
#define WINLAUNCHER_VER_STRINGIFY2(x)       #x

// 宽字符版本号字符串 (L"0.0.0.1")
#define WINLAUNCHER_VERSION_WSTR            L"" WINLAUNCHER_VER_STRINGIFY(WINLAUNCHER_VERSION_MAJOR) L"." \
                                            L"" WINLAUNCHER_VER_STRINGIFY(WINLAUNCHER_VERSION_MINOR) L"." \
                                            L"" WINLAUNCHER_VER_STRINGIFY(WINLAUNCHER_VERSION_PATCH) L"." \
                                            L"" WINLAUNCHER_VER_STRINGIFY(WINLAUNCHER_VERSION_BUILD)

// 多字节版本号字符串 ("0.0.0.1")
#define WINLAUNCHER_VERSION_ASTR            WINLAUNCHER_VER_STRINGIFY(WINLAUNCHER_VERSION_MAJOR) "." \
                                            WINLAUNCHER_VER_STRINGIFY(WINLAUNCHER_VERSION_MINOR) "." \
                                            WINLAUNCHER_VER_STRINGIFY(WINLAUNCHER_VERSION_PATCH) "." \
                                            WINLAUNCHER_VER_STRINGIFY(WINLAUNCHER_VERSION_BUILD)
