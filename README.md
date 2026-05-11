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

### 1
 Docs are ranked in parallel using a chunk-based strategy. Each chunk of 500 docs is scored entirely on one worker thread with a thread-local heap, then merged into the global top-N under a single lock. This minimizes both task queue contention (one enqueue per 500 docs) and result mutex contention (one lock per 500 docs).

```cpp
std::vector<std::string> query = {"open", "source", "search"};

// Position data must outlive parallel_rank since RankDoc holds raw pointers into it
std::vector<std::vector<std::vector<size_t>>> body_positions = { ... };

std::vector<RankDoc> docs = { ... };

ThreadPool pool(std::thread::hardware_concurrency());
Ranker ranker(query, /*weights_path=*/nullptr, /*maxResults=*/10);

ranker.parallel_rank(docs, pool);  // blocks until all chunks finish

for (const auto& r : ranker.TopResults())
    std::cout << r.score << "  " << r.url << "\n";
```

`TopResults()` returns results sorted highest-score first. Ties are broken by document position in the input vector (earlier index wins).
