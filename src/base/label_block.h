/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_label_block_h
#define ctrlplane_label_block_h

#include <vector>
#include <boost/intrusive_ptr.hpp>
#include <tbb/mutex.h>

#include "base/bitset.h"

class LabelBlock;
class LabelBlockManager;

typedef boost::intrusive_ptr<LabelBlockManager> LabelBlockManagerPtr;
typedef boost::intrusive_ptr<LabelBlock> LabelBlockPtr;

//
// This class represents the manager for a label space. The label space could
// be for the local node or for a remote node.  The manager maintains a list
// of LabelBlocks from which labels can be allocated.
//
// Clients locate LabelBLocks by specifying the first and last label values for
// a block. A new LabelBlock is created or the refcount for an existing one is
// updated as appropriate.  Note that we always return an intrusive pointer to
// LabelBlock, so that the removal of the LabelBlock happens automatically.
//
class LabelBlockManager {
public:
    LabelBlockManager();
    ~LabelBlockManager();
    LabelBlockPtr LocateBlock(uint32_t first, uint32_t last);
    void RemoveBlock(LabelBlock *block);
    tbb::mutex &mutex() { return mutex_; }

private:
    friend class LabelBlockTest;
    friend void intrusive_ptr_add_ref(LabelBlockManager *block_manager);
    friend void intrusive_ptr_release(LabelBlockManager *block_manager);

    typedef std::vector<LabelBlock *> LabelBlockList;

    size_t size();

    tbb::atomic<int> refcount_;

    // The vector of LabelBlocks is protected via the mutex_. This is needed
    // because we need to handle concurrent calls to LocateBlock/RemoveBLock.
    tbb::mutex mutex_;
    LabelBlockList blocks_;
};

inline void intrusive_ptr_add_ref(LabelBlockManager *block_manager) {
    block_manager->refcount_.fetch_and_increment();
}
inline void intrusive_ptr_release(LabelBlockManager *block_manager) {
    int prev = block_manager->refcount_.fetch_and_decrement();
    if (prev == 1) {
        delete block_manager;
    }
}

//
// This class represents a block of labels within a label space. Clients can
// make requests to allocate/release a single label from within this block.
// As mentioned above, clients always maintain an intrusive pointer to these
// objects.
//
// A BitSet is used to keep track of used/allocated values. A position in the
// BitSet represents an offset from the first value e.g. label value of first
// corresponds to bit position 0.
//
// TBD: A BitSet is not time efficient when managing a large label space so
// we should revisit the implementation of this class. Perhaps we could use
// an itable or a hierarchy of BitSets.
//
class LabelBlock {
public:
    LabelBlock(uint32_t first, uint32_t last);
    LabelBlock(LabelBlockManager *block_manager, uint32_t first, uint32_t last);
    ~LabelBlock();

    uint32_t AllocateLabel();
    void ReleaseLabel(uint32_t value);
    uint32_t first() { return first_; }
    uint32_t last() { return last_; }
    LabelBlockManagerPtr block_manager() { return block_manager_; }

private:
    friend class LabelBlockManager;
    friend class LabelBlockTest;
    friend void intrusive_ptr_add_ref(LabelBlock *block);
    friend void intrusive_ptr_release(LabelBlock *block);

    LabelBlockManagerPtr block_manager_;
    uint32_t first_, last_;
    size_t prev_pos_;
    tbb::atomic<int> refcount_;

    // The BitSet of used labels is protected via the mutex_. This is needed
    // since we need to handle concurrent calls to AllocateLabel/ReleaseLabel.
    tbb::mutex mutex_;
    BitSet used_bitset_;
};

inline void intrusive_ptr_add_ref(LabelBlock *block) {
    block->refcount_.fetch_and_increment();
}
inline void intrusive_ptr_release(LabelBlock *block) {
    tbb::mutex mutex;

    tbb::mutex::scoped_lock lock(block->block_manager() ?
                                     block->block_manager()->mutex() : mutex);

    int prev = block->refcount_.fetch_and_decrement();
    if (prev == 1) {
        delete block;
    }
}

#endif
