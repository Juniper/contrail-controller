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

//Every time allocate is called and if key is present in resource manager then
//reset of dirty bit should happen for key in map.
TEST_F(AgentDbEntry, dirty_bit_in_resource_key) {
    ResourceManager::KeyPtr key(new TestMplsResourceKey(agent_->
                                resource_manager(), "test"));
    uint32_t label_l = ((static_cast<IndexResourceData *>(agent_->
                                                          resource_manager()->
                                      Allocate(key).get()))->index());
    EXPECT_TRUE(key.get()->dirty() == false);
    key.get()->set_dirty();
    EXPECT_TRUE(key.get()->dirty() == true);
    ResourceManager::KeyPtr key2(new TestMplsResourceKey(agent_->
                                resource_manager(), "test"));
    uint32_t label_l_2 = ((static_cast<IndexResourceData *>(agent_->
                                                          resource_manager()->
                                      Allocate(key2).get()))->index());
    EXPECT_TRUE(label_l == label_l_2);
    EXPECT_TRUE(key.get()->dirty() == false);

    agent_->resource_manager()->Release(Resource::MPLS_INDEX, label_l);
    client->WaitForIdle();
}

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
    olist.push_back(OlistTunnelEntry(boost::uuids::nil_uuid(), 10,
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

static bool ReleaseNHReference(NextHopRef *ref) {
    (*ref).reset();
    return true;
}

static bool CreateRouteLabel(Agent *agent, uint32_t *label) {
    DiscardNHKey new_nh_key;
    *label = agent->mpls_table()->
        CreateRouteLabel(MplsTable::kInvalidLabel,
                         &new_nh_key, "vrf10", "ff:ff:ff:ff:ff:ff");
    return true;
}

static bool FreeLabel(Agent *agent, uint32_t label) {
    agent->mpls_table()->FreeLabel(label);
    return true;
}
//Re-use of label should not be allowed till corresponding dbentry(created by
//label) is removed. This ensures that user of label and dbentry is always in
//sync. If allowed freed label can be issued to any other user while dbentry
//corresponding to it may get deleted (going via deferred deleting).
TEST_F(AgentDbEntry, bug_1680720) {
    client->Reset();

    agent_->vrf_table()->CreateVrfReq("vrf10");
    client->WaitForIdle();
    WAIT_FOR(100, 10000, (VrfFind("vrf10") == true));

    VrfNHKey *key1= new VrfNHKey("vrf10", false, false);
    DBRequest nh_req;
    nh_req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    nh_req.key.reset(key1);
    nh_req.data.reset(new VrfNHData(false, false, false));
    agent_->nexthop_table()->Enqueue(&nh_req);
    client->WaitForIdle();

    VrfNHKey key2("vrf10", false, false);
    NextHop *nh = static_cast<NextHop *>(
        agent_->nexthop_table()->FindActiveEntry(&key2));
    EXPECT_TRUE(nh != NULL);
    EXPECT_TRUE(nh->mpls_label() != NULL);
    EXPECT_TRUE(nh->mpls_label()->label() != MplsTable::kInvalidLabel);
    client->WaitForIdle();

    //Attach a state on this NH and then issue delete
    NextHopRef nh_ref = nh;
    DBState *nh_state = new DBState();
    nh->SetState(nh->get_table(), DBEntryBase::ListenerId(100),
                 nh_state);
    DBState *state = new DBState();
    MplsLabel *mpls_label = agent_->mpls_table()->
        FindMplsLabel(nh->mpls_label()->label());
    mpls_label->SetState(mpls_label->get_table(),
                         DBEntryBase::ListenerId(100),
                         state);
    //Release reference of NH, resulting in trigger of mpls delete
    int task_id = TaskScheduler::GetInstance()->GetTaskId("db::DBTable");
    std::auto_ptr<TaskTrigger> trigger
        (new TaskTrigger(boost::bind(ReleaseNHReference, &nh_ref), task_id, 0));
    trigger->Set();
    client->WaitForIdle();

    //Now create a new NH and verify mpls label in NH is populated.
    uint32_t new_label = 0;
    trigger.reset(new TaskTrigger(boost::bind(CreateRouteLabel, agent_,
                                              &new_label), task_id, 0));
    trigger->Set();
    client->WaitForIdle();
    EXPECT_TRUE(new_label != 0);

    MplsLabel *label2 = agent_->mpls_table()->FindMplsLabel(new_label);
    EXPECT_TRUE(label2 != NULL);
    EXPECT_TRUE(label2 != mpls_label);

    //Release MPLS label reference
    mpls_label->ClearState(mpls_label->get_table(),
                         DBEntryBase::ListenerId(100));
    nh->ClearState(nh->get_table(), DBEntryBase::ListenerId(100));

    //cleanup
    trigger.reset(new TaskTrigger(boost::bind(FreeLabel, agent_,
                                              new_label), task_id, 0));
    trigger->Set();
    client->WaitForIdle();
    agent_->vrf_table()->DeleteVrfReq("vrf10");
    client->WaitForIdle();
    delete state;
    delete nh_state;
}

//Bug# 1681083
TEST_F(AgentDbEntry, fmg_label_freed_on_no_use_1681083) {
   struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.10", "00:00:01:01:01:10", 1, 1},
    };

    client->Reset();
    //Alloc label
    CreateVmportEnv(input, 1);
    client->WaitForIdle();
    agent_->mpls_table()->ReserveMulticastLabel(4000, 5000, 0);

    //Test
    //Add fabric group multicast route
    TunnelOlist olist;
    Ip4Address sip(0);
    Ip4Address broadcast(0xFFFFFFFF);
    MulticastHandler *mc_handler = static_cast<MulticastHandler *>(agent_->
                                       oper_db()->multicast());
    olist.push_back(OlistTunnelEntry(boost::uuids::nil_uuid(), 10,
                                     IpAddress::from_string("8.8.8.8").to_v4(),
                                     TunnelType::MplsType()));
    mc_handler->ModifyFabricMembers(agent_->multicast_tree_builder_peer(),
                                    "vrf1", broadcast, sip, 4100, olist, 1);
    client->WaitForIdle();
    EXPECT_TRUE(agent_->mpls_table()->FindMplsLabel(4100) != NULL);
    mc_handler->ModifyFabricMembers(agent_->multicast_tree_builder_peer(),
                                    "vrf1", broadcast, sip, 4101, olist, 1);
    client->WaitForIdle();
    EXPECT_TRUE(agent_->mpls_table()->FindMplsLabel(4100) == NULL);
    EXPECT_TRUE(agent_->mpls_table()->FindMplsLabel(4101) != NULL);

    //Delete label
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
