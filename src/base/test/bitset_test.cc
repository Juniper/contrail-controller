/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/bitset.h"
#include "base/logging.h"
#include "testing/gunit.h"

using namespace std;

class BitSetTest : public ::testing::Test {
protected:
    vector<uint64_t> &get_blocks(BitSet &bitset) {
        return bitset.blocks_;
    }
};

static int num_bits_set(uint64_t value) {
    int count = 0;
    while (value != 0) {
        value &= value - 1;
        count++;
    }
    return count;
}

TEST_F(BitSetTest, Basic) {
    BitSet bitset;
    vector<uint64_t> &blocks = get_blocks(bitset);
    EXPECT_EQ(bitset.size(), 0);
    EXPECT_EQ(blocks.size(), 0);
}

// Set and verify each individual bit position within block idx 0.
TEST_F(BitSetTest, set1) {
    for (int pos = 0; pos <= 63; pos++) {
        BitSet bitset;
        vector<uint64_t> &blocks = get_blocks(bitset);
        bitset.set(pos);
        EXPECT_EQ(blocks.size(), 1);
        EXPECT_EQ(blocks[0],  1LL << pos);
        EXPECT_EQ(num_bits_set(blocks[0]), 1);
    }
}

// Set and verify each individual bit position within block idx 1.
TEST_F(BitSetTest, set2) {
    for (int pos = 128; pos <= 191; pos++) {
        BitSet bitset;
        vector<uint64_t> &blocks = get_blocks(bitset);
        bitset.set(pos);
        EXPECT_EQ(blocks.size(), 3);
        EXPECT_EQ(blocks[0], 0 );
        EXPECT_EQ(blocks[1], 0 );
        EXPECT_EQ(blocks[2], 1LL << (pos - 128));
        EXPECT_EQ(num_bits_set(blocks[2]), 1);
    }
}

// Set and verify each individual bit position within block idx 0 thru 15.
TEST_F(BitSetTest, set3)  {
    for (int pos = 0; pos <= 1023; pos++) {
        BitSet bitset;
        vector<uint64_t> &blocks = get_blocks(bitset);
        bitset.set(pos);
        EXPECT_EQ(blocks.size(), pos / 64 + 1);
        EXPECT_EQ(blocks[pos / 64], 1LL << (pos % 64));
        EXPECT_EQ(num_bits_set(blocks[pos / 64]), 1);
    }
}

// Set all bits within block idx 1 and verify.
TEST_F(BitSetTest, set4) {
    BitSet bitset;
    vector<uint64_t> &blocks = get_blocks(bitset);
    for (int pos = 64; pos <= 127; pos++) {
        bitset.set(pos);
    }
    EXPECT_EQ(blocks.size(), 2);
    EXPECT_EQ(blocks[1], -1LL);
    EXPECT_EQ(num_bits_set(blocks[1]), 64);
}

// Set, Reset and verify each individual bit position within idx 0.
TEST_F(BitSetTest, reset1) {
    for (int pos = 0; pos <= 63; pos++) {
        BitSet bitset;
        vector<uint64_t> &blocks = get_blocks(bitset);
        bitset.set(pos);
        EXPECT_EQ(blocks.size(), 1);
        bitset.reset(pos);
        EXPECT_EQ(blocks.size(), 0);
    }
}

// Set, Reset and verify each individual bit position within idx 1.
TEST_F(BitSetTest, reset2) {
    for (int pos = 64; pos <= 127; pos++) {
        BitSet bitset;
        vector<uint64_t> &blocks = get_blocks(bitset);
        bitset.set(pos);
        EXPECT_EQ(blocks.size(), 2);
        bitset.reset(pos);
        EXPECT_EQ(blocks.size(), 0);
    }
}

// Set, Reset and verify each individual bit position within idx 0 thru 15.
TEST_F(BitSetTest, reset3) {
    for (int pos = 0; pos <= 1023; pos++) {
        BitSet bitset;
        vector<uint64_t> &blocks = get_blocks(bitset);
        bitset.set(pos);
        EXPECT_EQ(blocks.size(), pos / 64 + 1);
        bitset.reset(pos);
        EXPECT_EQ(blocks.size(), 0);
    }
}

// Try to reset position outside the BitSet.
TEST_F(BitSetTest, reset4)  {
    for (int pos = 64; pos <= 127; pos++) {
        BitSet bitset;
        vector<uint64_t> &blocks = get_blocks(bitset);
        bitset.set(pos);
        EXPECT_EQ(blocks.size(), 2);
        bitset.reset(128);
        EXPECT_EQ(blocks.size(), 2);
        EXPECT_EQ(num_bits_set(blocks[1]), 1);
    }
}

//  Set bits 0-127 and reset 0-63.
TEST_F(BitSetTest, reset5) {
    BitSet bitset;
    vector<uint64_t> &blocks = get_blocks(bitset);
    for (int pos = 0; pos <= 127; pos++) {
        bitset.set(pos);
    }
    EXPECT_EQ(blocks.size(), 2);
    EXPECT_EQ(blocks[0], -1LL);
    EXPECT_EQ(blocks[1], -1LL);
    for (int pos = 63; pos >= 0; pos--) {
        bitset.reset(pos);
    }
    EXPECT_EQ(blocks.size(), 2);
    EXPECT_EQ(blocks[1], -1LL);
}

//  Set bits 0-127 and reset 64-127.
TEST_F(BitSetTest, reset6) {
    BitSet bitset;
    vector<uint64_t> &blocks = get_blocks(bitset);
    for (int pos = 0; pos <= 127; pos++) {
        bitset.set(pos);
    }
    EXPECT_EQ(blocks.size(), 2);
    EXPECT_EQ(blocks[0], -1LL);
    EXPECT_EQ(blocks[1], -1LL);
    for (int pos = 127; pos >= 64; pos--) {

        EXPECT_EQ(blocks.size(), 2);
        bitset.reset(pos);
    }
    EXPECT_EQ(blocks.size(), 1);
    EXPECT_EQ(blocks[0], -1LL);
}

// Clear an empty BitSet.
TEST_F(BitSetTest, clear1) {
    BitSet bitset;
    vector<uint64_t> &blocks = get_blocks(bitset);
    bitset.clear();
    EXPECT_EQ(blocks.size(), 0);
}


// Clear BitSet with first/last bit set in each idx.
TEST_F(BitSetTest, clear2) {
    BitSet bitset;
    vector<uint64_t> &blocks = get_blocks(bitset);

    for (int idx = 0; idx < 32; idx++) {
        bitset.set(idx * 64);
    }
    EXPECT_EQ(blocks.size(), 32);
    bitset.clear();
    EXPECT_EQ(blocks.size(), 0);

    for (int idx = 0; idx < 32; idx++) {
        bitset.set(idx * 64 + 63);
    }
    EXPECT_EQ(blocks.size(), 32);
    bitset.clear();
    EXPECT_EQ(blocks.size(), 0);
}

// Clear BitSet with all bits set in idx 0 thru 15.
TEST_F(BitSetTest, clear3) {
    BitSet bitset;
    vector<uint64_t> &blocks = get_blocks(bitset);
    for (int pos = 0; pos < 64 * 16 ; pos++) {
        bitset.set(pos);
    }
    EXPECT_EQ(blocks.size(), 16);
    bitset.clear();
    EXPECT_EQ(blocks.size(), 0);
}

// Should always return false for an empty BitSet.
TEST_F(BitSetTest, test1) {
    BitSet bitset;
    for (int pos = 0; pos <= 1023; pos++) {
        EXPECT_TRUE(!bitset.test(pos));
    }
}

// Set each individual bit position within block idx 0/1.  For each bit make
// sure that test() succeeds for only that one.
TEST_F(BitSetTest, test2) {
    for (int pos = 64; pos <= 127; pos++) {
        BitSet bitset;
        bitset.set(pos);
        for (int pos2 = 0; pos2 <= 127; pos2++) {
            if (pos2 == pos) {
                EXPECT_TRUE(bitset.test(pos2));
            } else {
                EXPECT_TRUE(!bitset.test(pos2));
            }
        }
    }

    for (int pos = 0; pos <= 63; pos++) {
        BitSet bitset;
        bitset.set(pos);
        for (int pos2 = 0; pos2 <= 63; pos2++) {
            if (pos2 == pos) {
                EXPECT_TRUE(bitset.test(pos2));
            } else {
                EXPECT_TRUE(!bitset.test(pos2));
            }
        }
    }
}

// Set all bits within idx 0 thru 15 and test them.
TEST_F(BitSetTest, test3) {
    BitSet bitset;
    for (int pos = 0; pos <= 1023; pos++) {
        bitset.set(pos);
    }
    for (int pos = 0; pos <= 1023; pos++) {
        EXPECT_TRUE(bitset.test(pos));
    }
}

// Should always return 0 for an empty bitset.
TEST_F(BitSetTest, count1) {
    BitSet bitset;
    EXPECT_EQ(bitset.count(), 0);
}

// Check bitset count as we set each of the first 1K positions.
// Then check again as we reset each position.
TEST_F(BitSetTest, count2) {
    BitSet bitset;
    for (int pos = 0; pos <= 1023; pos++) {
        EXPECT_EQ(bitset.count(), pos);
        bitset.set(pos);
    }
    for (int pos = 1023; pos >= 0; pos--) {
        bitset.reset(pos);
        EXPECT_EQ(bitset.count(), pos);
    }
}

