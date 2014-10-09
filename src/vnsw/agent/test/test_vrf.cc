/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
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
#include "oper/operdb_init.h"
#include "pkt/pkt_init.h"
#include "services/services_init.h"
#include "ksync/ksync_init.h"
#include "oper/interface_common.h"
#include "oper/nexthop.h"
#include "route/route.h"
#include "oper/vrf.h"
#include "oper/mpls.h"
#include "oper/vm.h"
#include "oper/vn.h"
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
#include <boost/assign/list_of.hpp>

using namespace boost::assign;


using namespace pugi;
void RouterIdDepInit(Agent *agent) {
    Agent::GetInstance()->controller()->Connect();
}

static void ValidateSandeshResponse(Sandesh *sandesh, vector<int> &result) {
    //TBD
    //Validate the response by the expectation
}

class VrfTest : public ::testing::Test {
protected:
    VrfTest(): bgp_peer1(NULL) {
    };

    virtual void SetUp() {
        client->Reset();
        thread_ = new ServerThread(&evm_);
        bgp_peer1 = new test::ControlNodeMock(&evm_, "127.0.0.1");
        Agent::GetInstance()->set_controller_ifmap_xmpp_server("127.0.0.1", 0);
        Agent::GetInstance()->set_controller_ifmap_xmpp_port(bgp_peer1->GetServerPort(), 0);
        Agent::GetInstance()->set_dns_server("", 0);
        Agent::GetInstance()->set_dns_server_port(bgp_peer1->GetServerPort(), 0);
        RouterIdDepInit(Agent::GetInstance());
        thread_->Start();
        WAIT_FOR(100, 10000, (bgp_peer1->IsEstablished() == true));
    }

    virtual void TearDown() {
        Agent::GetInstance()->controller()->DisConnect();
        client->WaitForIdle();
        if (Agent::GetInstance()->headless_agent_mode()) {
            TaskScheduler::GetInstance()->Stop();
            Agent::GetInstance()->controller()->unicast_cleanup_timer().cleanup_timer_->Fire();
            Agent::GetInstance()->controller()->multicast_cleanup_timer().cleanup_timer_->Fire();
            TaskScheduler::GetInstance()->Start();
            client->WaitForIdle();
            Agent::GetInstance()->controller()->Cleanup();
            client->WaitForIdle();
        }
        Agent::GetInstance()->controller()->config_cleanup_timer().cleanup_timer_->Cancel();

        bgp_peer1->Shutdown();
        client->WaitForIdle();
        delete bgp_peer1;

        evm_.Shutdown();
        thread_->Join();
        delete thread_;
        client->WaitForIdle();
    }

    EventManager evm_;
    ServerThread *thread_;
    test::ControlNodeMock *bgp_peer1;
};

//Add VRF1 and fabric VRF
//Add routes in VRF1
//Delete VRF1, and verify route table is deleted
TEST_F(VrfTest, VrfAddDelTest_1) {
    client->Reset();
    VrfAddReq("vrf1");
    EXPECT_TRUE(client->VrfNotifyWait(1));
    EXPECT_TRUE(DBTableFind("vrf1.uc.route.0"));
 
    Ip4Address vm1_ip = Ip4Address::from_string("1.1.1.1");
    Ip4Address vm2_ip = Ip4Address::from_string("1.1.1.2");
 
    client->WaitForIdle();
    bgp_peer1->AddRoute("vrf1", "1.1.1.1/32", "10.1.1.10", 10, "vn1");
    bgp_peer1->AddRoute("vrf1", "1.1.1.2/32", "10.1.1.10", 10, "vn1");
    WAIT_FOR(100, 10000, (RouteFind("vrf1", vm1_ip, 32) == true));
    WAIT_FOR(100, 10000, (RouteFind("vrf1", vm2_ip, 32) == true));

    VrfDelReq("vrf1");
    client->WaitForIdle();
    WAIT_FOR(100, 10000, (VrfFind("vrf1")== false));
    EXPECT_FALSE(DBTableFind("vrf1.uc.route.0"));
}

TEST_F(VrfTest, VrfAddDelTest_sandesh_test_1) {
    client->Reset();
    VrfAddReq("vrf1");
    EXPECT_TRUE(client->VrfNotifyWait(1));
    EXPECT_TRUE(DBTableFind("vrf1.uc.route.0"));
 
    Ip4Address vm1_ip = Ip4Address::from_string("1.1.1.1");
    Ip4Address vm2_ip = Ip4Address::from_string("1.1.1.2");
 
    client->WaitForIdle();
    bgp_peer1->AddRoute("vrf1", "1.1.1.1/32", "10.1.1.10", 10, "vn1");
    bgp_peer1->AddRoute("vrf1", "1.1.1.2/32", "10.1.1.10", 10, "vn1");
    WAIT_FOR(100, 10000, (RouteFind("vrf1", vm1_ip, 32) == true));
    WAIT_FOR(100, 10000, (RouteFind("vrf1", vm2_ip, 32) == true));

    VrfListReq *vrf_list_req = new VrfListReq();
    std::vector<int> result = list_of(1);
    Sandesh::set_response_callback(boost::bind(ValidateSandeshResponse, _1, result));
    vrf_list_req->HandleRequest();
    client->WaitForIdle();
    vrf_list_req->Release();
    client->WaitForIdle();

    VrfDelReq("vrf1");
    client->WaitForIdle();
    WAIT_FOR(100, 10000, (VrfFind("vrf1")== false));
    EXPECT_FALSE(DBTableFind("vrf1.uc.route.0"));
}

