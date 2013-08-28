/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/bitset.h"

#include <cassert>
#include <string>
#include <string.h>

using namespace std;

//
// Provides the same functionality as ffsl.  Needed as ffsl is not supported
// on all platforms. Note that the positions are numbered 1 through 64, with
// a return value of 0 indicating that there are no set bits.
//
static int find_first_set64(uint64_t value) {
    int bit;

    int lower = value;
    if ((bit = ffs(lower)) > 0)
        return bit;

    int upper = value >> 32;
    if ((bit = ffs(upper)) > 0)
        return 32 + bit;

    return 0;
}

static int find_first_clear64(uint64_t value) {
    return find_first_set64(~value);
}

//
// Return the number of set bits. K&R method.
//
static int num_bits_set(uint64_t value) {
    int count = 0;
    while (value != 0) {
        value &= value - 1;
        count++;
    }
    return count;
}

// Position pos is w.r.t the entire bitset, starts at 0.
// Index    idx is the block number i.e. the index in the vector, starts at 0.
// Offset   offset is w.r.t a given 64 bit block, starts at 0.
static inline size_t block_index(size_t pos) {
    return pos / 64;
}

static inline size_t block_offset(size_t pos) {
    return pos % 64;
}

static inline size_t bit_position(size_t idx, size_t offset) {
    return (idx * 64 + offset);
}

const size_t BitSet::npos;

//
// Set bit at given position, growing the vector if needed.
//
BitSet &BitSet::set(size_t pos) {
    size_t idx = block_index(pos);
    if (idx >= blocks_.size())
        blocks_.resize(idx + 1);
    blocks_[idx] |= 1LL << block_offset(pos);
    return *this;
}

//
// Reset bit at given position, shrinking the vector if possible.
//
BitSet &BitSet::reset(size_t pos) {
    size_t idx = block_index(pos);
    if (idx < blocks_.size()) {
        blocks_[idx] &= ~(1LL << block_offset(pos));
        compact();
    }
    return *this;
}

// Test bit at given position.
bool BitSet::test(size_t pos) const {
    size_t idx = block_index(pos);
    if (idx < blocks_.size()) {
        return ((blocks_[idx] & (1LL << block_offset(pos))) != 0);
    } else {
        return false;
    }
}

//
// Shortcut to reset all bits in the bitset.
//
void BitSet::clear() {
    blocks_.resize(0);
}

//
// Return true if there are no bits in the bitset.
//
bool BitSet::empty() const {
    return (blocks_.size() == 0);
}

//
// Return true if no bits are set.
//
bool BitSet::none() const {
    return (blocks_.size() == 0);
}

//
// Return true at least one bit is set.
//
bool BitSet::any() const {
    return (blocks_.size() != 0);
}

//
// Return the raw number of bits in the bitset. Simply depends on the number
// of blocks in the vector.
//
size_t BitSet::size() const {
    return blocks_.size() * 64;
}

//
// Return total number of set bits.
//
size_t BitSet::count() const {
    size_t count = 0;
    for (size_t idx = 0; idx < blocks_.size(); idx++) {
        count += num_bits_set(blocks_[idx]);
    }
    return count;
}

//
// Shrink the underlying vector as much as possible.  All trailing blocks
// that are 0 can be removed.
//
// Note that the for loop does not handle idx 0 since the loop  variable
// is unsigned.
//
void BitSet::compact() {
    if (blocks_.size() == 0)
        return;

    for (size_t idx = blocks_.size() - 1; idx > 0; idx--) {
        if (blocks_[idx] != 0) {
            blocks_.resize(idx + 1);
            return;
        }
    }

    if (blocks_[0] != 0) {
        blocks_.resize(1);
        return;
    }

    blocks_.clear();
}

//
// Sanity check a bitset.  The last block must never be 0.  Always called
// after any compaction is done or in cases where no compaction is needed.
//
void BitSet::check_invariants() {
    size_t mysize = blocks_.size();
    if (mysize != 0)
        assert(blocks_[mysize -1] != 0);
}

//
// Return the position of the first set bit.  Needs to compensate for the
// return value convention used by find_first_set64.
//
size_t BitSet::find_first() const {
    for (size_t idx = 0; idx < blocks_.size(); idx++) {
        int bit = find_first_set64(blocks_[idx]);
        if (bit > 0)
            return bit_position(idx, bit - 1);
    }
    return BitSet::npos;
}