// Should always return BitSet::npos (-1) for an empty BitSet.
TEST_F(BitSetTest, find_first1) {
    BitSet bitset;
    EXPECT_EQ(bitset.size(), 0);
    EXPECT_EQ(bitset.find_first(), BitSet::npos);
}

// Run through each of the first 128 bit positions.
//  - Make sure that find_first returns that position.
//  - Then set all bits after the position and make sure the result is same.
//  - Then set bit 0 and make sure the result is 0.
//  - Then reset bit 0 and make sure we get the original position.
TEST_F(BitSetTest, find_first2) {
    for (int pos = 0; pos <= 127; pos++) {
        BitSet bitset;
        bitset.set(pos);
        EXPECT_EQ(bitset.find_first(), pos);
        for (int pos2 = pos; pos2 <= 127; pos2++) {
            bitset.set(pos2);
        }
        EXPECT_EQ(bitset.find_first(), pos);
        bitset.set(0);
        EXPECT_EQ(bitset.find_first(), 0);
        if (pos != 0) bitset.reset(0);
        EXPECT_EQ(bitset.find_first(), pos);
    }
}

// Should always return BitSet::npos (-1) for an empty BitSet.
TEST_F(BitSetTest, find_next1) {
    BitSet bitset;
    EXPECT_EQ(bitset.size(), 0);
    for (int pos = 0; pos <= 1023; pos++) {
        EXPECT_EQ(bitset.find_next(pos), BitSet::npos);
    }
}

// Run through each of the first 128 bit positions and make sure find_next
// returns BitSet::npos (-1) for each of them.
TEST_F(BitSetTest, find_next2) {
    BitSet bitset;
    for (int pos = 0; pos <= 127; pos++) {
        bitset.set(pos);
        EXPECT_EQ(bitset.find_next(pos), BitSet::npos);
    }
}

// Set all of the first 256 bits and run find_next for each of them.
TEST_F(BitSetTest, find_next3) {
    BitSet bitset;
    for (int pos = 0; pos <= 255; pos++) {
        bitset.set(pos);
    }
    for (int pos = 0; pos <= 254; pos++) {
        EXPECT_EQ(bitset.find_next(pos), pos+1);
    }
    EXPECT_EQ(bitset.find_next(255), BitSet::npos);
}

// Check find_next with first/last bit set within idx 0 thru 31.
TEST_F(BitSetTest, find_next4) {
    BitSet bitset;
    for (int idx = 0; idx <= 31; idx++) {
        bitset.set(idx * 64);
    }
    for (int idx = 0; idx < 31; idx++) {
        for (int pos = 0; pos <= 63; pos++) {
            EXPECT_EQ(bitset.find_next(idx * 64 + pos), (idx + 1) * 64);
        }
    }
    for (int offset = 0; offset <= 63; offset++) {
        EXPECT_EQ(bitset.find_next(31 * 64 + offset), BitSet::npos);
    }

    bitset.clear();
    EXPECT_EQ(bitset.size(), 0);
    for (int idx = 0; idx <= 31; idx++) {
        bitset.set(idx * 64 + 63);
    }
    for (int idx = 0; idx < 31; idx++) {
        for (int pos = 0; pos <= 63; pos++) {
            EXPECT_EQ(bitset.find_next(idx * 64 + 63 + pos), (idx+1) * 64 + 63);
        }
    }
    for (int offset = 0; offset <= 63; offset++) {
        EXPECT_EQ(bitset.find_next(31 * 64 + 63 + offset), BitSet::npos);
    }
}

// Set bit 1023 and verify find_next for all positions less than 1023.
TEST_F(BitSetTest, find_next5) {
    BitSet bitset;
    bitset.set(1023);
    for (int pos = 0; pos < 1023; pos++) {
        EXPECT_EQ(bitset.find_next(pos), 1023);
    }
    EXPECT_EQ(bitset.find_next(1023), BitSet::npos);
}

// Should always return BitSet::npos (-1) for an empty BitSet.
TEST_F(BitSetTest, find_last1) {
    BitSet bitset;
    EXPECT_EQ(bitset.size(), 0);
    EXPECT_EQ(bitset.find_last(), BitSet::npos);
}

// Run through each of the first 128 bit positions.
//  - Make sure that find_last returns that position.
//  - Then set all bits before the position and make sure the result is same.
//  - Then set bit 127 and make sure the result is 127.
//  - Then reset bit 127 and make sure we get the original position.
TEST_F(BitSetTest, find_last2) {
    for (int pos = 0; pos <= 127; pos++) {
        BitSet bitset;
        bitset.set(pos);
        EXPECT_EQ(bitset.find_last(), pos);
        for (int pos2 = 0; pos2 < pos; pos2++) {
            bitset.set(pos2);
        }
        EXPECT_EQ(bitset.find_last(), pos);
        bitset.set(127);
        EXPECT_EQ(bitset.find_last(), 127);
        if (pos != 127) bitset.reset(127);
        EXPECT_EQ(bitset.find_last(), pos);
    }
}

// Should always return 0 for an empty BitSet.
TEST_F(BitSetTest, find_first_clear1) {
    BitSet bitset;
    EXPECT_EQ(bitset.size(), 0);
    EXPECT_EQ(bitset.find_first_clear(), 0);
}

// Run through the first 1K bit positions and call find_first_clear before
// setting the bit in that position.
//
// Then go through the first 1K positions again, resetting and setting them
// again, calling find_first_clear in between.
//
// Then go through the positions in reverse order, reseting each position
// and calling find_first_clear.
TEST_F(BitSetTest, find_first_clear2) {
    BitSet bitset;

    for (int pos = 0; pos <= 1023; pos++) {
        EXPECT_EQ(bitset.find_first_clear(), pos);
        bitset.set(pos);
    }

    for (int pos = 0; pos <= 1023; pos++) {
        bitset.reset(pos);
        EXPECT_EQ(bitset.find_first_clear(), pos);
        bitset.set(pos);
    }

    for (int pos = 1023; pos >= 0; pos--) {
        bitset.reset(pos);
        EXPECT_EQ(bitset.find_first_clear(), pos);
    }
}

// Should always return the next position for an empty BitSet.
TEST_F(BitSetTest, find_next_clear1) {
    BitSet bitset;
    EXPECT_EQ(bitset.size(), 0);
    for (int pos = 0; pos <= 1023; pos++) {
        EXPECT_EQ(bitset.find_next_clear(pos), pos + 1);
    }
}

// Set each of the first 128 bit positions.
// Run through each of the first 128 bit positions, clearing them and make
// sure find_next_clear returns 128 for each of them.
TEST_F(BitSetTest, find_next_clear2) {
    BitSet bitset;
    for (int pos = 0; pos <= 127; pos++) {
        bitset.set(pos);
    }
    for (int pos = 0; pos <= 127; pos++) {
        bitset.reset(pos);
        EXPECT_EQ(bitset.find_next_clear(pos), 128);
    }
}

// Set bit 256.
// Run through each of the first 255 bits and run find_next_clear for each.
TEST_F(BitSetTest, find_next_clear3) {
    BitSet bitset;
    bitset.set(256);
    for (int pos = 0; pos < 255; pos++) {
        EXPECT_EQ(bitset.find_next_clear(pos), pos + 1);
    }
}

// Check find_next_clear with first/last bit clear within idx 0 thru 31.
TEST_F(BitSetTest, find_next_clear4) {
    BitSet bitset;
    for (int idx = 0; idx <= 31; idx++) {
        for (int pos = 0; pos <= 63 ; pos++) {
            bitset.set(idx * 64 + pos);
        }
    }
    for (int idx = 0; idx <= 31; idx++) {
        bitset.reset(idx * 64);
    }
    for (int idx = 0; idx <= 31; idx++) {
        for (int pos = 0; pos <= 63; pos++) {
            EXPECT_EQ(bitset.find_next_clear(idx * 64 + pos), (idx + 1) * 64);
        }
    }

    bitset.clear();
    EXPECT_EQ(bitset.size(), 0);

    for (int idx = 0; idx <= 31; idx++) {
        for (int pos = 0; pos <= 63 ; pos++) {
            bitset.set(idx * 64 + pos);
        }
    }
    for (int idx = 0; idx <= 31; idx++) {
        bitset.reset(idx * 64 + 63);
    }
    for (int idx = 0; idx < 31; idx++) {
        for (int pos = 0; pos < 63; pos++) {
            EXPECT_EQ(bitset.find_next_clear(idx * 64 + pos), idx * 64 + 63);
        }
        EXPECT_EQ(bitset.find_next_clear(idx * 64 + 63), (idx + 1) * 64 + 63);
    }
}

// Set bits 0-1022 and verify find_next_clear for all positions less than 1023.
TEST_F(BitSetTest, find_next_clear5) {
    BitSet bitset;
    for (int pos = 0; pos <= 1023; pos++) {
        bitset.set(pos);
    }
    bitset.reset(1023);
    for (int pos = 0; pos < 1023; pos++) {
        EXPECT_EQ(bitset.find_next_clear(pos), 1023);
    }
    EXPECT_EQ(bitset.find_next_clear(1023), 1024);
}

