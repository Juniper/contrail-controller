#include "base/os.h"
#include <test/test_basic_scale.h>
#include <controller/controller_route_walker.h>

class ControllerRouteWalkerTest : public ControllerRouteWalker {
public:
    ControllerRouteWalkerTest(Peer *peer) : ControllerRouteWalker(Agent::GetInstance(), peer),
    vrf_count_(0), route_count_(0), walk_done_(false) {
    };
    virtual ~ControllerRouteWalkerTest() { };

    virtual bool VrfWalkNotify(DBTablePartBase *partition, DBEntryBase *e) {
        VrfEntry *vrf = static_cast<VrfEntry *>(e);
        vrf_count_++;
        StartRouteWalk(vrf);
        return true;
    }

    virtual bool RouteWalkNotify(DBTablePartBase *partition, DBEntryBase *e) {
        AgentRoute *route = static_cast<AgentRoute *>(e);
        route_count_++;
        return true;
    }

    void WalkDoneNotifications() {
        walk_done_ = true;
        cout << "DEBUGROUTE " << route_count_ << endl;
    }

    uint32_t vrf_count_;
    uint32_t route_count_;
    bool walk_done_;
};

TEST_F(AgentBasicScaleTest, Basic) {
    client->Reset();
    client->WaitForIdle();

    //Setup 
    XmppConnectionSetUp();
    BuildVmPortEnvironment();

    //Create a walker and pass callback
    Peer *dummy_peer = new Peer(Peer::BGP_PEER, "dummy_peer");
    ControllerRouteWalkerTest *route_walker_test = 
        new ControllerRouteWalkerTest(dummy_peer);
    SetWalkerYield(walker_yield);
    route_walker_test->Start(ControllerRouteWalker::NOTIFYALL, true,
                            boost::bind(&ControllerRouteWalkerTest::WalkDoneNotifications,
                                        route_walker_test));
    WAIT_FOR(10000, 10000, route_walker_test->walk_done_);
    SetWalkerYield(DEFAULT_WALKER_YIELD);
    
    int total_interface = num_vns * num_vms_per_vn;
    int expected_route_count = 6 + (2 * num_vns) + (3 * total_interface); 
    EXPECT_TRUE(expected_route_count == route_walker_test->route_count_);
    route_walker_test->vrf_count_ = route_walker_test->route_count_ = 0;

    //Cleanup
    delete route_walker_test;
    delete dummy_peer;
    DeleteVmPortEnvironment();
}

TEST_F(AgentBasicScaleTest, local_and_remote) {
    client->Reset();
    client->WaitForIdle();

    //Setup 
    XmppConnectionSetUp();
    BuildVmPortEnvironment();

    int total_interface = num_vns * num_vms_per_vn;
    //int num_remote = num_remote;
    int total_v4_routes = Agent::GetInstance()->vrf_table()->
        GetInet4UnicastRouteTable("vrf1")->Size();

    mock_peer[0].get()->AddRemoteV4Routes(num_remote, "vrf1", "vn1", 
                                          "172.0.0.0");
    WAIT_FOR(10000, 10000, (Agent::GetInstance()->vrf_table()->
                            GetInet4UnicastRouteTable("vrf1")->Size() == 
                            (total_v4_routes + num_remote)));

    //Create a walker and pass callback
    Peer *dummy_peer = new Peer(Peer::BGP_PEER, "dummy_peer");
    ControllerRouteWalkerTest *route_walker_test = 
        new ControllerRouteWalkerTest(dummy_peer);
    SetWalkerYield(walker_yield);
    route_walker_test->Start(ControllerRouteWalker::NOTIFYALL, true,
                            boost::bind(&ControllerRouteWalkerTest::WalkDoneNotifications,
                                        route_walker_test));
    WAIT_FOR(10000, 10000, route_walker_test->walk_done_);
    SetWalkerYield(DEFAULT_WALKER_YIELD);
    
    int expected_route_count = 6 + (2 * num_vns) + (3 * total_interface) + 
        num_remote; 
    EXPECT_TRUE(expected_route_count == route_walker_test->route_count_);
    route_walker_test->vrf_count_ = route_walker_test->route_count_ = 0;

    mock_peer[0].get()->DeleteRemoteV4Routes(num_remote, "vrf1",
                                             "172.0.0.0");
    WAIT_FOR(10000, 10000, (Agent::GetInstance()->vrf_table()->
                            GetInet4UnicastRouteTable("vrf1")->Size() ==
                            total_v4_routes));

    //Cleanup
    delete route_walker_test;
    delete dummy_peer;
    DeleteVmPortEnvironment();
}

int main(int argc, char **argv) {
    GETSCALEARGS();
    char wait_time_env[80];
    if (walker_wait_usecs) {
        sprintf(wait_time_env, "DB_WALKER_WAIT_USECS=%d", walker_wait_usecs);
    }

    if ((num_vns * num_vms_per_vn) > MAX_INTERFACES) {
        LOG(DEBUG, "Max interfaces is 200");
        return false;
    }
    if (num_ctrl_peers == 0 || num_ctrl_peers > MAX_CONTROL_PEER) {
        LOG(DEBUG, "Supported values - 1, 2");
        return false;
    }

    client = TestInit(init_file, ksync_init);
    putenv(wait_time_env);

    InitXmppServers();

    int ret = RUN_ALL_TESTS();
    Agent::GetInstance()->event_manager()->Shutdown();
    AsioStop();
    TaskScheduler::GetInstance()->Terminate();
    return ret;
}
