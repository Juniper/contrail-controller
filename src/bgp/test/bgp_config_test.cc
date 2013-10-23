/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/bgp_config.h"

#include <fstream>
#include <boost/algorithm/string/replace.hpp>
#include <boost/bind.hpp>

#include "base/logging.h"
#include "base/task.h"
#include "base/test/task_test_util.h"
#include "bgp/bgp_config.h"
#include "bgp/bgp_config_parser.h"
#include "bgp/bgp_log.h"
#include "bgp/inet/inet_table.h"
#include "bgp/test/bgp_test_util.h"
#include "bgp/routing-instance/peer_manager.h"
#include "bgp/routing-instance/routing_instance.h"
#include "bgp/bgp_server.h"
#include "control-node/control_node.h"
#include "db/db.h"
#include "db/db_graph.h"
#include "db/test/db_test_util.h"
#include "ifmap/ifmap_link_table.h"
#include "ifmap/ifmap_node.h"
#include "ifmap/ifmap_server_table.h"
#include "ifmap/ifmap_server_parser.h"
#include "ifmap/test/ifmap_test_util.h"
#include "io/event_manager.h"
#include "net/mac_address.h"
#include "schema/vnc_cfg_types.h"
#include "testing/gunit.h"

using namespace std;

static string FileRead(const string &filename) {
    ifstream file(filename.c_str());
    string content((istreambuf_iterator<char>(file)),
                   istreambuf_iterator<char>());
    return content;
}

class BgpConfigManagerTest : public ::testing::Test {
protected:
    BgpConfigManagerTest()
        : parser_(&db_) {
    }
    virtual void SetUp() {
        IFMapLinkTable_Init(&db_, &db_graph_);
        vnc_cfg_Server_ModuleInit(&db_, &db_graph_);
        bgp_schema_Server_ModuleInit(&db_, &db_graph_);
        config_manager_.Initialize(&db_, &db_graph_, "local");
    }
    virtual void TearDown() {
        task_util::WaitForIdle();
        config_manager_.Terminate();
        task_util::WaitForIdle();
        db_util::Clear(&db_);
    }

    int GetInstanceCount() {
        int count = 0;

        for (BgpConfigData::BgpInstanceMap::const_iterator iter =
                config_manager_.config().instances().begin();
                iter != config_manager_.config().instances().end(); ++iter) {
            const BgpInstanceConfig *rti = iter->second;
            if (rti->name() != BgpConfigManager::kMasterInstance) {
                TASK_UTIL_EXPECT_EQ(1, rti->import_list().size());
            }
            count++;
        }
        return count;
    }

    const BgpInstanceConfig *FindInstanceConfig(const string instance_name) {
        return config_manager_.config().FindInstance(instance_name);
    }

    DB db_;
    DBGraph db_graph_;
    BgpConfigManager config_manager_;
    BgpConfigParser parser_;
};

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

//
// Config parsing tests
//
// TODO:
// Invalid config files.
// Instance delete order.

