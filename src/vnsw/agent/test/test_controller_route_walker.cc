/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
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
#include "controller/controller_route_walker.h"
#include "pkt/pkt_init.h"
#include "services/services_init.h"
#include "vrouter/ksync/ksync_init.h"
#include "oper/agent_route_walker.h"
#include "test_cmn_util.h"
#include "kstate/test/test_kstate_util.h"

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

class ControllerRouteWalkerTest : public ::testing::Test {
public:
    ControllerRouteWalkerTest() : agent_(Agent::GetInstance()) {
    }
    virtual ~ControllerRouteWalkerTest() {
    }

private:
    Agent *agent_;
};

TEST_F(ControllerRouteWalkerTest, test_1) {
    client->Reset();
    Agent::GetInstance()->set_headless_agent_mode(true);
    //Create some VM
    BgpPeer *bgp_peer_ptr = CreateBgpPeer(Ip4Address(1), "BGP Peer1");
    PeerConstPtr bgp_peer =
        bgp_peer_ptr->GetAgentXmppChannel()->bgp_peer_id_ref();
    CreateVmportEnv(input_1, 1);
    AddIPAM("vn1", ipam_info1, 1);
    client->WaitForIdle();

    //Get some example route entry
    BridgeRouteEntry *rt = L2RouteGet("vrf1",
                                      MacAddress::FromString("ff:ff:ff:ff:ff:ff"),
                                      Ip4Address(0));
    ControllerRouteWalker *route_walker = bgp_peer_ptr->route_walker();
    TaskScheduler::GetInstance()->Stop();
    //Bring channel down after stopping scheduler
    AgentXmppChannel::HandleAgentXmppClientChannelEvent(bgp_peer_ptr->
                                                        GetAgentXmppChannel(),
                                                        xmps::NOT_READY);
    //Explicitly call vrf notify from walker with peer decommisioned.
    route_walker->VrfWalkNotify(rt->vrf()->get_table_partition(), rt->vrf());
    TaskScheduler::GetInstance()->Start();
    client->WaitForIdle();

    //Delete
    DeleteVmportEnv(input_1, 1, true);
    DelIPAM("vn1");
    client->WaitForIdle();
    DeleteBgpPeer(bgp_peer_ptr);
    client->WaitForIdle();
    bgp_peer.reset();
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
