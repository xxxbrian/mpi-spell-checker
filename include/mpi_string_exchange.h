#ifndef MPI_STRING_EXCHANGE_H
#define MPI_STRING_EXCHANGE_H

#include <mpi.h>

#include <cstddef>
#include <cstring>
#include <string>
#include <vector>

struct StringExchangePlan {
  std::vector<int> send_counts;
  std::vector<int> send_displs;
  std::vector<int> recv_counts;
  std::vector<int> recv_displs;
  std::vector<int> send_lengths;
  std::vector<int> recv_lengths;
  std::vector<int> send_bytes_counts;
  std::vector<int> send_bytes_displs;
  std::vector<int> recv_bytes_counts;
  std::vector<int> recv_bytes_displs;
  std::vector<char> send_bytes;
  std::vector<char> recv_bytes;
  std::vector<int> send_indices;
  std::vector<int> recv_indices;
};

inline void ComputeDisplacements(const std::vector<int>& counts, std::vector<int>* displs) {
  displs->assign(counts.size(), 0);
  for (size_t i = 1; i < counts.size(); ++i) {
    (*displs)[i] = (*displs)[i - 1] + counts[i - 1];
  }
}

inline void PrepareStringExchange(const std::vector<std::vector<std::string>>& outbound,
                                  const std::vector<std::vector<int>>* index_lists, int rank,
                                  int world_size, StringExchangePlan* plan) {
  plan->send_counts.assign(world_size, 0);
  plan->recv_counts.assign(world_size, 0);
  plan->send_displs.assign(world_size, 0);
  plan->recv_displs.assign(world_size, 0);
  plan->send_bytes_counts.assign(world_size, 0);
  plan->recv_bytes_counts.assign(world_size, 0);
  plan->send_bytes_displs.assign(world_size, 0);
  plan->recv_bytes_displs.assign(world_size, 0);

  for (int target = 0; target < world_size; ++target) {
    if (target == rank)
      continue;
    plan->send_counts[target] = static_cast<int>(outbound[target].size());
  }

  MPI_Alltoall(plan->send_counts.data(), 1, MPI_INT, plan->recv_counts.data(), 1, MPI_INT,
               MPI_COMM_WORLD);

  int total_send_words = 0;
  int total_recv_words = 0;
  for (int target = 0; target < world_size; ++target) {
    total_send_words += plan->send_counts[target];
    total_recv_words += plan->recv_counts[target];
  }

  plan->send_lengths.assign(total_send_words, 0);
  plan->recv_lengths.assign(total_recv_words, 0);
  plan->send_indices.assign(total_send_words, 0);
  plan->recv_indices.assign(total_recv_words, 0);

  ComputeDisplacements(plan->send_counts, &plan->send_displs);
  ComputeDisplacements(plan->recv_counts, &plan->recv_displs);

  for (int target = 0; target < world_size; ++target) {
    int offset = plan->send_displs[target];
    for (int j = 0; j < plan->send_counts[target]; ++j) {
      plan->send_lengths[offset + j] = static_cast<int>(outbound[target][j].size());
      if (index_lists) {
        plan->send_indices[offset + j] = (*index_lists)[target][j];
      }
    }
  }

  int* send_lengths_ptr = total_send_words ? plan->send_lengths.data() : nullptr;
  int* recv_lengths_ptr = total_recv_words ? plan->recv_lengths.data() : nullptr;
  MPI_Alltoallv(send_lengths_ptr, plan->send_counts.data(), plan->send_displs.data(), MPI_INT,
                recv_lengths_ptr, plan->recv_counts.data(), plan->recv_displs.data(), MPI_INT,
                MPI_COMM_WORLD);

  if (index_lists) {
    int* send_indices_ptr = total_send_words ? plan->send_indices.data() : nullptr;
    int* recv_indices_ptr = total_recv_words ? plan->recv_indices.data() : nullptr;
    MPI_Alltoallv(send_indices_ptr, plan->send_counts.data(), plan->send_displs.data(), MPI_INT,
                  recv_indices_ptr, plan->recv_counts.data(), plan->recv_displs.data(), MPI_INT,
                  MPI_COMM_WORLD);
  }

  int total_send_bytes = 0;
  int total_recv_bytes = 0;
  for (int target = 0; target < world_size; ++target) {
    int bytes = 0;
    int offset = plan->send_displs[target];
    for (int j = 0; j < plan->send_counts[target]; ++j) {
      bytes += plan->send_lengths[offset + j];
    }
    plan->send_bytes_counts[target] = bytes;
    total_send_bytes += bytes;
  }
  for (int source = 0; source < world_size; ++source) {
    int bytes = 0;
    int offset = plan->recv_displs[source];
    for (int j = 0; j < plan->recv_counts[source]; ++j) {
      bytes += plan->recv_lengths[offset + j];
    }
    plan->recv_bytes_counts[source] = bytes;
    total_recv_bytes += bytes;
  }

  ComputeDisplacements(plan->send_bytes_counts, &plan->send_bytes_displs);
  ComputeDisplacements(plan->recv_bytes_counts, &plan->recv_bytes_displs);

  plan->send_bytes.assign(total_send_bytes, 0);
  plan->recv_bytes.assign(total_recv_bytes, 0);

  for (int target = 0; target < world_size; ++target) {
    int byte_offset = plan->send_bytes_displs[target];
    for (int j = 0; j < plan->send_counts[target]; ++j) {
      const std::string& word = outbound[target][j];
      std::memcpy(plan->send_bytes.data() + byte_offset, word.data(), word.size());
      byte_offset += static_cast<int>(word.size());
    }
  }
}

inline void ExecuteStringExchange(StringExchangePlan* plan) {
  char* send_bytes_ptr = plan->send_bytes.empty() ? nullptr : plan->send_bytes.data();
  char* recv_bytes_ptr = plan->recv_bytes.empty() ? nullptr : plan->recv_bytes.data();

  MPI_Alltoallv(send_bytes_ptr, plan->send_bytes_counts.data(), plan->send_bytes_displs.data(),
                MPI_CHAR, recv_bytes_ptr, plan->recv_bytes_counts.data(),
                plan->recv_bytes_displs.data(), MPI_CHAR, MPI_COMM_WORLD);
}

inline void UnpackStringExchange(const StringExchangePlan& plan,
                                 std::vector<std::vector<std::string>>* inbound) {
  inbound->assign(plan.recv_counts.size(), {});
  for (size_t source = 0; source < plan.recv_counts.size(); ++source) {
    int count = plan.recv_counts[source];
    int length_offset = plan.recv_displs[source];
    int byte_offset = plan.recv_bytes_displs[source];
    auto& bucket = (*inbound)[source];
    bucket.reserve(count);
    for (int j = 0; j < count; ++j) {
      int len = plan.recv_lengths[length_offset + j];
      bucket.emplace_back(plan.recv_bytes.data() + byte_offset,
                          plan.recv_bytes.data() + byte_offset + len);
      byte_offset += len;
    }
  }
}

#endif  // MPI_STRING_EXCHANGE_H
