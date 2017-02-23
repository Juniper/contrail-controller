/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#include "test_cmn_util.h"
#include <resource_manager/resource_manager.h>
#include <resource_manager/resource_table.h>
#include <resource_manager/mpls_index.h>

void RouterIdDepInit(Agent *agent) {
}

class AgentDbEntry : public ::testing::Test {
public:
    AgentDbEntry() {
        agent_ = Agent::GetInstance();
    }
    virtual ~AgentDbEntry() { }
    Agent *agent_;
};

//Bug# 1666139
TEST_F(AgentDbEntry, evpn_mcast_label_1666139) {
   struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.10", "00:00:01:01:01:10", 1, 1},
    };

    client->Reset();
    //Alloc label
    CreateVmportEnv(input, 1);
    client->WaitForIdle();

    //Test
    //Add fabric group multicast route
    TunnelOlist olist;
    Ip4Address sip(0);
    Ip4Address broadcast(0xFFFFFFFF);
    MulticastHandler *mc_handler = static_cast<MulticastHandler *>(agent_->
                                       oper_db()->multicast());
    olist.push_back(OlistTunnelEntry(nil_uuid(), 10,
                                     IpAddress::from_string("8.8.8.8").to_v4(),
                                     TunnelType::MplsType()));
    mc_handler->ModifyFabricMembers(agent_->multicast_tree_builder_peer(),
                                    "vrf1", broadcast, sip, 4100, olist, 1);
    client->WaitForIdle();
    BridgeRouteEntry *l2_rt =
        L2RouteGet("vrf1", MacAddress("FF:FF:FF:FF:FF:FF"));
    AgentPath *local_vm_path = l2_rt->FindPath(agent_->local_vm_peer());
    ResourceManager::KeyPtr key(new TestMplsResourceKey(agent_->
                                resource_manager(), "test"));
    agent_->mpls_table()->AllocLabel(key);
    uint32_t label_l = ((static_cast<IndexResourceData *>(agent_->
                                                          resource_manager()->
                                      Allocate(key).get()))->index());
    EXPECT_TRUE(local_vm_path->label() != label_l);

    //Delete label
    agent_->resource_manager()->Release(Resource::MPLS_INDEX, label_l);
    DeleteVmportEnv(input, 1, true);
    client->WaitForIdle();
}

int main(int argc, char *argv[]) {
    GETUSERARGS();
    client = TestInit(init_file, ksync_init, true, true, false);
    int ret = RUN_ALL_TESTS();
    TestShutdown();
    delete client;
    return ret;
}
