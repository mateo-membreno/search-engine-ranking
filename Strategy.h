#include "ranker.h"
#include "ranking_detail.h"

#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Static document-quality strategies
// ---------------------------------------------------------------------------

class DomainStrat : public Ranker::Strategy
{
public:
  double Score(const RankDoc& doc) const override
  {
    const std::string& domain = doc.domain;
    for (const char* s : TopTier)
      if (domain.find(s) != std::string::npos) return 1.0;
    for (const char* s : MidTier)
      if (domain.find(s) != std::string::npos) return 0.6;
    for (const char* s : LowTier)
      if (domain.find(s) != std::string::npos) return 0.3;
    return 0.15;
  }

private:
  const std::vector<const char*> TopTier = {".edu", ".gov", ".org"};
  const std::vector<const char*> MidTier = {".com"};
  const std::vector<const char*> LowTier = {".xyz", ".net", ".io"};
};

class ShortURLStrat : public Ranker::Strategy
{
  // piecewise linear: 1.0 for urls <= short_thres, linearly drops to 0.0 at long_thres
public:
  double Score(const RankDoc& doc) const override
  {
    const double len = static_cast<double>(doc.url.size());
    if (len <= short_thres) return 1.0;
    if (len >= long_thres)  return 0.0;
    return (long_thres - len) / (long_thres - short_thres);
  }

private:
  static constexpr double short_thres = 10.0;
  static constexpr double long_thres  = 120.0;
};

class ShortTitleStrat : public Ranker::Strategy
{
  // piecewise linear: 1.0 for titles <= short_thres words, linearly drops to 0.0 at long_thres
public:
  double Score(const RankDoc& doc) const override
  {
    const double words = static_cast<double>(doc.title_word_count);
    if (words <= short_thres) return 1.0;
    if (words >= long_thres)  return 0.0;
    return (long_thres - words) / (long_thres - short_thres);
  }

private:
  static constexpr double short_thres = 5.0;
  static constexpr double long_thres  = 15.0;
};

class OutlinkCount : public Ranker::Strategy
{
  // capped log scale: pages with 32+ outlinks all score 1.0
public:
  double Score(const RankDoc& doc) const override
  {
    if (doc.outlink_count == 0)
      return 0.0;
    const double score = log2(1.0 + static_cast<double>(doc.outlink_count)) / log2(33.0);
    return score >= 1.0 ? 1.0 : score;
  }
};

class UrlDepth : public Ranker::Strategy
{
  // number of link clicks from seed to get to page
public:
  double Score(const RankDoc& doc) const override
  {
    const double d = static_cast<double>(doc.depth);
    if (d <= shallow_thres) return 1.0;
    if (d >= deep_thres)    return 0.0;
    return (deep_thres - d) / (deep_thres - shallow_thres);
  }

private:
  static constexpr double shallow_thres = 2.0;
  static constexpr double deep_thres    = 8.0;
};

class SeedDomainDepth : public Ranker::Strategy
{
  // number of domain switches from the seed
public:
  double Score(const RankDoc& doc) const override
  {
    const double d = static_cast<double>(doc.seedDomainDepth);
    if (d <= shallow_thres) return 1.0;
    if (d >= deep_thres)    return 0.0;
    return (deep_thres - d) / (deep_thres - shallow_thres);
  }

private:
  static constexpr double shallow_thres = 1.0;
  static constexpr double deep_thres    = 5.0;
};

class UrlPathDepth : public Ranker::Strategy
{
  // 1.0 for 0-1 path segments, linearly drops to 0.0 at max_depth segments
public:
  double Score(const RankDoc& doc) const override
  {
    const std::string& url = doc.url;

    size_t path_start      = std::string::npos;
    const size_t scheme_sep = url.find("://");
    if (scheme_sep != std::string::npos)
      path_start = url.find('/', scheme_sep + 3);

    if (path_start == std::string::npos)
      return 1.0;

    size_t segments = 0;
    size_t i = path_start;
    while (i < url.size())
    {
      if (url[i] == '/')
        ++i;
      else
      {
        ++segments;
        while (i < url.size() && url[i] != '/') ++i;
      }
    }

    if (segments == 0)          return 1.0;
    if (segments >= max_depth)  return 0.0;
    return static_cast<double>(max_depth - segments) / static_cast<double>(max_depth);
  }

private:
  static constexpr size_t max_depth = 6;
};

class QueryParamPenalty : public Ranker::Strategy
{
  // returns 1.0 when the URL contains a "q=" query parameter (search-within-search)
public:
  double Score(const RankDoc& doc) const override
  {
    const std::string& url = doc.url;
    if (url.find("?q=")    != std::string::npos ||
        url.find("&q=")    != std::string::npos ||
        url.find("&amp;q=") != std::string::npos)
      return 1.0;
    return 0.0;
  }
};

