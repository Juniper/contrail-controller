/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/os.h"
#include <boost/assign/list_of.hpp>

#include <cfg/cfg_init.h>
#include <cfg/cfg_interface.h>
#include <oper/operdb_init.h>
#include <controller/controller_init.h>
#include <pkt/pkt_init.h>
#include <services/services_init.h>
#include <ksync/ksync_init.h>
#include <cmn/agent_cmn.h>
#include <base/task.h>
#include <io/event_manager.h>
#include <base/util.h>
#include <ifmap/ifmap_agent_parser.h>
#include <ifmap/ifmap_agent_table.h>
#include <oper/vn.h>
#include <oper/vm.h>
#include <oper/interface_common.h>

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

class CfgTest : public ::testing::Test {
public:
    CfgTest() : default_tunnel_type_(TunnelType::MPLS_GRE) { };
    ~CfgTest() { };

    TunnelType::Type default_tunnel_type_;
};

TEST_F(CfgTest, Global_vxlan_network_identifier_mode_config) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.10", "00:00:01:01:01:10", 1, 1},
    };

    client->Reset();
    AddEncapList("VXLAN", "MPLSoGRE", "MPLSoUDP");
    CreateVmportEnv(input, 1);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input, 0));
    EXPECT_TRUE(Agent::GetInstance()->vxlan_network_identifier_mode() ==
                Agent::AUTOMATIC);

    std::stringstream str;

    //Set to configured
    str << "<vxlan-network-identifier-mode>configured</vxlan-network-identifier-mode>" << endl;
    AddNode("global-vrouter-config", "vrouter-config", 1, str.str().c_str());
    client->WaitForIdle();
    EXPECT_TRUE(Agent::GetInstance()->vxlan_network_identifier_mode() ==
                Agent::CONFIGURED);

    //Any other string than configured/automatic should default to automatic
    str << "<vxlan-network-identifier-mode>junk</vxlan-network-identifier-mode>" << endl;
    AddNode("global-vrouter-config", "vrouter-config", 1, str.str().c_str());
    client->WaitForIdle();
    EXPECT_TRUE(Agent::GetInstance()->vxlan_network_identifier_mode() ==
                Agent::AUTOMATIC);

    //Set to configured and then delete node 
    str << "<vxlan-network-identifier-mode>configured</vxlan-network-identifier-mode>" << endl;
    AddNode("global-vrouter-config", "vrouter-config", 1, str.str().c_str());
    client->WaitForIdle();
    DelNode("global-vrouter-config", "vrouter-config");
    client->WaitForIdle();
    EXPECT_TRUE(Agent::GetInstance()->vxlan_network_identifier_mode() ==
                Agent::AUTOMATIC);

    DeleteVmportEnv(input, 1, true);
    client->WaitForIdle();
    DelEncapList();
    client->WaitForIdle();
}

TEST_F(CfgTest, TunnelType_test) {
    client->Reset();
    ASSERT_TRUE(TunnelType::DefaultType() == default_tunnel_type_);
    ASSERT_TRUE(TunnelType::ComputeType(TunnelType::AllType()) == 
                default_tunnel_type_);
    AddEncapList("MPLSoUDP", "MPLSoGRE", "VXLAN");
    client->WaitForIdle();

    ASSERT_TRUE(TunnelType::ComputeType(TunnelType::AllType()) == 
                TunnelType::MPLS_UDP);
    client->WaitForIdle();
    DelEncapList();
    client->WaitForIdle();
    ASSERT_TRUE(TunnelType::DefaultType() == default_tunnel_type_);
    ASSERT_TRUE(TunnelType::ComputeType(TunnelType::AllType()) == 
                default_tunnel_type_);
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
