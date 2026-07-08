param(
    [switch]$SkipPluginPackages
)

$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = Split-Path -Parent $scriptDir

$results = New-Object System.Collections.Generic.List[object]

function Add-Check {
    param(
        [string]$Name,
        [bool]$Passed,
        [string]$Detail
    )

    $results.Add([pscustomobject]@{
        Name = $Name
        Passed = $Passed
        Detail = $Detail
    }) | Out-Null
}

function Resolve-RepoPath {
    param([string]$RelativePath)
    return Join-Path $repoRoot $RelativePath
}

function Get-CurrentVersion {
    $versionFile = Resolve-RepoPath "WinLauncher\version.h"
    if (-not (Test-Path -LiteralPath $versionFile)) {
        throw "Missing WinLauncher\version.h"
    }

    $values = @{}
    Get-Content -LiteralPath $versionFile | ForEach-Object {
        if ($_ -match '^\s*#define\s+WINLAUNCHER_VERSION_(MAJOR|MINOR|PATCH|BUILD)\s+(\d+)\s*$') {
            $values[$matches[1]] = [int]$matches[2]
        }
    }

    foreach ($key in @("MAJOR", "MINOR", "PATCH", "BUILD")) {
        if (-not $values.ContainsKey($key)) {
            throw "Missing WINLAUNCHER_VERSION_$key in WinLauncher\version.h"
        }
    }

    return "v{0}.{1}.{2}.{3}" -f $values["MAJOR"], $values["MINOR"], $values["PATCH"], $values["BUILD"]
}

