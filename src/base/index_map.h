/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_index_map_h
#define ctrlplane_index_map_h

#include <cassert>
#include <map>
#include <vector>
#include "base/bitset.h"
#include "base/util.h"

//
// An key, value map associated with an index.
//
template <typename KeyType, typename ValueType,
          typename BitsetType = BitSet>
class IndexMap {
public:
    typedef std::vector<ValueType *> VectorType;
    typedef std::map<KeyType, ValueType *> MapType;
    typedef typename MapType::iterator iterator;

    IndexMap() { }
    ~IndexMap() {
        STLDeleteValues(&values_);
    }

    ValueType *At(int index) const {
        return values_[index];
    }
    ValueType *Find(const KeyType &key) const {
        typename MapType::const_iterator loc = map_.find(key);
        if (loc != map_.end()) {
            return loc->second;
        }
        return NULL;
    }

    // Allocate a new index associated with the new key.
    size_t Insert(const KeyType &key, ValueType *value) {
        std::pair<typename MapType::iterator, bool> result =
            map_.insert(std::make_pair(key, value));
        if (!result.second) {
            return -1;
        }
        size_t bit = bits_.find_first_clear();
        if (bit >= values_.size()) {
            assert(bit == values_.size());
            values_.push_back(value);
        } else {
            values_[bit] = value;
        }
        bits_.set(bit);
        return bit;
    }

    void Remove(const KeyType &key, int index) {
        typename MapType::iterator loc = map_.find(key);
        assert(loc != map_.end());
        assert(loc->second == values_[index]);
        map_.erase(loc);
        ValueType *value = values_[index];
        values_[index] = NULL;
        bits_.reset(index);
        delete value;
        for (ssize_t i = values_.size() - 1; i >= 0; i--) {
            if (values_[i] != NULL) {
                break;
            }
            values_.pop_back();
        }
    }

    ValueType *Locate(const KeyType &key) {
        ValueType *value = Find(key);
        if (value == NULL) {
            value = new ValueType(key);
            value->set_index(Insert(key, value));
        }
        return value;
    }

    size_t size() const { return values_.size(); }
    size_t count() const { return map_.size(); }
    bool empty() const { return map_.empty(); }
    
    void clear() {
        bits_.clear();
        STLDeleteValues(&values_);
        map_.clear();
    }

    const BitsetType &bits() const { return bits_; }

    iterator begin() { return map_.begin(); }
    iterator end() { return map_.end(); }
    iterator lower_bound(const KeyType &key) { return map_.lower_bound(key); }

private:
    BitsetType bits_;
    VectorType values_;
    MapType map_;
    DISALLOW_COPY_AND_ASSIGN(IndexMap);
};

#endif
