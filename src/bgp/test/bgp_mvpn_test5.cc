/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp_mvpn_test.cc"

// Add Type3 S-PMSI route and verify that Type4 Leaf-AD is not originated if
// PMSI information is not available for forwarding.
TEST_P(BgpMvpnTest, Type3_SPMSI_Without_ErmVpnRoute) {
    VerifyInitialState(preconfigure_pm_);

    // Inject Type3 route from a mock peer into bgp.mvpn.0 table with red1
    // route target. This route should go into red1 and green1 table.
    for (size_t i = 1; i <= instances_set_count_; i++)
        for (size_t j = 1; j <= groups_count_; j++)
            AddMvpnRoute(master_, prefix3(i, j), getRouteTarget(i, "1"), NULL,
                         true);

    if (!preconfigure_pm_) {
        TASK_UTIL_EXPECT_EQ(instances_set_count_*groups_count_,
                            master_->Size());
        for (size_t i = 1; i <= instances_set_count_; i++) {
            TASK_UTIL_EXPECT_EQ(groups_count_, red_[i-1]->Size());
            TASK_UTIL_EXPECT_EQ(0U, blue_[i - 1]->Size());
            TASK_UTIL_EXPECT_EQ(groups_count_, green_[i-1]->Size());
        }
        VerifyInitialState(true, groups_count_, 0, groups_count_,
                           instances_set_count_*groups_count_, groups_count_, 0,
                           groups_count_, instances_set_count_*groups_count_);
    }

    TASK_UTIL_EXPECT_EQ((4 + groups_count_)*instances_set_count_ + 1,
                        master_->Size());

    for (size_t i = 1; i <= instances_set_count_; i++) {
        // 1 local + groups_count_ remote(red1)
        TASK_UTIL_EXPECT_EQ(groups_count_+1, red_[i-1]->Size());
        TASK_UTIL_EXPECT_EQ(1U, blue_[i - 1]->Size());  // 1 local
        // 1 local + 2 remote(red1) + 1 remote(green1)
        TASK_UTIL_EXPECT_EQ(3 + 1*groups_count_, green_[i-1]->Size());
    }

    for (size_t i = 1; i <= instances_set_count_; i++)
        for (size_t j = 1; j <= groups_count_; j++)
            DeleteMvpnRoute(master_, prefix3(i, j));

    TASK_UTIL_EXPECT_EQ(4*instances_set_count_+1, master_->Size());

    for (size_t i = 1; i <= instances_set_count_; i++) {
        TASK_UTIL_EXPECT_EQ(1U, red_[i - 1]->Size());   // 1 local
        TASK_UTIL_EXPECT_EQ(1U, blue_[i - 1]->Size());  // 1 local
        // 1 local + 1 remote(red1) + 1 remote(blue1)
        TASK_UTIL_EXPECT_EQ(3U, green_[i - 1]->Size());
    }
}
