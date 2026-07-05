#pragma once
#include <csignal>
#include <cstdint>
#include <vector>

#include "counter/Component.h"

namespace gpmc {

class ComponentCache {
public:
    void init(size_t max_mb, int init_pow2);

    // External interruption request, shared with Counter; consulted in the
    // O(N) eviction sweep so SIGTERM can break out of a long collection.
    void set_stop_flag(volatile std::sig_atomic_t* flag) { stop_ = flag; }

    // lookup and insertion
    CacheEntryID lookup(const PackedComponent& pc);
    CacheEntryID newEntry(PackedComponent&& pc);
    CacheEntryID newRootEntry();
    void store(CacheEntryID id, SS count);
    void storeZero(CacheEntryID id);

    // entry access
    CachedBucket&       entry(CacheEntryID id)       { return entries_[id]; }
    const CachedBucket& entry(CacheEntryID id) const { return entries_[id]; }
    bool hasEntry(CacheEntryID id) const {
        return id < entries_.size() && !entries_[id].isFree();
    }
    void setDeletePermitted(CacheEntryID id, bool b) {
        entries_[id].setDeletePermitted(b);
    }

    // descendants tree and garbage collection
    void addDescendant(CacheEntryID parent, CacheEntryID child);
    void cleanDescendants(CacheEntryID id, int n);
    void evictIfNeeded();

    void printStats() const;

private:
    // hash table: slot -> head CacheEntryID, collisions chained via nextInBucket
    std::vector<CacheEntryID> table_;
    uint64_t                  mask_ = 0;

    // entry storage: CacheEntryID -> CachedBucket
    std::vector<CachedBucket> entries_;
    std::vector<CacheEntryID> free_slots_;
    CacheEntryID              root_id_ = INVALID_ENTRY;

    // external interruption flag (owned by Counter), may be null
    volatile std::sig_atomic_t* stop_ = nullptr;

    // memory accounting and access clock
    size_t   total_memory_  = 0;
    size_t   max_memory_    = 0;
    uint64_t current_time_  = 1;
    size_t   num_confirmed_ = 0;

    // statistics (declared in printStats() order)
    uint64_t num_lookups_          = 0;
    uint64_t num_hits_             = 0;
    uint64_t num_zero_stored_      = 0;
    uint64_t num_pollution_cleans_ = 0;
    uint64_t num_polluted_         = 0;
    uint64_t num_grows_            = 0;
    uint64_t num_evict_calls_      = 0;
    uint64_t num_evicted_          = 0;

    CacheEntryID allocEntry();
    void freeEntry(CacheEntryID id);
    void linkIntoTable(CacheEntryID id);
    void removeFromTable(CacheEntryID id);
    void removeFromDescendantsTree(CacheEntryID id);
    void cleanPollutions(CacheEntryID id);
    void growTableIfNeeded();
};

}
