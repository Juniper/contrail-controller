/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <base/logging.h>
#include <io/event_manager.h>
#include <tbb/task.h>
#include <base/task.h>

#include <boost/shared_ptr.hpp>

#include "cfg/cfg_init.h"
#include "cfg/cfg_interface.h"
#include "oper/operdb_init.h"
#include "controller/controller_init.h"
#include "pkt/pkt_init.h"
#include "services/services_init.h"
#include "ksync/ksync_init.h"
#include "oper/agent_route_walker.h"
#include "openstack/instance_service_server.h"
#include "test_cmn_util.h"
#include "kstate/test/test_kstate_util.h"
#include "vr_types.h"

using namespace std;

void RouterIdDepInit() {
}

static void ValidateSandeshResponse(Sandesh *sandesh, vector<int> &result) {
    //TBD
    //Validate the response by the expectation
}

struct PortInfo input_1[] = {
    {"vnet1", 1, "1.1.1.10", "00:00:01:01:01:10", 1, 1},
};

struct PortInfo input_2[] = {
    {"vnet2", 2, "2.2.2.20", "00:00:02:02:02:20", 2, 2},
};

class AgentRouteWalkerTest : public AgentRouteWalker, public ::testing::Test {
public:    
    AgentRouteWalkerTest() : AgentRouteWalker(AgentRouteWalker::ALL),
    default_tunnel_type_(TunnelType::MPLS_GRE) {
        vrf_name_1_ = "vrf1";
        vrf_name_2_ = "vrf2";
        server_ip_ = Ip4Address::from_string("10.1.1.11");
        local_vm_ip_1_ = Ip4Address::from_string("1.1.1.10");
        local_vm_ip_2_ = Ip4Address::from_string("2.2.2.20");
        remote_vm_ip_ = Ip4Address::from_string("1.1.1.11");
        local_vm_mac_1_ = (struct ether_addr *)malloc(sizeof(struct ether_addr));
        local_vm_mac_2_ = (struct ether_addr *)malloc(sizeof(struct ether_addr));
        remote_vm_mac_ = (struct ether_addr *)malloc(sizeof(struct ether_addr));
        memcpy (local_vm_mac_1_, ether_aton("00:00:01:01:01:10"), 
                sizeof(struct ether_addr));
        memcpy (local_vm_mac_2_, ether_aton("00:00:02:02:02:20"), 
                sizeof(struct ether_addr));
        memcpy (remote_vm_mac_, ether_aton("00:00:01:01:01:11"), 
                sizeof(struct ether_addr));
        route_notifications_ = 0;
        vrf_notifications_ = vrf_notifications_count_ = 0;
        total_rt_vrf_walk_done_ = 0;
    };
    ~AgentRouteWalkerTest() { };

    virtual void SetUp() {
        client->Reset();
        VxLanNetworkIdentifierMode(false);
        client->WaitForIdle();
        AddEncapList("MPLSoGRE", "MPLSoUDP", "VXLAN");
        client->WaitForIdle();

        //Create a VRF
        VrfAddReq(vrf_name_1_.c_str());
        VrfAddReq(vrf_name_2_.c_str());
        Agent::GetInstance()->GetDefaultInet4UnicastRouteTable()->AddResolveRoute(
                Agent::GetInstance()->GetDefaultVrf(), server_ip_, 24);
        client->WaitForIdle();
        client->WaitForIdle();
        CreateVmportEnv(input_1, 1);
        CreateVmportEnv(input_2, 1);
        client->WaitForIdle();
        client->Reset();
    }

    virtual void TearDown() {
        client->Reset();
        DeleteVmportEnv(input_1, 1, true);
        DeleteVmportEnv(input_2, 1, true);
        client->WaitForIdle();
        DelEncapList();
        client->WaitForIdle();

        VrfDelReq(vrf_name_1_.c_str());
        VrfDelReq(vrf_name_2_.c_str());
        client->WaitForIdle();
        WAIT_FOR(100, 100, (VrfFind(vrf_name_1_.c_str()) != true));
        WAIT_FOR(100, 100, (VrfFind(vrf_name_2_.c_str()) != true));
    }

    virtual bool RouteWalkNotify(DBTablePartBase *partition, DBEntryBase *e) {
        //Fabric VRF
        //0.0.0.0/32; 10.1.1.0/24; 10.1.1.1/32; 10.1.1.254/3; 10.1.1.255/32;
        //169.254.1.3/32; 169.254.2.4/32; 255.255.255.255
        //vrf1
        //1.1.1.10/32; 255.255.255.255; 0:0:1:1:1:10; ff:ff:ff:ff:ff:ff
        //vrf2
        //2.2.2.20/32; 255.255.255.255; 0:0:2:2:2:20; ff:ff:ff:ff:ff:ff
        route_notifications_++;
        return true;
    }

    virtual void RouteWalkDone(DBTableBase *part) {
        total_rt_vrf_walk_done_++;
    }

    virtual bool VrfWalkNotify(DBTablePartBase *partition, DBEntryBase *e) {
        vrf_notifications_++;
        VrfEntry *vrf = static_cast<VrfEntry *>(e);
        StartRouteWalk(vrf);
        return true;
    }

    virtual void VrfWalkDone(DBTableBase *part) {
        vrf_notifications_count_++;
    }

    void VerifyNotifications(uint32_t route_notifications,
                             uint32_t vrf_notifications,
                             uint32_t vrf_notifications_count,
                             uint32_t total_rt_vrf_walk_done) {
        client->WaitForIdle(10);
        WAIT_FOR(100, 1000, (route_notifications_ == route_notifications_));
        ASSERT_TRUE(route_notifications_ == route_notifications);
        ASSERT_TRUE(vrf_notifications_ == vrf_notifications);
        ASSERT_TRUE(vrf_notifications_count_ == vrf_notifications_count);
        ASSERT_TRUE(total_rt_vrf_walk_done_ == total_rt_vrf_walk_done);
    }

    TunnelType::Type default_tunnel_type_;
    std::string vrf_name_1_;
    std::string vrf_name_2_;
    Ip4Address  local_vm_ip_1_;
    Ip4Address  local_vm_ip_2_;
    Ip4Address  remote_vm_ip_;
    Ip4Address  server_ip_;
    struct ether_addr *local_vm_mac_1_;
    struct ether_addr *local_vm_mac_2_;
    struct ether_addr *remote_vm_mac_;
    static TunnelType::Type type_;
    uint32_t route_notifications_;
    uint32_t vrf_notifications_;
    uint32_t vrf_notifications_count_;
    uint32_t total_rt_vrf_walk_done_;
    RouteTableTypeWalkid route_table_type_walkid_;
};

TEST_F(AgentRouteWalkerTest, walk_all_routes) {
    client->Reset();
    StartVrfWalk();
    VerifyNotifications(16, 3, 1, 9);
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
