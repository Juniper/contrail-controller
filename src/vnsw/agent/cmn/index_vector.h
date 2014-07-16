/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef agent_index_vector_h
#define agent_index_vector_h

#include <cassert>
#include <vector>
#include <boost/dynamic_bitset.hpp>
#include <base/logging.h>

// Index management + Vector holding a pointer at allocated index
template <typename EntryType> 
class IndexVector {
public:
    static const size_t kGrowSize = 32;

    typedef std::vector<EntryType *> EntryTable;

    IndexVector() { }
    ~IndexVector() {
        // Make sure the bitmap is empty
        if (bitmap_.count() != bitmap_.size()) {
            LOG(ERROR, "IndexVector has " << bitmap_.size() - bitmap_.count() 
                << " entries in destructor");
        }
        bitmap_.clear();
    }

    // Get entry at an index
    EntryType *At(size_t index) const {
        if (index >= bitmap_.size()) {
            return NULL;
        }
        return entries_[index];
    }

    // Allocate a new index and store entry in vector at allocated index
    size_t Insert(EntryType *entry) {
        size_t index = bitmap_.find_first();
        if (index == bitmap_.npos) {
            size_t size = bitmap_.size();
            bitmap_.resize(size + kGrowSize, 1);
            entries_.resize(size + kGrowSize);
            index = bitmap_.find_first();
        }

        bitmap_.set(index, 0);
        entries_[index] = entry;
        return index;
    }

    void Update(size_t index, EntryType *entry) {
        assert(index < bitmap_.size());
        assert(bitmap_[index] == 0);
        entries_[index] = entry;
    }

    void Remove(size_t index) {
        assert(index < bitmap_.size());
        assert(bitmap_[index] == 0);
        bitmap_.set(index);
        entries_[index] = NULL;
    }

private:
    typedef boost::dynamic_bitset<> Bitmap;
    Bitmap bitmap_;
    EntryTable entries_;

    DISALLOW_COPY_AND_ASSIGN(IndexVector);
};

#endif
