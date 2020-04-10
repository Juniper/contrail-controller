/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp_mvpn_test.cc"

// Receive Type-7 remote join, but no type-5 source-active is received at all.
TEST_P(BgpMvpnTest, Type3_SPMSI_5) {
    VerifyInitialState(preconfigure_pm_);
    // Inject type-7 receiver route with red1 RI vit. There is no source-active
    // route yet, hence no type-3 s-pmsi should be generated.
    for (size_t i = 1; i <= instances_set_count_; i++) {
        for (size_t j = 1; j <= groups_count_; j++) {
            AddMvpnRoute(master_, prefix7(i,j), "target:127.0.0.1:" +
                integerToString(red_[i-1]->routing_instance()->index()));
        }
    }

    if (!preconfigure_pm_) {
        VerifyInitialState(false, groups_count_, 0, 1,
                           instances_set_count_*groups_count_,
                           groups_count_, 0, 0,
                           instances_set_count_*groups_count_);
        VerifyInitialState(true, groups_count_, 0, 0,
                           instances_set_count_*groups_count_,
                           groups_count_, 0, 0,
                           instances_set_count_*groups_count_);
    } else {
        // 4 local-ad + 1 remote-join
        TASK_UTIL_EXPECT_EQ((4+groups_count_)*instances_set_count_ + 1,
                            master_->Size());
        for (size_t i = 1; i <= instances_set_count_; i++) {
            // 1 local-ad + 1 remote-sa(red1)
            TASK_UTIL_EXPECT_EQ(1+groups_count_, red_[i-1]->Size());
            TASK_UTIL_EXPECT_EQ(1U, blue_[i - 1]->Size());  // 1 local
        }
    }

    for (size_t i = 1; i <= instances_set_count_; i++) {
        // 1 local-ad + 1 remote-ad(red1) + 1 remote-ad(blue1)
        TASK_UTIL_EXPECT_EQ(3U, green_[i - 1]->Size());
    }

    for (size_t i = 1; i <= instances_set_count_; i++)
        for (size_t j = 1; j <= groups_count_; j++)
            DeleteMvpnRoute(master_, prefix7(i, j));

    // 4 local-ad
    TASK_UTIL_EXPECT_EQ(4*instances_set_count_ + 1, master_->Size());
    for (size_t i = 1; i <= instances_set_count_; i++) {
        TASK_UTIL_EXPECT_EQ(1U, red_[i - 1]->Size());   // 1 local+ 1 join
        TASK_UTIL_EXPECT_EQ(1U, blue_[i - 1]->Size());  // 1 local
        // 1 local + 1 remote(red1) + 1 remote(blue1)
        TASK_UTIL_EXPECT_EQ(3U, green_[i - 1]->Size());
    }
}
