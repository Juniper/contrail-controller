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
    error_code err;
    UpdateBgpIdentifier("127.0.0.2");
    TASK_UTIL_EXPECT_EQ(4*instances_set_count_ + 1, master_->Size());

    for (size_t i = 1; i <= instances_set_count_; i++) {
        TASK_UTIL_EXPECT_EQ(1, red_[i-1]->Size());
        TASK_UTIL_EXPECT_NE(static_cast<MvpnRoute *>(NULL),
                            red_[i-1]->FindType1ADRoute());

        TASK_UTIL_EXPECT_EQ(1, blue_[i-1]->Size());
        TASK_UTIL_EXPECT_NE(static_cast<MvpnRoute *>(NULL),
                            blue_[i-1]->FindType1ADRoute());

        TASK_UTIL_EXPECT_EQ(3, green_[i-1]->Size()); // 1 green1+1 red1+1 blue1
        TASK_UTIL_EXPECT_NE(static_cast<MvpnRoute *>(NULL),
                            green_[i-1]->FindType1ADRoute());

        // Verify that only green1 has discovered a neighbor from red1.
        TASK_UTIL_EXPECT_EQ(0, red_[i-1]->manager()->neighbors_count());
        TASK_UTIL_EXPECT_EQ(0, blue_[i-1]->manager()->neighbors_count());
        TASK_UTIL_EXPECT_EQ(2, green_[i-1]->manager()->neighbors_count());

        MvpnNeighbor neighbor;
        EXPECT_TRUE(green_[i-1]->manager()->FindNeighbor(
                        *(red_[i-1]->routing_instance()->GetRD()), &neighbor));
        EXPECT_EQ(*(red_[i-1]->routing_instance()->GetRD()), neighbor.rd());
        EXPECT_EQ(0, neighbor.source_as());
        EXPECT_EQ(IpAddress::from_string("127.0.0.2", err),
                  neighbor.originator());

        EXPECT_TRUE(green_[i-1]->manager()->FindNeighbor(
                        *(blue_[i-1]->routing_instance()->GetRD()), &neighbor));
        EXPECT_EQ(*(blue_[i-1]->routing_instance()->GetRD()), neighbor.rd());
        EXPECT_EQ(0, neighbor.source_as());
        EXPECT_EQ(IpAddress::from_string("127.0.0.2", err),
                  neighbor.originator());
    }
}
