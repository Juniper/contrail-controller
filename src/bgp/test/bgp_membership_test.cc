/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */
#include <boost/scoped_ptr.hpp>
#include <boost/uuid/random_generator.hpp>
#include <tbb/atomic.h>

#include "base/task_annotations.h"
#include "control-node/control_node.h"
#include "bgp/inet/inet_table.h"
#include "bgp/bgp_config_ifmap.h"
#include "bgp/bgp_factory.h"
#include "bgp/bgp_membership.h"
#include "bgp/bgp_session_manager.h"
#include "bgp/scheduling_group.h"
#include "bgp/test/bgp_server_test_util.h"
#include "db/db_partition.h"

using boost::scoped_ptr;
using std::ostringstream;
using std::string;
using std::vector;
using tbb::atomic;

static int gbl_index;

class BgpTestPeer : public BgpPeer {
public:
    BgpTestPeer(BgpServer *server, RoutingInstance *instance,
                const BgpNeighborConfig *config)
       : BgpPeer(server, instance, config),
         policy_(BgpProto::IBGP, RibExportPolicy::BGP, -1, 0),
         index_(gbl_index++) {
         path_cb_count_ = 0;
    }
    virtual ~BgpTestPeer() { }

    virtual string ToString() const {
        ostringstream repr;
        repr << "Peer" << index_;
        return repr.str();
    }

    virtual bool SendUpdate(const uint8_t *msg, size_t msgsize) { return true; }
    virtual bool IsReady() const { return true; }
    BgpProto::BgpPeerType PeerType() const { return BgpProto::IBGP; }
    virtual uint32_t bgp_identifier() const { return 0; }
    RibExportPolicy GetRibExportPolicy() { return policy_; }
    void MembershipRequestCallback(BgpTable *table) { }
    bool MembershipPathCallback(DBTablePartBase *tpart, BgpRoute *route,
        BgpPath *path) {
        path_cb_count_++;
        return false;
    }
    void set_affinity(int affinity) {
        policy_ =
            RibExportPolicy(BgpProto::IBGP, RibExportPolicy::BGP, affinity, 0);
    }
    uint64_t path_cb_count() const { return path_cb_count_; }

private:
    RibExportPolicy policy_;
    int index_;
    atomic<uint64_t> path_cb_count_;
};

class BgpMembershipTest : public ::testing::Test {
protected:
    virtual void SetUp() {
        gbl_index = 0;
        evm_.reset(new EventManager());
        server_.reset(new BgpServerTest(evm_.get(), "Local"));
        server_->session_manager()->Initialize(0);
        mgr_ = server_->membership_mgr();
        walker_ = mgr_->walker();

        ConcurrencyScope scope("bgp::Config");
        RoutingInstance *rtinstance = NULL;

        // Get default routing instance.
        rtinstance =
            server_->routing_instance_mgr()->GetDefaultRoutingInstance();
        assert(rtinstance);
        inet_tbl_ = rtinstance->GetTable(Address::INET);
        vpn_tbl_ = rtinstance->GetTable(Address::INETVPN);

        // Create red routing instance.
        BgpInstanceConfig red_config("red");
        rtinstance = server_->routing_instance_mgr()->CreateRoutingInstance(
            &red_config);
        red_tbl_ = rtinstance->GetTable(Address::INET);

        // Create gray routing instance.
        BgpInstanceConfig gray_config("gray");
        rtinstance = server_->routing_instance_mgr()->CreateRoutingInstance(
            &gray_config);
        gray_tbl_ = rtinstance->GetTable(Address::INET);

        // Create blue routing instance.
        BgpInstanceConfig blue_config("blue");
        rtinstance = server_->routing_instance_mgr()->CreateRoutingInstance(
            &blue_config);
        blue_tbl_ = rtinstance->GetTable(Address::INET);

        CreatePeers();
        task_util::WaitForIdle();
    }

    void CreatePeers() {
        RoutingInstance *rtinstance =
            server_->routing_instance_mgr()->GetRoutingInstance(
                BgpConfigManager::kMasterInstance);
        for (int idx = 0; idx < 3; idx++) {
            ostringstream out;
            out << "A" << idx;
            BgpNeighborConfig *config = new BgpNeighborConfig();
            config->set_instance_name(rtinstance->name());
            config->set_name(out.str());
            boost::uuids::random_generator gen;
            config->set_uuid(UuidToString(gen()));
            BgpTestPeer *peer = static_cast<BgpTestPeer *>(
                rtinstance->peer_manager()->PeerLocate(server_.get(), config));
            peers_.push_back(peer);
            peer_configs_.push_back(config);
            peer_names_.push_back(out.str());
        }
    }