// Empty Bitsets are always equal.
TEST_F(BitSetTest, relational_equal1) {
    BitSet bitset1, bitset2;
    EXPECT_TRUE(bitset1 == bitset2);
    EXPECT_FALSE(bitset1 != bitset2);
}

// Bitsets of different sizes are not equal.
TEST_F(BitSetTest, relational_equal2) {
    BitSet bitset1, bitset2;
    for (int idx = 0; idx <= 31; idx++) {
        bitset1.set(idx * 64);
    }
    for (int idx = 0; idx <= 30; idx++) {
        bitset2.set(idx * 64);
    }
    EXPECT_FALSE(bitset1 == bitset2);
    EXPECT_TRUE(bitset1 != bitset2);
}

// Bitsets of same sizes but with different bits not equal.
TEST_F(BitSetTest, relational_equal3) {
    BitSet bitset1, bitset2;
    for (int idx = 0; idx <= 31; idx++) {
        bitset1.set(idx * 64);
    }
    for (int idx = 0; idx <= 31; idx++) {
        bitset2.set(idx * 64 + 63);
    }
    EXPECT_FALSE(bitset1 == bitset2);
    EXPECT_TRUE(bitset1 != bitset2);
}

// Compare bitsets with first and last bits set in idx 0 thru 31.
TEST_F(BitSetTest, relational_equal4) {
    BitSet bitset1, bitset2;
    for (int idx = 0; idx <= 31; idx++) {
        bitset1.set(idx * 64);
        bitset1.set(idx * 64 + 63);
    }
    for (int idx = 31; idx >= 0; idx--) {
        bitset2.set(idx * 64 + 63);
        bitset2.set(idx * 64);
    }
    EXPECT_TRUE(bitset1 == bitset2);
    EXPECT_FALSE(bitset1 != bitset2);
}

// Compare bitsets with all bits set in idx 0 thru 31.
TEST_F(BitSetTest, relational_equal5) {
    BitSet bitset1, bitset2;
    for (int pos = 0; pos <= 1023; pos++) {
        bitset1.set(pos);
    }
    for (int pos = 1023; pos >= 0; pos--) {
        bitset2.set(pos);
    }
    EXPECT_TRUE(bitset1 == bitset2);
    EXPECT_FALSE(bitset1 != bitset2);
}

// Verify results when one of the bitsets is empty. The other bitsets has the
// first and last bits within each idx set.
// The side loop allows reversing the LHS and RHS roles.
TEST_F(BitSetTest, bitwise_or1a) {
    BitSet bitset[2], result;
    for (int idx = 0; idx <= 31; idx++) {
        bitset[0].set(idx * 64);
        bitset[0].set(idx * 64 + 63);
    }
    for (int side = 0; side < 2; side++) {
        result = bitset[side] | bitset[1-side];
        EXPECT_TRUE(result == bitset[0]);
    }
}

// Verify results when one of the bitsets is empty.  The other bitset has all
// bits set.
// The side loop allows reversing the LHS and RHS roles.
TEST_F(BitSetTest, bitwise_or1b) {
    BitSet bitset[2], result;
    for (int pos = 0; pos <= 1023; pos++) {
        bitset[0].set(pos);
    }
    for (int side = 0; side < 2; side++) {
        result = bitset[side] | bitset[1-side];
        EXPECT_TRUE(result == bitset[0]);
    }
}

// Bitwise or with self gives self.
// The bitset has first and last bits within each idx set.
TEST_F(BitSetTest, bitwise_or2a) {
    BitSet bitset, result;
    for (int idx = 0; idx <= 31; idx++) {
        bitset.set(idx * 64);
        bitset.set(idx * 64 + 63);
    }
    result = bitset | bitset;
    EXPECT_TRUE(result == bitset);
}

// Bitwise or with self gives self.
// The bitset has all bits set.
TEST_F(BitSetTest, bitwise_or2b) {
    BitSet bitset, result;
    for (int pos = 0; pos <= 1023; pos++) {
        bitset.set(pos);
    }
    result = bitset | bitset;
    EXPECT_TRUE(result == bitset);
}

// Verify results when one of the bitsets is a subset of the other.
// The side loop allows reversing the LHS and RHS roles.
TEST_F(BitSetTest, bitwise_or3) {
    for (int pos = 0; pos <= 255; pos++) {
        BitSet bitset[2], expected, result;
        for (int pos1 = 0; pos1 <= 255; pos1++) {
            bitset[0].set(pos1);
            expected.set(pos1);
        }
        for (int pos2 = 0; pos2 <= pos; pos2++) {
            bitset[1].set(pos2);
        }
        for (int side = 0; side < 2; side++) {
            result = bitset[side] | bitset[1-side];
            EXPECT_TRUE(result == expected);
            EXPECT_TRUE(result == bitset[0]);
        }
    }
}

// Verify result when the bitsets are the same size and have no overlap.
// The side loop allows reversing the LHS and RHS roles.
TEST_F(BitSetTest, bitwise_or4a) {
    BitSet bitset[2], expected, result;
    for (int pos = 0; pos <= 1023; pos++) {
        if (pos % 2 != 0) bitset[0].set(pos);
        if (pos % 2 == 0) bitset[1].set(pos);
        expected.set(pos);
    }
    for (int side = 0; side < 2; side++) {
        result = bitset[side] | bitset[1-side];
        EXPECT_TRUE(result == expected);
    }
}

// Verify when LHS and RHS are different sizes and have no overlap.
// The side loop allows reversing the LHS and RHS roles.
TEST_F(BitSetTest, bittwise_or4b) {
    BitSet bitset[2], expected, result;
    for (int pos = 0; pos <= 1023; pos++) {
        if (pos % 2 != 0) {
            bitset[0].set(pos);
            expected.set(pos);
        }
    }
    for (int pos = 0; pos <= 511; pos++) {
        if (pos % 2 == 0) {
            bitset[1].set(pos);
            expected.set(pos);
        }
    }
    for (int side = 0; side < 2; side++) {
        result = bitset[side] | bitset[1-side];
        EXPECT_TRUE(result == expected);
    }
}

// Run through several (N,M) tuples that set every Nth bit in LHS and every
// Mth bit in RHS.
// The two bitsets are the same size since all N and M are <= 63.
// The side loop allows reversing the LHS and RHS roles.
TEST_F(BitSetTest, bitwise_or5) {
    int data[][2] = {
        {2, 3}, {3, 5}, {7, 9}, {3, 7}, {5, 6}, {4, 7}, {6, 10}, {5, 10},
        {7, 17}, {13, 17}, {19, 23}, {23, 29}, {37, 41}, {43, 47}, {53, 59}
    };

    for (size_t idx = 0; idx < sizeof(data)/sizeof(data[0]); idx++) {
        ASSERT_TRUE(data[idx][0] <= 63);
        ASSERT_TRUE(data[idx][1] <= 63);
        BitSet bitset[2], expected, result;
        for (int pos = 0; pos <= 1023; pos++) {
            if (pos % data[idx][0] == 0) {
                bitset[0].set(pos);
                expected.set(pos);
            }
            if (pos % data[idx][1] == 0) {
                bitset[1].set(pos);
                expected.set(pos);
            }
        }
        for (int side = 0; side < 2; side++) {
            result = bitset[side] | bitset[1-side];
            EXPECT_TRUE(result == expected);
        }
    }
}

// Run through several (N,M) tuples that set every Nth bit in LHS and every
// Mth bit in RHS.
// The two bitsets are different sizes.
// The range loop allows different position ranges for the smaller bitset.
// The side loop allows reversing the LHS and RHS roles.
TEST_F(BitSetTest, bitwise_or6) {
    int data[][2] = {
        {2, 3}, {3, 5}, {7, 9}, {3, 7}, {5, 6}, {4, 7}, {6, 10}, {5, 10},
        {7, 17}, {13, 17}, {19, 23}, {23, 29}, {37, 41}, {43, 47}, {53, 59}
    };

    for (size_t idx = 0; idx < sizeof(data)/sizeof(data[0]); idx++) {
        for (int range = 0; range <= 2; range++) {
            int pos_start = 256 * range;
            int pos_end = 256 * range + 511;
            BitSet bitset[2], expected, result;
            for (int pos = 0; pos <= 1023; pos++) {
                if (pos % data[idx][0] == 0) {
                    bitset[0].set(pos);
                    expected.set(pos);
                }
            }

            for (int pos = pos_start; pos <= pos_end; pos++) {
                if (pos % data[idx][1] == 0) {
                    bitset[1].set(pos);
                    expected.set(pos);
                }
            }

            for (int side = 0; side < 2; side++) {
                result = bitset[side] | bitset[1-side];
                EXPECT_TRUE(result == expected);
            }
        }
    }
}

// If one of the bitsets is empty, the LHS becomes empty.
// The other bitsets has the first and last bits within each idx set.
// The side loop allows reversing the LHS and RHS roles.
TEST_F(BitSetTest, assignment_bitwise_and1a) {
    for (int side = 0; side < 2; side++) {
        BitSet bitset[2];
        for (int idx = 0; idx <= 31; idx++) {
            bitset[0].set(idx * 64);
            bitset[0].set(idx * 64 + 63);
        }
        bitset[side] &= bitset[1-side];
        EXPECT_EQ(bitset[side].size(), 0);
    }
}

