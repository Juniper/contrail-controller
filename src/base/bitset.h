/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_bitset_h
#define ctrlplane_bitset_h

#include <inttypes.h>
#include <string>
#include <vector>

//
// BitSet automatically resizes the bit set when needed and allows for
// logical operations between bitsets of different sizes.  Implemented
// using a vector of uint64_t as the underlying storage.
//
class BitSet {
public:
    static const size_t npos = static_cast<size_t>(-1);

    BitSet &set(size_t pos);
    BitSet &reset(size_t pos);
    bool test(size_t pos) const;
    void clear();
    bool empty() const;
    bool none() const;
    bool any() const;
    size_t size() const;
    size_t count() const;
    size_t find_first() const;
    size_t find_next(size_t pos) const;
    size_t find_last() const;
    size_t find_first_clear() const;
    size_t find_next_clear(size_t pos) const;

    bool intersects(const BitSet &rhs) const;
    bool operator==(const BitSet &rhs) const;
    bool operator!=(const BitSet &rhs) const;
    BitSet operator&(const BitSet &rhs) const;
    BitSet operator|(const BitSet &rhs) const;
    BitSet &operator&=(const BitSet &rhs);
    BitSet &operator|=(const BitSet &rhs);

    void Set(const BitSet &rhs);
    void Reset(const BitSet &rhs);
    void BuildComplement(const BitSet &lhs, const BitSet &rhs);
    void BuildIntersection(const BitSet &lhs, const BitSet &rhs);
    bool Contains(const BitSet &rhs) const;
    std::string ToString() const;
    void FromString(std::string str);
    std::string ToNumberedString() const;

private:
    friend class BitSetTest;

    void compact();
    void check_invariants();

    std::vector<uint64_t> blocks_;
};

class IndexAllocator {
public:
    IndexAllocator(size_t max_index)
        : max_index_(max_index), last_index_(BitSet::npos) { }

    size_t AllocIndex() {
        size_t index;
        if (last_index_ == BitSet::npos) {
            index = bitset_.find_first_clear();
        } else {
            index = bitset_.find_next_clear(last_index_);
            if (index > max_index_) {
                index = bitset_.find_first_clear();
            }
        }
        if (index != BitSet::npos) {
            bitset_.set(index);
        }
        last_index_ = index;
        return index;
    }

    void FreeIndex(size_t index) {
        //assert(index <= max_index_);
        bitset_.reset(index);
    }

private:
    BitSet bitset_;
    size_t max_index_;
    size_t last_index_;
};

#endif
