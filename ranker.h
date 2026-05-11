#pragma once

#include "ranking_doc.h"
#include "ranker_weights.h"
#include "thread_pool.h"

#include <cstddef>
#include <functional>
#include <mutex>
#include <queue>
#include <string>
#include <vector>

class Ranker
{
public:
  explicit Ranker(const std::vector<std::string>& query_terms,
                  const char* weights_path = nullptr,
                  size_t maxResults = 10);
  ~Ranker();

  Ranker(const Ranker&)            = delete;
  Ranker& operator=(const Ranker&) = delete;

  class Strategy
  {
  public:
    virtual ~Strategy() = default;
    virtual double Score(const RankDoc& doc) const = 0;
  };

  void SetMaxResults(size_t maxResults);
  void ResetResults();
  void SetWeights(const RankerWeights& weights);

  double ScoreDetailed(const RankDoc& doc, std::vector<double>& out_scores) const;

  struct RankedResult
  {
    std::string url;
    std::string domain;
    size_t depth{0};
    size_t seedDomainDepth{0};
    std::string title;
    double score{0.0};
    size_t shardOrdinal{0};
    size_t insertionIndex{0};
    std::vector<double> strategy_scores; // weighted score per strategy, in registration order

    bool operator<(const RankedResult& other) const
    {
      if (score != other.score)
        return score < other.score;
      return insertionIndex > other.insertionIndex;
    }
    bool operator>(const RankedResult& other) const
    {
      return other < *this;
    }
  };

  void serial_rank(const std::vector<RankDoc>& all_docs);
  void parallel_rank(const std::vector<RankDoc>& all_docs, ThreadPool& pool);

  std::vector<RankedResult> TopResults() const;

private:
  struct WeightedStrategy
  {
    Strategy* strategy{nullptr};
    double weight{1.0};
  };

  std::vector<WeightedStrategy> Strategies;
  std::priority_queue<RankedResult, std::vector<RankedResult>, std::greater<RankedResult>> Top10;
  size_t MaxResults_{10};
  size_t insertionCount_{0};
  mutable std::mutex results_mutex_;
};
