#pragma once
#include <cassert>
#include <cmath>
#include <cstdint>
#include <vector>

#include "extern/rapidhash.h"

#include "include/gpmc/Semiring.h"
#include "include/gpmc/Types.h"

namespace gpmc {

using CacheEntryID = uint32_t;
constexpr CacheEntryID INVALID_ENTRY = 0;

class Component {
    std::vector<int> data_;
    int nvars_          = 0;
    int nsvars_in_comp_ = 0;
    int ndvars_in_comp_ = 0;
    int ncls_           = 0;
    CacheEntryID id_   = INVALID_ENTRY;

public:
    Component() = default;

    void reserve(int nvars, int ncls) { data_.reserve(nvars + 1 + ncls + 1); }

    void pushVar(Var v)      { data_.push_back(v); }
    void closeVars()         { data_.push_back(VAR_UNDEF); }
    void pushClause(int cid) { data_.push_back(cid); }
    void closeClauses()      { data_.push_back(CL_UNDEF); }

    void setNVars(int n)           { nvars_ = n; }
    void setNSVarsInComp(int n)    { nsvars_in_comp_ = n; }
    void setNDVarsInComp(int n)    { ndvars_in_comp_ = n; }
    void setNCls(int n)            { ncls_ = n; }

    int  operator[](int i) const { return data_[i]; }
    int  nVars()           const { return nvars_; }
    int  nSVarsInComp()    const { return nsvars_in_comp_; }
    int  nDVarsInComp()    const { return ndvars_in_comp_; }
    int  nClauses()        const { return ncls_; }
    bool hasSVar()         const { return nsvars_in_comp_ > 0; }

    CacheEntryID id()              const { return id_; }
    void         setId(CacheEntryID id)  { id_ = id; }
};

class PackedComponent {

    static inline unsigned bits_first_var_ = 1;
    static inline unsigned bits_first_cls_ = 1;

    static constexpr uint64_t ZERO_BIT   = 1ULL << 0;
    static constexpr uint64_t DELETE_BIT = 1ULL << 1;
    static constexpr unsigned TIME_SHIFT = 2;

protected:
    std::vector<uint32_t> data_;
    uint64_t hash_               = 0;

    uint64_t flags_ = 1ULL << TIME_SHIFT;

public:

    static void initBits(int num_vars, int num_long_clauses) {
        auto cbits = [](int n) -> unsigned {
            return n > 1 ? static_cast<unsigned>(std::ceil(std::log2(n))) : 1u;
        };
        bits_first_var_ = cbits(num_vars);
        bits_first_cls_ = cbits(num_long_clauses);
    }

    PackedComponent() = default;
    explicit PackedComponent(const Component& comp);
    PackedComponent(PackedComponent&&) = default;
    PackedComponent& operator=(PackedComponent&&) = default;

    uint64_t hash() const { return hash_; }
    bool equals(const PackedComponent& other) const;

    bool isZero() const { return flags_ & ZERO_BIT; }
    void setZero()      { flags_ |= ZERO_BIT; }

    bool deletePermitted() const { return flags_ & DELETE_BIT; }
    void setDeletePermitted(bool b) {
        if (b) flags_ |=  DELETE_BIT;
        else   flags_ &= ~DELETE_BIT;
    }

    uint64_t lastUsedTime() const { return flags_ >> TIME_SHIFT; }
    void setLastUsedTime(uint64_t t) {
        flags_ = (flags_ & (ZERO_BIT | DELETE_BIT)) | (t << TIME_SHIFT);
    }
};

class CachedBucket : public PackedComponent {
    SS           model_count_;
    CacheEntryID father_           = INVALID_ENTRY;
    CacheEntryID first_descendant_ = INVALID_ENTRY;
    CacheEntryID next_sibling_     = INVALID_ENTRY;
    CacheEntryID next_in_bucket_   = INVALID_ENTRY;
    bool         is_free_          = true;

public:
    CachedBucket() = default;
    explicit CachedBucket(PackedComponent&& pc)  : PackedComponent(std::move(pc)), is_free_(false) {}

    bool isFree() const { return is_free_; }
    void setFree() {
        model_count_.reset();
        data_.clear();
        data_.shrink_to_fit();
        father_ = first_descendant_ = next_sibling_ = next_in_bucket_ = INVALID_ENTRY;
        is_free_ = true;
    }
    void setUsed() { is_free_ = false; }
    void initAs(PackedComponent&& pc) {
        PackedComponent::operator=(std::move(pc));
        is_free_ = false;
    }

