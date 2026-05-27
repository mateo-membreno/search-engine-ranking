# Thread Pool & Search Ranker

A C++ document ranking system built on a custom thread pool. The ranker scores candidate documents across 23 weighted signals and returns the top-K results. Three ranking modes are provided: a single-threaded baseline and two parallel variants that demonstrate the allocation cost of per-document heap vectors.

---

## Redacted Files

`ranking_detail.h` and `ranker_weights.h` are partially redacted at the request of the original course instructor to prevent copying, as this ranking system was submitted as a class project. The following are omitted:

- **Ranker weights** — weight-loading function and scoring coefficients (`ranker_weights.h`)
- **Exact phrase match algorithm** (`ranking_detail.h`)
- **Term ordering score algorithm** (`ranking_detail.h`)
- **Term coverage algorithm** (`ranking_detail.h`)
- **Early term match score algorithm** (`ranking_detail.h`)

`ranker.h`, `ranker.cpp`, and `Strategy.h` are intact.

> **Note:** The ranking code in this repository reflects improvements made after the original submission and is not identical to what was turned in.

---

## Architecture

### Thread Pool

`ThreadPool` is a standard fixed-size worker pool backed by a `std::queue` of `std::function<void()>` tasks. Workers block on a `std::condition_variable` and wake when work is available. An atomic counter tracks in-flight tasks so that `wait()` blocks the caller until the queue drains completely.

```
enqueue(task) → push to queue, notify one worker
wait()        → block until active_tasks == 0 and queue is empty
```

### Ranker

The ranker implements a weighted, additive scoring model using the [Strategy pattern](https://en.wikipedia.org/wiki/Strategy_pattern). Each `Strategy` subclass computes a normalized score in `[0.0, 1.0]` for a single signal. The final document score is:

```
score(doc) = Σ weight_i × strategy_i.Score(doc)
```

Weights are loaded from a file at construction time; if no path is provided, all weights default to `1.0`. Each call to `ScoreDetailed` returns both the combined score and the per-strategy breakdown, which is stored on each `RankedResult` for inspection.

Results are maintained in a min-heap of size K (default 10). A document is only inserted if its score exceeds the current minimum, keeping memory and merge cost proportional to K rather than the full corpus size.

---

## Ranking Signals

The 23 strategies are organized into four groups:

### Document Quality

| Strategy | Description |
| :--- | :--- |
| `DomainStrat` | TLD tier score: `.edu`/`.gov`/`.org` → 1.0, `.com` → 0.6, `.xyz`/`.net`/`.io` → 0.3, other → 0.15 |
| `ShortURLStrat` | Piecewise linear: 1.0 for URLs ≤ 10 chars, drops to 0.0 at 120 chars |
| `ShortTitleStrat` | Piecewise linear: 1.0 for titles ≤ 5 words, drops to 0.0 at 15 words |
| `OutlinkCount` | Log₂-scaled outlink count, capped at 1.0 for 32+ outlinks |
| `UrlDepth` | Crawl depth from seed: 1.0 for depth ≤ 2, drops to 0.0 at depth 8 |
| `SeedDomainDepth` | Domain-hop count from seed: 1.0 for ≤ 1 hop, drops to 0.0 at 5 hops |
| `UrlPathDepth` | URL path segments: 1.0 for 0–1 segments, drops to 0.0 at 6 segments |
| `QueryParamPenalty` | Returns 1.0 if the URL contains a `q=` parameter (search-within-search signal) |

### Query–URL Match

| Strategy | Description |
| :--- | :--- |
| `UrlTermPresenceStrat` | Fraction of query terms that appear anywhere in the URL |
| `UrlTermProximityStrat` | Finds the longest ordered subsequence of query terms in the URL and scores by compactness: `(matched / total) × 1 / (1 + gap_chars)` |

### Content Signals (Body / Title / Anchor)

Each of the four algorithms below is applied independently to three text fields, yielding 12 strategies total.

| Algorithm | Description |
| :--- | :--- |
| **Exact phrase** | Binary: 1.0 if all query terms appear consecutively in the correct order, 0.0 otherwise |
| **Term ordering** | Continuous score for how well the relative order of query terms matches their order in the document |
| **Early match** | Rewards documents where query terms appear near the beginning of the content |
| **Term coverage** | Fraction of distinct query terms that appear at least once in the field |

### Composite

| Strategy | Description |
| :--- | :--- |
| `ExactTitleMatch` | 1.0 only if the title consists of exactly the query terms in order with no extra words — a strict, high-signal match |

---

## Parallel Ranking

The document corpus is split into chunks of 500 docs. Each chunk is submitted as a task to the thread pool. Workers maintain a **local** min-heap of size K and only acquire the global mutex once per chunk to merge their local results into the shared top-K heap. This keeps lock contention proportional to the number of chunks, not the number of documents.


| Method | Allocation | Notes |
| :--- | :--- | :--- |
| `parallel_rank_a` | One `vector<double>` **per document** | Fresh allocation on every `ScoreDetailed` call |
| `parallel_rank_b` | One `vector<double>` **per chunk**, reused | Allocated once with `reserve`, cleared and reused across all 500 docs in the chunk; copy on insert rather than move |

`parallel_rank_b` avoids ~499 redundant heap allocations per chunk, which accounts for most of its speedup over `parallel_rank_a`.

---

## Usage

```cpp
std::vector<std::string> query = {"open", "source", "search"};

// RankDoc holds raw pointers into per-document position data and does not own them.
// The position store must outlive the ranking call.
std::vector<std::vector<std::vector<size_t>>> position_store = { /* ... */ };
//  ^ outer:  one entry per document
//      ^ middle: one entry per query term
//          ^ inner: sorted absolute word positions where that term appears

std::vector<RankDoc> docs;
// populate docs[i].body_term_positions = &position_store[i], etc.

ThreadPool pool(std::thread::hardware_concurrency());

// Ranker(query_terms, weights_path, max_results)
// Pass nullptr for weights_path to use unit weights.
Ranker ranker(query, nullptr, 10);

ranker.parallel_rank_b(docs, pool);

for (const auto& r : ranker.TopResults())
    std::cout << r.score << "  " << r.url << "\n";
```

`TopResults()` returns results sorted highest-score first. Ties are broken by document position in the input vector (earlier index wins).

---

## Performance Benchmarks (8 Threads)

| Iteration | Serial | Parallel A | A Speedup | Parallel B | B Speedup |
| :---: | :---: | :---: | :---: | :---: | :---: |
| 1 | 2.48M docs/s | 6.25M docs/s | **2.53×** | 11.24M docs/s | **4.54×** |
| 2 | 2.55M docs/s | 6.06M docs/s | **2.38×** | 11.49M docs/s | **4.51×** |
| 3 | 2.56M docs/s | 6.29M docs/s | **2.45×** | 11.36M docs/s | **4.43×** |
| 4 | 2.59M docs/s | 6.21M docs/s | **2.40×** | 11.76M docs/s | **4.54×** |
| 5 | 2.59M docs/s | 6.29M docs/s | **2.43×** | 11.49M docs/s | **4.44×** |
| **Avg** | **2.55M docs/s** | **6.22M docs/s** | **2.44×** | **11.47M docs/s** | **4.49×** |

`parallel_rank_b` sustains roughly **11.5M docs/s** on 8 threads — about 56% parallel efficiency — with the gap over `parallel_rank_a` attributable almost entirely to eliminated per-document allocations.
