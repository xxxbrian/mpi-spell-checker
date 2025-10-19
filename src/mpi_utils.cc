#include "mpi_utils.h"
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string_view>

char* LoadDictionaryShard(const char* filename, int rank, int world_size, size_t* shard_size) {
  // Get file size
  MPI_File fh;
  MPI_File_open(MPI_COMM_WORLD, filename, MPI_MODE_RDONLY, MPI_INFO_NULL, &fh);

  MPI_Offset file_size;
  MPI_File_get_size(fh, &file_size);

  // Calculate approximate chunk size per rank
  MPI_Offset chunk_size = file_size / world_size;
  MPI_Offset start_offset = rank * chunk_size;
  MPI_Offset end_offset = (rank == world_size - 1) ? file_size : (rank + 1) * chunk_size;

  // Adjust to line boundaries
  char* buffer = nullptr;
  *shard_size = 0;

  if (start_offset < file_size) {
    // Move start_offset to beginning of next line (unless rank 0)
    if (rank > 0) {
      MPI_File_seek(fh, start_offset, MPI_SEEK_SET);
      char ch;
      while (start_offset < file_size) {
        MPI_File_read(fh, &ch, 1, MPI_CHAR, MPI_STATUS_IGNORE);
        start_offset++;
        if (ch == '\n')
          break;
      }
    }

    // Move end_offset to end of current line (unless last rank)
    if (rank < world_size - 1) {
      MPI_File_seek(fh, end_offset, MPI_SEEK_SET);
      char ch;
      while (end_offset < file_size) {
        MPI_File_read(fh, &ch, 1, MPI_CHAR, MPI_STATUS_IGNORE);
        end_offset++;
        if (ch == '\n')
          break;
      }
    } else {
      end_offset = file_size;
    }

    // Read the shard
    *shard_size = end_offset - start_offset;
    if (*shard_size > 0) {
      buffer = (char*)malloc(*shard_size);
      MPI_File_seek(fh, start_offset, MPI_SEEK_SET);
      MPI_File_read(fh, buffer, *shard_size, MPI_CHAR, MPI_STATUS_IGNORE);
    }
  }

  MPI_File_close(&fh);
  return buffer;
}

std::vector<std::string> BuildMpiWordList(const char* text, size_t text_size) {
  std::vector<std::string> words;
  size_t i = 0;

  while (i < text_size) {
    std::string word;
    while (i < text_size && text[i] != '\n') {
      word.push_back(text[i]);
      ++i;
    }

    if (!word.empty()) {
      words.push_back(word);
    }

    // Skip newline
    if (i < text_size && text[i] == '\n') {
      ++i;
    }
  }

  return words;
}

bool NormalizeFirstLower(std::string_view word, std::string* out) {
  if (!out || word.empty()) {
    return false;
  }
  unsigned char first = static_cast<unsigned char>(word[0]);
  char lower = static_cast<char>(std::tolower(first));
  if (lower == static_cast<char>(first)) {
    return false;
  }
  out->assign(word.begin(), word.end());
  (*out)[0] = lower;
  return true;
}

int GetOwnerRank(const std::string& word, int world_size) {
  std::hash<std::string> hasher;
  std::string normalized;
  const std::string* key = &word;
  if (NormalizeFirstLower(word, &normalized)) {
    key = &normalized;
  }
  size_t hash_value = hasher(*key);
  return hash_value % world_size;
}