//
//Create a interface, belonging to vrf1 and verify
//upon delete of vrf, local routes are deleted
TEST_F(VrfTest, VrfAddDelWithVm_1) {
    client->Reset();
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
        {"vnet2", 2, "1.1.1.2", "00:00:00:02:02:02", 1, 2},
    };
    Ip4Address vm1_ip = Ip4Address::from_string(input[0].addr);
    Ip4Address vm2_ip = Ip4Address::from_string(input[1].addr);

    CreateVmportEnv(input, 2);
    client->WaitForIdle();
    EXPECT_TRUE(client->VrfNotifyWait(1));
    EXPECT_TRUE(DBTableFind("vrf1.uc.route.0"));
    EXPECT_TRUE(RouteFind("vrf1", vm1_ip, 32));
    EXPECT_TRUE(RouteFind("vrf1", vm2_ip, 32));

    WAIT_FOR(100, 10000, PathCount("vrf1", vm1_ip, 32) == 2);
    WAIT_FOR(100, 10000, PathCount("vrf1", vm2_ip, 32) == 2);

    //Interface still present
    DeleteVmportEnv(input, 2, true);

    WAIT_FOR(100, 10000, (VrfFind("vrf1") == false));
    client->WaitForIdle();
    EXPECT_FALSE(DBTableFind("vrf1.uc.route.0"));
}

TEST_F(VrfTest, VrfAddDelWithNoRoutes_1) {
    AddVrf("vrf10");
    client->WaitForIdle();
    WAIT_FOR(100, 10000, (VrfFind("vrf10") == true));

    DelVrf("vrf10");
    WAIT_FOR(100, 10000, (VrfFind("vrf10") == false));
    EXPECT_FALSE(DBTableFind("vrf10.route.0"));
}

TEST_F(VrfTest, CheckDefaultVrfDelete) {
    AddVrf(Agent::GetInstance()->fabric_vrf_name().c_str());
    client->WaitForIdle();
    EXPECT_TRUE(VrfFind(Agent::GetInstance()->fabric_vrf_name().c_str()));

    DelVrf(Agent::GetInstance()->fabric_vrf_name().c_str());
    client->WaitForIdle();
    EXPECT_TRUE(VrfFind(Agent::GetInstance()->fabric_vrf_name().c_str()));
}

TEST_F(VrfTest, CheckTableDeleteAndEntryDelete) {
    // Vrf route table and entry have to deleted
    // at same time, as we may get some pending route
    // input after route delete is done
    AddVrf("vrf1");
    client->WaitForIdle();
    EXPECT_TRUE(VrfFind("vrf1"));
    EXPECT_TRUE(DBTableFind("vrf1.uc.route.0"));

    //Acquire a refernece to vrf1 to delay vrf deletion
    VrfEntryRef vrf_ref = VrfGet("vrf1");
    DelVrf("vrf1");
    client->WaitForIdle();
    VrfKey key("vrf1");
    EXPECT_TRUE(Agent::GetInstance()->vrf_table()->Find(&key, true));
    EXPECT_TRUE(DBTableFind("vrf1.uc.route.0"));

    //Release pending reference on vrf
    vrf_ref = NULL;
    client->WaitForIdle();
    EXPECT_FALSE(VrfFind("vrf1"));
    EXPECT_FALSE(DBTableFind("vrf1.uc.route.0"));
}

TEST_F(VrfTest, CheckVrfReuse) {
    VrfEntry *vrf = NULL;
    //Add a vrf config
    AddVrf("vrf1", 1);
    client->WaitForIdle();
    EXPECT_TRUE(VrfFind("vrf1"));
    EXPECT_TRUE(DBTableFind("vrf1.uc.route.0"));

    //Acquire a refernece to vrf1 to delay vrf deletion
    VrfEntryRef vrf_ref = VrfGet("vrf1");
    DelVrf("vrf1");
    client->WaitForIdle();
    VrfKey key("vrf1");
    vrf = static_cast<VrfEntry *>(Agent::GetInstance()->vrf_table()->Find(&key, true));
    EXPECT_TRUE(vrf->IsDeleted());
    EXPECT_TRUE(DBTableFind("vrf1.uc.route.0"));

    AddVrf("vrf1", 1); 
    //Release VRF reference
    vrf_ref = NULL;
    client->WaitForIdle();
    vrf = VrfGet("vrf1");
    EXPECT_FALSE(vrf->IsDeleted());
    EXPECT_TRUE(DBTableFind("vrf1.uc.route.0"));
    DelVrf("vrf1");
    client->WaitForIdle();
    EXPECT_FALSE(VrfFind("vrf1"));
    EXPECT_FALSE(DBTableFind("vrf1.uc.route.0"));
}

