/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/bgp_server.h"
#include "bgp/bgp_session_manager.h"

#include <boost/algorithm/string.hpp>
#include <boost/foreach.hpp>
#include <fstream>

#include "base/task.h"
#include "base/task_annotations.h"
#include "base/util.h"
#include "base/test/task_test_util.h"
#include "bgp/bgp_attr.h"
#include "bgp/bgp_config.h"
#include "bgp/bgp_path.h"
#include "bgp/community.h"
#include "bgp/inet/inet_table.h"
#include "bgp/l3vpn/inetvpn_table.h"
#include "bgp/routing-instance/routing_instance.h"
#include "bgp/routing-instance/rtarget_group.h"
#include "bgp/test/bgp_server_test_util.h"
#include "control-node/control_node.h"
#include "db/db_table_partition.h"
#include "io/test/event_manager_test.h"

#include "testing/gunit.h"

using namespace boost::asio;
using namespace boost;
using namespace std;

class RoutingInstanceModuleTest : public ::testing::Test {
protected:
    RoutingInstanceModuleTest()
        : server_(&evm_),
          red_(NULL),
          blue_(NULL),
          purple_(NULL),
          vpn_(NULL),
          green_(NULL),
          orange_(NULL),
          red_l_(DBTableBase::kInvalidId),
          blue_l_(DBTableBase::kInvalidId),
          purple_l_(DBTableBase::kInvalidId),
          vpn_l_(DBTableBase::kInvalidId),
          green_l_(DBTableBase::kInvalidId),
          orange_l_(DBTableBase::kInvalidId) {
    }

    virtual void SetUp() {
        ConcurrencyScope scope("bgp::Config");
        master_cfg_.reset(BgpTestUtil::CreateBgpInstanceConfig(
            BgpConfigManager::kMasterInstance));
        blue_cfg_.reset(BgpTestUtil::CreateBgpInstanceConfig("blue",
                "target:1.2.3.4:1", "target:1.2.3.4:1"));
        red_cfg_.reset(BgpTestUtil::CreateBgpInstanceConfig("red",
                "target:1:2", "target:1:2"));
        purple_cfg_.reset(BgpTestUtil::CreateBgpInstanceConfig("purple",
                "target:1:2", "target:1:2"));

        TaskScheduler *scheduler = TaskScheduler::GetInstance();
        scheduler->Stop();
        server_.routing_instance_mgr()->CreateRoutingInstance(blue_cfg_.get());
        server_.routing_instance_mgr()->CreateRoutingInstance(red_cfg_.get());
        server_.routing_instance_mgr()->CreateRoutingInstance(
                purple_cfg_.get());
        scheduler->Start();

        blue_ = static_cast<DBTable *>(
            server_.database()->FindTable("blue.inet.0"));
        purple_ = static_cast<DBTable *>(
            server_.database()->FindTable("purple.inet.0"));
        red_ = 
            static_cast<DBTable *>(server_.database()->FindTable("red.inet.0"));

        red_l_ = red_->Register(boost::bind(&RoutingInstanceModuleTest::TableListener,
                                            this, _1, _2));
        blue_l_ 
            = blue_->Register(boost::bind(&RoutingInstanceModuleTest::TableListener,
                                          this, _1, _2));
        purple_l_ 
            = purple_->Register(boost::bind(&RoutingInstanceModuleTest::TableListener,
                                            this, _1, _2));

        vpn_ = green_ = orange_ = NULL;
    }

    virtual void TearDown() {
        orange_->Unregister(orange_l_);
        green_->Unregister(green_l_);
        purple_->Unregister(purple_l_);
        blue_->Unregister(blue_l_);
        red_->Unregister(red_l_);
        vpn_->Unregister(vpn_l_);
        server_.Shutdown();
        task_util::WaitForIdle();
        evm_.Shutdown();
    }

