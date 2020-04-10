/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp_mvpn_test.cc"

// Add Type3 S-PMSI route and verify that Type4 Leaf-AD gets originated with the
// right set of path attributes.
TEST_P(BgpMvpnTest, Type3_SPMSI_With_ErmVpnRoute) {
    VerifyInitialState(preconfigure_pm_);
    // Inject Type3 route from a mock peer into bgp.mvpn.0 table with red1 route
    // target. This route should go into red1 and green1 table.
    ErmVpnRoute *ermvpn_rt[instances_set_count_*groups_count_];
    for (size_t i = 1; i <= instances_set_count_; i++) {
        for (size_t j = 1; j <= groups_count_; j++) {
            tbb::mutex::scoped_lock lock(pmsi_params_mutex);
            ermvpn_rt[(i-1)*groups_count_+(j-1)] = NULL;
            PMSIParams pmsi(PMSIParams(10, "1.2.3.4", "gre",
                            &ermvpn_rt[(i-1)*groups_count_+(j-1)]));
            pmsi_params.insert(make_pair(sg(i, j), pmsi));
            lock.release();
            AddMvpnRoute(red_[i-1], native_prefix7(j), getRouteTarget(i, "1"));
            AddMvpnRoute(green_[i-1], native_prefix7(j), getRouteTarget(i, "3"));
            AddMvpnRoute(master_, prefix3(i, j), getRouteTarget(i, "1"), NULL,
                    true);
        }
    }

    if (!preconfigure_pm_) {
        TASK_UTIL_EXPECT_EQ(instances_set_count_*groups_count_,
                            master_->Size());
        for (size_t i = 1; i <= instances_set_count_; i++) {
            TASK_UTIL_EXPECT_EQ(groups_count_*2, red_[i-1]->Size());
            TASK_UTIL_EXPECT_EQ(0U, blue_[i - 1]->Size());
            TASK_UTIL_EXPECT_EQ(groups_count_*2, green_[i-1]->Size());
        }
        VerifyInitialState(true, groups_count_*2, 0, groups_count_*2,
                   instances_set_count_*groups_count_, groups_count_*2,
                   0, groups_count_*2, instances_set_count_*groups_count_);
    }

    TASK_UTIL_EXPECT_EQ((4 + groups_count_)*instances_set_count_ + 1,
                        master_->Size());

    for (size_t i = 1; i <= instances_set_count_; i++) {
        for (size_t j = 1; j <= groups_count_; j++) {
            ErmVpnRoute *rt =
                AddErmVpnRoute(fabric_ermvpn_[i-1], ermvpn_prefix(i, j),
                               "target:127.0.0.1:1100");
            tbb::mutex::scoped_lock lock(pmsi_params_mutex);
            ermvpn_rt[(i-1)*groups_count_+(j-1)] = rt;
        }
    }

    for (size_t i = 1; i <= instances_set_count_; i++) {
        // 1 local+1 remote(red1)+1 leaf-ad
        TASK_UTIL_EXPECT_EQ(1 + 3*groups_count_, red_[i-1]->Size());
        TASK_UTIL_EXPECT_EQ(1U, blue_[i - 1]->Size());  // 1 local
        // 1 local + 1 remote(red1) + 1 remote(green1) + // AD
        // 1 red-spmsi + 1 red-leafad
        TASK_UTIL_EXPECT_EQ(3 + 3*groups_count_, green_[i-1]->Size());
        for (size_t j = 1; j <= groups_count_; j++) {
            // Lookup the actual leaf-ad route and verify its attributes.
            VerifyLeafADMvpnRoute(red_[i-1], prefix3(i, j),
                                  pmsi_params[sg(i, j)]);
            VerifyLeafADMvpnRoute(green_[i-1], prefix3(i, j),
                                  pmsi_params[sg(i, j)]);
        }
    }

    for (size_t i = 1; i <= instances_set_count_; i++) {
        for (size_t j = 1; j <= groups_count_; j++) {
            // Setup ermvpn route before type 3 spmsi route is added.
            DeleteMvpnRoute(master_, prefix3(i, j));
            DeleteMvpnRoute(red_[i-1], native_prefix7(j));
            DeleteMvpnRoute(green_[i-1], native_prefix7(j));
            {
                tbb::mutex::scoped_lock lock(pmsi_params_mutex);
                pmsi_params.erase(sg(i, j));
            }
            DeleteErmVpnRoute(fabric_ermvpn_[i-1], ermvpn_prefix(i, j));
        }
    }

    TASK_UTIL_EXPECT_EQ(4*instances_set_count_ + 1, master_->Size()); // 3 local
    for (size_t i = 1; i <= instances_set_count_; i++) {
        TASK_UTIL_EXPECT_EQ(1U, red_[i - 1]->Size());   // 1 local
        TASK_UTIL_EXPECT_EQ(1U, blue_[i - 1]->Size());  // 1 local
        // 1 local + 1 remote(red1) + 1 remote(blue1)
        TASK_UTIL_EXPECT_EQ(3U, green_[i - 1]->Size());
    }
}

