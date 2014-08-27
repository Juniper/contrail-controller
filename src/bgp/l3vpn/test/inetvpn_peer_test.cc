/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <tbb/mutex.h>

#include <fstream>

#include "base/task.h"
#include "base/test/task_test_util.h"
#include "bgp/bgp_factory.h"
#include "bgp/bgp_peer_membership.h"
#include "bgp/bgp_session_manager.h"
#include "bgp/community.h"
#include "bgp/inet/inet_table.h"
#include "bgp/l3vpn/inetvpn_table.h"
#include "bgp/routing-instance/peer_manager.h"
#include "bgp/routing-instance/routing_instance.h"
#include "bgp/test/bgp_server_test_util.h"
#include "control-node/control_node.h"
#include "db/db_table_partition.h"
#include "io/test/event_manager_test.h"

#include "testing/gunit.h"

using std::ifstream;
using std::istreambuf_iterator;
using std::map;
using std::ostringstream;
using std::string;
using ::testing::TestWithParam;
using ::testing::ValuesIn;
using ::testing::Combine;

#define CHECK_NOTIFICATION(expected, table) \
do { \
    TASK_UTIL_EXPECT_EQ(expected, GetNotificationCount(table)); \
} while (false)

#define CHECK_NOTIFICATION_MSG(expected, table, msg) \
do { \
    TASK_UTIL_EXPECT_EQ_MSG(expected, GetNotificationCount(table), msg); \
} while (false)

template <typename T>
static void Replace(string *str, const char *substr, T content) {
    size_t loc = str->find(substr);
    if (loc != string::npos) {
        ostringstream oss;
        oss << content;
        str->replace(loc, strlen(substr), oss.str());
    }
}

class L3VPNPeerTest : public ::testing::TestWithParam<const char *> {
protected:
    L3VPNPeerTest()
        : thread_(&evm_),
          a_red_(NULL),
          a_blue_(NULL),
          a_vpn_(NULL),
          a_red_inet_(NULL),
          a_blue_inet_(NULL),
          a_vpn_l_(DBTableBase::kInvalidId),
          a_red_inet_l_(DBTableBase::kInvalidId),
          a_blue_inet_l_(DBTableBase::kInvalidId),
          peer_a_(NULL),
          a_peer_red_(NULL),
          a_peer_blue_(NULL),
          b_red_(NULL),
          b_blue_(NULL),
          b_vpn_(NULL),
          b_red_inet_(NULL),
          b_blue_inet_(NULL),
          b_vpn_l_(DBTableBase::kInvalidId),
          b_red_inet_l_(DBTableBase::kInvalidId),
          b_blue_inet_l_(DBTableBase::kInvalidId),
          peer_b_(NULL),
          b_peer_blue_(NULL),
          vpn_notify_count_(0) {
    }

    void TableListener(DBTablePartBase *root, DBEntryBase *entry) {
        if (root->parent() == a_vpn_)
            BGP_DEBUG_UT("Server A VPN table notification");
        if (root->parent() == b_vpn_)
            BGP_DEBUG_UT("Server B VPN table notification");
        if (root->parent() == b_blue_inet_)
            BGP_DEBUG_UT("Server B Blue Inet table notification");
        if (root->parent() == b_red_inet_)
            BGP_DEBUG_UT("Server B Red Inet table notification");
        if (root->parent() == a_blue_inet_)
            BGP_DEBUG_UT("Server A Blue Inet table notification");
        if (root->parent() == a_red_inet_)
            BGP_DEBUG_UT("Server A Red Inet table notification");
        {
            tbb::mutex::scoped_lock lock(notification_count_lock_);
            notification_count_[root->parent()]++;
        }

        Route *rt = static_cast<Route *>(entry);

        if (rt->IsDeleted()) {
            BGP_DEBUG_UT("Route " << rt->ToString() << " Deleted");
            return;
        }

        Route::PathList::const_iterator it = rt->GetPathList().begin();

        // Verify the attribute
        const BgpPath *path = static_cast<const BgpPath *>(it.operator->());
        const BgpAttr* attr = path->GetAttr();
        const IPeer* peer = path->GetPeer();

        BGP_DEBUG_UT("Route " << rt->ToString() << " from path "
            << ((peer) ? peer->ToString():"Nil")
            << (path->IsFeasible() ? " is Feasible " : " is not feasible")
            << " Origin : " << attr->origin()
            << " Local Pref : " << attr->local_pref()
            << " Nexthop : " << attr->nexthop().to_v4().to_string()
            << " Med : " << attr->med()
            << " Atomic Agg : " << attr->atomic_aggregate());
    }

    string FileRead(const string &filename) {
        ifstream file(filename.c_str());
        string content((istreambuf_iterator<char>(file)),
                       istreambuf_iterator<char>());
        return content;
    }

