#pragma once

#include <cstdint>
#include <vector>

namespace gpmc {

static constexpr size_t kBitsetChunkBits = 64;

// A fixed-size bitset backed by 64-bit chunks.
class Bitset {
public:
    explicit Bitset(size_t size)
        : data_((size + kBitsetChunkBits - 1) / kBitsetChunkBits, 0) {}

    bool Get(size_t i) const {
        return data_[i/kBitsetChunkBits] & (uint64_t(1) << (i % kBitsetChunkBits));
    }
    void SetTrue(size_t i) { data_[i/kBitsetChunkBits] |= (uint64_t(1) << (i % kBitsetChunkBits)); }

private:
    std::vector<uint64_t> data_;
};

}