// ---------------------------------------------------------------------------
// URL query-term strategies
// ---------------------------------------------------------------------------

class UrlTermPresenceStrat : public Ranker::Strategy
{
  // fraction of query terms that appear anywhere in the URL
public:
  void SetQuery(const std::vector<std::string>& query) override { QueryTerms = query; }

  double Score(const RankDoc& doc) const override
  {
    if (QueryTerms.empty())
      return 0.0;
    const std::string& url = doc.url;
    size_t matched = 0;
    for (const std::string& term : QueryTerms)
      if (!term.empty() && url.find(term) != std::string::npos) ++matched;
    return static_cast<double>(matched) / static_cast<double>(QueryTerms.size());
  }

private:
  std::vector<std::string> QueryTerms;
};

class UrlTermProximityStrat : public Ranker::Strategy
{
  // how ordered and compact the matched query terms are within the URL.
  // returns 0.0 if fewer than 2 terms are matched in order.
public:
  void SetQuery(const std::vector<std::string>& query) override { QueryTerms = query; }

  double Score(const RankDoc& doc) const override
  {
    const std::string& url    = doc.url;
    const size_t query_len    = QueryTerms.size();
    if (query_len == 0) return 0.0;

    size_t best_len = 0;
    size_t best_gap = 0;

    for (size_t start_term = 0; start_term < query_len; ++start_term)
    {
      const std::string& first_term = QueryTerms[start_term];
      if (first_term.empty()) continue;

      size_t start_pos = url.find(first_term);
      while (start_pos != std::string::npos)
      {
        size_t len      = 1;
        size_t gap_sum  = 0;
        size_t prev_end = start_pos + first_term.size();

        for (size_t term_idx = start_term + 1; term_idx < query_len; ++term_idx)
        {
          const std::string& term = QueryTerms[term_idx];
          if (term.empty()) continue;

          size_t next_pos = url.find(term, prev_end);
          if (next_pos == std::string::npos) break;

          if (next_pos > prev_end) gap_sum += (next_pos - prev_end);
          prev_end = next_pos + term.size();
          ++len;
        }

        if (len > best_len || (len == best_len && gap_sum < best_gap))
        {
          best_len = len;
          best_gap = gap_sum;
        }

        start_pos = url.find(first_term, start_pos + 1);
      }
    }

    if (best_len < 2) return 0.0;

    const double length_ratio = static_cast<double>(best_len) / static_cast<double>(query_len);
    const double proximity    = 1.0 / (1.0 + static_cast<double>(best_gap));
    return length_ratio * proximity;
  }

private:
  std::vector<std::string> QueryTerms;
};

// ---------------------------------------------------------------------------
// Exact phrase match — body / title / anchor
// ---------------------------------------------------------------------------

class ExactPhraseBodyStrat : public Ranker::Strategy
{
public:
  double Score(const RankDoc& doc) const override
  {
    if (doc.body_term_positions == nullptr) return 0.0;
    using namespace ranking_strategy_detail;
    return HasExactPhrase(*doc.body_term_positions) ? 1.0 : 0.0;
  }
};

class ExactPhraseTitleStrat : public Ranker::Strategy
{
public:
  double Score(const RankDoc& doc) const override
  {
    if (doc.title_term_positions == nullptr) return 0.0;
    using namespace ranking_strategy_detail;
    return HasExactPhrase(*doc.title_term_positions) ? 1.0 : 0.0;
  }
};

class ExactPhraseAnchorStrat : public Ranker::Strategy
{
public:
  double Score(const RankDoc& doc) const override
  {
    if (doc.anchor_term_positions == nullptr) return 0.0;
    using namespace ranking_strategy_detail;
    return HasExactPhrase(*doc.anchor_term_positions) ? 1.0 : 0.0;
  }
};

// ---------------------------------------------------------------------------
// Term ordering — body / title / anchor
// ---------------------------------------------------------------------------

class OrderingBodyStrat : public Ranker::Strategy
{
public:
  double Score(const RankDoc& doc) const override
  {
    if (doc.body_term_positions == nullptr) return 0.0;
    using namespace ranking_strategy_detail;
    return OrderingScore(*doc.body_term_positions);
  }
};

class OrderingTitleStrat : public Ranker::Strategy
{
public:
  double Score(const RankDoc& doc) const override
  {
    if (doc.title_term_positions == nullptr) return 0.0;
    using namespace ranking_strategy_detail;
    return OrderingScore(*doc.title_term_positions);
  }
};

class OrderingAnchorStrat : public Ranker::Strategy
{
public:
  double Score(const RankDoc& doc) const override
  {
    if (doc.anchor_term_positions == nullptr) return 0.0;
    using namespace ranking_strategy_detail;
    return OrderingScore(*doc.anchor_term_positions);
  }
};

