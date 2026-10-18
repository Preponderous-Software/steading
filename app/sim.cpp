// steading-sim: run the colony headless and print one JSON line.
//
//   steading-sim [--seed N] [--size N] [--settlers N] [--max N] [--ticks N]
//
// Prints the final state summary, the state fingerprint and the tick rate. Used
// for quick checks and as the rules half of the benchmark.
#include <chrono>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "colony/generate.h"
#include "colony/systems.h"

using namespace steading;

int main(int argc, char** argv) {
    GenParams p;
    long ticks = 1000;
    for (int i = 1; i < argc; ++i) {
        auto next = [&](const char* flag) -> long {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "%s needs a value\n", flag);
                std::exit(2);
            }
            return std::strtol(argv[++i], nullptr, 10);
        };
        const std::string a = argv[i];
        if (a == "--seed") p.seed = static_cast<uint64_t>(next("--seed"));
        else if (a == "--size") p.width = p.height = static_cast<int32_t>(next("--size"));
        else if (a == "--settlers") p.settlers = static_cast<int32_t>(next("--settlers"));
        else if (a == "--max") p.maxSettlers = static_cast<int32_t>(next("--max"));
        else if (a == "--ticks") ticks = next("--ticks");
        else {
            std::fprintf(stderr, "usage: steading-sim [--seed N] [--size N] [--settlers N] [--max N] [--ticks N]\n");
            return 2;
        }
    }
    Colony c = generate(p);
    Derived d = derive(c);
    const Stats s;
    const auto t0 = std::chrono::steady_clock::now();
    for (long t = 0; t < ticks; ++t) tick(c, d, s);
    const double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    const char* out[] = {"playing", "won", "lost"};
    std::printf("{\"seed\":%" PRIu64 ",\"size\":%d,\"ticks\":%ld,\"settlers\":%zu,\"buildings\":%zu,"
                "\"wood\":%" PRId64 ",\"stone\":%" PRId64 ",\"food\":%" PRId64 ",\"outcome\":\"%s\","
                "\"fingerprint\":\"%016" PRIx64 "\",\"ticks_per_s\":%.1f}\n",
                p.seed, p.width, ticks, c.settlers.size(), c.buildings.size(), c.globals.wood, c.globals.stone,
                c.globals.food, out[static_cast<int>(outcome(c))], fingerprint(c), sec > 0 ? static_cast<double>(ticks) / sec : 0.0);
    return 0;
}
