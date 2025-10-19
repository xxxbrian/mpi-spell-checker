#include "sched_mpi.h"
#include <mpi.h>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <unordered_set>
#include <vector>
#include "mpi_bloom.h"
#include "mpi_candidates.h"
#include "mpi_dict.h"
#include "mpi_string_exchange.h"
#include "mpi_utils.h"

namespace {
enum class MpiTag : int {
  kRequestHeader = 100,
  kRequestLengths = 101,
  kRequestPayload = 102,
  kResponseHeader = 200,
  kResponseIndices = 201,
  kTerminate = 300,
  kGatherSize = 400,
  kGatherData = 401,
};

constexpr int TagValue(MpiTag tag) {
  return static_cast<int>(tag);
}

struct StageTiming {
  const char* name;
  double duration;
};

class ScopedTimer {
 public:
  ScopedTimer(const char* label, std::vector<StageTiming>& sink)
      : label_(label), sink_(sink), start_(MPI_Wtime()) {}

  ~ScopedTimer() { sink_.push_back({label_, MPI_Wtime() - start_}); }

 private:
  const char* label_;
  std::vector<StageTiming>& sink_;
  double start_;
};

struct BloomConfig {
  bool enabled = false;
  size_t bit_count = 0;
  size_t byte_count = 0;
  std::vector<uint8_t> all_bytes;
};

struct BloomStats {
  long long checked = 0;
  long long rejected = 0;
  long long forwarded = 0;
  long long confirmed = 0;
};

struct MembershipResult {
  std::vector<bool> in_dict;
  std::vector<int> owned_indices;
};

struct CandidateStageOutput {
  Misspelled local;
  BloomStats stats;
};

constexpr size_t kBloomHashCount = 3;

// Forward declarations for helper routines defined later in this file.
void DrainIncomingRequests(const DistributedDict& local_dict);
void SendCandidateBatch(int target_rank, int word_idx, const std::vector<std::string>& candidates);
std::vector<int> WaitForResponse(int target_rank, int expected_word_idx,
                                 const DistributedDict& local_dict);
void SendTerminationSignals(int rank, int world_size);
void WaitForTerminations(const DistributedDict& local_dict, int rank, int world_size);
std::vector<char> SerializeMisspelled(const Misspelled& misspelled);
Misspelled DeserializeMisspelled(const std::vector<char>& buffer);

void ReportStageTimings(const std::vector<StageTiming>& timings, int rank, int world_size) {
  for (const auto& timing : timings) {
    double local = timing.duration;
    double sum = 0.0;
    double min = 0.0;
    double max = 0.0;

    MPI_Reduce(&local, rank == 0 ? &sum : nullptr, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
    MPI_Reduce(&local, rank == 0 ? &min : nullptr, 1, MPI_DOUBLE, MPI_MIN, 0, MPI_COMM_WORLD);
    MPI_Reduce(&local, rank == 0 ? &max : nullptr, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);

    if (rank == 0) {
      double avg = sum / static_cast<double>(world_size);
      std::cout << "[MPI][timing] " << timing.name << " avg=" << avg << "s min=" << min
                << "s max=" << max << "s" << std::endl;
    }
  }
}

void ReportBloomMetrics(long long checked, long long rejected, long long forwarded,
                        long long confirmed, int rank, int world_size) {
  long long local[4] = {checked, rejected, forwarded, confirmed};
  long long global[4] = {0, 0, 0, 0};
  MPI_Reduce(local, global, 4, MPI_LONG_LONG, MPI_SUM, 0, MPI_COMM_WORLD);

  if (rank == 0) {
    double reject_ratio =
        global[0] > 0 ? (static_cast<double>(global[1]) / global[0]) * 100.0 : 0.0;
    double confirm_ratio =
        global[2] > 0 ? (static_cast<double>(global[3]) / global[2]) * 100.0 : 0.0;
    std::cout << "[MPI][bloom] checked=" << global[0] << " rejected=" << global[1] << " ("
              << reject_ratio << "%) forwarded=" << global[2] << " confirmed=" << global[3] << " ("
              << confirm_ratio << "%)" << std::endl;
  }
}

DistributedDict BuildDictionaryStage(const char* dict_text, size_t dict_size, int rank,
                                     int world_size, std::vector<StageTiming>& timings) {
  ScopedTimer timer("dict_build", timings);
  DistributedDict dict = BuildDistributedDict(dict_text, dict_size, rank, world_size);
  if (rank == 0) {
    std::cout << "[MPI] Local dict shard size (rank 0): " << dict.Size() << std::endl;
  }
  return dict;
}

BloomConfig SetupBloomStage(bool use_bloom, const DistributedDict& local_dict, int rank,
                            int world_size, std::vector<StageTiming>& timings) {
  BloomConfig config;
  if (!use_bloom) {
    return config;
  }

  ScopedTimer timer("bloom_setup", timings);

  unsigned long long local_word_count = static_cast<unsigned long long>(local_dict.Size());
  unsigned long long global_word_count = 0;
  MPI_Allreduce(&local_word_count, &global_word_count, 1, MPI_UNSIGNED_LONG_LONG, MPI_SUM,
                MPI_COMM_WORLD);

  size_t total_words = static_cast<size_t>(global_word_count);
  size_t bloom_bit_count = std::max<size_t>(total_words * 12, 1024ULL);
  BloomFilter local_bloom(bloom_bit_count, kBloomHashCount);
  bloom_bit_count = local_bloom.BitCount();

  for (const auto& dict_word : local_dict.Words()) {
    std::string normalized;
    const std::string* bloom_key = &dict_word;
    if (NormalizeFirstLower(dict_word, &normalized)) {
      bloom_key = &normalized;
    }
    local_bloom.Add(*bloom_key);
  }

  config.enabled = true;
  config.bit_count = local_bloom.BitCount();
  config.byte_count = local_bloom.ByteSize();
  config.all_bytes.resize(static_cast<size_t>(world_size) * config.byte_count);
  const auto& local_bits = local_bloom.Bits();
  MPI_Allgather(const_cast<uint8_t*>(local_bits.data()), static_cast<int>(config.byte_count),
                MPI_UNSIGNED_CHAR, config.all_bytes.data(), static_cast<int>(config.byte_count),
                MPI_UNSIGNED_CHAR, MPI_COMM_WORLD);

  if (rank == 0) {
    std::cout << "[MPI] Bloom filter enabled with " << config.bit_count << " bits and "
              << kBloomHashCount << " hashes" << std::endl;
  }

  return config;
}

std::vector<std::string> BuildWordListStage(const char* word_list_text, size_t word_list_size,
                                            int rank, std::vector<StageTiming>& timings) {
  ScopedTimer timer("word_list_parse", timings);
  std::vector<std::string> words = BuildMpiWordList(word_list_text, word_list_size);
  if (rank == 0) {
    std::cout << "[MPI] Word list size: " << words.size() << std::endl;
  }
  return words;
}

MembershipResult ComputeMembershipStage(const DistributedDict& local_dict,
                                        const std::vector<std::string>& word_list, int rank,
                                        int world_size, std::vector<StageTiming>& timings) {
  ScopedTimer timer("owner_lookup", timings);
  MembershipResult result;
  result.in_dict.assign(word_list.size(), false);

  if (world_size == 1) {
    for (size_t i = 0; i < word_list.size(); ++i) {
      if (local_dict.Contains(word_list[i])) {
        result.in_dict[i] = true;
      }
    }
  } else {
    std::vector<std::vector<std::string>> outbound(world_size);
    std::vector<std::vector<int>> outbound_indices(world_size);

    for (size_t idx = static_cast<size_t>(rank); idx < word_list.size();
         idx += static_cast<size_t>(world_size)) {
      const std::string& word = word_list[idx];
      int owner = GetOwnerRank(word, world_size);
      if (owner == rank) {
        if (local_dict.Contains(word)) {
          result.in_dict[idx] = true;
        }
      } else {
        outbound[owner].push_back(word);
        outbound_indices[owner].push_back(static_cast<int>(idx));
      }
    }

    StringExchangePlan plan;
    PrepareStringExchange(outbound, &outbound_indices, rank, world_size, &plan);
    ExecuteStringExchange(&plan);

    std::vector<std::vector<std::string>> inbound;
    UnpackStringExchange(plan, &inbound);

    for (int source = 0; source < world_size; ++source) {
      int length_offset = plan.recv_displs[source];
      for (int j = 0; j < plan.recv_counts[source]; ++j) {
        int word_index = plan.recv_indices[length_offset + j];
        const std::string& candidate = inbound[source][j];
        if (local_dict.Contains(candidate)) {
          result.in_dict[word_index] = true;
        }
      }
    }
  }

  for (size_t i = 0; i < word_list.size(); ++i) {
    if (!result.in_dict[i] && GetOwnerRank(word_list[i], world_size) == rank) {
      result.owned_indices.push_back(static_cast<int>(i));
    }
  }

  if (rank == 0) {
    std::cout << "[MPI] Rank 0 owns " << result.owned_indices.size() << " misspelled words"
              << std::endl;
  }

  return result;
}

CandidateStageOutput ProcessCandidatesStage(const std::vector<std::string>& word_list,
                                            const std::vector<int>& owned_indices,
                                            const DistributedDict& local_dict,
                                            const BloomConfig& bloom_config, int rank,
                                            int world_size, std::vector<StageTiming>& timings) {
  ScopedTimer timer("candidate_phase", timings);
  CandidateStageOutput output;

  const std::vector<uint8_t>* bloom_bytes = nullptr;
  if (bloom_config.enabled) {
    bloom_bytes = &bloom_config.all_bytes;
  }

  Misspelled local_misspelled;
  for (int word_idx : owned_indices) {
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
        if (bloom_config.enabled) {
          ++output.stats.checked;
          std::string normalized;
          const std::string* key = &cand;
          if (NormalizeFirstLower(cand, &normalized)) {
            key = &normalized;
          }
          const uint8_t* peer_bits =
              bloom_bytes->data() + static_cast<size_t>(target_rank) * bloom_config.byte_count;
          if (!BloomFilter::MaybeContainsRaw(peer_bits, bloom_config.bit_count, kBloomHashCount,
                                             *key)) {
            ++output.stats.rejected;
            continue;
          }
          ++output.stats.forwarded;
        }
        remote_candidates[target_rank].push_back(cand);
      }
    }

    for (int target_rank = 0; target_rank < world_size; ++target_rank) {
      if (target_rank == rank)
        continue;
      if (remote_candidates[target_rank].empty())
        continue;
      DrainIncomingRequests(local_dict);

      SendCandidateBatch(target_rank, word_idx, remote_candidates[target_rank]);
      std::vector<int> hit_indices = WaitForResponse(target_rank, word_idx, local_dict);
      for (int idx : hit_indices) {
        if (idx >= 0 && idx < static_cast<int>(remote_candidates[target_rank].size())) {
          verified.insert(remote_candidates[target_rank][idx]);
        }
      }
      if (bloom_config.enabled) {
        output.stats.confirmed += static_cast<long long>(hit_indices.size());
      }
    }

    Line line;
    line.word = word_list[word_idx];
    line.candidates.assign(verified.begin(), verified.end());
    local_misspelled.push_back(std::move(line));
  }

