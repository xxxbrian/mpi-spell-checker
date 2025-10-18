#include "mpi_dict.h"
#include <mpi.h>
#include <cctype>
#include <vector>
#include <cstring>
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

  std::vector<int> send_counts(world_size, 0);
  for (int target = 0; target < world_size; ++target) {
    if (target == rank) continue;
    send_counts[target] = static_cast<int>(outbound[target].size());
  }

  std::vector<int> recv_counts(world_size, 0);
  MPI_Alltoall(send_counts.data(), 1, MPI_INT, recv_counts.data(), 1, MPI_INT,
               MPI_COMM_WORLD);

  int total_send_words = 0;
  for (int target = 0; target < world_size; ++target) {
    total_send_words += send_counts[target];
  }

  std::vector<int> send_displs(world_size, 0);
  for (int target = 1; target < world_size; ++target) {
    send_displs[target] = send_displs[target - 1] + send_counts[target - 1];
  }

  std::vector<int> send_lengths(total_send_words, 0);
  for (int target = 0; target < world_size; ++target) {
    int offset = send_displs[target];
    for (int j = 0; j < send_counts[target]; ++j) {
      send_lengths[offset + j] = static_cast<int>(outbound[target][j].size());
    }
  }

  int total_recv_words = 0;
  for (int source = 0; source < world_size; ++source) {
    total_recv_words += recv_counts[source];
  }

  std::vector<int> recv_lengths(total_recv_words, 0);
  std::vector<int> recv_displs(world_size, 0);
  for (int source = 1; source < world_size; ++source) {
    recv_displs[source] = recv_displs[source - 1] + recv_counts[source - 1];
  }

  int* send_lengths_ptr = send_lengths.empty() ? nullptr : send_lengths.data();
  int* recv_lengths_ptr = recv_lengths.empty() ? nullptr : recv_lengths.data();
  MPI_Alltoallv(send_lengths_ptr, send_counts.data(), send_displs.data(),
                MPI_INT, recv_lengths_ptr, recv_counts.data(),
                recv_displs.data(), MPI_INT, MPI_COMM_WORLD);

  std::vector<int> send_bytes_counts(world_size, 0);
  std::vector<int> send_bytes_displs(world_size, 0);
  int total_send_bytes = 0;
  for (int target = 0; target < world_size; ++target) {
    int bytes = 0;
    int offset = send_displs[target];
    for (int j = 0; j < send_counts[target]; ++j) {
      bytes += send_lengths[offset + j];
    }
    send_bytes_counts[target] = bytes;
    if (target > 0) {
      send_bytes_displs[target] =
          send_bytes_displs[target - 1] + send_bytes_counts[target - 1];
    }
    total_send_bytes += bytes;
  }

  std::vector<char> send_bytes(total_send_bytes);
  for (int target = 0; target < world_size; ++target) {
    int byte_offset = send_bytes_displs[target];
    for (int j = 0; j < send_counts[target]; ++j) {
      const std::string& word = outbound[target][j];
      std::memcpy(send_bytes.data() + byte_offset, word.data(), word.size());
      byte_offset += static_cast<int>(word.size());
    }
  }

  std::vector<int> recv_bytes_counts(world_size, 0);
  std::vector<int> recv_bytes_displs(world_size, 0);
  int total_recv_bytes = 0;
  for (int source = 0; source < world_size; ++source) {
    int bytes = 0;
    int offset = recv_displs[source];
    for (int j = 0; j < recv_counts[source]; ++j) {
      bytes += recv_lengths[offset + j];
    }
    recv_bytes_counts[source] = bytes;
    if (source > 0) {
      recv_bytes_displs[source] =
          recv_bytes_displs[source - 1] + recv_bytes_counts[source - 1];
    }
    total_recv_bytes += bytes;
  }

  std::vector<char> recv_bytes(total_recv_bytes);

  char* send_bytes_ptr = send_bytes.empty() ? nullptr : send_bytes.data();
  char* recv_bytes_ptr = recv_bytes.empty() ? nullptr : recv_bytes.data();
  MPI_Alltoallv(send_bytes_ptr, send_bytes_counts.data(),
                send_bytes_displs.data(), MPI_CHAR, recv_bytes_ptr,
                recv_bytes_counts.data(), recv_bytes_displs.data(), MPI_CHAR,
                MPI_COMM_WORLD);

  for (int source = 0; source < world_size; ++source) {
    int length_offset = recv_displs[source];
    int byte_offset = recv_bytes_displs[source];
    for (int j = 0; j < recv_counts[source]; ++j) {
      int len = recv_lengths[length_offset + j];
      if (len > 0) {
        std::string word(recv_bytes.data() + byte_offset,
                         recv_bytes.data() + byte_offset + len);
        dict.Insert(word);
      }
      byte_offset += len;
    }
  }

  return dict;
}