    void TableListener(DBTablePartBase *root, DBEntryBase *entry) {
        if (root->parent() == blue_) BGP_DEBUG_UT("BLUE table notification");
        if (root->parent() == purple_) BGP_DEBUG_UT("PURPLE table notification");
        if (root->parent() == red_) BGP_DEBUG_UT("RED table notification");
        if (root->parent() == green_) BGP_DEBUG_UT("GREEN table notification");
        if (root->parent() == orange_) BGP_DEBUG_UT("ORANGE table notification");
        if (root->parent() == vpn_) BGP_DEBUG_UT("VPN table notification");

        notification_count_[root->parent()]++;

        Route *rt = static_cast<Route *>(entry);
        BGP_DEBUG_UT("Route " << rt->ToString() << " Deleted " 
            << rt->IsDeleted());
    }

    void ClearCounters() {
        if (red_) notification_count_[red_] = 0;
        if (blue_) notification_count_[blue_] = 0;
        if (purple_) notification_count_[purple_] = 0;
        if (vpn_) notification_count_[vpn_] = 0;
        if (green_) notification_count_[green_] = 0;
        if (orange_) notification_count_[orange_] = 0;
    }

    void VerifyVpnTable(RoutingInstance *from_instance, Ip4Prefix prefix) {
        // Verify the route in VPN Table
        const RouteDistinguisher *rd = from_instance->GetRD();
        InetVpnPrefix vpn(*rd, prefix.ip4_addr(), prefix.prefixlen());
        InetVpnTable::RequestKey key(vpn, NULL);
        Route *vpn_rt = static_cast<Route *>(vpn_->Find(&key));
        EXPECT_TRUE(vpn_rt != NULL);

        TASK_UTIL_EXPECT_EQ(1, vpn_rt->GetPathList().size());
        // Walk the Path and verify ..
        Route::PathList::const_iterator it = vpn_rt->GetPathList().begin(); 
        const BgpPath *path = static_cast<const BgpPath *>(it.operator->());

        // Verify that it is Replicated Route
        EXPECT_TRUE(path->IsReplicated());
        // Peer is NULL
        TASK_UTIL_EXPECT_EQ(static_cast<IPeer *>(NULL), path->GetPeer());

        // Verify the attribute
        const BgpAttr* vpn_attr_check = path->GetAttr();
        EXPECT_TRUE(vpn_attr_check->ext_community() != NULL);

        const ExtCommunity *ext_community = vpn_attr_check->ext_community();
        if (ext_community != NULL) {
            std::set<std::string> rt_inst_list;
            std::set<std::string> rt_pathattr_list;
            BOOST_FOREACH(RouteTarget rtarget, from_instance->GetExportList()) {
                rt_inst_list.insert(rtarget.ToString());
            }
            // For each extended community, verify that it matches 
            // RoutingInstance
            BOOST_FOREACH(const ExtCommunity::ExtCommunityValue &community, 
                          ext_community->communities()) {
                RouteTarget rt(community);
                rt_pathattr_list.insert(rt.ToString());
            }
            // Extended communities should match
            EXPECT_TRUE(rt_inst_list == rt_pathattr_list);
        }
    }

    void VerifyInetTable(DBTable *table, std::string route, 
                         bool missing=false) {
        // Verify the route in Table
        Ip4Prefix prefix(Ip4Prefix::FromString(route));
        InetTable::RequestKey key(prefix, NULL);
        Route *rt = static_cast<Route *>(table->Find(&key));
        if (missing) {
            EXPECT_TRUE(rt == NULL);
            return;
        } else {
            EXPECT_TRUE(rt != NULL);
        }
        // One Path
        TASK_UTIL_EXPECT_EQ(1, rt->GetPathList().size());
        Route::PathList::const_iterator it = rt->GetPathList().begin(); 
        const BgpPath *path = static_cast<const BgpPath *>(it.operator->());
        // Verify that it is Replicated Route
        EXPECT_TRUE(path->IsReplicated());
        // Peer should be NULL
        TASK_UTIL_EXPECT_EQ(static_cast<IPeer *>(NULL), path->GetPeer());
        // Verify the attribute
        const BgpAttr* red_attr_check = path->GetAttr();
        // Route target should not be removed
        if (!rt->IsDeleted()) {
            EXPECT_TRUE(red_attr_check->ext_community() != NULL);
        }
    }

