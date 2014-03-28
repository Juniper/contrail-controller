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
#include "bgp/bgp_factory.h"
#include "bgp/bgp_log.h"
#include "bgp/bgp_server.h"
#include "control-node/control_node.h"
#include "db/db.h"
#include "db/db_graph.h"
#include "db/test/db_test_util.h"
#include "ifmap/ifmap_link_table.h"
#include "ifmap/ifmap_node.h"
#include "ifmap/ifmap_server_parser.h"
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

class BgpConfigManagerMock : public BgpConfigManager {
public:
    BgpConfigManagerMock(BgpServer *server) : BgpConfigManager(server) {
        null_obs_.instance = NULL;
        null_obs_.protocol = NULL;
        null_obs_.neighbor = NULL;
    }

    ~BgpConfigManagerMock() {
    }

    void UnregisterObservers() { RegisterObservers(null_obs_); }

private:
    BgpConfigManager::Observers null_obs_;
};

class BgpConfigManagerTest : public ::testing::Test {
protected:
    BgpConfigManagerTest()
        : server_(&evm_), parser_(&db_) {
        config_manager_ =
            static_cast<BgpConfigManagerMock *>(server_.config_manager());
        config_manager_->UnregisterObservers();
    }

    virtual void SetUp() {
        IFMapLinkTable_Init(&db_, &db_graph_);
        vnc_cfg_Server_ModuleInit(&db_, &db_graph_);
        bgp_schema_Server_ModuleInit(&db_, &db_graph_);
        config_manager_->Initialize(&db_, &db_graph_, "local");
    }
    virtual void TearDown() {
        server_.Shutdown();
        task_util::WaitForIdle();
        db_util::Clear(&db_);
    }

    int GetInstanceCount() {
        int count = 0;

        for (BgpConfigData::BgpInstanceMap::const_iterator iter =
                config_manager_->config().instances().begin();
                iter != config_manager_->config().instances().end(); ++iter) {
            const BgpInstanceConfig *rti = iter->second;
            if (rti->name() != BgpConfigManager::kMasterInstance) {
                TASK_UTIL_EXPECT_EQ(1, rti->import_list().size());
            }
            count++;
        }
        return count;
    }

    const BgpInstanceConfig *FindInstanceConfig(const string instance_name) {
        return config_manager_->config().FindInstance(instance_name);
    }

    EventManager evm_;
    BgpServer server_;
    DB db_;
    DBGraph db_graph_;
    BgpConfigManagerMock *config_manager_;
    BgpConfigParser parser_;
};

TEST_F(BgpConfigManagerTest, BgpRouterIdentifierChange) {
    string content_a = FileRead("controller/src/bgp/testdata/config_test_25a.xml");
    EXPECT_TRUE(parser_.Parse(content_a));
    task_util::WaitForIdle();

    const BgpConfigData::BgpInstanceMap &instances =
        config_manager_->config().instances();
    TASK_UTIL_ASSERT_TRUE(instances.end() !=
                          instances.find(BgpConfigManager::kMasterInstance));
    BgpConfigData::BgpInstanceMap::const_iterator loc =
        instances.find(BgpConfigManager::kMasterInstance);
    const BgpInstanceConfig *instance_cfg = loc->second;
    TASK_UTIL_ASSERT_TRUE(instance_cfg != NULL);
    const BgpProtocolConfig *protocol_cfg = instance_cfg->protocol_config();
    TASK_UTIL_ASSERT_TRUE(protocol_cfg != NULL);
    TASK_UTIL_ASSERT_TRUE(protocol_cfg->bgp_router() != NULL);

    // Identifier should be address since it's not specified explicitly.
    TASK_UTIL_EXPECT_EQ("127.0.0.1", protocol_cfg->router_params().identifier);

    // Identifier should change to 10.1.1.1.
    string content_b = FileRead("controller/src/bgp/testdata/config_test_25b.xml");
    EXPECT_TRUE(parser_.Parse(content_b));
    TASK_UTIL_EXPECT_TRUE(protocol_cfg->bgp_router() != NULL);
    TASK_UTIL_EXPECT_EQ("10.1.1.1", protocol_cfg->router_params().identifier);

    // Identifier should change to 20.1.1.1.
    string content_c = FileRead("controller/src/bgp/testdata/config_test_25c.xml");
    EXPECT_TRUE(parser_.Parse(content_c));
    TASK_UTIL_EXPECT_TRUE(protocol_cfg->bgp_router() != NULL);
    TASK_UTIL_EXPECT_EQ("20.1.1.1", protocol_cfg->router_params().identifier);

    // Identifier should go back to address since it's not specified explicitly.
    content_a = FileRead("controller/src/bgp/testdata/config_test_25a.xml");
    EXPECT_TRUE(parser_.Parse(content_a));
    TASK_UTIL_EXPECT_TRUE(protocol_cfg->bgp_router() != NULL);
    TASK_UTIL_EXPECT_EQ("127.0.0.1", protocol_cfg->router_params().identifier);

    boost::replace_all(content_a, "<config>", "<delete>");
    boost::replace_all(content_a, "</config>", "</delete>");
    EXPECT_TRUE(parser_.Parse(content_a));
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(1, config_manager_->config().instances().size());
    TASK_UTIL_EXPECT_EQ(0, db_graph_.vertex_count());
}

