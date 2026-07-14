param(
    [int]$RequireSamples = 20,
    [double]$MaxP95Ms = 150.0
)

$ErrorActionPreference = "Stop"
$logPath = Join-Path $env:APPDATA "WinLauncher\logs\current.jsonl"
if (-not (Test-Path -LiteralPath $logPath)) {
    throw "No WinLauncher runtime log found: $logPath"
}

$samples = Get-Content -LiteralPath $logPath | ForEach-Object {
    try {
        $record = $_ | ConvertFrom-Json
        if ($record.component -eq "ui.popup" -and $record.event -eq "show_timing" -and $record.message -match 'total_ms=([0-9.]+).*cold=1') {
            [double]$matches[1]
        }
    } catch {}
} | Select-Object -Last $RequireSamples

if ($samples.Count -lt $RequireSamples) {
    throw "Need $RequireSamples cold-popup samples; found $($samples.Count). Trigger the popup $RequireSamples times, then rerun this check."
}

$ordered = @($samples | Sort-Object)
$index = [Math]::Ceiling($ordered.Count * 0.95) - 1
$p95 = $ordered[$index]
Write-Host ("Cold-popup samples={0} p50={1:N2}ms p95={2:N2}ms target={3:N2}ms" -f $ordered.Count, $ordered[[int][Math]::Floor(($ordered.Count - 1) * 0.5)], $p95, $MaxP95Ms)
if ($p95 -gt $MaxP95Ms) {
    throw ("Cold-popup p95 {0:N2}ms exceeds target {1:N2}ms" -f $p95, $MaxP95Ms)
}