  DrainIncomingRequests(local_dict);
  SendTerminationSignals(rank, world_size);
  WaitForTerminations(local_dict, rank, world_size);

  output.local = std::move(local_misspelled);
  return output;
}

Misspelled GatherResultsStage(const Misspelled& local_misspelled, int rank, int world_size,
                              std::vector<StageTiming>& timings) {
  ScopedTimer timer("gather_results", timings);
  if (rank == 0) {
    Misspelled global = local_misspelled;
    for (int src_rank = 1; src_rank < world_size; ++src_rank) {
      unsigned long long payload_size = 0;
      MPI_Recv(&payload_size, 1, MPI_UNSIGNED_LONG_LONG, src_rank, TagValue(MpiTag::kGatherSize),
               MPI_COMM_WORLD, MPI_STATUS_IGNORE);
      if (payload_size > static_cast<unsigned long long>(std::numeric_limits<int>::max())) {
        throw std::runtime_error("Gather payload exceeds MPI send limit");
      }

      std::vector<char> buffer(static_cast<size_t>(payload_size));
      if (payload_size > 0) {
        MPI_Recv(buffer.data(), static_cast<int>(payload_size), MPI_CHAR, src_rank,
                 TagValue(MpiTag::kGatherData), MPI_COMM_WORLD, MPI_STATUS_IGNORE);
      }

      Misspelled remote = DeserializeMisspelled(buffer);
      global.insert(global.end(), std::make_move_iterator(remote.begin()),
                    std::make_move_iterator(remote.end()));
    }

    std::cout << "[MPI] Total misspelled words: " << global.size() << std::endl;
    return global;
  }

  std::vector<char> payload = SerializeMisspelled(local_misspelled);
  unsigned long long payload_size = payload.size();
  if (payload_size > static_cast<unsigned long long>(std::numeric_limits<int>::max())) {
    throw std::runtime_error("Gather payload exceeds MPI send limit");
  }

  MPI_Send(&payload_size, 1, MPI_UNSIGNED_LONG_LONG, 0, TagValue(MpiTag::kGatherSize),
           MPI_COMM_WORLD);
  if (payload_size > 0) {
    MPI_Send(payload.data(), static_cast<int>(payload_size), MPI_CHAR, 0,
             TagValue(MpiTag::kGatherData), MPI_COMM_WORLD);
  }

  return Misspelled{};
}