    void ConfigureBgpRouter() {
        SCOPED_TRACE(__FUNCTION__);
        string content = FileRead("controller/src/bgp/testdata/bgpc_a.xml");
        Replace(&content, "%(port_a)d", a_->session_manager()->GetPort());
        Replace(&content, "%(port_b)d", b_->session_manager()->GetPort());
        if (peer_type_ == "IBGP") {
            Replace(&content, "%(as_a)d", 100);
            Replace(&content, "%(as_b)d", 100);
        } else {
            Replace(&content, "%(as_a)d", 100);
            Replace(&content, "%(as_b)d", 101);
        }
        EXPECT_TRUE(a_->Configure(content));
        EXPECT_TRUE(b_->Configure(content));
        WaitForIdle();

        string instance_name(BgpConfigManager::kMasterInstance);
        string a_name = instance_name + ":A";
        string b_name = instance_name + ":B";

        TASK_UTIL_EXPECT_NE(static_cast<BgpPeer *>(NULL),
            a_->FindPeer(BgpConfigManager::kMasterInstance, b_name));
        TASK_UTIL_EXPECT_NE(static_cast<BgpPeer *>(NULL),
            b_->FindPeer(BgpConfigManager::kMasterInstance, a_name));

        peer_b_ = dynamic_cast<BgpPeerTest *>(
                      a_->FindPeer(BgpConfigManager::kMasterInstance, b_name));
        peer_a_ = dynamic_cast<BgpPeerTest *>(
                      b_->FindPeer(BgpConfigManager::kMasterInstance, a_name));
        TASK_UTIL_EXPECT_EQ_MSG(StateMachine::ESTABLISHED, peer_a_->GetState(),
                                "Is peer ready");
        TASK_UTIL_EXPECT_EQ_MSG(StateMachine::ESTABLISHED, peer_b_->GetState(),
                                "Is peer ready");

        a_red_ = a_->routing_instance_mgr()->GetRoutingInstance("red");
        a_blue_ = a_->routing_instance_mgr()->GetRoutingInstance("blue");
        b_blue_ = b_->routing_instance_mgr()->GetRoutingInstance("blue");
        b_red_ = b_->routing_instance_mgr()->GetRoutingInstance("red");

        a_vpn_ =
            static_cast<BgpTable *>(a_->database()->FindTable("bgp.l3vpn.0"));
        b_vpn_ =
            static_cast<BgpTable *>(b_->database()->FindTable("bgp.l3vpn.0"));
        a_blue_inet_ =
            static_cast<BgpTable *>(a_->database()->FindTable("blue.inet.0"));
        ASSERT_TRUE(a_blue_inet_ != NULL);
        b_blue_inet_ =
            static_cast<BgpTable *>(b_->database()->FindTable("blue.inet.0"));
        ASSERT_TRUE(b_blue_inet_ != NULL);
        a_red_inet_ =
            static_cast<BgpTable *>(a_->database()->FindTable("red.inet.0"));
        ASSERT_TRUE(a_red_inet_ != NULL);
        b_red_inet_ =
            static_cast<BgpTable *>(b_->database()->FindTable("red.inet.0"));
        ASSERT_TRUE(b_red_inet_ != NULL);

        SetupRoutingInstancePeers(a_red_, a_blue_, b_blue_);

        ClearCounters();
        RegisterAllTables();
    }

    virtual void SetUp() {
        peer_type_ = GetParam();
        vpn_notify_count_ = 1;
        a_.reset(new BgpServerTest(&evm_, "A"));
        b_.reset(new BgpServerTest(&evm_, "B"));
        a_->session_manager()->Initialize(0);
        b_->session_manager()->Initialize(0);
        thread_.Start();
        func_ = boost::bind(&L3VPNPeerTest::TableListener, this, _1, _2);
        ConfigureBgpRouter();
    }

    void WaitForIdle() {
        task_util::WaitForIdle();
    }

    void RegisterAllTables() {
        a_vpn_l_ = a_vpn_->Register(func_);
        b_vpn_l_ = b_vpn_->Register(func_);
        a_blue_inet_l_ = a_blue_inet_->Register(func_);
        b_blue_inet_l_ = b_blue_inet_->Register(func_);
        a_red_inet_l_ = a_red_inet_->Register(func_);
        b_red_inet_l_ = b_red_inet_->Register(func_);
    }

    void UnRegisterAllTables() {
        a_vpn_->Unregister(a_vpn_l_);
        b_vpn_->Unregister(b_vpn_l_);
        a_blue_inet_->Unregister(a_blue_inet_l_);
        a_red_inet_->Unregister(a_red_inet_l_);
        b_blue_inet_->Unregister(b_blue_inet_l_);
        b_red_inet_->Unregister(b_red_inet_l_);
    }