// If one of the bitsets is empty, the LHS becomes empty.
// The other bitsets has the first and last bits within each idx set.
// The side loop allows reversing the LHS and RHS roles.
TEST_F(BitSetTest, assignment_bitwise_and1b) {
    for (int side = 0; side < 2; side++) {
        BitSet bitset[2];
        for (int pos = 0; pos <= 1023; pos++) {
            bitset[0].set(pos);
        }
        bitset[side] &= bitset[1-side];
        EXPECT_EQ(bitset[side].size(), 0);
    }
}

// If LHS and RHS are the same variable, the LHS should be unchanged.
// The bitset has first and last bits within each idx set.
TEST_F(BitSetTest, assignment_bitwise_and2a) {
    BitSet bitset, result;
    for (int idx = 0; idx <= 31; idx++) {
        bitset.set(idx * 64);
        bitset.set(idx * 64 + 63);
    }
    result = bitset;
    bitset &= bitset;
    EXPECT_TRUE(result == bitset);
}

// If LHS and RHS are the same variable, the LHS should be unchanged.
// The bitset has all bits set.
TEST_F(BitSetTest, assignment_bitwise_and2b) {
    BitSet bitset, result;
    for (int pos = 0; pos <= 1023; pos++) {
        bitset.set(pos);
    }
    result = bitset;
    bitset &= bitset;
    EXPECT_TRUE(result == bitset);
}

// LHS becomes the smaller of the 2 bitsets when one is a subset of the other.
// The side loop allows reversing the LHS and RHS roles.
TEST_F(BitSetTest, assignment_bitwise_and3) {
    for (int side = 0; side < 2; side++) {
        for (int pos = 0; pos <= 255; pos++) {
            BitSet bitset[2], expected;
            for (int pos1 = 0; pos1 <= 255; pos1++) {
                bitset[0].set(pos1);
            }
            for (int pos2 = 0; pos2 <= pos; pos2++) {
                bitset[1].set(pos2);
                expected.set(pos2);
            }
            bitset[side] &= bitset[1-side];
            EXPECT_TRUE(bitset[side] == expected);
            EXPECT_TRUE(bitset[side] == bitset[1]);
        }
    }
}

// If LHS and RHS are the same size and have no overlap, the LHS becomes empty.
// The side loop allows reversing the LHS and RHS roles.
TEST_F(BitSetTest, assignment_bitwise_and4) {
    for (int side = 0; side < 2; side++) {
        BitSet bitset[2];
        for (int pos = 0; pos <= 1023; pos++) {
            if (pos % 2 != 0) bitset[0].set(pos);
            if (pos % 2 == 0) bitset[1].set(pos);
        }
        bitset[side] &= bitset[1-side];
        EXPECT_EQ(bitset[side].size(), 0);
    }
}

// Run through several (N,M) tuples that set every Nth bit in LHS and every
// Mth bit in RHS.
// The two bitsets are the same size since all N and M are <= 63.
// The side loop allows reversing the LHS and RHS roles.
TEST_F(BitSetTest, assignment_bitwise_and5) {
    int data[][2] = {
        {2, 3}, {3, 5}, {7, 9}, {3, 7}, {5, 6}, {4, 7}, {6, 10}, {5, 10},
        {7, 17}, {13, 17}, {19, 23}, {23, 29}, {37, 41}, {43, 47}, {53, 59}
    };

    for (int side = 0; side < 2; side++) {
        for (size_t idx = 0; idx < sizeof(data)/sizeof(data[0]); idx++) {
            ASSERT_TRUE(data[idx][0] <= 63);
            ASSERT_TRUE(data[idx][1] <= 63);
            BitSet bitset[2], expected;
            for (int pos = 0; pos <= 1023; pos++) {
                if (pos % data[idx][0] == 0) bitset[0].set(pos);
                if (pos % data[idx][1] == 0) bitset[1].set(pos);
                if ((pos % data[idx][0] == 0) && (pos % data[idx][1] == 0))
                    expected.set(pos);
            }
            bitset[side] &= bitset[1-side];
            EXPECT_TRUE(bitset[side] == expected);
        }
    }
}

// Run through several (N,M) tuples that set every Nth bit in LHS and every
// Mth bit in RHS.
// The two bitsets are different sizes.
// The side loop allows reversing the LHS and RHS roles.
// The range loop allows different position ranges for the smaller bitset.
TEST_F(BitSetTest, assignment_bitwise_and6) {
    int data[][2] = {
        {2, 3}, {3, 5}, {7, 9}, {3, 7}, {5, 6}, {4, 7}, {6, 10}, {5, 10},
        {7, 17}, {13, 17}, {19, 23}, {23, 29}, {37, 41}, {43, 47}, {53, 59}
    };

    for (int side = 0; side < 2; side++) {
        for (size_t idx = 0; idx < sizeof(data)/sizeof(data[0]); idx++) {
            for (int range = 0; range <= 2; range++) {
                int pos_start = 256 * range;
                int pos_end = 256 * range + 511;
                BitSet bitset[2], expected;

                for (int pos = 0; pos <= 1023; pos++) {
                    if (pos % data[idx][0] == 0) bitset[0].set(pos);
                }

                for (int pos = pos_start; pos <= pos_end; pos++) {
                    if (pos % data[idx][1] == 0) bitset[1].set(pos);
                    if ((pos % data[idx][0] == 0) && (pos % data[idx][1] == 0))
                        expected.set(pos);
                }

                bitset[side] &= bitset[1-side];
                EXPECT_TRUE(bitset[side] == expected);
            }
        }
    }
}

// If one of the 2 bitsets is empty, LHS gets the value of the other one.
// The other bitsets has the first and last bits within each idx set.
// The side loop allows reversing the LHS and RHS roles.
TEST_F(BitSetTest, assignment_bitwise_or1a) {
    for (int side = 0; side < 2; side++) {
        BitSet bitset[2], expected;
        for (int idx = 0; idx <= 31; idx++) {
            bitset[0].set(idx * 64);
            bitset[0].set(idx * 64 + 63);
            expected.set(idx * 64);
            expected.set(idx * 64 + 63);
        }
        bitset[side] |= bitset[1-side];
        EXPECT_TRUE(bitset[side] == expected);
    }
}

// If one of the 2 bitsets is empty, LHS gets the value of the other one.
// The other bitsets has all bits set.
// The side loop allows reversing the LHS and RHS roles.
TEST_F(BitSetTest, assignment_bitwise_or1b) {
    for (int side = 0; side < 2; side++) {
        BitSet bitset[2], expected;
        for (int pos = 0; pos <= 1023; pos++) {
            bitset[0].set(pos);
            expected.set(pos);
        }
        bitset[side] |= bitset[1-side];
        EXPECT_TRUE(bitset[side] == expected);
    }
}

// If LHS and RHS are the same variable, the LHS should be unchanged.
// The bitset has first and last bits within each idx set.
TEST_F(BitSetTest, assignment_bitwise_or2a) {
    BitSet bitset, expected;
    for (int idx = 0; idx <= 31; idx++) {
        bitset.set(idx * 64);
        bitset.set(idx * 64 + 63);
        expected.set(idx * 64);
        expected.set(idx * 64 + 63);
    }
    bitset |= bitset;
    EXPECT_TRUE(expected == bitset);
}

// If LHS and RHS are the same variable, the LHS should be unchanged.
// The bitset has all bits set.
TEST_F(BitSetTest, assignment_bitwise_or2b) {
    BitSet bitset, expected;
    for (int pos = 0; pos <= 1023; pos++) {
        bitset.set(pos);
        expected.set(pos);
    }
    bitset |= bitset;
    EXPECT_TRUE(expected == bitset);
}

// LHS becomes the larger of the 2 bitsets when one is a subset of the other.
// The side loop allows reversing the LHS and RHS roles.
TEST_F(BitSetTest, assignment_bitwise_or3) {
    for (int side = 0; side < 2; side++) {
        for (int pos = 0; pos <= 255; pos++) {
            BitSet bitset[2], expected;
            for (int pos1 = 0; pos1 <= 255; pos1++) {
                bitset[0].set(pos1);
                expected.set(pos1);
            }
            for (int pos2 = 0; pos2 <= pos; pos2++) {
                bitset[1].set(pos2);
            }
            bitset[side] |= bitset[1-side];
            EXPECT_TRUE(bitset[side] == expected);
            EXPECT_TRUE(bitset[side] == bitset[0]);
        }
    }
}

// Verify result when the bitsets are the same size and have no overlap.
// The side loop allows reversing the LHS and RHS roles.
TEST_F(BitSetTest, assignment_bitwise_or4a) {
    for (int side = 0; side < 2; side++) {
        BitSet bitset[2], expected;
        for (int pos = 0; pos <= 1023; pos++) {
            if (pos % 2 != 0) bitset[0].set(pos);
            if (pos % 2 == 0) bitset[1].set(pos);
            expected.set(pos);
        }
        bitset[side] |= bitset[1-side];
        EXPECT_TRUE(bitset[side] == expected);
    }
}