    void UpdatePeers() {
        for (int idx = 0; idx < 3; idx++) {
            peers_[idx]->set_affinity(idx);
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

    string BuildPrefix(int index) const {
        assert(index <= 65535);
        string byte3 = integerToString(index / 256);
        string byte4 = integerToString(index % 256);
        string prefix("10.1.");
        uint8_t plen = Address::kMaxV4PrefixLen;
        return prefix + byte3 + "." + byte4 + "/" + integerToString(plen);
    }

    void Register(BgpTestPeer *peer, BgpTable *table) {
        task_util::TaskFire(
            boost::bind(&BgpMembershipManager::Register, mgr_, peer, table,
                peer->GetRibExportPolicy(), -1), "bgp::StateMachine");
    }

    void RegisterRibIn(BgpTestPeer *peer, BgpTable *table) {
        task_util::TaskFire(boost::bind(&BgpMembershipManager::RegisterRibIn,
            mgr_, peer, table), "bgp::StateMachine");
    }

    void Unregister(BgpTestPeer *peer, BgpTable *table) {
        task_util::TaskFire(boost::bind(&BgpMembershipManager::Unregister,
            mgr_, peer, table), "bgp::StateMachine");
    }

    void UnregisterRibIn(BgpTestPeer *peer, BgpTable *table) {
        task_util::TaskFire(boost::bind(&BgpMembershipManager::UnregisterRibIn,
            mgr_, peer, table), "bgp::StateMachine");
    }

    void UnregisterRibOut(BgpTestPeer *peer, BgpTable *table) {
        task_util::TaskFire(boost::bind(&BgpMembershipManager::UnregisterRibOut,
            mgr_, peer, table), "bgp::StateMachine");
    }

    void WalkRibIn(BgpTestPeer *peer, BgpTable *table) {
        task_util::TaskFire(boost::bind(&BgpMembershipManager::WalkRibIn,
            mgr_, peer, table), "bgp::StateMachine");
    }

    void AddRoute(BgpTestPeer *peer, BgpTable *table,
        const string &prefix_str, const string &nexthop_str) {
        boost::system::error_code ec;
        Ip4Prefix prefix = Ip4Prefix::FromString(prefix_str, &ec);
        EXPECT_FALSE(ec);
        DBRequest request;
        request.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
        request.key.reset(new InetTable::RequestKey(prefix, peer));

        BgpAttrSpec attr_spec;
        BgpAttrOrigin origin_spec(BgpAttrOrigin::INCOMPLETE);
        attr_spec.push_back(&origin_spec);

        BgpAttrLocalPref local_pref(100);
        attr_spec.push_back(&local_pref);

        IpAddress nh_addr = IpAddress::from_string(nexthop_str, ec);
        EXPECT_FALSE(ec);
        BgpAttrNextHop nh_spec(nh_addr);
        attr_spec.push_back(&nh_spec);

        BgpAttrPtr attr = server_->attr_db()->Locate(attr_spec);
        request.data.reset(new InetTable::RequestData(attr, 0, 0));
        table->Enqueue(&request);
    }

    void DeleteRoute(BgpTestPeer *peer, BgpTable *table,
        const string &prefix_str) {
        boost::system::error_code ec;
        Ip4Prefix prefix = Ip4Prefix::FromString(prefix_str, &ec);
        EXPECT_FALSE(ec);

        DBRequest request;
        request.oper = DBRequest::DB_ENTRY_DELETE;
        request.key.reset(new InetTable::RequestKey(prefix, peer));
        table->Enqueue(&request);
    }

    void SetQueueDisable(bool value) { mgr_->SetQueueDisable(value); }
    void SetWalkerDisable(bool value) { walker_->SetQueueDisable(value); }
    bool IsWalkerQueueEmpty() { return walker_->IsQueueEmpty(); }
    size_t GetWalkerQueueSize() { return walker_->GetQueueSize(); }
    size_t GetWalkerPeerListSize() { return walker_->GetPeerListSize(); }
    size_t GetWalkerPeerRibListSize() { return walker_->GetPeerRibListSize(); }
    size_t GetWalkerRibOutStateListSize() {
        return walker_->GetRibOutStateListSize();
    }
    void WalkerPostponeWalk() { walker_->PostponeWalk(); }
    void WalkerResumeWalk() { walker_->ResumeWalk(); }

    BgpMembershipManager *mgr_;
    BgpMembershipManager::Walker *walker_;
    scoped_ptr<EventManager> evm_;
    scoped_ptr<BgpServerTest> server_;
    vector<BgpTestPeer *> peers_;
    vector<BgpNeighborConfig *> peer_configs_;
    vector<string> peer_names_;
    BgpTable *inet_tbl_;
    BgpTable *vpn_tbl_;
    BgpTable *red_tbl_, *gray_tbl_, *blue_tbl_;
    SchedulingGroupManager sg_mgr_;
};

//
// Verify register and unregister for 1 single peer to single table.
//
TEST_F(BgpMembershipTest, Basic) {
    uint64_t blue_walk_count = blue_tbl_->walk_complete_count();

    // Register to blue.
    Register(peers_[0], blue_tbl_);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(mgr_->GetRegistrationInfo(peers_[0], blue_tbl_));
    TASK_UTIL_EXPECT_EQ(1, mgr_->GetMembershipCount());
    TASK_UTIL_EXPECT_EQ(blue_walk_count + 1, blue_tbl_->walk_complete_count());

    // Unregister from blue.
    Unregister(peers_[0], blue_tbl_);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_FALSE(mgr_->GetRegistrationInfo(peers_[0], blue_tbl_));
    TASK_UTIL_EXPECT_EQ(0, mgr_->GetMembershipCount());
    TASK_UTIL_EXPECT_EQ(blue_walk_count + 2, blue_tbl_->walk_complete_count());
}

//
// Verify register and unregister for 1 single peer to multiple tables.
// Registration and unregistration requests to both tables are made together.
//
TEST_F(BgpMembershipTest, MultipleTables1) {
    uint64_t blue_walk_count = blue_tbl_->walk_complete_count();
    uint64_t red_walk_count = red_tbl_->walk_complete_count();

    Register(peers_[0], blue_tbl_);
    Register(peers_[0], red_tbl_);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(mgr_->GetRegistrationInfo(peers_[0], blue_tbl_));
    TASK_UTIL_EXPECT_TRUE(mgr_->GetRegistrationInfo(peers_[0], red_tbl_));
    TASK_UTIL_EXPECT_EQ(2, mgr_->GetMembershipCount());
    TASK_UTIL_EXPECT_EQ(blue_walk_count + 1, blue_tbl_->walk_complete_count());
    TASK_UTIL_EXPECT_EQ(red_walk_count + 1, red_tbl_->walk_complete_count());

    Unregister(peers_[0], blue_tbl_);
    Unregister(peers_[0], red_tbl_);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_FALSE(mgr_->GetRegistrationInfo(peers_[0], blue_tbl_));
    TASK_UTIL_EXPECT_FALSE(mgr_->GetRegistrationInfo(peers_[0], red_tbl_));
    TASK_UTIL_EXPECT_EQ(0, mgr_->GetMembershipCount());
    TASK_UTIL_EXPECT_EQ(blue_walk_count + 2, blue_tbl_->walk_complete_count());
    TASK_UTIL_EXPECT_EQ(red_walk_count + 2, red_tbl_->walk_complete_count());
}

//
// Verify register and unregister for 1 single peer to multiple tables.
// Registration and unregistration requests to both tables are made separately.
//
TEST_F(BgpMembershipTest, MultipleTables2) {
    uint64_t blue_walk_count = blue_tbl_->walk_complete_count();
    uint64_t red_walk_count = red_tbl_->walk_complete_count();

    Register(peers_[0], blue_tbl_);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(mgr_->GetRegistrationInfo(peers_[0], blue_tbl_));
    TASK_UTIL_EXPECT_EQ(1, mgr_->GetMembershipCount());
    TASK_UTIL_EXPECT_EQ(blue_walk_count + 1, blue_tbl_->walk_complete_count());

    Register(peers_[0], red_tbl_);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(mgr_->GetRegistrationInfo(peers_[0], red_tbl_));
    TASK_UTIL_EXPECT_EQ(2, mgr_->GetMembershipCount());
    TASK_UTIL_EXPECT_EQ(red_walk_count + 1, red_tbl_->walk_complete_count());

    Unregister(peers_[0], blue_tbl_);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_FALSE(mgr_->GetRegistrationInfo(peers_[0], blue_tbl_));
    TASK_UTIL_EXPECT_EQ(1, mgr_->GetMembershipCount());
    TASK_UTIL_EXPECT_EQ(blue_walk_count + 2, blue_tbl_->walk_complete_count());

    Unregister(peers_[0], red_tbl_);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_FALSE(mgr_->GetRegistrationInfo(peers_[0], red_tbl_));
    TASK_UTIL_EXPECT_EQ(0, mgr_->GetMembershipCount());
    TASK_UTIL_EXPECT_EQ(red_walk_count + 2, red_tbl_->walk_complete_count());
}

//
// Verify register and unregister for 1 single peer to multiple tables.
// Registration and unregistration requests to all tables are made when the
// walker is disabled. Exercise the case where all requests are queued in the
// walker and it needs to schedule one walk after another, without any new
// external triggers other than notification from table walk infrastructure.
//
TEST_F(BgpMembershipTest, MultipleTables3) {
    uint64_t blue_walk_count = blue_tbl_->walk_complete_count();
    uint64_t red_walk_count = red_tbl_->walk_complete_count();
    uint64_t gray_walk_count = gray_tbl_->walk_complete_count();

    // Disable walker.
    SetWalkerDisable(true);

    Register(peers_[0], blue_tbl_);
    Register(peers_[0], red_tbl_);
    Register(peers_[0], gray_tbl_);
    task_util::WaitForIdle();

    // Enable walker.
    SetWalkerDisable(false);

    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(mgr_->GetRegistrationInfo(peers_[0], blue_tbl_));
    TASK_UTIL_EXPECT_TRUE(mgr_->GetRegistrationInfo(peers_[0], red_tbl_));
    TASK_UTIL_EXPECT_TRUE(mgr_->GetRegistrationInfo(peers_[0], gray_tbl_));
    TASK_UTIL_EXPECT_EQ(3, mgr_->GetMembershipCount());
    TASK_UTIL_EXPECT_EQ(blue_walk_count + 1, blue_tbl_->walk_complete_count());
    TASK_UTIL_EXPECT_EQ(red_walk_count + 1, red_tbl_->walk_complete_count());
    TASK_UTIL_EXPECT_EQ(gray_walk_count + 1, gray_tbl_->walk_complete_count());

    // Disable walker.
    SetWalkerDisable(true);

    Unregister(peers_[0], blue_tbl_);
    Unregister(peers_[0], red_tbl_);
    Unregister(peers_[0], gray_tbl_);
    task_util::WaitForIdle();

    // Enable walker.
    SetWalkerDisable(false);

    TASK_UTIL_EXPECT_FALSE(mgr_->GetRegistrationInfo(peers_[0], blue_tbl_));
    TASK_UTIL_EXPECT_FALSE(mgr_->GetRegistrationInfo(peers_[0], red_tbl_));
    TASK_UTIL_EXPECT_FALSE(mgr_->GetRegistrationInfo(peers_[0], gray_tbl_));
    TASK_UTIL_EXPECT_EQ(0, mgr_->GetMembershipCount());
    TASK_UTIL_EXPECT_EQ(blue_walk_count + 2, blue_tbl_->walk_complete_count());
    TASK_UTIL_EXPECT_EQ(red_walk_count + 2, red_tbl_->walk_complete_count());
    TASK_UTIL_EXPECT_EQ(gray_walk_count + 2, gray_tbl_->walk_complete_count());
}

//
// Verify register and unregister for RibIn only.
//
TEST_F(BgpMembershipTest, RibIn) {
    static const int kRouteCount = 8;
    uint64_t blue_walk_count = blue_tbl_->walk_complete_count();

    RegisterRibIn(peers_[0], blue_tbl_);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(mgr_->GetRegistrationInfo(peers_[0], blue_tbl_));
    TASK_UTIL_EXPECT_EQ(1, mgr_->GetMembershipCount());
    TASK_UTIL_EXPECT_EQ(blue_walk_count, blue_tbl_->walk_complete_count());
    TASK_UTIL_EXPECT_EQ(0, peers_[0]->path_cb_count());

    // Add paths from peer.
    for (int idx = 0; idx < kRouteCount; idx++) {
        AddRoute(peers_[0], blue_tbl_, BuildPrefix(idx), "192.168.1.0");
    }
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(kRouteCount, blue_tbl_->Size());

    // Unregister RibIn.
    UnregisterRibIn(peers_[0], blue_tbl_);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_FALSE(mgr_->GetRegistrationInfo(peers_[0], blue_tbl_));
    TASK_UTIL_EXPECT_EQ(0, mgr_->GetMembershipCount());
    TASK_UTIL_EXPECT_EQ(blue_walk_count + 1, blue_tbl_->walk_complete_count());
    TASK_UTIL_EXPECT_EQ(kRouteCount, peers_[0]->path_cb_count());

    // Delete paths from peer.
    // The paths would normally be deleted during RibIn walk by the client.
    for (int idx = 0; idx < kRouteCount; idx++) {
        DeleteRoute(peers_[0], blue_tbl_, BuildPrefix(idx));
    }
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(0, blue_tbl_->Size());
}

//
// Verify register and separate unregister for RibOut and RibIn when paths
// have been added by peer.
//
TEST_F(BgpMembershipTest, UnregisterRibOutWithPaths) {
    static const int kRouteCount = 8;
    uint64_t blue_walk_count = blue_tbl_->walk_complete_count();

    // Register.
    Register(peers_[0], blue_tbl_);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(mgr_->GetRegistrationInfo(peers_[0], blue_tbl_));
    TASK_UTIL_EXPECT_EQ(1, mgr_->GetMembershipCount());
    TASK_UTIL_EXPECT_EQ(blue_walk_count + 1, blue_tbl_->walk_complete_count());

    // Add paths from peer.
    for (int idx = 0; idx < kRouteCount; idx++) {
        AddRoute(peers_[0], blue_tbl_, BuildPrefix(idx), "192.168.1.0");
    }
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(kRouteCount, blue_tbl_->Size());

    // Unregister RibOut.
    UnregisterRibOut(peers_[0], blue_tbl_);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(mgr_->GetRegistrationInfo(peers_[0], blue_tbl_));
    TASK_UTIL_EXPECT_EQ(1, mgr_->GetMembershipCount());
    TASK_UTIL_EXPECT_EQ(blue_walk_count + 2, blue_tbl_->walk_complete_count());
    TASK_UTIL_EXPECT_EQ(kRouteCount, peers_[0]->path_cb_count());

    // Delete paths from peer.
    for (int idx = 0; idx < kRouteCount; idx++) {
        DeleteRoute(peers_[0], blue_tbl_, BuildPrefix(idx));
    }
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(0, blue_tbl_->Size());

    // Unregister RibIn.
    UnregisterRibIn(peers_[0], blue_tbl_);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_FALSE(mgr_->GetRegistrationInfo(peers_[0], blue_tbl_));
    TASK_UTIL_EXPECT_EQ(0, mgr_->GetMembershipCount());
    TASK_UTIL_EXPECT_EQ(blue_walk_count + 3, blue_tbl_->walk_complete_count());
}

//
// Verify sequence used in graceful restart i.e. register, then unregister for
// RibOut and then register again.
//
TEST_F(BgpMembershipTest, GracefulRestart) {
    static const int kRouteCount = 8;
    uint64_t blue_walk_count = blue_tbl_->walk_complete_count();

    // Register.
    Register(peers_[0], blue_tbl_);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(mgr_->GetRegistrationInfo(peers_[0], blue_tbl_));
    TASK_UTIL_EXPECT_EQ(1, mgr_->GetMembershipCount());
    TASK_UTIL_EXPECT_EQ(blue_walk_count + 1, blue_tbl_->walk_complete_count());

    // Add paths from peer.
    for (int idx = 0; idx < kRouteCount; idx++) {
        AddRoute(peers_[0], blue_tbl_, BuildPrefix(idx), "192.168.1.0");
    }
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(kRouteCount, blue_tbl_->Size());

    // Unregister RibOut.
    UnregisterRibOut(peers_[0], blue_tbl_);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(mgr_->GetRegistrationInfo(peers_[0], blue_tbl_));
    TASK_UTIL_EXPECT_EQ(1, mgr_->GetMembershipCount());
    TASK_UTIL_EXPECT_EQ(blue_walk_count + 2, blue_tbl_->walk_complete_count());
    TASK_UTIL_EXPECT_EQ(kRouteCount, peers_[0]->path_cb_count());

    // Register.
    Register(peers_[0], blue_tbl_);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(mgr_->GetRegistrationInfo(peers_[0], blue_tbl_));
    TASK_UTIL_EXPECT_EQ(1, mgr_->GetMembershipCount());
    TASK_UTIL_EXPECT_EQ(blue_walk_count + 3, blue_tbl_->walk_complete_count());
    TASK_UTIL_EXPECT_EQ(kRouteCount, peers_[0]->path_cb_count());

    // Delete paths from peer.
    for (int idx = 0; idx < kRouteCount; idx++) {
        DeleteRoute(peers_[0], blue_tbl_, BuildPrefix(idx));
    }
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(0, blue_tbl_->Size());

    // Unregister.
    Unregister(peers_[0], blue_tbl_);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_FALSE(mgr_->GetRegistrationInfo(peers_[0], blue_tbl_));
    TASK_UTIL_EXPECT_EQ(0, mgr_->GetMembershipCount());
    TASK_UTIL_EXPECT_EQ(blue_walk_count + 4, blue_tbl_->walk_complete_count());
}

//
// Verify WalkRibIn functionality.
//
TEST_F(BgpMembershipTest, WalkRibIn) {
    static const int kRouteCount = 8;
    uint64_t blue_walk_count = blue_tbl_->walk_complete_count();

    // Register.
    Register(peers_[0], blue_tbl_);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(mgr_->GetRegistrationInfo(peers_[0], blue_tbl_));
    TASK_UTIL_EXPECT_EQ(1, mgr_->GetMembershipCount());
    TASK_UTIL_EXPECT_EQ(blue_walk_count + 1, blue_tbl_->walk_complete_count());

    // Add paths from peer.
    for (int idx = 0; idx < kRouteCount; idx++) {
        AddRoute(peers_[0], blue_tbl_, BuildPrefix(idx), "192.168.1.0");
    }
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(kRouteCount, blue_tbl_->Size());

    // Walk the blue table.
    WalkRibIn(peers_[0], blue_tbl_);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(blue_walk_count + 2, blue_tbl_->walk_complete_count());
    TASK_UTIL_EXPECT_EQ(kRouteCount, peers_[0]->path_cb_count());

    // Delete paths from peer.
    for (int idx = 0; idx < kRouteCount; idx++) {
        DeleteRoute(peers_[0], blue_tbl_, BuildPrefix(idx));
    }
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(0, blue_tbl_->Size());

    // Unregister.
    Unregister(peers_[0], blue_tbl_);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_FALSE(mgr_->GetRegistrationInfo(peers_[0], blue_tbl_));
    TASK_UTIL_EXPECT_EQ(0, mgr_->GetMembershipCount());
    TASK_UTIL_EXPECT_EQ(blue_walk_count + 3, blue_tbl_->walk_complete_count());
}

//
// Verify register/unregister of multiple peers to single table.
// Register for peers should be combined into single table walk.
// Unregister for peers should be combined into single table walk.
//
TEST_F(BgpMembershipTest, MultiplePeers1) {
    uint64_t blue_walk_count = blue_tbl_->walk_complete_count();

    // Disable walker.
    SetWalkerDisable(true);

    // Register all peers.
    Register(peers_[0], blue_tbl_);
    Register(peers_[1], blue_tbl_);
    Register(peers_[2], blue_tbl_);
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(1, GetWalkerQueueSize());
    TASK_UTIL_EXPECT_EQ(blue_walk_count, blue_tbl_->walk_complete_count());

    // Enable walker.
    SetWalkerDisable(false);
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(0, GetWalkerQueueSize());
    TASK_UTIL_EXPECT_TRUE(mgr_->GetRegistrationInfo(peers_[0], blue_tbl_));
    TASK_UTIL_EXPECT_TRUE(mgr_->GetRegistrationInfo(peers_[1], blue_tbl_));
    TASK_UTIL_EXPECT_TRUE(mgr_->GetRegistrationInfo(peers_[2], blue_tbl_));
    TASK_UTIL_EXPECT_EQ(3, mgr_->GetMembershipCount());
    TASK_UTIL_EXPECT_EQ(blue_walk_count + 1, blue_tbl_->walk_complete_count());

    // Disable walker.
    SetWalkerDisable(true);

    // Unregister all peers.
    Unregister(peers_[0], blue_tbl_);
    Unregister(peers_[1], blue_tbl_);
    Unregister(peers_[2], blue_tbl_);
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(1, GetWalkerQueueSize());
    TASK_UTIL_EXPECT_EQ(blue_walk_count + 1, blue_tbl_->walk_complete_count());

    // Enable walker.
    SetWalkerDisable(false);
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(0, GetWalkerQueueSize());
    TASK_UTIL_EXPECT_FALSE(mgr_->GetRegistrationInfo(peers_[0], blue_tbl_));
    TASK_UTIL_EXPECT_FALSE(mgr_->GetRegistrationInfo(peers_[1], blue_tbl_));
    TASK_UTIL_EXPECT_FALSE(mgr_->GetRegistrationInfo(peers_[2], blue_tbl_));
    TASK_UTIL_EXPECT_EQ(0, mgr_->GetMembershipCount());
    TASK_UTIL_EXPECT_EQ(blue_walk_count + 2, blue_tbl_->walk_complete_count());
}

//
// Verify register/unregister of multiple peers to single table.
// Register and unregister for peers should be combined into 1 table walk.
//
TEST_F(BgpMembershipTest, MultiplePeers2) {
    uint64_t blue_walk_count = blue_tbl_->walk_complete_count();

    // Register first peer.
    Register(peers_[0], blue_tbl_);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(0, GetWalkerQueueSize());
    TASK_UTIL_EXPECT_EQ(1, mgr_->GetMembershipCount());
    TASK_UTIL_EXPECT_EQ(blue_walk_count + 1, blue_tbl_->walk_request_count());
    TASK_UTIL_EXPECT_EQ(blue_walk_count + 1, blue_tbl_->walk_complete_count());

    // Disable walker.
    SetWalkerDisable(true);

    // Register remaining peers and unregister first peer.
    Register(peers_[1], blue_tbl_);
    Register(peers_[2], blue_tbl_);
    Unregister(peers_[0], blue_tbl_);
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(1, GetWalkerQueueSize());
    TASK_UTIL_EXPECT_EQ(3, mgr_->GetMembershipCount());
    TASK_UTIL_EXPECT_EQ(blue_walk_count + 1, blue_tbl_->walk_request_count());
    TASK_UTIL_EXPECT_EQ(blue_walk_count + 1, blue_tbl_->walk_complete_count());

    // Enable walker.
    SetWalkerDisable(false);
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(0, GetWalkerQueueSize());
    TASK_UTIL_EXPECT_EQ(2, mgr_->GetMembershipCount());
    TASK_UTIL_EXPECT_EQ(blue_walk_count + 2, blue_tbl_->walk_request_count());
    TASK_UTIL_EXPECT_EQ(blue_walk_count + 2, blue_tbl_->walk_complete_count());

    // Disable walker.
    SetWalkerDisable(true);

    // Unregister remaining peers.
    Unregister(peers_[1], blue_tbl_);
    Unregister(peers_[2], blue_tbl_);
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(1, GetWalkerQueueSize());
    TASK_UTIL_EXPECT_EQ(2, mgr_->GetMembershipCount());
    TASK_UTIL_EXPECT_EQ(blue_walk_count + 2, blue_tbl_->walk_request_count());
    TASK_UTIL_EXPECT_EQ(blue_walk_count + 2, blue_tbl_->walk_complete_count());

    // Enable walker.
    SetWalkerDisable(false);
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(0, GetWalkerQueueSize());
    TASK_UTIL_EXPECT_EQ(0, mgr_->GetMembershipCount());
    TASK_UTIL_EXPECT_EQ(blue_walk_count + 3, blue_tbl_->walk_request_count());
    TASK_UTIL_EXPECT_EQ(blue_walk_count + 3, blue_tbl_->walk_complete_count());
}

//
// Verify register/unregister of multiple peers to single table.
// Register for remaining peers needs another table walk if walk has already
// been started for some peers.
//
TEST_F(BgpMembershipTest, MultiplePeers3) {
    uint64_t blue_walk_count = blue_tbl_->walk_complete_count();

    // Postpone walk.
    WalkerPostponeWalk();

    // Register first peer.
    Register(peers_[0], blue_tbl_);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(0, GetWalkerQueueSize());
    TASK_UTIL_EXPECT_EQ(blue_walk_count, blue_tbl_->walk_request_count());
    TASK_UTIL_EXPECT_EQ(blue_walk_count, blue_tbl_->walk_complete_count());

    // Register remaining peers.
    Register(peers_[1], blue_tbl_);
    Register(peers_[2], blue_tbl_);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, GetWalkerQueueSize());
    TASK_UTIL_EXPECT_EQ(blue_walk_count, blue_tbl_->walk_request_count());
    TASK_UTIL_EXPECT_EQ(blue_walk_count, blue_tbl_->walk_complete_count());

    // Resume walk.
    WalkerResumeWalk();
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(0, GetWalkerQueueSize());
    TASK_UTIL_EXPECT_EQ(3, mgr_->GetMembershipCount());
    TASK_UTIL_EXPECT_EQ(blue_walk_count + 2, blue_tbl_->walk_request_count());
    TASK_UTIL_EXPECT_EQ(blue_walk_count + 2, blue_tbl_->walk_complete_count());

    // Disable walker.
    SetWalkerDisable(true);

    // Unregister all peers.
    Unregister(peers_[0], blue_tbl_);
    Unregister(peers_[1], blue_tbl_);
    Unregister(peers_[2], blue_tbl_);
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(1, GetWalkerQueueSize());
    TASK_UTIL_EXPECT_EQ(blue_walk_count + 2, blue_tbl_->walk_request_count());
    TASK_UTIL_EXPECT_EQ(blue_walk_count + 2, blue_tbl_->walk_complete_count());

    // Enable walker.
    SetWalkerDisable(false);
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(0, GetWalkerQueueSize());
    TASK_UTIL_EXPECT_EQ(0, mgr_->GetMembershipCount());
    TASK_UTIL_EXPECT_EQ(blue_walk_count + 3, blue_tbl_->walk_request_count());
    TASK_UTIL_EXPECT_EQ(blue_walk_count + 3, blue_tbl_->walk_complete_count());
}

//
// Verify register/unregister of multiple peers to single table.
// Unregister for remaining peers needs another table walk if walk has already
// been started for some peers.
//
TEST_F(BgpMembershipTest, MultiplePeers4) {
    uint64_t blue_walk_count = blue_tbl_->walk_complete_count();

    // Disable walker.
    SetWalkerDisable(true);

    // Register all peers.
    Register(peers_[0], blue_tbl_);
    Register(peers_[1], blue_tbl_);
    Register(peers_[2], blue_tbl_);
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(1, GetWalkerQueueSize());
    TASK_UTIL_EXPECT_EQ(blue_walk_count, blue_tbl_->walk_complete_count());

    // Enable walker.
    SetWalkerDisable(false);
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(0, GetWalkerQueueSize());
    TASK_UTIL_EXPECT_EQ(3, mgr_->GetMembershipCount());
    TASK_UTIL_EXPECT_EQ(blue_walk_count + 1, blue_tbl_->walk_request_count());
    TASK_UTIL_EXPECT_EQ(blue_walk_count + 1, blue_tbl_->walk_complete_count());

    // Postpone walk.
    WalkerPostponeWalk();
    task_util::WaitForIdle();

    // Unregister first peer.
    Unregister(peers_[0], blue_tbl_);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(0, GetWalkerQueueSize());
    TASK_UTIL_EXPECT_EQ(blue_walk_count + 1, blue_tbl_->walk_request_count());
    TASK_UTIL_EXPECT_EQ(blue_walk_count + 1, blue_tbl_->walk_complete_count());

    // Unregister remaining peers.
    Unregister(peers_[1], blue_tbl_);
    Unregister(peers_[2], blue_tbl_);
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(1, GetWalkerQueueSize());
    TASK_UTIL_EXPECT_EQ(blue_walk_count + 1, blue_tbl_->walk_request_count());
    TASK_UTIL_EXPECT_EQ(blue_walk_count + 1, blue_tbl_->walk_complete_count());

    // Resume walk.
    WalkerResumeWalk();
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(0, GetWalkerQueueSize());
    TASK_UTIL_EXPECT_EQ(0, mgr_->GetMembershipCount());
    TASK_UTIL_EXPECT_EQ(blue_walk_count + 3, blue_tbl_->walk_request_count());
    TASK_UTIL_EXPECT_EQ(blue_walk_count + 3, blue_tbl_->walk_complete_count());
}

//
// Verify register/unregister for peers with different RibOuts is combined
// into single table walk.
//
TEST_F(BgpMembershipTest, MultiplePeersDifferentRibOuts) {
    UpdatePeers();
    uint64_t blue_walk_count = blue_tbl_->walk_complete_count();

    // Disable walker.
    SetWalkerDisable(true);

    // Register all peers.
    Register(peers_[0], blue_tbl_);
    Register(peers_[1], blue_tbl_);
    Register(peers_[2], blue_tbl_);
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(1, GetWalkerQueueSize());
    TASK_UTIL_EXPECT_EQ(blue_walk_count, blue_tbl_->walk_request_count());
    TASK_UTIL_EXPECT_EQ(blue_walk_count, blue_tbl_->walk_complete_count());

    // Postpone walk.
    WalkerPostponeWalk();

    // Enable walker.
    SetWalkerDisable(false);
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(0, GetWalkerQueueSize());
    TASK_UTIL_EXPECT_EQ(3, GetWalkerRibOutStateListSize());
    TASK_UTIL_EXPECT_EQ(blue_walk_count, blue_tbl_->walk_request_count());
    TASK_UTIL_EXPECT_EQ(blue_walk_count, blue_tbl_->walk_complete_count());

    // Resume walk.
    WalkerResumeWalk();
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(0, GetWalkerQueueSize());
    TASK_UTIL_EXPECT_EQ(0, GetWalkerRibOutStateListSize());
    TASK_UTIL_EXPECT_EQ(3, mgr_->GetMembershipCount());
    TASK_UTIL_EXPECT_EQ(blue_walk_count + 1, blue_tbl_->walk_request_count());
    TASK_UTIL_EXPECT_EQ(blue_walk_count + 1, blue_tbl_->walk_complete_count());

    // Disable walker.
    SetWalkerDisable(true);

    // Unregister all peers.
    Unregister(peers_[0], blue_tbl_);
    Unregister(peers_[1], blue_tbl_);
    Unregister(peers_[2], blue_tbl_);
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(1, GetWalkerQueueSize());
    TASK_UTIL_EXPECT_EQ(blue_walk_count + 1, blue_tbl_->walk_request_count());
    TASK_UTIL_EXPECT_EQ(blue_walk_count + 1, blue_tbl_->walk_complete_count());

    // Postpone walk.
    WalkerPostponeWalk();

    // Enable walker.
    SetWalkerDisable(false);
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(0, GetWalkerQueueSize());
    TASK_UTIL_EXPECT_EQ(3, GetWalkerRibOutStateListSize());
    TASK_UTIL_EXPECT_EQ(blue_walk_count + 1, blue_tbl_->walk_request_count());
    TASK_UTIL_EXPECT_EQ(blue_walk_count + 1, blue_tbl_->walk_complete_count());

    // Resume walk.
    WalkerResumeWalk();
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(0, GetWalkerQueueSize());
    TASK_UTIL_EXPECT_EQ(0, GetWalkerRibOutStateListSize());
    TASK_UTIL_EXPECT_EQ(0, mgr_->GetMembershipCount());
    TASK_UTIL_EXPECT_EQ(blue_walk_count + 2, blue_tbl_->walk_request_count());
    TASK_UTIL_EXPECT_EQ(blue_walk_count + 2, blue_tbl_->walk_complete_count());
}

//
// Verify WalkRibIn functionality for multiple peers.
// Walk requests from multiple peers can be combined into single table walk.
//
TEST_F(BgpMembershipTest, MultiplePeersWalkRibIn) {
    static const int kRouteCount = 8;

    // Register all peers.
    Register(peers_[0], blue_tbl_);
    Register(peers_[1], blue_tbl_);
    Register(peers_[2], blue_tbl_);
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_TRUE(IsWalkerQueueEmpty());
    TASK_UTIL_EXPECT_EQ(3, mgr_->GetMembershipCount());
    uint64_t blue_walk_count = blue_tbl_->walk_complete_count();

    // Add paths from all peers.
    for (int idx = 0; idx < kRouteCount; idx++) {
        AddRoute(peers_[0], blue_tbl_, BuildPrefix(idx), "192.168.1.0");
        AddRoute(peers_[1], blue_tbl_, BuildPrefix(idx), "192.168.1.1");
        AddRoute(peers_[2], blue_tbl_, BuildPrefix(idx), "192.168.1.2");
    }
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(kRouteCount, blue_tbl_->Size());

    // Disable walker.
    SetWalkerDisable(true);

    // Request walk for some peers.
    WalkRibIn(peers_[0], blue_tbl_);
    WalkRibIn(peers_[2], blue_tbl_);

    // Postpone walk.
    WalkerPostponeWalk();

    // Enable walker.
    SetWalkerDisable(false);
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(blue_walk_count, blue_tbl_->walk_request_count());
    TASK_UTIL_EXPECT_EQ(blue_walk_count, blue_tbl_->walk_complete_count());
    TASK_UTIL_EXPECT_EQ(2, GetWalkerPeerListSize());

    // Resume walk.
    WalkerResumeWalk();
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_TRUE(IsWalkerQueueEmpty());
    TASK_UTIL_EXPECT_EQ(blue_walk_count + 1, blue_tbl_->walk_request_count());
    TASK_UTIL_EXPECT_EQ(blue_walk_count + 1, blue_tbl_->walk_complete_count());
    TASK_UTIL_EXPECT_EQ(0, GetWalkerPeerListSize());
    TASK_UTIL_EXPECT_EQ(kRouteCount, peers_[0]->path_cb_count());
    TASK_UTIL_EXPECT_EQ(0, peers_[1]->path_cb_count());
    TASK_UTIL_EXPECT_EQ(kRouteCount, peers_[2]->path_cb_count());

    // Delete paths from all peers.
    for (int idx = 0; idx < kRouteCount; idx++) {
        DeleteRoute(peers_[0], blue_tbl_, BuildPrefix(idx));
        DeleteRoute(peers_[1], blue_tbl_, BuildPrefix(idx));
        DeleteRoute(peers_[2], blue_tbl_, BuildPrefix(idx));
    }
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(0, blue_tbl_->Size());

    // Unregister all peers.
    Unregister(peers_[0], blue_tbl_);
    Unregister(peers_[1], blue_tbl_);
    Unregister(peers_[2], blue_tbl_);
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_TRUE(IsWalkerQueueEmpty());
    TASK_UTIL_EXPECT_EQ(0, mgr_->GetMembershipCount());
}

//
// Verify WalkRibIn functionality for multiple peers.
// Walk requests from multiple peers and register from other peer is combined
// into single table walk.
//
TEST_F(BgpMembershipTest, MultiplePeersWalkRibInRegister) {
    static const int kRouteCount = 8;

    // Register some peers.
    Register(peers_[0], blue_tbl_);
    Register(peers_[2], blue_tbl_);
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_TRUE(IsWalkerQueueEmpty());
    TASK_UTIL_EXPECT_EQ(2, mgr_->GetMembershipCount());
    uint64_t blue_walk_count = blue_tbl_->walk_complete_count();

    for (int idx = 0; idx < kRouteCount; idx++) {
        AddRoute(peers_[0], blue_tbl_, BuildPrefix(idx), "192.168.1.0");
        AddRoute(peers_[2], blue_tbl_, BuildPrefix(idx), "192.168.1.2");
    }
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(kRouteCount, blue_tbl_->Size());

    // Disable walker.
    SetWalkerDisable(true);

    // Request walk for registered peers.
    WalkRibIn(peers_[0], blue_tbl_);
    WalkRibIn(peers_[2], blue_tbl_);

    // Register another peer.
    Register(peers_[1], blue_tbl_);
    task_util::WaitForIdle();

    // Postpone walk.
    WalkerPostponeWalk();

    // Enable walker.
    SetWalkerDisable(false);
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(blue_walk_count, blue_tbl_->walk_request_count());
    TASK_UTIL_EXPECT_EQ(blue_walk_count, blue_tbl_->walk_complete_count());
    TASK_UTIL_EXPECT_EQ(2, GetWalkerPeerListSize());
    TASK_UTIL_EXPECT_EQ(3, GetWalkerPeerRibListSize());

    // Resume walk.
    WalkerResumeWalk();
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_TRUE(IsWalkerQueueEmpty());
    TASK_UTIL_EXPECT_EQ(blue_walk_count + 1, blue_tbl_->walk_request_count());
    TASK_UTIL_EXPECT_EQ(blue_walk_count + 1, blue_tbl_->walk_complete_count());
    TASK_UTIL_EXPECT_EQ(0, GetWalkerPeerListSize());
    TASK_UTIL_EXPECT_EQ(0, GetWalkerPeerRibListSize());
    TASK_UTIL_EXPECT_EQ(kRouteCount, peers_[0]->path_cb_count());
    TASK_UTIL_EXPECT_EQ(0, peers_[1]->path_cb_count());
    TASK_UTIL_EXPECT_EQ(kRouteCount, peers_[2]->path_cb_count());

    for (int idx = 0; idx < kRouteCount; idx++) {
        DeleteRoute(peers_[0], blue_tbl_, BuildPrefix(idx));
        DeleteRoute(peers_[2], blue_tbl_, BuildPrefix(idx));
    }
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(0, blue_tbl_->Size());

    // Unregister all peers.
    Unregister(peers_[0], blue_tbl_);
    Unregister(peers_[1], blue_tbl_);
    Unregister(peers_[2], blue_tbl_);
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_TRUE(IsWalkerQueueEmpty());
    TASK_UTIL_EXPECT_EQ(0, mgr_->GetMembershipCount());
}

//
// Verify register/unregister for multiple peer in multiple tables.
//
TEST_F(BgpMembershipTest, MultiplePeersMultipleTable) {
    uint64_t blue_walk_count = blue_tbl_->walk_complete_count();
    uint64_t red_walk_count = red_tbl_->walk_complete_count();

    // Disable walker.
    SetWalkerDisable(true);

    // Register all peers.
    Register(peers_[0], blue_tbl_);
    Register(peers_[1], blue_tbl_);
    Register(peers_[2], blue_tbl_);
    Register(peers_[0], red_tbl_);
    Register(peers_[1], red_tbl_);
    Register(peers_[2], red_tbl_);
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(2, GetWalkerQueueSize());
    TASK_UTIL_EXPECT_EQ(blue_walk_count, blue_tbl_->walk_complete_count());
    TASK_UTIL_EXPECT_EQ(red_walk_count, red_tbl_->walk_complete_count());

    // Enable walker.
    SetWalkerDisable(false);
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(0, GetWalkerQueueSize());
    TASK_UTIL_EXPECT_TRUE(mgr_->GetRegistrationInfo(peers_[0], blue_tbl_));
    TASK_UTIL_EXPECT_TRUE(mgr_->GetRegistrationInfo(peers_[1], blue_tbl_));
    TASK_UTIL_EXPECT_TRUE(mgr_->GetRegistrationInfo(peers_[2], blue_tbl_));
    TASK_UTIL_EXPECT_TRUE(mgr_->GetRegistrationInfo(peers_[0], red_tbl_));
    TASK_UTIL_EXPECT_TRUE(mgr_->GetRegistrationInfo(peers_[1], red_tbl_));
    TASK_UTIL_EXPECT_TRUE(mgr_->GetRegistrationInfo(peers_[2], red_tbl_));
    TASK_UTIL_EXPECT_EQ(6, mgr_->GetMembershipCount());
    TASK_UTIL_EXPECT_EQ(blue_walk_count + 1, blue_tbl_->walk_complete_count());
    TASK_UTIL_EXPECT_EQ(red_walk_count + 1, red_tbl_->walk_complete_count());

    // Disable walker.
    SetWalkerDisable(true);

    // Unregister all peers.
    Unregister(peers_[0], blue_tbl_);
    Unregister(peers_[1], blue_tbl_);
    Unregister(peers_[2], blue_tbl_);
    Unregister(peers_[0], red_tbl_);
    Unregister(peers_[1], red_tbl_);
    Unregister(peers_[2], red_tbl_);
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(2, GetWalkerQueueSize());
    TASK_UTIL_EXPECT_EQ(blue_walk_count + 1, blue_tbl_->walk_complete_count());
    TASK_UTIL_EXPECT_EQ(red_walk_count + 1, red_tbl_->walk_complete_count());

    // Enable walker.
    SetWalkerDisable(false);
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(0, GetWalkerQueueSize());
    TASK_UTIL_EXPECT_FALSE(mgr_->GetRegistrationInfo(peers_[0], blue_tbl_));
    TASK_UTIL_EXPECT_FALSE(mgr_->GetRegistrationInfo(peers_[1], blue_tbl_));
    TASK_UTIL_EXPECT_FALSE(mgr_->GetRegistrationInfo(peers_[2], blue_tbl_));
    TASK_UTIL_EXPECT_FALSE(mgr_->GetRegistrationInfo(peers_[0], red_tbl_));
    TASK_UTIL_EXPECT_FALSE(mgr_->GetRegistrationInfo(peers_[1], red_tbl_));
    TASK_UTIL_EXPECT_FALSE(mgr_->GetRegistrationInfo(peers_[2], red_tbl_));
    TASK_UTIL_EXPECT_EQ(0, mgr_->GetMembershipCount());
    TASK_UTIL_EXPECT_EQ(blue_walk_count + 2, blue_tbl_->walk_complete_count());
    TASK_UTIL_EXPECT_EQ(red_walk_count + 2, red_tbl_->walk_complete_count());
}

//
// Duplicate register causes assertion.
// Duplicate register happens after original is fully processed.
//
TEST_F(BgpMembershipTest, DuplicateRegister1DeathTest) {
    uint64_t blue_walk_count = blue_tbl_->walk_complete_count();

    // Register to blue.
    Register(peers_[0], blue_tbl_);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(mgr_->GetRegistrationInfo(peers_[0], blue_tbl_));
    TASK_UTIL_EXPECT_EQ(1, mgr_->GetMembershipCount());
    TASK_UTIL_EXPECT_EQ(blue_walk_count + 1, blue_tbl_->walk_complete_count());

    // Register to blue again.
    // Note that this happens only in the cloned/forked child.
    TASK_UTIL_EXPECT_DEATH(Register(peers_[0], blue_tbl_), ".*");

    // Unregister from blue.
    Unregister(peers_[0], blue_tbl_);
    task_util::WaitForIdle();
}

//
// Duplicate register causes assertion.
// Duplicate register happens before original is fully processed.
//
TEST_F(BgpMembershipTest, DuplicateRegister2DeathTest) {
    uint64_t blue_walk_count = blue_tbl_->walk_complete_count();

    // Disable membership manager.
    SetQueueDisable(true);

    // Register to blue.
    Register(peers_[0], blue_tbl_);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(mgr_->GetRegistrationInfo(peers_[0], blue_tbl_));
    TASK_UTIL_EXPECT_EQ(1, mgr_->GetMembershipCount());
    TASK_UTIL_EXPECT_EQ(blue_walk_count, blue_tbl_->walk_complete_count());

    // Register to blue again.
    // Note that this happens only in the cloned/forked child.
    TASK_UTIL_EXPECT_DEATH(Register(peers_[0], blue_tbl_), ".*");

    // Enable membership manager.
    SetQueueDisable(false);
    task_util::WaitForIdle();

    // Unregister from blue.
    Unregister(peers_[0], blue_tbl_);
    task_util::WaitForIdle();
}

//
// Duplicate register for ribin causes assertion.
// Duplicate register happens after original is fully processed.
//
TEST_F(BgpMembershipTest, DuplicateRegisterRibIn1DeathTest) {
    uint64_t blue_walk_count = blue_tbl_->walk_complete_count();

    // Register to blue.
    Register(peers_[0], blue_tbl_);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(mgr_->GetRegistrationInfo(peers_[0], blue_tbl_));
    TASK_UTIL_EXPECT_EQ(1, mgr_->GetMembershipCount());
    TASK_UTIL_EXPECT_EQ(blue_walk_count + 1, blue_tbl_->walk_complete_count());

    // Register for ribin to blue.
    // Note that this happens only in the cloned/forked child.
    TASK_UTIL_EXPECT_DEATH(RegisterRibIn(peers_[0], blue_tbl_), ".*");

    // Unregister from blue.
    Unregister(peers_[0], blue_tbl_);
    task_util::WaitForIdle();
}

//
// Duplicate register for ribin causes assertion.
// Duplicate register happens before original is fully processed.
//
TEST_F(BgpMembershipTest, DuplicateRegisterRibIn2DeathTest) {
    uint64_t blue_walk_count = blue_tbl_->walk_complete_count();

    // Disable membership manager.
    SetQueueDisable(true);

    // Register to blue.
    Register(peers_[0], blue_tbl_);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(mgr_->GetRegistrationInfo(peers_[0], blue_tbl_));
    TASK_UTIL_EXPECT_EQ(1, mgr_->GetMembershipCount());
    TASK_UTIL_EXPECT_EQ(blue_walk_count, blue_tbl_->walk_complete_count());

    // Register for ribin to blue.
    // Note that this happens only in the cloned/forked child.
    TASK_UTIL_EXPECT_DEATH(RegisterRibIn(peers_[0], blue_tbl_), ".*");

    // Enable membership manager.
    SetQueueDisable(false);
    task_util::WaitForIdle();

    // Unregister from blue.
    Unregister(peers_[0], blue_tbl_);
    task_util::WaitForIdle();
}

//
// Duplicate register for ribin causes assertion.
// Duplicate register happens after original is fully processed.
// First register is also for ribin.
//
TEST_F(BgpMembershipTest, DuplicateRegisterRibIn3DeathTest) {
    uint64_t blue_walk_count = blue_tbl_->walk_complete_count();

    // Register for ribin to blue.
    RegisterRibIn(peers_[0], blue_tbl_);
    TASK_UTIL_EXPECT_TRUE(mgr_->GetRegistrationInfo(peers_[0], blue_tbl_));
    TASK_UTIL_EXPECT_EQ(1, mgr_->GetMembershipCount());

    // Register for ribin to blue again.
    // Note that this happens only in the cloned/forked child.
    TASK_UTIL_EXPECT_DEATH(RegisterRibIn(peers_[0], blue_tbl_), ".*");

    // Unregister from blue.
    UnregisterRibIn(peers_[0], blue_tbl_);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(blue_walk_count + 1, blue_tbl_->walk_complete_count());
}

//
// Duplicate register for ribin causes assertion.
// Duplicate register happens before original is fully processed.
// First register is also for ribin.
//
TEST_F(BgpMembershipTest, DuplicateRegisterRibIn4DeathTest) {
    ConcurrencyScope scope("bgp::StateMachine");
    uint64_t blue_walk_count = blue_tbl_->walk_complete_count();

    // Disable membership manager.
    SetQueueDisable(true);

    // Register for ribin to blue.
    mgr_->RegisterRibIn(peers_[0], blue_tbl_);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(mgr_->GetRegistrationInfo(peers_[0], blue_tbl_));
    TASK_UTIL_EXPECT_EQ(1, mgr_->GetMembershipCount());

    // Register for ribin to blue again.
    // Note that this happens only in the cloned/forked child.
    TASK_UTIL_EXPECT_DEATH(RegisterRibIn(peers_[0], blue_tbl_), ".*");

    // Enable membership manager.
    SetQueueDisable(false);
    task_util::WaitForIdle();

    // Unregister for ribin from blue.
    mgr_->UnregisterRibIn(peers_[0], blue_tbl_);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(blue_walk_count + 1, blue_tbl_->walk_complete_count());
}

//
// Unregister without register causes assertion.
//
TEST_F(BgpMembershipTest, UnregisterWithoutRegisterDeathTest) {
    // Unregister from blue.
    // Note that this happens only in the cloned/forked child.
    TASK_UTIL_EXPECT_DEATH(Unregister(peers_[0], blue_tbl_), ".*");
}

//
// Unregister with pending register causes assertion.
//
TEST_F(BgpMembershipTest, UnregisterWithPendingRegisterDeathTest) {
    uint64_t blue_walk_count = blue_tbl_->walk_complete_count();

    // Disable membership manager.
    SetQueueDisable(true);

    // Register to blue.
    Register(peers_[0], blue_tbl_);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(mgr_->GetRegistrationInfo(peers_[0], blue_tbl_));
    TASK_UTIL_EXPECT_EQ(1, mgr_->GetMembershipCount());
    TASK_UTIL_EXPECT_EQ(blue_walk_count, blue_tbl_->walk_complete_count());

    // Unregister from blue.
    // Note that this happens only in the cloned/forked child.
    TASK_UTIL_EXPECT_DEATH(Unregister(peers_[0], blue_tbl_), ".*");

    // Enable membership manager.
    SetQueueDisable(false);
    task_util::WaitForIdle();

    // Unregister from blue.
    Unregister(peers_[0], blue_tbl_);
    task_util::WaitForIdle();
}

//
// Unregister with pending unregister causes assertion.
//
TEST_F(BgpMembershipTest, UnregisterWithPendingUnregisterDeathTest) {
    uint64_t blue_walk_count = blue_tbl_->walk_complete_count();

    // Register to blue.
    Register(peers_[0], blue_tbl_);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(mgr_->GetRegistrationInfo(peers_[0], blue_tbl_));
    TASK_UTIL_EXPECT_EQ(1, mgr_->GetMembershipCount());
    TASK_UTIL_EXPECT_EQ(blue_walk_count + 1, blue_tbl_->walk_complete_count());

    // Disable membership manager.
    SetQueueDisable(true);

    // Unregister from blue.
    Unregister(peers_[0], blue_tbl_);
    task_util::WaitForIdle();

    // Unregister from blue again.
    // Note that this happens only in the cloned/forked child.
    TASK_UTIL_EXPECT_DEATH(Unregister(peers_[0], blue_tbl_), ".*");

    // Enable membership manager.
    SetQueueDisable(false);
    task_util::WaitForIdle();
}

//
// Unregister with pending walk causes assertion.
//
TEST_F(BgpMembershipTest, UnregisterWithPendingWalkDeathTest) {
    uint64_t blue_walk_count = blue_tbl_->walk_complete_count();

    // Register to blue.
    Register(peers_[0], blue_tbl_);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(mgr_->GetRegistrationInfo(peers_[0], blue_tbl_));
    TASK_UTIL_EXPECT_EQ(1, mgr_->GetMembershipCount());
    TASK_UTIL_EXPECT_EQ(blue_walk_count + 1, blue_tbl_->walk_complete_count());

    // Disable membership manager.
    SetQueueDisable(true);

    // Walk blue.
    WalkRibIn(peers_[0], blue_tbl_);
    task_util::WaitForIdle();

    // Unregister from blue.
    // Note that this happens only in the cloned/forked child.
    TASK_UTIL_EXPECT_DEATH(Unregister(peers_[0], blue_tbl_), ".*");

    // Enable membership manager.
    SetQueueDisable(false);
    task_util::WaitForIdle();

    // Unregister from blue.
    Unregister(peers_[0], blue_tbl_);
    task_util::WaitForIdle();
}

//
// Walk without register causes assertion.
//
TEST_F(BgpMembershipTest, WalkWithoutRegisterDeathTest) {
    // Walk blue.
    // Note that this happens only in the cloned/forked child.
    TASK_UTIL_EXPECT_DEATH(WalkRibIn(peers_[0], blue_tbl_), ".*");
}

//
// Walk with pending register causes assertion.
//
TEST_F(BgpMembershipTest, WalkWithPendingRegisterDeathTest) {
    uint64_t blue_walk_count = blue_tbl_->walk_complete_count();

    // Disable membership manager.
    SetQueueDisable(true);

    // Register to blue.
    Register(peers_[0], blue_tbl_);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(mgr_->GetRegistrationInfo(peers_[0], blue_tbl_));
    TASK_UTIL_EXPECT_EQ(1, mgr_->GetMembershipCount());
    TASK_UTIL_EXPECT_EQ(blue_walk_count, blue_tbl_->walk_complete_count());

    // Walk blue.
    // Note that this happens only in the cloned/forked child.
    TASK_UTIL_EXPECT_DEATH(WalkRibIn(peers_[0], blue_tbl_), ".*");

    // Enable membership manager.
    SetQueueDisable(false);
    task_util::WaitForIdle();

    // Unregister from blue.
    Unregister(peers_[0], blue_tbl_);
    task_util::WaitForIdle();
}

//
// Walk with pending unregister causes assertion.
//
TEST_F(BgpMembershipTest, WalkWithPendingUnregisterDeathTest) {
    uint64_t blue_walk_count = blue_tbl_->walk_complete_count();

    // Register to blue.
    Register(peers_[0], blue_tbl_);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(mgr_->GetRegistrationInfo(peers_[0], blue_tbl_));
    TASK_UTIL_EXPECT_EQ(1, mgr_->GetMembershipCount());
    TASK_UTIL_EXPECT_EQ(blue_walk_count + 1, blue_tbl_->walk_complete_count());

    // Disable membership manager.
    SetQueueDisable(true);

    // Unregister from blue.
    Unregister(peers_[0], blue_tbl_);
    task_util::WaitForIdle();

    // Walk blue.
    // Note that this happens only in the cloned/forked child.
    TASK_UTIL_EXPECT_DEATH(WalkRibIn(peers_[0], blue_tbl_), ".*");

    // Enable membership manager.
    SetQueueDisable(false);
    task_util::WaitForIdle();
}

//
// Walk with pending walk causes assertion.
//
TEST_F(BgpMembershipTest, WalkWithPendingWalkDeathTest) {
    uint64_t blue_walk_count = blue_tbl_->walk_complete_count();

    // Register to blue.
    Register(peers_[0], blue_tbl_);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(mgr_->GetRegistrationInfo(peers_[0], blue_tbl_));
    TASK_UTIL_EXPECT_EQ(1, mgr_->GetMembershipCount());
    TASK_UTIL_EXPECT_EQ(blue_walk_count + 1, blue_tbl_->walk_complete_count());

    // Disable membership manager.
    SetQueueDisable(true);

    // Walk blue.
    WalkRibIn(peers_[0], blue_tbl_);
    task_util::WaitForIdle();

    // Walk blue again.
    // Note that this happens only in the cloned/forked child.
    TASK_UTIL_EXPECT_DEATH(WalkRibIn(peers_[0], blue_tbl_), ".*");

    // Enable membership manager.
    SetQueueDisable(false);
    task_util::WaitForIdle();

    // Unregister from blue.
    Unregister(peers_[0], blue_tbl_);
    task_util::WaitForIdle();
}

static void SetUp() {
    bgp_log_test::init();
    BgpObjectFactory::Register<BgpPeer>(
        boost::factory<BgpTestPeer *>());
    BgpObjectFactory::Register<BgpConfigManager>(
        boost::factory<BgpIfmapConfigManager *>());
    ControlNode::SetDefaultSchedulingPolicy();
}

static void TearDown() {
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    scheduler->Terminate();
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    ::testing::FLAGS_gtest_death_test_style = "threadsafe";
    SetUp();
    int result = RUN_ALL_TESTS();
    TearDown();
    return result;
}
