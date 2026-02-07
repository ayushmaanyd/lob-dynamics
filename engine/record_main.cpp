// record_main.cpp
//
// Drives the matching engine over (1) a warmup file that builds an initial
// two-sided book, then (2) a deterministic, seeded synthetic order stream,
// while recording the market data the analytics layer consumes:
//
//   quotes.csv : ts_ns,bid,ask,bid_sz,ask_sz,mid,spread,microprice   (L1, per event)
//   trades.csv : ts_ns,price,qty,side                                (per execution)
//
// These are the canonical CSV schemas the analytics + backtester read directly.
//
// Determinism: the active stream uses a fixed-seed std::mt19937, so the same
// --seed always produces byte-identical CSVs -> reproducible PnL/risk.

#include "Book.hpp"
#include "Limit.hpp"
#include "Order.hpp"
#include "OrderPipeline.hpp"
#include "taq_writer.hpp"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <random>
#include <string>

namespace {

struct Args {
    std::string initial = "data/initial_orders.txt";
    std::string out_quotes = "quotes.csv";
    std::string out_trades = "trades.csv";
    long        n = 20000;                 // active orders to generate
    uint64_t    seed = 42;
    int64_t     base_ns = 1704067200000000000LL; // 2024-01-01T00:00:00Z
    int64_t     step_ns = 100000000LL;     // 100 ms between events
};

Args parse(int argc, char** argv) {
    Args a;
    for (int i = 1; i < argc; ++i) {
        std::string k = argv[i];
        auto next = [&]() -> std::string { return (i + 1 < argc) ? argv[++i] : std::string(); };
        if      (k == "--initial")    a.initial    = next();
        else if (k == "--out-quotes") a.out_quotes = next();
        else if (k == "--out-trades") a.out_trades = next();
        else if (k == "--n")          a.n          = std::stol(next());
        else if (k == "--seed")       a.seed       = std::stoull(next());
        else if (k == "--base-ns")    a.base_ns    = std::stoll(next());
        else if (k == "--step-ns")    a.step_ns    = std::stoll(next());
        else { std::cerr << "Unknown arg: " << k << "\n"; std::exit(2); }
    }
    return a;
}

} // namespace

int main(int argc, char** argv) {
    Args args = parse(argc, argv);

    Book book;

    // (1) Warmup: build the initial resting book. No recording here; these are
    // non-crossing limits, so there is nothing interesting to sample yet.
    OrderPipeline pipeline(&book);
    pipeline.processOrdersFromFile(args.initial);

    if (book.getHighestBuy() == nullptr || book.getLowestSell() == nullptr) {
        std::cerr << "Warmup did not produce a two-sided book (check --initial path: "
                  << args.initial << ")\n";
        return 1;
    }

    // (2) Start recording.
    lob::TaqWriter writer;
    if (!writer.open(args.out_quotes, args.out_trades)) return 1;

    // Trade observer: current event timestamp is stashed here so the callback can
    // stamp each fill. Side convention matches taq_writer: aggressing buy -> 'B'.
    int64_t now_ns = args.base_ns;
    book.onTrade = [&](int price, int shares, bool aggressorBuy) {
        writer.write_trade_row(now_ns, static_cast<double>(price),
                               static_cast<double>(shares), aggressorBuy ? 'B' : 'A');
    };

    auto snapshot = [&]() {
        Limit* bb = book.getHighestBuy();
        Limit* ba = book.getLowestSell();
        double bid_px = bb ? static_cast<double>(bb->getLimitPrice())   : 0.0;
        double bid_sz = bb ? static_cast<double>(bb->getTotalVolume())  : 0.0;
        double ask_px = ba ? static_cast<double>(ba->getLimitPrice())   : 0.0;
        double ask_sz = ba ? static_cast<double>(ba->getTotalVolume())  : 0.0;
        writer.write_quote_row(now_ns, bid_px, bid_sz, ask_px, ask_sz);
    };

    // Deterministic synthetic order flow.
    std::mt19937 gen(static_cast<uint32_t>(args.seed));
    std::uniform_int_distribution<int> action(0, 99);
    std::uniform_int_distribution<int> sharesDist(1, 200);
    std::uniform_int_distribution<int> offsetDist(0, 4);
    std::uniform_int_distribution<int> sideDist(0, 1);

    int nextId = 100000; // above the warmup id range to avoid collisions

    for (long i = 0; i < args.n; ++i) {
        now_ns = args.base_ns + static_cast<int64_t>(i) * args.step_ns;

        Limit* bb = book.getHighestBuy();
        Limit* ba = book.getLowestSell();

        // Keep the book two-sided: if a side drained, replenish it passively.
        if (bb == nullptr || ba == nullptr) {
            if (bb == nullptr && ba != nullptr)
                book.addLimitOrder(nextId++, /*buy=*/true, sharesDist(gen), ba->getLimitPrice() - 1);
            else if (ba == nullptr && bb != nullptr)
                book.addLimitOrder(nextId++, /*buy=*/false, sharesDist(gen), bb->getLimitPrice() + 1);
            snapshot();
            continue;
        }

        int bestBid = bb->getLimitPrice();
        int bestAsk = ba->getLimitPrice();
        int a = action(gen);
        bool buy = sideDist(gen) == 1;
        int shares = sharesDist(gen);

        if (a < 45) {
            // Passive limit near the touch (adds depth, no cross).
            int px = buy ? bestBid - offsetDist(gen) : bestAsk + offsetDist(gen);
            book.addLimitOrder(nextId++, buy, shares, px);
        } else if (a < 75) {
            // Aggressive market order -> generates trades.
            book.marketOrder(nextId++, buy, shares);
        } else if (a < 90) {
            // Marketable limit that crosses the spread -> generates trades.
            int px = buy ? bestAsk + offsetDist(gen) : bestBid - offsetDist(gen);
            book.addLimitOrder(nextId++, buy, shares, px);
        } else {
            // Cancel a random resting limit order (key 0 = limit orders).
            Order* victim = book.getRandomOrder(0, gen);
            if (victim != nullptr) book.cancelLimitOrder(victim->getOrderId());
            else book.addLimitOrder(nextId++, buy, shares, buy ? bestBid : bestAsk);
        }

        snapshot();
    }

    writer.close();
    std::cerr << "Recorded " << args.n << " events -> "
              << args.out_quotes << ", " << args.out_trades << "\n";
    return 0;
}
