#pragma once
#include <windows.h>
#include <string>
#include <functional>

struct AppContext;
class ConfigViewModel;

namespace QuickLauncherImportFlow
{
    bool Run(HWND hwndOwner, AppContext* appCtx, const std::wstring& configDir, ConfigViewModel* viewModel, const std::function<void()>& onReload);
}
