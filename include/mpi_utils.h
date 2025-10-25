#ifndef MPI_UTILS_H
#define MPI_UTILS_H

#include <mpi.h>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

char* LoadDictionaryShard(const char* filename, int rank, int world_size, size_t* shard_size);

std::vector<std::string> BuildMpiWordList(const char* text, size_t text_size);

bool NormalizeFirstLower(std::string_view word, std::string* out);

int GetOwnerRank(const std::string& word, int world_size);

#endif  // MPI_UTILS_H