    virtual void TearDown() {
        UnRegisterAllTables();
        a_->Shutdown();
        b_->Shutdown();
        WaitForIdle();
        evm_.Shutdown();
        WaitForIdle();
        thread_.Join();
    }

    void ClearCounters() {
        tbb::mutex::scoped_lock lock(notification_count_lock_);
        notification_count_[a_vpn_] = 0;
        notification_count_[b_vpn_] = 0;
        notification_count_[a_blue_inet_] = 0;
        notification_count_[a_red_inet_] = 0;
        notification_count_[b_blue_inet_] = 0;
        notification_count_[b_red_inet_] = 0;
    }

    int GetNotificationCount(DBTableBase *table_base) {
        tbb::mutex::scoped_lock lock(notification_count_lock_);
        return notification_count_[table_base];
    }

    void VerifyVpnTable(RoutingInstance *from_instance, string prefix,
        DBTable *dest, bool present = true, BgpPeer *peer = NULL) {
        Ip4Prefix rt_prefix(Ip4Prefix::FromString(prefix));
        WaitForIdle();
        const RouteDistinguisher *rd = from_instance->GetRD();
        InetVpnPrefix vpn(*rd, rt_prefix.ip4_addr(), rt_prefix.prefixlen());
        InetVpnTable::RequestKey key(vpn, NULL);
        BgpRoute *vpn_rt = static_cast<BgpRoute *>(dest->Find(&key));
        if (present) {
            TASK_UTIL_EXPECT_NE(static_cast<BgpRoute *>(NULL), vpn_rt);
        } else {
            TASK_UTIL_EXPECT_EQ(static_cast<BgpRoute *>(NULL), vpn_rt);
        }
        if (peer) {
            BGP_DEBUG_UT("Wait for path from peer");
            TASK_UTIL_EXPECT_NE_MSG(static_cast<BgpPath *>(NULL),
                              vpn_rt->FindPath(BgpPath::BGP_XMPP, peer, 0),
                              "Wait for BGP path from peer");
        }
    }

    void VerifyInetTable(string prefix, DBTable *dest, bool present = true) {
        Ip4Prefix rt_prefix(Ip4Prefix::FromString(prefix));
        WaitForIdle();
        // Verify the route in VPN Table
        InetTable::RequestKey key(rt_prefix, NULL);
        if (present) {
            TASK_UTIL_EXPECT_NE(static_cast<Route *>(NULL),
                                static_cast<Route *>(dest->Find(&key)));
        } else {
            TASK_UTIL_EXPECT_EQ(static_cast<Route *>(NULL),
                                static_cast<Route *>(dest->Find(&key)));
        }
    }

    bool IsReady() const { return true; }

    void SetupRoutingInstancePeers(RoutingInstance *a_red,
                                   RoutingInstance *a_blue,
                                   RoutingInstance *b_blue) {
        RibExportPolicy policy(BgpProto::IBGP, RibExportPolicy::BGP, 1, 0);
        autogen::BgpRouter rtr_config;
        autogen::BgpRouterParams params;
        params.address = "127.0.0.1";

        params.port = 1;
        params.autonomous_system = 65531;
        rtr_config.SetProperty("bgp-router-parameters",
                               static_cast<AutogenProperty *>(&params));

        a_peer_red_config_.reset(new BgpNeighborConfig(a_red->config(),
                                                       "a_red", "a_red_local",
                                                       &rtr_config));
        a_peer_red_ = dynamic_cast<BgpPeerTest *>(
            a_red->peer_manager()->PeerLocate(
                a_.get(), a_peer_red_config_.get()));
        a_peer_red_->IsReady_fnc_ = boost::bind(&L3VPNPeerTest::IsReady, this);
        a_->membership_mgr()->Register(a_peer_red_, a_red_inet_, policy, -1);

        params.port = 2;
        params.autonomous_system = 65532;
        rtr_config.SetProperty("bgp-router-parameters",
                               static_cast<AutogenProperty *>(&params));
        a_peer_blue_config_.reset(new BgpNeighborConfig(a_blue->config(),
                                                        "a_blue",
                                                        "a_blue_local",
                                                        &rtr_config));
        a_peer_blue_ = dynamic_cast<BgpPeerTest *>(
            a_blue->peer_manager()->PeerLocate(
                a_.get(), a_peer_blue_config_.get()));
        a_peer_blue_->IsReady_fnc_ = boost::bind(&L3VPNPeerTest::IsReady, this);
        a_->membership_mgr()->Register(a_peer_blue_, a_blue_inet_, policy, -1);

        params.port = 3;
        params.autonomous_system = 65533;
        rtr_config.SetProperty("bgp-router-parameters",
                               static_cast<AutogenProperty *>(&params));
        b_peer_blue_config_.reset(new BgpNeighborConfig(b_blue->config(),
                                                        "b_blue",
                                                        "b_blue_local",
                                                        &rtr_config));
        b_peer_blue_ = dynamic_cast<BgpPeerTest *>(
            b_blue->peer_manager()->PeerLocate(
                b_.get(), b_peer_blue_config_.get()));
        b_peer_blue_->IsReady_fnc_ = boost::bind(&L3VPNPeerTest::IsReady, this);
        b_->membership_mgr()->Register(b_peer_blue_, b_blue_inet_, policy, -1);

        WaitForIdle();
    }

