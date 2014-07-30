/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/task.h"
#include "base/task_annotations.h"
#include "base/test/task_test_util.h"
#include "control-node/control_node.h"
#include "bgp/inet/inet_table.h"
#include "bgp/l3vpn/inetvpn_table.h"
#include "bgp/bgp_config.h"
#include "bgp/bgp_factory.h"
#include "bgp/bgp_log.h"
#include "bgp/bgp_peer.h"
#include "bgp/bgp_peer_membership.h"
#include "bgp/bgp_proto.h"
#include "bgp/bgp_server.h"
#include "bgp/bgp_session_manager.h"
#include "bgp/scheduling_group.h"
#include "bgp/test/bgp_server_test_util.h"
#include "db/db.h"
#include "db/db_partition.h"
#include "io/test/event_manager_test.h"
#include "testing/gunit.h"

using namespace std;

static int gbl_index;

class BgpTestPeer : public BgpPeer {
public:
    BgpTestPeer(BgpServer *server, RoutingInstance *instance,
                const BgpNeighborConfig *config)
       : BgpPeer(server, instance, config),
         policy_(BgpProto::IBGP, RibExportPolicy::BGP, -1, 0),
         index_(gbl_index++) {
    }

    virtual ~BgpTestPeer() { }

    virtual std::string ToString() const {
        std::ostringstream repr;
        repr << "Peer" << index_;
        return repr.str();
    }

    virtual bool SendUpdate(const uint8_t *msg, size_t msgsize) {
        return true;
    }

    virtual bool IsReady() const { return false; }

    BgpProto::BgpPeerType PeerType() const {
        return BgpProto::IBGP;
    }
    virtual uint32_t bgp_identifier() const {
        return 0;
    }

    RibExportPolicy GetRibExportPolicy() { return(policy_); }

private:
    RibExportPolicy policy_;
    int index_;
};

class PeerRibMembershipManagerTest : public PeerRibMembershipManager {
public:

    explicit PeerRibMembershipManagerTest(BgpServer *server)
            : PeerRibMembershipManager(server) {
    }

    void SetQueueDisable(bool disable) {
        event_queue_->set_disable(disable);
    }
};

class PeerMembershipMgrTest : public ::testing::Test {
protected:
    virtual void SetUp() {
        ConcurrencyScope scope("bgp::Config");

        gbl_index = 0;
        evm_.reset(new EventManager());
        server_.reset(new BgpServerTest(evm_.get(), "Local"));
        server_->session_manager()->Initialize(0);

        RoutingInstance *rtinstance;

        //
        // Create red routing instance
        //
        BgpInstanceConfig red_config("red");
        rtinstance = server_->routing_instance_mgr()->CreateRoutingInstance(
                                                          &red_config);
        red_tbl_ = rtinstance->GetTable(Address::INET);

        //
        // Create green routing instance
        //
        BgpInstanceConfig green_config("green");
        rtinstance = server_->routing_instance_mgr()->CreateRoutingInstance(
                                                          &green_config);
        green_tbl_ = rtinstance->GetTable(Address::INET);

        //
        // Create blue routing instance
        //
        BgpInstanceConfig blue_config("blue");
        rtinstance = server_->routing_instance_mgr()->CreateRoutingInstance(
                                                          &blue_config);
        blue_tbl_ = rtinstance->GetTable(Address::INET);

        //
        // Create default routing instance
        //
        BgpInstanceConfig default_config(BgpConfigManager::kMasterInstance);
        rtinstance = server_->routing_instance_mgr()->CreateRoutingInstance(
                                                          &default_config);
        inet_tbl_ = rtinstance->GetTable(Address::INET);
        vpn_tbl_ = rtinstance->GetTable(Address::INETVPN);

        for (int idx = 0; idx < 3; idx++) {
            ostringstream out;
            out << "A" << idx;
            BgpNeighborConfig *config =
                new BgpNeighborConfig(rtinstance->config(), out.str(),
                                      out.str(), &router_);
            BgpTestPeer *peer = static_cast<BgpTestPeer *>(
                rtinstance->peer_manager()->PeerLocate(server(), config));
            peers_.push_back(peer);
            peer_configs_.push_back(config);
            peer_names_.push_back(out.str());
        }
    }

    virtual void TearDown() {
        task_util::WaitForIdle();
        server_->Shutdown();
        TASK_UTIL_ASSERT_EQ(0, server_->routing_instance_mgr()->count());
        evm_->Shutdown();
        task_util::WaitForIdle();
        STLDeleteValues(&peer_configs_);
        task_util::WaitForIdle();
    }

