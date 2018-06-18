/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp_mvpn_test.cc"

// Change Identifier and ensure that routes have updated originator id.
TEST_P(BgpMvpnTest, Type1ADLocalWithIdentifierChanged) {
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
    UpdateBgpIdentifier("127.0.0.2");
    TASK_UTIL_EXPECT_EQ(4*instances_set_count_ + 1, master_->Size());

    // Retry Type1 route checks as router-id update processing is executed
    // completely asynchronously.
    for (size_t i = 1; i <= instances_set_count_; i++)
        TASK_UTIL_EXPECT_TRUE(CheckMvpnNeighborRoute(i));
}
