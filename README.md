# Thread Pool & Search Ranker

## Redacted Files

`ranking_detail.h` and `ranker_weights.h`. Specifically, the following have been redacted:

- **Ranker weights** — load function and scoring coefficients used across ranking signals (`ranker_weights.h`)
- **Exact phrase match algorithm** (`ranking_detail.h`)
- **Term ordering score algorithm** (`ranking_detail.h`)
- **Term coverage algorithm** (`ranking_detail.h`)
- **Early term match score algorithm** (`ranking_detail.h`)

`ranker.h` and `ranker.cpp` are intact. Some redactions / modifications were done at the request of my professor and myself to prevent future students from copying the work, as this ranking system was originally submitted as a class project.

## Note on the Ranking Code

The ranking code visible in this repository is **not** the version submitted for class. It reflects improvements and additions made after submission.


## Usage

Three ranking methods are available:

**`serial_rank`** — single-threaded baseline. Scores docs one at a time on the calling thread.

**`parallel_rank_a`** — chunks of 500 docs are scored in parallel across worker threads. Each doc allocates a fresh `vector<double>` for its strategy scores.

**`parallel_rank_b`** — same chunk structure as `parallel_rank_a` but with the strategy score vector allocated once per chunk and reused across all 500 docs. String copies into `RankedResult`

```cpp
std::vector<std::string> query = {"open", "source", "search"};

// RankDoc holds raw pointers into the position data and does not own them
// The position store must be kept alive in the same scope as the ranking call.
std::vector<std::vector<std::vector<size_t>>> position_store = { ... };
//  ^ outer: one entry per doc
//      ^ middle: one entry per query term
//          ^ inner: sorted absolute word positions where that term appears

// Each doc points into position_store[i]
std::vector<RankDoc> docs;

ThreadPool pool(std::thread::hardware_concurrency());

// ranker(query, weights path, max results)
Ranker ranker(query, nullptr, 10);

ranker.parallel_rank_b(docs, pool);

for (const auto& r : ranker.TopResults())
    std::cout << r.score << "  " << r.url << "\n";
```

`TopResults()` returns results sorted highest-score first. Ties are broken by document position in the input vector (earlier index wins).



## Speed

1000000 docs        serial 404ms    (8 threads) parallel_a 160ms  2.525x   parallel_b 89ms  4.53933x
1000000 docs        serial 392ms    (8 threads) parallel_a 165ms  2.37576x parallel_b 87ms  4.50575x
1000000 docs        serial 390ms    (8 threads) parallel_a 159ms  2.45283x parallel_b 88ms  4.43182x
1000000 docs        serial 386ms    (8 threads) parallel_a 161ms  2.39752x parallel_b 85ms  4.54118x
1000000 docs        serial 386ms    (8 threads) parallel_a 159ms  2.42767x parallel_b 87ms  4.43678x
