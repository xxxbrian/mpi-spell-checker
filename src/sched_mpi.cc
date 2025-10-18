#include "sched_mpi.h"
#include <mpi.h>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <iostream>
#include <unordered_set>
#include <vector>
#include "mpi_candidates.h"
#include "mpi_bloom.h"
#include "mpi_dict.h"
#include "mpi_utils.h"

namespace {
constexpr int TAG_REQ_HEADER = 100;
constexpr int TAG_REQ_LENGTHS = 101;
constexpr int TAG_REQ_PAYLOAD = 102;
constexpr int TAG_RESP_HEADER = 200;
constexpr int TAG_RESP_INDICES = 201;
constexpr int TAG_TERMINATE = 300;

struct StageTiming {
  const char* name;
  double duration;
};

void ReportStageTimings(const std::vector<StageTiming>& timings, int rank,
                        int world_size) {
  for (const auto& timing : timings) {
    double local = timing.duration;
    double sum = 0.0;
    double min = 0.0;
    double max = 0.0;

    MPI_Reduce(&local, rank == 0 ? &sum : nullptr, 1, MPI_DOUBLE, MPI_SUM, 0,
               MPI_COMM_WORLD);
    MPI_Reduce(&local, rank == 0 ? &min : nullptr, 1, MPI_DOUBLE, MPI_MIN, 0,
               MPI_COMM_WORLD);
    MPI_Reduce(&local, rank == 0 ? &max : nullptr, 1, MPI_DOUBLE, MPI_MAX, 0,
               MPI_COMM_WORLD);

    if (rank == 0) {
      double avg = sum / static_cast<double>(world_size);
      std::cout << "[MPI][timing] " << timing.name << " avg=" << avg
                << "s min=" << min << "s max=" << max << "s" << std::endl;
    }
  }
}

void ReportBloomMetrics(long long checked, long long rejected,
                        long long forwarded, long long confirmed, int rank,
                        int world_size) {
  long long local[4] = {checked, rejected, forwarded, confirmed};
  long long global[4] = {0, 0, 0, 0};
  MPI_Reduce(local, global, 4, MPI_LONG_LONG, MPI_SUM, 0, MPI_COMM_WORLD);

  if (rank == 0) {
    double reject_ratio =
        global[0] > 0 ? (static_cast<double>(global[1]) / global[0]) * 100.0 : 0.0;
    double confirm_ratio = global[2] > 0
                               ? (static_cast<double>(global[3]) / global[2]) * 100.0
                               : 0.0;
    std::cout << "[MPI][bloom] checked=" << global[0]
              << " rejected=" << global[1] << " (" << reject_ratio
              << "%) forwarded=" << global[2] << " confirmed=" << global[3]
              << " (" << confirm_ratio << "%)" << std::endl;
  }
}

std::string NormalizeFirstLower(const std::string& word) {
  std::string normalized = word;
  if (!normalized.empty()) {
    normalized[0] =
        static_cast<char>(std::tolower(static_cast<unsigned char>(normalized[0])));
  }
  return normalized;
}

void DrainIncomingRequests(const DistributedDict& local_dict) {
  int flag = 0;
  MPI_Status status;
  MPI_Iprobe(MPI_ANY_SOURCE, TAG_REQ_HEADER, MPI_COMM_WORLD, &flag, &status);
  while (flag) {
    int src = status.MPI_SOURCE;
    int header[2] = {0, 0};
    MPI_Recv(header, 2, MPI_INT, src, TAG_REQ_HEADER, MPI_COMM_WORLD,
             MPI_STATUS_IGNORE);
    int word_idx = header[0];
    int count = header[1];

    std::vector<int> hit_indices;
    std::vector<int> lengths;
    std::string buffer;

    if (count > 0) {
      lengths.resize(count);
      MPI_Recv(lengths.data(), count, MPI_INT, src, TAG_REQ_LENGTHS,
               MPI_COMM_WORLD, MPI_STATUS_IGNORE);

      size_t total_bytes = 0;
      for (int len : lengths) {
        total_bytes += static_cast<size_t>(len);
      }

      buffer.resize(total_bytes);
      if (total_bytes > 0) {
        MPI_Recv(buffer.data(), static_cast<int>(total_bytes), MPI_CHAR, src,
                 TAG_REQ_PAYLOAD, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
      }

      size_t offset = 0;
      for (int i = 0; i < count; ++i) {
        std::string candidate(buffer.data() + offset,
                              static_cast<size_t>(lengths[i]));
        if (local_dict.Contains(candidate)) {
          hit_indices.push_back(i);
        }
        offset += static_cast<size_t>(lengths[i]);
      }
    }

    int response_header[2] = {word_idx, static_cast<int>(hit_indices.size())};
    MPI_Send(response_header, 2, MPI_INT, src, TAG_RESP_HEADER,
             MPI_COMM_WORLD);
    if (!hit_indices.empty()) {
      MPI_Send(hit_indices.data(), static_cast<int>(hit_indices.size()),
               MPI_INT, src, TAG_RESP_INDICES, MPI_COMM_WORLD);
    }

    MPI_Iprobe(MPI_ANY_SOURCE, TAG_REQ_HEADER, MPI_COMM_WORLD, &flag, &status);
  }
}

void SendCandidateBatch(int target_rank, int word_idx,
                        const std::vector<std::string>& candidates) {
  int header[2] = {word_idx, static_cast<int>(candidates.size())};
  MPI_Send(header, 2, MPI_INT, target_rank, TAG_REQ_HEADER, MPI_COMM_WORLD);
  if (candidates.empty()) {
    return;
  }

  std::vector<int> lengths(candidates.size());
  size_t total_bytes = 0;
  for (size_t i = 0; i < candidates.size(); ++i) {
    lengths[i] = static_cast<int>(candidates[i].size());
    total_bytes += candidates[i].size();
  }

  MPI_Send(lengths.data(), static_cast<int>(lengths.size()), MPI_INT,
           target_rank, TAG_REQ_LENGTHS, MPI_COMM_WORLD);

  std::string buffer;
  buffer.reserve(total_bytes);
  for (const auto& cand : candidates) {
    buffer.append(cand);
  }
  if (!buffer.empty()) {
    MPI_Send(buffer.data(), static_cast<int>(buffer.size()), MPI_CHAR,
             target_rank, TAG_REQ_PAYLOAD, MPI_COMM_WORLD);
  }
}

std::vector<int> WaitForResponse(int target_rank, int expected_word_idx,
                                 const DistributedDict& local_dict) {
  MPI_Status status;
  while (true) {
    DrainIncomingRequests(local_dict);
    int flag = 0;
    MPI_Iprobe(target_rank, TAG_RESP_HEADER, MPI_COMM_WORLD, &flag, &status);
    if (flag) {
      int response_header[2] = {0, 0};
      MPI_Recv(response_header, 2, MPI_INT, target_rank, TAG_RESP_HEADER,
               MPI_COMM_WORLD, MPI_STATUS_IGNORE);
      (void)expected_word_idx;
      int hit_count = response_header[1];
      std::vector<int> indices(hit_count);
      if (hit_count > 0) {
        MPI_Recv(indices.data(), hit_count, MPI_INT, target_rank,
                 TAG_RESP_INDICES, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
      }
      return indices;
    }
  }
}

void SendTerminationSignals(int rank, int world_size) {
  for (int peer = 0; peer < world_size; ++peer) {
    if (peer == rank) continue;
    MPI_Send(nullptr, 0, MPI_CHAR, peer, TAG_TERMINATE, MPI_COMM_WORLD);
  }
}

void WaitForTerminations(const DistributedDict& local_dict, int rank,
                         int world_size) {
  std::vector<bool> done(world_size, false);
  done[rank] = true;
  int done_count = 0;

  while (done_count < world_size - 1) {
    DrainIncomingRequests(local_dict);
    int flag = 0;
    MPI_Status status;
    MPI_Iprobe(MPI_ANY_SOURCE, TAG_TERMINATE, MPI_COMM_WORLD, &flag, &status);
    if (flag) {
      MPI_Recv(nullptr, 0, MPI_CHAR, status.MPI_SOURCE, TAG_TERMINATE,
               MPI_COMM_WORLD, MPI_STATUS_IGNORE);
      if (!done[status.MPI_SOURCE]) {
        done[status.MPI_SOURCE] = true;
        ++done_count;
      }
    }
  }
}
}  // namespace

Misspelled RunMpi(const char* dict_text, size_t dict_size,
                  const char* word_list_text, size_t word_list_size,
                  bool use_bloom) {
  int rank = 0, world_size = 1;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  MPI_Comm_size(MPI_COMM_WORLD, &world_size);

  if (rank == 0) {
    std::cout << "[MPI] Starting distributed spell check with " << world_size
              << " ranks" << std::endl;
  }

  double overall_start = MPI_Wtime();
  std::vector<StageTiming> timings;

  double stage_start = MPI_Wtime();

  // Step 1: Build local dictionary shard from the already-loaded dict_text
  // Since dict_text is already loaded by rank 0, we need to distribute it
  // For simplicity in basic version, all ranks build from provided text
  // (In production, would use MPI-IO from file directly)
  DistributedDict local_dict =
      BuildDistributedDict(dict_text, dict_size, rank, world_size);
  timings.push_back({"dict_build", MPI_Wtime() - stage_start});

  if (rank == 0) {
    std::cout << "[MPI] Local dict shard size (rank 0): " << local_dict.Size()
              << std::endl;
  }

  BloomFilter local_bloom;
  std::vector<uint8_t> all_bloom_bytes;
  size_t bloom_bit_count = 0;
  size_t bloom_byte_count = 0;
  const size_t bloom_hash_count = 3;

  double bloom_elapsed = 0.0;
  if (use_bloom) {
    stage_start = MPI_Wtime();
    unsigned long long local_word_count =
        static_cast<unsigned long long>(local_dict.Size());
    unsigned long long global_word_count = 0;
    MPI_Allreduce(&local_word_count, &global_word_count, 1,
                  MPI_UNSIGNED_LONG_LONG, MPI_SUM, MPI_COMM_WORLD);

    size_t total_words = static_cast<size_t>(global_word_count);
    bloom_bit_count = std::max<size_t>(total_words * 12, 1024ULL);
    local_bloom = BloomFilter(bloom_bit_count, bloom_hash_count);
    bloom_bit_count = local_bloom.BitCount();
    for (const auto& dict_word : local_dict.Words()) {
      local_bloom.Add(NormalizeFirstLower(dict_word));
    }

    bloom_byte_count = local_bloom.ByteSize();
    all_bloom_bytes.resize(static_cast<size_t>(world_size) * bloom_byte_count);
    const auto& local_bits = local_bloom.Bits();
    MPI_Allgather(const_cast<uint8_t*>(local_bits.data()),
                  static_cast<int>(bloom_byte_count), MPI_UNSIGNED_CHAR,
                  all_bloom_bytes.data(), static_cast<int>(bloom_byte_count),
                  MPI_UNSIGNED_CHAR, MPI_COMM_WORLD);

    if (rank == 0) {
      std::cout << "[MPI] Bloom filter enabled with " << bloom_bit_count
                << " bits and " << bloom_hash_count << " hashes" << std::endl;
    }
    bloom_elapsed = MPI_Wtime() - stage_start;
  }
  if (use_bloom) {
    timings.push_back({"bloom_setup", bloom_elapsed});
  }

  // Step 2: Build word list (all ranks have the same word list)
  stage_start = MPI_Wtime();
  std::vector<std::string> word_list =
      BuildMpiWordList(word_list_text, word_list_size);
  timings.push_back({"word_list_parse", MPI_Wtime() - stage_start});

  if (rank == 0) {
    std::cout << "[MPI] Word list size: " << word_list.size() << std::endl;
  }

  // Step 3: Global in-dict check using Allreduce
  stage_start = MPI_Wtime();
  std::vector<bool> in_dict(word_list.size(), false);

  if (world_size == 1) {
    for (size_t i = 0; i < word_list.size(); ++i) {
      if (local_dict.Contains(word_list[i])) {
        in_dict[i] = true;
      }
    }
  } else {
    std::vector<std::vector<int>> send_word_indices(world_size);
    std::vector<std::vector<std::string>> send_word_strings(world_size);

    for (size_t idx = static_cast<size_t>(rank); idx < word_list.size();
         idx += static_cast<size_t>(world_size)) {
      const std::string& word = word_list[idx];
      int owner = GetOwnerRank(word, world_size);
      if (owner == rank) {
        if (local_dict.Contains(word)) {
          in_dict[idx] = true;
        }
      } else {
        send_word_indices[owner].push_back(static_cast<int>(idx));
        send_word_strings[owner].push_back(word);
      }
    }

    std::vector<int> send_counts(world_size, 0);
    for (int target = 0; target < world_size; ++target) {
      if (target == rank) continue;
      send_counts[target] = static_cast<int>(send_word_indices[target].size());
    }

    std::vector<int> recv_counts(world_size, 0);
    MPI_Alltoall(send_counts.data(), 1, MPI_INT, recv_counts.data(), 1, MPI_INT,
                 MPI_COMM_WORLD);

    int total_send_words = 0;
    for (int target = 0; target < world_size; ++target) {
      total_send_words += send_counts[target];
    }

    int total_recv_words = 0;
    for (int source = 0; source < world_size; ++source) {
      total_recv_words += recv_counts[source];
    }

    std::vector<int> send_displs(world_size, 0);
    std::vector<int> recv_displs(world_size, 0);
    for (int i = 1; i < world_size; ++i) {
      send_displs[i] = send_displs[i - 1] + send_counts[i - 1];
      recv_displs[i] = recv_displs[i - 1] + recv_counts[i - 1];
    }

    std::vector<int> send_indices_flat(total_send_words);
    std::vector<int> send_lengths(total_send_words);
    for (int target = 0; target < world_size; ++target) {
      int offset = send_displs[target];
      for (int j = 0; j < send_counts[target]; ++j) {
        send_indices_flat[offset + j] = send_word_indices[target][j];
        send_lengths[offset + j] =
            static_cast<int>(send_word_strings[target][j].size());
      }
    }

    std::vector<int> recv_indices(total_recv_words);
    std::vector<int> recv_lengths(total_recv_words);

    int* send_indices_ptr = total_send_words ? send_indices_flat.data() : nullptr;
    int* recv_indices_ptr = total_recv_words ? recv_indices.data() : nullptr;
    MPI_Alltoallv(send_indices_ptr, send_counts.data(), send_displs.data(),
                  MPI_INT, recv_indices_ptr, recv_counts.data(),
                  recv_displs.data(), MPI_INT, MPI_COMM_WORLD);

    int* send_lengths_ptr = total_send_words ? send_lengths.data() : nullptr;
    int* recv_lengths_ptr = total_recv_words ? recv_lengths.data() : nullptr;
    MPI_Alltoallv(send_lengths_ptr, send_counts.data(), send_displs.data(),
                  MPI_INT, recv_lengths_ptr, recv_counts.data(),
                  recv_displs.data(), MPI_INT, MPI_COMM_WORLD);

    std::vector<int> send_bytes_counts(world_size, 0);
    std::vector<int> send_bytes_displs(world_size, 0);
    int total_send_bytes = 0;
    for (int target = 0; target < world_size; ++target) {
      int bytes = 0;
      for (int j = 0; j < send_counts[target]; ++j) {
        bytes += static_cast<int>(send_word_strings[target][j].size());
      }
      send_bytes_counts[target] = bytes;
      if (target > 0) {
        send_bytes_displs[target] =
            send_bytes_displs[target - 1] + send_bytes_counts[target - 1];
      }
      total_send_bytes += bytes;
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

    std::vector<char> send_bytes(total_send_bytes);
    for (int target = 0; target < world_size; ++target) {
      int byte_offset = send_bytes_displs[target];
      for (int j = 0; j < send_counts[target]; ++j) {
        const std::string& word = send_word_strings[target][j];
        std::memcpy(send_bytes.data() + byte_offset, word.data(), word.size());
        byte_offset += static_cast<int>(word.size());
      }
    }

    std::vector<char> recv_bytes(total_recv_bytes);

    char* send_bytes_ptr = total_send_bytes ? send_bytes.data() : nullptr;
    char* recv_bytes_ptr = total_recv_bytes ? recv_bytes.data() : nullptr;
    MPI_Alltoallv(send_bytes_ptr, send_bytes_counts.data(),
                  send_bytes_displs.data(), MPI_CHAR, recv_bytes_ptr,
                  recv_bytes_counts.data(), recv_bytes_displs.data(), MPI_CHAR,
                  MPI_COMM_WORLD);

    for (int source = 0; source < world_size; ++source) {
      int length_offset = recv_displs[source];
      int byte_offset = recv_bytes_displs[source];
      for (int j = 0; j < recv_counts[source]; ++j) {
        int idx = recv_indices[length_offset + j];
        int len = recv_lengths[length_offset + j];
        std::string word(recv_bytes.data() + byte_offset,
                         recv_bytes.data() + byte_offset + len);
        if (local_dict.Contains(word)) {
          in_dict[idx] = true;
        }
        byte_offset += len;
      }
    }
  }
  timings.push_back({"global_in_dict", MPI_Wtime() - stage_start});

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
  stage_start = MPI_Wtime();
  long long bloom_checked = 0;
  long long bloom_rejected = 0;
  long long bloom_forwarded = 0;
  long long bloom_confirmed = 0;

  Misspelled local_misspelled;
  for (int word_idx : owned_word_indices) {
    DrainIncomingRequests(local_dict);

    const std::string& word = word_list[word_idx];
    std::vector<std::string> candidates = GenerateEditDistance1Candidates(word);

    std::unordered_set<std::string> verified;
    std::vector<std::vector<std::string>> remote_candidates(world_size);

    for (const std::string& cand : candidates) {
      DrainIncomingRequests(local_dict);
      int target_rank = GetOwnerRank(cand, world_size);
      if (target_rank == rank) {
        if (local_dict.Contains(cand)) {
          verified.insert(cand);
        }
      } else {
        if (use_bloom) {
          ++bloom_checked;
          const std::string normalized = NormalizeFirstLower(cand);
          const uint8_t* peer_bits =
              all_bloom_bytes.data() +
              static_cast<size_t>(target_rank) * bloom_byte_count;
          if (!BloomFilter::MaybeContainsRaw(peer_bits, bloom_bit_count,
                                             bloom_hash_count, normalized)) {
            ++bloom_rejected;
            continue;
          }
          ++bloom_forwarded;
        }
        remote_candidates[target_rank].push_back(cand);
      }
    }

    for (int target_rank = 0; target_rank < world_size; ++target_rank) {
      if (target_rank == rank) continue;
      if (remote_candidates[target_rank].empty()) continue;
      DrainIncomingRequests(local_dict);

      SendCandidateBatch(target_rank, word_idx, remote_candidates[target_rank]);
      std::vector<int> hit_indices =
          WaitForResponse(target_rank, word_idx, local_dict);
      for (int idx : hit_indices) {
        if (idx >= 0 &&
            idx < static_cast<int>(remote_candidates[target_rank].size())) {
          verified.insert(remote_candidates[target_rank][idx]);
        }
      }
      if (use_bloom) {
        bloom_confirmed += static_cast<long long>(hit_indices.size());
      }
    }

    Line line;
    line.word = word_list[word_idx];
    line.candidates.assign(verified.begin(), verified.end());

    // Sort candidates by strcmp
    std::sort(line.candidates.begin(), line.candidates.end(),
              [](const std::string& a, const std::string& b) {
                return std::strcmp(a.c_str(), b.c_str()) < 0;
              });

    local_misspelled.push_back(line);
  }

  DrainIncomingRequests(local_dict);
  SendTerminationSignals(rank, world_size);
  WaitForTerminations(local_dict, rank, world_size);
  timings.push_back({"candidate_phase", MPI_Wtime() - stage_start});

  stage_start = MPI_Wtime();
  // Step 6: Gather all results to rank 0
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

  timings.push_back({"gather_results", MPI_Wtime() - stage_start});
  timings.push_back({"total_mpi", MPI_Wtime() - overall_start});

  ReportStageTimings(timings, rank, world_size);
  if (use_bloom) {
    ReportBloomMetrics(bloom_checked, bloom_rejected, bloom_forwarded,
                       bloom_confirmed, rank, world_size);
  }

  return global_misspelled;
}