    EventManager evm_;
    BgpServer server_;

    DBTable *red_;
    DBTable *blue_;
    DBTable *purple_;
    DBTable *vpn_;
    DBTable *green_;
    DBTable *orange_;

    scoped_ptr<BgpInstanceConfigTest> master_cfg_;
    scoped_ptr<BgpInstanceConfigTest> purple_cfg_;
    scoped_ptr<BgpInstanceConfigTest> blue_cfg_;
    scoped_ptr<BgpInstanceConfigTest> red_cfg_;
    scoped_ptr<BgpInstanceConfigTest> green_cfg_;
    scoped_ptr<BgpInstanceConfigTest> orange_cfg_;

    DBTableBase::ListenerId red_l_;
    DBTableBase::ListenerId blue_l_;
    DBTableBase::ListenerId purple_l_;
    DBTableBase::ListenerId vpn_l_;
    DBTableBase::ListenerId green_l_;
    DBTableBase::ListenerId orange_l_;

    std::map<DBTableBase *, tbb::atomic<int> > notification_count_;
};

namespace {
TEST_F(RoutingInstanceModuleTest, Connection) {
    RoutingInstance *red = 
        server_.routing_instance_mgr()->GetRoutingInstance("red");
    RoutingInstance *blue = 
        server_.routing_instance_mgr()->GetRoutingInstance("blue");
    RoutingInstance *purple = 
        server_.routing_instance_mgr()->GetRoutingInstance("purple");

    EXPECT_TRUE(purple != NULL);
    EXPECT_TRUE(red != NULL);
    EXPECT_TRUE(blue != NULL);

    InetTable *red_table =
            static_cast<InetTable *>(red->GetTable(Address::INET));
    InetTable *blue_table =
            static_cast<InetTable *>(blue->GetTable(Address::INET));
    InetTable *purple_table =
            static_cast<InetTable *>(purple->GetTable(Address::INET));

    EXPECT_TRUE(red_table != NULL);
    EXPECT_TRUE(blue_table != NULL);
    EXPECT_TRUE(purple_table != NULL);

    TASK_UTIL_EXPECT_EQ(blue_table, blue_);
    TASK_UTIL_EXPECT_EQ(red_table, red_);
    TASK_UTIL_EXPECT_EQ(purple_table, purple_);

    ClearCounters();

    ///////////// RED.inet.0 Table //////////////////
    // Create a route prefix & Attr
    BgpAttrSpec red_attrs;
    Ip4Prefix red_prefix(Ip4Prefix::FromString("192.168.24.0/24"));
    BgpAttrPtr red_attr = server_.attr_db()->Locate(red_attrs);

    // Add Entry to the VRF "red"
    DBRequest redAddReq;
    redAddReq.key.reset(new InetTable::RequestKey(red_prefix, NULL));
    redAddReq.data.reset(new InetTable::RequestData(red_attr, 0, 0));
    redAddReq.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    red_->Enqueue(&redAddReq);
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(1, notification_count_[red_]);
    TASK_UTIL_EXPECT_EQ(1, notification_count_[purple_]);
    TASK_UTIL_EXPECT_EQ(0, notification_count_[blue_]);

    VerifyInetTable(purple_, "192.168.24.0/24");

    ClearCounters();

    ///////////// BLUE.inet.0 Table //////////////////
    // Create a route prefix & Attr
    BgpAttrSpec blue_attrs;
    Ip4Prefix blue_prefix(Ip4Prefix::FromString("192.168.23.0/24"));
    BgpAttrPtr blue_attr = server_.attr_db()->Locate(blue_attrs);

    // Add Entry to the VRF "blue"
    DBRequest blueAddReq;
    blueAddReq.key.reset(new InetTable::RequestKey(blue_prefix, NULL));
    blueAddReq.data.reset(new InetTable::RequestData(blue_attr, 0, 0));
    blueAddReq.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    blue_->Enqueue(&blueAddReq);
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(0, notification_count_[red_]);
    TASK_UTIL_EXPECT_EQ(0, notification_count_[purple_]);
    TASK_UTIL_EXPECT_EQ(1, notification_count_[blue_]);

    BGP_DEBUG_UT("Add bgp.l3vpn.0");
    ConcurrencyScope scope("bgp::Config");
    ///////////// bgp.l3vpn.0 Table //////////////////
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    scheduler->Stop();
    server_.routing_instance_mgr()->CreateRoutingInstance(master_cfg_.get());
    vpn_ = static_cast<DBTable *>(server_.database()->FindTable("bgp.l3vpn.0"));
    EXPECT_TRUE(vpn_ != NULL);

    vpn_l_ = vpn_->Register(boost::bind(&RoutingInstanceModuleTest_Connection_Test::TableListener,
                                        this, _1, _2));
    RoutingInstance *master = 
        server_.routing_instance_mgr()->GetRoutingInstance(
            BgpConfigManager::kMasterInstance);
    EXPECT_TRUE(master != NULL);

    InetVpnTable *vpn_table = 
        static_cast<InetVpnTable *>(master->GetTable(Address::INETVPN));
    EXPECT_TRUE(vpn_table != NULL);
    ClearCounters();
    scheduler->Start();
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(2, notification_count_[vpn_]);
    // Verify the route in VPN Table .. Due to Bulk sync
    VerifyVpnTable(blue, blue_prefix);
    VerifyVpnTable(red, red_prefix);

    ClearCounters();

    // Create RouteTarget Attr
    RouteTarget rt(RouteTarget::FromString("target:1:2"));
    ExtCommunitySpec spec;
    spec.communities.push_back(get_value(rt.GetExtCommunity().begin(), 8));
    BgpAttrSpec vpn_attrs;
    vpn_attrs.push_back(&spec);
    BgpAttrPtr vpn_attr = server_.attr_db()->Locate(vpn_attrs);

    // Attribute is located correct?
    EXPECT_TRUE(vpn_attr.get() != NULL);

    // Create a route prefix & Attr
    InetVpnPrefix iv_prefix(InetVpnPrefix::FromString("2:20:192.168.22.0/24"));

    // Enqueue the update
    DBRequest vpnAddReq;
    vpnAddReq.key.reset(new InetVpnTable::RequestKey(iv_prefix, NULL));
    vpnAddReq.data.reset(new InetVpnTable::RequestData(vpn_attr, 0, 20));
    vpnAddReq.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    // Add Entry to the bgp.l3vpn.0
    vpn_->Enqueue(&vpnAddReq);

    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(1, notification_count_[red_]);
    TASK_UTIL_EXPECT_EQ(1, notification_count_[vpn_]);
    TASK_UTIL_EXPECT_EQ(1, notification_count_[purple_]);
    TASK_UTIL_EXPECT_EQ(0, notification_count_[blue_]);

    VerifyInetTable(red_, "192.168.22.0/24");
    VerifyInetTable(purple_, "192.168.22.0/24");

    ClearCounters();

    ///////////// bgp.l3vpn.0 Table : Multiple route targets //////////////////
    // Create RouteTarget Attr
    RouteTarget rt1(RouteTarget::FromString("target:1:2"));
    RouteTarget rt2(RouteTarget::FromString("target:1.2.3.4:1"));
    ExtCommunitySpec multi_rts;
    multi_rts.communities.push_back(get_value(rt1.GetExtCommunity().begin(),
                                                    8));
    multi_rts.communities.push_back(get_value(rt2.GetExtCommunity().begin(),
                                                    8));
    BgpAttrSpec multi_comm_vpn_attrs;
    multi_comm_vpn_attrs.push_back(&multi_rts);
    BgpAttrPtr multi_comm_vpn_attr 
        = server_.attr_db()->Locate(multi_comm_vpn_attrs);

    // Attribute is located correct?
    EXPECT_TRUE(multi_comm_vpn_attr.get() != NULL);

    // Create a route prefix & Attr
    InetVpnPrefix prefix_1(InetVpnPrefix::FromString("2:20:192.168.21.0/24"));

    // Enqueue the update
    DBRequest vpnAddReq_1;
    vpnAddReq_1.key.reset(new InetVpnTable::RequestKey(prefix_1, NULL));
    vpnAddReq_1.data.reset(new InetVpnTable::RequestData(multi_comm_vpn_attr, 
                                                         0, 20));
    vpnAddReq_1.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    // Add Entry to the bgp.l3vpn.0
    vpn_->Enqueue(&vpnAddReq_1);

    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, notification_count_[red_]);
    TASK_UTIL_EXPECT_EQ(1, notification_count_[purple_]);
    TASK_UTIL_EXPECT_EQ(1, notification_count_[vpn_]);
    TASK_UTIL_EXPECT_EQ(1, notification_count_[blue_]);