    void AddRoute(BgpTable *table, string prefix, BgpPeer *peer,
                  BgpAttrPtr rt_attr) {
        Ip4Prefix rt_prefix(Ip4Prefix::FromString(prefix));
        DBRequest addReq;
        addReq.key.reset(new InetTable::RequestKey(rt_prefix, peer));
        addReq.data.reset(new InetTable::RequestData(rt_attr, 0, 16));
        addReq.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
        table->Enqueue(&addReq);
        WaitForIdle();
    }

    void DeleteRoute(BgpTable *table, string prefix, BgpPeer *peer) {
        Ip4Prefix rt_prefix(Ip4Prefix::FromString(prefix));
        DBRequest delReq;
        delReq.key.reset(new InetTable::RequestKey(rt_prefix, peer));
        delReq.oper = DBRequest::DB_ENTRY_DELETE;
        table->Enqueue(&delReq);
        WaitForIdle();
    }

    string peer_type_;
    EventManager evm_;
    ServerThread thread_;

    /* Server A */
    boost::scoped_ptr<BgpServerTest> a_;
    RoutingInstance *a_red_;
    RoutingInstance *a_blue_;
    BgpTable *a_vpn_;
    BgpTable *a_red_inet_;
    BgpTable *a_blue_inet_;
    DBTableBase::ListenerId a_vpn_l_;
    DBTableBase::ListenerId a_red_inet_l_;
    DBTableBase::ListenerId a_blue_inet_l_;
    BgpPeerTest *peer_a_;
    BgpPeerTest *a_peer_red_;
    BgpPeerTest *a_peer_blue_;

    /* Server B */
    boost::scoped_ptr<BgpServerTest> b_;
    RoutingInstance *b_red_;
    RoutingInstance *b_blue_;
    BgpTable *b_vpn_;
    BgpTable *b_red_inet_;
    BgpTable *b_blue_inet_;
    boost::scoped_ptr<BgpNeighborConfig> a_peer_red_config_;
    boost::scoped_ptr<BgpNeighborConfig> a_peer_blue_config_;
    boost::scoped_ptr<BgpNeighborConfig> b_peer_blue_config_;
    DBTableBase::ListenerId b_vpn_l_;
    DBTableBase::ListenerId b_red_inet_l_;
    DBTableBase::ListenerId b_blue_inet_l_;
    BgpPeerTest *peer_b_;
    BgpPeerTest *b_peer_blue_;
    int vpn_notify_count_;

    tbb::mutex notification_count_lock_;
    map<DBTableBase *, int> notification_count_;

    DBTableBase::ChangeCallback func_;
};


