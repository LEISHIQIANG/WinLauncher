#include <WinLauncher/WinLauncherPluginABI.h>
#include <cwchar>
#include <string>
#include <vector>

#include <windows.h>
#include <shellscalingapi.h>
#include <powrprof.h>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "kernel32.lib")
#pragma comment(lib, "shcore.lib")
#pragma comment(lib, "powrprof.lib")

namespace
{
    using namespace std;

    void WriteResult(WLStringResultV1* out, const wstring& text)
    {
        if (!out) return;
        uint32_t n = (uint32_t)text.size() + 1;
        out->requiredLength = n;
        if (out->buffer && out->bufferLength >= n)
            wcscpy_s(out->buffer, out->bufferLength, text.c_str());
    }

    // ── CPU Info ────────────────────────────────────────────────────

    wstring GetCpuInfo()
    {
        // Get CPU name via registry
        wchar_t cpuName[256] = L"Unknown";
        HKEY hKey;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
            L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
            0, KEY_READ, &hKey) == ERROR_SUCCESS)
        {
            DWORD size = sizeof(cpuName);
            RegQueryValueExW(hKey, L"ProcessorNameString", nullptr, nullptr, (BYTE*)cpuName, &size);
            RegCloseKey(hKey);
        }

        SYSTEM_INFO si;
        GetNativeSystemInfo(&si);

        // Trim trailing spaces from the CPU name
        wstring name(cpuName);
        while (!name.empty() && name.back() == L' ') name.pop_back();

        wchar_t buf[512]{};
        swprintf_s(buf, L"CPU: %s\nCores: %u (logical)\nArchitecture: %s",
            name.c_str(),
            si.dwNumberOfProcessors,
#ifdef _M_AMD64
            L"x64"
#else
            L"x86"
#endif
        );
        return buf;
    }

    // ── Memory Info ─────────────────────────────────────────────────

    wstring GetMemoryInfo()
    {
        MEMORYSTATUSEX msx{};
        msx.dwLength = sizeof(msx);
        if (!GlobalMemoryStatusEx(&msx))
            return L"Error: GlobalMemoryStatusEx failed";

        auto fmtBytes = [](DWORDLONG bytes) -> wstring {
            const wchar_t* units[] = { L"B", L"KB", L"MB", L"GB", L"TB" };
            int idx = 0;
            double val = (double)bytes;
            while (val >= 1024.0 && idx < 4) { val /= 1024.0; ++idx; }
            wchar_t buf[64]{};
            swprintf_s(buf, L"%.2f %s", val, units[idx]);
            return buf;
        };

        DWORDLONG total = msx.ullTotalPhys;
        DWORDLONG avail = msx.ullAvailPhys;
        DWORDLONG used = total - avail;
        int percent = total ? (int)((used * 100) / total) : 0;

        wchar_t buf[512]{};
        swprintf_s(buf, L"Physical Memory:\n  Total:   %s\n  Used:    %s (%d%%)\n  Available: %s\n\nVirtual Memory:\n  Total:   %s\n  Available: %s\n\nPage File:\n  Total:   %s\n  Available: %s",
            fmtBytes(total).c_str(),
            fmtBytes(used).c_str(), percent,
            fmtBytes(avail).c_str(),
            fmtBytes(msx.ullTotalVirtual).c_str(),
            fmtBytes(msx.ullAvailVirtual).c_str(),
            fmtBytes(msx.ullTotalPageFile).c_str(),
            fmtBytes(msx.ullAvailPageFile).c_str()
        );
        return buf;
    }

    // ── Disk Info ───────────────────────────────────────────────────

    wstring GetDiskInfo()
    {
        wstring result;
        wchar_t drives[256]{};
        DWORD len = GetLogicalDriveStringsW((DWORD)_countof(drives), drives);
        if (len == 0) return L"Error: GetLogicalDriveStringsW failed";

        for (const wchar_t* d = drives; *d; d += wcslen(d) + 1)
        {
            UINT dt = GetDriveTypeW(d);
            if (dt != DRIVE_FIXED && dt != DRIVE_REMOVABLE) continue;

            ULARGE_INTEGER freeBytes, totalBytes, totalFreeBytes;
            if (!GetDiskFreeSpaceExW(d, &freeBytes, &totalBytes, &totalFreeBytes)) continue;

            auto fmt = [](DWORDLONG b) -> wstring {
                const wchar_t* u[] = { L"B", L"KB", L"MB", L"GB", L"TB" };
                int i = 0; double v = (double)b;
                while (v >= 1024.0 && i < 4) { v /= 1024.0; ++i; }
                wchar_t buf[32]{}; swprintf_s(buf, L"%6.1f %s", v, u[i]); return buf;
            };

            DWORDLONG used = totalBytes.QuadPart - freeBytes.QuadPart;
            int pct = totalBytes.QuadPart ? (int)((used * 100) / totalBytes.QuadPart) : 0;

            wchar_t line[256]{};
            swprintf_s(line, L"%s %s / %s  (%d%% used)\n", d, fmt(used).c_str(), fmt(totalBytes.QuadPart).c_str(), pct);
            result += line;
        }

        if (result.empty()) result = L"No fixed drives found.";
        return L"Disk Usage:\n" + result;
    }

    // ── System Uptime ───────────────────────────────────────────────

    wstring GetUptime()
    {
        ULONGLONG sec = GetTickCount64() / 1000;
        ULONGLONG days = sec / 86400; sec %= 86400;
        ULONGLONG hours = sec / 3600; sec %= 3600;
        ULONGLONG mins = sec / 60;
        ULONGLONG secs = sec % 60;

        wchar_t buf[128]{};
        swprintf_s(buf, L"System Uptime: %llu day%s %02llu:%02llu:%02llu",
            days, days == 1 ? L"" : L"s", hours, mins, secs);
        return buf;
    }

    // ── Full System Summary ─────────────────────────────────────────

    wstring GetSystemSummary()
    {
        // OS version
        wchar_t os[128] = L"Unknown";
        HKEY hKey;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
            L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
            0, KEY_READ, &hKey) == ERROR_SUCCESS)
        {
            wchar_t pn[128] = L"", cv[128] = L"";
            wchar_t buildNum[16] = L"";
            DWORD size = sizeof(pn);
            RegQueryValueExW(hKey, L"ProductName", nullptr, nullptr, (BYTE*)pn, &size);
            size = sizeof(cv);
            RegQueryValueExW(hKey, L"DisplayVersion", nullptr, nullptr, (BYTE*)cv, &size);
            size = sizeof(buildNum);
            RegQueryValueExW(hKey, L"CurrentBuild", nullptr, nullptr, (BYTE*)buildNum, &size);

            // Windows 11 is build 22000+ but ProductName still says "Windows 10"
            int build = _wtoi(buildNum);
            wstring fixedName(pn);
            if (build >= 22000)
            {
                size_t pos = fixedName.find(L"Windows 10");
                if (pos != wstring::npos)
                    fixedName.replace(pos, 10, L"Windows 11");
            }

            if (cv[0] != L'\0')
                swprintf_s(os, L"%s (Build %s)", fixedName.c_str(), cv);
            else
                swprintf_s(os, L"%s (Build %s)", fixedName.c_str(), buildNum);
            RegCloseKey(hKey);
        }

        // Motherboard / BIOS
        wchar_t board[256] = L"Unknown";
        {
            HKEY hBio;
            if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                L"HARDWARE\\DESCRIPTION\\System\\BIOS",
                0, KEY_READ, &hBio) == ERROR_SUCCESS)
            {
                wchar_t mfr[64] = L"", prod[128] = L"", bios[64] = L"";
                DWORD sz = sizeof(mfr);
                RegQueryValueExW(hBio, L"BaseBoardManufacturer", nullptr, nullptr, (BYTE*)mfr, &sz);
                sz = sizeof(prod);
                RegQueryValueExW(hBio, L"BaseBoardProduct", nullptr, nullptr, (BYTE*)prod, &sz);
                sz = sizeof(bios);
                RegQueryValueExW(hBio, L"BIOSVersion", nullptr, nullptr, (BYTE*)bios, &sz);
                RegCloseKey(hBio);
                swprintf_s(board, L"%s %s  (BIOS %s)", mfr, prod, bios);
            }
        }

        // Power plan
        wchar_t powerPlan[64] = L"Unknown";
        {
            GUID* activeGuid = nullptr;
            if (PowerGetActiveScheme(nullptr, &activeGuid) == ERROR_SUCCESS && activeGuid)
            {
                wchar_t guidStr[64]{};
                swprintf_s(guidStr, L"{%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}",
                    activeGuid->Data1, activeGuid->Data2, activeGuid->Data3,
                    activeGuid->Data4[0], activeGuid->Data4[1], activeGuid->Data4[2], activeGuid->Data4[3],
                    activeGuid->Data4[4], activeGuid->Data4[5], activeGuid->Data4[6], activeGuid->Data4[7]);

                if (wcscmp(guidStr, L"{381B4222-F694-41F0-9685-FF5BB260DF2E}") == 0)
                    wcscpy_s(powerPlan, L"Balanced");
                else if (wcscmp(guidStr, L"{8C5E7FDA-E8BF-4A96-9A85-A6E23A8C635C}") == 0)
                    wcscpy_s(powerPlan, L"High Performance");
                else if (wcscmp(guidStr, L"{A1841308-3541-4FAB-BC81-F71556F20B4A}") == 0)
                    wcscpy_s(powerPlan, L"Power Saver");
                else if (wcscmp(guidStr, L"{E9A42B02-D5DF-448D-AA00-03F14749EB61}") == 0)
                    wcscpy_s(powerPlan, L"Ultimate Performance");
                else
                    swprintf_s(powerPlan, L"Custom (%s)", guidStr);

                LocalFree(activeGuid);
            }
        }

        // CPU
        wchar_t cpu[256] = L"Unknown";
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
            L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
            0, KEY_READ, &hKey) == ERROR_SUCCESS)
        {
            DWORD size = sizeof(cpu);
            RegQueryValueExW(hKey, L"ProcessorNameString", nullptr, nullptr, (BYTE*)cpu, &size);
            RegCloseKey(hKey);
        }
        wstring cpuName(cpu);
        while (!cpuName.empty() && cpuName.back() == L' ') cpuName.pop_back();

        SYSTEM_INFO si;
        GetNativeSystemInfo(&si);

        MEMORYSTATUSEX msx{};
        msx.dwLength = sizeof(msx);
        GlobalMemoryStatusEx(&msx);

        auto fmt = [](DWORDLONG b) -> wstring {
            const wchar_t* u[] = { L"B", L"KB", L"MB", L"GB", L"TB" };
            int i = 0; double v = (double)b;
            while (v >= 1024.0 && i < 4) { v /= 1024.0; ++i; }
            wchar_t buf[32]{}; swprintf_s(buf, L"%.2f %s", v, u[i]); return buf;
        };

        // GPU
        wchar_t gpu[128] = L"Unknown";
        DISPLAY_DEVICEW dd{};
        dd.cb = sizeof(dd);
        if (EnumDisplayDevicesW(nullptr, 0, &dd, 0))
        {
            // Strip trailing spaces
            wcscpy_s(gpu, dd.DeviceString);
            size_t glen = wcslen(gpu);
            while (glen > 0 && gpu[glen - 1] == L' ') gpu[--glen] = L'\0';
        }

        // Disk: all fixed drives
        wstring diskLines;
        {
            wchar_t drives[256]{};
            DWORD len = GetLogicalDriveStringsW((DWORD)_countof(drives), drives);
            for (const wchar_t* d = drives; d && *d; d += wcslen(d) + 1)
            {
                if (GetDriveTypeW(d) != DRIVE_FIXED) continue;
                ULARGE_INTEGER freeBytes, totalBytes, dummy;
                if (!GetDiskFreeSpaceExW(d, &freeBytes, &totalBytes, &dummy)) continue;

                DWORDLONG used = totalBytes.QuadPart - freeBytes.QuadPart;
                int pct = totalBytes.QuadPart ? (int)((used * 100) / totalBytes.QuadPart) : 0;

                wchar_t line[128]{};
                swprintf_s(line, L"Disk %-4s %s / %s  (%d%%)\n",
                    d, fmt(used).c_str(), fmt(totalBytes.QuadPart).c_str(), pct);
                diskLines += line;
            }
        }
        if (diskLines.empty()) diskLines = L"Disk:     (none)\n";

        // Memory percentage
        int memPct = msx.ullTotalPhys ? (int)(((msx.ullTotalPhys - msx.ullAvailPhys) * 100) / msx.ullTotalPhys) : 0;

        // Architecture
        const wchar_t* arch = L"x86";