void AppendUint32(std::vector<char>& buffer, uint32_t value) {
  char encoded[sizeof(uint32_t)];
  std::memcpy(encoded, &value, sizeof(uint32_t));
  buffer.insert(buffer.end(), encoded, encoded + sizeof(uint32_t));
}

uint32_t ReadUint32(const std::vector<char>& buffer, size_t* offset) {
  if (*offset + sizeof(uint32_t) > buffer.size()) {
    throw std::runtime_error("Buffer underflow while decoding uint32");
  }
  uint32_t value = 0;
  std::memcpy(&value, buffer.data() + *offset, sizeof(uint32_t));
  *offset += sizeof(uint32_t);
  return value;
}

std::vector<char> SerializeMisspelled(const Misspelled& misspelled) {
  std::vector<char> buffer;
  buffer.reserve(16 + misspelled.size() * 32);
  AppendUint32(buffer, static_cast<uint32_t>(misspelled.size()));
  for (const Line& line : misspelled) {
    AppendUint32(buffer, static_cast<uint32_t>(line.word.size()));
    buffer.insert(buffer.end(), line.word.begin(), line.word.end());

    AppendUint32(buffer, static_cast<uint32_t>(line.candidates.size()));
    for (const std::string& cand : line.candidates) {
      AppendUint32(buffer, static_cast<uint32_t>(cand.size()));
      buffer.insert(buffer.end(), cand.begin(), cand.end());
    }
  }
  return buffer;
}

