/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/bgp_config.h"

#include <bitset>
#include <fstream>
#include <boost/algorithm/string/replace.hpp>
#include <boost/foreach.hpp>

#include "base/logging.h"
#include "base/test/task_test_util.h"
#include "bgp/bgp_config.h"
#include "bgp/bgp_config_parser.h"
#include "bgp/bgp_factory.h"
#include "bgp/bgp_log.h"
#include "bgp/bgp_peer_types.h"
#include "bgp/bgp_sandesh.h"
#include "bgp/bgp_server.h"
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
    static bool validate_done_;

    static void ValidateShowInstanceResponse(Sandesh *sandesh,
        const vector<ShowBgpInstanceConfig> &instance_list);
    static void ValidateShowNeighborResponse(Sandesh *sandesh,
        const vector<ShowBgpNeighborConfig> &neighbor_list);
    static void ValidateShowPeeringResponse(Sandesh *sandesh,
        const vector<ShowBgpPeeringConfig> &peering_list);

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

    size_t GetPeeringCount() {
        return config_manager_->config().peerings().size();
    }

    const BgpPeeringConfig *FindPeeringConfig(const string peering_name) {
        return config_manager_->config().FindPeering(peering_name);
    }

    void VerifyBgpSessionExists(const BgpPeeringConfig *peering, string uuid) {
        TASK_UTIL_EXPECT_TRUE(peering->bgp_peering() != NULL);
        const autogen::BgpPeeringAttributes &attr =
            peering->bgp_peering()->data();
        bool found = false;
        for (autogen::BgpPeeringAttributes::const_iterator iter = attr.begin();
             iter != attr.end(); ++iter) {
            if (iter->uuid == uuid) {
                found = true;
                break;
            }
        }
        TASK_UTIL_EXPECT_TRUE(found);
    }

    void VerifyBgpSessions(const BgpPeeringConfig *peering,
        const bitset<8> &session_mask) {
        for (int idx = 0; idx < session_mask.size(); ++idx) {
            if (!session_mask.test(idx))
                continue;
            string uuid = integerToString(idx);
            VerifyBgpSessionExists(peering, uuid);
        }
    }

    EventManager evm_;
    BgpServer server_;
    DB db_;
    DBGraph db_graph_;
    BgpConfigManagerMock *config_manager_;
    BgpConfigParser parser_;
};

bool BgpConfigManagerTest::validate_done_;

void BgpConfigManagerTest::ValidateShowInstanceResponse(Sandesh *sandesh,
    const vector<ShowBgpInstanceConfig> &instance_list) {
    ShowBgpInstanceConfigResp *resp =
        dynamic_cast<ShowBgpInstanceConfigResp *>(sandesh);
    EXPECT_TRUE(resp != NULL);
    EXPECT_EQ(instance_list.size(), resp->get_instances().size() - 1);

    LOG(DEBUG, "************************************************************");
    BOOST_FOREACH(const ShowBgpInstanceConfig &resp_instance,
        resp->get_instances()) {
        if (resp_instance.get_name() == BgpConfigManager::kMasterInstance)
            continue;
        LOG(DEBUG, resp_instance.get_name());
        LOG(DEBUG, "  VN Name:  " << resp_instance.get_virtual_network());
        LOG(DEBUG, "  VN Index: " << resp_instance.get_virtual_network_index());
    }
    LOG(DEBUG, "************************************************************");

    BOOST_FOREACH(const ShowBgpInstanceConfig &instance, instance_list) {
        bool found = false;
        BOOST_FOREACH(const ShowBgpInstanceConfig &resp_instance,
            resp->get_instances()) {
            if (instance.get_name() == resp_instance.get_name()) {
                found = true;
                EXPECT_EQ(instance.get_virtual_network(),
                    resp_instance.get_virtual_network());
                EXPECT_EQ(instance.get_virtual_network_index(),
                    resp_instance.get_virtual_network_index());
                continue;
            }
        }
        EXPECT_TRUE(found);
        LOG(DEBUG, "Verified " << instance.get_name());
    }

    validate_done_ = true;
}