// Verify when LHS and RHS are different sizes and have no overlap.
// The side loop allows reversing the LHS and RHS roles.
TEST_F(BitSetTest, assignment_bitwise_or4b) {
    for (int side = 0; side < 2; side++) {
        BitSet bitset[2], expected;
        for (int pos = 0; pos <= 1023; pos++) {
            if (pos % 2 != 0) {
                bitset[0].set(pos);
                expected.set(pos);
            }
        }
        for (int pos = 0; pos <= 511; pos++) {
            if (pos % 2 == 0) {
                bitset[1].set(pos);
                expected.set(pos);
            }
        }
        bitset[side] |= bitset[1-side];
        EXPECT_TRUE(bitset[side] == expected);
    }
}

// Run through several (N,M) tuples that set every Nth bit in LHS and every
// Mth bit in RHS.
// The two bitsets are the same size since all N and M are <= 63.
// The side loop allows reversing the LHS and RHS roles.
TEST_F(BitSetTest, assignment_bitwise_or5) {
    int data[][2] = {
        {2, 3}, {3, 5}, {7, 9}, {3, 7}, {5, 6}, {4, 7}, {6, 10}, {5, 10},
        {7, 17}, {13, 17}, {19, 23}, {23, 29}, {37, 41}, {43, 47}, {53, 59}
    };

    for (int side = 0; side < 2; side++) {
        for (size_t idx = 0; idx < sizeof(data)/sizeof(data[0]); idx++) {
            ASSERT_TRUE(data[idx][0] <= 63);
            ASSERT_TRUE(data[idx][1] <= 63);
            BitSet bitset[2], expected;
            for (int pos = 0; pos <= 1023; pos++) {
                if (pos % data[idx][0] == 0) {
                    bitset[0].set(pos);
                    expected.set(pos);
                }
                if (pos % data[idx][1] == 0) {
                    bitset[1].set(pos);
                    expected.set(pos);
                }
            }
            bitset[side] |= bitset[1-side];
            EXPECT_TRUE(bitset[side] == expected);
        }
    }
}

// Run through several (N,M) tuples that set every Nth bit in LHS and every Mth
// bit in RHS.
// The two bitsets are different sizes.
// The side loop allows reversing the LHS and RHS roles.
// The range loop allows different position ranges for the smaller bitset.
TEST_F(BitSetTest, assignment_bitwise_or6) {
    int data[][2] = {
        {2, 3}, {3, 5}, {7, 9}, {3, 7}, {5, 6}, {4, 7}, {6, 10}, {5, 10},
        {7, 17}, {13, 17}, {19, 23}, {23, 29}, {37, 41}, {43, 47}, {53, 59}
    };

    for (int side = 0; side < 2; side++) {
        for (size_t idx = 0; idx < sizeof(data)/sizeof(data[0]); idx++) {
            for (int range = 0; range <= 2; range++) {
                int pos_start = 256 * range;
                int pos_end = 256 * range + 511;
                BitSet bitset[2], expected;
                for (int pos = 0; pos <= 1023; pos++) {
                    if (pos % data[idx][0] == 0) {
                        bitset[0].set(pos);
                        expected.set(pos);
                    }
                }

                for (int pos = pos_start; pos <= pos_end; pos++) {
                    if (pos % data[idx][1] == 0) {
                        bitset[1].set(pos);
                        expected.set(pos);
                    }
                }
                bitset[side] |= bitset[1-side];
                EXPECT_TRUE(bitset[side] == expected);
            }
        }
    }
}

// Verify results when one of the bitsets is empty. The other bitsets has the
// first and last bits within each idx set.
// The side loop allows reversing the LHS and RHS roles.
TEST_F(BitSetTest, Reset1a) {
    for (int side = 0; side < 2; side++) {
        BitSet bitset[2], expected[2];
        for (int idx = 0; idx <= 31; idx++) {
            bitset[0].set(idx * 64);
            bitset[0].set(idx * 64 + 63);
            expected[0].set(idx * 64);
            expected[0].set(idx * 64 + 63);
        }
        bitset[side].Reset(bitset[1-side]);
        EXPECT_TRUE(bitset[side] == expected[side]);
    }
}

// Verify results when one of the bitsets is empty.  The other bitset has all
// bits set.
// The side loop allows reversing the LHS and RHS roles.
TEST_F(BitSetTest, Reset1b) {
    for (int side = 0; side < 2; side++) {
        BitSet bitset[2], expected[2];
        for (int pos = 0; pos <= 1023; pos++) {
            bitset[0].set(pos);
            expected[0].set(pos);
        }
        bitset[side].Reset(bitset[1-side]);
        EXPECT_TRUE(bitset[side] == expected[side]);
    }
}

// If LHS and RHS are the same variable, the LHS should become empty.
// The bitset has first and last bits within each idx set.
TEST_F(BitSetTest, Reset2a) {
    BitSet bitset;
    for (int idx = 0; idx <= 31; idx++) {
        bitset.set(idx * 64);
        bitset.set(idx * 64 + 63);
    }
    bitset.Reset(bitset);
    EXPECT_EQ(bitset.size(), 0);
}

// If LHS and RHS are the same variable, the LHS should become empty.
// The bitset has all bits set.
TEST_F(BitSetTest, Reset2b) {
    BitSet bitset;
    for (int pos = 0; pos <= 1023; pos++) {
        bitset.set(pos);
    }
    bitset.Reset(bitset);
    EXPECT_EQ(bitset.size(), 0);
}

// Verify results when one of the bitsets is a subset of the other.
// The side loop allows reversing the LHS and RHS roles.
TEST_F(BitSetTest, Reset3) {
    for (int side = 0; side < 2; side++) {
        for (int pos = 0; pos <= 255; pos++) {
            BitSet bitset[2], expected[2];
            for (int pos1 = 0; pos1 <= 255; pos1++) {
                bitset[0].set(pos1);
                if (pos1 > pos) expected[0].set(pos1);
            }
            for (int pos2 = 0; pos2 <= pos; pos2++) {
                bitset[1].set(pos2);
            }
            bitset[side].Reset(bitset[1-side]);
            EXPECT_TRUE(bitset[side] == expected[side]);
        }
    }
}

// If LHS and RHS are the same size and have no overlap, they stay intact.
// The side loop allows reversing the LHS and RHS roles.
TEST_F(BitSetTest, Reset4a) {
    for (int side = 0; side < 2; side++) {
        BitSet bitset[2], expected[2];
        for (int pos = 0; pos <= 1023; pos++) {
            if (pos % 2 != 0) {
                bitset[0].set(pos);
                expected[0].set(pos);
            }
            if (pos % 2 == 0) {
                bitset[1].set(pos);
                expected[1].set(pos);
            }
        }
        bitset[side].Reset(bitset[1-side]);
        EXPECT_TRUE(bitset[side] == expected[side]);
    }
}

// If LHS and RHS are different sizes and have no overlap, they stay intact.
// The side loop allows reversing the LHS and RHS roles.
TEST_F(BitSetTest, Reset4b) {
    for (int side = 0; side < 2; side++) {
        BitSet bitset[2], expected[2];
        for (int pos = 0; pos <= 1023; pos++) {
            if (pos % 2 != 0) {
                bitset[0].set(pos);
                expected[0].set(pos);
            }
        }
        for (int pos = 0; pos <= 511; pos++) {
            if (pos % 2 == 0) {
                bitset[1].set(pos);
                expected[1].set(pos);
            }
        }
        bitset[side].Reset(bitset[1-side]);
        EXPECT_TRUE(bitset[side] == expected[side]);
    }
}

// Run through several (N,M) tuples that set every Nth bit in LHS and every
// Mth bit in RHS.
// The two bitsets are the same size since all N and M are <= 63.
// The side loop allows reversing the LHS and RHS roles.
TEST_F(BitSetTest, Reset5) {
    int data[][2] = {
        {2, 3}, {3, 5}, {7, 9}, {3, 7}, {5, 6}, {4, 7}, {6, 10}, {5, 10},
        {7, 17}, {13, 17}, {19, 23}, {23, 29}, {37, 41}, {43, 47}, {53, 59}
    };

    for (int side = 0; side < 2; side++) {
        for (size_t idx = 0; idx < sizeof(data)/sizeof(data[0]); idx++) {
            ASSERT_TRUE(data[idx][0] <= 63);
            ASSERT_TRUE(data[idx][1] <= 63);
            BitSet bitset[2], expected[2];
            for (int pos = 0; pos <= 1023; pos++) {
                if (pos % data[idx][0] == 0) bitset[0].set(pos);
                if (pos % data[idx][1] == 0) bitset[1].set(pos);
                if ((pos % data[idx][0] == 0) && (pos % data[idx][1] != 0))
                    expected[0].set(pos);
                if ((pos % data[idx][1] == 0) && (pos % data[idx][0] != 0))
                    expected[1].set(pos);
            }
            bitset[side].Reset(bitset[1-side]);
            EXPECT_TRUE(bitset[side] == expected[side]);
        }
    }
}

