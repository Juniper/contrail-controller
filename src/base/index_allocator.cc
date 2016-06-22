/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */
#include "base/index_allocator.h"
#include "base/util.h"

size_t IndexAllocator::AllocIndex() {
    size_t index = BitSet::npos;
    if (last_index_ == BitSet::npos) {
        index = bitset_.find_first_clear();
    } else {
        index = bitset_.find_next_clear(last_index_);
        if (index > max_index_) {
            index = bitset_.find_first_clear();
        }
    }

    if (index > max_index_) index = BitSet::npos;
    if (index != BitSet::npos) {
        bitset_.set(index);
    }
    last_index_ = index;
    return index;
}

void IndexAllocator::FreeIndex(size_t index) {
    assert(index <= max_index_);
    bitset_.reset(index);
}
