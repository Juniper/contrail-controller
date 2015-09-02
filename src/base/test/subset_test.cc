/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/subset.h"

#include <map>
#include <sstream>
#include <vector>

#include "base/logging.h"
#include "testing/gunit.h"

using namespace std;

class SubsetTest : public ::testing::Test {
protected:
    void Display(const vector<int> &vec) const {
        stringstream strs;
        for (size_t i = 0; i < vec.size(); i++) {
            if (i > 0) {
                strs << " ";
            }
            strs << vec[i];
        }
        LOG(DEBUG, strs.str());
    }

    vector<int> group_;
};

#define arraysize(Arr)  sizeof(Arr)/sizeof(Arr[0])

TEST_F(SubsetTest, Basic)  {
    typedef map<vector<int>, vector<int> > PairMap;
    static const int values[] = {
        1, 2, 3, 4, 5, 6
    };
    for (size_t i = 0; i < arraysize(values); i++) {
        group_.push_back(values[i]);
    }

    PairMap pairs;
    SubsetGenerator<vector<int> > generator(group_);
    int count = 0;
    int unique = 0;
    while (generator.HasNext()) {
        vector<int> lhs;
        vector<int> rhs;
        generator.Next(&lhs, &rhs);
        pairs.insert(make_pair(rhs, lhs));
        LOG(DEBUG, "Permutation " << count);
        Display(lhs);
        Display(rhs);
        if (pairs.find(lhs) != pairs.end()) {
            LOG(DEBUG, "Duplicate");
        } else {
            unique++;
        }
        count++;
    }
    EXPECT_EQ(unique, count);
}

TEST_F(SubsetTest, Random)  {
    typedef set<vector<int> > SetSet;
    static const int values[] = {
        2, 4, 6, 8, 10, 12, 14
    };
    for (size_t i = 0; i < arraysize(values); i++) {
        group_.push_back(values[i]);
    }
    SetSet sets;
    SubsetGenerator<vector<int> > generator(group_);
    while (generator.HasNext()) {
        vector<int> lhs;
        vector<int> rhs;
        generator.Next(&lhs, &rhs);
        pair<SetSet::iterator, bool> result = sets.insert(lhs);
        EXPECT_TRUE(result.second);
        result = sets.insert(rhs);
        EXPECT_TRUE(result.second);
    }
    for (int i = 0; i < 128; i++) {
        vector<int> testset;
        for (size_t j = 0; j < arraysize(values); j++) {
#ifdef __APPLE__
            if (arc4random_uniform(2) > 0) {
#else
            if (rand()%2 > 0) {
#endif
                testset.push_back(values[j]);
            }
        }
        if (testset.empty()) continue;
        if (testset.size() == arraysize(values)) continue;
        Display(testset);
        EXPECT_TRUE(sets.find(testset) != sets.end());
    }
}

TEST_F(SubsetTest, One)  {
    static const int values[] = {
        1
    };
    for (size_t i = 0; i < arraysize(values); i++) {
        group_.push_back(values[i]);
    }
    SubsetGenerator<vector<int> > generator(group_);
    int count = 0;
    while (generator.HasNext()) {
        vector<int> lhs;
        vector<int> rhs;
        generator.Next(&lhs, &rhs);
        count++;
    }
    EXPECT_EQ(0, count);
}

TEST_F(SubsetTest, Two)  {
    static const int values[] = {
        3, 4
    };
    for (size_t i = 0; i < arraysize(values); i++) {
        group_.push_back(values[i]);
    }
    SubsetGenerator<vector<int> > generator(group_);
    int count = 0;
    while (generator.HasNext()) {
        vector<int> lhs;
        vector<int> rhs;
        generator.Next(&lhs, &rhs);
        count++;
    }
    EXPECT_EQ(1, count);
}

int main(int argc, char **argv) {
    LoggingInit();
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
