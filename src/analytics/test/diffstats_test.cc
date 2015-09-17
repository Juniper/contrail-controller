//
// Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
//

#include <testing/gunit.h>

#include <boost/ptr_container/ptr_map.hpp>
#include <analytics/diffstats.h>
#include "diffstats_test_types.h"

namespace {

struct TestMsgStats {
    TestMsgStats() :
        messages_(0)  {
    }
    void Get(const std::string &key, TestMsgOutputStats *ostats) const {
        ostats->name = key;
        ostats->messages = messages_;
    }
    friend TestMsgStats operator+(const TestMsgStats &a, const TestMsgStats &b);
    friend TestMsgStats operator-(const TestMsgStats &a, const TestMsgStats &b);
    uint64_t messages_;
};

bool operator==(const TestMsgStats& lhs, const TestMsgStats& rhs) {
    return lhs.messages_ == rhs.messages_;
}

TestMsgStats operator+(const TestMsgStats &a, const TestMsgStats &b) {
    TestMsgStats sum;
    sum.messages_ = a.messages_ + b.messages_;
    return sum;
}

TestMsgStats operator-(const TestMsgStats &a, const TestMsgStats &b) {
    TestMsgStats diff;
    diff.messages_ = a.messages_ - b.messages_;
    return diff;
}

typedef boost::ptr_map<std::string, TestMsgStats> TestMsgMap;

class DiffStatsTest : public ::testing::Test {
 protected:
    void PopulateMsgs(TestMsgMap *map,
        std::vector<TestMsgOutputStats> *v_ostats,
        int start_num_types, int num_types, int num_messages_per_type,
        int diff_messages_per_type) {
        for (int i = start_num_types; i < start_num_types + num_types; i++) {
           std::stringstream ms;
           ms << msg_prefix << i;
           std::string msg_name(ms.str());
           TestMsgStats *mstats(new TestMsgStats);
           ASSERT_TRUE(mstats != NULL);
           mstats->messages_ = num_messages_per_type;
           map->insert(msg_name, mstats);
           TestMsgOutputStats ostats;
           ostats.name = msg_name;
           ostats.messages = diff_messages_per_type;
           v_ostats->push_back(ostats);
        }
    }

    static const std::string msg_prefix;
    TestMsgMap test_map_;
    TestMsgMap old_test_map_;
};

const std::string DiffStatsTest::msg_prefix("TestMsg");

TEST_F(DiffStatsTest, Basic) {
    // CASE 1: Empty old map
    EXPECT_TRUE(old_test_map_.empty());
    std::vector<TestMsgOutputStats> exp_v_ostats;
    std::vector<TestMsgOutputStats> act_v_ostats;
    PopulateMsgs(&test_map_, &exp_v_ostats, 0, 3, 4, 4);
    TestMsgMap temp_map1(test_map_);
    TestMsgMap e_old_test_map(temp_map1);
    GetDiffStats<TestMsgMap, std::string, TestMsgStats, TestMsgOutputStats>(
        &test_map_, &old_test_map_, &act_v_ostats);
    EXPECT_EQ(exp_v_ostats, act_v_ostats);
    EXPECT_EQ(e_old_test_map, old_test_map_);
    EXPECT_TRUE(test_map_.empty());
    exp_v_ostats.clear();
    act_v_ostats.clear();
    e_old_test_map.clear();
    // CASE 2: Add new entries
    PopulateMsgs(&test_map_, &exp_v_ostats, 3, 1, 5, 5);
    TestMsgMap temp_map2(test_map_);
    e_old_test_map.insert(test_map_.begin(), test_map_.end());
    e_old_test_map.insert(temp_map1.begin(), temp_map1.end());
    GetDiffStats<TestMsgMap, std::string, TestMsgStats, TestMsgOutputStats>(
        &test_map_, &old_test_map_, &act_v_ostats);
    EXPECT_EQ(exp_v_ostats, act_v_ostats);
    EXPECT_EQ(e_old_test_map, old_test_map_);
    EXPECT_TRUE(test_map_.empty());
    exp_v_ostats.clear();
    act_v_ostats.clear();
    e_old_test_map.clear();
    // CASE 3: Add to existing entry
    PopulateMsgs(&test_map_, &exp_v_ostats, 0, 3, 8, 4);
    TestMsgMap temp_map3(test_map_);
    e_old_test_map.insert(test_map_.begin(), test_map_.end());
    e_old_test_map.insert(temp_map2.begin(), temp_map2.end());
    GetDiffStats<TestMsgMap, std::string, TestMsgStats, TestMsgOutputStats>(
        &test_map_, &old_test_map_, &act_v_ostats);
    EXPECT_EQ(exp_v_ostats, act_v_ostats);
    EXPECT_EQ(e_old_test_map, old_test_map_);
    EXPECT_TRUE(test_map_.empty());
    exp_v_ostats.clear();
    act_v_ostats.clear();
    e_old_test_map.clear();
    // CASE 4: Empty new map
    e_old_test_map.insert(old_test_map_.begin(), old_test_map_.end());
    GetDiffStats<TestMsgMap, std::string, TestMsgStats, TestMsgOutputStats>(
        &test_map_, &old_test_map_, &act_v_ostats);
    EXPECT_EQ(exp_v_ostats, act_v_ostats);
    EXPECT_EQ(e_old_test_map, old_test_map_);
    EXPECT_TRUE(test_map_.empty());
    exp_v_ostats.clear();
    act_v_ostats.clear();
    e_old_test_map.clear();
}

} // namespace

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    LoggingInit();
    int result = RUN_ALL_TESTS();
    return result;
}