void BgpConfigManagerTest::ValidateShowNeighborResponse(Sandesh *sandesh,
    const vector<ShowBgpNeighborConfig> &neighbor_list) {
    ShowBgpNeighborConfigResp *resp =
        dynamic_cast<ShowBgpNeighborConfigResp *>(sandesh);
    EXPECT_TRUE(resp != NULL);
    EXPECT_EQ(neighbor_list.size(), resp->get_neighbors().size());

    LOG(DEBUG, "************************************************************");
    BOOST_FOREACH(const ShowBgpNeighborConfig &resp_neighbor,
        resp->get_neighbors()) {
        LOG(DEBUG, resp_neighbor.get_name());
        LOG(DEBUG, "  Instance:   " << resp_neighbor.get_instance_name());
        LOG(DEBUG, "  Vendor:     " << resp_neighbor.get_vendor());
        LOG(DEBUG, "  AS:         " << resp_neighbor.get_autonomous_system());
        LOG(DEBUG, "  Identifier: " << resp_neighbor.get_identifier());
        LOG(DEBUG, "  Address:    " << resp_neighbor.get_address());
    }
    LOG(DEBUG, "************************************************************");

    BOOST_FOREACH(const ShowBgpNeighborConfig &neighbor, neighbor_list) {
        bool found = false;
        BOOST_FOREACH(const ShowBgpNeighborConfig &resp_neighbor,
            resp->get_neighbors()) {
            if (neighbor.get_name() == resp_neighbor.get_name()) {
                found = true;
                EXPECT_EQ(neighbor.get_instance_name(),
                    resp_neighbor.get_instance_name());
                EXPECT_EQ(neighbor.get_vendor(),
                    resp_neighbor.get_vendor());
                EXPECT_EQ(neighbor.get_autonomous_system(),
                    resp_neighbor.get_autonomous_system());
                EXPECT_EQ(neighbor.get_identifier(),
                    resp_neighbor.get_identifier());
                EXPECT_EQ(neighbor.get_address(),
                    resp_neighbor.get_address());
                continue;
            }
        }
        EXPECT_TRUE(found);
        LOG(DEBUG, "Verified " << neighbor.get_name());
    }

    validate_done_ = true;
}

void BgpConfigManagerTest::ValidateShowPeeringResponse(Sandesh *sandesh,
    const vector<ShowBgpPeeringConfig> &peering_list) {
    ShowBgpPeeringConfigResp *resp =
        dynamic_cast<ShowBgpPeeringConfigResp *>(sandesh);
    EXPECT_TRUE(resp != NULL);
    EXPECT_EQ(peering_list.size(), resp->get_peerings().size());

    LOG(DEBUG, "************************************************************");
    BOOST_FOREACH(const ShowBgpPeeringConfig &resp_peering,
        resp->get_peerings()) {
        LOG(DEBUG, resp_peering.get_name());
        LOG(DEBUG, "  Instance:       " << resp_peering.get_instance_name());
        LOG(DEBUG, "  Neighbor Count: " << resp_peering.get_neighbor_count());
    }
    LOG(DEBUG, "************************************************************");

    BOOST_FOREACH(const ShowBgpPeeringConfig &peering, peering_list) {
        bool found = false;
        BOOST_FOREACH(const ShowBgpPeeringConfig &resp_peering,
            resp->get_peerings()) {
            if (peering.get_name() == resp_peering.get_name()) {
                found = true;
                EXPECT_EQ(peering.get_instance_name(),
                    resp_peering.get_instance_name());
                EXPECT_EQ(peering.get_neighbor_count(),
                    resp_peering.get_neighbor_count());
                continue;
            }
        }
        EXPECT_TRUE(found);
        LOG(DEBUG, "Verified " << peering.get_name());
    }

    validate_done_ = true;
}

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

TEST_F(BgpConfigManagerTest, PropagateBgpRouterIdentifierChangeToNeighbor) {
    string content_a = FileRead("controller/src/bgp/testdata/config_test_31.xml");
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
    const BgpInstanceConfig::NeighborMap &neighbors = instance_cfg->neighbors();
    TASK_UTIL_EXPECT_EQ(3, neighbors.size());

    for (BgpInstanceConfig::NeighborMap::const_iterator it = neighbors.begin();
         it != neighbors.end(); ++it) {
        TASK_UTIL_EXPECT_EQ("10.1.1.100", it->second->local_identifier());
    }

    autogen::BgpRouterParams *params = new autogen::BgpRouterParams;
    params->Copy(protocol_cfg->router_params());
    params->identifier = "10.1.1.200";
    string id = string(BgpConfigManager::kMasterInstance) + ":local";
    ifmap_test_util::IFMapMsgPropertyAdd(&db_, "bgp-router", id,
        "bgp-router-parameters", params);
    task_util::WaitForIdle();

    for (BgpInstanceConfig::NeighborMap::const_iterator it = neighbors.begin();
         it != neighbors.end(); ++it) {
        TASK_UTIL_EXPECT_EQ("10.1.1.200", it->second->local_identifier());
    }

    boost::replace_all(content_a, "<config>", "<delete>");
    boost::replace_all(content_a, "</config>", "</delete>");
    EXPECT_TRUE(parser_.Parse(content_a));
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(1, config_manager_->config().instances().size());
    TASK_UTIL_EXPECT_EQ(0, db_graph_.vertex_count());
}

