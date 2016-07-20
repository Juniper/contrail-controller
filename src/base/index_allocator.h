/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_index_allocator_h
#define ctrlplane_index_allocator_h

#include <assert.h>
#include <inttypes.h>
#include <string>
#include <vector>
#include <base/bitset.h>

class IndexAllocator {
public:
    IndexAllocator(size_t max_index)
        : max_index_(max_index), last_index_(BitSet::npos) { }

    size_t AllocIndex();
    void FreeIndex(size_t index);

private:
    BitSet bitset_;
    size_t max_index_;
    size_t last_index_;
};

#endif
