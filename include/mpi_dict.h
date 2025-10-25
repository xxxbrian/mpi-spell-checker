#ifndef MPI_DICT_H
#define MPI_DICT_H

#include <string>
#include <unordered_set>
#include <vector>

// MPI-specific dictionary shard for distributed spell checking
class DistributedDict {
 public:
  DistributedDict() = default;

  void Insert(const std::string& word);

  bool Contains(const std::string& word) const;

  size_t Size() const { return words_.size(); }

  const std::unordered_set<std::string>& Words() const { return words_; }

 private:
  std::unordered_set<std::string> words_;
};

DistributedDict BuildDistributedDict(const char* text, size_t text_size, int rank, int world_size);

#endif  // MPI_DICT_H
