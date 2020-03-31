/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/os.h"
#include <stdio.h>
#include <stdlib.h>

#include "testing/gunit.h"
#include "test/test_cmn_util.h"
#include <boost/assign/list_of.hpp>
#include "base/util.h"

using namespace boost::assign;

void RouterIdDepInit(Agent *agent) {
}

static void ValidateSandeshResponse(Sandesh *sandesh, vector<int> &result) {
    //TBD
    //Validate the response by the expectation
}

static void NovaIntfAdd(int id, const char *name, const char *addr,
                        const char *mac) {
    IpAddress ip = Ip4Address::from_string(addr);
    VmInterface::NovaAdd(Agent::GetInstance()->interface_table(),
                         MakeUuid(id), name, ip.to_v4(), mac, "",
                         MakeUuid(kProjectUuid), VmInterface::kInvalidVlanId,
                         VmInterface::kInvalidVlanId, Agent::NullString(),
                         Ip6Address(), VmInterface::vHostUserClient,
                         Interface::TRANSPORT_ETHERNET, 1);
}

static void NovaDel(int id) {
    VmInterface::Delete(Agent::GetInstance()->interface_table(),
                        MakeUuid(id), VmInterface::INSTANCE_MSG);
}

static void CfgIntfSync(int id, const char *cfg_str, int vn, int vm, std::string ) {
    VmInterface::FloatingIpList list;
    boost::uuids::uuid intf_uuid = MakeUuid(id);
    boost::uuids::uuid vn_uuid = MakeUuid(vn);
    boost::uuids::uuid vm_uuid = MakeUuid(vm);

    std::string cfg_name = cfg_str;

    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;

    VmInterfaceKey *key = new VmInterfaceKey(AgentKey::RESYNC, intf_uuid, "");
    req.key.reset(key);

    VmInterfaceConfigData *cfg_data = new VmInterfaceConfigData(NULL, NULL);
    InterfaceData *data = static_cast<InterfaceData *>(cfg_data);
    data->VmPortInit();

    cfg_data->cfg_name_ = cfg_name;
    cfg_data->vn_uuid_ = vn_uuid;
    cfg_data->vm_uuid_ = vm_uuid;
    cfg_data->floating_ip_list_ = list;
    req.data.reset(cfg_data);
    Agent::GetInstance()->interface_table()->Enqueue(&req);
}

struct PortInfo input1[] = {
    {"vnet1", 1, "1.1.1.1", "00:00:01:01:01:01", 1, 1},
};

class VrfAssignTest : public ::testing::Test {
public:
    VrfAssignTest() : agent_(Agent::GetInstance()),
    trigger_(boost::bind(&VrfAssignTest::ResetVlanNhRef, this),
                         TaskScheduler::GetInstance()->GetTaskId("db::DBTable"),
                         0) {
    }

    bool ResetVlanNhRef() {
        vlan_nh_.reset();
        return true;
    }

    virtual void SetUp() {
        client->Reset();
        CreateVmportEnv(input1, 1);
        client->WaitForIdle();
        EXPECT_TRUE(VmPortActive(1));
        VmInterface *intrface = static_cast<VmInterface *>(VmPortGet(1));
        MacAddress mac;
        VlanNH::CreateReq(intrface->GetUuid(), 1, "vrf1", mac, mac);
        client->WaitForIdle();
        VlanNHKey key(intrface->GetUuid(), 1);
        vlan_nh_ = static_cast<NextHop *>(agent_->nexthop_table()->
                       FindActiveEntry(&key));
    }

    virtual void TearDown() {
        VmInterface *intrface = static_cast<VmInterface *>(VmPortGet(1));
        trigger_.Set();
        client->WaitForIdle();
        VlanNH::DeleteReq(intrface->GetUuid(), 1);
        DeleteVmportEnv(input1, 1, true);
        client->WaitForIdle();
        EXPECT_FALSE(VmPortActive(1));
        EXPECT_FALSE(VrfFind("vrf1"));
        EXPECT_FALSE(VnFind(1));
    }

    Agent *agent_;
    NextHopRef vlan_nh_;
    TaskTrigger trigger_;
};

TEST_F(VrfAssignTest, basic_1) {
    VrfAssignTable::CreateVlanReq(MakeUuid(1), "vrf1", 1);
    client->WaitForIdle();
    EXPECT_TRUE(VrfAssignTable::FindVlanReq(MakeUuid(1), 1) != NULL);

    //Check for sandesh request
    VrfAssignReq *vrf_assign_list_req = new VrfAssignReq();
    std::vector<int> result = {1};
    vrf_assign_list_req->set_uuid(UuidToString(MakeUuid(1)));
    Sandesh::set_response_callback(boost::bind(ValidateSandeshResponse, _1, result));
    vrf_assign_list_req->HandleRequest();
    client->WaitForIdle();
    vrf_assign_list_req->Release();
    client->WaitForIdle();

    VrfAssignTable::DeleteVlanReq(MakeUuid(1), 1);
    client->WaitForIdle();
    EXPECT_TRUE(VrfAssignTable::FindVlanReq(MakeUuid(1), 1) == NULL);
}

