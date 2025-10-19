#ifndef MPI_UTILS_H
#define MPI_UTILS_H

#include <mpi.h>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

// Load dictionary shard for this rank using MPI-IO or simple partitioning
// Returns the text chunk that this rank is responsible for
char* LoadDictionaryShard(const char* filename, int rank, int world_size, size_t* shard_size);

// Build word list from text (all ranks can do this independently or use broadcast)
std::vector<std::string> BuildMpiWordList(const char* text, size_t text_size);

// Normalize the first character to lowercase when needed.
// Returns true when normalization happened and stores the result in `out`.
bool NormalizeFirstLower(std::string_view word, std::string* out);

// Compute owner rank for a given word (hash-based distribution)
int GetOwnerRank(const std::string& word, int world_size);

#endif  // MPI_UTILS_H
