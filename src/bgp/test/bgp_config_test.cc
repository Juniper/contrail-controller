/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/bgp_config.h"

#include <fstream>
#include <boost/algorithm/string/replace.hpp>

#include "base/logging.h"
#include "base/test/task_test_util.h"
#include "bgp/bgp_config.h"
#include "bgp/bgp_config_parser.h"
#include "bgp/bgp_log.h"
#include "bgp/bgp_server.h"
#include "bgp/state_machine.h"
#include "bgp/inet/inet_table.h"
#include "bgp/routing-instance/peer_manager.h"
#include "bgp/routing-instance/routing_instance.h"
#include "control-node/control_node.h"
#include "db/db.h"
#include "db/db_graph.h"
#include "db/test/db_test_util.h"
#include "ifmap/ifmap_link_table.h"
#include "ifmap/ifmap_node.h"
#include "ifmap/ifmap_server_parser.h"
#include "ifmap/test/ifmap_test_util.h"
#include "io/event_manager.h"
#include "schema/vnc_cfg_types.h"
#include "testing/gunit.h"

using namespace std;

static string FileRead(const string &filename) {
    ifstream file(filename.c_str());
    string content((istreambuf_iterator<char>(file)),
                   istreambuf_iterator<char>());
    return content;
}

class BgpConfigTest : public ::testing::Test {
protected:
    BgpConfigTest()
        : server_(&evm_), parser_(&config_db_) {
    }

    virtual void SetUp() {
        IFMapLinkTable_Init(&config_db_, &db_graph_);
        vnc_cfg_Server_ModuleInit(&config_db_, &db_graph_);
        bgp_schema_Server_ModuleInit(&config_db_, &db_graph_);
        server_.config_manager()->Initialize(&config_db_, &db_graph_, "local");
    }

    virtual void TearDown() {
        task_util::WaitForIdle();
        TASK_UTIL_ASSERT_EQ(1, server_.routing_instance_mgr()->count());

        server_.Shutdown();
        task_util::WaitForIdle();
        TASK_UTIL_ASSERT_EQ(0, server_.routing_instance_mgr()->count());
        TASK_UTIL_ASSERT_EQ(static_cast<BgpSessionManager *>(NULL),
                            server_.session_manager());
        db_util::Clear(&config_db_);
    }

    const StateMachine *GetPeerStateMachine(BgpPeer *peer) {
        return peer->state_machine();
    }

    EventManager evm_;
    BgpServer server_;
    DB config_db_;
    DBGraph db_graph_;
    BgpConfigParser parser_;
};

static void PauseDelete(LifetimeActor *actor) {
    TaskScheduler::GetInstance()->Stop();
    actor->PauseDelete();
    TaskScheduler::GetInstance()->Start();
}

static void ResumeDelete(LifetimeActor *actor) {
    TaskScheduler::GetInstance()->Stop();
    actor->ResumeDelete();
    TaskScheduler::GetInstance()->Start();
}

TEST_F(BgpConfigTest, BgpRouterIdentifierChange) {
    string content_a = FileRead("controller/src/bgp/testdata/config_test_25a.xml");
    EXPECT_TRUE(parser_.Parse(content_a));
    task_util::WaitForIdle();

    // Identifier should be address since it's not specified explicitly.
    TASK_UTIL_EXPECT_EQ("127.0.0.1",
        Ip4Address(server_.bgp_identifier()).to_string());

    // Identifier should change to 10.1.1.1.
    string content_b = FileRead("controller/src/bgp/testdata/config_test_25b.xml");
    EXPECT_TRUE(parser_.Parse(content_b));
    TASK_UTIL_EXPECT_EQ("10.1.1.1",
        Ip4Address(server_.bgp_identifier()).to_string());

    // Identifier should change to 20.1.1.1.
    string content_c = FileRead("controller/src/bgp/testdata/config_test_25c.xml");
    EXPECT_TRUE(parser_.Parse(content_c));
    TASK_UTIL_EXPECT_EQ("20.1.1.1",
        Ip4Address(server_.bgp_identifier()).to_string());

    // Identifier should go back to address since it's not specified explicitly.
    content_a = FileRead("controller/src/bgp/testdata/config_test_25a.xml");
    EXPECT_TRUE(parser_.Parse(content_a));
    TASK_UTIL_EXPECT_EQ("127.0.0.1",
        Ip4Address(server_.bgp_identifier()).to_string());

    boost::replace_all(content_a, "<config>", "<delete>");
    boost::replace_all(content_a, "</config>", "</delete>");
    EXPECT_TRUE(parser_.Parse(content_a));
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(0, db_graph_.edge_count());
    TASK_UTIL_EXPECT_EQ(0, db_graph_.vertex_count());
}

TEST_F(BgpConfigTest, BgpRouterAutonomousSystemChange) {
    string content_a = FileRead("controller/src/bgp/testdata/config_test_24a.xml");
    EXPECT_TRUE(parser_.Parse(content_a));
    task_util::WaitForIdle();

    // AS should be kDefaultAutonomousSystem as it's not specified.
    TASK_UTIL_EXPECT_EQ(BgpConfigManager::kDefaultAutonomousSystem,
        server_.autonomous_system());

    // AS should change to 100.
    string content_b = FileRead("controller/src/bgp/testdata/config_test_24b.xml");
    EXPECT_TRUE(parser_.Parse(content_b));
    TASK_UTIL_EXPECT_EQ(100, server_.autonomous_system());

    // AS should change to 101.
    string content_c = FileRead("controller/src/bgp/testdata/config_test_24c.xml");
    EXPECT_TRUE(parser_.Parse(content_c));
    TASK_UTIL_EXPECT_EQ(101, server_.autonomous_system());

    // AS should go back to kDefaultAutonomousSystem since it's not specified.
    content_a = FileRead("controller/src/bgp/testdata/config_test_24a.xml");
    EXPECT_TRUE(parser_.Parse(content_a));
    TASK_UTIL_EXPECT_EQ(BgpConfigManager::kDefaultAutonomousSystem,
        server_.autonomous_system());

    boost::replace_all(content_a, "<config>", "<delete>");
    boost::replace_all(content_a, "</config>", "</delete>");
    EXPECT_TRUE(parser_.Parse(content_a));
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(0, db_graph_.edge_count());
    TASK_UTIL_EXPECT_EQ(0, db_graph_.vertex_count());
}

