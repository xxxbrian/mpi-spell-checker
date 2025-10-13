#include "sched_mpi.h"
#include <mpi.h>
#include <algorithm>
#include <cstring>
#include <iostream>
#include <map>
#include <unordered_set>
#include <vector>
#include "mpi_candidates.h"
#include "mpi_dict.h"
#include "mpi_utils.h"

Misspelled RunMpi(const char* dict_text, size_t dict_size,
                  const char* word_list_text, size_t word_list_size) {
  int rank = 0, world_size = 1;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  MPI_Comm_size(MPI_COMM_WORLD, &world_size);

  if (rank == 0) {
    std::cout << "[MPI] Starting distributed spell check with " << world_size
              << " ranks" << std::endl;
  }

  // Step 1: Build local dictionary shard from the already-loaded dict_text
  // Since dict_text is already loaded by rank 0, we need to distribute it
  // For simplicity in basic version, all ranks build from provided text
  // (In production, would use MPI-IO from file directly)
  DistributedDict local_dict = BuildDistributedDict(dict_text, dict_size);

  if (rank == 0) {
    std::cout << "[MPI] Local dict shard size (rank 0): " << local_dict.Size()
              << std::endl;
  }

  // Step 2: Build word list (all ranks have the same word list)
  std::vector<std::string> word_list =
      BuildMpiWordList(word_list_text, word_list_size);

  if (rank == 0) {
    std::cout << "[MPI] Word list size: " << word_list.size() << std::endl;
  }

  // Step 3: Global in-dict check using Allreduce
  std::vector<bool> in_dict(word_list.size(), false);
  for (size_t i = 0; i < word_list.size(); ++i) {
    bool local_hit = local_dict.Contains(word_list[i]);
    bool global_hit = false;
    MPI_Allreduce(&local_hit, &global_hit, 1, MPI_C_BOOL, MPI_LOR,
                  MPI_COMM_WORLD);
    in_dict[i] = global_hit;
  }

  // Step 4: Determine which words this rank owns (for candidate generation)
  std::vector<int> owned_word_indices;
  for (size_t i = 0; i < word_list.size(); ++i) {
    if (!in_dict[i] && GetOwnerRank(word_list[i], world_size) == rank) {
      owned_word_indices.push_back(i);
    }
  }

  if (rank == 0) {
    std::cout << "[MPI] Rank 0 owns " << owned_word_indices.size()
              << " misspelled words" << std::endl;
  }

  // Step 5: Generate candidates for owned words
  std::map<int, std::vector<std::string>> word_candidates;  // word_idx -> candidates
  for (int word_idx : owned_word_indices) {
    const std::string& word = word_list[word_idx];
    std::vector<std::string> candidates = GenerateEditDistance1Candidates(word);
    word_candidates[word_idx] = candidates;
  }

  // Step 6: Verify candidates across all shards
  // For basic version: each rank verifies its owned candidates against all shards
  // We'll use a simple all-to-all exchange pattern

  // Prepare send buffers: what candidates to send to each rank
  std::vector<std::vector<std::string>> send_candidates(world_size);
  std::vector<std::vector<int>> send_word_indices(world_size);

  for (const auto& entry : word_candidates) {
    int word_idx = entry.first;
    const std::vector<std::string>& candidates = entry.second;

    // Send all candidates to all ranks for verification (basic version)
    for (int target_rank = 0; target_rank < world_size; ++target_rank) {
      for (const std::string& cand : candidates) {
        send_candidates[target_rank].push_back(cand);
        send_word_indices[target_rank].push_back(word_idx);
      }
    }
  }

  // Exchange candidates using Alltoallv pattern (simplified with Send/Recv)
  // Each rank will verify received candidates and send back hits

  // Collect verified candidates for each word
  std::map<int, std::unordered_set<std::string>> verified_candidates;
  for (int word_idx : owned_word_indices) {
    verified_candidates[word_idx] = std::unordered_set<std::string>();
  }

  // Simple verification: each rank checks its own candidates against own shard
  for (const auto& entry : word_candidates) {
    int word_idx = entry.first;
    for (const std::string& cand : entry.second) {
      if (local_dict.Contains(cand)) {
        verified_candidates[word_idx].insert(cand);
      }
    }
  }

  // Exchange with other ranks using MPI_Sendrecv to avoid deadlock
  for (int target_rank = 0; target_rank < world_size; ++target_rank) {
    if (target_rank == rank) continue;

    int send_count = send_candidates[target_rank].size();
    int recv_count = 0;

    // Step 1: Exchange counts using Sendrecv
    MPI_Sendrecv(&send_count, 1, MPI_INT, target_rank, 0,
                 &recv_count, 1, MPI_INT, target_rank, 0,
                 MPI_COMM_WORLD, MPI_STATUS_IGNORE);

    // Prepare send buffer
    std::string send_buffer;
    if (send_count > 0) {
      for (size_t i = 0; i < send_candidates[target_rank].size(); ++i) {
        send_buffer += send_candidates[target_rank][i] + "\n";
      }
    }

    // Step 2: Exchange candidate strings and word indices
    // First exchange buffer sizes
    int send_buf_size = send_buffer.size();
    int recv_buf_size = 0;
    MPI_Sendrecv(&send_buf_size, 1, MPI_INT, target_rank, 1,
                 &recv_buf_size, 1, MPI_INT, target_rank, 1,
                 MPI_COMM_WORLD, MPI_STATUS_IGNORE);

    // Allocate receive buffers
    char* recv_buffer = nullptr;
    if (recv_buf_size > 0) {
      recv_buffer = new char[recv_buf_size + 1];
    }

    // Exchange candidate strings
    if (send_buf_size > 0 || recv_buf_size > 0) {
      const char* send_ptr = send_buf_size > 0 ? send_buffer.c_str() : nullptr;
      char* recv_ptr = recv_buf_size > 0 ? recv_buffer : nullptr;
      
      int actual_send = send_buf_size > 0 ? send_buf_size : 0;
      int actual_recv = recv_buf_size > 0 ? recv_buf_size : 0;
      
      if (actual_send > 0 && actual_recv > 0) {
        MPI_Sendrecv(send_ptr, actual_send, MPI_CHAR, target_rank, 2,
                     recv_ptr, actual_recv, MPI_CHAR, target_rank, 2,
                     MPI_COMM_WORLD, MPI_STATUS_IGNORE);
      } else if (actual_send > 0) {
        MPI_Send(send_ptr, actual_send, MPI_CHAR, target_rank, 2, MPI_COMM_WORLD);
      } else if (actual_recv > 0) {
        MPI_Recv(recv_ptr, actual_recv, MPI_CHAR, target_rank, 2, 
                 MPI_COMM_WORLD, MPI_STATUS_IGNORE);
      }
    }

    // Exchange word indices
    std::vector<int> recv_word_indices(recv_count);
    if (send_count > 0 || recv_count > 0) {
      const int* send_idx_ptr = send_count > 0 ? send_word_indices[target_rank].data() : nullptr;
      int* recv_idx_ptr = recv_count > 0 ? recv_word_indices.data() : nullptr;
      
      if (send_count > 0 && recv_count > 0) {
        MPI_Sendrecv(send_idx_ptr, send_count, MPI_INT, target_rank, 3,
                     recv_idx_ptr, recv_count, MPI_INT, target_rank, 3,
                     MPI_COMM_WORLD, MPI_STATUS_IGNORE);
      } else if (send_count > 0) {
        MPI_Send(send_idx_ptr, send_count, MPI_INT, target_rank, 3, MPI_COMM_WORLD);
      } else if (recv_count > 0) {
        MPI_Recv(recv_idx_ptr, recv_count, MPI_INT, target_rank, 3,
                 MPI_COMM_WORLD, MPI_STATUS_IGNORE);
      }
    }

    // Step 3: Verify received candidates and prepare hit response
    std::vector<int> hit_indices;
    if (recv_count > 0) {
      recv_buffer[recv_buf_size] = '\0';
      
      // Parse received candidates
      std::vector<std::string> recv_candidates;
      std::string line;
      for (int i = 0; i < recv_buf_size; ++i) {
        if (recv_buffer[i] == '\n') {
          if (!line.empty()) {
            recv_candidates.push_back(line);
            line.clear();
          }
        } else {
          line += recv_buffer[i];
        }
      }

      // Verify against local dictionary
      for (size_t i = 0; i < recv_candidates.size(); ++i) {
        if (local_dict.Contains(recv_candidates[i])) {
          hit_indices.push_back(i);
        }
      }

      delete[] recv_buffer;
    }

    // Step 4: Exchange hit results
    int my_hit_count = hit_indices.size();
    int peer_hit_count = 0;
    MPI_Sendrecv(&my_hit_count, 1, MPI_INT, target_rank, 4,
                 &peer_hit_count, 1, MPI_INT, target_rank, 4,
                 MPI_COMM_WORLD, MPI_STATUS_IGNORE);

    // Exchange hit indices
    std::vector<int> peer_hit_indices(peer_hit_count);
    if (my_hit_count > 0 || peer_hit_count > 0) {
      const int* my_hit_ptr = my_hit_count > 0 ? hit_indices.data() : nullptr;
      int* peer_hit_ptr = peer_hit_count > 0 ? peer_hit_indices.data() : nullptr;
      
      if (my_hit_count > 0 && peer_hit_count > 0) {
        MPI_Sendrecv(my_hit_ptr, my_hit_count, MPI_INT, target_rank, 5,
                     peer_hit_ptr, peer_hit_count, MPI_INT, target_rank, 5,
                     MPI_COMM_WORLD, MPI_STATUS_IGNORE);
      } else if (my_hit_count > 0) {
        MPI_Send(my_hit_ptr, my_hit_count, MPI_INT, target_rank, 5, MPI_COMM_WORLD);
      } else if (peer_hit_count > 0) {
        MPI_Recv(peer_hit_ptr, peer_hit_count, MPI_INT, target_rank, 5,
                 MPI_COMM_WORLD, MPI_STATUS_IGNORE);
      }
    }

    // Step 5: Add verified candidates from peer
    for (int idx : peer_hit_indices) {
      int word_idx = send_word_indices[target_rank][idx];
      const std::string& cand = send_candidates[target_rank][idx];
      verified_candidates[word_idx].insert(cand);
    }
  }

  // Step 7: Build local misspelled results
  Misspelled local_misspelled;
  for (int word_idx : owned_word_indices) {
    Line line;
    line.word = word_list[word_idx];
    line.candidates =
        std::vector<std::string>(verified_candidates[word_idx].begin(),
                                 verified_candidates[word_idx].end());

    // Sort candidates by strcmp
    std::sort(line.candidates.begin(), line.candidates.end(),
              [](const std::string& a, const std::string& b) {
                return std::strcmp(a.c_str(), b.c_str()) < 0;
              });

    local_misspelled.push_back(line);
  }

  // Step 8: Gather all results to rank 0
  Misspelled global_misspelled;

  if (rank == 0) {
    global_misspelled = local_misspelled;

    // Receive from other ranks
    for (int src_rank = 1; src_rank < world_size; ++src_rank) {
      int recv_count = 0;
      MPI_Recv(&recv_count, 1, MPI_INT, src_rank, 5, MPI_COMM_WORLD,
               MPI_STATUS_IGNORE);

      for (int i = 0; i < recv_count; ++i) {
        // Receive word
        int word_len = 0;
        MPI_Recv(&word_len, 1, MPI_INT, src_rank, 6, MPI_COMM_WORLD,
                 MPI_STATUS_IGNORE);
        char* word_buf = new char[word_len + 1];
        MPI_Recv(word_buf, word_len, MPI_CHAR, src_rank, 7, MPI_COMM_WORLD,
                 MPI_STATUS_IGNORE);
        word_buf[word_len] = '\0';

        // Receive candidates count
        int cand_count = 0;
        MPI_Recv(&cand_count, 1, MPI_INT, src_rank, 8, MPI_COMM_WORLD,
                 MPI_STATUS_IGNORE);

        Line line;
        line.word = std::string(word_buf);
        delete[] word_buf;

        for (int j = 0; j < cand_count; ++j) {
          int cand_len = 0;
          MPI_Recv(&cand_len, 1, MPI_INT, src_rank, 9, MPI_COMM_WORLD,
                   MPI_STATUS_IGNORE);
          char* cand_buf = new char[cand_len + 1];
          MPI_Recv(cand_buf, cand_len, MPI_CHAR, src_rank, 10, MPI_COMM_WORLD,
                   MPI_STATUS_IGNORE);
          cand_buf[cand_len] = '\0';
          line.candidates.push_back(std::string(cand_buf));
          delete[] cand_buf;
        }

        global_misspelled.push_back(line);
      }
    }

    // Sort final results
    std::sort(global_misspelled.begin(), global_misspelled.end(),
              [](const Line& a, const Line& b) {
                if (a.candidates.size() == b.candidates.size()) {
                  return std::strcmp(a.word.c_str(), b.word.c_str()) < 0;
                }
                return a.candidates.size() < b.candidates.size();
              });

    std::cout << "[MPI] Total misspelled words: " << global_misspelled.size()
              << std::endl;

  } else {
    // Send local results to rank 0
    int send_count = local_misspelled.size();
    MPI_Send(&send_count, 1, MPI_INT, 0, 5, MPI_COMM_WORLD);

    for (const Line& line : local_misspelled) {
      // Send word
      int word_len = line.word.size();
      MPI_Send(&word_len, 1, MPI_INT, 0, 6, MPI_COMM_WORLD);
      MPI_Send(line.word.c_str(), word_len, MPI_CHAR, 0, 7, MPI_COMM_WORLD);

      // Send candidates
      int cand_count = line.candidates.size();
      MPI_Send(&cand_count, 1, MPI_INT, 0, 8, MPI_COMM_WORLD);

      for (const std::string& cand : line.candidates) {
        int cand_len = cand.size();
        MPI_Send(&cand_len, 1, MPI_INT, 0, 9, MPI_COMM_WORLD);
        MPI_Send(cand.c_str(), cand_len, MPI_CHAR, 0, 10, MPI_COMM_WORLD);
      }
    }
  }

  return global_misspelled;
}
