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

bool FlowMgmtUpdateCompleted(Agent *agent) {
    std::vector<FlowMgmtManager *>::const_iterator it =
        agent->pkt()->flow_mgmt_manager_iterator_begin();
    while (it != agent->pkt()->flow_mgmt_manager_iterator_end()) {
        if (0 != (*it)->FlowUpdateQueueLength()) {
            return false;
        }
        it++;
    }
    return true;
}

bool FlowMgmtDbEventCompleted(Agent *agent) {
    std::vector<FlowMgmtManager *>::const_iterator it =
        agent->pkt()->flow_mgmt_manager_iterator_begin();
    while (it != agent->pkt()->flow_mgmt_manager_iterator_end()) {
        if (0 != (*it)->FlowDBQueueLength()) {
            return false;
        }
        it++;
    }
    return true;
}

bool FlowModuleDbEventCompleted(Agent *agent) {
    if (!FlowMgmtDbEventCompleted(agent)) {
        return false;
    }

    if (0 != agent->pkt()->get_flow_proto()->FlowUpdateQueueLength()) {
        return false;
    }

    return true;
}

//Test flow deletion on interface deletion and make sure 
//interface DB Entry is not deleted while FlowMgr Instance has reference.
TEST_F(FlowTest, FlowDeleteInterface) {
    KSyncSockTypeMap *sock = static_cast<KSyncSockTypeMap *>(KSyncSock::Get(0));
    sock->set_is_incremental_index(true);
    EXPECT_EQ(0U, get_flow_proto()->FlowCount());
    struct PortInfo input[] = {
        {"vif0", 11, "11.1.1.11", "00:00:00:01:01:11", 5, 6},
        {"vif1", 12, "11.1.1.12", "00:00:00:01:01:12", 5, 6},
    };
    CreateVmportEnv(input, 2);
    client->WaitForIdle();

    WAIT_FOR(1000, 1000, VmPortActive(input, 0));
    WAIT_FOR(1000, 1000, VmPortActive(input, 1));

    client->WaitForIdle();

    InterfaceRef intf(VmInterfaceGet(input[0].intf_id));

    EXPECT_EQ(0U, get_flow_proto()->FlowCount());

    for (int i = 1; i <= 100; i++) {
        // since we will use incremental hash id for reverse flow
        // will need to skip those while simulating pkt trap to
        // avoid eviction processing
        TxTcpPacket(intf->id(), "11.1.1.11", "11.1.1.12", 30, 40 + i, false,
                    (i * 2) - 1, VrfGet("vrf5")->vrf_id());
    }

    client->WaitForIdle();
    WAIT_FOR(1000, 1000, (200 == get_flow_proto()->FlowCount()));

    // wait for flow module to complete building mgmt db
    WAIT_FOR(1000, 1000, (FlowMgmtUpdateCompleted(agent())));
    WAIT_FOR(1000, 1000, (FlowModuleDbEventCompleted(agent())));
    client->WaitForIdle();

    // route delete notification happens before interface delete notification
    // processing of this event can remove flow from interface mgmt tree due
    // to short flow, which will hinder deletion testing of flow due to
    // interface deletion.
    get_flow_proto()->DisableFlowUpdateQueue(true);
    client->WaitForIdle();

    //Delete the interface with reference help
    DeleteVmportEnv(input, 2, false);
    client->WaitForIdle();

    //Ensure DBEntry is not removed/freed
    EXPECT_TRUE(VmPortFindRetDel(input[0].intf_id));

    client->WaitForIdle();

    // enable flow update queue to allow flow deletion to complete
    get_flow_proto()->DisableFlowUpdateQueue(false);
    client->WaitForIdle();

    // wait for db events to be completed
    WAIT_FOR(1000, 1000, (FlowModuleDbEventCompleted(agent())));
    client->WaitForIdle();

    WAIT_FOR(1000, 1000, (0 == get_flow_proto()->FlowCount()));

    // validate updation of flow mgmt tree
    WAIT_FOR(1000, 1000, (FlowMgmtUpdateCompleted(agent())));
    client->WaitForIdle();

    sock->set_is_incremental_index(false);
    client->WaitForIdle();
}

int main(int argc, char *argv[]) {
    GETUSERARGS();

    // use config with large flow tokens to avoid task delays because of token
    // unavailability
    strcpy(init_file, "controller/src/vnsw/agent/pkt/test/flow-table-tokens.ini");
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
