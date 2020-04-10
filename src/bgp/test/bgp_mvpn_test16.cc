/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp_mvpn_test.cc"

// Receive Type-5 source active, but no type-7 join is received at all.
TEST_P(BgpMvpnTest, Type3_SPMSI_6) {
    VerifyInitialState(preconfigure_pm_);
    for (size_t i = 1; i <= instances_set_count_; i++) {
        for (size_t j = 1; j <= groups_count_; j++) {
            AddType5MvpnRoute(red_[i-1], prefix5(i, j), getRouteTarget(i, "1"),
                              "10.1.1.1");
        }
    }

    if (!preconfigure_pm_) {
        VerifyInitialState(false, groups_count_, 0, groups_count_,
                           instances_set_count_*groups_count_, groups_count_,
                           0, groups_count_,
                           instances_set_count_*groups_count_);
        VerifyInitialState(true, groups_count_, 0, groups_count_,
                           instances_set_count_*groups_count_, groups_count_,
                           0, groups_count_,
                           instances_set_count_*groups_count_);
    } else {
        TASK_UTIL_EXPECT_EQ((4 + groups_count_)*instances_set_count_ + 1,
                            master_->Size());
        for (size_t i = 1; i <= instances_set_count_; i++) {
            // 1 local + 1 remote(red1)
            TASK_UTIL_EXPECT_EQ(groups_count_ + 1, red_[i-1]->Size());
            TASK_UTIL_EXPECT_EQ(1U, blue_[i - 1]->Size());  // 1 local
        }
    }
    for (size_t i = 1; i <= instances_set_count_; i++) {
        // 1 local + 2 remote(red1) + 1 remote(green1)
        TASK_UTIL_EXPECT_EQ(groups_count_+3, green_[i-1]->Size());
    }

    for (size_t i = 1; i <= instances_set_count_; i++)
        for (size_t j = 1; j <= groups_count_; j++)
            DeleteMvpnRoute(red_[i-1], prefix5(i,j));

    // 4 local-ad
    TASK_UTIL_EXPECT_EQ(4*instances_set_count_ + 1, master_->Size());
    for (size_t i = 1; i <= instances_set_count_; i++) {
        TASK_UTIL_EXPECT_EQ(1U, red_[i - 1]->Size());   // 1 local+ 1 join
        TASK_UTIL_EXPECT_EQ(1U, blue_[i - 1]->Size());  // 1 local
        // 1 local + 1 remote(red1) + 1 remote(blue1)
        TASK_UTIL_EXPECT_EQ(3U, green_[i - 1]->Size());
    }
}