TEST_F(BgpConfigManagerTest, BgpRouterAutonomousSystemChange) {
    string content_a = FileRead("controller/src/bgp/testdata/config_test_24a.xml");
    EXPECT_TRUE(parser_.Parse(content_a));
    task_util::WaitForIdle();

    const BgpConfigData::BgpInstanceMap &instances =
        config_manager_->config().instances();
    TASK_UTIL_ASSERT_TRUE(instances.end() !=
                          instances.find(BgpConfigManager::kMasterInstance));
    BgpConfigData::BgpInstanceMap::const_iterator loc =
        instances.find(BgpConfigManager::kMasterInstance);
    const BgpInstanceConfig *instance_cfg = loc->second;
    TASK_UTIL_ASSERT_TRUE(instance_cfg != NULL);
    const BgpProtocolConfig *protocol_cfg = instance_cfg->protocol_config();
    TASK_UTIL_ASSERT_TRUE(protocol_cfg != NULL);
    TASK_UTIL_ASSERT_TRUE(protocol_cfg->bgp_router() != NULL);

    // AS should be kDefaultAutonomousSystem as it's not specified.
    TASK_UTIL_EXPECT_EQ(BgpConfigManager::kDefaultAutonomousSystem,
        protocol_cfg->router_params().autonomous_system);

    // AS time should change to 100.
    string content_b = FileRead("controller/src/bgp/testdata/config_test_24b.xml");
    EXPECT_TRUE(parser_.Parse(content_b));
    TASK_UTIL_EXPECT_TRUE(protocol_cfg->bgp_router() != NULL);
    TASK_UTIL_EXPECT_EQ(100, protocol_cfg->router_params().autonomous_system);

    // AS time should change to 101.
    string content_c = FileRead("controller/src/bgp/testdata/config_test_24c.xml");
    EXPECT_TRUE(parser_.Parse(content_c));
    TASK_UTIL_EXPECT_TRUE(protocol_cfg->bgp_router() != NULL);
    TASK_UTIL_EXPECT_EQ(101, protocol_cfg->router_params().autonomous_system);

    // AS should go back to kDefaultAutonomousSystem as it's not specified.
    content_a = FileRead("controller/src/bgp/testdata/config_test_24a.xml");
    EXPECT_TRUE(parser_.Parse(content_a));
    TASK_UTIL_EXPECT_TRUE(protocol_cfg->bgp_router() != NULL);
    TASK_UTIL_EXPECT_EQ(BgpConfigManager::kDefaultAutonomousSystem,
        protocol_cfg->router_params().autonomous_system);

    boost::replace_all(content_a, "<config>", "<delete>");
    boost::replace_all(content_a, "</config>", "</delete>");
    EXPECT_TRUE(parser_.Parse(content_a));
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(1, config_manager_->config().instances().size());
    TASK_UTIL_EXPECT_EQ(0, db_graph_.vertex_count());
}