Misspelled DeserializeMisspelled(const std::vector<char>& buffer) {
  Misspelled result;
  size_t offset = 0;
  uint32_t line_count = ReadUint32(buffer, &offset);
  result.reserve(line_count);

  for (uint32_t i = 0; i < line_count; ++i) {
    uint32_t word_len = ReadUint32(buffer, &offset);
    if (offset + word_len > buffer.size()) {
      throw std::runtime_error("Buffer underflow while decoding word");
    }
    std::string word(buffer.data() + offset, buffer.data() + offset + word_len);
    offset += word_len;

    uint32_t cand_count = ReadUint32(buffer, &offset);
    Line line;
    line.word = std::move(word);
    line.candidates.reserve(cand_count);
    for (uint32_t j = 0; j < cand_count; ++j) {
      uint32_t cand_len = ReadUint32(buffer, &offset);
      if (offset + cand_len > buffer.size()) {
        throw std::runtime_error("Buffer underflow while decoding candidate");
      }
      line.candidates.emplace_back(buffer.data() + offset, buffer.data() + offset + cand_len);
      offset += cand_len;
    }
    result.push_back(std::move(line));
  }

  if (offset != buffer.size()) {
    throw std::runtime_error("Trailing bytes found in misspelled payload");
  }

  return result;
}