TEST_F(BgpConfigManagerTest, MasterNeighbors) {
    string content = FileRead("src/bgp/testdata/config_test_1.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();
    const BgpConfigData::BgpInstanceMap &instances =
        config_manager_.config().instances();
    TASK_UTIL_ASSERT_TRUE(instances.end() !=
                          instances.find(BgpConfigManager::kMasterInstance));
    BgpConfigData::BgpInstanceMap::const_iterator loc =
        instances.find(BgpConfigManager::kMasterInstance);
    const BgpInstanceConfig *cfg = loc->second;
    TASK_UTIL_ASSERT_TRUE(cfg != NULL);
    const BgpInstanceConfig::NeighborMap &neighbors = cfg->neighbors();
    TASK_UTIL_EXPECT_EQ(3, neighbors.size());
    const char *addresses[] = {"10.1.1.1", "10.1.1.2", "10.1.1.2"};
    int index = 0;
    for (BgpInstanceConfig::NeighborMap::const_iterator iter =
                 neighbors.begin(); iter != neighbors.end(); ++iter) {
        TASK_UTIL_EXPECT_EQ(addresses[index],
                            iter->second->peer_config().address);
        index++;
    }
    TASK_UTIL_EXPECT_EQ(3, index);

    const char config_update[] = "\
<config>\
    <bgp-router name='remote1'>\
        <address>10.1.1.1</address>\
        <autonomous-system>102</autonomous-system>\
        <identifier>192.168.1.1</identifier>\
    </bgp-router>\
</config>\
";
    EXPECT_TRUE(parser_.Parse(config_update));
    task_util::WaitForIdle();

    TASK_UTIL_ASSERT_EQ(3, neighbors.size());
    BgpNeighborConfig *peer_config = neighbors.begin()->second;
    TASK_UTIL_EXPECT_EQ(102, peer_config->peer_as());
    
    const char config_delete[] = "\
<delete>\
    <bgp-router name='remote1'>\
        <address>10.1.1.1</address>\
        <session to='local'/>\
        <session to='remote2'/>\
        <session to='remote3'/>\
    </bgp-router>\
</delete>\
";
    EXPECT_TRUE(parser_.Parse(config_delete));
    task_util::WaitForIdle();

    TASK_UTIL_ASSERT_EQ(2, neighbors.size());
    peer_config = neighbors.begin()->second;
    TASK_UTIL_EXPECT_EQ("10.1.1.2", peer_config->peer_config().address);
    
    boost::replace_all(content, "<config>", "<delete>");
    boost::replace_all(content, "</config>", "</delete>");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, config_manager_.config().instances().size());

    TASK_UTIL_EXPECT_EQ(0, db_graph_.edge_count());
    for (DBGraph::edge_iterator edge_iter = db_graph_.edge_list_begin();
         edge_iter != db_graph_.edge_list_end(); ++edge_iter) {
        IFMapNode *left = static_cast<IFMapNode *>(edge_iter->first);
        IFMapNode *right = static_cast<IFMapNode *>(edge_iter->second);
        BGP_DEBUG_UT("[" << left->name() << ", " << right->name() << "]");
    }

    TASK_UTIL_EXPECT_EQ(0, db_graph_.vertex_count());
    for (DBGraph::vertex_iterator v_iter = db_graph_.vertex_list_begin();
         v_iter != db_graph_.vertex_list_end(); ++v_iter) {
        IFMapNode *node = static_cast<IFMapNode *>(v_iter.operator->());
        BGP_DEBUG_UT("vertex: " << node->name());
    }
}

TEST_F(BgpConfigManagerTest, MasterNeighborsAdd) {
    string content_a = FileRead("src/bgp/testdata/config_test_15a.xml");
    EXPECT_TRUE(parser_.Parse(content_a));
    task_util::WaitForIdle();

    const BgpConfigData::BgpInstanceMap &instances =
        config_manager_.config().instances();
    TASK_UTIL_ASSERT_TRUE(instances.end() !=
                          instances.find(BgpConfigManager::kMasterInstance));

    BgpConfigData::BgpInstanceMap::const_iterator loc =
        instances.find(BgpConfigManager::kMasterInstance);
    const BgpInstanceConfig *cfg = loc->second;
    TASK_UTIL_ASSERT_TRUE(cfg != NULL);
    const BgpInstanceConfig::NeighborMap &neighbors = cfg->neighbors();

    TASK_UTIL_EXPECT_EQ(2, neighbors.size());

    string content_b = FileRead("src/bgp/testdata/config_test_15b.xml");
    EXPECT_TRUE(parser_.Parse(content_b));
    task_util::WaitForIdle();

    TASK_UTIL_ASSERT_EQ(6, neighbors.size());

    boost::replace_all(content_b, "<config>", "<delete>");
    boost::replace_all(content_b, "</config>", "</delete>");
    EXPECT_TRUE(parser_.Parse(content_b));
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(1, config_manager_.config().instances().size());

    TASK_UTIL_EXPECT_EQ(0, db_graph_.edge_count());
    TASK_UTIL_EXPECT_EQ(0, db_graph_.vertex_count());
}

TEST_F(BgpConfigManagerTest, MasterNeighborsDelete) {
    string content_a = FileRead("src/bgp/testdata/config_test_16a.xml");
    EXPECT_TRUE(parser_.Parse(content_a));
    task_util::WaitForIdle();

    const BgpConfigData::BgpInstanceMap &instances =
        config_manager_.config().instances();
    TASK_UTIL_ASSERT_TRUE(instances.end() !=
                          instances.find(BgpConfigManager::kMasterInstance));

    BgpConfigData::BgpInstanceMap::const_iterator loc =
        instances.find(BgpConfigManager::kMasterInstance);
    const BgpInstanceConfig *cfg = loc->second;
    TASK_UTIL_ASSERT_TRUE(cfg != NULL);
    const BgpInstanceConfig::NeighborMap &neighbors = cfg->neighbors();

    TASK_UTIL_EXPECT_EQ(6, neighbors.size());

    string content_b = FileRead("src/bgp/testdata/config_test_16b.xml");
    EXPECT_TRUE(parser_.Parse(content_b));
    task_util::WaitForIdle();

    TASK_UTIL_ASSERT_EQ(4, neighbors.size());

    boost::replace_all(content_a, "<config>", "<delete>");
    boost::replace_all(content_a, "</config>", "</delete>");
    EXPECT_TRUE(parser_.Parse(content_a));
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(1, config_manager_.config().instances().size());

    TASK_UTIL_EXPECT_EQ(0, db_graph_.edge_count());
    TASK_UTIL_EXPECT_EQ(0, db_graph_.vertex_count());
}

