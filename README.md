# lob_dynamics

A limit-order-book matching engine (C++) instrumented to emit market data, feeding a
Python analytics + execution-backtesting stack.

Over one dataset produced by the matching engine you get:

- **Microstructure analytics** — spread, L1 imbalance, microprice, market-impact
  curves, realized volatility (Parkinson / Garman–Klass), order-flow autocorrelation,
  drift-vs-imbalance.
- **Execution backtests** — TWAP, VWAP, POV — with a cost model, latency, and seeded RNG.
- **PnL & risk** — realized/unrealized PnL, mark-to-mid equity curve, max drawdown,
  Sharpe-like ratio, turnover.
- **Reproducibility** — the seeded engine emits byte-identical CSVs across runs, and
  every backtest writes SHA-256 checksums of its outputs.

## Architecture

```
 C++ matching engine                        Python analytics (lobkit)
 ┌───────────────────────┐                  ┌──────────────────────────────────┐
 │ Book (AVL + FIFO)      │   quotes.csv     │ metrics.py        spread/imbalance│
 │  + onTrade observer    │ ───────────────► │ microstructure.py impact/RV/oflow │
 │ record_main.cpp        │   trades.csv     ├──────────────────────────────────┤
 │  (seeded order stream) │ ───────────────► │ backtest.py   TWAP/VWAP/POV       │
 └───────────────────────┘                  │   └► fills.csv ─► risk.py  PnL/risk│
                                             └──────────────────────────────────┘
```

The only coupling is a **CSV contract**: the engine writes it, everything downstream
reads it.

## CSV contract

| file          | columns                                                            |
|---------------|--------------------------------------------------------------------|
| `quotes.csv`  | `ts_ns,bid,ask,bid_sz,ask_sz,mid,spread,microprice` (L1, per event) |
| `trades.csv`  | `ts_ns,price,qty,side`  (`side`: `B`=aggressing buy, `A`=aggressing sell) |
| `*_fills.csv` | `ts_ns,px,qty`  (backtester output, consumed by `risk.py`)         |

## Layout

```
engine/               C++ matching engine + data recorder
  Book.{hpp,cpp}        order book: AVL tree of price levels + FIFO queues,
  Limit.{hpp,cpp}       market/limit/stop orders. onTrade observer added in
  Order.{hpp,cpp}       Book::marketOrderHelper for recording executions.
  OrderPipeline.{hpp,cpp}  parses an order file and applies it to the book
  taq_writer.{hpp,cpp}     dependency-free quotes/trades CSV writer
  record_main.cpp          driver: warmup + seeded stream -> quotes/trades CSV
  data/initial_orders.txt  warmup orders that build the initial two-sided book
analytics/lobkit/     Python analytics + backtest package
  metrics.py, microstructure.py, strategies.py, strategy/pov.py,
  backtest.py, risk.py, checksum.py, queue_model.py
configs/              twap.yaml, vwap.yaml, pov.yaml
data/                 generated quotes.csv, trades.csv
results/              analytics JSON + PNGs, and per-strategy backtest outputs
scripts/              build_record.sh, run_all.ps1
```

## Prerequisites

- **WSL Ubuntu** with `g++` (13+) — builds/runs the C++ engine. No CMake needed.
- **Windows Python venv** at `.\.venv` with `pandas numpy matplotlib pyyaml pyarrow`
  (optional `scikit-learn` for impact-curve clustering).

```powershell
python -m venv .venv
.\.venv\Scripts\python -m pip install pandas numpy matplotlib pyyaml pyarrow
```

## Run everything

```powershell
powershell -ExecutionPolicy Bypass -File scripts\run_all.ps1
```

Or step by step:

```powershell
# 1. Engine: build + generate market data (WSL)
wsl bash /mnt/z/everything/projects/lob_dynamics/scripts/build_record.sh

# 2. Analytics (Windows venv)
$env:PYTHONUTF8=1; $env:PYTHONPATH="$PWD\analytics"
.\.venv\Scripts\python analytics\lobkit\metrics.py --quotes data\quotes.csv --out-json results\metrics.json --plots-out results\plots
.\.venv\Scripts\python analytics\lobkit\microstructure.py --quotes data\quotes.csv --trades data\trades.csv --plots-out results\plots_micro --out-json results\microstructure.json

# 3. Backtests
.\.venv\Scripts\python -m lobkit.backtest --strategy configs\twap.yaml --quotes data\quotes.csv --trades data\trades.csv --out results\twap --seed 42
```

Change the market data with recorder flags: `--n <events> --seed <int> --step-ns <ns>`.

## How the engine is instrumented

Executions are recorded via a single observer hook (search for `onTrade`):

1. `Book.hpp` — a public `std::function<void(int,int,bool)> onTrade` (null by default).
2. `Book::marketOrderHelper` — the one choke point through which *every* execution
   passes; it calls `onTrade(price, qty, aggressorBuy)` at each fill.

Quotes are sampled from the public top-of-book getters after every event in
`record_main.cpp`. When `onTrade` is unused the engine behaves exactly as before.

## Notes

- **`pnl_total` interpretation.** `risk.py` reports a mark-to-mid *account equity*
  figure. For a buy-only program that ends holding inventory it includes the full cash
  outflow, so it reads as a large negative number rather than execution slippage. The
  meaningful execution-quality signal is **`avg_cost` vs. arrival mid** (here avg_cost
  ≈ 300.0 vs mid ≈ 299.5 ⇒ ~0.5 tick of buy-side slippage). A proper
  implementation-shortfall metric is a good next addition.
- **POV aggressiveness.** With `target_pov: 0.1` against this dataset's per-bar volume,
  the parent fills in one clip; lower `target_pov` or raise `qty` to see it slice.
- Prices/shares are integers (engine ticks), so `tick_size`/`lot_size` are `1` in configs.

## Future scope

- **Market-making** with engine-coupled fills (queue position): route child orders back
  into the live book rather than filling against the replayed tape.
- **Top-10 depth analytics** (`depth.csv` tidy schema `ts_ns,side,level,price,qty`);
  the microstructure module already supports it when present.
