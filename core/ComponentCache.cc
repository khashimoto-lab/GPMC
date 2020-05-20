#include <iostream>
#include <gmpxx.h>
#include "core/Component.h"
#include "utils/Options.h"

using namespace GPMC;
using namespace std;

unsigned PackedComponent::_bits_per_clause = 0;
unsigned PackedComponent::_bits_per_variable = 0;
unsigned PackedComponent::_variable_mask = 0;
unsigned PackedComponent::_clause_mask = 0;

void ComponentCache::init(int cachesize) {
	table_.clear();
	entry_base_.clear();
	entry_base_.reserve(2000000);
	entry_base_.push_back(new CachedComponent()); // dummy Element
	table_.resize(900001, NULL);
	free_entry_base_slots_.clear();
	free_entry_base_slots_.reserve(10000);

	maximum_cache_size_bytes = (int64_t) cachesize * 1048576;

#ifdef DEBUG
	cout << "Cache size:\t"
			<< maximum_cache_size_bytes / 1048576 << " MB" << endl
			<< endl;
#endif
	recompute_bytes_memory_usage();
}

CacheEntryID ComponentCache::createEntryFor(Component &comp, unsigned stack_id) {
	my_time_++;
	CacheEntryID id;

	if (cache_bytes_memory_usage_ >= maximum_cache_size_bytes) {
		deleteEntries();
		num_cache_reduce_++;
	}

	assert(cache_bytes_memory_usage_ < maximum_cache_size_bytes);
	if (free_entry_base_slots_.empty()) {
		if (entry_base_.capacity() == entry_base_.size()) {
			entry_base_.reserve(2 * entry_base_.size());
		}
		entry_base_.push_back(new CachedComponent(comp));
		id = entry_base_.size() - 1;
	} else {
		id = free_entry_base_slots_.back();
		assert(id < entry_base_.size());
		assert(entry_base_[id] == nullptr);
		free_entry_base_slots_.pop_back();
		entry_base_[id] = new CachedComponent(comp);
	}
	entry_base_[id]->setComponentStackID(stack_id);
	entry_base_[id]->set_creation_time(my_time_);

	assert(entry_base_[id]->first_descendant() == 0);
	assert(entry_base_[id]->next_sibling() == 0);
	cache_bytes_memory_usage_ += entry_base_[id]->SizeInBytes();
	sum_size_cached_components_ += entry_base_[id]->num_variables();
	num_cached_components_++;

	return id;
}

#if 0
void ComponentCache::test_descendantstree_consistency() {
	for (unsigned id = 2; id < entry_base_.size(); id++)
		if (entry_base_[id] != nullptr) {
			CacheEntryID act_child = entry(id).first_descendant();
			while (act_child) {
				CacheEntryID next_child = entry(act_child).next_sibling();
				assert(entry(act_child).father() == id);

				act_child = next_child;
			}
			CacheEntryID father = entry(id).father();
			CacheEntryID act_sib = entry(father).first_descendant();
#ifndef NDEBUG
			bool found = false;
#endif
			while (act_sib) {
				CacheEntryID next_sib = entry(act_sib).next_sibling();
#ifndef NDEBUG
				if (act_sib == id)
					found = true;
#endif
				act_sib = next_sib;
			}
			assert(found);
		}
}
#endif

uint64_t ComponentCache::recompute_bytes_memory_usage() {
	cache_bytes_memory_usage_ = sizeof(ComponentCache) + sizeof(CacheBucket *) * table_.capacity();
	for (auto pbucket : table_)
		if (pbucket != nullptr)
			cache_bytes_memory_usage_ += pbucket->getBytesMemoryUsage();
	for (auto pentry : entry_base_)
		if (pentry != nullptr)
			cache_bytes_memory_usage_ += pentry->SizeInBytes();

	return cache_bytes_memory_usage_;
}