#ifdef _M_AMD64
        arch = L"x64";
#elif defined(_M_ARM64)
        arch = L"ARM64";
#endif

        // Display: enumerate all monitors with model, resolution, DPI, refresh rate
        wstring displayLines;
        {
            struct MonitorInfo { wstring name; int w, h, dpi, hz; bool primary; };
            vector<MonitorInfo> monitors;

            EnumDisplayMonitors(nullptr, nullptr,
                [](HMONITOR hMon, HDC, LPRECT, LPARAM lp) -> BOOL {
                    auto* vec = (vector<MonitorInfo>*)lp;
                    MONITORINFOEXW mi{};
                    mi.cbSize = sizeof(mi);
                    if (!GetMonitorInfoW(hMon, &mi)) return TRUE;

                    MonitorInfo info;
                    info.primary = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;

                    // Resolution
                    info.w = mi.rcMonitor.right - mi.rcMonitor.left;
                    info.h = mi.rcMonitor.bottom - mi.rcMonitor.top;

                    // DPI
                    UINT dx = 0, dy = 0;
                    if (GetDpiForMonitor(hMon, MDT_EFFECTIVE_DPI, &dx, &dy) == S_OK)
                        info.dpi = (int)dx;
                    else
                        info.dpi = 96; // default fallback

                    // Refresh rate
                    DEVMODEW dm{};
                    dm.dmSize = sizeof(dm);
                    if (EnumDisplaySettingsW(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm))
                        info.hz = dm.dmDisplayFrequency;
                    else
                        info.hz = 0;

                    // Monitor name
                    DISPLAY_DEVICEW dd{};
                    dd.cb = sizeof(dd);
                    if (EnumDisplayDevicesW(mi.szDevice, 0, &dd, 0))
                    {
                        info.name = dd.DeviceString;
                        // Strip trailing spaces
                        while (!info.name.empty() && info.name.back() == L' ')
                            info.name.pop_back();
                    }
                    if (info.name.empty())
                        info.name = mi.szDevice;

                    vec->push_back(info);
                    return TRUE;
                }, (LPARAM)&monitors);

            if (monitors.empty())
            {
                displayLines = L"Display:   (none)\n";
            }
            else
            {
                for (size_t i = 0; i < monitors.size(); ++i)
                {
                    auto& m = monitors[i];
                    wchar_t line[256]{};
                    if (m.hz > 0)
                        swprintf_s(line, L"Display %c: %s\n"
                                         L"          %d x %d @ %dHz, %d DPI\n",
                            m.primary ? L'*' : (wchar_t)(L'1' + (int)i),
                            m.name.c_str(),
                            m.w, m.h, m.hz, m.dpi);
                    else
                        swprintf_s(line, L"Display %c: %s\n"
                                         L"          %d x %d, %d DPI\n",
                            m.primary ? L'*' : (wchar_t)(L'1' + (int)i),
                            m.name.c_str(),
                            m.w, m.h, m.dpi);
                    displayLines += line;
                }
            }
        }

        // Uptime
        ULONGLONG ut = GetTickCount64() / 1000;
        ULONGLONG ud = ut / 86400; ut %= 86400;
        ULONGLONG uh = ut / 3600;  ut %= 3600;
        ULONGLONG um = ut / 60;
        ULONGLONG us = ut % 60;

        wchar_t hostName[MAX_COMPUTERNAME_LENGTH + 1] = L"Unknown";
        DWORD hostNameLen = (DWORD)_countof(hostName);
        GetComputerNameW(hostName, &hostNameLen);

        wchar_t buf[2560]{};
        swprintf_s(buf,
            L"System Information\n"
            L"====================\n"
            L"OS:        %s\n"
            L"Board:     %s\n"
            L"Power:     %s\n"
            L"CPU:       %s  (%u cores, %s)\n"
            L"GPU:       %s\n"
            L"Memory:    %s / %s  (%d%%)\n"
            L"%s"
            L"%s"
            L"Uptime:    %llu day%s %02llu:%02llu:%02llu\n"
            L"Hostname:  %s",
            os, board, powerPlan,
            cpuName.c_str(), si.dwNumberOfProcessors, arch,
            gpu,
            fmt(msx.ullTotalPhys - msx.ullAvailPhys).c_str(), fmt(msx.ullTotalPhys).c_str(), memPct,
            diskLines.c_str(),
            displayLines.c_str(),
            ud, ud == 1 ? L"" : L"s", uh, um, us,
            hostName
        );

        return buf;
    }

    struct Plugin { const WLHostApiV1* host = nullptr; };

    bool WL_CALL OnLoad(void*)       { return true; }
    void WL_CALL OnUnload(void*)     {}
    bool WL_CALL ExecuteCommand(void*, const WLCommandContextV1*, WLStringResultV1*) { return true; }

    bool WL_CALL ExecuteSlashCommand(void* userData, const WLSlashCommandContextV1* ctx, WLStringResultV1* out)
    {
        if (!ctx || !out) return false;
        wstring cmd = ctx->command ? ctx->command : L"";

        auto* p = static_cast<Plugin*>(userData);

        if (cmd == L"sysinfo")        WriteResult(out, GetSystemSummary());
        else if (cmd == L"cpu")       WriteResult(out, GetCpuInfo());
        else if (cmd == L"memory")    WriteResult(out, GetMemoryInfo());
        else if (cmd == L"disk")      WriteResult(out, GetDiskInfo());
        else if (cmd == L"uptime")    WriteResult(out, GetUptime());
        else WriteResult(out, L"Use /sysinfo for a full system overview.");

        return true;
    }

    bool WL_CALL Search(void*, const WLSearchRequestV1*, WLSearchResponseV1*) { return true; }
}

WL_EXPORT uint32_t WL_CALL WinLauncherPlugin_GetAbiVersion() { return WINLAUNCHER_PLUGIN_ABI_VERSION; }

WL_EXPORT bool WL_CALL WinLauncherPlugin_Create(const WLHostApiV1* host, WLPluginInstanceV1** outInstance)
{
    if (!host || !outInstance) return false;
    auto* p = new Plugin(); p->host = host;
    auto* i = new WLPluginInstanceV1();
    i->size = sizeof(WLPluginInstanceV1);
    i->userData = p;
    i->onLoad = &OnLoad; i->onUnload = &OnUnload;
    i->executeCommand = &ExecuteCommand; i->executeSlashCommand = &ExecuteSlashCommand;
    i->onPopupShown = nullptr; i->onPopupHidden = nullptr; i->search = &Search;
    i->requestShutdown = nullptr; i->isShutdownComplete = nullptr;
    *outInstance = i;
    return true;
}

WL_EXPORT void WL_CALL WinLauncherPlugin_Destroy(WLPluginInstanceV1* i)
{
    if (!i) return;
    delete static_cast<Plugin*>(i->userData);
    delete i;
}
