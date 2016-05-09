/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#include "base/os.h"
#include "testing/gunit.h"

#include <base/logging.h>
#include <io/event_manager.h>
#include <io/test/event_manager_test.h>
#include <tbb/task.h>
#include <base/task.h>

#include <cmn/agent_cmn.h>

#include "cfg/cfg_init.h"
#include "cfg/cfg_interface.h"
#include "pkt/pkt_init.h"
#include "services/services_init.h"
#include "vrouter/ksync/ksync_init.h"
#include "oper/interface_common.h"
#include "oper/nexthop.h"
#include "oper/tunnel_nh.h"
#include "route/route.h"
#include "oper/vrf.h"
#include "oper/mpls.h"
#include "oper/vm.h"
#include "oper/vn.h"
#include "oper/physical_device_vn.h"
#include "filter/acl.h"
#include "openstack/instance_service_server.h"
#include "test_cmn_util.h"
#include "vr_types.h"

#include "openstack/instance_service_server.h"
#include "xmpp/xmpp_init.h"
#include "xmpp/test/xmpp_test_util.h"
#include "vr_types.h"
#include "control_node_mock.h"
#include "xml/xml_pugi.h"
#include "controller/controller_peer.h"
#include "controller/controller_export.h"
#include "controller/controller_vrf_export.h"

using namespace pugi;

EventManager evm1;
ServerThread *thread1;
test::ControlNodeMock *bgp_peer1;

EventManager evm2;
ServerThread *thread2;
test::ControlNodeMock *bgp_peer2;

void RouterIdDepInit(Agent *agent) {
    Agent::GetInstance()->controller()->Connect();
}

class TsnRoute : public ::testing::Test {
protected:
    TsnRoute() {
    }

    virtual void SetUp() {
        client->Reset();
        agent_ = Agent::GetInstance();
    }

    virtual void TearDown() {
    }

    void AddPhysicalDeviceVn(int dev_id, int vn_id) {
        DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
        req.key.reset(new PhysicalDeviceVnKey(MakeUuid(dev_id),
                                              MakeUuid(vn_id)));
        agent_->physical_device_vn_table()->Enqueue(&req);
        PhysicalDeviceVn key(MakeUuid(dev_id), MakeUuid(vn_id));
        WAIT_FOR(100, 10000,
               (agent_->physical_device_vn_table()->Find(&key, false) != NULL));
    }

    void DelPhysicalDeviceVn(int dev_id, int vn_id) {
        DBRequest req(DBRequest::DB_ENTRY_DELETE);
        req.key.reset(new PhysicalDeviceVnKey(MakeUuid(dev_id),
                                              MakeUuid(vn_id)));
        agent_->physical_device_vn_table()->Enqueue(&req);
        PhysicalDeviceVn key(MakeUuid(dev_id), MakeUuid(vn_id));
        WAIT_FOR(100, 10000,
                (agent_->physical_device_vn_table()->Find(&key, false) == NULL));
    }

    Agent *agent_;
};

//Verify that when agent is in TSN mode, AgentStatsCollector is not created
TEST_F(TsnRoute, NoAgentStatsCollector) {
    EXPECT_TRUE((agent_->stats_collector() == NULL));
}

TEST_F(TsnRoute, Basic) {
}

TEST_F(TsnRoute, vxlan_id_changed) {
    char local_vm_mac_str_[100];
    MacAddress  local_vm_mac_;
    strcpy(local_vm_mac_str_, "ff:ff:ff:ff:ff:ff");
    local_vm_mac_ = MacAddress::FromString(local_vm_mac_str_);

    //Add vrf
    VrfAddReq("vrf1");
    WAIT_FOR(100, 10000, (VrfGet("vrf1", false) != NULL));
    //Add VN
    VnAddReq(1, "vn1", "vrf1");
    WAIT_FOR(100, 10000, (VnGet(1) != NULL));
    //Add device
    DBRequest device_req(DBRequest::DB_ENTRY_ADD_CHANGE);
    device_req.key.reset(new PhysicalDeviceKey(MakeUuid(1)));
    device_req.data.reset(new PhysicalDeviceData(agent_, "test-router",
                                                 "test-router", "",
                                                 Ip4Address::from_string("1.1.1.1"),
                                                 Ip4Address::from_string("2.2.2.2"),
                                                 "OVS", NULL));
    agent_->physical_device_table()->Enqueue(&device_req);
    WAIT_FOR(100, 10000,
             (agent_->physical_device_table()->Find(MakeUuid(1)) != NULL));
    //Add device_vn
    AddPhysicalDeviceVn(1, 1);

    //Initialization done, now delete VRF VN link and then update VXLAN id in
    //VN.
    TestClient::WaitForIdle();
    VxLanNetworkIdentifierMode(true);
    TestClient::WaitForIdle();
    //Verify mcast route
    BridgeRouteEntry *rt = L2RouteGet("vrf1", local_vm_mac_);
    EXPECT_TRUE(rt != NULL);

    //Delete
    DelPhysicalDeviceVn(1, 1);
    DBRequest del_dev_req(DBRequest::DB_ENTRY_DELETE);
    del_dev_req.key.reset(new PhysicalDeviceVnKey(MakeUuid(1),
                                                  MakeUuid(1)));
    agent_->physical_device_table()->Enqueue(&del_dev_req);
    WAIT_FOR(1000, 10000,
             (agent_->physical_device_table()->
              Find(MakeUuid(1)) == NULL));
    VrfDelReq("vrf1");
    VnDelReq(1);
    WAIT_FOR(1000, 10000, (VrfGet("vrf1", true) == NULL));
    WAIT_FOR(1000, 10000, (VnGet(1) == NULL));
}

int main(int argc, char *argv[]) {
    ::testing::InitGoogleTest(&argc, argv);
    GETUSERARGS();
    strcpy(init_file, DEFAULT_VNSW_TSN_CONFIG_FILE);
    client = TestInit(init_file, ksync_init, true, false);
    int ret = RUN_ALL_TESTS();
    TestShutdown();
    return ret;
}