// Run through several (N,M) tuples that set every Nth bit in LHS and every
// Mth bit in RHS.
// The two bitsets are different sizes.
// The side loop allows reversing the LHS and RHS roles.
// The range loop allows different position ranges for the smaller bitset.
TEST_F(BitSetTest, Reset6) {
    int data[][2] = {
        {2, 3}, {3, 5}, {7, 9}, {3, 7}, {5, 6}, {4, 7}, {6, 10}, {5, 10},
        {7, 17}, {13, 17}, {19, 23}, {23, 29}, {37, 41}, {43, 47}, {53, 59}
    };

    for (int side = 0; side < 2; side++) {
        for (size_t idx = 0; idx < sizeof(data)/sizeof(data[0]); idx++) {
            for (int range = 0; range <= 2; range++) {
                int pos_start = 256 * range;
                int pos_end = 256 * range + 511;
                BitSet bitset[2], expected[2];

                for (int pos = 0; pos <= 1023; pos++) {
                    if (pos % data[idx][0] == 0)
                        bitset[0].set(pos);
                    if ((pos % data[idx][0] == 0) &&
                        (pos < pos_start || pos > pos_end ||
                         pos % data[idx][1] != 0))
                        expected[0].set(pos);
                }

                for (int pos = pos_start; pos <= pos_end; pos++) {
                    if (pos % data[idx][1] == 0) bitset[1].set(pos);
                    if ((pos % data[idx][1] == 0) && (pos % data[idx][0] != 0))
                        expected[1].set(pos);
                }
                bitset[side].Reset(bitset[1-side]);
                EXPECT_TRUE(bitset[side] == expected[side]);
            }
        }
    }
}

// Verify results when one of the bitsets is empty. The other bitsets has the
// first and last bits within each idx set.
// The side loop allows reversing the LHS and RHS roles.
TEST_F(BitSetTest, BuildComplement1a) {
    for (int side = 0; side < 2; side++) {
        BitSet bitset[2], expected[2], result;
        for (int idx = 0; idx <= 31; idx++) {
            bitset[0].set(idx * 64);
            bitset[0].set(idx * 64 + 63);
            expected[0].set(idx * 64);
            expected[0].set(idx * 64 + 63);
        }
        result.BuildComplement(bitset[side], bitset[1-side]);
        EXPECT_TRUE(result == expected[side]);
    }
}

// Verify results when one of the bitsets is empty.  The other bitset has all
// bits set.
// The side loop allows reversing the LHS and RHS roles.
TEST_F(BitSetTest, BuildComplement1b) {
    for (int side = 0; side < 2; side++) {
        BitSet bitset[2], expected[2], result;
        for (int pos = 0; pos <= 1023; pos++) {
            bitset[0].set(pos);
            expected[0].set(pos);
        }
        result.BuildComplement(bitset[side], bitset[1-side]);
        EXPECT_TRUE(result == expected[side]);
    }
}

// If LHS and RHS are the same variable, the result should be empty.
// The bitset has first and last bits within each idx set.
TEST_F(BitSetTest, BuildComplement2a) {
    BitSet bitset, result;
    for (int idx = 0; idx <= 31; idx++) {
        bitset.set(idx * 64);
        bitset.set(idx * 64 + 63);
    }
    result.BuildComplement(bitset, bitset);
    EXPECT_EQ(result.size(), 0);
}

// If LHS and RHS are the same variable, the result should be empty.
// The bitset has all bits set.
TEST_F(BitSetTest, BuildComplement2b) {
    BitSet bitset, result;
    for (int pos = 0; pos <= 1023; pos++) {
        bitset.set(pos);
    }
    result.BuildComplement(bitset, bitset);
    EXPECT_EQ(result.size(), 0);
}

// Verify results when one of the bitsets is a subset of the other.
// The side loop allows reversing the LHS and RHS roles.
TEST_F(BitSetTest, BuildComplement3) {
    for (int side = 0; side < 2; side++) {
        for (int pos = 0; pos <= 255; pos++) {
            BitSet bitset[2], expected[2], result;
            for (int pos1 = 0; pos1 <= 255; pos1++) {
                bitset[0].set(pos1);
                if (pos1 > pos) expected[0].set(pos1);
            }
            for (int pos2 = 0; pos2 <= pos; pos2++) {
                bitset[1].set(pos2);
            }
            result.BuildComplement(bitset[side], bitset[1-side]);
            EXPECT_TRUE(result == expected[side]);
        }
    }
}

// Verify when LHS and RHS are the same size and have no overlap.
// The side loop allows reversing the LHS and RHS roles.
TEST_F(BitSetTest, BuildComplement4a) {
    for (int side = 0; side < 2; side++) {
        BitSet bitset[2], expected[2], result;
        for (int pos = 0; pos <= 1023; pos++) {
            if (pos % 2 != 0) {
                bitset[0].set(pos);
                expected[0].set(pos);
            }
            if (pos % 2 == 0) {
                bitset[1].set(pos);
                expected[1].set(pos);
            }
        }
        result.BuildComplement(bitset[side], bitset[1-side]);
        EXPECT_TRUE(result == expected[side]);
    }
}

// Verify when LHS and RHS are different sizes and have no overlap.
// The side loop allows reversing the LHS and RHS roles.
TEST_F(BitSetTest, BuildComplement4b) {
    for (int side = 0; side < 2; side++) {
        BitSet bitset[2], expected[2], result;
        for (int pos = 0; pos <= 1023; pos++) {
            if (pos % 2 != 0) {
                bitset[0].set(pos);
                expected[0].set(pos);
            }
        }
        for (int pos = 0; pos <= 511; pos++) {
            if (pos % 2 == 0) {
                bitset[1].set(pos);
                expected[1].set(pos);
            }
        }
        result.BuildComplement(bitset[side], bitset[1-side]);
        EXPECT_TRUE(result == expected[side]);
        EXPECT_TRUE(result == bitset[side]);
    }
}

// Run through several (N,M) tuples that set every Nth bit in LHS and every
// Mth bit in RHS.
// The two bitsets are the same size since all N and M are <= 63.
// The side loop allows reversing the LHS and RHS roles.
TEST_F(BitSetTest, BuildComplement5) {
    int data[][2] = {
            {2, 3}, {3, 5}, {7, 9}, {3, 7}, {5, 6}, {4, 7}, {6, 10}, {5, 10},
            {7, 17}, {13, 17}, {19, 23}, {23, 29}, {37, 41}, {43, 47}, {53, 59}
    };

    for (int side = 0; side < 2; side++) {
        for (size_t idx = 0; idx < sizeof(data)/sizeof(data[0]); idx++) {
            ASSERT_TRUE(data[idx][0] <= 63);
            ASSERT_TRUE(data[idx][1] <= 63);
            BitSet bitset[2], expected[2], result;
            for (int pos = 0; pos <= 1023; pos++) {
                if (pos % data[idx][0] == 0) bitset[0].set(pos);
                if (pos % data[idx][1] == 0) bitset[1].set(pos);
                if ((pos % data[idx][0] == 0) && (pos % data[idx][1] != 0))
                    expected[0].set(pos);
                if ((pos % data[idx][1] == 0) && (pos % data[idx][0] != 0))
                    expected[1].set(pos);
            }
            result.BuildComplement(bitset[side], bitset[1-side]);
            EXPECT_TRUE(result == expected[side]);
        }
    }
}

// Run through several (N,M) tuples that set every Nth bit in LHS and every
// Mth bit in RHS.
// The two bitsets are different sizes.
// The side loop allows reversing the LHS and RHS roles.
// The range loop allows different position ranges for the smaller bitset.
TEST_F(BitSetTest, BuildComplement6) {
    int data[][2] = {
            {2, 3}, {3, 5}, {7, 9}, {3, 7}, {5, 6}, {4, 7}, {6, 10}, {5, 10},
            {7, 17}, {13, 17}, {19, 23}, {23, 29}, {37, 41}, {43, 47}, {53, 59}
    };

    for (int side = 0; side < 2; side++) {
        for (size_t idx = 0; idx < sizeof(data)/sizeof(data[0]); idx++) {
            for (int range = 0; range <= 2; range++) {
                int pos_start = 256 * range;
                int pos_end = 256 * range + 511;
                BitSet bitset[2], expected[2], result;

                for (int pos = 0; pos <= 1023; pos++) {
                    if (pos % data[idx][0] == 0)
                        bitset[0].set(pos);
                    if ((pos % data[idx][0] == 0) &&
                        ((pos < pos_start || pos > pos_end) ||
                         (pos % data[idx][1] != 0)))
                        expected[0].set(pos);
                }

                for (int pos = pos_start; pos <= pos_end; pos++) {
                    if (pos % data[idx][1] == 0) bitset[1].set(pos);
                    if ((pos % data[idx][1] == 0) && (pos % data[idx][0] != 0))
                        expected[1].set(pos);
                }
                result.BuildComplement(bitset[side], bitset[1-side]);
                EXPECT_TRUE(result == expected[side]);
            }
        }
    }
}

// Verify results when one of the bitsets is empty. The other bitsets has the
// first and last bits within each idx set.
// The side loop allows reversing the LHS and RHS roles.
TEST_F(BitSetTest, BuildIntersection1a) {
    for (int side = 0; side < 2; side++) {
        BitSet bitset[2], result;
        for (int idx = 0; idx <= 31; idx++) {
            bitset[0].set(idx * 64);
            bitset[0].set(idx * 64 + 63);
        }
        result.BuildIntersection(bitset[side], bitset[1-side]);
        EXPECT_EQ(result.size(), 0);
        bool isect = bitset[side].intersects(bitset[1-side]);
        EXPECT_EQ(result.any(), isect);
    }
}