TEST_F(BgpConfigManagerTest, InstanceTargetExport1) {
    string content = FileRead("src/bgp/testdata/config_test_7.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(2, config_manager_.config().instances().size());
    const BgpInstanceConfig *red = FindInstanceConfig("red");
    TASK_UTIL_ASSERT_TRUE(red != NULL);

    const BgpInstanceConfig::RouteTargetList elist = red->export_list();
    TASK_UTIL_EXPECT_EQ(2, elist.size());
    TASK_UTIL_EXPECT_TRUE(elist.find("target:100:1") != elist.end());
    TASK_UTIL_EXPECT_TRUE(elist.find("target:100:2") != elist.end());

    const BgpInstanceConfig::RouteTargetList ilist = red->import_list();
    TASK_UTIL_EXPECT_EQ(1, ilist.size());
    TASK_UTIL_EXPECT_TRUE(ilist.find("target:100:1") != ilist.end());

    boost::replace_all(content, "<config>", "<delete>");
    boost::replace_all(content, "</config>", "</delete>");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(1, config_manager_.config().instances().size());
    TASK_UTIL_EXPECT_EQ(0, db_graph_.vertex_count());
}

TEST_F(BgpConfigManagerTest, InstanceTargetExport2) {
    string content = FileRead("src/bgp/testdata/config_test_8.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(2, config_manager_.config().instances().size());
    const BgpInstanceConfig *red = FindInstanceConfig("red");
    TASK_UTIL_ASSERT_TRUE(red != NULL);

    const BgpInstanceConfig::RouteTargetList elist = red->export_list();
    TASK_UTIL_EXPECT_EQ(3, elist.size());
    TASK_UTIL_EXPECT_TRUE(elist.find("target:100:1") != elist.end());
    TASK_UTIL_EXPECT_TRUE(elist.find("target:100:2") != elist.end());
    TASK_UTIL_EXPECT_TRUE(elist.find("target:100:3") != elist.end());

    const BgpInstanceConfig::RouteTargetList ilist = red->import_list();
    TASK_UTIL_EXPECT_EQ(1, ilist.size());
    TASK_UTIL_EXPECT_TRUE(ilist.find("target:100:1") != ilist.end());

    boost::replace_all(content, "<config>", "<delete>");
    boost::replace_all(content, "</config>", "</delete>");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(1, config_manager_.config().instances().size());
    TASK_UTIL_EXPECT_EQ(0, db_graph_.vertex_count());
}

TEST_F(BgpConfigManagerTest, InstanceTargetImport1) {
    string content = FileRead("src/bgp/testdata/config_test_9.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(2, config_manager_.config().instances().size());
    const BgpInstanceConfig *red = FindInstanceConfig("red");
    TASK_UTIL_ASSERT_TRUE(red != NULL);

    const BgpInstanceConfig::RouteTargetList elist = red->export_list();
    TASK_UTIL_EXPECT_EQ(1, elist.size());
    TASK_UTIL_EXPECT_TRUE(elist.find("target:100:1") != elist.end());

    const BgpInstanceConfig::RouteTargetList ilist = red->import_list();
    TASK_UTIL_EXPECT_EQ(2, ilist.size());
    TASK_UTIL_EXPECT_TRUE(ilist.find("target:100:1") != ilist.end());
    TASK_UTIL_EXPECT_TRUE(ilist.find("target:100:2") != ilist.end());

    boost::replace_all(content, "<config>", "<delete>");
    boost::replace_all(content, "</config>", "</delete>");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(1, config_manager_.config().instances().size());
    TASK_UTIL_EXPECT_EQ(0, db_graph_.vertex_count());
}

TEST_F(BgpConfigManagerTest, InstanceTargetImport2) {
    string content = FileRead("src/bgp/testdata/config_test_10.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(2, config_manager_.config().instances().size());
    const BgpInstanceConfig *red = FindInstanceConfig("red");
    TASK_UTIL_ASSERT_TRUE(red != NULL);

    const BgpInstanceConfig::RouteTargetList elist = red->export_list();
    TASK_UTIL_EXPECT_EQ(1, elist.size());
    TASK_UTIL_EXPECT_TRUE(elist.find("target:100:1") != elist.end());

    const BgpInstanceConfig::RouteTargetList ilist = red->import_list();
    TASK_UTIL_EXPECT_EQ(3, ilist.size());
    TASK_UTIL_EXPECT_TRUE(ilist.find("target:100:1") != ilist.end());
    TASK_UTIL_EXPECT_TRUE(ilist.find("target:100:2") != ilist.end());
    TASK_UTIL_EXPECT_TRUE(ilist.find("target:100:3") != ilist.end());

    boost::replace_all(content, "<config>", "<delete>");
    boost::replace_all(content, "</config>", "</delete>");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(1, config_manager_.config().instances().size());
    TASK_UTIL_EXPECT_EQ(0, db_graph_.vertex_count());
}

TEST_F(BgpConfigManagerTest, Instances) {
    string content = FileRead("src/bgp/testdata/config_test_2.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(4, GetInstanceCount());

    boost::replace_all(content, "<config>", "<delete>");
    boost::replace_all(content, "</config>", "</delete>");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, config_manager_.config().instances().size());
    TASK_UTIL_EXPECT_EQ(0, db_graph_.vertex_count());
}

TEST_F(BgpConfigManagerTest, InstanceNeighbors) {
    string content = FileRead("src/bgp/testdata/config_test_3.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();
    
    TASK_UTIL_ASSERT_TRUE(config_manager_.config().instances().end() !=
                          config_manager_.config().instances().find("test"));
    BgpConfigData::BgpInstanceMap::const_iterator loc =
        config_manager_.config().instances().find("test");
    const BgpInstanceConfig *rti = loc->second;
    const BgpInstanceConfig::NeighborMap &neighbors = rti->neighbors();
    TASK_UTIL_ASSERT_EQ(1, neighbors.size());
    const BgpNeighborConfig *peer = neighbors.begin()->second;
    TASK_UTIL_EXPECT_EQ(2, peer->peer_as());

    string update = FileRead("src/bgp/testdata/config_test_4.xml");
    EXPECT_TRUE(parser_.Parse(update));
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(2, neighbors.size());
    for (BgpInstanceConfig::NeighborMap::const_iterator iter =
         neighbors.begin(); iter != neighbors.end(); ++iter) {
        TASK_UTIL_EXPECT_EQ(3, iter->second->peer_as());
    }

    const char config_delete[] = "\
<delete>\
    <routing-instance name=\'test\'>\
        <bgp-router name=\'ce-prime\'>\
            <session to=\'local\'/>\
        </bgp-router>\
    </routing-instance>\
</delete>\
";
    EXPECT_TRUE(parser_.Parse(config_delete));
    task_util::WaitForIdle();
    TASK_UTIL_ASSERT_EQ(1, neighbors.size());

    boost::replace_all(content, "<config>", "<delete>");
    boost::replace_all(content, "</config>", "</delete>");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, config_manager_.config().instances().size());
    TASK_UTIL_EXPECT_EQ(0, db_graph_.vertex_count());
}

// Server data structures.
// Create/update/delete.
TEST_F(BgpConfigTest, MasterNeighbors) {
    string content = FileRead("src/bgp/testdata/config_test_5.xml");
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
    string content_a = FileRead("src/bgp/testdata/config_test_18a.xml");
    string content_b = FileRead("src/bgp/testdata/config_test_18b.xml");
    string content_c = FileRead("src/bgp/testdata/config_test_18c.xml");

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
    string content_a = FileRead("src/bgp/testdata/config_test_18a.xml");
    string content_b = FileRead("src/bgp/testdata/config_test_18b.xml");
    string content_c = FileRead("src/bgp/testdata/config_test_18c.xml");

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
    string content_a = FileRead("src/bgp/testdata/config_test_19a.xml");
    string content_b = FileRead("src/bgp/testdata/config_test_19b.xml");
    string content_c = FileRead("src/bgp/testdata/config_test_19c.xml");
    string content_d = FileRead("src/bgp/testdata/config_test_19d.xml");

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
    string content_a = FileRead("src/bgp/testdata/config_test_18a.xml");
    string content_b = FileRead("src/bgp/testdata/config_test_18b.xml");
    string content_c = FileRead("src/bgp/testdata/config_test_18c.xml");

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
    string content = FileRead("src/bgp/testdata/config_test_7.xml");
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
    string content = FileRead("src/bgp/testdata/config_test_8.xml");
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

TEST_F(BgpConfigTest, InstanceTargetImport1) {
    string content = FileRead("src/bgp/testdata/config_test_9.xml");
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
    string content = FileRead("src/bgp/testdata/config_test_10.xml");
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
    string content = FileRead("src/bgp/testdata/config_test_2.xml");
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
    string content = FileRead("src/bgp/testdata/config_test_6.xml");
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
    string content = FileRead("src/bgp/testdata/config_test_11.xml");
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
    string content = FileRead("src/bgp/testdata/config_test_17.xml");
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

    content = FileRead("src/bgp/testdata/config_test_7.xml");
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
    string content = FileRead("src/bgp/testdata/config_test_17.xml");
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

    content = FileRead("src/bgp/testdata/config_test_6.xml");
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
    string content = FileRead("src/bgp/testdata/config_test_17.xml");
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

    content = FileRead("src/bgp/testdata/config_test_7.xml");
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
    string content = FileRead("src/bgp/testdata/config_test_12.xml");
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
    string content = FileRead("src/bgp/testdata/config_test_13.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    RoutingInstance *rti = server_.routing_instance_mgr()->GetRoutingInstance(
        BgpConfigManager::kMasterInstance);
    TASK_UTIL_ASSERT_TRUE(rti != NULL);
    BgpPeer *peer = rti->peer_manager()->PeerFind("10.1.1.1");
    TASK_UTIL_ASSERT_TRUE(peer != NULL);

    TASK_UTIL_EXPECT_EQ(2, peer->families().size());
    TASK_UTIL_EXPECT_TRUE(peer->LookupFamily(Address::INETVPN));
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
    string content = FileRead("src/bgp/testdata/config_test_14.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    RoutingInstance *rti = server_.routing_instance_mgr()->GetRoutingInstance(
        BgpConfigManager::kMasterInstance);
    TASK_UTIL_ASSERT_TRUE(rti != NULL);
    BgpPeer *peer = rti->peer_manager()->PeerFind("10.1.1.1");
    TASK_UTIL_ASSERT_TRUE(peer != NULL);

    TASK_UTIL_EXPECT_EQ(1, peer->families().size());
    TASK_UTIL_EXPECT_TRUE(peer->LookupFamily(Address::INET));

    boost::replace_all(content, "<config>", "<delete>");
    boost::replace_all(content, "</config>", "</delete>");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(0, db_graph_.vertex_count());
}

class IFMapConfigTest : public ::testing::Test {
  protected:
    IFMapConfigTest()
        : bgp_server_(new BgpServer(&evm_)) {
    }
    virtual void SetUp() {
        IFMapLinkTable_Init(&config_db_, &config_graph_);
        vnc_cfg_Server_ModuleInit(&config_db_, &config_graph_);
        bgp_schema_Server_ModuleInit(&config_db_, &config_graph_);
        IFMapServerParser *parser = IFMapServerParser::GetInstance("schema");
        vnc_cfg_ParserInit(parser);
        bgp_schema_ParserInit(parser);
    }

    virtual void TearDown() {
        bgp_server_->Shutdown();
        task_util::WaitForIdle();
        IFMapServerParser *parser = IFMapServerParser::GetInstance("schema");
        parser->MetadataClear("schema");
        db_util::Clear(&config_db_);
    }

    EventManager evm_;
    DB config_db_;
    DBGraph config_graph_;
    boost::scoped_ptr<BgpServer> bgp_server_;
};

TEST_F(IFMapConfigTest, InitialConfig) {
    bgp_server_->config_manager()->Initialize(&config_db_, &config_graph_,
                                              "system0001");
    string content = FileRead("src/bgp/testdata/initial-config.xml");
    IFMapServerParser *parser =
        IFMapServerParser::GetInstance("schema");
    parser->Receive(&config_db_, content.data(), content.length(), 0);
    task_util::WaitForIdle();
}

int main(int argc, char **argv) {
    bgp_log_test::init();
    ControlNode::SetDefaultSchedulingPolicy();
    ::testing::InitGoogleTest(&argc, argv);
    int error = RUN_ALL_TESTS();
    TaskScheduler::GetInstance()->Terminate();
    return error;
}
