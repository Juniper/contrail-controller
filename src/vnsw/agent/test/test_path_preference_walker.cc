/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/os.h"
#include <base/logging.h>
#include <io/event_manager.h>
#include <boost/shared_ptr.hpp>

#include "cfg/cfg_init.h"
#include "cfg/cfg_interface.h"
#include "oper/operdb_init.h"
#include "oper/agent_path.h"
#include "oper/path_preference.h"
#include "controller/controller_init.h"
#include "pkt/pkt_init.h"
#include "services/services_init.h"
#include "test_cmn_util.h"

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

IpamInfo ipam_info1[] = {
    {"1.1.1.0", 24, "1.1.1.1"},
};

IpamInfo ipam_info2[] = {
    {"2.2.2.0", 24, "2.2.2.1"},
};

IpamInfo ipam_info3[] = {
    {"3.3.3.0", 24, "3.3.3.1"},
};

struct TestPathPreferenceRouteListener : public PathPreferenceRouteListener {
    TestPathPreferenceRouteListener(Agent *agent, AgentRouteTable *table) :
        PathPreferenceRouteListener(agent, table), walk_done_(false) { }
    bool DeleteState(DBTablePartBase *partition, DBEntryBase *e) {
        bool ret = PathPreferenceRouteListener::DeleteState(partition, e);
        PathPreferenceRouteListener::Notify(partition, e);
        return ret;
    }
    void Walkdone(DBTableBase *partition, PathPreferenceRouteListener *state) {
        PathPreferenceRouteListener::Walkdone(partition, state);
        walk_done_ = true;
    }
    virtual void Delete() { } //Dont call parent delete as it will start
                              // parallel walk

    bool walk_done_;
};

class PathPreferenceRouteTableWalkerTest : public ::testing::Test {
public:    
    PathPreferenceRouteTableWalkerTest() {
        vrf_name_1_ = "vrf1";
        vrf_name_2_ = "vrf2";
        vrf_name_3_ = "vrf3";
        server_ip_ = Ip4Address::from_string("10.1.1.11");
        local_vm_ip_1_ = Ip4Address::from_string("1.1.1.10");
        local_vm_ip_2_ = Ip4Address::from_string("2.2.2.20");
        remote_vm_ip_ = Ip4Address::from_string("1.1.1.11");
        test_listener_ = NULL;
        agent_ = Agent::GetInstance();
    };
    ~PathPreferenceRouteTableWalkerTest() { 
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
        VrfEntry *vrf = VrfGet("vrf1", false);
        WAIT_FOR(1000, 1000, vrf->GetEvpnRouteTable() != NULL);
        EvpnAgentRouteTable *evpn_rt_table =
            static_cast<EvpnAgentRouteTable*>(vrf->GetEvpnRouteTable());
        test_listener_ =
            new TestPathPreferenceRouteListener(agent_, evpn_rt_table);
        client->WaitForIdle();
        test_listener_->Init();
        client->WaitForIdle();
        InetInterfaceKey vhost_intf_key(
                Agent::GetInstance()->vhost_interface()->name());
        Agent::GetInstance()->fabric_inet4_unicast_table()->AddResolveRoute(
                Agent::GetInstance()->local_peer(),
                Agent::GetInstance()->fabric_vrf_name(), server_ip_, 24,
                vhost_intf_key, 0, false, "", SecurityGroupList());
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
        AddIPAM("vn1", ipam_info1, 1);
        client->WaitForIdle();
        AddIPAM("vn2", ipam_info2, 1);
        client->WaitForIdle();
        AddIPAM("vn3", ipam_info3, 1);
        client->WaitForIdle();
    }

    virtual void TearDown() {
        client->Reset();
        DelEncapList();
        client->WaitForIdle();
        DelIPAM("vn1");
        client->WaitForIdle();
        DelIPAM("vn2");
        client->WaitForIdle();
        DelIPAM("vn3");
        client->WaitForIdle();
    }
 
    std::string vrf_name_1_;
    std::string vrf_name_2_;
    std::string vrf_name_3_;
    Ip4Address  local_vm_ip_1_;
    Ip4Address  local_vm_ip_2_;
    Ip4Address  remote_vm_ip_;
    Ip4Address  server_ip_;
    TestPathPreferenceRouteListener *test_listener_;
    Agent *agent_;
};

TEST_F(PathPreferenceRouteTableWalkerTest, notify_on_deleted_before_walk_done) {
    client->Reset();
    SetupEnvironment(3);
    VrfDelReq("vrf1");
    client->WaitForIdle();
    DBTableWalker *walker = agent_->db()->GetWalker();
    VrfEntry *vrf = VrfGet("vrf1", true);
    EvpnAgentRouteTable *evpn_rt_table =
        static_cast<EvpnAgentRouteTable*>(vrf->GetEvpnRouteTable());
    test_listener_->set_deleted();//Artificially mark it for delete
    walker->WalkTable(evpn_rt_table, NULL,
                      boost::bind(&TestPathPreferenceRouteListener::DeleteState,
                                  test_listener_, _1, _2),
                      boost::bind(&TestPathPreferenceRouteListener::Walkdone,
                                  test_listener_, _1, test_listener_));
    WAIT_FOR(1000, 1000, (test_listener_->walk_done_ == true));
    DeleteEnvironment(3);
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