TEST_F(BgpConfigManagerTest, BgpRouterHoldTimeChange) {
    string content_a = FileRead("controller/src/bgp/testdata/config_test_23a.xml");
    EXPECT_TRUE(parser_.Parse(content_a));
    task_util::WaitForIdle();

    const BgpConfigData::BgpInstanceMap &instances =
        config_manager_->config().instances();
    TASK_UTIL_ASSERT_TRUE(instances.end() !=
                          instances.find(BgpConfigManager::kMasterInstance));
    BgpConfigData::BgpInstanceMap::const_iterator loc =
        instances.find(BgpConfigManager::kMasterInstance);
    const BgpInstanceConfig *instance_cfg = loc->second;
    TASK_UTIL_ASSERT_TRUE(instance_cfg != NULL);
    const BgpProtocolConfig *protocol_cfg = instance_cfg->protocol_config();
    TASK_UTIL_ASSERT_TRUE(protocol_cfg != NULL);
    TASK_UTIL_ASSERT_TRUE(protocol_cfg->bgp_router() != NULL);

    // Hold time should be 0 since it's not specified explicitly.
    TASK_UTIL_EXPECT_EQ(0, protocol_cfg->router_params().hold_time);

    // Hold time should change to 9.
    string content_b = FileRead("controller/src/bgp/testdata/config_test_23b.xml");
    EXPECT_TRUE(parser_.Parse(content_b));
    TASK_UTIL_EXPECT_TRUE(protocol_cfg->bgp_router() != NULL);
    TASK_UTIL_EXPECT_EQ(9, protocol_cfg->router_params().hold_time);

    // Hold time should change to 27.
    string content_c = FileRead("controller/src/bgp/testdata/config_test_23c.xml");
    EXPECT_TRUE(parser_.Parse(content_c));
    TASK_UTIL_EXPECT_TRUE(protocol_cfg->bgp_router() != NULL);
    TASK_UTIL_EXPECT_EQ(27, protocol_cfg->router_params().hold_time);

    // Hold time should go back to 0 since it's not specified explicitly.
    content_a = FileRead("controller/src/bgp/testdata/config_test_23a.xml");
    EXPECT_TRUE(parser_.Parse(content_a));
    TASK_UTIL_EXPECT_TRUE(protocol_cfg->bgp_router() != NULL);
    TASK_UTIL_EXPECT_EQ(0, protocol_cfg->router_params().hold_time);

    boost::replace_all(content_a, "<config>", "<delete>");
    boost::replace_all(content_a, "</config>", "</delete>");
    EXPECT_TRUE(parser_.Parse(content_a));
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(1, config_manager_->config().instances().size());
    TASK_UTIL_EXPECT_EQ(0, db_graph_.vertex_count());
}