TEST_F(VrfTest, CheckIntfActivate) {
    //Add couple of interface in a VRF
    client->Reset();
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
        {"vnet2", 2, "1.1.1.2", "00:00:00:02:02:02", 1, 2},
    };
    Ip4Address vm1_ip = Ip4Address::from_string(input[0].addr);
    Ip4Address vm2_ip = Ip4Address::from_string(input[1].addr);

    CreateVmportEnv(input, 2);
    client->WaitForIdle();
    EXPECT_TRUE(client->VrfNotifyWait(1));
    EXPECT_TRUE(DBTableFind("vrf1.uc.route.0"));
    EXPECT_TRUE(RouteFind("vrf1", vm1_ip, 32));
    EXPECT_TRUE(RouteFind("vrf1", vm2_ip, 32));

    VrfEntryRef vrf_ref = VrfGet("vrf1");
    DeleteVmportEnv(input, 2, true);
    client->WaitForIdle();

    //Vrf is pending delete, due to reference
    //Re-add the vmport and since VRF link is
    //not parsed the interface should be inactive
    CreateVmportEnv(input, 2);
    client->WaitForIdle();
    EXPECT_FALSE(VmPortActive(input, 0) == true);
    EXPECT_FALSE(VmPortActive(input, 1) == true);
    EXPECT_FALSE(RouteFind("vrf1", vm1_ip, 32));
    EXPECT_FALSE(RouteFind("vrf1", vm2_ip, 32));
    //Release reference to VRF, and expect interface 
    //to be active
    vrf_ref = NULL;
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input, 0) == true);
    EXPECT_TRUE(VmPortActive(input, 1) == true);
    EXPECT_TRUE(RouteFind("vrf1", vm1_ip, 32));
    EXPECT_TRUE(RouteFind("vrf1", vm2_ip, 32));
    DeleteVmportEnv(input, 2, true);
    client->WaitForIdle();
}

TEST_F(VrfTest, FloatingIpRouteWithdraw) {
    client->Reset();
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
        {"vnet2", 2, "1.1.1.2", "00:00:00:02:02:02", 2, 2},
    };
    Ip4Address vm1_ip = Ip4Address::from_string(input[0].addr);
    Ip4Address vm2_ip = Ip4Address::from_string(input[1].addr);

    CreateVmportFIpEnv(input, 2);
    client->WaitForIdle();
    EXPECT_TRUE(client->VrfNotifyWait(2));
    EXPECT_TRUE(DBTableFind("default-project:vn1:vn1.uc.route.0"));
    EXPECT_TRUE(RouteFind("default-project:vn1:vn1", vm1_ip, 32));

    EXPECT_TRUE(DBTableFind("default-project:vn2:vn2.uc.route.0"));
    EXPECT_TRUE(RouteFind("default-project:vn2:vn2", vm2_ip, 32));

    //Add floating IP for vm2 to talk to vm1
    AddFloatingIpPool("fip-pool1", 1);
    AddFloatingIp("fip1", 1, "2.1.1.100");
    AddLink("floating-ip", "fip1", "floating-ip-pool", "fip-pool1");
    AddLink("floating-ip-pool", "fip-pool1", "virtual-network",
            "default-project:vn1");
    client->WaitForIdle();
    AddLink("virtual-machine-interface", "vnet2", "floating-ip", "fip1");
    client->WaitForIdle();
    Ip4Address floating_ip = Ip4Address::from_string("2.1.1.100");
    EXPECT_TRUE(RouteFind("default-project:vn1:vn1", floating_ip, 32));
    WAIT_FOR(100, 10000,
             PathCount("default-project:vn1:vn1", floating_ip, 32) == 2);

    //Delete floating IP and expect route to get deleted
    DelLink("floating-ip", "fip1", "floating-ip-pool", "fip-pool1");
    DelLink("floating-ip-pool", "fip-pool1", "virtual-network",
            "default-project:vn1");
    DelLink("virtual-machine-interface", "vnet2", "floating-ip", "fip1");
    DelFloatingIp("fip1");
    client->WaitForIdle();
    WAIT_FOR(100, 1000,
             RouteFind("default-project:vn1:vn1",floating_ip, 32) == false);
    DeleteVmportFIpEnv(input, 2, true);
    client->WaitForIdle();
}

int main(int argc, char **argv) {
    GETUSERARGS();
    client = TestInit(init_file, ksync_init, false, true, false);

    int ret = RUN_ALL_TESTS();
    client->WaitForIdle();
    TestShutdown();
    delete client;
    return ret;
}