TEST_F(BgpConfigTest, BgpRouterHoldTimeChange) {
    string content_a = FileRead("controller/src/bgp/testdata/config_test_23a.xml");
    EXPECT_TRUE(parser_.Parse(content_a));
    task_util::WaitForIdle();

    RoutingInstance *rti = server_.routing_instance_mgr()->GetRoutingInstance(
        BgpConfigManager::kMasterInstance);
    TASK_UTIL_ASSERT_TRUE(rti != NULL);
    TASK_UTIL_EXPECT_EQ(1, rti->peer_manager()->size());
    string name = rti->name() + ":" + "remote";

    BgpPeer *peer;
    TASK_UTIL_EXPECT_TRUE(rti->peer_manager()->PeerLookup(name) != NULL);
    peer = rti->peer_manager()->PeerLookup(name);
    const StateMachine *state_machine = GetPeerStateMachine(peer);

    // Hold time should be 90 since it's not specified explicitly.
    TASK_UTIL_EXPECT_EQ(0, server_.hold_time());
    TASK_UTIL_EXPECT_EQ(90, state_machine->GetConfiguredHoldTime());

    // Hold time should change to 9.
    string content_b = FileRead("controller/src/bgp/testdata/config_test_23b.xml");
    EXPECT_TRUE(parser_.Parse(content_b));
    TASK_UTIL_EXPECT_EQ(9, server_.hold_time());
    TASK_UTIL_EXPECT_EQ(9, state_machine->GetConfiguredHoldTime());

    // Hold time should change to 27.
    string content_c = FileRead("controller/src/bgp/testdata/config_test_23c.xml");
    EXPECT_TRUE(parser_.Parse(content_c));
    TASK_UTIL_EXPECT_EQ(27, server_.hold_time());
    TASK_UTIL_EXPECT_EQ(27, state_machine->GetConfiguredHoldTime());

    // Hold time should go back to 90 since it's not specified explicitly.
    content_a = FileRead("controller/src/bgp/testdata/config_test_23a.xml");
    EXPECT_TRUE(parser_.Parse(content_a));
    TASK_UTIL_EXPECT_EQ(0, server_.hold_time());
    TASK_UTIL_EXPECT_EQ(90, state_machine->GetConfiguredHoldTime());

    boost::replace_all(content_a, "<config>", "<delete>");
    boost::replace_all(content_a, "</config>", "</delete>");
    EXPECT_TRUE(parser_.Parse(content_a));
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(0, db_graph_.edge_count());
    TASK_UTIL_EXPECT_EQ(0, db_graph_.vertex_count());
}

