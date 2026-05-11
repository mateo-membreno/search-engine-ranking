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



### Performance Benchmarks (8 Threads)

| Iteration | Document Count | Serial Time | Parallel A | A Speedup | Parallel B | B Speedup |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| 1 | 1,000,000 | 404ms | 160ms | **2.53x** | 89ms | **4.54x** |
| 2 | 1,000,000 | 392ms | 165ms | **2.38x** | 87ms | **4.51x** |
| 3 | 1,000,000 | 390ms | 159ms | **2.45x** | 88ms | **4.43x** |
| 4 | 1,000,000 | 386ms | 161ms | **2.40x** | 85ms | **4.54x** |
| 5 | 1,000,000 | 386ms | 159ms | **2.43x** | 87ms | **4.44x** |
| **AVG** | **1,000,000** | **391.6ms** | **160.8ms** | **2.44x** | **87.2ms** | **4.49x** |