TEST_P(L3VPNPeerTest, RouteExchange) {
    SCOPED_TRACE(__FUNCTION__);

    ///////////////////////////////////////////////////////////////////////////
    //  Verify whether route is exchanged between bgp servers                //
    ///////////////////////////////////////////////////////////////////////////

    ///////////////////////////////////////////////////////////////////////////
    //    Add route to VRF table and verify that route is added in VRF table //
    //    other BGP server that matches the export-rt of source VRF table    //
    ///////////////////////////////////////////////////////////////////////////
    BgpAttrSpec attrs;
    BgpAttrPtr rt_attr = a_->attr_db()->Locate(attrs);
    AddRoute(a_red_inet_, "192.168.24.0/24", a_peer_red_, rt_attr);
    CHECK_NOTIFICATION_MSG(1, b_red_inet_,
                           "Route to appear in RED Inet table on Server B");

    CHECK_NOTIFICATION(vpn_notify_count_, a_vpn_);
    CHECK_NOTIFICATION(1, a_red_inet_);
    CHECK_NOTIFICATION(0, a_blue_inet_);
    CHECK_NOTIFICATION(1, b_vpn_);
    CHECK_NOTIFICATION(1, b_red_inet_);
    CHECK_NOTIFICATION(0, b_blue_inet_);
    VerifyVpnTable(a_red_, "192.168.24.0/24", a_vpn_);
    VerifyVpnTable(a_red_, "192.168.24.0/24", b_vpn_, true, peer_a_);
    VerifyInetTable("192.168.24.0/24", a_red_inet_);
    VerifyInetTable("192.168.24.0/24", b_red_inet_);
    ClearCounters();

    rt_attr = b_->attr_db()->Locate(attrs);
    AddRoute(b_blue_inet_, "192.168.25.0/24", b_peer_blue_, rt_attr);
    CHECK_NOTIFICATION_MSG(1, a_blue_inet_,
                           "Route to appear in BLUE Inet table on Server A");
    CHECK_NOTIFICATION(1, a_vpn_);
    CHECK_NOTIFICATION(0, a_red_inet_);
    CHECK_NOTIFICATION(1, a_blue_inet_);
    CHECK_NOTIFICATION(vpn_notify_count_, b_vpn_);
    CHECK_NOTIFICATION(0, b_red_inet_);
    CHECK_NOTIFICATION(1, b_blue_inet_);
    VerifyVpnTable(b_blue_, "192.168.25.0/24", a_vpn_, true, peer_b_);
    VerifyVpnTable(b_blue_, "192.168.25.0/24", b_vpn_);
    VerifyInetTable("192.168.25.0/24", a_blue_inet_);
    VerifyInetTable("192.168.25.0/24", b_blue_inet_);

    ///////////////////////////////////////////////////////////////////////////
    //    Delete route from VRF table and verify that route is removed       //
    ///////////////////////////////////////////////////////////////////////////
    DeleteRoute(a_red_inet_, "192.168.24.0/24", a_peer_red_);
    DeleteRoute(b_blue_inet_, "192.168.25.0/24", b_peer_blue_);

    TASK_UTIL_EXPECT_EQ_MSG(0, a_red_inet_->Size(),
                           "Routes are cleaned up in RED VRF");
    TASK_UTIL_EXPECT_EQ_MSG(0, a_blue_inet_->Size(),
                           "Routes are cleaned up in BLUE VRF");
    TASK_UTIL_EXPECT_EQ_MSG(0, b_red_inet_->Size(),
                           "Routes are cleaned up in BLUE VRF");
    TASK_UTIL_EXPECT_EQ_MSG(0, b_blue_inet_->Size(),
                           "Routes are cleaned up in RED VRF");
    TASK_UTIL_EXPECT_EQ_MSG(0, a_vpn_->Size(),
                           "Routes are cleaned up in VPN");
    TASK_UTIL_EXPECT_EQ_MSG(0, b_vpn_->Size(),
                           "Routes are cleaned up in VPN");
}

