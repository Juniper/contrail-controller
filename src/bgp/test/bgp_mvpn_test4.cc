/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp_mvpn_test.cc"

// Add Type1AD route from a mock bgp peer into bgp.mvpn.0 table.
TEST_P(BgpMvpnTest, Type1AD_Remote) {
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

    // Inject a Type1 route from a mock peer into bgp.mvpn.0 table with red1
    // route-target.

    for (size_t i = 1; i <= instances_set_count_; i++) {
        // Verify that only green1 has discovered a neighbor from red1.
        TASK_UTIL_EXPECT_EQ(0U, red_[i - 1]->manager()->neighbors_count());
        TASK_UTIL_EXPECT_EQ(0U, blue_[i - 1]->manager()->neighbors_count());
        TASK_UTIL_EXPECT_EQ(2U, green_[i - 1]->manager()->neighbors_count());

        AddMvpnRoute(master_, prefix1(i), getRouteTarget(i, "1"));

        TASK_UTIL_EXPECT_EQ(2U,
                            red_[i - 1]->Size());  // 1 local + 1 remote(red1)
        TASK_UTIL_EXPECT_EQ(1U, blue_[i - 1]->Size());  // 1 local
        TASK_UTIL_EXPECT_EQ(4U,
                            green_[i - 1]->Size());  // 1 local + 1 remote(red1)

        // Verify that neighbor is detected.
        TASK_UTIL_EXPECT_EQ(1U, red_[i - 1]->manager()->neighbors_count());
        TASK_UTIL_EXPECT_EQ(0U, blue_[i - 1]->manager()->neighbors_count());
        TASK_UTIL_EXPECT_EQ(3U, green_[i - 1]->manager()->neighbors_count());

        MvpnNeighbor nbr;
        error_code err;
        ostringstream os;
        os << i;
        string is = os.str();

        EXPECT_TRUE(red_[i-1]->manager()->FindNeighbor(
                        RouteDistinguisher::FromString("10.1.1.1:"+is, &err),
                        &nbr));
        EXPECT_EQ(0U, nbr.source_as());
        EXPECT_EQ(IpAddress::from_string("9.8.7.6", err), nbr.originator());

        EXPECT_TRUE(green_[i-1]->manager()->FindNeighbor(
                        RouteDistinguisher::FromString("10.1.1.1:"+is, &err),
                        &nbr));
        EXPECT_EQ(0U, nbr.source_as());
        EXPECT_EQ(IpAddress::from_string("9.8.7.6", err), nbr.originator());
    }

    TASK_UTIL_EXPECT_EQ(5*instances_set_count_ + 1, master_->Size());
    for (size_t i = 1; i <= instances_set_count_; i++)
        DeleteMvpnRoute(master_, prefix1(i));

    // Verify that neighbor is deleted.
    TASK_UTIL_EXPECT_EQ(4*instances_set_count_ + 1, master_->Size());
    for (size_t i = 1; i <= instances_set_count_; i++) {
        TASK_UTIL_EXPECT_EQ(1U, red_[i - 1]->Size());   // 1 local
        TASK_UTIL_EXPECT_EQ(1U, blue_[i - 1]->Size());  // 1 local
        TASK_UTIL_EXPECT_EQ(3U,
                            green_[i - 1]->Size());  // 1 local+1 red1+1 blue1
        TASK_UTIL_EXPECT_EQ(0U, red_[i - 1]->manager()->neighbors_count());
        TASK_UTIL_EXPECT_EQ(0U, blue_[i - 1]->manager()->neighbors_count());
        TASK_UTIL_EXPECT_EQ(2U, green_[i - 1]->manager()->neighbors_count());
    }
}
