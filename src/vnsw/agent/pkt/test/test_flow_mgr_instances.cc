/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#include "base/os.h"
#include <algorithm>
#include <net/address_util.h>
#include "test/test_cmn_util.h"
#include "test_flow_util.h"
#include "ksync/ksync_sock_user.h"
#include "oper/tunnel_nh.h"
#include "pkt/flow_table.h"
#include "pkt/flow_mgmt.h"
#include "pkt/flow_mgmt_dbclient.h"

#include "test_flow_base.cc"

//Test flow deletion on interface deletion and make sure 
//interface DB Entry is not deleted while FlowMgr Instance has reference.
TEST_F(FlowTest, FlowDeleteInterface) {
    EXPECT_EQ(0U, get_flow_proto()->FlowCount());
    struct PortInfo input[] = {
        {"vif0", 11, "11.1.1.11", "00:00:00:01:01:11", 5, 6},
        {"vif1", 12, "11.1.1.12", "00:00:00:01:01:12", 5, 6},
    };
    CreateVmportEnv(input, 2);
    client->WaitForIdle();

    InterfaceRef intf(VmInterfaceGet(input[0].intf_id));

    for (int i = 1; i <= 100; i++) {
        TxTcpPacket(intf->id(), "11.1.1.11", "11.1.1.12", 30, 40 + i, false, 1,
             VrfGet("vrf5")->vrf_id());
    }

    client->WaitForIdle();
    EXPECT_EQ(100U, get_flow_proto()->FlowCount());

    //Disable WorkQueues for an instance for it hold reference for DB Entry
    FlowMgmtManager *mgr = Agent::GetInstance()->pkt()->flow_mgmt_manager(0);
    mgr->FlowUpdateQueueDisable(true);

    //Delete the interface with reference help
    DeleteVmportEnv(input, 2, false);
    client->WaitForIdle();

    //Ensure DBEntry is not removed/freed
    EXPECT_TRUE(VmPortFindRetDel(input[0].intf_id));

    mgr->FlowUpdateQueueDisable(false);

    client->WaitForIdle();
    EXPECT_EQ(0U, get_flow_proto()->FlowCount());
}

int main(int argc, char *argv[]) {
    GETUSERARGS();

    strcpy(init_file, "controller/src/vnsw/agent/pkt/test/flow-table.ini");
    client = 
        TestInit(init_file, ksync_init, true, true, true, (1000000 * 60 * 10), (3 * 60 * 1000));
    if (vm.count("config")) {
        eth_itf = Agent::GetInstance()->fabric_interface_name();
    } else {
        eth_itf = "eth0";
        PhysicalInterface::CreateReq(Agent::GetInstance()->interface_table(),
                                eth_itf,
                                Agent::GetInstance()->fabric_vrf_name(),
                                PhysicalInterface::FABRIC,
                                PhysicalInterface::ETHERNET, false, nil_uuid(),
                                Ip4Address(0), Interface::TRANSPORT_ETHERNET);
        client->WaitForIdle();
    }

    FlowTest::TestSetup(ksync_init);
    int ret = RUN_ALL_TESTS();
    client->WaitForIdle();
    TestShutdown();
    delete client;
    return ret;
}
