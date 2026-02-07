#!/usr/bin/env bash
# Compile the C++ matching-engine recorder (WSL g++, no CMake) and generate the
# market-data CSVs the analytics layer consumes.
set -e
ROOT=/mnt/z/everything/projects/lob_dynamics
cd "$ROOT/engine"

echo "[build] compiling recorder..."
g++ -std=c++20 -O2 -o record_lob \
    Book.cpp Limit.cpp Order.cpp OrderPipeline.cpp taq_writer.cpp record_main.cpp
echo "[build] OK -> engine/record_lob"

echo "[run] generating market data (seed=42, n=20000)..."
./record_lob \
    --initial data/initial_orders.txt \
    --n 20000 --seed 42 \
    --out-quotes "$ROOT/data/quotes.csv" \
    --out-trades "$ROOT/data/trades.csv"

echo "[out] line counts:"
wc -l "$ROOT/data/quotes.csv" "$ROOT/data/trades.csv"
