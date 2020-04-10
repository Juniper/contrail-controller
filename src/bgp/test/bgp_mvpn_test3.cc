/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp_mvpn_test.cc"

// Reset BGP Identifier and ensure that Type1 route is no longer generated.
TEST_P(BgpMvpnTest, Type1ADLocalWithIdentifierRemoved) {
    if (!preconfigure_pm_) {
        for (size_t i = 1; i <= instances_set_count_; i++) {
            TASK_UTIL_EXPECT_EQ(static_cast<MvpnRoute *>(NULL),
                                red_[i-1]->FindType1ADRoute());
            TASK_UTIL_EXPECT_EQ(static_cast<MvpnRoute *>(NULL),
                                blue_[i-1]->FindType1ADRoute());
            TASK_UTIL_EXPECT_EQ(static_cast<MvpnRoute *>(NULL),
                                green_[i-1]->FindType1ADRoute());
        }
    }
    VerifyInitialState();
    error_code err;
    UpdateBgpIdentifier("0.0.0.0");
    TASK_UTIL_EXPECT_EQ(0U, master_->Size());

    for (size_t i = 1; i <= instances_set_count_; i++) {
        TASK_UTIL_EXPECT_EQ(0U, red_[i - 1]->Size());
        TASK_UTIL_EXPECT_EQ(static_cast<MvpnRoute *>(NULL),
                            red_[i-1]->FindType1ADRoute());

        TASK_UTIL_EXPECT_EQ(0U, blue_[i - 1]->Size());
        TASK_UTIL_EXPECT_EQ(static_cast<MvpnRoute *>(NULL),
                            blue_[i-1]->FindType1ADRoute());

        TASK_UTIL_EXPECT_EQ(0U,
                            green_[i - 1]->Size());  // 1 green1+1 red1+1 blue1
        TASK_UTIL_EXPECT_EQ(static_cast<MvpnRoute *>(NULL),
                            green_[i-1]->FindType1ADRoute());

        // Verify that only green1 has discovered a neighbor from red1.
        TASK_UTIL_EXPECT_EQ(0U, red_[i - 1]->manager()->neighbors_count());
        TASK_UTIL_EXPECT_EQ(0U, blue_[i - 1]->manager()->neighbors_count());
        TASK_UTIL_EXPECT_EQ(0U, green_[i - 1]->manager()->neighbors_count());
    }
}