TEST_P(L3VPNPeerTest, AsPathLoop) {
    SCOPED_TRACE(__FUNCTION__);

    ///////////////////////////////////////////////////////////////////////////
    //  Verify whether route is rejected on AS Path loop                     //
    //       Verify that path is not replicated to VRF table                 //
    //       Verify that path in bgp.l3vpn.0 is marked as infeasible         //
    ///////////////////////////////////////////////////////////////////////////
    AsPathSpec path_spec;
    AsPathSpec::PathSegment *path_seg = new AsPathSpec::PathSegment;
    path_seg->path_segment_type = AsPathSpec::PathSegment::AS_SEQUENCE;
    path_seg->path_segment.push_back(peer_type_ == "EBGP" ? 101 : 100);
    path_spec.path_segments.push_back(path_seg);
    BgpAttrSpec asloop_attrs;
    asloop_attrs.push_back(&path_spec);

    BgpAttrPtr asloop_attr = a_->attr_db()->Locate(asloop_attrs);
    AddRoute(a_blue_inet_, "192.168.26.0/24", a_peer_blue_, asloop_attr);
    if (peer_type_ == "IBGP") {
        CHECK_NOTIFICATION_MSG(1, b_vpn_,
                               "Route to appear in VPN table on Server B");
    }
    CHECK_NOTIFICATION(1, a_vpn_);
    CHECK_NOTIFICATION(0, a_red_inet_);
    CHECK_NOTIFICATION(1, a_blue_inet_);
    if (peer_type_ == "IBGP")
        CHECK_NOTIFICATION(1, b_vpn_);
    CHECK_NOTIFICATION(0, b_red_inet_);
    CHECK_NOTIFICATION(0, b_blue_inet_);

    // No poison route advertisement as path is infeasible
    VerifyVpnTable(a_blue_, "192.168.26.0/24", a_vpn_);
    if (peer_type_ == "IBGP")
        VerifyVpnTable(a_blue_, "192.168.26.0/24", b_vpn_, true, peer_a_);
    VerifyInetTable("192.168.26.0/24", a_blue_inet_);
    ClearCounters();

    ///////////////////////////////////////////////////////////////////////////
    //  Verify whether route is rejected on AS Path loop                     //
    //  Verify that path already replicated to VRF table is deleted          //
    ///////////////////////////////////////////////////////////////////////////
    BgpAttrSpec attrs;
    BgpAttrPtr rt_attr = a_->attr_db()->Locate(attrs);
    AddRoute(a_red_inet_, "192.168.27.0/24", a_peer_red_, rt_attr);
    CHECK_NOTIFICATION(1, b_red_inet_);
    VerifyVpnTable(a_red_, "192.168.27.0/24", b_vpn_, true, peer_a_);
    ClearCounters();

    AddRoute(a_red_inet_, "192.168.27.0/24", a_peer_red_, asloop_attr);
    CHECK_NOTIFICATION_MSG(1, b_red_inet_,
                            "Route to disappear in RED INET table on Server B");
    CHECK_NOTIFICATION(vpn_notify_count_, a_vpn_);
    CHECK_NOTIFICATION(1, a_red_inet_);
    CHECK_NOTIFICATION(0, a_blue_inet_);
    CHECK_NOTIFICATION(1, b_vpn_);
    CHECK_NOTIFICATION(1, b_red_inet_);
    CHECK_NOTIFICATION(0, b_blue_inet_);

    // No poison route advertisement as path is infeasible
    VerifyVpnTable(a_red_, "192.168.27.0/24", a_vpn_);
    if (peer_type_ == "IBGP")
        VerifyVpnTable(a_red_, "192.168.27.0/24", b_vpn_, true, peer_a_);
    VerifyInetTable("192.168.27.0/24", a_red_inet_);
    VerifyInetTable("192.168.27.0/24", b_red_inet_, false);
    ClearCounters();

    ///////////////////////////////////////////////////////////////////////////
    //  Verify whether route is accepted on AS Path loop corrected attrs     //
    ///////////////////////////////////////////////////////////////////////////
    AddRoute(a_blue_inet_, "192.168.26.0/24", a_peer_blue_, rt_attr);
    CHECK_NOTIFICATION_MSG(1, b_blue_inet_,
                           "Route to appear in BLUE INET table on Server B");
    CHECK_NOTIFICATION(vpn_notify_count_, a_vpn_);
    CHECK_NOTIFICATION(1, a_blue_inet_);
    CHECK_NOTIFICATION(0, a_red_inet_);
    CHECK_NOTIFICATION(1, b_vpn_);
    CHECK_NOTIFICATION(0, b_red_inet_);
    CHECK_NOTIFICATION(1, b_blue_inet_);
    VerifyVpnTable(a_blue_, "192.168.26.0/24", a_vpn_);
    VerifyVpnTable(a_blue_, "192.168.26.0/24", b_vpn_, true, peer_a_);
    VerifyInetTable("192.168.26.0/24", a_blue_inet_);
    VerifyInetTable("192.168.26.0/24", b_blue_inet_);
    ClearCounters();
}