    BgpServer *server() { return server_.get(); }
    int size() { return server()->membership_mgr()->peer_rib_set_.size(); }

    auto_ptr<EventManager> evm_;
    auto_ptr<BgpServerTest> server_;
    vector<BgpTestPeer *> peers_;
    vector<BgpNeighborConfig *> peer_configs_;
    vector<std::string> peer_names_;
    BgpTable *inet_tbl_;
    BgpTable *vpn_tbl_;
    BgpTable *red_tbl_, *green_tbl_, *blue_tbl_;
    SchedulingGroupManager mgr_;
    autogen::BgpRouter router_;

};

// Single peer with inet table.
TEST_F(PeerMembershipMgrTest, SinglePeerSingleTable) {
    PeerRibMembershipManager *mgr = server()->membership_mgr();

    // Make sure we start out clean.
    ASSERT_EQ(size(), 0);

    // Register to inet.
    mgr->Register(peers_[0], inet_tbl_, peers_[0]->GetRibExportPolicy(), -1);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(size() == 1);

    // Unregister from inet.
    mgr->Unregister(peers_[0], inet_tbl_);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(size() == 0);

    // Register to inet.
    mgr->Register(peers_[0], inet_tbl_, peers_[0]->GetRibExportPolicy(), -1);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, size());

    // Duplicate register to inet.
    mgr->Register(peers_[0], inet_tbl_, peers_[0]->GetRibExportPolicy(), -1);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, size());

    // Unregister from inet.
    mgr->Unregister(peers_[0], inet_tbl_);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(size() == 0);

    // Duplicate unregister from inet.
    mgr->Unregister(peers_[0], inet_tbl_);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(size() == 0);

    // Back to back register and unregister to inet.
    mgr->Register(peers_[0], inet_tbl_, peers_[0]->GetRibExportPolicy(), -1);
    mgr->Unregister(peers_[0], inet_tbl_);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(size() == 0);
}

// Single peer with inet and inetvpn table.
TEST_F(PeerMembershipMgrTest, SinglePeerMultipleTable) {
    PeerRibMembershipManager *mgr = server()->membership_mgr();

    // Make sure we start out clean.
    ASSERT_EQ(size(), 0);

    // Register to inet.
    mgr->Register(peers_[0], inet_tbl_, peers_[0]->GetRibExportPolicy(), -1);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(size() == 1);

    // Register to inetvpn.
    mgr->Register(peers_[0], vpn_tbl_, peers_[0]->GetRibExportPolicy(), -1);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(size() == 2);

    // Unregister from both inet and inetvpn.
    mgr->Unregister(peers_[0], inet_tbl_);
    mgr->Unregister(peers_[0], vpn_tbl_);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(size() == 0);

    // Register to both inet and inetvpn.
    mgr->Register(peers_[0], inet_tbl_, peers_[0]->GetRibExportPolicy(), -1);
    mgr->Register(peers_[0], vpn_tbl_, peers_[0]->GetRibExportPolicy(), -1);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(size() == 2);

    // Unregister from inetvpn.
    mgr->Unregister(peers_[0], vpn_tbl_);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(size() == 1);

    // Duplicate unregister from inetvpn.
    mgr->Unregister(peers_[0], vpn_tbl_);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(size() == 1);

    // Register to inetvpn.
    mgr->Register(peers_[0], vpn_tbl_, peers_[0]->GetRibExportPolicy(), -1);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(size() == 2);

    // Unregister from both inet and inetvpn.
    mgr->Unregister(peers_[0], inet_tbl_);
    mgr->Unregister(peers_[0], vpn_tbl_);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(size() == 0);
}