function Test-ZipPackage {
    param(
        [string]$PackagePath,
        [string]$ExpectedDll
    )

    Add-Type -AssemblyName System.IO.Compression.FileSystem

    $zip = [System.IO.Compression.ZipFile]::OpenRead($PackagePath)
    try {
        $entries = @($zip.Entries | Where-Object { $_.FullName -and -not $_.FullName.EndsWith("/") })
        $names = @($entries | ForEach-Object { $_.FullName.Replace("\", "/") })

        $unsafe = @($names | Where-Object {
            $_ -match '(^|/)\.\.(/|$)' -or
            $_ -match '^[A-Za-z]:' -or
            $_.StartsWith("/") -or
            $_.StartsWith("\") -or
            $_ -match '\\'
        })
        if ($unsafe.Count -gt 0) {
            return "unsafe package paths: $($unsafe -join ', ')"
        }

        if ($names -notcontains "plugin.json") {
            return "missing root plugin.json"
        }

        if ($names -notcontains $ExpectedDll) {
            return "missing root DLL $ExpectedDll"
        }

        $blockedExtensions = @(
            ".c", ".cpp", ".h", ".hpp", ".vcxproj", ".filters", ".user",
            ".pdb", ".obj", ".ilk", ".tlog", ".log", ".ps1", ".bat"
        )
        $blocked = @($names | Where-Object {
            $ext = [System.IO.Path]::GetExtension($_).ToLowerInvariant()
            $blockedExtensions -contains $ext
        })
        if ($blocked.Count -gt 0) {
            return "package contains non-runtime files: $($blocked -join ', ')"
        }

        return $null
    }
    finally {
        $zip.Dispose()
    }
}

try {
    $currentVersion = Get-CurrentVersion
    Add-Check "current version parsed" $true $currentVersion
}
catch {
    Add-Check "current version parsed" $false $_.Exception.Message
    $currentVersion = $null
}

$releaseNotesPath = Resolve-RepoPath "RELEASE_NOTES.md"
if (Test-Path -LiteralPath $releaseNotesPath) {
    $releaseNotes = Get-Content -LiteralPath $releaseNotesPath -Raw
    $hasCurrentSection = $currentVersion -and ($releaseNotes -match "(?m)^##\s+$([regex]::Escape($currentVersion))\s*$")
    Add-Check "release notes current version" $hasCurrentSection "expected section: ## $currentVersion"
}
else {
    Add-Check "release notes current version" $false "missing RELEASE_NOTES.md"
}

$requiredFiles = @(
    "MAINTENANCE.md",
    "scripts\build_plugins.ps1",
    "PLUGIN_SYSTEM_PLAN.md",
    "PLUGIN_API_PLAN.md",
    "SDK\docs\PLUGIN_DEV.md",
    "WinLauncher\SDK\include\WinLauncher\WinLauncherPluginABI.h",
    "WinLauncher\SDK\include\WinLauncher\WinLauncherPluginCpp.h",
    "plugins\README.md"
)

foreach ($relativePath in $requiredFiles) {
    $path = Resolve-RepoPath $relativePath
    Add-Check "required file: $relativePath" (Test-Path -LiteralPath $path) $relativePath
}

$mainProjectFiles = @(
    "WinLauncher.sln",
    "WinLauncher\WinLauncher.vcxproj",
    "WinLauncher\main.cpp",
    "WinLauncher\App\Application.cpp",
    "WinLauncher\PopupWindow.cpp",
    "WinLauncher\GlassWindow.cpp",
    "WinLauncher\ShadowWindow.cpp",
    "WinLauncher\Config\ConfigWindow.cpp",
    "WinLauncher\Config\SettingsPage.cpp",
    "WinLauncher\Config\CommandPanelWindow.cpp",
    "WinLauncher\Services\ConfigPath.h",
    "WinLauncher\Services\UpdateService.cpp",
    "WinLauncher\Services\CommandVariableService.cpp",
    "WinLauncher\Services\FileSelectionService.cpp"
)

foreach ($relativePath in $mainProjectFiles) {
    $path = Resolve-RepoPath $relativePath
    Add-Check "main project file: $relativePath" (Test-Path -LiteralPath $path) $relativePath
}

$updateHeaderPath = Resolve-RepoPath "WinLauncher\Services\UpdateService.h"
if (Test-Path -LiteralPath $updateHeaderPath) {
    $updateHeader = Get-Content -LiteralPath $updateHeaderPath -Raw
    $mockUpdateDisabled = $updateHeader -match '(?m)^\s*#define\s+MOCK_UPDATE_SERVICE\s+0\s*$'
    Add-Check "release update mock disabled" $mockUpdateDisabled "MOCK_UPDATE_SERVICE must be 0 for production maintenance"
}
else {
    Add-Check "release update mock disabled" $false "missing WinLauncher\Services\UpdateService.h"
}

$resourcePath = Resolve-RepoPath "WinLauncher\resource.rc"
if (Test-Path -LiteralPath $resourcePath) {
    $resourceText = Get-Content -LiteralPath $resourcePath -Raw
    $usesVersionHeader = $resourceText -match '#include\s+"version\.h"'
    $usesFileVersionMacro = $resourceText -match 'FILEVERSION\s+WINLAUNCHER_VERSION_RC'
    $usesProductVersionMacro = $resourceText -match 'PRODUCTVERSION\s+WINLAUNCHER_VERSION_RC'
    $usesStringVersionMacro = $resourceText -match 'VALUE\s+"FileVersion",\s+WINLAUNCHER_VERSION_ASTR' -and
        $resourceText -match 'VALUE\s+"ProductVersion",\s+WINLAUNCHER_VERSION_ASTR'
    Add-Check "resource version macros" ($usesVersionHeader -and $usesFileVersionMacro -and $usesProductVersionMacro -and $usesStringVersionMacro) "resource.rc should use WinLauncher/version.h macros"
}
else {
    Add-Check "resource version macros" $false "missing WinLauncher\resource.rc"
}

$mainProjectPath = Resolve-RepoPath "WinLauncher\WinLauncher.vcxproj"
$mainFiltersPath = Resolve-RepoPath "WinLauncher\WinLauncher.vcxproj.filters"
if ((Test-Path -LiteralPath $mainProjectPath) -and (Test-Path -LiteralPath $mainFiltersPath)) {
    try {
        $projectXml = [xml](Get-Content -LiteralPath $mainProjectPath)
        $filtersXml = [xml](Get-Content -LiteralPath $mainFiltersPath)

        $projectNs = New-Object System.Xml.XmlNamespaceManager($projectXml.NameTable)
        $projectNs.AddNamespace("msb", "http://schemas.microsoft.com/developer/msbuild/2003")
        $filtersNs = New-Object System.Xml.XmlNamespaceManager($filtersXml.NameTable)
        $filtersNs.AddNamespace("msb", "http://schemas.microsoft.com/developer/msbuild/2003")

        foreach ($itemKind in @("ClCompile", "ClInclude", "ResourceCompile", "Manifest")) {
            $projectItems = @($projectXml.SelectNodes("//msb:$itemKind", $projectNs) | ForEach-Object { $_.Include } | Sort-Object -Unique)
            $filterItems = @($filtersXml.SelectNodes("//msb:$itemKind", $filtersNs) | ForEach-Object { $_.Include } | Sort-Object -Unique)

            $missingFiles = @($projectItems | Where-Object { -not (Test-Path -LiteralPath (Join-Path (Resolve-RepoPath "WinLauncher") $_)) })
            Add-Check "vcxproj files exist: $itemKind" ($missingFiles.Count -eq 0) $(if ($missingFiles.Count -eq 0) { "$($projectItems.Count) item(s)" } else { $missingFiles -join ", " })

            $missingFilters = @($projectItems | Where-Object { $_ -notin $filterItems })
            Add-Check "vcxproj filters sync: $itemKind" ($missingFilters.Count -eq 0) $(if ($missingFilters.Count -eq 0) { "$($projectItems.Count) item(s)" } else { $missingFilters -join ", " })
        }
    }
    catch {
        Add-Check "main project XML parse" $false $_.Exception.Message
    }
}
else {
    Add-Check "main project XML files" $false "missing WinLauncher.vcxproj or WinLauncher.vcxproj.filters"
}

$pluginRoot = Resolve-RepoPath "plugins"
$expectedPlugins = @(
    "wl.file_tools",
    "wl.network_tools",
    "wl.system_info",
    "wl.text_tools",
    "wl.time_tools"
)

if (Test-Path -LiteralPath $pluginRoot) {
    foreach ($pluginId in $expectedPlugins) {
        $matchingManifest = Get-ChildItem -LiteralPath $pluginRoot -Directory |
            ForEach-Object { Join-Path $_.FullName "plugin.json" } |
            Where-Object {
                if (-not (Test-Path -LiteralPath $_)) { return $false }
                try {
                    ((Get-Content -LiteralPath $_ -Raw) | ConvertFrom-Json).id -eq $pluginId
                }
                catch {
                    return $false
                }
            } |
            Select-Object -First 1

        Add-Check "built-in plugin manifest: $pluginId" ($null -ne $matchingManifest) $pluginId
    }

    foreach ($pluginDir in Get-ChildItem -LiteralPath $pluginRoot -Directory) {
        $manifestPath = Join-Path $pluginDir.FullName "plugin.json"
        $packageScript = Join-Path $pluginDir.FullName "package.ps1"
        if (-not (Test-Path -LiteralPath $manifestPath) -or -not (Test-Path -LiteralPath $packageScript)) {
            continue
        }

        try {
            $manifest = (Get-Content -LiteralPath $manifestPath -Raw) | ConvertFrom-Json
            $id = [string]$manifest.id
            $entry = [string]$manifest.entry
            $validManifest = -not [string]::IsNullOrWhiteSpace($id) -and
                -not [string]::IsNullOrWhiteSpace($entry) -and
                $entry.ToLowerInvariant().EndsWith(".dll")
            Add-Check "plugin manifest shape: $($pluginDir.Name)" $validManifest "id=$id entry=$entry"

            $scriptText = Get-Content -LiteralPath $packageScript -Raw
            $directPackageArchive = $scriptText -match 'Compress-Archive[\s\S]*-DestinationPath\s+\$(pkg|packagePath)\b'
            Add-Check "plugin package script temp zip: $($pluginDir.Name)" (-not $directPackageArchive) "package.ps1 should archive to a temporary .zip before renaming to .wlplugin"

            $distManifestPath = Join-Path (Join-Path (Join-Path $pluginDir.FullName "dist") $id) "plugin.json"
            if (Test-Path -LiteralPath $distManifestPath) {
                $sourceHash = (Get-FileHash -LiteralPath $manifestPath -Algorithm SHA256).Hash
                $distHash = (Get-FileHash -LiteralPath $distManifestPath -Algorithm SHA256).Hash
                Add-Check "plugin dist manifest sync: $id" ($sourceHash -eq $distHash) $distManifestPath
            }
            else {
                Add-Check "plugin dist manifest sync: $id" $false "missing $distManifestPath"
            }

            if (-not $SkipPluginPackages) {
                $packagePath = Join-Path (Join-Path $pluginDir.FullName "dist") "$id.wlplugin"
                if (Test-Path -LiteralPath $packagePath) {
                    $packageError = Test-ZipPackage -PackagePath $packagePath -ExpectedDll $entry
                    Add-Check "plugin package contents: $id" ($null -eq $packageError) $(if ($packageError) { $packageError } else { $packagePath })
                }
                else {
                    Add-Check "plugin package contents: $id" $false "missing $packagePath"
                }
            }
        }
        catch {
            Add-Check "plugin manifest shape: $($pluginDir.Name)" $false $_.Exception.Message
        }
    }
}
else {
    Add-Check "plugins directory" $false "missing plugins directory"
}

$sampleManifestPath = Resolve-RepoPath "SDK\samples\hello_world\plugin.json"
$samplePackageScript = Resolve-RepoPath "SDK\samples\hello_world\package.ps1"
if ((Test-Path -LiteralPath $sampleManifestPath) -and (Test-Path -LiteralPath $samplePackageScript)) {
    try {
        $sampleManifest = Get-Content -LiteralPath $sampleManifestPath -Raw | ConvertFrom-Json
        $sampleId = [string]$sampleManifest.id
        $sampleEntry = [string]$sampleManifest.entry
        $validSampleManifest = $sampleId -eq "example.hello_world" -and
            -not [string]::IsNullOrWhiteSpace($sampleEntry) -and
            $sampleEntry.ToLowerInvariant().EndsWith(".dll")
        Add-Check "SDK sample manifest shape" $validSampleManifest "id=$sampleId entry=$sampleEntry"

        $sampleScriptText = Get-Content -LiteralPath $samplePackageScript -Raw
        $sampleDirectPackageArchive = $sampleScriptText -match 'Compress-Archive[\s\S]*-DestinationPath\s+\$(pkg|packagePath)\b'
        Add-Check "SDK sample package script temp zip" (-not $sampleDirectPackageArchive) "package.ps1 should archive to a temporary .zip before renaming to .wlplugin"

        $samplePackagePath = Resolve-RepoPath "SDK\samples\hello_world\dist\example.hello_world.wlplugin"
        if (Test-Path -LiteralPath $samplePackagePath) {
            $samplePackageError = Test-ZipPackage -PackagePath $samplePackagePath -ExpectedDll $sampleEntry
            Add-Check "SDK sample package contents" ($null -eq $samplePackageError) $(if ($samplePackageError) { $samplePackageError } else { $samplePackagePath })
        }
        else {
            Add-Check "SDK sample package contents" $true "package not built; run scripts\build_plugins.ps1 -IncludeSamples for release validation"
        }
    }
    catch {
        Add-Check "SDK sample manifest shape" $false $_.Exception.Message
    }
}
else {
    Add-Check "SDK sample files" $false "missing SDK sample manifest or package script"
}

$failed = @($results | Where-Object { -not $_.Passed })
foreach ($result in $results) {
    $prefix = if ($result.Passed) { "[PASS]" } else { "[FAIL]" }
    Write-Host "$prefix $($result.Name) - $($result.Detail)"
}

if ($failed.Count -gt 0) {
    Write-Error "Maintenance check failed with $($failed.Count) issue(s)."
    exit 1
}

Write-Host "Maintenance check passed."
