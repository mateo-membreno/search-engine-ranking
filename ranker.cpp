#include "ranker.h"
#include "ranking_detail.h"

#include <algorithm>

Ranker::Ranker(size_t maxResults)
{
  SetMaxResults(maxResults);
}

Ranker::~Ranker()
{
  for (const WeightedStrategy& ws : Strategies)
    delete ws.strategy;
}

void Ranker::SetMaxResults(size_t maxResults)
{
  MaxResults_ = maxResults == 0 ? 10 : maxResults;
}

void Ranker::ResetResults()
{
  Top10 = {};
  insertionCount_ = 0;
}

void Ranker::AddStrategy(Strategy* strategy, double weight)
{
  Strategies.push_back({strategy, weight});
}

void Ranker::SetWeight(size_t index, double weight)
{
  if (index < Strategies.size())
    Strategies[index].weight = weight;
}

void Ranker::SetQuery(const std::vector<std::string>& query)
{
  for (const WeightedStrategy& ws : Strategies)
    ws.strategy->SetQuery(query);
}

double Ranker::ScoreDetailed(const RankDoc& doc, std::vector<double>& out_scores) const
{
  double combined = 0.0;
  out_scores.clear();

  for (const WeightedStrategy& ws : Strategies)
  {
    const double s = ws.weight * ws.strategy->Score(doc);
    out_scores.push_back(s);
    combined += s;
  }

  return combined;
}

void Ranker::serial_rank(const std::vector<RankDoc>& all_docs)
{
  std::vector<double> per_strategy;
  per_strategy.reserve(Strategies.size());

  for (size_t doc_idx = 0; doc_idx < all_docs.size(); ++doc_idx)
  {
    const RankDoc& doc = all_docs[doc_idx];
    if (doc.url.empty()) continue;

    const double score = ScoreDetailed(doc, per_strategy);

    if (Top10.size() == MaxResults_ && score <= Top10.top().score)
      continue;

    RankedResult r;
    r.url             = doc.url;
    r.domain          = doc.domain;
    r.depth           = doc.depth;
    r.seedDomainDepth = doc.seedDomainDepth;
    r.title           = doc.title.empty() ? doc.url : doc.title;
    r.score           = score;
    r.strategy_scores = per_strategy;  // copy, not move — keeps per_strategy's capacity
    r.shardOrdinal    = doc.shardOrdinal;
    r.insertionIndex  = doc_idx;

    Top10.push(std::move(r));
    if (Top10.size() > MaxResults_)
      Top10.pop();
  }
}

void Ranker::parallel_rank_a(const std::vector<RankDoc>& all_docs, ThreadPool& pool)
{
  constexpr size_t chunk_size = 500;
  const size_t total = all_docs.size();

  for (size_t i = 0; i < total; i += chunk_size)
  {
    const size_t end = std::min(i + chunk_size, total);

    pool.enqueue([this, i, end, &all_docs]()
    {
      std::priority_queue<RankedResult, std::vector<RankedResult>, std::greater<RankedResult>> local_top;

      for (size_t doc_idx = i; doc_idx < end; ++doc_idx)
      {
        const RankDoc& doc = all_docs[doc_idx];
        if (doc.url.empty()) continue;

        std::vector<double> per_strategy;
        const double score = ScoreDetailed(doc, per_strategy);

        if (local_top.size() == MaxResults_ && score <= local_top.top().score)
          continue;
        
        RankedResult r;
        r.url             = doc.url;
        r.domain          = doc.domain;
        r.depth           = doc.depth;
        r.seedDomainDepth = doc.seedDomainDepth;
        r.title           = doc.title.empty() ? doc.url : doc.title;
        r.score           = score;
        r.strategy_scores = std::move(per_strategy);
        r.shardOrdinal    = doc.shardOrdinal;
        r.insertionIndex  = doc_idx;

        local_top.push(std::move(r));
        if (local_top.size() > MaxResults_)
          local_top.pop();
      }

      std::lock_guard<std::mutex> lock(results_mutex_);
      while (!local_top.empty())
      {
        RankedResult candidate = local_top.top();
        local_top.pop();
        if (Top10.size() < MaxResults_ || candidate > Top10.top())
        {
          Top10.push(std::move(candidate));
          if (Top10.size() > MaxResults_)
            Top10.pop();
        }
      }
    });
  }

  pool.wait();
}

void Ranker::parallel_rank_b(const std::vector<RankDoc>& all_docs, ThreadPool& pool)
{
  constexpr size_t chunk_size = 500;
  const size_t total = all_docs.size();

  for (size_t i = 0; i < total; i += chunk_size)
  {
    const size_t end = std::min(i + chunk_size, total);

    pool.enqueue([this, i, end, &all_docs]()
    {
      std::priority_queue<RankedResult, std::vector<RankedResult>, std::greater<RankedResult>> local_top;

      std::vector<double> per_strategy;
      per_strategy.reserve(Strategies.size());

      for (size_t doc_idx = i; doc_idx < end; ++doc_idx)
      {
        const RankDoc& doc = all_docs[doc_idx];
        if (doc.url.empty()) continue;

        const double score = ScoreDetailed(doc, per_strategy);

        if (local_top.size() == MaxResults_ && score <= local_top.top().score)
          continue;

        RankedResult r;
        r.url             = doc.url;
        r.domain          = doc.domain;
        r.depth           = doc.depth;
        r.seedDomainDepth = doc.seedDomainDepth;
        r.title           = doc.title.empty() ? doc.url : doc.title;
        r.score           = score;
        r.strategy_scores = per_strategy;
        r.shardOrdinal    = doc.shardOrdinal;
        r.insertionIndex  = doc_idx;

        local_top.push(std::move(r));
        if (local_top.size() > MaxResults_)
          local_top.pop();
      }

      std::lock_guard<std::mutex> lock(results_mutex_);
      while (!local_top.empty())
      {
        RankedResult candidate = local_top.top();
        local_top.pop();
        if (Top10.size() < MaxResults_ || candidate > Top10.top())
        {
          Top10.push(std::move(candidate));
          if (Top10.size() > MaxResults_)
            Top10.pop();
        }
      }
    });
  }

  pool.wait();
}

std::vector<Ranker::RankedResult> Ranker::TopResults()
{
  std::vector<RankedResult> result;
  result.reserve(Top10.size());
  while (!Top10.empty())
  {
    result.push_back(Top10.top());
    Top10.pop();
  }
  std::sort(result.begin(), result.end(),
            [](const RankedResult& a, const RankedResult& b) { return a > b; });
  return result;
}
