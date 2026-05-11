#pragma once

#include <cstddef>
#include <string>
#include <vector>

struct RankDoc
{
  // URL entry fields
  std::string url;
  std::string domain;
  size_t depth{0};
  size_t seedDomainDepth{0};

  // Query-time signals
  std::string title;
  size_t shardOrdinal{0};
  size_t outlink_count{0};
  size_t title_word_count{0};
  const std::vector<std::vector<size_t>>* body_term_positions{nullptr};   // always set
  const std::vector<std::vector<size_t>>* title_term_positions{nullptr};  // null if unavailable
  const std::vector<std::vector<size_t>>* anchor_term_positions{nullptr}; // null if unavailable
};