TEST_F(BgpConfigManagerTest, MasterNeighbors) {
    string content = FileRead("controller/src/bgp/testdata/config_test_1.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();
    const BgpConfigData::BgpInstanceMap &instances =
        config_manager_->config().instances();
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
    TASK_UTIL_EXPECT_EQ(1, config_manager_->config().instances().size());

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
    string content_a = FileRead("controller/src/bgp/testdata/config_test_15a.xml");
    EXPECT_TRUE(parser_.Parse(content_a));
    task_util::WaitForIdle();

    const BgpConfigData::BgpInstanceMap &instances =
        config_manager_->config().instances();
    TASK_UTIL_ASSERT_TRUE(instances.end() !=
                          instances.find(BgpConfigManager::kMasterInstance));

    BgpConfigData::BgpInstanceMap::const_iterator loc =
        instances.find(BgpConfigManager::kMasterInstance);
    const BgpInstanceConfig *cfg = loc->second;
    TASK_UTIL_ASSERT_TRUE(cfg != NULL);
    const BgpInstanceConfig::NeighborMap &neighbors = cfg->neighbors();

    TASK_UTIL_EXPECT_EQ(2, neighbors.size());

    string content_b = FileRead("controller/src/bgp/testdata/config_test_15b.xml");
    EXPECT_TRUE(parser_.Parse(content_b));
    task_util::WaitForIdle();

    TASK_UTIL_ASSERT_EQ(6, neighbors.size());

    boost::replace_all(content_b, "<config>", "<delete>");
    boost::replace_all(content_b, "</config>", "</delete>");
    EXPECT_TRUE(parser_.Parse(content_b));
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(1, config_manager_->config().instances().size());

    TASK_UTIL_EXPECT_EQ(0, db_graph_.edge_count());
    TASK_UTIL_EXPECT_EQ(0, db_graph_.vertex_count());
}

TEST_F(BgpConfigManagerTest, MasterNeighborsDelete) {
    string content_a = FileRead("controller/src/bgp/testdata/config_test_16a.xml");
    EXPECT_TRUE(parser_.Parse(content_a));
    task_util::WaitForIdle();

    const BgpConfigData::BgpInstanceMap &instances =
        config_manager_->config().instances();
    TASK_UTIL_ASSERT_TRUE(instances.end() !=
                          instances.find(BgpConfigManager::kMasterInstance));

    BgpConfigData::BgpInstanceMap::const_iterator loc =
        instances.find(BgpConfigManager::kMasterInstance);
    const BgpInstanceConfig *cfg = loc->second;
    TASK_UTIL_ASSERT_TRUE(cfg != NULL);
    const BgpInstanceConfig::NeighborMap &neighbors = cfg->neighbors();

    TASK_UTIL_EXPECT_EQ(6, neighbors.size());

    string content_b = FileRead("controller/src/bgp/testdata/config_test_16b.xml");
    EXPECT_TRUE(parser_.Parse(content_b));
    task_util::WaitForIdle();

    TASK_UTIL_ASSERT_EQ(4, neighbors.size());

    boost::replace_all(content_a, "<config>", "<delete>");
    boost::replace_all(content_a, "</config>", "</delete>");
    EXPECT_TRUE(parser_.Parse(content_a));
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(1, config_manager_->config().instances().size());

    TASK_UTIL_EXPECT_EQ(0, db_graph_.edge_count());
    TASK_UTIL_EXPECT_EQ(0, db_graph_.vertex_count());
}

TEST_F(BgpConfigManagerTest, InstanceTargetExport1) {
    string content = FileRead("controller/src/bgp/testdata/config_test_7.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(2, config_manager_->config().instances().size());
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

    TASK_UTIL_EXPECT_EQ(1, config_manager_->config().instances().size());
    TASK_UTIL_EXPECT_EQ(0, db_graph_.vertex_count());
}

TEST_F(BgpConfigManagerTest, InstanceTargetExport2) {
    string content = FileRead("controller/src/bgp/testdata/config_test_8.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(2, config_manager_->config().instances().size());
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

    TASK_UTIL_EXPECT_EQ(1, config_manager_->config().instances().size());
    TASK_UTIL_EXPECT_EQ(0, db_graph_.vertex_count());
}

TEST_F(BgpConfigManagerTest, InstanceTargetImport1) {
    string content = FileRead("controller/src/bgp/testdata/config_test_9.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(2, config_manager_->config().instances().size());
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

    TASK_UTIL_EXPECT_EQ(1, config_manager_->config().instances().size());
    TASK_UTIL_EXPECT_EQ(0, db_graph_.vertex_count());
}

TEST_F(BgpConfigManagerTest, InstanceTargetImport2) {
    string content = FileRead("controller/src/bgp/testdata/config_test_10.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(2, config_manager_->config().instances().size());
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

    TASK_UTIL_EXPECT_EQ(1, config_manager_->config().instances().size());
    TASK_UTIL_EXPECT_EQ(0, db_graph_.vertex_count());
}

TEST_F(BgpConfigManagerTest, VirtualNetwork1) {
    string content = FileRead("controller/src/bgp/testdata/config_test_20.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(3, config_manager_->config().instances().size());

    const BgpInstanceConfig *blue = FindInstanceConfig("blue");
    TASK_UTIL_ASSERT_TRUE(blue != NULL);
    TASK_UTIL_EXPECT_EQ("blue-vn", blue->virtual_network());
    TASK_UTIL_EXPECT_EQ(101, blue->virtual_network_index());

    const BgpInstanceConfig *red = FindInstanceConfig("red");
    TASK_UTIL_ASSERT_TRUE(red != NULL);
    TASK_UTIL_EXPECT_EQ("red-vn", red->virtual_network());
    TASK_UTIL_EXPECT_EQ(102, red->virtual_network_index());

    boost::replace_all(content, "<config>", "<delete>");
    boost::replace_all(content, "</config>", "</delete>");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(1, config_manager_->config().instances().size());
    TASK_UTIL_EXPECT_EQ(0, db_graph_.vertex_count());
}

TEST_F(BgpConfigManagerTest, VirtualNetwork2) {
    string content_a = FileRead("controller/src/bgp/testdata/config_test_21a.xml");
    EXPECT_TRUE(parser_.Parse(content_a));
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(3, config_manager_->config().instances().size());

    const BgpInstanceConfig *blue = FindInstanceConfig("blue");
    TASK_UTIL_ASSERT_TRUE(blue != NULL);
    TASK_UTIL_EXPECT_EQ("blue-vn", blue->virtual_network());
    TASK_UTIL_EXPECT_EQ(0, blue->virtual_network_index());

    const BgpInstanceConfig *red = FindInstanceConfig("red");
    TASK_UTIL_ASSERT_TRUE(red != NULL);
    TASK_UTIL_EXPECT_EQ("red-vn", red->virtual_network());
    TASK_UTIL_EXPECT_EQ(0, red->virtual_network_index());

    string content_b = FileRead("controller/src/bgp/testdata/config_test_21b.xml");
    EXPECT_TRUE(parser_.Parse(content_b));
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ("blue-vn", blue->virtual_network());
    TASK_UTIL_EXPECT_EQ(101, blue->virtual_network_index());

    TASK_UTIL_EXPECT_EQ("red-vn", red->virtual_network());
    TASK_UTIL_EXPECT_EQ(102, red->virtual_network_index());

    boost::replace_all(content_b, "<config>", "<delete>");
    boost::replace_all(content_b, "</config>", "</delete>");
    EXPECT_TRUE(parser_.Parse(content_b));
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(1, config_manager_->config().instances().size());
    TASK_UTIL_EXPECT_EQ(0, db_graph_.vertex_count());
}

TEST_F(BgpConfigManagerTest, VirtualNetwork3) {
    string content_a = FileRead("controller/src/bgp/testdata/config_test_22a.xml");
    EXPECT_TRUE(parser_.Parse(content_a));
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(3, config_manager_->config().instances().size());

    const BgpInstanceConfig *blue = FindInstanceConfig("blue");
    TASK_UTIL_ASSERT_TRUE(blue != NULL);
    TASK_UTIL_EXPECT_EQ("", blue->virtual_network());
    TASK_UTIL_EXPECT_EQ(0, blue->virtual_network_index());

    const BgpInstanceConfig *red = FindInstanceConfig("red");
    TASK_UTIL_ASSERT_TRUE(red != NULL);
    TASK_UTIL_EXPECT_EQ("", red->virtual_network());
    TASK_UTIL_EXPECT_EQ(0, red->virtual_network_index());

    string content_b = FileRead("controller/src/bgp/testdata/config_test_22b.xml");
    EXPECT_TRUE(parser_.Parse(content_b));
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ("blue-vn", blue->virtual_network());
    TASK_UTIL_EXPECT_EQ(101, blue->virtual_network_index());

    TASK_UTIL_EXPECT_EQ("red-vn", red->virtual_network());
    TASK_UTIL_EXPECT_EQ(102, red->virtual_network_index());

    boost::replace_all(content_b, "<config>", "<delete>");
    boost::replace_all(content_b, "</config>", "</delete>");
    EXPECT_TRUE(parser_.Parse(content_b));
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(1, config_manager_->config().instances().size());
    TASK_UTIL_EXPECT_EQ(0, db_graph_.vertex_count());
}

TEST_F(BgpConfigManagerTest, Instances) {
    string content = FileRead("controller/src/bgp/testdata/config_test_2.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(4, GetInstanceCount());

    boost::replace_all(content, "<config>", "<delete>");
    boost::replace_all(content, "</config>", "</delete>");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, config_manager_->config().instances().size());
    TASK_UTIL_EXPECT_EQ(0, db_graph_.vertex_count());
}

TEST_F(BgpConfigManagerTest, InstanceNeighbors) {
    string content = FileRead("controller/src/bgp/testdata/config_test_3.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    TASK_UTIL_ASSERT_TRUE(config_manager_->config().instances().end() !=
                          config_manager_->config().instances().find("test"));
    BgpConfigData::BgpInstanceMap::const_iterator loc =
        config_manager_->config().instances().find("test");
    const BgpInstanceConfig *rti = loc->second;
    const BgpInstanceConfig::NeighborMap &neighbors = rti->neighbors();
    TASK_UTIL_ASSERT_EQ(1, neighbors.size());
    const BgpNeighborConfig *peer = neighbors.begin()->second;
    TASK_UTIL_EXPECT_EQ(2, peer->peer_as());

    string update = FileRead("controller/src/bgp/testdata/config_test_4.xml");
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
    TASK_UTIL_EXPECT_EQ(1, config_manager_->config().instances().size());
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
    string content = FileRead("controller/src/bgp/testdata/initial-config.xml");
    IFMapServerParser *parser =
        IFMapServerParser::GetInstance("schema");
    parser->Receive(&config_db_, content.data(), content.length(), 0);
    task_util::WaitForIdle();
}

int main(int argc, char **argv) {
    bgp_log_test::init();
    ControlNode::SetDefaultSchedulingPolicy();
    BgpObjectFactory::Register<BgpConfigManager>(
        boost::factory<BgpConfigManagerMock *>());
    ::testing::InitGoogleTest(&argc, argv);
    int error = RUN_ALL_TESTS();
    TaskScheduler::GetInstance()->Terminate();
    return error;
}
