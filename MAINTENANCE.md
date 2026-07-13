# WinLauncher Maintenance Priorities

This file is the project maintenance contract for day-to-day changes. The maintenance focus is the main WinLauncher application first: launcher startup, popup interaction, search and command execution, custom-drawn UI, configuration, update, and release reliability. Plugins and the SDK are maintained as compatibility surfaces around that core.

## Priority Order

1. Main launcher stability
   - Keep startup, tray behavior, popup show/hide, local shortcut launch, command execution, settings, configuration persistence, update checks, and clean shutdown reliable.
   - Treat `PopupWindow`, `GlassWindow`, `ShadowWindow`, `ConfigWindow`, `SettingsPage`, `ShortcutPage`, `CommandPanelWindow`, `Application`, `ConfigPath`, and `UpdateService` as primary maintenance surfaces.
   - Prefer targeted fixes with real runtime/log evidence for intermittent UI or lifecycle issues.

2. Popup search and command execution
   - Preserve separate matching paths for local shortcuts, normal plugin commands, dynamic plugin results, and `/` slash commands.
   - Local shortcuts and built-in command execution must remain usable when plugins are missing, invalid, disabled, slow, or failing.
   - Slow commands must show the command panel immediately, run in the background, stream or append output when possible, and preserve the original execution snapshot for refresh.

3. Custom-drawn UI stability
   - Keep settings, plugin management, dialogs, Toasts, menus, and command panels inside the existing custom-drawn WinLauncher UI flow.
   - Treat `GlassWindow`, `ShadowWindow`, menu windows, Toasts, settings pages, and command panels as shared visual infrastructure.
   - For flicker fixes, verify close/disappear paths, outside-click teardown, shadow opacity, and DPI-aware hit testing.

4. Configuration, user data, and release path
   - Runtime state belongs under `%APPDATA%\WinLauncher`; config, backup, plugin install, plugin state, cache, log, and private plugin data must not leak into the source tree.
   - Every project change must update `RELEASE_NOTES.md` for the current version from `WinLauncher/version.h`.
   - Release validation must confirm the executable, update metadata, plugin packages when changed, SDK docs when changed, and release notes describe the same behavior.

5. Plugin system and SDK compatibility
   - Keep `WLHostApiV1` append-only and compatible. New fields must be added at the end and guarded by the `size` field.
   - Keep `plugin.json`, `.wlplugin` packaging, permissions, private plugin config, SDK headers, SDK docs, templates, and samples in sync.
   - Do not introduce ABI v2, plugin stores, online repositories, untrusted plugin defaults, or long-running background plugin features until the current in-process trusted plugin model is stable.
   - File-related plugin commands should consume the captured `selectedFiles` context by default and support files or folders when the command semantics allow it.

6. Bundled plugins
   - Maintain `file_tools`, `network_tools`, `text_tools`, `time_tools`, and `system_info` as the supported built-in plugin set.
   - After changing plugin code or manifest data, rebuild the plugin and regenerate its `.wlplugin` package.
   - Packages must contain only runtime files such as `plugin.json`, the plugin DLL, and icons.

## Architecture and Code Boundaries

The following rules keep the launcher maintainable as features are added. They are design constraints for new work and a checklist when refactoring existing code.

1. Ownership and dependency direction
   - `Application` is the composition root. It creates `AppContext`, starts and stops process-wide services, and owns the order of application shutdown.
   - `AppContext` is the shared runtime seam for long-lived services. Do not add globals or service-locator singletons for new cross-cutting behavior.
   - `Model` types remain UI- and Win32-window-independent. `Services` may depend on models and platform APIs, but must not depend on concrete configuration pages or popup windows.
   - Custom windows may use `AppContext`, service interfaces, view models, and `IConfigWindow`; pages and forms must not reach into another concrete window's private state.
   - Keep custom application messages in `AppMessages.h`. Allocate a named `WM_APP` value before adding a message handler, and remove the value when its last consumer is removed.