TEST_F(VrfAssignTest, basic_1_invalid_vrf) {
    VrfAssignTable::CreateVlanReq(MakeUuid(1), "vrf2", 1);
    client->WaitForIdle();
    EXPECT_TRUE(VrfAssignTable::FindVlanReq(MakeUuid(1), 1) != NULL);

    //Check for sandesh request
    VrfAssignReq *vrf_assign_list_req = new VrfAssignReq();
    std::vector<int> result = {1};
    vrf_assign_list_req->set_uuid(UuidToString(MakeUuid(1)));
    Sandesh::set_response_callback(boost::bind(ValidateSandeshResponse, _1, result));
    vrf_assign_list_req->HandleRequest();
    client->WaitForIdle();
    vrf_assign_list_req->Release();
    client->WaitForIdle();

    VrfAssignTable::DeleteVlanReq(MakeUuid(1), 1);
    client->WaitForIdle();
    EXPECT_TRUE(VrfAssignTable::FindVlanReq(MakeUuid(1), 1) == NULL);
}

TEST_F(VrfAssignTest, Check_key_manipulations) {
    VrfAssignTable::CreateVlanReq(MakeUuid(1), "vrf2", 1);
    client->WaitForIdle();
    EXPECT_TRUE(VrfAssignTable::FindVlanReq(MakeUuid(1), 1) != NULL);

    Interface *intrface = VrfAssignTable::FindInterface(MakeUuid(1));
    EXPECT_TRUE(intrface != NULL);
    //Verify key
    VrfAssign *vrf_assign = VrfAssignTable::FindVlanReq(MakeUuid(1), 1);
    DBEntryBase::KeyPtr tmp_key = vrf_assign->GetDBRequestKey();
    VrfAssign::VrfAssignKey *key =
        static_cast<VrfAssign::VrfAssignKey *>(tmp_key.get());
    VlanVrfAssign *vlan_vrf_assign = static_cast<VlanVrfAssign *>(vrf_assign);
    EXPECT_TRUE(key != NULL);
    EXPECT_TRUE(key->vlan_tag_ == 1);
    EXPECT_TRUE(key->intf_uuid_ == MakeUuid(1));
    EXPECT_TRUE(key->type_ == VrfAssign::VLAN);
    std::auto_ptr<VrfAssign::VrfAssignKey> new_key(new VrfAssign::VrfAssignKey());
    new_key->VlanInit(MakeUuid(1), 2);
    vlan_vrf_assign->SetKey(new_key.get());
    EXPECT_TRUE(vlan_vrf_assign->GetVlanTag() == 2);
    vrf_assign->SetKey(key);
    EXPECT_TRUE(vlan_vrf_assign->GetVlanTag() == 1);

    VrfAssignTable::DeleteVlanReq(MakeUuid(1), 1);
    client->WaitForIdle();
    EXPECT_TRUE(VrfAssignTable::FindVlanReq(MakeUuid(1), 1) == NULL);
}

TEST_F(VrfAssignTest, AddDelete) {
    IFMapTable *table = IFMapTable::FindTable(agent_->db(),
                                              "virtual-machine-interface");
    IFMapNode *node = table->FindNode("vnet1");
    if (node == NULL) {
        assert(0);
    }

    InterfaceTable *intf_table = agent_->interface_table();

    //Build interface config data without service vlan
    //configuration
    DBRequest request1;
    boost::uuids::uuid u;
    intf_table->IFNodeToUuid(node, u);
    intf_table->VmiProcessConfig(node, request1, u);

    //Build interface config data with service vlan configuration
    AddVrf("service-vrf1", 12);
    AddVmPortVrf("ser1", "10.1.1.2", 2);
    AddLink("virtual-machine-interface-routing-instance", "ser1",
            "routing-instance", "service-vrf1");
    AddLink("virtual-machine-interface-routing-instance", "ser1",
            "virtual-machine-interface", "vnet1");
    client->WaitForIdle();
    EXPECT_TRUE(RouteFind("service-vrf1", "10.1.1.2", 32));
    EXPECT_TRUE(VrfAssignTable::FindVlanReq(MakeUuid(1), 2) != NULL);

    DBRequest request2;
    intf_table->VmiProcessConfig(node, request2, u);

    DelLink("virtual-machine-interface-routing-instance", "ser1",
            "virtual-machine-interface", "vnet1");
    client->WaitForIdle();

    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    scheduler->Stop();
    intf_table->Enqueue(&request2);
    intf_table->Enqueue(&request1);

    scheduler->Start();
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(1));
    EXPECT_FALSE(RouteFind("service-vrf1", "10.1.1.2", 32));
    EXPECT_TRUE(VrfAssignTable::FindVlanReq(MakeUuid(1), 2) == NULL);
    DelLink("virtual-machine-interface-routing-instance", "ser1",
            "routing-instance", "service-vrf1");
    DelVmPortVrf("ser1");
    DelVrf("service-vrf1");
    client->WaitForIdle();
}

int main(int argc, char **argv) {
    GETUSERARGS();
    client = TestInit(init_file, ksync_init);

    int ret = RUN_ALL_TESTS();
    TestShutdown();
    delete client;
    return ret;
}
