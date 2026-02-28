# run_all.ps1 - one-command pipeline.
#
# Stage 1 (C++/WSL):    build the engine recorder and generate market data
# Stage 2 (Python/Win): analytics (spread/imbalance/impact/RV/order-flow)
# Stage 3 (Python/Win): TWAP/VWAP/POV backtests -> fills -> PnL + risk metrics
#
# Prereqs: WSL Ubuntu with g++, and the Windows venv at .\.venv (see README).

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot           # repo root (scripts/..)
$py   = "$root\.venv\Scripts\python.exe"
$env:PYTHONUTF8 = 1
$env:PYTHONPATH = "$root\analytics"

Write-Host "`n=== [1/3] Build engine + generate market data (WSL g++) ===" -ForegroundColor Cyan
$buildScriptWin = (Join-Path $PSScriptRoot 'build_record.sh') -replace '\\','/'
$buildScriptWsl = wsl wslpath -u "$buildScriptWin"
wsl bash "$buildScriptWsl"
if ($LASTEXITCODE -ne 0) { throw "engine build/record failed" }

Write-Host "`n=== [2/3] Microstructure analytics ===" -ForegroundColor Cyan
& $py "$root\analytics\lobkit\metrics.py" `
    --quotes "$root\data\quotes.csv" `
    --out-json "$root\results\metrics.json" `
    --plots-out "$root\results\plots"
& $py "$root\analytics\lobkit\microstructure.py" `
    --quotes "$root\data\quotes.csv" `
    --trades "$root\data\trades.csv" `
    --plots-out "$root\results\plots_micro" `
    --out-json "$root\results\microstructure.json" `
    --bar-sec 30 --impact-horizons-ms 1000,5000

Write-Host "`n=== [3/3] Backtests (TWAP / VWAP / POV) ===" -ForegroundColor Cyan
foreach ($s in "twap","vwap","pov") {
    Write-Host "--- $s ---" -ForegroundColor Yellow
    & $py -m lobkit.backtest `
        --strategy "$root\configs\$s.yaml" `
        --quotes "$root\data\quotes.csv" `
        --trades "$root\data\trades.csv" `
        --out "$root\results\$s" `
        --seed 42
    Get-Content "$root\results\$s\risk_summary.json" -Raw
}

Write-Host "`nDone. Outputs in results\  (metrics.json, microstructure.json, plots\, plots_micro\, twap\, vwap\, pov\)" -ForegroundColor Green