// Verify results when one of the bitsets is empty.  The other bitset has all
// bits set.
// The side loop allows reversing the LHS and RHS roles.
TEST_F(BitSetTest, BuildIntersection1b) {
    for (int side = 0; side < 2; side++) {
        BitSet bitset[2], result;
        for (int pos = 0; pos <= 1023; pos++) {
            bitset[0].set(pos);
        }
        result.BuildIntersection(bitset[side], bitset[1-side]);
        EXPECT_EQ(result.size(), 0);
        bool isect = bitset[side].intersects(bitset[1-side]);
        EXPECT_EQ(result.any(), isect);
    }
}

// If LHS and RHS are the same variable, the result should be same.
// The bitset has first and last bits within each idx set.
TEST_F(BitSetTest, BuildIntersection2a) {
    BitSet bitset, result;
    for (int idx = 0; idx <= 31; idx++) {
        bitset.set(idx * 64);
        bitset.set(idx * 64 + 63);
    }
    result.BuildIntersection(bitset, bitset);
    EXPECT_TRUE(result == bitset);
    bool isect = bitset.intersects(bitset);
    EXPECT_EQ(result.any(), isect);
}

// If LHS and RHS are the same variable, the result should be same.
// The bitset has all bits set.
TEST_F(BitSetTest, BuildIntersection2b) {
    BitSet bitset, result;
    for (int pos = 0; pos <= 1023; pos++) {
        bitset.set(pos);
    }
    result.BuildIntersection(bitset, bitset);
    EXPECT_TRUE(result == bitset);
    bool isect = bitset.intersects(bitset);
    EXPECT_EQ(result.any(), isect);
}

// Verify results when one of the bitsets is a subset of the other.
// The side loop allows reversing the LHS and RHS roles.
TEST_F(BitSetTest, BuildIntersection3) {
    for (int side = 0; side < 2; side++) {
        for (int pos = 0; pos <= 255; pos++) {
            BitSet bitset[2], expected, result;
            for (int pos1 = 0; pos1 <= 255; pos1++) {
                bitset[0].set(pos1);
            }
            for (int pos2 = 0; pos2 <= pos; pos2++) {
                bitset[1].set(pos2);
                expected.set(pos2);
            }
            result.BuildIntersection(bitset[side], bitset[1-side]);
            EXPECT_TRUE(result == expected);
            EXPECT_TRUE(result == bitset[1]);
            bool isect = bitset[side].intersects(bitset[1-side]);
            EXPECT_EQ(result.any(), isect);
        }
    }
}

// Verify result when the bitsets are the same size and have no overlap.
// The side loop allows reversing the LHS and RHS roles.
TEST_F(BitSetTest, BuildIntersection4a) {
    for (int side = 0; side < 2; side++) {
        BitSet bitset[2], result;
        for (int pos = 0; pos <= 1023; pos++) {
            if (pos % 2 != 0) bitset[0].set(pos);
            if (pos % 2 == 0) bitset[1].set(pos);
        }
        result.BuildIntersection(bitset[side], bitset[1-side]);
        EXPECT_EQ(result.size(), 0);
        bool isect = bitset[side].intersects(bitset[1-side]);
        EXPECT_EQ(result.any(), isect);
    }
}

// Verify when LHS and RHS are different sizes and have no overlap.
// The side loop allows reversing the LHS and RHS roles.
TEST_F(BitSetTest, BuildIntersection4b) {
    for (int side = 0; side < 2; side++) {
        BitSet bitset[2], result;
        for (int pos = 0; pos <= 1023; pos++) {
            if (pos % 2 != 0) bitset[0].set(pos);
        }
        for (int pos = 0; pos <= 511; pos++) {
            if (pos % 2 == 0) bitset[1].set(pos);
        }
        result.BuildIntersection(bitset[side], bitset[1-side]);
        EXPECT_EQ(result.size(), 0);
        bool isect = bitset[side].intersects(bitset[1-side]);
        EXPECT_EQ(result.any(), isect);
    }
}

// Run through several (N,M) tuples that set every Nth bit in LHS and every
// Mth bit in RHS.
// The two bitsets are the same size since all N and M are <= 63.
// The side loop allows reversing the LHS and RHS roles.
TEST_F(BitSetTest, BuildIntersection5) {
    int data[][2] = {
        {2, 3}, {3, 5}, {7, 9}, {3, 7}, {5, 6}, {4, 7}, {6, 10}, {5, 10},
        {7, 17}, {13, 17}, {19, 23}, {23, 29}, {37, 41}, {43, 47}, {53, 59}
    };

    for (size_t idx = 0; idx < sizeof(data)/sizeof(data[0]); idx++) {
        ASSERT_TRUE(data[idx][0] <= 63);
        ASSERT_TRUE(data[idx][1] <= 63);
        BitSet bitset[2], expected, result;
        for (int pos = 0; pos <= 1023; pos++) {
            if (pos % data[idx][0] == 0) bitset[0].set(pos);
            if (pos % data[idx][1] == 0) bitset[1].set(pos);
            if ((pos % data[idx][0] == 0) && (pos % data[idx][1] == 0))
                expected.set(pos);
        }
        for (int side = 0; side < 2; side++) {
            result.BuildIntersection(bitset[side], bitset[1-side]);
            EXPECT_TRUE(result == expected);
            bool isect = bitset[side].intersects(bitset[1-side]);
            EXPECT_EQ(result.any(), isect);
        }
    }
}

// Run through several (N,M) tuples that set every Nth bit in LHS and every
// Mth bit in RHS.
// The two bitsets are different sizes.
// The side loop allows reversing the LHS and RHS roles.
// The range loop allows different position ranges for the smaller bitset.
TEST_F(BitSetTest, BuildIntersection6) {
    int data[][2] = {
        {2, 3}, {3, 5}, {7, 9}, {3, 7}, {5, 6}, {4, 7}, {6, 10}, {5, 10},
        {7, 17}, {13, 17}, {19, 23}, {23, 29}, {37, 41}, {43, 47}, {53, 59}
    };

    for (size_t idx = 0; idx < sizeof(data)/sizeof(data[0]); idx++) {
        for (int range = 0; range <= 2; range++) {
            int pos_start = 256 * range;
            int pos_end = 256 * range + 511;
            BitSet bitset[2], expected, result;

            for (int pos = 0; pos <= 1023; pos++) {
                if (pos % data[idx][0] == 0)
                    bitset[0].set(pos);
            }

            for (int pos = pos_start; pos <= pos_end; pos++) {
                if (pos % data[idx][1] == 0) bitset[1].set(pos);
                if ((pos % data[idx][0] == 0) && (pos % data[idx][1] == 0))
                    expected.set(pos);
            }

            for (int side = 0; side < 2; side++) {
                result.BuildIntersection(bitset[side], bitset[1-side]);
                EXPECT_TRUE(result == expected);
                bool isect = bitset[side].intersects(bitset[1-side]);
                EXPECT_EQ(result.any(), isect);
            }
        }
    }
}

//

// Verify results when one of the bitsets is empty. The other bitsets has the
// first and last bits within each idx set.
// The side loop allows reversing the LHS and RHS roles.
TEST_F(BitSetTest, Contains1a) {
    BitSet bitset[2];
    for (int idx = 0; idx <= 31; idx++) {
        bitset[0].set(idx * 64);
        bitset[0].set(idx * 64 + 63);
    }
    for (int side = 0; side < 2; side++) {
        bool expected = (side == 0);
        bool result = bitset[side].Contains(bitset[1-side]);
        EXPECT_EQ(result, expected);
    }
}

// Verify results when one of the bitsets is empty. The other bitsets has all
// bits set.
// The side loop allows reversing the LHS and RHS roles.
TEST_F(BitSetTest, Contains1b) {
    BitSet bitset[2];
    for (int pos = 0; pos <= 1023; pos++) {
        bitset[0].set(pos);
    }
    for (int side = 0; side < 2; side++) {
        bool expected = (side == 0);
        bool result = bitset[side].Contains(bitset[1-side]);
        EXPECT_EQ(result, expected);
    }
}

// If LHS and RHS are the same variable, the result should be true.
// The bitset has first and last bits within each idx set.
TEST_F(BitSetTest, Contains2a) {
    BitSet bitset, result;
    for (int idx = 0; idx <= 31; idx++) {
        bitset.set(idx * 64);
        bitset.set(idx * 64 + 63);
    }
    EXPECT_TRUE(bitset.Contains(bitset));
}

// If LHS and RHS are the same variable, the result should be true.
// The bitset has all bits set.
TEST_F(BitSetTest, Contains2b) {
    BitSet bitset, result;
    for (int pos = 0; pos <= 1023; pos++) {
        bitset.set(pos);
    }
    EXPECT_TRUE(bitset.Contains(bitset));
}