//
// Return the position of the next set bit.  Needs to compensate for the
// return value convention used by find_first_set64.
//
size_t BitSet::find_next(size_t pos) const {
    size_t idx = block_index(pos);

    // If the block index is beyond the vector, we're done.
    if (idx >= blocks_.size())
        return BitSet::npos;

    // If the offset is not 63, clear out the bits from 0 through offset
    // and look for the first set bit.
    if (block_offset(pos) < 63) {
        uint64_t temp = blocks_[idx] & ~((1LL << (block_offset(pos) + 1)) - 1);
        int bit = find_first_set64(temp);
        if (bit > 0)
            return bit_position(idx, bit - 1);
    }

    // Go through all blocks after the start block for the pos and see if
    // there's a set bit.
    for (idx++; idx < blocks_.size(); idx++) {
        int bit = find_first_set64(blocks_[idx]);
        if (bit > 0)
            return bit_position(idx, bit - 1);
    }
    return BitSet::npos;
}

//
// Return the position of the first clear bit.  It could be beyond the last
// block in the vector. This is fine as we automatically grow the vector if
// needed from set().
//
// Need to compensate for return value convention used by find_first_clear64.
//
size_t BitSet::find_first_clear() const {
    for (size_t idx = 0; idx < blocks_.size(); idx++) {
        int bit = find_first_clear64(blocks_[idx]);
        if (bit > 0) {
            return bit_position(idx, bit - 1);
        }
    }
    return size();
}

//
// Return the position of the next clear bit.  It could be beyond the last
// block in the vector. This is fine as we automatically grow the vector if
// needed from set().
//
// Need to compensate for return value convention used by find_first_clear64.
//
size_t BitSet::find_next_clear(size_t pos) const {
    size_t idx = block_index(pos);

    // If the block index is beyond the vector, we're done.
    if (idx >= blocks_.size())
        return pos + 1;

    // If the offset is not 63, set all the bits from 0 through offset and
    // look for the first clear bit.
    if (block_offset(pos) < 63) {
        uint64_t temp = blocks_[idx] | ((1LL << (block_offset(pos) + 1)) - 1);
        int bit = find_first_clear64(temp);
        if (bit > 0)
            return bit_position(idx, bit - 1);
    }

    // Go through all blocks after the start block for the pos and see if
    // there's a clear bit.
    for (idx++; idx < blocks_.size(); idx++) {
        int bit = find_first_clear64(blocks_[idx]);
        if (bit > 0) {
            return bit_position(idx, bit - 1);
        }
    }
    return size();
}

//
// Return (*this & rhs != 0).
//
bool BitSet::intersects(const BitSet &rhs) const {
    size_t minsize = std::min(blocks_.size(), rhs.blocks_.size());
    for (size_t idx = 0; idx < minsize; idx++) {
        if (blocks_[idx] & rhs.blocks_[idx])
            return true;
    }
    return false;
}

//
// Return (*this == rhs).
//
// Note that it's fine to first compare the number of blocks in the vectors
// since we always shrink the vectors whenever possible.
//
bool BitSet::operator==(const BitSet &rhs) const {
    if (blocks_.size() != rhs.blocks_.size())
        return false;
    for (size_t idx = 0; idx < blocks_.size(); idx++) {
        if (blocks_[idx] != rhs.blocks_[idx])
            return false;
    }
    return true;
}

//
// Return (*this != rhs).
//
bool BitSet::operator!=(const BitSet &rhs) const {
    return !operator==(rhs);
}

//
// Return (*this & rhs).
//
BitSet BitSet::operator&(const BitSet &rhs) const {
    BitSet temp;
    temp.BuildIntersection(*this, rhs);
    temp.check_invariants();
    return temp;
}

//
// Return (*this | rhs).
//
BitSet BitSet::operator|(const BitSet &rhs) const {
    BitSet temp;
    size_t minsize = std::min(blocks_.size(), rhs.blocks_.size());
    size_t maxsize = std::max(blocks_.size(), rhs.blocks_.size());
    temp.blocks_.resize(maxsize);

    // Process common blocks.
    for (size_t idx = 0; idx < minsize; idx++) {
        temp.blocks_[idx] = blocks_[idx] | rhs.blocks_[idx];
    }

    // Process blocks that exist in LHS only. It's a noop if RHS is bigger.
    for (size_t idx = minsize; idx < blocks_.size(); idx++) {
        temp.blocks_[idx] = blocks_[idx];
    }

    // Process blocks that exist in RHS only. It's a noop if LHS is bigger.
    for (size_t idx = minsize; idx < rhs.blocks_.size(); idx++) {
        temp.blocks_[idx] = rhs.blocks_[idx];
    }

    temp.check_invariants();
    return temp;
}

//
// Implement (*this &= rhs).
//
// Note that we can't simply resize the vector to minsize since we may be
// able to shrink it even more depending on the values in the blocks.
//
BitSet &BitSet::operator&=(const BitSet &rhs) {
    size_t minsize = std::min(blocks_.size(), rhs.blocks_.size());
    for (size_t idx = 0; idx < minsize; idx++) {
        blocks_[idx] &= rhs.blocks_[idx];
    }
    for (size_t idx = minsize; idx < blocks_.size(); idx++) {
        blocks_[idx] = 0;
    }
    compact();
    check_invariants();
    return *this;
}