// Single peer with red, green and blue vrf tables.
TEST_F(PeerMembershipMgrTest, SinglePeerDynamicTables) {
    PeerRibMembershipManager *mgr = server()->membership_mgr();

    // Make sure we start out clean.
    ASSERT_EQ(size(), 0);

    // Register to red, green and blue.
    mgr->Register(peers_[0], red_tbl_, peers_[0]->GetRibExportPolicy(), -1);
    mgr->Register(peers_[0], green_tbl_, peers_[0]->GetRibExportPolicy(), -1);
    mgr->Register(peers_[0], blue_tbl_, peers_[0]->GetRibExportPolicy(), -1);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(size() == 3);

    // Unregister from green.
    mgr->Unregister(peers_[0], green_tbl_);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(size() == 2);

    // Duplicate unregister from green.
    mgr->Unregister(peers_[0], green_tbl_);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(size() == 2);

    // Register to green.
    mgr->Register(peers_[0], green_tbl_, peers_[0]->GetRibExportPolicy(), -1);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(size() == 3);

    // Unregister from red green, and blue.
    mgr->Unregister(peers_[0], red_tbl_);
    mgr->Unregister(peers_[0], green_tbl_);
    mgr->Unregister(peers_[0], blue_tbl_);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(size() == 0);

    // Back to back register and unregister to all tables.
    mgr->Register(peers_[0], red_tbl_, peers_[0]->GetRibExportPolicy(), -1);
    mgr->Register(peers_[0], green_tbl_, peers_[0]->GetRibExportPolicy(), -1);
    mgr->Register(peers_[0], blue_tbl_, peers_[0]->GetRibExportPolicy(), -1);
    mgr->Unregister(peers_[0], red_tbl_);
    mgr->Unregister(peers_[0], green_tbl_);
    mgr->Unregister(peers_[0], blue_tbl_);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(size() == 0);
}

// Multiple peers with single vrf table.
TEST_F(PeerMembershipMgrTest, MultiplePeersSingleTable) {
    PeerRibMembershipManager *mgr = server()->membership_mgr();

    // Make sure we start out clean.
    ASSERT_EQ(size(), 0);

    // Register all peers.
    mgr->Register(peers_[0], red_tbl_, peers_[0]->GetRibExportPolicy(), -1);
    mgr->Register(peers_[1], red_tbl_, peers_[1]->GetRibExportPolicy(), -1);
    mgr->Register(peers_[2], red_tbl_, peers_[2]->GetRibExportPolicy(), -1);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(size() == 3);

    // Unregister peer 1.
    mgr->Unregister(peers_[1], red_tbl_);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(size() == 2);

    // Register peer 1.
    mgr->Register(peers_[1], red_tbl_, peers_[1]->GetRibExportPolicy(), -1);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(size() == 3);

    // Unregister peers 0 and 2.
    mgr->Unregister(peers_[0], red_tbl_);
    mgr->Unregister(peers_[2], red_tbl_);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(size() == 1);

    // Register peers 0 and 2.
    mgr->Register(peers_[0], red_tbl_, peers_[0]->GetRibExportPolicy(), -1);
    mgr->Register(peers_[2], red_tbl_, peers_[2]->GetRibExportPolicy(), -1);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(size() == 3);

    // Unregister all peers.
    mgr->Unregister(peers_[2], red_tbl_);
    mgr->Unregister(peers_[0], red_tbl_);
    mgr->Unregister(peers_[1], red_tbl_);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(size() == 0);

    // Back to back register and unregister for all peers.
    mgr->Register(peers_[0], red_tbl_, peers_[2]->GetRibExportPolicy(), -1);
    mgr->Register(peers_[2], red_tbl_, peers_[2]->GetRibExportPolicy(), -1);
    mgr->Register(peers_[1], red_tbl_, peers_[1]->GetRibExportPolicy(), -1);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(size() == 3);
    mgr->Unregister(peers_[1], red_tbl_);
    mgr->Unregister(peers_[0], red_tbl_);
    mgr->Unregister(peers_[2], red_tbl_);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(size() == 0);

    // Register all peers.
    mgr->Register(peers_[0], red_tbl_, peers_[0]->GetRibExportPolicy(), -1);
    mgr->Register(peers_[2], red_tbl_, peers_[2]->GetRibExportPolicy(), -1);
    mgr->Register(peers_[1], red_tbl_, peers_[1]->GetRibExportPolicy(), -1);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(size() == 3);

    // Unregister all peers.
    mgr->Unregister(peers_[0], red_tbl_);
    mgr->Unregister(peers_[1], red_tbl_);
    mgr->Unregister(peers_[2], red_tbl_);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(size() == 0);
}