bool ComponentCache::requestValueOf(Component &comp, mpz_class &rn) {
	CachedComponent &packedcomp = entry(comp.id());

	unsigned int v = clip(packedcomp.hashkey());
	if (!isBucketAt(v))
		return false;

	CachedComponent *pcomp;
	num_cache_look_ups_++;

	for (auto it = table_[v]->begin(); it != table_[v]->end(); it++) {
		pcomp = &entry(*it);
		if (packedcomp.hashkey() == pcomp->hashkey()
				&& pcomp->equals(packedcomp)) {
			num_cache_hits_++;

			sum_cache_hit_sizes_ += pcomp->num_variables();
			rn = pcomp->model_count();
			// pcomp->set_creation_time(my_time_);
			return true;
		}
	}
	return false;
}

bool ComponentCache::deleteEntries() {
	assert(cache_bytes_memory_usage_ >= maximum_cache_size_bytes);

	vector<double> scores;
	for (auto it = entry_base_.begin() + 1; it != entry_base_.end(); it++)
		if (*it != nullptr && (*it)->deletable()) {
			scores.push_back((double) (*it)->creation_time());
		}
	sort(scores.begin(), scores.end());
	double cutoff = scores[scores.size() / 2];

	//cout << "cutoff" << cutoff  << " entries: "<< entry_base_.size()<< endl;

	// first : go through the EntryBase and mark the entries to be deleted as deleted (i.e. EMPTY
	// note we start at index 2,
	// since index 1 is the whole formula,
	// should always stay here!
	for (unsigned id = 2; id < entry_base_.size(); id++)
		if (entry_base_[id] != nullptr && entry_base_[id]->deletable()) {
			double as = (double) entry_base_[id]->creation_time();
			if (as <= cutoff) {
				removeFromDescendantsTree(id);
				eraseEntry(id);
			}
		}
	// then go through the Hash Table and erase all Links to empty entries
	for (auto pbucket : table_)
		if (pbucket != nullptr) {
			for (auto bt = pbucket->rbegin(); bt != pbucket->rend(); bt++) {
				if (entry_base_[*bt] == nullptr) {
					*bt = pbucket->back();
					pbucket->pop_back();
				}
			}
		}
#if 0
	test_descendantstree_consistency();
#endif

	sum_size_cached_components_ = 0;
	for (unsigned id = 2; id < entry_base_.size(); id++)
		if (entry_base_[id] != nullptr) {
			sum_size_cached_components_ += entry_base_[id]->num_variables();
		}

	num_cached_components_ = entry_base_.size();
	cache_bytes_memory_usage_ = recompute_bytes_memory_usage();
#ifdef DEBUG
	cout << "Cache reduced to size " << recompute_bytes_memory_usage() / 1000000 << "MB" << endl;
	//cout << " \t entries: "<< entry_base_.size() - free_entry_base_slots_.size()<< endl;
#endif
	return true;
}

void ComponentCache::deleteallentries(){
	for (unsigned id = 2; id < entry_base_.size(); id++)
		if (entry_base_[id] != nullptr) {
			removeFromDescendantsTree(id);
			eraseEntry(id);
		}

	for (auto pbucket : table_)
		if (pbucket != nullptr) {
			for (auto bt = pbucket->rbegin(); bt != pbucket->rend(); bt++) {
				if (entry_base_[*bt] == nullptr) {
					*bt = pbucket->back();
					pbucket->pop_back();
				}
			}
		}

	sum_size_cached_components_ = 0;
	num_cached_components_ = entry_base_.size();
	cache_bytes_memory_usage_ = recompute_bytes_memory_usage();
}

void PackedComponent::adjustPackSize(unsigned int maxVarId, unsigned int maxClId) {
	_bits_per_variable = (unsigned int) ceil(log((double) maxVarId + 1) / log(2.0));
	_bits_per_clause = (unsigned int) ceil(log((double) maxClId + 1) / log(2.0));

	_variable_mask = _clause_mask = 0;
	for (unsigned int i = 0; i < _bits_per_variable; i++)
		_variable_mask = (_variable_mask << 1) + 1;
	for (unsigned int i = 0; i < _bits_per_clause; i++)
		_clause_mask = (_clause_mask << 1) + 1;
}