//
// Implement (*this |= rhs).
//
// Note that we grow the vector only once instead of doing it multiple
// times.
//
BitSet &BitSet::operator|=(const BitSet &rhs) {
    if (blocks_.size() < rhs.blocks_.size())
        blocks_.resize(rhs.blocks_.size());
    for (size_t idx = 0; idx < rhs.blocks_.size(); idx++) {
        blocks_[idx] |= rhs.blocks_[idx];
    }
    check_invariants();
    return *this;
}

//
// Identical to operator|=.
//
void BitSet::Set(const BitSet &rhs) {
    this->operator|=(rhs);
    check_invariants();
}

//
// Implement (*this &= ~rhs).
//
void BitSet::Reset(const BitSet &rhs) {
    size_t minsize = std::min(blocks_.size(), rhs.blocks_.size());
    for (size_t idx = 0; idx < minsize; idx++) {
        blocks_[idx] &= ~rhs.blocks_[idx];
    }
    compact();
    check_invariants();
}

//
// Implement (*this = lhs & ~rhs).
//
// Note that we won't enter the second for loop at all if lhs is not bigger
// than rhs.  Need to compact only for this case, but it is cheap enough to
// try (and do nothing) when lhs is bigger than rhs.
//
void BitSet::BuildComplement(const BitSet &lhs, const BitSet &rhs) {
    blocks_.clear();
    blocks_.resize(lhs.blocks_.size());
    size_t minsize = std::min(blocks_.size(), rhs.blocks_.size());
    for (size_t idx = 0; idx < minsize; idx++) {
        blocks_[idx] = lhs.blocks_[idx] & ~rhs.blocks_[idx];
    }
    for (size_t idx = minsize; idx < lhs.blocks_.size(); idx++) {
        blocks_[idx] = lhs.blocks_[idx];
    }
    compact();
    check_invariants();
}

//
// Implement (*this = lhs & rhs).
//
// We avoid the need to compact or to resize multiple times by building
// the blocks in reverse order.
//
// Note that the for loop does not handle idx 0 since the loop  variable
// is unsigned.
//
void BitSet::BuildIntersection(const BitSet &lhs, const BitSet &rhs) {
    blocks_.clear();
    size_t minsize = std::min(lhs.blocks_.size(), rhs.blocks_.size());

    if (minsize == 0)
        return;

    for (size_t idx = minsize - 1; idx > 0; idx--) {
        if (lhs.blocks_[idx] & rhs.blocks_[idx]) {
            if (blocks_.size() == 0)
                blocks_.resize(idx + 1);
            blocks_[idx] = lhs.blocks_[idx] & rhs.blocks_[idx];
        }
    }

    if (lhs.blocks_[0] & rhs.blocks_[0]) {
        if (blocks_.size() == 0)
            blocks_.resize(1);
        blocks_[0] = lhs.blocks_[0] & rhs.blocks_[0];
    }

    check_invariants();
}

//
// Return true if *this contains rhs.  Implemented as (rhs & ~*this != 0).
//
bool BitSet::Contains(const BitSet &rhs) const {
    if (blocks_.size() < rhs.blocks_.size())
        return false;
    for (size_t idx = 0; idx < rhs.blocks_.size(); idx++) {
        if (rhs.blocks_[idx] & ~blocks_[idx])
            return false;
    }
    return true;
}

//
// Returns string representation of the bitset. A character in the string is
// '1' if the corresponding bit is set, and '0' if it is not.  The character
// position i in the string corresponds to bit position size() - 1 - i in the
// bitset.  This is consistent with the boost::dynamic_bitset free function
// to_string().
//
string BitSet::ToString() const {
    string str;
    for (size_t last_pos = -1, pos = find_first(); pos != BitSet::npos;
         last_pos = pos, pos = find_next(pos)) {
        str = str.append(pos - last_pos - 1, '0');
        str += '1';
    }
    return str;
}

//
// Intialize the bitset from the provided string representation. The string
// must use the same format as described in ToString above. We traverse the
// string in reverse order to ensure that we do not have to resize multiple
// times.
//
// Note that the for loop does not handle str_idx 0 since the loop variable
// is unsigned.
//
void BitSet::FromString(string str) {
    blocks_.clear();

    if (str.length() == 0)
        return;

    for (size_t str_idx = str.length() - 1; str_idx > 0; str_idx--) {
        if (str[str_idx] == '1')
            set(str_idx);
    }

    if (str[0] == '1')
        set(0);
}
