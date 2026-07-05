#include "counter/ComponentCache.h"

#include <algorithm>
#include <cassert>
#include <cstdio>

namespace gpmc {

void ComponentCache::init(size_t max_mb, int init_pow2) {
    max_memory_ = max_mb * 1024ULL * 1024ULL;
    total_memory_ = 0;
    current_time_ = 1;

    size_t initial_size = size_t(1) << init_pow2;
    table_.assign(initial_size, INVALID_ENTRY);
    mask_ = initial_size - 1;

    entries_.clear();
    entries_.emplace_back();
}

CacheEntryID ComponentCache::lookup(const PackedComponent& pc) {
    num_lookups_++;
    CacheEntryID id = table_[pc.hash() & mask_];
    while (id != INVALID_ENTRY) {
        auto& e = entries_[id];
        if (e.hash() == pc.hash()) {
            if (e.equals(pc)) {
                e.setLastUsedTime(current_time_++);
                num_hits_++;
                return id;
            }
        }
        id = e.nextInBucket();
    }
    return INVALID_ENTRY;
}

CacheEntryID ComponentCache::newEntry(PackedComponent&& pc) {
    CacheEntryID id = allocEntry();
    entries_[id].initAs(std::move(pc));
    total_memory_ += entries_[id].sizeInBytes();
    evictIfNeeded();
    return id;
}

CacheEntryID ComponentCache::newRootEntry() {
    CacheEntryID id = allocEntry();
    entries_[id].setUsed();
    entries_[id].setDeletePermitted(false);
    total_memory_ += entries_[id].sizeInBytes();
    root_id_ = id;
    return id;
}

void ComponentCache::store(CacheEntryID id, SS count) {
    assert(!entries_[id].isFree());
    assert(!entries_[id].hasModelCount());

    total_memory_ += count->internal_size();
    entries_[id].setModelCount(std::move(count));
    entries_[id].setLastUsedTime(current_time_++);

    linkIntoTable(id);
}

void ComponentCache::storeZero(CacheEntryID id) {
    assert(!entries_[id].isFree());
    num_zero_stored_++;

    entries_[id].setZero();
    entries_[id].setLastUsedTime(current_time_++);
    entries_[id].setDeletePermitted(true);

    linkIntoTable(id);
}

void ComponentCache::addDescendant(CacheEntryID parent, CacheEntryID child) {
    assert(!entries_[parent].isFree() && !entries_[child].isFree());
    entries_[child].setFather(parent);
    entries_[child].setNextSibling(entries_[parent].firstDescendant());
    entries_[parent].setFirstDescendant(child);
}

void ComponentCache::cleanDescendants(CacheEntryID id, int n) {
    if (entries_[id].isFree() || n <= 0) return;
    num_pollution_cleans_++;

    CacheEntryID child = entries_[id].firstDescendant();
    int remaining = n;
    while (child != INVALID_ENTRY && remaining > 0) {
        CacheEntryID next = entries_[child].nextSibling();
        cleanPollutions(child);
        child = next;
        remaining--;
    }
    entries_[id].setFirstDescendant(child);
}

void ComponentCache::evictIfNeeded() {
    if (total_memory_ <= max_memory_) return;
    num_evict_calls_++;

    std::vector<uint64_t> times;
    times.reserve(entries_.size());
    for (auto& e : entries_)
        if (!e.isFree() && e.deletePermitted() && e.isConfirmed())
            times.push_back(e.lastUsedTime());

    if (times.empty()) return;

    auto mid = times.begin() + times.size() / 2;
    std::nth_element(times.begin(), mid, times.end());
    uint64_t cutoff = *mid;

    for (CacheEntryID id = 1; id < entries_.size(); id++) {
        // Poll the stop flag periodically (O(N) sweep) so SIGTERM can
        // interrupt; entries already removed stay consistent if we bail out.
        if ((id & 0xFFFF) == 0 && stop_ && *stop_) return;
        auto& e = entries_[id];
        if (!e.isFree() && e.deletePermitted() && e.isConfirmed()
                && e.lastUsedTime() <= cutoff) {
            removeFromDescendantsTree(id);
            removeFromTable(id);
            freeEntry(id);
            num_evicted_++;
        }
    }
}

void ComponentCache::printStats() const {
    std::printf("c o   %-22s %lu\n", "cache_lookups",    num_lookups_);
    std::printf("c o   %-22s %lu\n", "cache_hits",       num_hits_);
    std::printf("c o   %-22s %lu\n", "cache_zero_stored",num_zero_stored_);
    std::printf("c o   %-22s %lu\n", "cache_poll_cleans",num_pollution_cleans_);
    std::printf("c o   %-22s %lu\n", "cache_polluted",   num_polluted_);
    std::printf("c o   %-22s %lu\n", "cache_grows",      num_grows_);
    std::printf("c o   %-22s %lu\n", "cache_evict_calls",num_evict_calls_);
    std::printf("c o   %-22s %lu\n", "cache_evictions",  num_evicted_);
}

CacheEntryID ComponentCache::allocEntry() {
    if (!free_slots_.empty()) {
        CacheEntryID id = free_slots_.back();
        free_slots_.pop_back();
        return id;
    }
    entries_.emplace_back();
    return static_cast<CacheEntryID>(entries_.size() - 1);
}

void ComponentCache::freeEntry(CacheEntryID id) {
    assert(!entries_[id].isFree());
    total_memory_ -= entries_[id].sizeInBytes();
    entries_[id].setFree();
    free_slots_.push_back(id);
}

void ComponentCache::linkIntoTable(CacheEntryID id) {
    uint64_t slot = entries_[id].hash() & mask_;
    entries_[id].setNextInBucket(table_[slot]);
    table_[slot] = id;
    num_confirmed_++;
    growTableIfNeeded();
}

void ComponentCache::removeFromTable(CacheEntryID id) {
    assert(!entries_[id].isFree());
    uint64_t slot = entries_[id].hash() & mask_;
    CacheEntryID* cur = &table_[slot];
    while (*cur != INVALID_ENTRY) {
        if (*cur == id) {
            *cur = entries_[id].nextInBucket();
            entries_[id].setNextInBucket(INVALID_ENTRY);
            num_confirmed_--;
            return;
        }
        cur = &entries_[*cur].nextInBucket_ref();
    }
}

void ComponentCache::removeFromDescendantsTree(CacheEntryID id) {
    assert(!entries_[id].isFree());
    CacheEntryID father = entries_[id].father();
    assert(father != INVALID_ENTRY && !entries_[father].isFree());

    if (entries_[father].firstDescendant() == id) {
        entries_[father].setFirstDescendant(entries_[id].nextSibling());
    } else {
        CacheEntryID sib = entries_[father].firstDescendant();
        while (sib != INVALID_ENTRY) {
            CacheEntryID next_sib = entries_[sib].nextSibling();
            if (next_sib == id) {
                entries_[sib].setNextSibling(entries_[id].nextSibling());
                break;
            }
            sib = next_sib;
        }
    }

    CacheEntryID child = entries_[id].firstDescendant();
    entries_[id].setFirstDescendant(INVALID_ENTRY);
    while (child != INVALID_ENTRY) {
        CacheEntryID next_child = entries_[child].nextSibling();
        entries_[child].setFather(father);
        entries_[child].setNextSibling(entries_[father].firstDescendant());
        entries_[father].setFirstDescendant(child);
        child = next_child;
    }
}

void ComponentCache::cleanPollutions(CacheEntryID id) {
    // Iterative (not recursive) to avoid stack overflow; post-order,
    // matching the original recursion.
    std::vector<std::pair<CacheEntryID, bool>> stack{{id, false}};
    while (!stack.empty()) {
        auto [cur, ready_to_finish] = stack.back();
        stack.pop_back();

        if (ready_to_finish) {
            removeFromTable(cur);
            freeEntry(cur);
            num_polluted_++;
            continue;
        }

        assert(!entries_[cur].isFree());
        stack.push_back({cur, true});

        CacheEntryID child = entries_[cur].firstDescendant();
        while (child != INVALID_ENTRY) {
            CacheEntryID next = entries_[child].nextSibling();
            stack.push_back({child, false});
            child = next;
        }
    }
}

void ComponentCache::growTableIfNeeded() {
    if (num_confirmed_ <= table_.size()) return;

    num_grows_++;
    size_t new_size = table_.size() * 2;
    uint64_t new_mask = new_size - 1;
    std::vector<CacheEntryID> new_table(new_size, INVALID_ENTRY);

    for (CacheEntryID id = 1; id < entries_.size(); id++) {
        auto& e = entries_[id];
        if (e.isFree() || !e.isConfirmed()) continue;
        uint64_t slot = e.hash() & new_mask;
        e.setNextInBucket(new_table[slot]);
        new_table[slot] = id;
    }

    table_ = std::move(new_table);
    mask_  = new_mask;
}

}
