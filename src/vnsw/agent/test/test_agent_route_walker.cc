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
#include "vrouter/ksync/ksync_init.h"
#include "oper/agent_route_walker.h"
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

IpamInfo ipam_info1[] = {
    {"1.1.1.0", 24, "1.1.1.1"},
};

IpamInfo ipam_info2[] = {
    {"2.2.2.0", 24, "2.2.2.1"},
};

IpamInfo ipam_info3[] = {
    {"3.3.3.0", 24, "3.3.3.1"},
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
        walk_task_context_mismatch_ = false;
        route_table_walk_started_ = false;
        is_vrf_walk_done_ = false;
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

    virtual bool RouteWalker(boost::shared_ptr<AgentRouteWalkerQueueEntry> data) {
        if ((Task::Running()->GetTaskId() != TaskScheduler::GetInstance()->
            GetTaskId("Agent::RouteWalker")) ||
            (Task::Running()->GetTaskInstance() != 0))
            walk_task_context_mismatch_ = true;
        AgentRouteWalker::RouteWalker(data);
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
        route_table_walk_started_ = true;
        assert(AreAllWalksDone() == false);
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
        assert(AreAllWalksDone() == false);
        return true;
    }

    virtual void VrfWalkDone(DBTableBase *part) {
        vrf_notifications_count_++;
        is_vrf_walk_done_ = true;
        AgentRouteWalker::VrfWalkDone(part);
        if (is_vrf_walk_done_ &&
            !route_table_walk_started_ &&
            (queued_walk_done_count() == AgentRouteWalker::kInvalidWalkCount) &&
            AreAllWalksDone())
            assert(0);
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
    int walk_task_instance_;
    string walk_task_name_;
    bool walk_task_context_mismatch_;
    bool route_table_walk_started_;
    bool is_vrf_walk_done_;
    friend class SetupTask;
};

class SetupTask : public Task {
    public:
        SetupTask(AgentRouteWalkerTest *test, std::string name) :
            Task((TaskScheduler::GetInstance()->
                  GetTaskId("db::DBTable")), 0), test_(test),
            test_name_(name) {
        }

        static void AllWalkDone(AgentRouteWalkerTest *test_walker) {
            if (test_walker->is_vrf_walk_done_ && !test_walker->route_table_walk_started_)
                assert(0);
        }

        virtual bool Run() {
            if (test_name_ == "restart_walk_with_2_vrf") {
                test_->StartVrfWalk();
                test_->StartVrfWalk();
            } else if (test_name_ == "cancel_vrf_walk_with_2_vrf") {
                test_->StartVrfWalk();
                test_->CancelVrfWalk();
            } else if (test_name_ == "vrf_state_deleted") {
                test_->WalkDoneCallback(boost::bind(&SetupTask::AllWalkDone,
                                                    test_));
                test_->StartVrfWalk();
            } else if (test_name_ ==
                       "walk_on_deleted_vrf_with_deleted_route_table") {
                test_->StartVrfWalk();
            }
            return true;
        }
    std::string Description() const { return "SetupTask"; }
    private:
        AgentRouteWalkerTest *test_;
        std::string test_name_;
};

TEST_F(AgentRouteWalkerTest, walk_all_routes_wih_no_vrf) {
    client->Reset();
    SetupEnvironment(0);
    StartVrfWalk();
    VerifyNotifications(9, 1, 1, Agent::ROUTE_TABLE_MAX - 1);
    EXPECT_TRUE(walk_task_context_mismatch_ == false);
    walk_task_context_mismatch_ = true;
    DeleteEnvironment(0);
}

TEST_F(AgentRouteWalkerTest, walk_all_routes_wih_1_vrf) {
    client->Reset();
    SetupEnvironment(1);
    StartVrfWalk();
    VerifyNotifications(22, 2, 1, ((Agent::ROUTE_TABLE_MAX - 1) * 2));
    EXPECT_TRUE(walk_task_context_mismatch_ == false);
    walk_task_context_mismatch_ = true;
    DeleteEnvironment(1);
}

TEST_F(AgentRouteWalkerTest, walk_all_routes_with_2_vrf) {
    client->Reset();
    SetupEnvironment(2);
    StartVrfWalk();
    VerifyNotifications(35, 3, 1, ((Agent::ROUTE_TABLE_MAX - 1) * 3));
    EXPECT_TRUE(walk_task_context_mismatch_ == false);
    walk_task_context_mismatch_ = true;
    DeleteEnvironment(2);
}

TEST_F(AgentRouteWalkerTest, walk_all_routes_with_3_vrf) {
    client->Reset();
    SetupEnvironment(3);
    StartVrfWalk();
    VerifyNotifications(48, 4, 1, ((Agent::ROUTE_TABLE_MAX - 1) * 4));
    EXPECT_TRUE(walk_task_context_mismatch_ == false);
    walk_task_context_mismatch_ = true;
    DeleteEnvironment(3);
}

TEST_F(AgentRouteWalkerTest, restart_walk_with_2_vrf) {
    client->Reset();
    SetupEnvironment(2);
    SetupTask * task = new SetupTask(this, "restart_walk_with_2_vrf");
    TaskScheduler::GetInstance()->Enqueue(task);
    WAIT_FOR(100, 1000, IsWalkCompleted() == true);
    EXPECT_TRUE(walk_task_context_mismatch_ == false);
    walk_task_context_mismatch_ = true;
    DeleteEnvironment(2);
}

TEST_F(AgentRouteWalkerTest, cancel_vrf_walk_with_2_vrf) {
    client->Reset();
    SetupEnvironment(2);
    SetupTask * task = new SetupTask(this, "cancel_vrf_walk_with_2_vrf");
    TaskScheduler::GetInstance()->Enqueue(task);
    WAIT_FOR(100, 1000, IsWalkCompleted() == true);
    //TODO validate
    EXPECT_TRUE(walk_task_context_mismatch_ == false);
    walk_task_context_mismatch_ = true;
    DeleteEnvironment(2);
}

TEST_F(AgentRouteWalkerTest, test_setup_teardown) {
}

TEST_F(AgentRouteWalkerTest, vrf_state_deleted) {
    client->Reset();
    SetupEnvironment(1);
    SetupTask * task = new SetupTask(this, "vrf_state_deleted");
    TaskScheduler::GetInstance()->Enqueue(task);
    WAIT_FOR(1000, 1000, IsWalkCompleted() == true);
    DeleteEnvironment(1);
}

TEST_F(AgentRouteWalkerTest, walk_on_deleted_vrf_with_deleted_route_table) {
    client->Reset();
    SetupEnvironment(1);
    SetupTask * task = new SetupTask(this,
                                     "walk_on_deleted_vrf_with_deleted_route_table");
    VrfEntryRef vrf = VrfGet("vrf1");
    EXPECT_TRUE(vrf != NULL);
    DeleteEnvironment(1);
    EXPECT_TRUE(vrf->IsDeleted());
    Agent::GetInstance()->vrf_table()->OnZeroRefcount(vrf.get());
    TaskScheduler::GetInstance()->Enqueue(task);
    WAIT_FOR(1000, 1000, IsWalkCompleted() == true);
    SetupEnvironment(1);
    vrf = NULL;
    DeleteEnvironment(1);
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
