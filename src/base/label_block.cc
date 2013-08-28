/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/label_block.h"

#include <cassert>

using namespace std;
using namespace tbb;

LabelBlockManager::LabelBlockManager() {
    refcount_ = 0;
}

LabelBlockManager::~LabelBlockManager() {
    assert(blocks_.size() == 0);
}

LabelBlockPtr LabelBlockManager::LocateBlock(uint32_t first, uint32_t last) {
    mutex::scoped_lock lock(mutex_);

    for (LabelBlockList::iterator it = blocks_.begin();
         it != blocks_.end(); ++it) {
        LabelBlock *block = *it;
        if (block->first_ == first && block->last_ == last) {
            return LabelBlockPtr(block);
        }
    }

    LabelBlock *block = new LabelBlock(this, first, last);
    blocks_.push_back(block);
    return LabelBlockPtr(block);
}

void LabelBlockManager::RemoveBlock(LabelBlock *block) {
    for (LabelBlockList::iterator it = blocks_.begin();
         it != blocks_.end(); ++it) {
        if (*it == block) {
            blocks_.erase(it);
            return;
        }
    }
    assert(false);
}

size_t LabelBlockManager::size() {
    mutex::scoped_lock lock(mutex_);
    return blocks_.size();
}

LabelBlock::LabelBlock(uint32_t first, uint32_t last)
    : block_manager_(NULL),
      first_(first),
      last_(last),
      prev_pos_(BitSet::npos) {
      refcount_ = 0;
}

LabelBlock::LabelBlock(
        LabelBlockManager *block_manager, uint32_t first, uint32_t last)
    : block_manager_(block_manager),
      first_(first),
      last_(last),
      prev_pos_(BitSet::npos) {
      refcount_ = 0;
}

LabelBlock::~LabelBlock() {
    assert(used_bitset_.empty());
    if (block_manager_)
        block_manager_->RemoveBlock(this);
}

uint32_t LabelBlock::AllocateLabel() {
    mutex::scoped_lock lock(mutex_);

    size_t pos;
    for (int idx = 0; idx < 2; prev_pos_ = BitSet::npos, idx++) {
        if (prev_pos_ == BitSet::npos) {
            pos = used_bitset_.find_first_clear();
        } else {
            pos = used_bitset_.find_next_clear(prev_pos_);
        }

        if (first_ + pos <= last_) {
            used_bitset_.set(pos);
            prev_pos_ = pos;
            return (first_ + pos);
        }
    }

    return 0;
}

void LabelBlock::ReleaseLabel(uint32_t value) {
    mutex::scoped_lock lock(mutex_);

    assert(value >= first_ && value <= last_);
    size_t pos = value - first_;
    used_bitset_.reset(pos);
}
