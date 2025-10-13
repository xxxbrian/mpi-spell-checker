#include "mpi_dict.h"
#include <cctype>

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

DistributedDict BuildDistributedDict(const char* text, size_t text_size) {
  DistributedDict dict;
  size_t i = 0;

  while (i < text_size) {
    std::string word;
    while (i < text_size && text[i] != '\n') {
      word.push_back(text[i]);
      ++i;
    }

    if (!word.empty()) {
      dict.Insert(word);

      // Also insert the version with first letter uppercase
      // to handle lowercase dictionary entries matching uppercase queries
      if (!word.empty() && std::islower(word[0])) {
        std::string upper_first = word;
        upper_first[0] = std::toupper(upper_first[0]);
        dict.Insert(upper_first);
      }
    }

    // Skip newline
    if (i < text_size && text[i] == '\n') {
      ++i;
    }
  }

  return dict;
}
