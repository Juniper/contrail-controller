/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp_mvpn_test.cc"

// Receive Type-7 join route and ensure that Type-3 S-PMSI is generated.
// Type-7 join comes in first followed by Type-5 source-active
// Type-7 join route gets deleted first, afterwards.
TEST_P(BgpMvpnTest, Type3_SPMSI_4) {
    VerifyInitialState(true);
    // Inject type-7 receiver route with red1 RI vit. There is no source-active
    // route yet, hence no type-3 s-pmsi should be generated.
    for (size_t i = 1; i <= instances_set_count_; i++) {
        for (size_t j = 1; j <= groups_count_; j++) {
            AddMvpnRoute(master_, prefix7(i,j), "target:127.0.0.1:" +
                integerToString(red_[i-1]->routing_instance()->index()));
        }
    }

    // 4 local-ad + 1 remote-join
    TASK_UTIL_EXPECT_EQ((4+groups_count_)*instances_set_count_ + 1,
                        master_->Size());
    for (size_t i = 1; i <= instances_set_count_; i++) {
        // 1 local-ad + 1 remote-sa(red1)
        TASK_UTIL_EXPECT_EQ(1+groups_count_, red_[i-1]->Size());
        TASK_UTIL_EXPECT_EQ(1U, blue_[i - 1]->Size());  // 1 local
        // 1 local-ad + 1 remote-ad(red1) + 1 remote-ad(blue1)
        TASK_UTIL_EXPECT_EQ(3U, green_[i - 1]->Size());
    }

    // Now inject a remote type5.
    for (size_t i = 1; i <= instances_set_count_; i++) {
        for (size_t j = 1; j <= groups_count_; j++) {
            AddType5MvpnRoute(red_[i-1], prefix5(i, j), getRouteTarget(i, "1"),
                              "10.1.1.1");
        }
    }

    // Route should go only into red_ which has the source-active route. This
    // should cause a Type3 S-PMSI route to be originated. This route will get
    // imported into green1 but no type-4 will get generated as there is no
    // active receiver agent joined yet.

    // 4 local-ad + 1 remote-sa + 1 remote-join + 1 local-spmsi
    TASK_UTIL_EXPECT_EQ((4 + 3*groups_count_)*instances_set_count_ + 1,
        master_->Size());
    for (size_t i = 1; i <= instances_set_count_; i++) {
        // 1 local-ad + 1 remote-sa(red1)+1 remote-join + 1 spmsi
        TASK_UTIL_EXPECT_EQ(1 + 3*groups_count_, red_[i-1]->Size());
        TASK_UTIL_EXPECT_EQ(1U, blue_[i - 1]->Size());  // 1 local
        // 1 local-ad + 1 remote-ad(red1) + 1 remote-ad(blue1) +
        // 1 remote-sa(red) + 1 spmsi(red1)
        TASK_UTIL_EXPECT_EQ(3 + 2*groups_count_, green_[i-1]->Size());
    }

    // Remove join route.
    for (size_t i = 1; i <= instances_set_count_; i++)
        for (size_t j = 1; j <= groups_count_; j++)
            DeleteMvpnRoute(master_, prefix7(i, j));

    TASK_UTIL_EXPECT_EQ((4 + groups_count_)*instances_set_count_ + 1,
                        master_->Size());
    for (size_t i = 1; i <= instances_set_count_; i++) {
        // 1 local + 1 remote(red1)
        TASK_UTIL_EXPECT_EQ(groups_count_ + 1, red_[i-1]->Size());
        TASK_UTIL_EXPECT_EQ(1U, blue_[i - 1]->Size());  // 1 local
        // 1 local + 2 remote(red1) + 1 remote(green1)
        TASK_UTIL_EXPECT_EQ(groups_count_+3, green_[i-1]->Size());
    }

    // Remove source-active route.
    for (size_t i = 1; i <= instances_set_count_; i++)
        for (size_t j = 1; j <= groups_count_; j++)
            DeleteMvpnRoute(red_[i-1], prefix5(i, j));

    // 4 local-ad
    TASK_UTIL_EXPECT_EQ(4*instances_set_count_ + 1, master_->Size());
    for (size_t i = 1; i <= instances_set_count_; i++) {
        TASK_UTIL_EXPECT_EQ(1U, red_[i - 1]->Size());   // 1 local+ 1 join
        TASK_UTIL_EXPECT_EQ(1U, blue_[i - 1]->Size());  // 1 local
        // 1 local + 1 remote(red1) + 1 remote(blue1)
        TASK_UTIL_EXPECT_EQ(3U, green_[i - 1]->Size());
    }
}
