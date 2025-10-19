#ifndef MPI_DICT_H
#define MPI_DICT_H

#include <string>
#include <unordered_set>
#include <vector>

// MPI-specific dictionary shard for distributed spell checking
class DistributedDict {
 public:
  DistributedDict() = default;

  // Add a word to the local shard
  void Insert(const std::string& word);

  // Check if word is in this shard (exact match or first-letter lowercase match)
  bool Contains(const std::string& word) const;

  // Get the number of words in this shard
  size_t Size() const { return words_.size(); }
  const std::unordered_set<std::string>& Words() const { return words_; }

 private:
  std::unordered_set<std::string> words_;
};

// Build a dictionary shard from text chunk
DistributedDict BuildDistributedDict(const char* text, size_t text_size,
                                     int rank, int world_size);

#endif  // MPI_DICT_H
