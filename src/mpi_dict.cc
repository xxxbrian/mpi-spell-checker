#include "mpi_dict.h"
#include <mpi.h>
#include <cctype>
#include <vector>
#include <cstring>
#include "mpi_string_exchange.h"
#include "mpi_utils.h"

void DistributedDict::Insert(const std::string& word) {
  if (!word.empty()) {
    words_.insert(word);
  }
}

bool DistributedDict::Contains(const std::string& word) const {
  // Exact match
  if (words_.count(word)) {
    return true;
  }

  // First-letter lowercase match (e.ii in spec)
  if (!word.empty() && std::isupper(word[0])) {
    std::string lower_first = word;
    lower_first[0] = std::tolower(lower_first[0]);
    if (words_.count(lower_first)) {
      return true;
    }
  }

  return false;
}

DistributedDict BuildDistributedDict(const char* text, size_t text_size,
                                     int rank, int world_size) {
  DistributedDict dict;

  if (world_size <= 1) {
    size_t i = 0;
    while (i < text_size) {
      std::string word;
      while (i < text_size && text[i] != '\n') {
        word.push_back(text[i]);
        ++i;
      }

      if (!word.empty()) {
        dict.Insert(word);
      }

      if (i < text_size && text[i] == '\n') {
        ++i;
      }
    }
    return dict;
  }

  std::vector<std::vector<std::string>> outbound(world_size);

  size_t i = 0;
  while (i < text_size) {
    std::string word;
    while (i < text_size && text[i] != '\n') {
      word.push_back(text[i]);
      ++i;
    }

    if (!word.empty()) {
      int owner = GetOwnerRank(word, world_size);
      if (owner == rank) {
        dict.Insert(word);
      } else {
        outbound[owner].push_back(word);
      }
    }

    if (i < text_size && text[i] == '\n') {
      ++i;
    }
  }

  StringExchangePlan plan;
  PrepareStringExchange(outbound, nullptr, rank, world_size, &plan);
  ExecuteStringExchange(&plan);

  std::vector<std::vector<std::string>> inbound;
  UnpackStringExchange(plan, &inbound);

  for (int source = 0; source < world_size; ++source) {
    for (const auto& word : inbound[source]) {
      dict.Insert(word);
    }
  }

  return dict;
}
