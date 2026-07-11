# WinLauncher Test Directory

This directory contains lightweight automation for source-level regressions that are easy to miss in manual Windows UI testing.

## Current Tests

- `run_tests.ps1` runs the complete script-based test suite.
- `project_static_tests.ps1` checks high-risk release and maintenance invariants:
  - update mock mode is disabled,
  - update downloads must validate read/write completion before install,
  - update replacement commands must use PowerShell-safe literal paths,
  - executable version resources use `WinLauncher/version.h`,
  - command execution remains async/risk-gated,
  - Visual Studio filters stay synchronized with the main project.
- `script_static_tests.ps1` checks the automation itself:
  - required CI/test scripts exist,
  - PowerShell scripts parse,
  - GitHub Actions calls the local source gate on `windows-latest`,
  - plugin package builds remain opt-in.
- `native/WinLauncherNativeTests.vcxproj` is a dependency-free native harness for background-task exception isolation, callback invalidation, and child-process minidump generation. The Release CI preflight runs it after compilation.
- `scenario_regression.ps1` verifies the non-interactive contracts for diagnostics privacy, local usage ranking, and migration preflight. It never opens the desktop UI or reads real user data.

For every release candidate, manually verify popup show/hide, keyboard search and execution, cancelling a slow command, and importing a migration backup in a clean Windows user-data directory. Sleep/resume and Explorer selection behavior remain manual scenarios because they depend on the active desktop session.

Run from the repository root:

```powershell
.\tests\run_tests.ps1
```

For the full local CI preflight, run:

```powershell
.\scripts\ci_check.ps1
```