TEST_P(L3VPNPeerTest, Community) {
    SCOPED_TRACE(__FUNCTION__);

    ///////////////////////////////////////////////////////////////////////////
    //  Verify whether route's tagged with NO_ADVERTISE is handled correctly //
    //       Verify that route is sent out for IBGP                          //
    ///////////////////////////////////////////////////////////////////////////
    CommunitySpec noadv_community;
    noadv_community.communities.push_back(0xFFFFFF01);
    BgpAttrSpec noadv_attrs;
    noadv_attrs.push_back(&noadv_community);
    BgpAttrPtr noadv_attr = a_->attr_db()->Locate(noadv_attrs);

    AddRoute(a_blue_inet_, "192.168.26.0/24", a_peer_blue_, noadv_attr);
    if (peer_type_ == "IBGP") {
        CHECK_NOTIFICATION_MSG(1, b_blue_inet_,
                             "Route to appear in BLUE INET table on Server B");
    } else {
        CHECK_NOTIFICATION_MSG(1, a_vpn_,
                               "Route to appear in VPN table on Server A");
    }
    CHECK_NOTIFICATION(1, a_vpn_);
    CHECK_NOTIFICATION(0, a_red_inet_);
    CHECK_NOTIFICATION(1, a_blue_inet_);
    CHECK_NOTIFICATION((peer_type_ == "IBGP" ? 1:0), b_vpn_);
    CHECK_NOTIFICATION(0, b_red_inet_);
    CHECK_NOTIFICATION((peer_type_ == "IBGP" ? 1:0), b_blue_inet_);
    VerifyVpnTable(a_blue_, "192.168.26.0/24", a_vpn_);
    VerifyInetTable("192.168.26.0/24", a_blue_inet_);
    if (peer_type_ == "IBGP") {
        VerifyVpnTable(a_blue_, "192.168.26.0/24", b_vpn_, true, peer_a_);
        VerifyInetTable("192.168.26.0/24", b_blue_inet_);
    } else {
        VerifyVpnTable(a_blue_, "192.168.26.0/24", b_vpn_, false);
        VerifyInetTable("192.168.26.0/24", b_blue_inet_, false);
    }
    ClearCounters();

    ///////////////////////////////////////////////////////////////////////////
    //  Verify whether route's tagged with NO_EXPORT is handled correctly    //
    //       Verify that route is not sent out                               //
    ///////////////////////////////////////////////////////////////////////////
    CommunitySpec noexport_community;
    noexport_community.communities.push_back(0xFFFFFF02);
    BgpAttrSpec noexport_attrs;
    noexport_attrs.push_back(&noexport_community);
    BgpAttrPtr noexport_attr = a_->attr_db()->Locate(noexport_attrs);

    AddRoute(a_blue_inet_, "192.168.27.0/24", a_peer_blue_, noexport_attr);
    CHECK_NOTIFICATION_MSG(1, a_vpn_,
                            "Route to appear in VPN table on Server A");
    CHECK_NOTIFICATION(1, a_vpn_);
    CHECK_NOTIFICATION(0, a_red_inet_);
    CHECK_NOTIFICATION(1, a_blue_inet_);
    CHECK_NOTIFICATION(0, b_vpn_);
    CHECK_NOTIFICATION(0, b_red_inet_);
    CHECK_NOTIFICATION(0, b_blue_inet_);
    VerifyVpnTable(a_blue_, "192.168.27.0/24", a_vpn_);
    VerifyInetTable("192.168.27.0/24", a_blue_inet_);
    ClearCounters();

    ///////////////////////////////////////////////////////////////////////////
    //  Verify that path already replicated to VRF table is deleted when     //
    //  community is updated to NO_EXPORT                                    //
    ///////////////////////////////////////////////////////////////////////////
    BgpAttrSpec attrs;
    BgpAttrPtr rt_attr = a_->attr_db()->Locate(attrs);
    AddRoute(a_red_inet_, "192.168.28.0/24", a_peer_red_, rt_attr);
    CHECK_NOTIFICATION(1, b_red_inet_);
    VerifyVpnTable(a_red_, "192.168.28.0/24", b_vpn_, true, peer_a_);
    ClearCounters();

    AddRoute(a_red_inet_, "192.168.28.0/24", a_peer_red_, noexport_attr);
    CHECK_NOTIFICATION_MSG(1, b_red_inet_,
                           "Route to disappear in RED INET table on Server B");
    CHECK_NOTIFICATION(vpn_notify_count_, a_vpn_);
    CHECK_NOTIFICATION(1, a_red_inet_);
    CHECK_NOTIFICATION(0, a_blue_inet_);
    CHECK_NOTIFICATION(1, b_vpn_);
    CHECK_NOTIFICATION(1, b_red_inet_);
    CHECK_NOTIFICATION(0, b_blue_inet_);
    // No poison route adv.. as path is infeasible
    VerifyVpnTable(a_red_, "192.168.28.0/24", a_vpn_);
    VerifyVpnTable(a_red_, "192.168.28.0/24", b_vpn_, false);
    VerifyInetTable("192.168.28.0/24", a_red_inet_);
    VerifyInetTable("192.168.28.0/24", b_red_inet_, false);
    ClearCounters();

    ///////////////////////////////////////////////////////////////////////////
    //  Verify whether route's tagged with NO_EXPORT is handled correctly    //
    //       Verify that route is not sent out. If already exported, it is   //
    //       withdrawn                                                       //
    ///////////////////////////////////////////////////////////////////////////
    CommunitySpec noexport_subconf_community;
    noexport_subconf_community.communities.push_back(0xFFFFFF03);
    BgpAttrSpec noexport_subconf_attrs;
    noexport_subconf_attrs.push_back(&noexport_subconf_community);
    BgpAttrPtr noexport_subconf_attr =
        a_->attr_db()->Locate(noexport_subconf_attrs);

    AddRoute(a_blue_inet_, "192.168.29.0/24", a_peer_blue_,
        noexport_subconf_attr);
    if (peer_type_ == "IBGP") {
        CHECK_NOTIFICATION_MSG(1, b_blue_inet_,
                              "Route to appear in BLUE INET table on Server B");
    } else {
        CHECK_NOTIFICATION_MSG(1, a_vpn_,
                               "Route to appear in VPN table on Server A");
    }
    CHECK_NOTIFICATION(1, a_vpn_);
    CHECK_NOTIFICATION(0, a_red_inet_);
    CHECK_NOTIFICATION(1, a_blue_inet_);
    CHECK_NOTIFICATION((peer_type_ == "IBGP" ? 1:0), b_vpn_);
    CHECK_NOTIFICATION(0, b_red_inet_);
    CHECK_NOTIFICATION((peer_type_ == "IBGP" ? 1:0), b_blue_inet_);
    VerifyVpnTable(a_blue_, "192.168.29.0/24", a_vpn_);
    VerifyInetTable("192.168.29.0/24", a_blue_inet_);
    if (peer_type_ == "IBGP") {
        VerifyVpnTable(a_blue_, "192.168.29.0/24", b_vpn_, true, peer_a_);
        VerifyInetTable("192.168.29.0/24", b_blue_inet_);
    }
    ClearCounters();
}