2. UI, background work, and lifetime
   - Keep all Direct2D, HWND, menu, Toast, and dialog mutations on the UI thread. Background work returns through `UiDispatcher` with a lifetime check.
   - Submit feature work through `BackgroundTaskService`; do not introduce detached threads, unmanaged callbacks, or unbounded waits.
   - Every asynchronous owner must have a cancellation point for hide, close, replacement, and application shutdown. Obsolete results must be discarded rather than written into a newer window or request.
   - Startup probes such as optional command-environment detection run once in the background. Their consumers reuse the completed snapshot and must not repeat disk or PATH scans on UI interaction paths.

3. State, configuration, and compatibility
   - Persist launcher-owned state only through `IConfigService` and `ConfigPath`. Save before publishing configuration-change notifications so observers never reload stale data.
   - Treat user data operations as recoverable: validate imported data, create a backup before destructive replacement, and preserve plugin private data boundaries.
   - Keep `WLHostApiV1` append-only. New host fields are added only at the end and callers must check the advertised `size` before use.
   - Preserve existing stored command types and shortcuts even when a local optional dependency is unavailable; the editor may hide unavailable choices, while execution reports a clear actionable error.

4. Code hygiene and removal
   - Remove code only after proving that it has no production caller, serialized-data contract, plugin ABI consumer, build-script dependency, or documented user workflow. Remove the implementation, declarations, project/filter entries, tests, and documentation together.
   - Prefer one service as the source of truth for each system capability. Delete duplicated detection, parsing, or persistence logic after moving callers to the shared implementation.
   - Keep headers minimal and self-sufficient; remove duplicate or unused includes and avoid exposing concrete service implementations when an interface is sufficient.
   - Do not commit local build output, logs, IDE user files, generated caches, crash dumps, or runtime data. Keep release artifacts only where the release workflow explicitly tracks them.

5. Refactoring workflow
   - Make changes in small behavior-preserving slices. Add or update a focused regression check before removing an old path, then run the required maintenance checks.
   - Prefer a new interface or adapter at a boundary over a cross-layer include. Preserve compatibility wrappers only while real consumers still require them, and document their removal condition.
   - Keep user-visible behavior, release metadata, README architecture descriptions, and `RELEASE_NOTES.md` synchronized with the source tree.

## Required Checks

Run the maintenance check before finishing any change that touches plugins, search, command execution, custom UI, configuration, packaging, or release files:

```powershell
.\scripts\maintenance_check.ps1
```

Run the local CI preflight before release-facing changes:

```powershell
.\scripts\ci_check.ps1
```

For release candidates, build the main app first:

```powershell
& "E:\Visual Studio 2026\MSBuild\Current\Bin\MSBuild.exe" WinLauncher.sln /p:Configuration=Release /p:Platform=x64 /m:1
```

Build plugin packages only when plugin, SDK, packaging, or plugin-related release behavior changed:

```powershell
.\scripts\build_plugins.ps1 -Configuration Release -Platform x64 -MsBuildPath "E:\Visual Studio 2026\MSBuild\Current\Bin\MSBuild.exe" -IncludeSamples
```

## Acceptance Scenarios

- Main app: clean startup, tray menu, popup trigger, local shortcut launch, settings open/save, update page behavior, and clean exit.
- Search and execution: local search remains fast and independent; slash search does not affect local shortcuts; command panel refresh reruns with the original parameters.
- UI: popup open/close, menu outside-click close, Toast close, shadow sync, and settings page controls remain stable under different scale values.
- Plugin compatibility: no plugin directory, empty installed directory, invalid manifest, disabled plugin, enabled plugin, install, uninstall, private setting read/write, selected file/folder context, and package validation.
- Release: main executable is rebuilt for release candidates; packages contain no source, project files, PDBs, object files, logs, or local build paths when plugin packages are rebuilt; `RELEASE_NOTES.md` has the current version section.
- Architecture: service ownership has one clear composition path, new UI work observes thread/lifetime boundaries, optional capabilities have one shared availability source, and removed code has no lingering project, test, or documentation reference.
