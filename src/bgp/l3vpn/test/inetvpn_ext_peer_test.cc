/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/bgp_server.h"
#include "bgp/bgp_session_manager.h"

#include <boost/foreach.hpp>
#include <fstream>

#include "base/util.h"
#include "base/task.h"
#include "base/test/task_test_util.h"
#include "control-node/control_node.h"
#include "bgp/bgp_attr.h"
#include "bgp/bgp_config.h"
#include "bgp/bgp_log.h"
#include "bgp/bgp_path.h"
#include "bgp/community.h"
#include "bgp/test/bgp_server_test_util.h"
#include "bgp/inet/inet_table.h"
#include "bgp/l3vpn/inetvpn_table.h"
#include "bgp/routing-instance/routing_instance.h"
#include "bgp/test/bgp_server_test_util.h"
#include "db/db_table_partition.h"
#include "io/test/event_manager_test.h"
#include "testing/gunit.h"

using namespace boost::asio;
using namespace std;

class L3VPNExtPeerTest : public ::testing::Test {
  public:
    void TableListener(DBTablePartBase *root, DBEntryBase *entry) {
        if (root->parent() == vpn_) 
            BGP_DEBUG_UT("VPN table notification");
        if (root->parent() == red_) 
            BGP_DEBUG_UT("RED Inet table notification");
        if (root->parent() == blue_) 
            BGP_DEBUG_UT("BLUE Inet table notification");

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

protected:
    L3VPNExtPeerTest()
            : thread_(&evm_),  server_(&evm_, "Contrail") {
    }

    virtual void SetUp() {
        server_.session_manager()->Initialize(0);
        BGP_DEBUG_UT("Created server at port: " << 
            server_.session_manager()->GetPort());

        thread_.Start();
    }

    virtual void TearDown() {
        server_.session_manager()->Shutdown();
        evm_.Shutdown();
        thread_.Join();
    }

    string FileRead(const string &filename) {
        ifstream file(filename.c_str());
        string content((istreambuf_iterator<char>(file)),
                       istreambuf_iterator<char>());
        return content;
    }


    EventManager evm_;
    ServerThread thread_;
    BgpServerTest server_;

    DBTable *vpn_;
    DBTable *red_;
    DBTable *blue_;
    DBTableBase::ListenerId vpn_l_;
    DBTableBase::ListenerId red_l_;
    DBTableBase::ListenerId blue_l_;
};

namespace {
TEST_F(L3VPNExtPeerTest, Connection) {
    // create a BGP peer
    string content = FileRead("controller/src/bgp/testdata/bgpc_ext_peer.xml");
    EXPECT_TRUE(server_.Configure(content));

    task_util::WaitForIdle();

    DB *db = server_.database();
    vpn_ = static_cast<DBTable *>(db->FindTable("bgp.l3vpn.0"));
    red_ = static_cast<DBTable *>(db->FindTable("red.inet.0"));
    blue_ = static_cast<DBTable *>(db->FindTable("blue.inet.0"));

    vpn_l_ = vpn_->Register(boost::bind(&L3VPNExtPeerTest::TableListener,
                                            this, _1, _2));
    red_l_ = red_->Register(boost::bind(&L3VPNExtPeerTest::TableListener,
                                            this, _1, _2));
    blue_l_ = blue_->Register(boost::bind(&L3VPNExtPeerTest::TableListener,
                                            this, _1, _2));
    ///////////// bgp.l3vpn.0 Table //////////////////
    // Create RouteTarget Attr
    RouteTarget rt(RouteTarget::FromString("target:1:2"));
    ExtCommunitySpec extcommspec;
    extcommspec.communities.push_back(get_value(rt.GetExtCommunity().begin(), 
                                                8));
    BgpAttrNextHop nexthop(0xc0a801fd);
    BgpAttrOrigin origin(BgpAttrOrigin::IGP);
    BgpAttrLocalPref local_pref(100);
    AsPathSpec path_spec;
    AsPathSpec::PathSegment *path_seg = new AsPathSpec::PathSegment;
    path_seg->path_segment_type = AsPathSpec::PathSegment::AS_SEQUENCE;
    path_seg->path_segment.push_back(65534);
    path_spec.path_segments.push_back(path_seg);

    BgpAttrSpec vpn_attrs;
    vpn_attrs.push_back(&extcommspec);
    vpn_attrs.push_back(&local_pref);
    vpn_attrs.push_back(&nexthop);
    vpn_attrs.push_back(&origin);
    vpn_attrs.push_back(&path_spec);

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

    // Enqueue the DELETE
    DBRequest vpnDelReq;
    vpnDelReq.key.reset(new InetVpnTable::RequestKey(iv_prefix, NULL));
    vpnDelReq.oper = DBRequest::DB_ENTRY_DELETE;
    vpn_->Enqueue(&vpnDelReq);


    while(1) sleep(100);

    vpn_->Unregister(vpn_l_);
    // TODO: delete config
}
}

static void SetUp() {
    bgp_log_test::init();
    ControlNode::SetDefaultSchedulingPolicy();
}

static void TearDown() {
    task_util::WaitForIdle();
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