void DrainIncomingRequests(const DistributedDict& local_dict) {
  int flag = 0;
  MPI_Status status;
  MPI_Iprobe(MPI_ANY_SOURCE, TagValue(MpiTag::kRequestHeader), MPI_COMM_WORLD, &flag, &status);
  while (flag) {
    int src = status.MPI_SOURCE;
    int header[2] = {0, 0};
    MPI_Recv(header, 2, MPI_INT, src, TagValue(MpiTag::kRequestHeader), MPI_COMM_WORLD,
             MPI_STATUS_IGNORE);
    int word_idx = header[0];
    int count = header[1];

    std::vector<int> hit_indices;
    std::vector<int> lengths;
    std::string buffer;

    if (count > 0) {
      lengths.resize(count);
      MPI_Recv(lengths.data(), count, MPI_INT, src, TagValue(MpiTag::kRequestLengths),
               MPI_COMM_WORLD, MPI_STATUS_IGNORE);

      size_t total_bytes = 0;
      for (int len : lengths) {
        total_bytes += static_cast<size_t>(len);
      }

      buffer.resize(total_bytes);
      if (total_bytes > 0) {
        MPI_Recv(buffer.data(), static_cast<int>(total_bytes), MPI_CHAR, src,
                 TagValue(MpiTag::kRequestPayload), MPI_COMM_WORLD, MPI_STATUS_IGNORE);
      }

      size_t offset = 0;
      for (int i = 0; i < count; ++i) {
        std::string candidate(buffer.data() + offset, static_cast<size_t>(lengths[i]));
        if (local_dict.Contains(candidate)) {
          hit_indices.push_back(i);
        }
        offset += static_cast<size_t>(lengths[i]);
      }
    }

    int response_header[2] = {word_idx, static_cast<int>(hit_indices.size())};
    MPI_Send(response_header, 2, MPI_INT, src, TagValue(MpiTag::kResponseHeader), MPI_COMM_WORLD);
    if (!hit_indices.empty()) {
      MPI_Send(hit_indices.data(), static_cast<int>(hit_indices.size()), MPI_INT, src,
               TagValue(MpiTag::kResponseIndices), MPI_COMM_WORLD);
    }

    MPI_Iprobe(MPI_ANY_SOURCE, TagValue(MpiTag::kRequestHeader), MPI_COMM_WORLD, &flag, &status);
  }
}

void SendCandidateBatch(int target_rank, int word_idx, const std::vector<std::string>& candidates) {
  int header[2] = {word_idx, static_cast<int>(candidates.size())};
  MPI_Send(header, 2, MPI_INT, target_rank, TagValue(MpiTag::kRequestHeader), MPI_COMM_WORLD);
  if (candidates.empty()) {
    return;
  }

  std::vector<int> lengths(candidates.size());
  size_t total_bytes = 0;
  for (size_t i = 0; i < candidates.size(); ++i) {
    lengths[i] = static_cast<int>(candidates[i].size());
    total_bytes += candidates[i].size();
  }

  MPI_Send(lengths.data(), static_cast<int>(lengths.size()), MPI_INT, target_rank,
           TagValue(MpiTag::kRequestLengths), MPI_COMM_WORLD);

  std::string buffer;
  buffer.reserve(total_bytes);
  for (const auto& cand : candidates) {
    buffer.append(cand);
  }
  if (!buffer.empty()) {
    MPI_Send(buffer.data(), static_cast<int>(buffer.size()), MPI_CHAR, target_rank,
             TagValue(MpiTag::kRequestPayload), MPI_COMM_WORLD);
  }
}