    VerifyInetTable(red_, "192.168.21.0/24");
    VerifyInetTable(purple_, "192.168.21.0/24");
    VerifyInetTable(blue_, "192.168.21.0/24");

    scheduler->Stop();
    green_cfg_.reset(BgpTestUtil::CreateBgpInstanceConfig("green",
            "target:1:2", "target:1:2"));
    server_.routing_instance_mgr()->CreateRoutingInstance(green_cfg_.get());
    RoutingInstance *green = 
        server_.routing_instance_mgr()->GetRoutingInstance("green");
    EXPECT_TRUE(green != NULL);

    green_ = 
        static_cast<DBTable *>(server_.database()->FindTable("green.inet.0"));
    EXPECT_TRUE(green_ != NULL);

    green_l_ = green_->Register(boost::bind(&RoutingInstanceModuleTest_Connection_Test::TableListener,
                                        this, _1, _2));
    ClearCounters();
    scheduler->Start();
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(0, notification_count_[red_]);
    TASK_UTIL_EXPECT_EQ(0, notification_count_[purple_]);
    TASK_UTIL_EXPECT_EQ(0, notification_count_[vpn_]);
    TASK_UTIL_EXPECT_EQ(0, notification_count_[blue_]);
    TASK_UTIL_EXPECT_EQ(3, notification_count_[green_]);