// Verify result when the bitsets are the same size and have no overlap.
// The side loop allows reversing the LHS and RHS roles.
TEST_F(BitSetTest, Contains4a) {
    BitSet bitset[2];
    for (int pos = 0; pos <= 1023; pos++) {
        if (pos % 2 != 0) bitset[0].set(pos);
        if (pos % 2 == 0) bitset[1].set(pos);
    }
    for (int side = 0; side < 2; side++) {
        EXPECT_FALSE(bitset[side].Contains(bitset[1-side]));
    }
}

// Verify when LHS and RHS are different sizes and have no overlap.
// The side loop allows reversing the LHS and RHS roles.
TEST_F(BitSetTest, Contains4b) {
    BitSet bitset[2];
    for (int pos = 0; pos <= 1023; pos++) {
        if (pos % 2 != 0) bitset[0].set(pos);
    }
    for (int pos = 0; pos <= 511; pos++) {
        if (pos % 2 == 0) bitset[1].set(pos);
    }
    for (int side = 0; side < 2; side++) {
        EXPECT_FALSE(bitset[side].Contains(bitset[1-side]));
    }
}

// Run through several (N,M) tuples that set every Nth bit in LHS and every
// Mth bit in RHS, where M is a multiple of N.
// The two bitsets are the same size since all N and M are <= 63.
// The side loop allows reversing the LHS and RHS roles.
TEST_F(BitSetTest, Contains5a) {
    int data[][2] = {
        {1, 2}, {2, 4}, {2, 6}, {3, 6}, {3, 9}, {7, 14}, {6, 18}, {7, 21},
        {6, 24}, {4, 28}, {11, 33}, {13, 39}, {6, 42}, {8, 56}, {9, 63}
    };

    for (size_t idx = 0; idx < sizeof(data)/sizeof(data[0]); idx++) {
        ASSERT_TRUE(data[idx][1] <= 63);
        ASSERT_TRUE(data[idx][1] % data[idx][0] == 0);
        BitSet bitset[2];
        for (int pos = 0; pos <= 1023; pos++) {
            if (pos % data[idx][0] == 0) bitset[0].set(pos);
            if (pos % data[idx][1] == 0) bitset[1].set(pos);
        }
        for (int side = 0; side < 2; side++) {
            bool expected = (side == 0);
            bool result = bitset[side].Contains(bitset[1-side]);
            EXPECT_EQ(result, expected);
        }
    }
}

// Run through several (N,M) tuples that set every Nth bit in LHS and every
// Mth bit in RHS, where M is not a multiple of N.
// The two bitsets are the same size since all N and M are <= 63.
// The side loop allows reversing the LHS and RHS roles.
TEST_F(BitSetTest, Contains5b) {
    int data[][2] = {
        {2, 3}, {3, 5}, {7, 9}, {3, 7}, {5, 6}, {4, 7}, {6, 10}, {5, 7},
        {7, 17}, {13, 17}, {19, 23}, {23, 29}, {37, 41}, {43, 47}, {53, 59}
    };

    for (size_t idx = 0; idx < sizeof(data)/sizeof(data[0]); idx++) {
        ASSERT_TRUE(data[idx][1] <= 63);
        ASSERT_TRUE(data[idx][1] % data[idx][0] != 0);
        BitSet bitset[2];
        for (int pos = 0; pos <= 1023; pos++) {
            if (pos % data[idx][0] == 0) bitset[0].set(pos);
            if (pos % data[idx][1] == 0) bitset[1].set(pos);
        }
        for (int side = 0; side < 2; side++) {
            EXPECT_FALSE(bitset[side].Contains(bitset[1-side]));
        }
    }
}

// Run through several (N,M) tuples that set every Nth bit in LHS and every
// Mth bit in RHS, where M is a multiple of N.
// The two bitsets are different sizes.
// The range loop allows different position ranges for the smaller bitset.
// The side loop allows reversing the LHS and RHS roles.
TEST_F(BitSetTest, Contains6a) {
    int data[][2] = {
        {1, 2}, {2, 4}, {2, 6}, {3, 6}, {3, 9}, {7, 14}, {6, 18}, {7, 21},
        {6, 24}, {4, 28}, {11, 33}, {13, 39}, {6, 42}, {8, 56}, {9, 63}
    };

    for (size_t idx = 0; idx < sizeof(data)/sizeof(data[0]); idx++) {
        ASSERT_TRUE(data[idx][1] <= 63);
        ASSERT_TRUE(data[idx][1] % data[idx][0] == 0);
        for (int range = 0; range <= 2; range++) {
            int pos_start = 256 * range;
            int pos_end = 256 * range + 511;
            BitSet bitset[2];

            for (int pos = 0; pos <= 1023; pos++) {
                if (pos % data[idx][0] == 0) bitset[0].set(pos);
            }

            for (int pos = pos_start; pos <= pos_end; pos++) {
                if (pos % data[idx][1] == 0) bitset[1].set(pos);
            }

            for (int side = 0; side < 2; side++) {
                bool expected = (side == 0);
                bool result = bitset[side].Contains(bitset[1-side]);
                EXPECT_EQ(result, expected);
            }
        }
    }
}

// Run through several (N,M) tuples that set every Nth bit in LHS and every
// Mth bit in RHS, where M is not a multiple of N.
// The two bitsets are different sizes.
// The range loop allows different position ranges for the smaller bitset.
// The side loop allows reversing the LHS and RHS roles.
TEST_F(BitSetTest, Contains6b) {
    int data[][2] = {
        {2, 3}, {3, 5}, {7, 9}, {3, 7}, {5, 6}, {4, 7}, {6, 10}, {5, 7},
        {7, 17}, {13, 17}, {19, 23}, {23, 29}, {37, 41}, {43, 47}, {53, 59}
    };

    for (size_t idx = 0; idx < sizeof(data)/sizeof(data[0]); idx++) {
        ASSERT_TRUE(data[idx][1] <= 63);
        ASSERT_TRUE(data[idx][1] % data[idx][0] != 0);
        for (int range = 0; range <= 2; range++) {
            int pos_start = 256 * range;
            int pos_end = 256 * range + 511;
            BitSet bitset[2];

            for (int pos = 0; pos <= 1023; pos++) {
                if (pos % data[idx][0] == 0) bitset[0].set(pos);
            }
            for (int pos = pos_start; pos <= pos_end; pos++) {
                if (pos % data[idx][1] == 0) bitset[1].set(pos);
            }
            for (int side = 0; side < 2; side++) {
                EXPECT_FALSE(bitset[side].Contains(bitset[1-side]));
            }
        }
    }
}


// Verify results for empty bitset.
TEST_F(BitSetTest, String1) {
    BitSet bitset, expected;
    string str = bitset.ToString();
    expected.FromString(str);
    EXPECT_EQ(bitset, expected);
}

// Run through bitsets of size 511 with every Nth bit set, N=[1,64].
TEST_F(BitSetTest, String2) {
    for (int num = 1; num <= 64; num++) {
        BitSet bitset, expected;
        for (int pos = 0; pos <= 511; pos++) {
            if (pos % num == 0) bitset.set(pos);
        }
        string str = bitset.ToString();
        expected.FromString(str);
        EXPECT_EQ(bitset, expected);
    }
}

TEST_F(BitSetTest, NumberedString1) {
    BitSet even_bitset, odd_bitset;
    for (int num = 0; num <= 11 ; num++) {
        if (num % 2 == 0) {
            even_bitset.set(num);
        } else {
            odd_bitset.set(num);
        }
    }

    EXPECT_EQ("0,2,4,6,8,10", even_bitset.ToNumberedString());
    EXPECT_EQ("1,3,5,7,9,11", odd_bitset.ToNumberedString());
}

TEST_F(BitSetTest, NumberedString2) {
    BitSet bitset;
    EXPECT_EQ("", bitset.ToNumberedString());
    bitset.set(1);
    EXPECT_EQ("1", bitset.ToNumberedString());
    bitset.set(3);
    EXPECT_EQ("1,3", bitset.ToNumberedString());
    bitset.set(2);
    EXPECT_EQ("1-3", bitset.ToNumberedString());
    bitset.reset(3);
    EXPECT_EQ("1-2", bitset.ToNumberedString());
    bitset.set(3);
    EXPECT_EQ("1-3", bitset.ToNumberedString());
    bitset.reset(1);
    EXPECT_EQ("2-3", bitset.ToNumberedString());
    bitset.set(1);
    EXPECT_EQ("1-3", bitset.ToNumberedString());
    bitset.set(5);
    EXPECT_EQ("1-3,5", bitset.ToNumberedString());
    bitset.set(7);
    EXPECT_EQ("1-3,5,7", bitset.ToNumberedString());
    bitset.set(9);
    EXPECT_EQ("1-3,5,7,9", bitset.ToNumberedString());
    bitset.set(8);
    EXPECT_EQ("1-3,5,7-9", bitset.ToNumberedString());
    bitset.set(11);
    EXPECT_EQ("1-3,5,7-9,11", bitset.ToNumberedString());
    bitset.reset(2);
    EXPECT_EQ("1,3,5,7-9,11", bitset.ToNumberedString());
    bitset.set(4);
    EXPECT_EQ("1,3-5,7-9,11", bitset.ToNumberedString());
    bitset.reset(11);
    EXPECT_EQ("1,3-5,7-9", bitset.ToNumberedString());
}

int main(int argc, char **argv) {
    LoggingInit();
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