TEST_F(BgpConfigTest, MasterNeighbors) {
    string content = FileRead("controller/src/bgp/testdata/config_test_5.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();
    RoutingInstance *rti = server_.routing_instance_mgr()->GetRoutingInstance(
        BgpConfigManager::kMasterInstance);
    TASK_UTIL_ASSERT_TRUE(rti != NULL);
    TASK_UTIL_EXPECT_EQ(3, rti->peer_manager()->size());

    const char config_update[] = "\
<config>\
    <bgp-router name='remote1'>\
        <address>20.1.1.1</address>\
        <autonomous-system>101</autonomous-system>\
    </bgp-router>\
</config>\
";

    EXPECT_TRUE(parser_.Parse(config_update));
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(3, rti->peer_manager()->size());

    const char config_delete[] = "\
<delete>\
    <bgp-router name='remote1'>\
    </bgp-router>\
</delete>\
";

    EXPECT_TRUE(parser_.Parse(config_delete));
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(2, rti->peer_manager()->size());
}

//
// Pause and resume neighbor deletion.  The peer should get destroyed
// after deletion is resumed.
//
TEST_F(BgpConfigTest, DelayDeletedNeighbor) {
    string content_a = FileRead("controller/src/bgp/testdata/config_test_18a.xml");
    string content_b = FileRead("controller/src/bgp/testdata/config_test_18b.xml");
    string content_c = FileRead("controller/src/bgp/testdata/config_test_18c.xml");

    EXPECT_TRUE(parser_.Parse(content_a));
    task_util::WaitForIdle();

    RoutingInstance *rti = server_.routing_instance_mgr()->GetRoutingInstance(
        BgpConfigManager::kMasterInstance);
    TASK_UTIL_ASSERT_TRUE(rti != NULL);
    TASK_UTIL_EXPECT_EQ(1, rti->peer_manager()->size());
    string name = rti->name() + ":" + "remote";

    // Make sure the peer exists.
    TASK_UTIL_EXPECT_TRUE(rti->peer_manager()->PeerLookup(name) != NULL);
    BgpPeer *peer = rti->peer_manager()->PeerLookup(name);
    TASK_UTIL_EXPECT_FALSE(peer->IsDeleted());
    TASK_UTIL_EXPECT_TRUE(peer->config() != NULL);

    // Pause deletion of the peer.
    PauseDelete(peer->deleter());
    task_util::WaitForIdle();

    // Delete the neighbor config - this should trigger peer deletion.
    // The peer can't get destroyed because deletion has been paused.
    EXPECT_TRUE(parser_.Parse(content_b));
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(peer->IsDeleted());
    TASK_UTIL_EXPECT_TRUE(peer->config() == NULL);

    // Resume deletion of the peer.
    ResumeDelete(peer->deleter());
    task_util::WaitForIdle();

    // Make sure that the peer is gone.
    TASK_UTIL_EXPECT_TRUE(rti->peer_manager()->PeerLookup(name) == NULL);

    EXPECT_TRUE(parser_.Parse(content_c));
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(0, db_graph_.edge_count());
    TASK_UTIL_EXPECT_EQ(0, db_graph_.vertex_count());
}

//
// Config for neighbor is re-added before the previous incarnation has been
// destroyed. The peer should get resurrected after the old incarnation has
// been destroyed.
//
TEST_F(BgpConfigTest, CreateDeletedNeighbor) {
    string content_a = FileRead("controller/src/bgp/testdata/config_test_18a.xml");
    string content_b = FileRead("controller/src/bgp/testdata/config_test_18b.xml");
    string content_c = FileRead("controller/src/bgp/testdata/config_test_18c.xml");

    EXPECT_TRUE(parser_.Parse(content_a));
    task_util::WaitForIdle();

    RoutingInstance *rti = server_.routing_instance_mgr()->GetRoutingInstance(
        BgpConfigManager::kMasterInstance);
    TASK_UTIL_ASSERT_TRUE(rti != NULL);
    TASK_UTIL_EXPECT_EQ(1, rti->peer_manager()->size());
    string name = rti->name() + ":" + "remote";

    // Make sure the peer exists.
    BgpPeer *peer;
    TASK_UTIL_EXPECT_TRUE(rti->peer_manager()->PeerLookup(name) != NULL);
    peer = rti->peer_manager()->PeerLookup(name);
    TASK_UTIL_EXPECT_FALSE(peer->IsDeleted());
    TASK_UTIL_EXPECT_TRUE(peer->config() != NULL);

    // Pause deletion of the peer.
    PauseDelete(peer->deleter());
    task_util::WaitForIdle();

    // Delete the neighbor config - this should trigger peer deletion.
    // The peer can't get destroyed because deletion has been paused.
    EXPECT_TRUE(parser_.Parse(content_b));
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(peer->IsDeleted());
    TASK_UTIL_EXPECT_TRUE(peer->config() == NULL);

    // Recreate neighbor config. The old peer should still be around.
    EXPECT_TRUE(parser_.Parse(content_a));
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(peer->IsDeleted());
    TASK_UTIL_EXPECT_TRUE(peer->config() == NULL);

    // Resume deletion of the peer.
    ResumeDelete(peer->deleter());
    task_util::WaitForIdle();

    // Make sure the peer got resurrected.
    TASK_UTIL_EXPECT_TRUE(rti->peer_manager()->PeerLookup(name) != NULL);
    peer = rti->peer_manager()->PeerLookup(name);
    TASK_UTIL_EXPECT_FALSE(peer->IsDeleted());
    TASK_UTIL_EXPECT_TRUE(peer->config() != NULL);

    // Get rid of the peer.
    EXPECT_TRUE(parser_.Parse(content_c));
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(0, db_graph_.edge_count());
    TASK_UTIL_EXPECT_EQ(0, db_graph_.vertex_count());
}

//
// Config for neighbor is re-added and updated before the previous incarnation
// has been destroyed. The peer should get resurrected with the latest config
// after the old incarnation has been destroyed.
//
TEST_F(BgpConfigTest, UpdateDeletedNeighbor) {
    string content_a = FileRead("controller/src/bgp/testdata/config_test_19a.xml");
    string content_b = FileRead("controller/src/bgp/testdata/config_test_19b.xml");
    string content_c = FileRead("controller/src/bgp/testdata/config_test_19c.xml");
    string content_d = FileRead("controller/src/bgp/testdata/config_test_19d.xml");

    EXPECT_TRUE(parser_.Parse(content_a));
    task_util::WaitForIdle();

    RoutingInstance *rti = server_.routing_instance_mgr()->GetRoutingInstance(
        BgpConfigManager::kMasterInstance);
    TASK_UTIL_ASSERT_TRUE(rti != NULL);
    TASK_UTIL_EXPECT_EQ(1, rti->peer_manager()->size());
    string name = rti->name() + ":" + "remote";

    // Make sure the peer exists.
    BgpPeer *peer;
    TASK_UTIL_EXPECT_TRUE(rti->peer_manager()->PeerLookup(name) != NULL);
    peer = rti->peer_manager()->PeerLookup(name);
    TASK_UTIL_EXPECT_FALSE(peer->IsDeleted());
    TASK_UTIL_EXPECT_EQ(101, peer->peer_as());
    TASK_UTIL_EXPECT_TRUE(peer->config() != NULL);

    // Pause deletion of the peer.
    PauseDelete(peer->deleter());
    task_util::WaitForIdle();

    // Delete the neighbor config - this should trigger peer deletion.
    // The peer can't get destroyed because deletion has been paused.
    EXPECT_TRUE(parser_.Parse(content_b));
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(peer->IsDeleted());
    TASK_UTIL_EXPECT_EQ(101, peer->peer_as());
    TASK_UTIL_EXPECT_TRUE(peer->config() == NULL);

    // Recreate neighbor config. The old peer should still be around.
    EXPECT_TRUE(parser_.Parse(content_a));
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(peer->IsDeleted());
    TASK_UTIL_EXPECT_EQ(101, peer->peer_as());
    TASK_UTIL_EXPECT_TRUE(peer->config() == NULL);

    // Update neighbor config. The old peer should still be around.
    EXPECT_TRUE(parser_.Parse(content_c));
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(peer->IsDeleted());
    TASK_UTIL_EXPECT_EQ(101, peer->peer_as());
    TASK_UTIL_EXPECT_TRUE(peer->config() == NULL);

    // Resume deletion of the peer.
    ResumeDelete(peer->deleter());
    task_util::WaitForIdle();

    // Make sure the peer got resurrected with the latest config (AS 202).
    TASK_UTIL_EXPECT_TRUE(rti->peer_manager()->PeerLookup(name) != NULL);
    peer = rti->peer_manager()->PeerLookup(name);
    TASK_UTIL_EXPECT_FALSE(peer->IsDeleted());
    TASK_UTIL_EXPECT_EQ(202, peer->peer_as());
    TASK_UTIL_EXPECT_TRUE(peer->config() != NULL);

    // Get rid of the peer.
    EXPECT_TRUE(parser_.Parse(content_d));
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(0, db_graph_.edge_count());
    TASK_UTIL_EXPECT_EQ(0, db_graph_.vertex_count());
}

//
// Config for neighbor is re-added and deleted before the previous incarnation
// is destroyed. The peer should not get resurrected after the old incarnation
// has been destroyed.
//
TEST_F(BgpConfigTest, DeleteDeletedNeighbor) {
    string content_a = FileRead("controller/src/bgp/testdata/config_test_18a.xml");
    string content_b = FileRead("controller/src/bgp/testdata/config_test_18b.xml");
    string content_c = FileRead("controller/src/bgp/testdata/config_test_18c.xml");

    EXPECT_TRUE(parser_.Parse(content_a));
    task_util::WaitForIdle();

    RoutingInstance *rti = server_.routing_instance_mgr()->GetRoutingInstance(
        BgpConfigManager::kMasterInstance);
    TASK_UTIL_ASSERT_TRUE(rti != NULL);
    TASK_UTIL_EXPECT_EQ(1, rti->peer_manager()->size());
    string name = rti->name() + ":" + "remote";

    // Make sure the peer exists.
    BgpPeer *peer;
    TASK_UTIL_EXPECT_TRUE(rti->peer_manager()->PeerLookup(name) != NULL);
    peer = rti->peer_manager()->PeerLookup(name);
    TASK_UTIL_EXPECT_FALSE(peer->IsDeleted());
    TASK_UTIL_EXPECT_TRUE(peer->config() != NULL);

    // Pause deletion of the peer.
    PauseDelete(peer->deleter());
    task_util::WaitForIdle();

    // Delete the neighbor config - this should trigger peer deletion.
    // The peer can't get destroyed because deletion has been paused.
    EXPECT_TRUE(parser_.Parse(content_b));
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(peer->IsDeleted());
    TASK_UTIL_EXPECT_TRUE(peer->config() == NULL);

    // Recreate neighbor config. The old peer should still be around.
    EXPECT_TRUE(parser_.Parse(content_a));
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(peer->IsDeleted());
    TASK_UTIL_EXPECT_TRUE(peer->config() == NULL);

    // Delete neighbor config. The old peer should still be around.
    EXPECT_TRUE(parser_.Parse(content_b));
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(peer->IsDeleted());
    TASK_UTIL_EXPECT_TRUE(peer->config() == NULL);

    // Resume deletion of the peer.
    ResumeDelete(peer->deleter());
    task_util::WaitForIdle();

    // Make sure that the peer is gone.
    TASK_UTIL_EXPECT_TRUE(rti->peer_manager()->PeerLookup(name) == NULL);

    // Get rid of the reset of the config.
    EXPECT_TRUE(parser_.Parse(content_c));
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(0, db_graph_.edge_count());
    TASK_UTIL_EXPECT_EQ(0, db_graph_.vertex_count());
}

TEST_F(BgpConfigTest, InstanceTargetExport1) {
    string content = FileRead("controller/src/bgp/testdata/config_test_7.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    RoutingInstanceMgr *mgr = server_.routing_instance_mgr();
    RoutingInstance *red = mgr->GetRoutingInstance("red");
    TASK_UTIL_ASSERT_TRUE(red != NULL);
    RouteTarget rtarget;

    const RoutingInstance::RouteTargetList elist = red->GetExportList();
    TASK_UTIL_EXPECT_EQ(2, elist.size());
    rtarget = RouteTarget::FromString("target:100:1");
    TASK_UTIL_EXPECT_TRUE(elist.find(rtarget) != elist.end());
    rtarget = RouteTarget::FromString("target:100:2");
    TASK_UTIL_EXPECT_TRUE(elist.find(rtarget) != elist.end());

    const RoutingInstance::RouteTargetList ilist = red->GetImportList();
    TASK_UTIL_EXPECT_EQ(1, red->GetImportList().size());
    rtarget = RouteTarget::FromString("target:100:1");
    TASK_UTIL_EXPECT_TRUE(ilist.find(rtarget) != ilist.end());

    boost::replace_all(content, "<config>", "<delete>");
    boost::replace_all(content, "</config>", "</delete>");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(1, mgr->count());
    TASK_UTIL_EXPECT_EQ(0, db_graph_.vertex_count());
}

TEST_F(BgpConfigTest, InstanceTargetExport2) {
    string content = FileRead("controller/src/bgp/testdata/config_test_8.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    RoutingInstanceMgr *mgr = server_.routing_instance_mgr();
    RoutingInstance *red = mgr->GetRoutingInstance("red");
    TASK_UTIL_ASSERT_TRUE(red != NULL);
    RouteTarget rtarget;

    const RoutingInstance::RouteTargetList elist = red->GetExportList();
    TASK_UTIL_EXPECT_EQ(3, elist.size());
    rtarget = RouteTarget::FromString("target:100:1");
    TASK_UTIL_EXPECT_TRUE(elist.find(rtarget) != elist.end());
    rtarget = RouteTarget::FromString("target:100:2");
    TASK_UTIL_EXPECT_TRUE(elist.find(rtarget) != elist.end());
    rtarget = RouteTarget::FromString("target:100:3");
    TASK_UTIL_EXPECT_TRUE(elist.find(rtarget) != elist.end());

    const RoutingInstance::RouteTargetList ilist = red->GetImportList();
    TASK_UTIL_EXPECT_EQ(1, red->GetImportList().size());
    rtarget = RouteTarget::FromString("target:100:1");
    TASK_UTIL_EXPECT_TRUE(ilist.find(rtarget) != ilist.end());

    boost::replace_all(content, "<config>", "<delete>");
    boost::replace_all(content, "</config>", "</delete>");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(1, mgr->count());
    TASK_UTIL_EXPECT_EQ(0, db_graph_.vertex_count());
}

TEST_F(BgpConfigTest, InstanceTargetExport3) {
    string content = FileRead("controller/src/bgp/testdata/config_test_32.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    RoutingInstanceMgr *mgr = server_.routing_instance_mgr();
    RoutingInstance *red = mgr->GetRoutingInstance("red");
    TASK_UTIL_ASSERT_TRUE(red != NULL);
    RouteTarget rtarget;

    const RoutingInstance::RouteTargetList elist = red->GetExportList();
    TASK_UTIL_EXPECT_EQ(1, elist.size());
    rtarget = RouteTarget::FromString("target:100:2");
    TASK_UTIL_EXPECT_TRUE(elist.find(rtarget) != elist.end());

    const RoutingInstance::RouteTargetList ilist = red->GetImportList();
    TASK_UTIL_EXPECT_EQ(0, red->GetImportList().size());

    boost::replace_all(content, "<config>", "<delete>");
    boost::replace_all(content, "</config>", "</delete>");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(1, mgr->count());
    TASK_UTIL_EXPECT_EQ(0, db_graph_.vertex_count());
}

TEST_F(BgpConfigTest, InstanceTargetImport1) {
    string content = FileRead("controller/src/bgp/testdata/config_test_9.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    RoutingInstanceMgr *mgr = server_.routing_instance_mgr();
    RoutingInstance *red = mgr->GetRoutingInstance("red");
    TASK_UTIL_ASSERT_TRUE(red != NULL);
    RouteTarget rtarget;

    const RoutingInstance::RouteTargetList elist = red->GetExportList();
    TASK_UTIL_EXPECT_EQ(1, elist.size());
    rtarget = RouteTarget::FromString("target:100:1");
    TASK_UTIL_EXPECT_TRUE(elist.find(rtarget) != elist.end());

    const RoutingInstance::RouteTargetList ilist = red->GetImportList();
    TASK_UTIL_EXPECT_EQ(2, red->GetImportList().size());
    rtarget = RouteTarget::FromString("target:100:1");
    TASK_UTIL_EXPECT_TRUE(ilist.find(rtarget) != ilist.end());
    rtarget = RouteTarget::FromString("target:100:2");
    TASK_UTIL_EXPECT_TRUE(ilist.find(rtarget) != ilist.end());

    boost::replace_all(content, "<config>", "<delete>");
    boost::replace_all(content, "</config>", "</delete>");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(1, mgr->count());
    TASK_UTIL_EXPECT_EQ(0, db_graph_.vertex_count());
}

TEST_F(BgpConfigTest, InstanceTargetImport2) {
    string content = FileRead("controller/src/bgp/testdata/config_test_10.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    RoutingInstanceMgr *mgr = server_.routing_instance_mgr();
    RoutingInstance *red = mgr->GetRoutingInstance("red");
    TASK_UTIL_ASSERT_TRUE(red != NULL);
    RouteTarget rtarget;

    const RoutingInstance::RouteTargetList elist = red->GetExportList();
    TASK_UTIL_EXPECT_EQ(1, elist.size());
    rtarget = RouteTarget::FromString("target:100:1");
    TASK_UTIL_EXPECT_TRUE(elist.find(rtarget) != elist.end());

    const RoutingInstance::RouteTargetList ilist = red->GetImportList();
    TASK_UTIL_EXPECT_EQ(3, red->GetImportList().size());
    rtarget = RouteTarget::FromString("target:100:1");
    TASK_UTIL_EXPECT_TRUE(ilist.find(rtarget) != ilist.end());
    rtarget = RouteTarget::FromString("target:100:2");
    TASK_UTIL_EXPECT_TRUE(ilist.find(rtarget) != ilist.end());
    rtarget = RouteTarget::FromString("target:100:3");
    TASK_UTIL_EXPECT_TRUE(ilist.find(rtarget) != ilist.end());

    boost::replace_all(content, "<config>", "<delete>");
    boost::replace_all(content, "</config>", "</delete>");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(1, mgr->count());
    TASK_UTIL_EXPECT_EQ(0, db_graph_.vertex_count());
}

TEST_F(BgpConfigTest, Instances) {
    string content = FileRead("controller/src/bgp/testdata/config_test_2.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    RoutingInstanceMgr *mgr = server_.routing_instance_mgr();
    TASK_UTIL_EXPECT_EQ(4, mgr->count());

    ifmap_test_util::IFMapMsgLink(&config_db_,
                                  "routing-instance", "blue",
                                  "routing-instance", "red",
                                  "connection");
    task_util::WaitForIdle();
    RoutingInstance *blue = mgr->GetRoutingInstance("blue");
    TASK_UTIL_ASSERT_TRUE(blue != NULL);
    TASK_UTIL_EXPECT_EQ(2, blue->GetImportList().size());

    RoutingInstance *red = mgr->GetRoutingInstance("red");
    TASK_UTIL_ASSERT_TRUE(red != NULL);
    TASK_UTIL_EXPECT_EQ(2, red->GetImportList().size());

    ifmap_test_util::IFMapMsgUnlink(&config_db_,
                                    "routing-instance", "blue",
                                    "routing-instance", "red",
                                    "connection");
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, blue->GetImportList().size());
    TASK_UTIL_EXPECT_EQ(1, red->GetImportList().size());

    ifmap_test_util::IFMapMsgLink(&config_db_,
                                  "routing-instance", "blue",
                                  "routing-instance", "green",
                                  "connection");
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(2, blue->GetImportList().size());

    ifmap_test_util::IFMapMsgUnlink(&config_db_,
                                    "routing-instance", "green",
                                    "route-target", "target:1:3",
                                    "instance-target");
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, blue->GetImportList().size());

    ifmap_test_util::IFMapMsgLink(&config_db_,
                                  "routing-instance", "green",
                                  "route-target", "target:1:4",
                                  "instance-target");

    task_util::WaitForIdle();
    const RoutingInstance::RouteTargetList &rtlist = blue->GetImportList();
    TASK_UTIL_EXPECT_EQ(2, rtlist.size());
    const char *targets[] = {"target:1:1", "target:1:4"};
    int index = 0;
    for (RoutingInstance::RouteTargetList::const_iterator iter = rtlist.begin();
         iter != rtlist.end(); ++iter) {
        TASK_UTIL_EXPECT_EQ(targets[index], iter->ToString());
        index++;
    }

    boost::replace_all(content, "<config>", "<delete>");
    boost::replace_all(content, "</config>", "</delete>");
    EXPECT_TRUE(parser_.Parse(content));

    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(3, mgr->count());

    ifmap_test_util::IFMapMsgUnlink(&config_db_,
                                    "routing-instance", "blue",
                                    "routing-instance", "green",
                                    "connection");
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(2, mgr->count());

    ifmap_test_util::IFMapMsgUnlink(&config_db_,
                                    "routing-instance", "green",
                                    "route-target", "target:1:4",
                                    "instance-target");
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, mgr->count());
    TASK_UTIL_EXPECT_EQ(0, db_graph_.vertex_count());
}

TEST_F(BgpConfigTest, Instances2) {
    string content = FileRead("controller/src/bgp/testdata/config_test_6.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    RoutingInstanceMgr *mgr = server_.routing_instance_mgr();

    RoutingInstance *red = mgr->GetRoutingInstance("red");
    TASK_UTIL_ASSERT_TRUE(red != NULL);
    TASK_UTIL_EXPECT_EQ(2, red->GetExportList().size());
    TASK_UTIL_EXPECT_EQ(2, red->GetImportList().size());

    RoutingInstance *green = mgr->GetRoutingInstance("green");
    TASK_UTIL_ASSERT_TRUE(green != NULL);
    TASK_UTIL_EXPECT_EQ(2, green->GetExportList().size());
    TASK_UTIL_EXPECT_EQ(2, green->GetImportList().size());

    TASK_UTIL_EXPECT_EQ(3, mgr->count());

    ifmap_test_util::IFMapMsgLink(&config_db_,
                                  "routing-instance", "red",
                                  "routing-instance", "green",
                                  "connection");
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(2, red->GetExportList().size());
    TASK_UTIL_EXPECT_EQ(4, red->GetImportList().size());

    TASK_UTIL_EXPECT_EQ(2, green->GetExportList().size());
    TASK_UTIL_EXPECT_EQ(4, green->GetImportList().size());

    ifmap_test_util::IFMapMsgUnlink(&config_db_,
                                    "routing-instance", "red",
                                    "routing-instance", "green",
                                    "connection");
    task_util::WaitForIdle();

    boost::replace_all(content, "<config>", "<delete>");
    boost::replace_all(content, "</config>", "</delete>");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(1, mgr->count());
    TASK_UTIL_EXPECT_EQ(0, db_graph_.vertex_count());
}

TEST_F(BgpConfigTest, Instances3) {
    string content = FileRead("controller/src/bgp/testdata/config_test_11.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    RoutingInstanceMgr *mgr = server_.routing_instance_mgr();

    RoutingInstance *red = mgr->GetRoutingInstance("red");
    TASK_UTIL_ASSERT_TRUE(red != NULL);
    TASK_UTIL_EXPECT_EQ(2, red->GetExportList().size());
    TASK_UTIL_EXPECT_EQ(1, red->GetImportList().size());

    RoutingInstance *green = mgr->GetRoutingInstance("green");
    TASK_UTIL_ASSERT_TRUE(green != NULL);
    TASK_UTIL_EXPECT_EQ(1, green->GetExportList().size());
    TASK_UTIL_EXPECT_EQ(2, green->GetImportList().size());

    TASK_UTIL_EXPECT_EQ(3, mgr->count());

    ifmap_test_util::IFMapMsgLink(&config_db_,
                                  "routing-instance", "red",
                                  "routing-instance", "green",
                                  "connection");
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(2, red->GetExportList().size());
    TASK_UTIL_EXPECT_EQ(2, red->GetImportList().size());

    TASK_UTIL_EXPECT_EQ(1, green->GetExportList().size());
    TASK_UTIL_EXPECT_EQ(4, green->GetImportList().size());

    ifmap_test_util::IFMapMsgUnlink(&config_db_,
                                    "routing-instance", "red",
                                    "routing-instance", "green",
                                    "connection");
    task_util::WaitForIdle();

    boost::replace_all(content, "<config>", "<delete>");
    boost::replace_all(content, "</config>", "</delete>");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(1, mgr->count());
    TASK_UTIL_EXPECT_EQ(0, db_graph_.vertex_count());
}

TEST_F(BgpConfigTest, InstancesDelayDelete) {
    string content = FileRead("controller/src/bgp/testdata/config_test_17.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    RoutingInstanceMgr *mgr = server_.routing_instance_mgr();
    TASK_UTIL_EXPECT_EQ(2, mgr->count());

    RoutingInstance *red = mgr->GetRoutingInstance("red");
    TASK_UTIL_ASSERT_TRUE(red != NULL);

    BgpAttrSpec red_attrs;
    Ip4Prefix red_prefix(Ip4Prefix::FromString("192.168.24.0/24"));
    BgpAttrPtr red_attr = server_.attr_db()->Locate(red_attrs);
    DBRequest dbReq;
    dbReq.key.reset(new InetTable::RequestKey(red_prefix, NULL));
    dbReq.data.reset(new InetTable::RequestData(red_attr, 0, 0));
    dbReq.oper = DBRequest::DB_ENTRY_ADD_CHANGE;

    string tbl_name("red.inet.0");
    BgpTable *table = 
        static_cast<BgpTable *>(server_.database()->FindTable(tbl_name));
    table->Enqueue(&dbReq);

    boost::replace_all(content, "<config>", "<delete>");
    boost::replace_all(content, "</config>", "</delete>");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(2, mgr->count());
    TASK_UTIL_EXPECT_TRUE(red->deleted());

    content = FileRead("controller/src/bgp/testdata/config_test_7.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_TRUE(red->deleted());
    TASK_UTIL_EXPECT_EQ(2, mgr->count());

    dbReq.key.reset(new InetTable::RequestKey(red_prefix, NULL));
    dbReq.oper = DBRequest::DB_ENTRY_DELETE;
    table->Enqueue(&dbReq);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(2, mgr->count());

    red = mgr->GetRoutingInstance("red");
    TASK_UTIL_ASSERT_TRUE(red != NULL);
    TASK_UTIL_EXPECT_FALSE(red->deleted());
    RouteTarget rtarget;

    const RoutingInstance::RouteTargetList elist = red->GetExportList();
    TASK_UTIL_EXPECT_EQ(2, elist.size());
    rtarget = RouteTarget::FromString("target:100:1");
    TASK_UTIL_EXPECT_TRUE(elist.find(rtarget) != elist.end());
    rtarget = RouteTarget::FromString("target:100:2");
    TASK_UTIL_EXPECT_TRUE(elist.find(rtarget) != elist.end());

    const RoutingInstance::RouteTargetList ilist = red->GetImportList();
    TASK_UTIL_EXPECT_EQ(1, red->GetImportList().size());
    rtarget = RouteTarget::FromString("target:100:1");
    TASK_UTIL_EXPECT_TRUE(ilist.find(rtarget) != ilist.end());

    boost::replace_all(content, "<config>", "<delete>");
    boost::replace_all(content, "</config>", "</delete>");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, mgr->count());
}

TEST_F(BgpConfigTest, UpdatePendingDelete) {
    string content = FileRead("controller/src/bgp/testdata/config_test_17.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    RoutingInstanceMgr *mgr = server_.routing_instance_mgr();
    TASK_UTIL_EXPECT_EQ(2, mgr->count());

    RoutingInstance *red = mgr->GetRoutingInstance("red");
    TASK_UTIL_ASSERT_TRUE(red != NULL);

    BgpAttrSpec red_attrs;
    Ip4Prefix red_prefix(Ip4Prefix::FromString("192.168.24.0/24"));
    BgpAttrPtr red_attr = server_.attr_db()->Locate(red_attrs);
    DBRequest dbReq;
    dbReq.key.reset(new InetTable::RequestKey(red_prefix, NULL));
    dbReq.data.reset(new InetTable::RequestData(red_attr, 0, 0));
    dbReq.oper = DBRequest::DB_ENTRY_ADD_CHANGE;

    string tbl_name("red.inet.0");
    BgpTable *table = 
        static_cast<BgpTable *>(server_.database()->FindTable(tbl_name));
    table->Enqueue(&dbReq);

    boost::replace_all(content, "<config>", "<delete>");
    boost::replace_all(content, "</config>", "</delete>");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(2, mgr->count());
    TASK_UTIL_EXPECT_TRUE(red->deleted());

    content = FileRead("controller/src/bgp/testdata/config_test_6.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_TRUE(red->deleted());
    TASK_UTIL_EXPECT_EQ(3, mgr->count());

    ifmap_test_util::IFMapMsgLink(&config_db_,
                                  "routing-instance", "red",
                                  "routing-instance", "green",
                                  "connection");
    task_util::WaitForIdle();

    dbReq.key.reset(new InetTable::RequestKey(red_prefix, NULL));
    dbReq.oper = DBRequest::DB_ENTRY_DELETE;
    table->Enqueue(&dbReq);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(3, mgr->count());

    red = mgr->GetRoutingInstance("red");
    TASK_UTIL_ASSERT_TRUE(red != NULL);
    TASK_UTIL_EXPECT_FALSE(red->deleted());
    RouteTarget rtarget;

    const RoutingInstance::RouteTargetList elist = red->GetExportList();
    TASK_UTIL_EXPECT_EQ(2, elist.size());
    rtarget = RouteTarget::FromString("target:100:11");
    TASK_UTIL_EXPECT_TRUE(elist.find(rtarget) != elist.end());
    rtarget = RouteTarget::FromString("target:100:12");
    TASK_UTIL_EXPECT_TRUE(elist.find(rtarget) != elist.end());

    const RoutingInstance::RouteTargetList ilist = red->GetImportList();
    TASK_UTIL_EXPECT_EQ(4, red->GetImportList().size());
    rtarget = RouteTarget::FromString("target:100:11");
    TASK_UTIL_EXPECT_TRUE(ilist.find(rtarget) != ilist.end());
    rtarget = RouteTarget::FromString("target:100:12");
    TASK_UTIL_EXPECT_TRUE(ilist.find(rtarget) != ilist.end());
    rtarget = RouteTarget::FromString("target:100:21");
    TASK_UTIL_EXPECT_TRUE(ilist.find(rtarget) != ilist.end());
    rtarget = RouteTarget::FromString("target:100:22");
    TASK_UTIL_EXPECT_TRUE(ilist.find(rtarget) != ilist.end());

    ifmap_test_util::IFMapMsgUnlink(&config_db_,
                                    "routing-instance", "red",
                                    "routing-instance", "green",
                                    "connection");
    task_util::WaitForIdle();

    boost::replace_all(content, "<config>", "<delete>");
    boost::replace_all(content, "</config>", "</delete>");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, mgr->count());
}

TEST_F(BgpConfigTest, DeletePendingDelete) {
    string content = FileRead("controller/src/bgp/testdata/config_test_17.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    RoutingInstanceMgr *mgr = server_.routing_instance_mgr();
    TASK_UTIL_EXPECT_EQ(2, mgr->count());

    RoutingInstance *red = mgr->GetRoutingInstance("red");
    TASK_UTIL_ASSERT_TRUE(red != NULL);

    BgpAttrSpec red_attrs;
    Ip4Prefix red_prefix(Ip4Prefix::FromString("192.168.24.0/24"));
    BgpAttrPtr red_attr = server_.attr_db()->Locate(red_attrs);
    DBRequest dbReq;
    dbReq.key.reset(new InetTable::RequestKey(red_prefix, NULL));
    dbReq.data.reset(new InetTable::RequestData(red_attr, 0, 0));
    dbReq.oper = DBRequest::DB_ENTRY_ADD_CHANGE;

    string tbl_name("red.inet.0");
    BgpTable *table = 
        static_cast<BgpTable *>(server_.database()->FindTable(tbl_name));
    table->Enqueue(&dbReq);

    boost::replace_all(content, "<config>", "<delete>");
    boost::replace_all(content, "</config>", "</delete>");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(2, mgr->count());
    TASK_UTIL_EXPECT_TRUE(red->deleted());

    content = FileRead("controller/src/bgp/testdata/config_test_7.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    boost::replace_all(content, "<config>", "<delete>");
    boost::replace_all(content, "</config>", "</delete>");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    dbReq.key.reset(new InetTable::RequestKey(red_prefix, NULL));
    dbReq.oper = DBRequest::DB_ENTRY_DELETE;
    table->Enqueue(&dbReq);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, mgr->count());
}

//
// The per session address-families config should be used if present.
//
TEST_F(BgpConfigTest, AddressFamilies1) {
    string content = FileRead("controller/src/bgp/testdata/config_test_12.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    RoutingInstance *rti = server_.routing_instance_mgr()->GetRoutingInstance(
        BgpConfigManager::kMasterInstance);
    TASK_UTIL_ASSERT_TRUE(rti != NULL);
    BgpPeer *peer = rti->peer_manager()->PeerFind("10.1.1.1");
    TASK_UTIL_ASSERT_TRUE(peer != NULL);

    TASK_UTIL_EXPECT_EQ(1, peer->families().size());
    TASK_UTIL_EXPECT_TRUE(peer->LookupFamily(Address::INETVPN));

    boost::replace_all(content, "<config>", "<delete>");
    boost::replace_all(content, "</config>", "</delete>");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(0, db_graph_.vertex_count());
}

//
// The address-families config for the local bgp-router should  be used when
// there's no per session address-families configuration.
//
TEST_F(BgpConfigTest, AddressFamilies2) {
    string content = FileRead("controller/src/bgp/testdata/config_test_13.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    RoutingInstance *rti = server_.routing_instance_mgr()->GetRoutingInstance(
        BgpConfigManager::kMasterInstance);
    TASK_UTIL_ASSERT_TRUE(rti != NULL);
    BgpPeer *peer = rti->peer_manager()->PeerFind("10.1.1.1");
    TASK_UTIL_ASSERT_TRUE(peer != NULL);

    TASK_UTIL_EXPECT_EQ(3, peer->families().size());
    TASK_UTIL_EXPECT_TRUE(peer->LookupFamily(Address::INETVPN));
    TASK_UTIL_EXPECT_TRUE(peer->LookupFamily(Address::ERMVPN));
    TASK_UTIL_EXPECT_TRUE(peer->LookupFamily(Address::EVPN));

    boost::replace_all(content, "<config>", "<delete>");
    boost::replace_all(content, "</config>", "</delete>");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(0, db_graph_.vertex_count());
}

//
// The default family should be used if there's no address-families config
// for the session or the local bgp-router.
//
TEST_F(BgpConfigTest, AddressFamilies3) {
    string content = FileRead("controller/src/bgp/testdata/config_test_14.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    RoutingInstance *rti = server_.routing_instance_mgr()->GetRoutingInstance(
        BgpConfigManager::kMasterInstance);
    TASK_UTIL_ASSERT_TRUE(rti != NULL);
    BgpPeer *peer = rti->peer_manager()->PeerFind("10.1.1.1");
    TASK_UTIL_ASSERT_TRUE(peer != NULL);

    TASK_UTIL_EXPECT_EQ(2, peer->families().size());
    TASK_UTIL_EXPECT_TRUE(peer->LookupFamily(Address::INET));
    TASK_UTIL_EXPECT_TRUE(peer->LookupFamily(Address::INETVPN));

    boost::replace_all(content, "<config>", "<delete>");
    boost::replace_all(content, "</config>", "</delete>");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(0, db_graph_.vertex_count());
}

int main(int argc, char **argv) {
    bgp_log_test::init();
    ControlNode::SetDefaultSchedulingPolicy();
    ::testing::InitGoogleTest(&argc, argv);
    int error = RUN_ALL_TESTS();
    TaskScheduler::GetInstance()->Terminate();
    return error;
}