// ---------------------------------------------------------------------------
// Early term match — body / title / anchor
// ---------------------------------------------------------------------------

class EarlyMatchBodyStrat : public Ranker::Strategy
{
public:
  double Score(const RankDoc& doc) const override
  {
    if (doc.body_term_positions == nullptr) return 0.0;
    using namespace ranking_strategy_detail;
    return EarlyMatchScore(*doc.body_term_positions);
  }
};

class EarlyMatchTitleStrat : public Ranker::Strategy
{
public:
  double Score(const RankDoc& doc) const override
  {
    if (doc.title_term_positions == nullptr) return 0.0;
    using namespace ranking_strategy_detail;
    return EarlyMatchScore(*doc.title_term_positions);
  }
};

class EarlyMatchAnchorStrat : public Ranker::Strategy
{
public:
  double Score(const RankDoc& doc) const override
  {
    if (doc.anchor_term_positions == nullptr) return 0.0;
    using namespace ranking_strategy_detail;
    return EarlyMatchScore(*doc.anchor_term_positions);
  }
};

// ---------------------------------------------------------------------------
// Term coverage — body / title / anchor
// ---------------------------------------------------------------------------

class CoverageBodyStrat : public Ranker::Strategy
{
public:
  double Score(const RankDoc& doc) const override
  {
    if (doc.body_term_positions == nullptr) return 0.0;
    using namespace ranking_strategy_detail;
    return CoverageScore(*doc.body_term_positions);
  }
};

class CoverageTitleStrat : public Ranker::Strategy
{
public:
  double Score(const RankDoc& doc) const override
  {
    if (doc.title_term_positions == nullptr) return 0.0;
    using namespace ranking_strategy_detail;
    return CoverageScore(*doc.title_term_positions);
  }
};

class CoverageAnchorStrat : public Ranker::Strategy
{
public:
  double Score(const RankDoc& doc) const override
  {
    if (doc.anchor_term_positions == nullptr) return 0.0;
    using namespace ranking_strategy_detail;
    return CoverageScore(*doc.anchor_term_positions);
  }
};

// ---------------------------------------------------------------------------
// Composite title strategies
// ---------------------------------------------------------------------------

class ExactTitleMatch : public Ranker::Strategy
{
  // all query terms appear as a consecutive phrase in the title and the title has no extra words
public:
  void SetQuery(const std::vector<std::string>& query) override { QueryTermCount = query.size(); }

  double Score(const RankDoc& doc) const override
  {
    if (QueryTermCount == 0 || doc.title_term_positions == nullptr)
      return 0.0;
    using namespace ranking_strategy_detail;
    const auto& tp = *doc.title_term_positions;
    if (CoverageScore(tp) != 1.0 || !HasExactPhrase(tp))
      return 0.0;
    return doc.title_word_count == QueryTermCount ? 1.0 : 0.0;
  }

private:
  size_t QueryTermCount{0};
};

// ---------------------------------------------------------------------------
// Convenience: register the full default set of 23 strategies.
// Indices match the order below, which is the same as the old RankerWeights
// field order, so SetWeight(index, w) still maps 1:1 to each named signal.
// ---------------------------------------------------------------------------

inline void RegisterAllStrategies(Ranker& ranker)
{
  // [0]  document quality
  ranker.AddStrategy(new DomainStrat());
  ranker.AddStrategy(new ShortURLStrat());
  ranker.AddStrategy(new ShortTitleStrat());
  ranker.AddStrategy(new OutlinkCount());
  ranker.AddStrategy(new UrlDepth());
  ranker.AddStrategy(new SeedDomainDepth());
  ranker.AddStrategy(new UrlPathDepth());
  ranker.AddStrategy(new QueryParamPenalty());
  ranker.AddStrategy(new UrlTermPresenceStrat());
  ranker.AddStrategy(new UrlTermProximityStrat());
  ranker.AddStrategy(new ExactPhraseTitleStrat());
  ranker.AddStrategy(new ExactPhraseBodyStrat());
  ranker.AddStrategy(new ExactPhraseAnchorStrat());
  ranker.AddStrategy(new OrderingTitleStrat());
  ranker.AddStrategy(new OrderingBodyStrat());
  ranker.AddStrategy(new OrderingAnchorStrat());
  ranker.AddStrategy(new EarlyMatchTitleStrat());
  ranker.AddStrategy(new EarlyMatchBodyStrat());
  ranker.AddStrategy(new EarlyMatchAnchorStrat());
  ranker.AddStrategy(new CoverageTitleStrat());
  ranker.AddStrategy(new CoverageBodyStrat());
  ranker.AddStrategy(new CoverageAnchorStrat());
  ranker.AddStrategy(new ExactTitleMatch());
}
