#include "mpi_candidates.h"

std::vector<std::string> GenerateEditDistance1Candidates(const std::string& word) {
  std::vector<std::string> candidates;
  const std::string alphabet =
      "abcdefghijklmnopqrstuvwxyz"
      "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
      "0123456789";

  // 1. Deletion: remove one character
  for (size_t i = 0; i < word.length(); ++i) {
    std::string candidate = word.substr(0, i) + word.substr(i + 1);
    candidates.push_back(candidate);
  }

  // 2. Replacement: replace one character with another alphanumeric
  for (size_t i = 0; i < word.length(); ++i) {
    for (char c : alphabet) {
      if (c != word[i]) {
        std::string candidate = word;
        candidate[i] = c;
        candidates.push_back(candidate);
      }
    }
  }

  // 3. Insertion: add one alphanumeric character at any position
  for (size_t i = 0; i <= word.length(); ++i) {
    for (char c : alphabet) {
      std::string candidate = word.substr(0, i) + c + word.substr(i);
      candidates.push_back(candidate);
    }
  }

  return candidates;
}