    VerifyInetTable(green_, "192.168.21.0/24");
    VerifyInetTable(green_, "192.168.22.0/24");
    VerifyInetTable(green_, "192.168.24.0/24");

    scheduler->Stop();
    orange_cfg_.reset(BgpTestUtil::CreateBgpInstanceConfig("orange",
            "target:1:2 target:1.2.3.4:1", "target:1:2"));
    server_.routing_instance_mgr()->CreateRoutingInstance(orange_cfg_.get());
    RoutingInstance *orange = 
        server_.routing_instance_mgr()->GetRoutingInstance("orange");
    EXPECT_TRUE(orange != NULL);

    orange_ = 
        static_cast<DBTable *>(server_.database()->FindTable("orange.inet.0"));
    EXPECT_TRUE(orange_ != NULL);

    orange_l_ = 
        orange_->Register(boost::bind(&RoutingInstanceModuleTest_Connection_Test::TableListener,
                                        this, _1, _2));
    ClearCounters();
    scheduler->Start();
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(0, notification_count_[red_]);
    TASK_UTIL_EXPECT_EQ(0, notification_count_[purple_]);
    TASK_UTIL_EXPECT_EQ(0, notification_count_[vpn_]);
    TASK_UTIL_EXPECT_EQ(0, notification_count_[blue_]);
    TASK_UTIL_EXPECT_EQ(0, notification_count_[green_]);
    TASK_UTIL_EXPECT_EQ(4, notification_count_[orange_]);

