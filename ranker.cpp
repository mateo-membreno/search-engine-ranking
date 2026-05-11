#include "ranker.h"
#include "ranking_detail.h"
#include "ranker_weights.h"
#include "Strategy.h"

#include <algorithm>

Ranker::Ranker(const std::vector<std::string>& query_terms, const char* weights_path, size_t maxResults)
{
  SetMaxResults(maxResults);

  Strategies.push_back({new DomainStrat()});
  Strategies.push_back({new ShortURLStrat()});
  Strategies.push_back({new ShortTitleStrat()});
  Strategies.push_back({new OutlinkCount()});
  Strategies.push_back({new UrlDepth()});
  Strategies.push_back({new SeedDomainDepth()});
  Strategies.push_back({new UrlPathDepth()});
  Strategies.push_back({new QueryParamPenalty()});
  Strategies.push_back({new UrlTermPresenceStrat(query_terms)});
  Strategies.push_back({new UrlTermProximityStrat(query_terms)});
  Strategies.push_back({new ExactPhraseTitleStrat()});
  Strategies.push_back({new ExactPhraseBodyStrat()});
  Strategies.push_back({new ExactPhraseAnchorStrat()});
  Strategies.push_back({new OrderingTitleStrat()});
  Strategies.push_back({new OrderingBodyStrat()});
  Strategies.push_back({new OrderingAnchorStrat()});
  Strategies.push_back({new EarlyMatchTitleStrat()});
  Strategies.push_back({new EarlyMatchBodyStrat()});
  Strategies.push_back({new EarlyMatchAnchorStrat()});
  Strategies.push_back({new CoverageTitleStrat()});
  Strategies.push_back({new CoverageBodyStrat()});
  Strategies.push_back({new CoverageAnchorStrat()});
  Strategies.push_back({new ExactTitleMatch(query_terms)});

  SetWeights(weights_path ? LoadRankerWeights(weights_path) : RankerWeights{});
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

void Ranker::SetWeights(const RankerWeights& w)
{
  Strategies[0].weight  = w.domain;
  Strategies[1].weight  = w.short_url;
  Strategies[2].weight  = w.short_title;
  Strategies[3].weight  = w.outlink_count;
  Strategies[4].weight  = w.url_depth;
  Strategies[5].weight  = w.seed_domain_depth;
  Strategies[6].weight  = w.url_path_depth;
  Strategies[7].weight  = w.query_param_penalty;
  Strategies[8].weight  = w.url_term_presence;
  Strategies[9].weight  = w.url_term_proximity;
  Strategies[10].weight = w.exact_phrase_title;
  Strategies[11].weight = w.exact_phrase_body;
  Strategies[12].weight = w.exact_phrase_anchor;
  Strategies[13].weight = w.ordering_title;
  Strategies[14].weight = w.ordering_body;
  Strategies[15].weight = w.ordering_anchor;
  Strategies[16].weight = w.early_match_title;
  Strategies[17].weight = w.early_match_body;
  Strategies[18].weight = w.early_match_anchor;
  Strategies[19].weight = w.coverage_title;
  Strategies[20].weight = w.coverage_body;
  Strategies[21].weight = w.coverage_anchor;
  Strategies[22].weight = w.exact_title_match;
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
