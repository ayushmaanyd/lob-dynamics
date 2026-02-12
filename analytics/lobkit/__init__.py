# lobkit: order-book analytics + execution backtesting.
#
# Pure-Python package that consumes the CSVs produced by the C++ engine
# (quotes.csv / trades.csv). Submodules:
#   metrics         - spread / imbalance / microprice / depth
#   microstructure  - impact curves / realized vol / order-flow autocorrelation
#   strategies      - TWAP / VWAP execution schedules + cost model
#   strategy.pov    - POV (participation-of-volume) schedule
#   backtest        - runs a strategy over the tape, emits fills
#   risk            - fills -> PnL, drawdown, Sharpe, turnover
#   checksum        - SHA-256 reproducibility manifest