    VerifyInetTable(orange_, "192.168.21.0/24");
    VerifyInetTable(orange_, "192.168.22.0/24");
    VerifyInetTable(orange_, "192.168.23.0/24");
    VerifyInetTable(orange_, "192.168.24.0/24");

    scheduler->Stop();
    BGP_DEBUG_UT("Update the import of orange");
    BgpTestUtil::UpdateBgpInstanceConfig(orange_cfg_.get(),
            "target:1:2", "target:1:2");
    server_.routing_instance_mgr()->UpdateRoutingInstance(orange_cfg_.get());
    ClearCounters();
    scheduler->Start();
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(0, notification_count_[red_]);
    TASK_UTIL_EXPECT_EQ(0, notification_count_[purple_]);
    TASK_UTIL_EXPECT_EQ(0, notification_count_[vpn_]);
    TASK_UTIL_EXPECT_EQ(0, notification_count_[blue_]);
    TASK_UTIL_EXPECT_EQ(0, notification_count_[green_]);
    TASK_UTIL_EXPECT_EQ(1, notification_count_[orange_]);

    VerifyInetTable(orange_, "192.168.23.0/24", true);

    scheduler->Stop();
    BGP_DEBUG_UT("Update the import/export of Green");
    BgpTestUtil::UpdateBgpInstanceConfig(green_cfg_.get(),
            "target:1:2 target:1.2.3.4:1", "target:1:2 target:1.2.3.4:1");
    server_.routing_instance_mgr()->UpdateRoutingInstance(green_cfg_.get());
    ClearCounters();
    scheduler->Start();
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(0, notification_count_[red_]);
    TASK_UTIL_EXPECT_EQ(0, notification_count_[purple_]);
    TASK_UTIL_EXPECT_EQ(0, notification_count_[vpn_]);
    TASK_UTIL_EXPECT_EQ(0, notification_count_[blue_]);
    TASK_UTIL_EXPECT_EQ(1, notification_count_[green_]);
    TASK_UTIL_EXPECT_EQ(0, notification_count_[orange_]);

    VerifyInetTable(green_, "192.168.23.0/24");

    scheduler->Stop();
    BGP_DEBUG_UT("Remove the import/export of Green");
    BgpTestUtil::UpdateBgpInstanceConfig(green_cfg_.get(), "", "");
    server_.routing_instance_mgr()->UpdateRoutingInstance(green_cfg_.get());
    ClearCounters();
    scheduler->Start();
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(0, notification_count_[red_]);
    TASK_UTIL_EXPECT_EQ(0, notification_count_[purple_]);
    TASK_UTIL_EXPECT_EQ(0, notification_count_[vpn_]);
    TASK_UTIL_EXPECT_EQ(0, notification_count_[blue_]);
    TASK_UTIL_EXPECT_EQ(4, notification_count_[green_]);
    TASK_UTIL_EXPECT_EQ(0, notification_count_[orange_]);

    VerifyInetTable(green_, "192.168.21.0/24", true);
    VerifyInetTable(green_, "192.168.22.0/24", true);
    VerifyInetTable(green_, "192.168.23.0/24", true);
    VerifyInetTable(green_, "192.168.24.0/24", true);
    ClearCounters();

