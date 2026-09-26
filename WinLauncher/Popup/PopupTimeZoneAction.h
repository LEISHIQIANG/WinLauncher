#pragma once

struct AppContext;

// Self-contained "China <-> Los Angeles" time-zone toggle behind the popup's
// built-in system action. Runs tzutil.exe hidden on a background task so the
// UI thread never waits on the OS; a second toggle while one is still in
// flight is ignored.
namespace PopupTimeZoneAction
{
    bool ToggleChinaLosAngelesAsync(AppContext* ctx);
}