// Delete a peer with membership request pending
TEST_F(PeerMembershipMgrTest, PeerDeleteWithPendingMembershipRequestPending) {
    PeerRibMembershipManager *mgr = server()->membership_mgr();

    // Make sure we start out clean.
    ASSERT_EQ(size(), 0);

    size_t count = server_->lifetime_manager()->GetQueueDeferCount();

    // Stop membership manager's queue processing
    static_cast<PeerRibMembershipManagerTest *>(
        server_->membership_mgr())->SetQueueDisable(true);

    // Register to inet.
    mgr->Register(peers_[0], inet_tbl_, peers_[0]->GetRibExportPolicy(), -1);
    task_util::WaitForIdle();

    //
    // Make sure that the membership manager queue is not empty
    //
    TASK_UTIL_EXPECT_FALSE(server_->membership_mgr()->IsQueueEmpty());

    //
    // Trigger peer deletion
    //
    peers_[0]->ManagedDelete();

    //
    // Make sure that life time manager does not delete this peer as there is
    // a reference to the peer from membership manager's queue
    //
    TASK_UTIL_EXPECT_TRUE(
        server_->lifetime_manager()->GetQueueDeferCount() > count);

    //
    // Make sure that the membership manager queue is not empty
    //
    TASK_UTIL_EXPECT_FALSE(server_->membership_mgr()->IsQueueEmpty());

    //
    // Make sure that the peer still exists
    //
    TASK_UTIL_EXPECT_EQ(peers_[0],
        server_->FindPeer(BgpConfigManager::kMasterInstance, peer_names_[0]));

    //
    // Enable the membership queue processing and wait for it to get drained
    //
    static_cast<PeerRibMembershipManagerTest *>(
        server_->membership_mgr())->SetQueueDisable(false);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(server_->membership_mgr()->IsQueueEmpty());

    //
    // Make sure that the peer is deleted, after the membership queue is
    // processed
    //
    TASK_UTIL_EXPECT_EQ(static_cast<BgpTestPeer *>(NULL),
        server_->FindPeer(BgpConfigManager::kMasterInstance, peer_names_[0]));
}

// Delete a peer with db-request pending
TEST_F(PeerMembershipMgrTest, PeerDeleteWithDBRequestPending) {
    PeerRibMembershipManager *mgr = server()->membership_mgr();

    // Make sure we start out clean.
    ASSERT_EQ(size(), 0);

    size_t count = server_->lifetime_manager()->GetQueueDeferCount();

    // Register to inet.
    mgr->Register(peers_[0], inet_tbl_, peers_[0]->GetRibExportPolicy(), -1);

    BgpAttrSpec attr_spec;
    BgpAttrPtr attr = server_->attr_db()->Locate(attr_spec);

    //
    // Stop DB request processing
    //
    DBRequest req;
    Ip4Prefix prefix(Ip4Prefix::FromString("192.168.255.0/24"));
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    req.key.reset(new InetTable::RequestKey(prefix, peers_[0]));
    req.data.reset(new InetTable::RequestData(attr, 0, 0));

    DBTablePartBase *tpart = inet_tbl_->GetTablePartition(req.key.get());
    DBPartition *partition = server_->database()->GetPartition(tpart->index());
    partition->SetQueueDisable(true);

    // Enqueue a db request
    inet_tbl_->Enqueue(&req);

    //
    // Trigger peer deletion
    //
    peers_[0]->ManagedDelete();

    //
    // Make sure that life time manager does not delete this peer as there is
    // a reference to the peer from membership manager's queue
    //
    TASK_UTIL_EXPECT_TRUE(
        server_->lifetime_manager()->GetQueueDeferCount() > count);

    //
    // Make sure that the db queue is still not empty
    //
    TASK_UTIL_EXPECT_FALSE(partition->IsDBQueueEmpty());

    //
    // Make sure that the peer still exists
    //
    TASK_UTIL_EXPECT_EQ(peers_[0],
        server_->FindPeer(BgpConfigManager::kMasterInstance, peer_names_[0]));

    //
    // Enable db queue processing
    //
    partition->SetQueueDisable(false);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(partition->IsDBQueueEmpty());

    //
    // Make sure that the peer is deleted, after the membership queue is
    // processed
    //
    TASK_UTIL_EXPECT_EQ(static_cast<BgpTestPeer *>(NULL),
        server_->FindPeer(BgpConfigManager::kMasterInstance, peer_names_[0]));
}

static void SetUp() {
    bgp_log_test::init();
    ControlNode::SetDefaultSchedulingPolicy();
    BgpObjectFactory::Register<BgpPeer>(
        boost::factory<BgpTestPeer *>());
    BgpObjectFactory::Register<PeerRibMembershipManager>(
        boost::factory<PeerRibMembershipManagerTest *>());
}

static void TearDown() {
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    scheduler->Terminate();
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    SetUp();
    int result = RUN_ALL_TESTS();
    TearDown();
    return result;
}