    // Test Delete of route to Red
    DBRequest delReq;
    delReq.key.reset(new InetTable::RequestKey(red_prefix, NULL));
    delReq.oper = DBRequest::DB_ENTRY_DELETE;
    red_->Enqueue(&delReq);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, notification_count_[red_]);
    TASK_UTIL_EXPECT_EQ(1, notification_count_[purple_]);
    TASK_UTIL_EXPECT_EQ(1, notification_count_[vpn_]);
    TASK_UTIL_EXPECT_EQ(0, notification_count_[blue_]);
    TASK_UTIL_EXPECT_EQ(0, notification_count_[green_]);
    TASK_UTIL_EXPECT_EQ(1, notification_count_[orange_]);
    VerifyInetTable(orange_, "192.168.24.0/24", true);
    VerifyInetTable(purple_, "192.168.24.0/24", true);
    VerifyInetTable(red_, "192.168.24.0/24", true);
    ClearCounters();

    // Enqueue the Delete to Blue
    delReq.key.reset(new InetTable::RequestKey(blue_prefix, NULL));
    delReq.oper = DBRequest::DB_ENTRY_DELETE;
    blue_->Enqueue(&delReq);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(0, notification_count_[red_]);
    TASK_UTIL_EXPECT_EQ(0, notification_count_[purple_]);
    TASK_UTIL_EXPECT_EQ(1, notification_count_[vpn_]);
    TASK_UTIL_EXPECT_EQ(1, notification_count_[blue_]);
    TASK_UTIL_EXPECT_EQ(0, notification_count_[green_]);
    TASK_UTIL_EXPECT_EQ(0, notification_count_[orange_]);
    ClearCounters();

    // Enqueue the Delete to VPN
    delReq.key.reset(new InetVpnTable::RequestKey(iv_prefix, NULL));
    delReq.oper = DBRequest::DB_ENTRY_DELETE;
    vpn_->Enqueue(&delReq);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, notification_count_[red_]);
    TASK_UTIL_EXPECT_EQ(1, notification_count_[purple_]);
    TASK_UTIL_EXPECT_EQ(1, notification_count_[vpn_]);
    TASK_UTIL_EXPECT_EQ(0, notification_count_[blue_]);
    TASK_UTIL_EXPECT_EQ(0, notification_count_[green_]);
    TASK_UTIL_EXPECT_EQ(1, notification_count_[orange_]);
    VerifyInetTable(orange_, "192.168.22.0/24", true);
    VerifyInetTable(purple_, "192.168.22.0/24", true);
    VerifyInetTable(red_, "192.168.22.0/24", true);
    ClearCounters();

    // Enqueue the Delete to VPN
    delReq.key.reset(new InetVpnTable::RequestKey(prefix_1, NULL));
    delReq.oper = DBRequest::DB_ENTRY_DELETE;
    vpn_->Enqueue(&delReq);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, notification_count_[red_]);
    TASK_UTIL_EXPECT_EQ(1, notification_count_[purple_]);
    TASK_UTIL_EXPECT_EQ(1, notification_count_[vpn_]);
    TASK_UTIL_EXPECT_EQ(1, notification_count_[blue_]);
    TASK_UTIL_EXPECT_EQ(0, notification_count_[green_]);
    TASK_UTIL_EXPECT_EQ(1, notification_count_[orange_]);
    VerifyInetTable(orange_, "192.168.21.0/24", true);
    VerifyInetTable(purple_, "192.168.21.0/24", true);
    VerifyInetTable(red_, "192.168.21.0/24", true);
    VerifyInetTable(blue_, "192.168.21.0/24", true);
    ClearCounters();

    TASK_UTIL_EXPECT_EQ(0, vpn_->Size());
    TASK_UTIL_EXPECT_EQ(0, green_->Size());
    TASK_UTIL_EXPECT_EQ(0, orange_->Size());
    TASK_UTIL_EXPECT_EQ(0, blue_->Size());
    TASK_UTIL_EXPECT_EQ(0, red_->Size());
    TASK_UTIL_EXPECT_EQ(0, purple_->Size());
}
}

class TestEnvironment : public ::testing::Environment {
    virtual ~TestEnvironment() { }
};

static void SetUp() {
    ControlNode::SetDefaultSchedulingPolicy();
}

static void TearDown() {
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    scheduler->Terminate();
}

int main(int argc, char **argv) {
    bgp_log_test::init();
    ::testing::InitGoogleTest(&argc, argv);
    ::testing::AddGlobalTestEnvironment(new TestEnvironment());
    SetUp();
    int result = RUN_ALL_TESTS();
    TearDown();
    return result;
}
