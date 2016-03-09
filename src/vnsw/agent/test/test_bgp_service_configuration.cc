/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#include "base/os.h"
#include <boost/assign/list_of.hpp>

#include <cfg/cfg_init.h>
#include <cfg/cfg_interface.h>
#include <oper/operdb_init.h>
#include <controller/controller_init.h>
#include <pkt/pkt_init.h>
#include <services/services_init.h>
#include <vrouter/ksync/ksync_init.h>
#include <cmn/agent_cmn.h>
#include <base/task.h>
#include <io/event_manager.h>
#include <base/util.h>
#include <ifmap/ifmap_agent_parser.h>
#include <ifmap/ifmap_agent_table.h>
#include <oper/vn.h>
#include <oper/vm.h>
#include <oper/interface_common.h>
#include <oper/bgp_as_service.h>

#include "testing/gunit.h"
#include "test_cmn_util.h"
#include "vr_types.h"

using namespace std;
using namespace boost::assign;

void RouterIdDepInit(Agent *agent) {
}

static void ValidateSandeshResponse(Sandesh *sandesh, vector<int> &result) {
    //TBD
    //Validate the response by the expectation
}

class BgpCfgTest : public ::testing::Test {
public:    
    BgpCfgTest() : agent_(Agent::GetInstance()) {
    }

    Agent *agent_;
};

TEST_F(BgpCfgTest, Test_1) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.10", "00:00:01:01:01:10", 1, 1},
    };

    client->Reset();
    CreateVmportEnv(input, 1);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input, 0));
    uint32_t bgp_service_count =
        agent_->oper_db()->bgp_as_a_service()->bgp_as_a_service_map().size();
    EXPECT_TRUE(bgp_service_count == 0);
    SendBgpServiceConfig("1.1.1.10", 50000, 1, "vnet1", "vrf1", "bgpaas-client",
                         false);
    bgp_service_count =
        agent_->oper_db()->bgp_as_a_service()->bgp_as_a_service_map().size();
    EXPECT_TRUE(bgp_service_count == 1);

    //Delete
    SendBgpServiceConfig("1.1.1.10", 50000, 1, "vnet1", "vrf1", "bgpaas-client",
                         true);
    DeleteVmportEnv(input, 1, true);
    client->WaitForIdle();
    bgp_service_count =
        agent_->oper_db()->bgp_as_a_service()->bgp_as_a_service_map().size();
    EXPECT_TRUE(bgp_service_count == 0);
    EXPECT_FALSE(VmPortActive(input, 0));
}

TEST_F(BgpCfgTest, Test_2) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.10", "00:00:01:01:01:10", 1, 1},
    };

    client->Reset();
    CreateVmportEnv(input, 1);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input, 0));
    uint32_t bgp_service_count =
        agent_->oper_db()->bgp_as_a_service()->bgp_as_a_service_map().size();
    EXPECT_TRUE(bgp_service_count == 0);
    SendBgpServiceConfig("1.1.1.10", 50000, 1, "vnet1", "vrf1", "bgpaas-server",
                         false);
    bgp_service_count =
        agent_->oper_db()->bgp_as_a_service()->bgp_as_a_service_map().size();
    EXPECT_TRUE(bgp_service_count == 0);

    //Delete
    SendBgpServiceConfig("1.1.1.10", 50000, 1, "vnet1", "vrf1", "bgpaas-server",
                         true);
    DeleteVmportEnv(input, 1, true);
    client->WaitForIdle();
    bgp_service_count =
        agent_->oper_db()->bgp_as_a_service()->bgp_as_a_service_map().size();
    EXPECT_TRUE(bgp_service_count == 0);
    EXPECT_FALSE(VmPortActive(input, 0));
}

int main(int argc, char **argv) {
    GETUSERARGS();

    client = TestInit(init_file, ksync_init);
    int ret = RUN_ALL_TESTS();
    client->WaitForIdle();
    TestShutdown();
    delete client;
    return ret;
}