TEST_F(BgpConfigManagerTest, PropagateBgpRouterAutonomousSystemChangeToNeighbor) {
    string content_a = FileRead("controller/src/bgp/testdata/config_test_31.xml");
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
    const BgpInstanceConfig::NeighborMap &neighbors = instance_cfg->neighbors();
    TASK_UTIL_EXPECT_EQ(3, neighbors.size());

    for (BgpInstanceConfig::NeighborMap::const_iterator it = neighbors.begin();
         it != neighbors.end(); ++it) {
        TASK_UTIL_EXPECT_EQ(100, it->second->local_as());
    }

    autogen::BgpRouterParams *params = new autogen::BgpRouterParams;
    params->Copy(protocol_cfg->router_params());
    params->autonomous_system = 200;
    string id = string(BgpConfigManager::kMasterInstance) + ":local";
    ifmap_test_util::IFMapMsgPropertyAdd(&db_, "bgp-router", id,
        "bgp-router-parameters", params);
    task_util::WaitForIdle();

    for (BgpInstanceConfig::NeighborMap::const_iterator it = neighbors.begin();
         it != neighbors.end(); ++it) {
        TASK_UTIL_EXPECT_EQ(200, it->second->local_as());
    }

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

// Add and delete new sessions between existing ones.
TEST_F(BgpConfigManagerTest, MasterPeeringUpdate1) {
    const BgpPeeringConfig *peering;
    char full_name[1024];
    snprintf(full_name, sizeof(full_name), "attr(%s:%s,%s:%s)",
        BgpConfigManager::kMasterInstance, "local",
        BgpConfigManager::kMasterInstance, "remote");

    string content_a = FileRead("controller/src/bgp/testdata/config_test_28a.xml");
    EXPECT_TRUE(parser_.Parse(content_a));
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(FindPeeringConfig(full_name) != NULL);
    peering = FindPeeringConfig(full_name);
    TASK_UTIL_EXPECT_EQ(2, peering->size());
    VerifyBgpSessionExists(peering, "1001");
    VerifyBgpSessionExists(peering, "1004");

    string content_b = FileRead("controller/src/bgp/testdata/config_test_28b.xml");
    EXPECT_TRUE(parser_.Parse(content_b));
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(FindPeeringConfig(full_name) != NULL);
    peering = FindPeeringConfig(full_name);
    TASK_UTIL_EXPECT_EQ(4, peering->size());
    VerifyBgpSessionExists(peering, "1001");
    VerifyBgpSessionExists(peering, "1002");
    VerifyBgpSessionExists(peering, "1003");
    VerifyBgpSessionExists(peering, "1004");

    EXPECT_TRUE(parser_.Parse(content_a));
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(FindPeeringConfig(full_name) != NULL);
    peering = FindPeeringConfig(full_name);
    TASK_UTIL_EXPECT_EQ(2, peering->size());
    VerifyBgpSessionExists(peering, "1001");
    VerifyBgpSessionExists(peering, "1004");

    boost::replace_all(content_a, "<config>", "<delete>");
    boost::replace_all(content_a, "</config>", "</delete>");
    EXPECT_TRUE(parser_.Parse(content_a));
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(1, config_manager_->config().instances().size());

    TASK_UTIL_EXPECT_EQ(0, db_graph_.edge_count());
    TASK_UTIL_EXPECT_EQ(0, db_graph_.vertex_count());
}

// Add and delete new sessions after existing ones.
TEST_F(BgpConfigManagerTest, MasterPeeringUpdate2) {
    const BgpPeeringConfig *peering;
    char full_name[1024];
    snprintf(full_name, sizeof(full_name), "attr(%s:%s,%s:%s)",
        BgpConfigManager::kMasterInstance, "local",
        BgpConfigManager::kMasterInstance, "remote");

    string content_a = FileRead("controller/src/bgp/testdata/config_test_29a.xml");
    EXPECT_TRUE(parser_.Parse(content_a));
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(FindPeeringConfig(full_name) != NULL);
    peering = FindPeeringConfig(full_name);
    TASK_UTIL_EXPECT_EQ(2, peering->size());
    VerifyBgpSessionExists(peering, "1001");
    VerifyBgpSessionExists(peering, "1002");

    string content_b = FileRead("controller/src/bgp/testdata/config_test_29b.xml");
    EXPECT_TRUE(parser_.Parse(content_b));
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(FindPeeringConfig(full_name) != NULL);
    peering = FindPeeringConfig(full_name);
    TASK_UTIL_EXPECT_EQ(4, peering->size());
    VerifyBgpSessionExists(peering, "1001");
    VerifyBgpSessionExists(peering, "1002");
    VerifyBgpSessionExists(peering, "1003");
    VerifyBgpSessionExists(peering, "1004");

    EXPECT_TRUE(parser_.Parse(content_a));
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(FindPeeringConfig(full_name) != NULL);
    peering = FindPeeringConfig(full_name);
    TASK_UTIL_EXPECT_EQ(2, peering->size());
    VerifyBgpSessionExists(peering, "1001");
    VerifyBgpSessionExists(peering, "1002");

    boost::replace_all(content_a, "<config>", "<delete>");
    boost::replace_all(content_a, "</config>", "</delete>");
    EXPECT_TRUE(parser_.Parse(content_a));
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(1, config_manager_->config().instances().size());

    TASK_UTIL_EXPECT_EQ(0, db_graph_.edge_count());
    TASK_UTIL_EXPECT_EQ(0, db_graph_.vertex_count());
}

// Add and delete new sessions before existing ones.
TEST_F(BgpConfigManagerTest, MasterPeeringUpdate3) {
    const BgpPeeringConfig *peering;
    char full_name[1024];
    snprintf(full_name, sizeof(full_name), "attr(%s:%s,%s:%s)",
        BgpConfigManager::kMasterInstance, "local",
        BgpConfigManager::kMasterInstance, "remote");

    string content_a = FileRead("controller/src/bgp/testdata/config_test_30a.xml");
    EXPECT_TRUE(parser_.Parse(content_a));
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(FindPeeringConfig(full_name) != NULL);
    peering = FindPeeringConfig(full_name);
    TASK_UTIL_EXPECT_EQ(2, peering->size());
    VerifyBgpSessionExists(peering, "1003");
    VerifyBgpSessionExists(peering, "1004");

    string content_b = FileRead("controller/src/bgp/testdata/config_test_30b.xml");
    EXPECT_TRUE(parser_.Parse(content_b));
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(FindPeeringConfig(full_name) != NULL);
    peering = FindPeeringConfig(full_name);
    TASK_UTIL_EXPECT_EQ(4, peering->size());
    VerifyBgpSessionExists(peering, "1001");
    VerifyBgpSessionExists(peering, "1002");
    VerifyBgpSessionExists(peering, "1003");
    VerifyBgpSessionExists(peering, "1004");

    EXPECT_TRUE(parser_.Parse(content_a));
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(FindPeeringConfig(full_name) != NULL);
    peering = FindPeeringConfig(full_name);
    TASK_UTIL_EXPECT_EQ(2, peering->size());
    VerifyBgpSessionExists(peering, "1003");
    VerifyBgpSessionExists(peering, "1004");

    boost::replace_all(content_a, "<config>", "<delete>");
    boost::replace_all(content_a, "</config>", "</delete>");
    EXPECT_TRUE(parser_.Parse(content_a));
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(1, config_manager_->config().instances().size());

    TASK_UTIL_EXPECT_EQ(0, db_graph_.edge_count());
    TASK_UTIL_EXPECT_EQ(0, db_graph_.vertex_count());
}

static string GeneratePeeringConfig(const bitset<8> &session_mask) {
    ostringstream oss;

    oss << "<config>";
    oss << "<bgp-router name=\'local\'>";
    oss << "    <address>127.0.0.1</address>";
    oss << "    <autonomous-system>1</autonomous-system>";
    oss << "    <address-families>";
    oss << "        <family>inet-vpn</family>";
    oss << "    </address-families>";

    for (int idx = 0; idx < session_mask.size(); ++idx) {
        if (!session_mask.test(idx))
            continue;
        oss << "<session to=\'remote:" << idx << "\'>";
        oss << "    <address-families>";
        oss << "        <family>inet-vpn</family>";
        oss << "    </address-families>";
        oss << "</session>";
    }
    oss << "</bgp-router>";

    oss << "<bgp-router name=\'remote\'>";
    oss << "    <address>127.0.0.2</address>";
    oss << "    <autonomous-system>1</autonomous-system>";
    oss << "    <address-families>";
    oss << "        <family>inet-vpn</family>";
    oss << "    </address-families>";

    for (int idx = 0; idx < session_mask.size(); ++idx) {
        if (!session_mask.test(idx))
            continue;
        oss << "<session to=\'local:" << idx << "\'>";
        oss << "    <address-families>";
        oss << "        <family>inet-vpn</family>";
        oss << "    </address-families>";
        oss << "</session>";
    }
    oss << "</bgp-router>";
    oss << "</config>";

    return oss.str();
}

//
// Iterate through all non-zero 8-bit values.  Each value is treated as a
// bitmask of sessions 0-7 and the corresponding config is generated and
// applied. The config is then modified to have all sessions (value 255).
// Next we go back to the original config based on the current value and
// then finally delete the entire config.
//
TEST_F(BgpConfigManagerTest, MasterPeeringUpdate4) {
    char full_name[1024];
    snprintf(full_name, sizeof(full_name), "attr(%s:%s,%s:%s)",
        BgpConfigManager::kMasterInstance, "local",
        BgpConfigManager::kMasterInstance, "remote");

    for (int idx = 1; idx <= 255; ++idx) {
        const BgpPeeringConfig *peering;

        bitset<8> session_mask_a(idx);
        string content_a = GeneratePeeringConfig(session_mask_a);
        EXPECT_TRUE(parser_.Parse(content_a));
        task_util::WaitForIdle();
        TASK_UTIL_EXPECT_TRUE(FindPeeringConfig(full_name) != NULL);
        peering = FindPeeringConfig(full_name);
        TASK_UTIL_EXPECT_EQ(session_mask_a.count(), peering->size());
        VerifyBgpSessions(peering, session_mask_a);

        bitset<8> session_mask_b(255);
        string content_b = GeneratePeeringConfig(session_mask_b);
        EXPECT_TRUE(parser_.Parse(content_b));
        task_util::WaitForIdle();
        TASK_UTIL_EXPECT_TRUE(FindPeeringConfig(full_name) != NULL);
        peering = FindPeeringConfig(full_name);
        TASK_UTIL_EXPECT_EQ(session_mask_b.count(), peering->size());
        VerifyBgpSessions(peering, session_mask_b);

        EXPECT_TRUE(parser_.Parse(content_a));
        task_util::WaitForIdle();
        TASK_UTIL_EXPECT_TRUE(FindPeeringConfig(full_name) != NULL);
        peering = FindPeeringConfig(full_name);
        TASK_UTIL_EXPECT_EQ(session_mask_a.count(), peering->size());
        VerifyBgpSessions(peering, session_mask_a);

        boost::replace_all(content_a, "<config>", "<delete>");
        boost::replace_all(content_a, "</config>", "</delete>");
        EXPECT_TRUE(parser_.Parse(content_a));
        task_util::WaitForIdle();

        TASK_UTIL_EXPECT_EQ(1, config_manager_->config().instances().size());

        TASK_UTIL_EXPECT_EQ(0, db_graph_.edge_count());
        TASK_UTIL_EXPECT_EQ(0, db_graph_.vertex_count());
    }
}

//
// Iterate through 8-bit values except 0/255. Each value is treated as a
// bitmask of sessions 0-7 and the corresponding config is generated and
// applied. The config is then modified to have sessions corresponding to
// the inverse of the bitmask. Next we go back to original config and then
// finally delete the entire config.
//
TEST_F(BgpConfigManagerTest, MasterPeeringUpdate5) {
    char full_name[1024];
    snprintf(full_name, sizeof(full_name), "attr(%s:%s,%s:%s)",
        BgpConfigManager::kMasterInstance, "local",
        BgpConfigManager::kMasterInstance, "remote");

    for (int idx = 1; idx <= 254; ++idx) {
        const BgpPeeringConfig *peering;

        bitset<8> session_mask_a(idx);
        string content_a = GeneratePeeringConfig(session_mask_a);
        EXPECT_TRUE(parser_.Parse(content_a));
        task_util::WaitForIdle();
        TASK_UTIL_EXPECT_TRUE(FindPeeringConfig(full_name) != NULL);
        peering = FindPeeringConfig(full_name);
        TASK_UTIL_EXPECT_EQ(session_mask_a.count(), peering->size());
        VerifyBgpSessions(peering, session_mask_a);

        bitset<8> session_mask_b = session_mask_a.flip();
        string content_b = GeneratePeeringConfig(session_mask_b);
        EXPECT_TRUE(parser_.Parse(content_b));
        task_util::WaitForIdle();
        TASK_UTIL_EXPECT_TRUE(FindPeeringConfig(full_name) != NULL);
        peering = FindPeeringConfig(full_name);
        TASK_UTIL_EXPECT_EQ(session_mask_b.count(), peering->size());
        VerifyBgpSessions(peering, session_mask_b);

        session_mask_a.flip();
        EXPECT_TRUE(parser_.Parse(content_a));
        task_util::WaitForIdle();
        TASK_UTIL_EXPECT_TRUE(FindPeeringConfig(full_name) != NULL);
        peering = FindPeeringConfig(full_name);
        TASK_UTIL_EXPECT_EQ(session_mask_a.count(), peering->size());
        VerifyBgpSessions(peering, session_mask_a);

        boost::replace_all(content_a, "<config>", "<delete>");
        boost::replace_all(content_a, "</config>", "</delete>");
        EXPECT_TRUE(parser_.Parse(content_a));
        task_util::WaitForIdle();

        TASK_UTIL_EXPECT_EQ(1, config_manager_->config().instances().size());

        TASK_UTIL_EXPECT_EQ(0, db_graph_.edge_count());
        TASK_UTIL_EXPECT_EQ(0, db_graph_.vertex_count());
    }
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

TEST_F(BgpConfigManagerTest, ShowInstances1) {
    string content = FileRead("controller/src/bgp/testdata/config_test_26a.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(4, GetInstanceCount());

    BgpSandeshContext sandesh_context;
    sandesh_context.bgp_server = &server_;
    Sandesh::set_client_context(&sandesh_context);

    const char *instance_name_list[] = { "blue", "green", "red" };
    vector<ShowBgpInstanceConfig> instance_list;
    BOOST_FOREACH(const char *instance_name, instance_name_list) {
        const BgpInstanceConfig *config = FindInstanceConfig(instance_name);
        TASK_UTIL_EXPECT_TRUE(config != NULL);
        ShowBgpInstanceConfig instance;
        instance.set_name(config->name());
        instance.set_virtual_network(config->virtual_network());
        instance.set_virtual_network_index(config->virtual_network_index());
        instance_list.push_back(instance);
    }
    Sandesh::set_response_callback(
        boost::bind(ValidateShowInstanceResponse, _1, instance_list));

    ShowBgpInstanceConfigReq *show_req = new ShowBgpInstanceConfigReq;
    validate_done_ = false;
    show_req->HandleRequest();
    show_req->Release();
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(validate_done_);

    boost::replace_all(content, "<config>", "<delete>");
    boost::replace_all(content, "</config>", "</delete>");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, config_manager_->config().instances().size());
    TASK_UTIL_EXPECT_EQ(0, db_graph_.vertex_count());
}

TEST_F(BgpConfigManagerTest, ShowInstances2) {
    string content = FileRead("controller/src/bgp/testdata/config_test_26b.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(3, GetInstanceCount());

    BgpSandeshContext sandesh_context;
    sandesh_context.bgp_server = &server_;
    Sandesh::set_client_context(&sandesh_context);

    const char *instance_name_list[] = { "blue-to-red", "red-to-blue" };
    vector<ShowBgpInstanceConfig> instance_list;
    BOOST_FOREACH(const char *instance_name, instance_name_list) {
        const BgpInstanceConfig *config = FindInstanceConfig(instance_name);
        TASK_UTIL_EXPECT_TRUE(config != NULL);
        ShowBgpInstanceConfig instance;
        instance.set_name(config->name());
        instance.set_virtual_network(config->virtual_network());
        instance.set_virtual_network_index(config->virtual_network_index());
        instance_list.push_back(instance);
    }
    Sandesh::set_response_callback(
        boost::bind(ValidateShowInstanceResponse, _1, instance_list));

    ShowBgpInstanceConfigReq *show_req = new ShowBgpInstanceConfigReq;
    validate_done_ = false;
    show_req->HandleRequest();
    show_req->Release();
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(validate_done_);

    boost::replace_all(content, "<config>", "<delete>");
    boost::replace_all(content, "</config>", "</delete>");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, config_manager_->config().instances().size());
    TASK_UTIL_EXPECT_EQ(0, db_graph_.vertex_count());
}

TEST_F(BgpConfigManagerTest, ShowInstances3) {
    string content = FileRead("controller/src/bgp/testdata/config_test_26c.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(2, GetInstanceCount());

    BgpSandeshContext sandesh_context;
    sandesh_context.bgp_server = &server_;
    Sandesh::set_client_context(&sandesh_context);

    const char *instance_name_list[] = { "blue-to-nat" };
    vector<ShowBgpInstanceConfig> instance_list;
    BOOST_FOREACH(const char *instance_name, instance_name_list) {
        const BgpInstanceConfig *config = FindInstanceConfig(instance_name);
        TASK_UTIL_EXPECT_TRUE(config != NULL);
        ShowBgpInstanceConfig instance;
        instance.set_name(config->name());
        instance.set_virtual_network(config->virtual_network());
        instance.set_virtual_network_index(config->virtual_network_index());
        instance_list.push_back(instance);
    }
    Sandesh::set_response_callback(
        boost::bind(ValidateShowInstanceResponse, _1, instance_list));

    ShowBgpInstanceConfigReq *show_req = new ShowBgpInstanceConfigReq;
    validate_done_ = false;
    show_req->HandleRequest();
    show_req->Release();
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(validate_done_);

    boost::replace_all(content, "<config>", "<delete>");
    boost::replace_all(content, "</config>", "</delete>");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, config_manager_->config().instances().size());
    TASK_UTIL_EXPECT_EQ(0, db_graph_.vertex_count());
}

TEST_F(BgpConfigManagerTest, ShowNeighbors) {
    string content = FileRead("controller/src/bgp/testdata/config_test_27.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(4, GetInstanceCount());

    BgpSandeshContext sandesh_context;
    sandesh_context.bgp_server = &server_;
    Sandesh::set_client_context(&sandesh_context);

    const BgpInstanceConfig *instance =
        FindInstanceConfig(BgpConfigManager::kMasterInstance);
    TASK_UTIL_EXPECT_TRUE(instance != NULL);

    const char *neighbor_name_list[] = { "remote1", "remote2", "remote3" };
    vector<ShowBgpNeighborConfig> neighbor_list;
    BOOST_FOREACH(const char *neighbor_name, neighbor_name_list) {
        string full_name(BgpConfigManager::kMasterInstance);
        full_name += ":";
        full_name += neighbor_name;
        const BgpNeighborConfig *config = instance->FindNeighbor(full_name);
        TASK_UTIL_EXPECT_TRUE(config != NULL);
        const autogen::BgpRouterParams &params = config->peer_config();
        ShowBgpNeighborConfig neighbor;
        neighbor.set_name(config->name());
        neighbor.set_instance_name(BgpConfigManager::kMasterInstance);
        neighbor.set_vendor(params.vendor);
        neighbor.set_autonomous_system(params.autonomous_system);
        neighbor.set_identifier(params.identifier);
        neighbor.set_address(params.address);
        neighbor_list.push_back(neighbor);
    }
    Sandesh::set_response_callback(
        boost::bind(ValidateShowNeighborResponse, _1, neighbor_list));

    ShowBgpNeighborConfigReq *show_req = new ShowBgpNeighborConfigReq;
    validate_done_ = false;
    show_req->HandleRequest();
    show_req->Release();
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(validate_done_);

    boost::replace_all(content, "<config>", "<delete>");
    boost::replace_all(content, "</config>", "</delete>");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, config_manager_->config().instances().size());
    TASK_UTIL_EXPECT_EQ(0, db_graph_.vertex_count());
}

TEST_F(BgpConfigManagerTest, ShowPeerings) {
    string content = FileRead("controller/src/bgp/testdata/config_test_27.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(3, GetPeeringCount());

    BgpSandeshContext sandesh_context;
    sandesh_context.bgp_server = &server_;
    Sandesh::set_client_context(&sandesh_context);

    const char *peering_name_list[] = { "remote1", "remote2", "remote3" };
    vector<ShowBgpPeeringConfig> peering_list;
    BOOST_FOREACH(const char *peering_name, peering_name_list) {
        char full_name[1024];
        snprintf(full_name, sizeof(full_name), "attr(%s:%s,%s:%s)",
            BgpConfigManager::kMasterInstance, "local",
            BgpConfigManager::kMasterInstance, peering_name);
        const BgpPeeringConfig *config = FindPeeringConfig(full_name);
        TASK_UTIL_EXPECT_TRUE(config != NULL);
        ShowBgpPeeringConfig peering;
        peering.set_name(config->name());
        peering.set_instance_name(BgpConfigManager::kMasterInstance);
        peering.set_neighbor_count(config->size());
        peering_list.push_back(peering);
    }

    Sandesh::set_response_callback(
        boost::bind(ValidateShowPeeringResponse, _1, peering_list));

    ShowBgpPeeringConfigReq *show_req = new ShowBgpPeeringConfigReq;
    validate_done_ = false;
    show_req->HandleRequest();
    show_req->Release();
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(validate_done_);

    boost::replace_all(content, "<config>", "<delete>");
    boost::replace_all(content, "</config>", "</delete>");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, config_manager_->config().instances().size());
    TASK_UTIL_EXPECT_EQ(0, db_graph_.vertex_count());
}

TEST_F(BgpConfigManagerTest, AddBgpRouterBeforeParentLink) {
    // Shenanigans to get rid of the default bgp-router config.
    string bgp_router_id = string(BgpConfigManager::kMasterInstance) + ":local";
    ifmap_test_util::IFMapMsgNodeAdd(&db_, "bgp-router", bgp_router_id);
    task_util::WaitForIdle();
    ifmap_test_util::IFMapMsgNodeDelete(&db_, "bgp-router", bgp_router_id);
    task_util::WaitForIdle();
    const BgpInstanceConfig *instance =
        FindInstanceConfig(BgpConfigManager::kMasterInstance);
    TASK_UTIL_EXPECT_TRUE(instance->protocol_config() == NULL);

    // Add bgp-router with parameters.
    autogen::BgpRouterParams *params = new autogen::BgpRouterParams;
    params->Clear();
    params->autonomous_system = 100;
    params->identifier = "10.1.1.100";
    params->address = "127.0.0.100";
    ifmap_test_util::IFMapMsgPropertyAdd(&db_, "bgp-router", bgp_router_id,
        "bgp-router-parameters", params);
    task_util::WaitForIdle();

    // Now add a link between the bgp-router and the routing-instance.
    ifmap_test_util::IFMapMsgLink(&db_,
        "routing-instance", BgpConfigManager::kMasterInstance,
        "bgp-router", bgp_router_id, "instance-bgp-router");
    task_util::WaitForIdle();

    // Verify the protocol config.
    TASK_UTIL_EXPECT_TRUE(instance->protocol_config() != NULL);
    const BgpProtocolConfig *protocol = instance->protocol_config();
    TASK_UTIL_EXPECT_TRUE(protocol->bgp_router() != NULL);
    const autogen::BgpRouterParams &router_params = protocol->router_params();
    TASK_UTIL_EXPECT_EQ(100, router_params.autonomous_system);
    TASK_UTIL_EXPECT_EQ("10.1.1.100", router_params.identifier);
    TASK_UTIL_EXPECT_EQ("127.0.0.100", router_params.address);
}

TEST_F(BgpConfigManagerTest, RemoveParentLinkBeforeBgpRouter) {
    autogen::BgpRouterParams *params = new autogen::BgpRouterParams;
    params->Clear();
    params->autonomous_system = 100;
    params->identifier = "10.1.1.100";
    params->address = "127.0.0.100";
    string bgp_router_id = string(BgpConfigManager::kMasterInstance) + ":local";
    ifmap_test_util::IFMapMsgLink(&db_,
        "routing-instance", BgpConfigManager::kMasterInstance,
        "bgp-router", bgp_router_id, "instance-bgp-router");
    ifmap_test_util::IFMapMsgPropertyAdd(&db_, "bgp-router", bgp_router_id,
        "bgp-router-parameters", params);
    task_util::WaitForIdle();

    // Remove the link between the bgp-router and the routing-instance.
    // Then notify the bgp-router to ensure that it's seen by the config.
    ifmap_test_util::IFMapMsgUnlink(&db_,
        "routing-instance", BgpConfigManager::kMasterInstance,
        "bgp-router", bgp_router_id, "instance-bgp-router");
    ifmap_test_util::IFMapNodeNotify(&db_, "bgp-router", bgp_router_id);
    task_util::WaitForIdle();

    // Verify the protocol config.
    const BgpInstanceConfig *instance =
        FindInstanceConfig(BgpConfigManager::kMasterInstance);
    TASK_UTIL_EXPECT_TRUE(instance->protocol_config() != NULL);
    const BgpProtocolConfig *protocol = instance->protocol_config();
    TASK_UTIL_EXPECT_TRUE(protocol->bgp_router() != NULL);
    const autogen::BgpRouterParams &router_params = protocol->router_params();
    TASK_UTIL_EXPECT_EQ(100, router_params.autonomous_system);
    TASK_UTIL_EXPECT_EQ("10.1.1.100", router_params.identifier);
    TASK_UTIL_EXPECT_EQ("127.0.0.100", router_params.address);
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