std::vector<int> WaitForResponse(int target_rank, int expected_word_idx,
                                 const DistributedDict& local_dict) {
  MPI_Status status;
  while (true) {
    DrainIncomingRequests(local_dict);
    int flag = 0;
    MPI_Iprobe(target_rank, TagValue(MpiTag::kResponseHeader), MPI_COMM_WORLD, &flag, &status);
    if (flag) {
      int response_header[2] = {0, 0};
      MPI_Recv(response_header, 2, MPI_INT, target_rank, TagValue(MpiTag::kResponseHeader),
               MPI_COMM_WORLD, MPI_STATUS_IGNORE);
      (void)expected_word_idx;
      int hit_count = response_header[1];
      std::vector<int> indices(hit_count);
      if (hit_count > 0) {
        MPI_Recv(indices.data(), hit_count, MPI_INT, target_rank,
                 TagValue(MpiTag::kResponseIndices), MPI_COMM_WORLD, MPI_STATUS_IGNORE);
      }
      return indices;
    }
  }
}

void SendTerminationSignals(int rank, int world_size) {
  for (int peer = 0; peer < world_size; ++peer) {
    if (peer == rank)
      continue;
    MPI_Send(nullptr, 0, MPI_CHAR, peer, TagValue(MpiTag::kTerminate), MPI_COMM_WORLD);
  }
}

void WaitForTerminations(const DistributedDict& local_dict, int rank, int world_size) {
  std::vector<bool> done(world_size, false);
  done[rank] = true;
  int done_count = 0;

  while (done_count < world_size - 1) {
    DrainIncomingRequests(local_dict);
    int flag = 0;
    MPI_Status status;
    MPI_Iprobe(MPI_ANY_SOURCE, TagValue(MpiTag::kTerminate), MPI_COMM_WORLD, &flag, &status);
    if (flag) {
      MPI_Recv(nullptr, 0, MPI_CHAR, status.MPI_SOURCE, TagValue(MpiTag::kTerminate),
               MPI_COMM_WORLD, MPI_STATUS_IGNORE);
      if (!done[status.MPI_SOURCE]) {
        done[status.MPI_SOURCE] = true;
        ++done_count;
      }
    }
  }
}
}  // namespace

Misspelled RunMpi(const char* dict_text, size_t dict_size, const char* word_list_text,
                  size_t word_list_size, bool use_bloom) {
  int rank = 0, world_size = 1;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  MPI_Comm_size(MPI_COMM_WORLD, &world_size);

  if (rank == 0) {
    std::cout << "[MPI] Starting distributed spell check with " << world_size << " ranks"
              << std::endl;
  }

  double overall_start = MPI_Wtime();
  std::vector<StageTiming> timings;
  BloomStats bloom_stats;
  bool bloom_enabled = false;

  DistributedDict local_dict =
      BuildDictionaryStage(dict_text, dict_size, rank, world_size, timings);

  BloomConfig bloom_config = SetupBloomStage(use_bloom, local_dict, rank, world_size, timings);
  bloom_enabled = bloom_config.enabled;

  std::vector<std::string> word_list =
      BuildWordListStage(word_list_text, word_list_size, rank, timings);

  MembershipResult membership =
      ComputeMembershipStage(local_dict, word_list, rank, world_size, timings);
  const std::vector<int>& owned_word_indices = membership.owned_indices;

  CandidateStageOutput candidate_output = ProcessCandidatesStage(
      word_list, owned_word_indices, local_dict, bloom_config, rank, world_size, timings);
  Misspelled local_misspelled = std::move(candidate_output.local);
  bloom_stats = candidate_output.stats;

  Misspelled global_misspelled = GatherResultsStage(local_misspelled, rank, world_size, timings);

  timings.push_back({"total_mpi", MPI_Wtime() - overall_start});

  ReportStageTimings(timings, rank, world_size);
  if (bloom_enabled) {
    ReportBloomMetrics(bloom_stats.checked, bloom_stats.rejected, bloom_stats.forwarded,
                       bloom_stats.confirmed, rank, world_size);
  }

  return global_misspelled;
}