TEST_P(L3VPNPeerTest, ExtendedCommunity) {
    SCOPED_TRACE(__FUNCTION__);

    ///////////////////////////////////////////////////////////////////////////
    //  Verify whether route tagged with extended community is exchanged     //
    //    Add a route with RouteTarget and see whether it is imported        //
    //    to corresponding VRF on rxing BGP server                           //
    ///////////////////////////////////////////////////////////////////////////
    RouteTarget rt(RouteTarget::FromString("target:1:2"));
    ExtCommunitySpec spec;
    spec.communities.push_back(get_value(rt.GetExtCommunity().begin(), 8));
    BgpAttrSpec rt_attrs;;
    rt_attrs.push_back(&spec);
    BgpAttrPtr rt_attr = a_->attr_db()->Locate(rt_attrs);

    AddRoute(a_blue_inet_, "192.168.24.0/24", a_peer_blue_, rt_attr);
    CHECK_NOTIFICATION_MSG(1, b_red_inet_,
                           "Route to appear in BLUE INET table on Server B");
    CHECK_NOTIFICATION(vpn_notify_count_, a_vpn_);
    CHECK_NOTIFICATION(1, a_red_inet_);
    CHECK_NOTIFICATION(1, a_blue_inet_);
    CHECK_NOTIFICATION(1, b_vpn_);
    CHECK_NOTIFICATION(1, b_red_inet_);
    CHECK_NOTIFICATION(1, b_blue_inet_);
    VerifyVpnTable(a_blue_, "192.168.24.0/24", a_vpn_);
    VerifyVpnTable(a_blue_, "192.168.24.0/24", b_vpn_, true, peer_a_);
    VerifyInetTable("192.168.24.0/24", a_blue_inet_);
    VerifyInetTable("192.168.24.0/24", b_blue_inet_);
    VerifyInetTable("192.168.24.0/24", a_red_inet_);
    VerifyInetTable("192.168.24.0/24", b_red_inet_);
    ClearCounters();

    ///////////////////////////////////////////////////////////////////////////
    //  Verify whether update of the ExtendedCommunity attribute is exchanged//
    ///////////////////////////////////////////////////////////////////////////
    BgpAttrSpec attrs;
    rt_attr = a_->attr_db()->Locate(attrs);
    AddRoute(a_blue_inet_, "192.168.24.0/24", a_peer_blue_, rt_attr);

    CHECK_NOTIFICATION_MSG(1, b_red_inet_,
                           "Route to disappear in RED INET table on Server B");
    CHECK_NOTIFICATION(vpn_notify_count_, a_vpn_);
    CHECK_NOTIFICATION(1, a_red_inet_);
    CHECK_NOTIFICATION(1, a_blue_inet_);
    CHECK_NOTIFICATION(1, b_vpn_);
    CHECK_NOTIFICATION(1, b_red_inet_);
    CHECK_NOTIFICATION(1, b_blue_inet_);
    VerifyVpnTable(a_blue_, "192.168.24.0/24", a_vpn_);
    VerifyVpnTable(a_blue_, "192.168.24.0/24", b_vpn_, true, peer_a_);
    VerifyInetTable("192.168.24.0/24", a_blue_inet_);
    VerifyInetTable("192.168.24.0/24", b_blue_inet_);
    VerifyInetTable("192.168.24.0/24", a_red_inet_, false);
    VerifyInetTable("192.168.24.0/24", b_red_inet_, false);
    ClearCounters();
}

static void SetUp() {
    ControlNode::SetDefaultSchedulingPolicy();
    BgpServerTest::GlobalSetUp();
    BgpObjectFactory::Register<StateMachine>(
        boost::factory<StateMachineTest *>());

    //
    // This test uses multiple peering sessions between two BgpServers.
    // Hence the name needs to include uuid as well to maintain unique-ness
    // among the peers
    //
    BgpPeerTest::verbose_name(true);
}

static void TearDown() {
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    scheduler->Terminate();
}

class TestEnvironment : public ::testing::Environment {
    virtual ~TestEnvironment() { }
};

INSTANTIATE_TEST_CASE_P(InetVpnPeerTestWithParams, L3VPNPeerTest,
                        ::testing::Values("IBGP", "EBGP"));

int main(int argc, char **argv) {
    bgp_log_test::init();
    ::testing::InitGoogleTest(&argc, argv);
    ::testing::AddGlobalTestEnvironment(new TestEnvironment());
    SetUp();
    int ret = RUN_ALL_TESTS();
    TearDown();
    return ret;
}