// Add Type3 S-PMSI route and verify that Type4 Leaf-AD does not generate
// since LeafInfoRequired is not set.
TEST_P(BgpMvpnTest, Type3_SPMSI_Without_LeafInfoRequired) {
    VerifyInitialState(preconfigure_pm_);
    // Inject Type3 route from a mock peer into bgp.mvpn.0 table with red1 route
    // target. This route should go into red1 and green1 table.
    ErmVpnRoute *ermvpn_rt[instances_set_count_*groups_count_];
    for (size_t i = 1; i <= instances_set_count_; i++) {
        for (size_t j = 1; j <= groups_count_; j++) {
            tbb::mutex::scoped_lock lock(pmsi_params_mutex);
            ermvpn_rt[(i-1)*groups_count_+(j-1)] = NULL;
            PMSIParams pmsi(PMSIParams(10, "1.2.3.4", "gre",
                            &ermvpn_rt[(i-1)*groups_count_+(j-1)]));
            pmsi_params.insert(make_pair(sg(i, j), pmsi));
            lock.release();
            AddMvpnRoute(master_, prefix3(i,j), getRouteTarget(i, "1"));
        }
    }

    if (!preconfigure_pm_) {
        TASK_UTIL_EXPECT_EQ(instances_set_count_*groups_count_,
                            master_->Size());
        for (size_t i = 1; i <= instances_set_count_; i++) {
            TASK_UTIL_EXPECT_EQ(groups_count_, red_[i-1]->Size()); // 1 remote
            TASK_UTIL_EXPECT_EQ(0U, blue_[i - 1]->Size());
            TASK_UTIL_EXPECT_EQ(groups_count_, green_[i-1]->Size()); // 1 remote
        }
        VerifyInitialState(true, groups_count_, 0, groups_count_,
                           instances_set_count_*groups_count_, groups_count_, 0,
                           groups_count_, instances_set_count_*groups_count_);
    }

    TASK_UTIL_EXPECT_EQ((4 + groups_count_)*instances_set_count_ + 1,
                        master_->Size());

    for (size_t i = 1; i <= instances_set_count_; i++) {
        for (size_t j = 1; j <= groups_count_; j++) {
            ErmVpnRoute *rt =
                AddErmVpnRoute(fabric_ermvpn_[i-1], ermvpn_prefix(i, j),
                               "target:127.0.0.1:1100");
            tbb::mutex::scoped_lock lock(pmsi_params_mutex);
            ermvpn_rt[(i-1)*groups_count_+(j-1)] = rt;
        }
    }

    for (size_t i = 1; i <= instances_set_count_; i++) {
        // 1 local+1 remote(red1)
        TASK_UTIL_EXPECT_EQ(1 + groups_count_, red_[i-1]->Size());
        TASK_UTIL_EXPECT_EQ(1U, blue_[i - 1]->Size());  // 1 local
        // 1 local + 1 remote(red1) + 1 remote(green1)
        TASK_UTIL_EXPECT_EQ(3 + groups_count_, green_[i-1]->Size());
    }

    for (size_t i = 1; i <= instances_set_count_; i++) {
        for (size_t j = 1; j <= groups_count_; j++) {
            // Setup ermvpn route before type 3 spmsi route is added.
            DeleteMvpnRoute(master_, prefix3(i, j));
            {
                tbb::mutex::scoped_lock lock(pmsi_params_mutex);
                pmsi_params.erase(sg(i, j));
            }
            DeleteErmVpnRoute(fabric_ermvpn_[i-1], ermvpn_prefix(i, j));
        }
    }

    TASK_UTIL_EXPECT_EQ(4*instances_set_count_ + 1, master_->Size()); // 3 local
    for (size_t i = 1; i <= instances_set_count_; i++) {
        TASK_UTIL_EXPECT_EQ(1U, red_[i - 1]->Size());   // 1 local
        TASK_UTIL_EXPECT_EQ(1U, blue_[i - 1]->Size());  // 1 local
        // 1 local + 1 remote(red1) + 1 remote(blue1)
        TASK_UTIL_EXPECT_EQ(3U, green_[i - 1]->Size());
    }
}