    bool hasModelCount() const { return model_count_ != nullptr; }
    bool isConfirmed()   const { return hasModelCount() || isZero(); }
    void setModelCount(SS count) { model_count_ = std::move(count); }
    const Semiring& modelCount() const {
        assert(model_count_);
        return *model_count_;
    }

    CacheEntryID father()          const { return father_; }
    CacheEntryID firstDescendant() const { return first_descendant_; }
    CacheEntryID nextSibling()     const { return next_sibling_; }
    CacheEntryID  nextInBucket()     const { return next_in_bucket_; }
    CacheEntryID& nextInBucket_ref()       { return next_in_bucket_; }
    void setFather(CacheEntryID id)          { father_           = id; }
    void setFirstDescendant(CacheEntryID id) { first_descendant_ = id; }
    void setNextSibling(CacheEntryID id)     { next_sibling_     = id; }
    void setNextInBucket(CacheEntryID id)    { next_in_bucket_   = id; }

    size_t sizeInBytes() const {
        size_t base = sizeof(CachedBucket) + data_.size() * sizeof(uint32_t);
        if (model_count_) base += model_count_->internal_size();
        return base;
    }
};

inline PackedComponent::PackedComponent(const Component& comp) {

    int max_diff = 1, prev = comp[0];
    for (int i = 1; comp[i] != VAR_UNDEF; i++) {
        int diff = comp[i] - prev;
        if (diff > max_diff) max_diff = diff;
        prev = comp[i];
    }
    unsigned bits_var = static_cast<unsigned>(std::ceil(std::log2(max_diff + 1)));

    int cls_start = comp.nVars() + 1;
    unsigned bits_cls = 1;
    if (comp.nClauses() > 1) {
        int max_cdiff = 1;
        prev = comp[cls_start];
        for (int i = cls_start + 1; comp[i] != CL_UNDEF; i++) {
            int diff = comp[i] - prev;
            if (diff > max_cdiff) max_cdiff = diff;
            prev = comp[i];
        }
        bits_cls = static_cast<unsigned>(std::ceil(std::log2(max_cdiff + 1)));
    }

    const unsigned BPB = 32;
    const unsigned WIDTH_HEADER_BITS = 5;  // self-describing bits_var/bits_cls field (<32 fits in 5 bits)
    unsigned total_bits = bits_first_var_
                        + WIDTH_HEADER_BITS + (comp.nVars()    - 1) * bits_var
                        + (comp.nClauses() > 0 ? bits_first_cls_                                      : 0)
                        + (comp.nClauses() > 1 ? WIDTH_HEADER_BITS + (comp.nClauses() - 1) * bits_cls : 0);
    data_.resize(total_bits / BPB + 4, 0);  // +4 words of slack: 2 trailing sentinel + 1 cross-word carry + 1 division remainder

    auto pack = [&](uint32_t*& p, unsigned& bitpos, uint32_t val, unsigned nbits) {
        *p |= val << bitpos;
        bitpos += nbits;
        if (bitpos >= BPB) {
            bitpos -= BPB;
            *(++p) = val >> (nbits - bitpos);
        }
    };

    uint32_t* p = data_.data();
    unsigned  bp = 0;

    pack(p, bp, static_cast<uint32_t>(comp[0]), bits_first_var_);
    pack(p, bp, bits_var, WIDTH_HEADER_BITS);
    prev = comp[0];
    for (int i = 1; comp[i] != VAR_UNDEF; i++) {
        pack(p, bp, comp[i] - prev, bits_var);
        prev = comp[i];
    }
    if (bp > 0) p++;
    bp = 0;

    if (comp.nClauses() > 0) {
        pack(p, bp, static_cast<uint32_t>(comp[cls_start]), bits_first_cls_);
        if (comp.nClauses() > 1) {
            pack(p, bp, bits_cls, WIDTH_HEADER_BITS);
            int prev = comp[cls_start];
            for (int i = cls_start + 1; comp[i] != CL_UNDEF; i++) {
                pack(p, bp, comp[i] - prev, bits_cls);
                prev = comp[i];
            }
        }
        if (bp > 0) p++;
    }

    *p = 0; *(p+1) = 0;
    data_.resize(static_cast<size_t>(p - data_.data()) + 2);

    hash_ = rapidhash(data_.data(), data_.size() * sizeof(uint32_t));
}

inline bool PackedComponent::equals(const PackedComponent& other) const {
    if (data_.size() != other.data_.size()) return false;
    return data_ == other.data_;
}

}
