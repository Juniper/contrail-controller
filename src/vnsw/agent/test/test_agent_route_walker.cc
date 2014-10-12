/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/os.h"
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

void RouterIdDepInit(Agent *agent) {
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

struct PortInfo input_3[] = {
    {"vnet3", 3, "3.3.3.30", "00:00:03:03:03:30", 3, 3},
};

class AgentRouteWalkerTest : public AgentRouteWalker, public ::testing::Test {
public:    
    AgentRouteWalkerTest() : AgentRouteWalker(Agent::GetInstance(),
                                              AgentRouteWalker::ALL),
    default_tunnel_type_(TunnelType::MPLS_GRE) {
        vrf_name_1_ = "vrf1";
        vrf_name_2_ = "vrf2";
        vrf_name_3_ = "vrf3";
        server_ip_ = Ip4Address::from_string("10.1.1.11");
        local_vm_ip_1_ = Ip4Address::from_string("1.1.1.10");
        local_vm_ip_2_ = Ip4Address::from_string("2.2.2.20");
        remote_vm_ip_ = Ip4Address::from_string("1.1.1.11");
        route_notifications_ = 0;
        vrf_notifications_ = vrf_notifications_count_ = 0;
        total_rt_vrf_walk_done_ = 0;
    };
    ~AgentRouteWalkerTest() { 
    }

    void SetupEnvironment(int num_vrfs) {
        client->Reset();
        if (num_vrfs == 0)
            return;

        if (num_vrfs > 0) {
            VrfAddReq(vrf_name_1_.c_str());
        }
        if (num_vrfs > 1) {
            VrfAddReq(vrf_name_2_.c_str());
        }
        if (num_vrfs > 2) {
            VrfAddReq(vrf_name_3_.c_str());
        }
        Agent::GetInstance()->fabric_inet4_unicast_table()->AddResolveRoute(
                Agent::GetInstance()->fabric_vrf_name(), server_ip_, 24);
        client->WaitForIdle();
        client->WaitForIdle();
        if (num_vrfs > 0) {
            CreateVmportEnv(input_1, 1);
        }
        if (num_vrfs > 1) {
            CreateVmportEnv(input_2, 1);
        }
        if (num_vrfs > 2) {
            CreateVmportEnv(input_3, 1);
        }
        client->WaitForIdle();
        client->Reset();
    }

    void DeleteEnvironment(int num_vrfs) {
        client->Reset();
        if (num_vrfs == 0)
            return;

        if (num_vrfs > 0) {
            DeleteVmportEnv(input_1, 1, true);
        }
        if (num_vrfs > 1) {
            DeleteVmportEnv(input_2, 1, true);
        }
        if (num_vrfs > 2) {
            DeleteVmportEnv(input_3, 1, true);
        }
        client->WaitForIdle();
        if (num_vrfs > 0) {
            VrfDelReq(vrf_name_1_.c_str());
            client->WaitForIdle();
            WAIT_FOR(100, 100, (VrfFind(vrf_name_1_.c_str()) != true));
        }
        if (num_vrfs > 1) {
            VrfDelReq(vrf_name_2_.c_str());
            client->WaitForIdle();
            WAIT_FOR(100, 100, (VrfFind(vrf_name_2_.c_str()) != true));
        }
        if (num_vrfs > 2) {
            VrfDelReq(vrf_name_3_.c_str());
            client->WaitForIdle();
            WAIT_FOR(100, 100, (VrfFind(vrf_name_3_.c_str()) != true));
        }
        client->WaitForIdle();
    }

    virtual void SetUp() {
        client->Reset();
        VxLanNetworkIdentifierMode(false);
        client->WaitForIdle();
        AddEncapList("MPLSoGRE", "MPLSoUDP", "VXLAN");
        client->WaitForIdle();
    }

    virtual void TearDown() {
        client->Reset();
        DelEncapList();
        client->WaitForIdle();
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
        AgentRouteWalker::RouteWalkDone(part);
    }

    virtual bool VrfWalkNotify(DBTablePartBase *partition, DBEntryBase *e) {
        vrf_notifications_++;
        VrfEntry *vrf = static_cast<VrfEntry *>(e);
        StartRouteWalk(vrf);
        return true;
    }

    virtual void VrfWalkDone(DBTableBase *part) {
        vrf_notifications_count_++;
        AgentRouteWalker::VrfWalkDone(part);
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
    std::string vrf_name_3_;
    Ip4Address  local_vm_ip_1_;
    Ip4Address  local_vm_ip_2_;
    Ip4Address  remote_vm_ip_;
    Ip4Address  server_ip_;
    static TunnelType::Type type_;
    uint32_t route_notifications_;
    uint32_t vrf_notifications_;
    uint32_t vrf_notifications_count_;
    uint32_t total_rt_vrf_walk_done_;
};

TEST_F(AgentRouteWalkerTest, walk_all_routes_wih_no_vrf) {
    client->Reset();
    SetupEnvironment(0);
    StartVrfWalk();
    VerifyNotifications(6, 1, 1, Agent::ROUTE_TABLE_MAX);
    DeleteEnvironment(0);
}

TEST_F(AgentRouteWalkerTest, walk_all_routes_wih_1_vrf) {
    client->Reset();
    SetupEnvironment(1);
    StartVrfWalk();
    VerifyNotifications(11, 2, 1, (Agent::ROUTE_TABLE_MAX * 2));
    DeleteEnvironment(1);
}

TEST_F(AgentRouteWalkerTest, walk_all_routes_with_2_vrf) {
    client->Reset();
    SetupEnvironment(2);
    StartVrfWalk();
    VerifyNotifications(16, 3, 1, (Agent::ROUTE_TABLE_MAX * 3));
    DeleteEnvironment(2);
}

TEST_F(AgentRouteWalkerTest, walk_all_routes_with_3_vrf) {
    client->Reset();
    SetupEnvironment(3);
    StartVrfWalk();
    VerifyNotifications(21, 4, 1, (Agent::ROUTE_TABLE_MAX * 4));
    DeleteEnvironment(3);
}

TEST_F(AgentRouteWalkerTest, restart_walk_with_2_vrf) {
    client->Reset();
    SetupEnvironment(2);
    StartVrfWalk();
    StartVrfWalk();
    //TODO validate
    WAIT_FOR(100, 1000, IsWalkCompleted() == true);
    DeleteEnvironment(2);
}

TEST_F(AgentRouteWalkerTest, cancel_vrf_walk_with_2_vrf) {
    client->Reset();
    SetupEnvironment(2);
    StartVrfWalk();
    CancelVrfWalk();
    WAIT_FOR(100, 1000, IsWalkCompleted() == true);
    //TODO validate
    client->WaitForIdle(10);
    DeleteEnvironment(2);
}

TEST_F(AgentRouteWalkerTest, cancel_route_walk_with_2_vrf) {
    //TODO
}

//TODO REMAINING TESTS
// - based on walktype - unicast/multicast/all
//
int main(int argc, char **argv) {
    GETUSERARGS();

    client = TestInit(init_file, ksync_init);
    int ret = RUN_ALL_TESTS();
    client->WaitForIdle();
    TestShutdown();
    delete client;
    return ret;
}
