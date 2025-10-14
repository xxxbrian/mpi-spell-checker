#ifndef MPI_BLOOM_H
#define MPI_BLOOM_H

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

class BloomFilter {
 public:
  BloomFilter() = default;
  BloomFilter(size_t bit_count, size_t hash_count) {
    Init(bit_count, hash_count);
  }

  void Add(const std::string& key) {
    if (bit_count_ == 0 || hash_count_ == 0) return;
    std::hash<std::string_view> hasher;
    std::string_view key_view(key);
    size_t h1 = hasher(key_view);
    size_t h2 = MixHash(h1, key.size());
    if (h2 == 0) {
      h2 = kMixConstant;
    }
    h2 |= 1ULL;

    for (size_t i = 0; i < hash_count_; ++i) {
      size_t combined = (h1 + i * h2 + i * i) % bit_count_;
      bits_[combined >> 3] |= static_cast<uint8_t>(1U << (combined & 7));
    }
  }

  bool PossiblyContains(const std::string& key) const {
    if (bit_count_ == 0 || hash_count_ == 0) return false;
    std::hash<std::string_view> hasher;
    std::string_view key_view(key);
    size_t h1 = hasher(key_view);
    size_t h2 = MixHash(h1, key.size());
    if (h2 == 0) {
      h2 = kMixConstant;
    }
    h2 |= 1ULL;

    for (size_t i = 0; i < hash_count_; ++i) {
      size_t combined = (h1 + i * h2 + i * i) % bit_count_;
      uint8_t mask = static_cast<uint8_t>(1U << (combined & 7));
      if ((bits_[combined >> 3] & mask) == 0) {
        return false;
      }
    }
    return true;
  }

  const std::vector<uint8_t>& Bits() const { return bits_; }
  size_t BitCount() const { return bit_count_; }
  size_t HashCount() const { return hash_count_; }
  size_t ByteSize() const { return bits_.size(); }

  static bool MaybeContainsRaw(const uint8_t* data, size_t bit_count,
                               size_t hash_count, const std::string& key) {
    if (!data || bit_count == 0 || hash_count == 0) return false;
    std::hash<std::string_view> hasher;
    std::string_view key_view(key);
    size_t h1 = hasher(key_view);
    size_t h2 = MixHash(h1, key.size());
    if (h2 == 0) {
      h2 = kMixConstant;
    }
    h2 |= 1ULL;

    for (size_t i = 0; i < hash_count; ++i) {
      size_t combined = (h1 + i * h2 + i * i) % bit_count;
      uint8_t mask = static_cast<uint8_t>(1U << (combined & 7));
      if ((data[combined >> 3] & mask) == 0) {
        return false;
      }
    }
    return true;
  }

 private:
  void Init(size_t bit_count, size_t hash_count) {
    bit_count_ = std::max<size_t>(bit_count, 8);
    hash_count_ = std::max<size_t>(hash_count, 1);
    size_t byte_count = (bit_count_ + 7) / 8;
    bits_.assign(byte_count, 0);
    bit_count_ = byte_count * 8;
  }

  static size_t MixHash(size_t value, size_t seed) {
    value ^= seed + kMixConstant + (value << 6) + (value >> 2);
    return value;
  }

  static constexpr size_t kMixConstant = 0x9e3779b97f4a7c15ULL;

  size_t bit_count_ = 0;
  size_t hash_count_ = 0;
  std::vector<uint8_t> bits_;
};

#endif  // MPI_BLOOM_H
