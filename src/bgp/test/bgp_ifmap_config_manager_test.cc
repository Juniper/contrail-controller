/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/bgp_config_ifmap.h"

#include <bitset>
#include <fstream>
#include <boost/foreach.hpp>
#include <boost/assign/list_of.hpp>

#include "base/test/task_test_util.h"
#include "base/string_util.h"
#include "bgp/bgp_config_parser.h"
#include "bgp/bgp_factory.h"
#include "bgp/bgp_ifmap_sandesh.h"
#include "bgp/bgp_log.h"
#include "bgp/bgp_server.h"
#include "bgp/bgp_peer_internal_types.h"
#include "bgp/bgp_sandesh.h"
#include "control-node/control_node.h"
#include "db/db_graph.h"
#include "db/test/db_test_util.h"
#include "ifmap/ifmap_link_table.h"
#include "ifmap/ifmap_node.h"
#include "ifmap/test/ifmap_test_util.h"
#include "schema/bgp_schema_types.h"
#include "schema/vnc_cfg_types.h"

using boost::assign::list_of;
using namespace std;

typedef pair<std::string, std::string> string_pair_t;

static string FileRead(const string &filename) {
    ifstream file(filename.c_str());
    string content((istreambuf_iterator<char>(file)),
                   istreambuf_iterator<char>());
    return content;
}

static std::string BgpIdentifierToString(uint32_t identifier) {
    Ip4Address addr(ntohl(identifier));
    return addr.to_string();
}

static int ConfigInstanceCount(BgpConfigManager *manager) {
    int count = 0;
    typedef std::pair<std::string, const BgpInstanceConfig *> iter_t;
    BOOST_FOREACH(iter_t iter, manager->InstanceMapItems()) {
        const BgpInstanceConfig *rti = iter.second;
        if (rti->name() != BgpConfigManager::kMasterInstance) {
            TASK_UTIL_EXPECT_EQ(1, rti->import_list().size());
        }
        count++;
    }
    return count;
}

/*
 * Unit tests for BgpIfmapConfigManager
 */
class BgpIfmapConfigManagerTest : public ::testing::Test {
protected:
    BgpIfmapConfigManagerTest()
            : db_(TaskScheduler::GetInstance()->GetTaskId("db::IFMapTable")),
              config_manager_(new BgpIfmapConfigManager(NULL)),
              parser_(&db_) {
    }

    virtual void SetUp() {
        IFMapLinkTable_Init(&db_, &db_graph_);
        vnc_cfg_Server_ModuleInit(&db_, &db_graph_);
        bgp_schema_Server_ModuleInit(&db_, &db_graph_);
        config_manager_->Initialize(&db_, &db_graph_, "local");
    }

    virtual void TearDown() {
        config_manager_->Terminate();
        task_util::WaitForIdle();
        db_util::Clear(&db_);
    }

    int GetInstanceCount() {
        return ConfigInstanceCount(config_manager_.get());
    }

    const BgpInstanceConfig *FindInstanceConfig(const string instance_name) {
        return config_manager_->FindInstance(instance_name);
    }

    size_t GetPeeringCount() {
        return config_manager_->config()->PeeringCount();
    }

    const BgpIfmapPeeringConfig *FindPeeringConfig(const string peering_name) {
        return config_manager_->config()->FindPeering(peering_name);
    }

    const BgpIfmapRoutingPolicyConfig *
        FindRoutingPolicyIfmapConfig(const string policy_name) {
        return config_manager_->config()->FindRoutingPolicy(policy_name);
    }

    const BgpRoutingPolicyConfig *
        FindRoutingPolicyConfig(const string policy_name) {
        return config_manager_->FindRoutingPolicy(policy_name);
    }

    DB db_;
    DBGraph db_graph_;
    std::auto_ptr<BgpIfmapConfigManager> config_manager_;
    BgpConfigParser parser_;
};

/*
 * Show command tests for configuration. These require a bgp_server
 * object to be created since that is the context passed to the show
 * command dispatch functions.
 */
class BgpIfmapConfigManagerShowTest : public ::testing::Test {
  protected:
    BgpIfmapConfigManagerShowTest()
            : db_(TaskScheduler::GetInstance()->GetTaskId("db::IFMapTable")),
              server_(&evm_), parser_(&db_) {
        config_manager_ =
                static_cast<BgpIfmapConfigManager *>(server_.config_manager());
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

    const BgpIfmapPeeringConfig *FindPeeringConfig(const string peering_name) {
        return config_manager_->config()->FindPeering(peering_name);
    }

    const BgpInstanceConfig *FindInstanceConfig(const string instance_name) {
        return config_manager_->FindInstance(instance_name);
    }

    const BgpRoutingPolicyConfig *
        FindRoutingPolicyConfig(const string policy_name) {
        return config_manager_->FindRoutingPolicy(policy_name);
    }

    EventManager evm_;
    DB db_;
    DBGraph db_graph_;
    BgpServer server_;
    BgpIfmapConfigManager *config_manager_;
    BgpConfigParser parser_;
};

static void VerifyBgpSessionExists(const BgpIfmapPeeringConfig *peering,
                                   string uuid) {
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

static void VerifyBgpSessions(const BgpIfmapPeeringConfig *peering,
                              const bitset<8> &session_mask) {
    for (size_t idx = 0; idx < session_mask.size(); ++idx) {
        if (!session_mask.test(idx))
            continue;
        string uuid = integerToString(idx);
        VerifyBgpSessionExists(peering, uuid);
    }
}

static void ValidateShowRoutingPolicyResponse(
    Sandesh *sandesh, bool *done,
    const vector<ShowBgpRoutingPolicyConfig> &policy_list) {
    ShowBgpRoutingPolicyConfigResp *resp =
        dynamic_cast<ShowBgpRoutingPolicyConfigResp *>(sandesh);
    EXPECT_TRUE(resp != NULL);
    EXPECT_EQ(policy_list.size(), resp->get_routing_policies().size());

    BOOST_FOREACH(const ShowBgpRoutingPolicyConfig &policy, policy_list) {
        bool found = false;
        BOOST_FOREACH(const ShowBgpRoutingPolicyConfig &resp_policy,
            resp->get_routing_policies()) {
            if (policy.get_name() == resp_policy.get_name()) {
                found = true;
                EXPECT_EQ(policy.get_terms().size(),
                          resp_policy.get_terms().size());
                ASSERT_TRUE(std::equal(policy.get_terms().begin(),
                               policy.get_terms().end(),
                               resp_policy.get_terms().begin()));
                break;
            }
        }
        EXPECT_TRUE(found);
        LOG(DEBUG, "Verified " << policy.get_name());
    }

    *done = true;
}

static void ValidateShowInstanceResponse(
    Sandesh *sandesh, bool *done,
    const vector<ShowBgpInstanceConfig> &instance_list) {
    ShowBgpInstanceConfigResp *resp =
        dynamic_cast<ShowBgpInstanceConfigResp *>(sandesh);
    EXPECT_TRUE(resp != NULL);
    EXPECT_EQ(instance_list.size(), resp->get_instances().size());

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
        if (instance.get_name() == BgpConfigManager::kMasterInstance)
            continue;
        bool found = false;
        BOOST_FOREACH(const ShowBgpInstanceConfig &resp_instance,
            resp->get_instances()) {
            if (instance.get_name() == resp_instance.get_name()) {
                found = true;
                EXPECT_EQ(instance.get_virtual_network(),
                    resp_instance.get_virtual_network());
                EXPECT_EQ(instance.get_virtual_network_index(),
                    resp_instance.get_virtual_network_index());
                EXPECT_EQ(instance.get_aggregate_routes().size(),
                          resp_instance.get_aggregate_routes().size());
                ASSERT_TRUE(std::equal(instance.get_aggregate_routes().begin(),
                               instance.get_aggregate_routes().end(),
                               resp_instance.get_aggregate_routes().begin()));
                EXPECT_EQ(instance.get_routing_policies().size(),
                          resp_instance.get_routing_policies().size());
                ASSERT_TRUE(std::equal(instance.get_routing_policies().begin(),
                               instance.get_routing_policies().end(),
                               resp_instance.get_routing_policies().begin()));
                break;
            }
        }
        EXPECT_TRUE(found);
        LOG(DEBUG, "Verified " << instance.get_name());
    }

    *done = true;
}

static void ValidateShowNeighborResponse(
    Sandesh *sandesh, bool *done,
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
        LOG(DEBUG, "  Log:        " << resp_neighbor.log());
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

    *done = true;
}

static void ValidateShowPeeringResponse(
    Sandesh *sandesh, bool *done,
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

    *done = true;
}

TEST_F(BgpIfmapConfigManagerTest, BgpRouterIdentifierChange) {
    string content_a = FileRead("controller/src/bgp/testdata/config_test_25a.xml");
    EXPECT_TRUE(parser_.Parse(content_a));
    task_util::WaitForIdle();

    const BgpProtocolConfig *protocol_cfg =
            config_manager_->GetProtocolConfig(
                BgpConfigManager::kMasterInstance);
    ASSERT_TRUE(protocol_cfg != NULL);

    // Identifier should be address since it's not specified explicitly.
    TASK_UTIL_EXPECT_EQ("127.0.0.1",
                        BgpIdentifierToString(protocol_cfg->identifier()));

    // Identifier should change to 10.1.1.1.
    string content_b = FileRead("controller/src/bgp/testdata/config_test_25b.xml");
    EXPECT_TRUE(parser_.Parse(content_b));
    TASK_UTIL_EXPECT_EQ("10.1.1.1",
                        BgpIdentifierToString(protocol_cfg->identifier()));

    // Identifier should change to 20.1.1.1.
    string content_c = FileRead("controller/src/bgp/testdata/config_test_25c.xml");
    EXPECT_TRUE(parser_.Parse(content_c));
    TASK_UTIL_EXPECT_EQ("20.1.1.1",
                        BgpIdentifierToString(protocol_cfg->identifier()));

    // Identifier should go back to address since it's not specified explicitly.
    content_a = FileRead("controller/src/bgp/testdata/config_test_25a.xml");
    EXPECT_TRUE(parser_.Parse(content_a));
    TASK_UTIL_EXPECT_EQ("127.0.0.1",
                        BgpIdentifierToString(protocol_cfg->identifier()));

    boost::replace_all(content_a, "<config>", "<delete>");
    boost::replace_all(content_a, "</config>", "</delete>");
    EXPECT_TRUE(parser_.Parse(content_a));
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(1, config_manager_->config()->instances().size());
    TASK_UTIL_EXPECT_EQ(0, db_graph_.vertex_count());
}

TEST_F(BgpIfmapConfigManagerTest, BgpRouterAutonomousSystemChange) {
    string content_a = FileRead("controller/src/bgp/testdata/config_test_24a.xml");
    EXPECT_TRUE(parser_.Parse(content_a));
    task_util::WaitForIdle();

    const BgpProtocolConfig *protocol_cfg =
            config_manager_->GetProtocolConfig(
                BgpConfigManager::kMasterInstance);
    ASSERT_TRUE(protocol_cfg != NULL);

    // AS should be kDefaultAutonomousSystem as it's not specified.
    EXPECT_EQ(BgpConfigManager::kDefaultAutonomousSystem,
              protocol_cfg->autonomous_system());

    // AS time should change to 100.
    string content_b = FileRead("controller/src/bgp/testdata/config_test_24b.xml");
    EXPECT_TRUE(parser_.Parse(content_b));
    TASK_UTIL_EXPECT_EQ(100, protocol_cfg->autonomous_system());

    // AS time should change to 101.
    string content_c = FileRead("controller/src/bgp/testdata/config_test_24c.xml");
    EXPECT_TRUE(parser_.Parse(content_c));
    TASK_UTIL_EXPECT_EQ(101, protocol_cfg->autonomous_system());

    // AS should go back to kDefaultAutonomousSystem as it's not specified.
    content_a = FileRead("controller/src/bgp/testdata/config_test_24a.xml");
    EXPECT_TRUE(parser_.Parse(content_a));
    TASK_UTIL_EXPECT_EQ(BgpConfigManager::kDefaultAutonomousSystem,
                        protocol_cfg->autonomous_system());

    boost::replace_all(content_a, "<config>", "<delete>");
    boost::replace_all(content_a, "</config>", "</delete>");
    EXPECT_TRUE(parser_.Parse(content_a));
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(1, config_manager_->config()->instances().size());
    TASK_UTIL_EXPECT_EQ(0, db_graph_.vertex_count());
}

TEST_F(BgpIfmapConfigManagerTest, BgpRouterHoldTimeChange) {
    string content_a = FileRead(
        "controller/src/bgp/testdata/config_test_23a.xml");
    EXPECT_TRUE(parser_.Parse(content_a));
    task_util::WaitForIdle();

    const BgpProtocolConfig *protocol_cfg =
            config_manager_->GetProtocolConfig(
                BgpConfigManager::kMasterInstance);
    ASSERT_TRUE(protocol_cfg != NULL);

    // Hold time should be 0 since it's not specified explicitly.
    EXPECT_EQ(0, protocol_cfg->hold_time());

    // Hold time should change to 9.
    string content_b = FileRead(
        "controller/src/bgp/testdata/config_test_23b.xml");
    EXPECT_TRUE(parser_.Parse(content_b));
    TASK_UTIL_EXPECT_EQ(9, protocol_cfg->hold_time());

    // Hold time should change to 27.
    string content_c = FileRead(
        "controller/src/bgp/testdata/config_test_23c.xml");
    EXPECT_TRUE(parser_.Parse(content_c));
    TASK_UTIL_EXPECT_EQ(27, protocol_cfg->hold_time());

    // Hold time should go back to 0 since it's not specified explicitly.
    content_a = FileRead("controller/src/bgp/testdata/config_test_23a.xml");
    EXPECT_TRUE(parser_.Parse(content_a));
    TASK_UTIL_EXPECT_EQ(0, protocol_cfg->hold_time());

    boost::replace_all(content_a, "<config>", "<delete>");
    boost::replace_all(content_a, "</config>", "</delete>");
    EXPECT_TRUE(parser_.Parse(content_a));
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(1, config_manager_->config()->instances().size());
    TASK_UTIL_EXPECT_EQ(0, db_graph_.vertex_count());
}

TEST_F(BgpIfmapConfigManagerTest,
       PropagateBgpRouterIdentifierChangeToNeighbor) {
    string content_a = FileRead(
        "controller/src/bgp/testdata/config_test_31.xml");
    EXPECT_TRUE(parser_.Parse(content_a));
    task_util::WaitForIdle();

    int count = 0;
    typedef std::pair<std::string, const BgpNeighborConfig *> iter_t;
    BOOST_FOREACH(iter_t iter,
                  config_manager_->NeighborMapItems(
                      BgpConfigManager::kMasterInstance)) {
        TASK_UTIL_EXPECT_EQ(
            "10.1.1.100",
            BgpIdentifierToString(iter.second->local_identifier()));
        count++;
    }
    EXPECT_EQ(3, count);

    autogen::BgpRouterParams *params = new autogen::BgpRouterParams;
    params->Clear();
    params->identifier = "10.1.1.200";
    string id = string(BgpConfigManager::kMasterInstance) + ":local";
    ifmap_test_util::IFMapMsgPropertyAdd(&db_, "bgp-router", id,
        "bgp-router-parameters", params);
    task_util::WaitForIdle();

    BOOST_FOREACH(iter_t iter,
                  config_manager_->NeighborMapItems(
                      BgpConfigManager::kMasterInstance)) {
        TASK_UTIL_EXPECT_EQ(
            "10.1.1.200",
            BgpIdentifierToString(iter.second->local_identifier()));
    }

    boost::replace_all(content_a, "<config>", "<delete>");
    boost::replace_all(content_a, "</config>", "</delete>");
    EXPECT_TRUE(parser_.Parse(content_a));
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(1, config_manager_->config()->instances().size());
    TASK_UTIL_EXPECT_EQ(0, db_graph_.vertex_count());
}

TEST_F(BgpIfmapConfigManagerTest,
       PropagateBgpRouterAutonomousSystemChangeToNeighbor) {
    string content_a = FileRead(
        "controller/src/bgp/testdata/config_test_31.xml");
    EXPECT_TRUE(parser_.Parse(content_a));
    task_util::WaitForIdle();

    int count = 0;
    typedef std::pair<std::string, const BgpNeighborConfig *> iter_t;
    BOOST_FOREACH(iter_t iter,
                  config_manager_->NeighborMapItems(
                      BgpConfigManager::kMasterInstance)) {
        TASK_UTIL_EXPECT_EQ(100, iter.second->local_as());
        count++;
    }
    EXPECT_EQ(3, count);

    autogen::BgpRouterParams *params = new autogen::BgpRouterParams;
    params->Clear();
    params->autonomous_system = 200;
    string id = string(BgpConfigManager::kMasterInstance) + ":local";
    ifmap_test_util::IFMapMsgPropertyAdd(&db_, "bgp-router", id,
        "bgp-router-parameters", params);
    task_util::WaitForIdle();

    BOOST_FOREACH(iter_t iter,
                  config_manager_->NeighborMapItems(
                      BgpConfigManager::kMasterInstance)) {
        TASK_UTIL_EXPECT_EQ(200, iter.second->local_as());
    }

    boost::replace_all(content_a, "<config>", "<delete>");
    boost::replace_all(content_a, "</config>", "</delete>");
    EXPECT_TRUE(parser_.Parse(content_a));
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(1, config_manager_->config()->instances().size());
    TASK_UTIL_EXPECT_EQ(0, db_graph_.vertex_count());
}

TEST_F(BgpIfmapConfigManagerTest, MasterNeighbors) {
    string content = FileRead("controller/src/bgp/testdata/config_test_1.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    const char *addresses[] = {"10.1.1.1", "10.1.1.2", "10.1.1.2"};
    int index = 0;
    typedef std::pair<std::string, const BgpNeighborConfig *> iter_t;
    BOOST_FOREACH(iter_t iter,
                  config_manager_->NeighborMapItems(
                      BgpConfigManager::kMasterInstance)) {
        EXPECT_EQ(addresses[index], iter.second->peer_address().to_string());
        index++;
    }
    EXPECT_EQ(3, index);

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

    ASSERT_EQ(
        3, config_manager_->NeighborCount(BgpConfigManager::kMasterInstance));
    const BgpNeighborConfig *peer_config =
            config_manager_->FindNeighbor(
                BgpConfigManager::kMasterInstance, "remote1");
    ASSERT_TRUE(peer_config);
    EXPECT_EQ(102, peer_config->peer_as());

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

    ASSERT_EQ(
        2, config_manager_->NeighborCount(BgpConfigManager::kMasterInstance));

    peer_config = config_manager_->FindNeighbor(
        BgpConfigManager::kMasterInstance, "remote2");
    ASSERT_TRUE(peer_config);
    EXPECT_EQ("10.1.1.2", peer_config->peer_address().to_string());

    boost::replace_all(content, "<config>", "<delete>");
    boost::replace_all(content, "</config>", "</delete>");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, config_manager_->config()->instances().size());

    TASK_UTIL_EXPECT_EQ(0, db_graph_.edge_count());
    for (DBGraph::edge_iterator edge_iter = db_graph_.edge_list_begin();
         edge_iter != db_graph_.edge_list_end(); ++edge_iter) {
        const DBGraph::DBEdgeInfo &tuple = *edge_iter;
        IFMapNode *left = static_cast<IFMapNode *>(boost::get<0>(tuple));
        IFMapNode *right = static_cast<IFMapNode *>(boost::get<1>(tuple));
        BGP_DEBUG_UT("[" << left->name() << ", " << right->name() << "]");
    }

    TASK_UTIL_EXPECT_EQ(0, db_graph_.vertex_count());
    for (DBGraph::vertex_iterator v_iter = db_graph_.vertex_list_begin();
         v_iter != db_graph_.vertex_list_end(); ++v_iter) {
        IFMapNode *node = static_cast<IFMapNode *>(v_iter.operator->());
        BGP_DEBUG_UT("vertex: " << node->name());
    }
}

TEST_F(BgpIfmapConfigManagerTest, MasterNeighborsAdd) {
    string content_a = FileRead(
        "controller/src/bgp/testdata/config_test_15a.xml");
    EXPECT_TRUE(parser_.Parse(content_a));
    task_util::WaitForIdle();

    EXPECT_EQ(
        2, config_manager_->NeighborCount(BgpConfigManager::kMasterInstance));

    string content_b = FileRead(
        "controller/src/bgp/testdata/config_test_15b.xml");
    EXPECT_TRUE(parser_.Parse(content_b));
    task_util::WaitForIdle();

    EXPECT_EQ(
        6, config_manager_->NeighborCount(BgpConfigManager::kMasterInstance));

    boost::replace_all(content_b, "<config>", "<delete>");
    boost::replace_all(content_b, "</config>", "</delete>");
    EXPECT_TRUE(parser_.Parse(content_b));
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(1, config_manager_->config()->instances().size());

    TASK_UTIL_EXPECT_EQ(0, db_graph_.edge_count());
    TASK_UTIL_EXPECT_EQ(0, db_graph_.vertex_count());
}

TEST_F(BgpIfmapConfigManagerTest, MasterNeighborsDelete) {
    string content_a = FileRead(
        "controller/src/bgp/testdata/config_test_16a.xml");
    EXPECT_TRUE(parser_.Parse(content_a));
    task_util::WaitForIdle();

    EXPECT_EQ(
        6, config_manager_->NeighborCount(BgpConfigManager::kMasterInstance));

    string content_b = FileRead(
        "controller/src/bgp/testdata/config_test_16b.xml");
    EXPECT_TRUE(parser_.Parse(content_b));
    task_util::WaitForIdle();

    EXPECT_EQ(
        4, config_manager_->NeighborCount(BgpConfigManager::kMasterInstance));

    boost::replace_all(content_a, "<config>", "<delete>");
    boost::replace_all(content_a, "</config>", "</delete>");
    EXPECT_TRUE(parser_.Parse(content_a));
    task_util::WaitForIdle();

    EXPECT_EQ(1, config_manager_->config()->instances().size());

    TASK_UTIL_EXPECT_EQ(0, db_graph_.edge_count());
    TASK_UTIL_EXPECT_EQ(0, db_graph_.vertex_count());
}

TEST_F(BgpIfmapConfigManagerTest, MasterNeighborAttributes) {
    string content_a = FileRead(
        "controller/src/bgp/testdata/config_test_35a.xml");
    EXPECT_TRUE(parser_.Parse(content_a));
    task_util::WaitForIdle();

    EXPECT_EQ(
        1, config_manager_->NeighborCount(BgpConfigManager::kMasterInstance));
    const BgpNeighborConfig *config1 = config_manager_->FindNeighbor(
        BgpConfigManager::kMasterInstance, "remote:100");
    EXPECT_EQ(2, config1->family_attributes_list().size());
    BOOST_FOREACH(const BgpFamilyAttributesConfig &family_config,
        config1->family_attributes_list()) {
        if (family_config.family == "inet") {
            EXPECT_EQ(2, family_config.loop_count);
        } else if (family_config.family == "inet-vpn") {
            EXPECT_EQ(4, family_config.loop_count);
        } else {
            EXPECT_TRUE(false);
        }
    }

    string content_b = FileRead(
        "controller/src/bgp/testdata/config_test_35b.xml");
    EXPECT_TRUE(parser_.Parse(content_b));
    task_util::WaitForIdle();

    const BgpNeighborConfig *config2 = config_manager_->FindNeighbor(
        BgpConfigManager::kMasterInstance, "remote:100");
    EXPECT_EQ(2, config2->family_attributes_list().size());
    BOOST_FOREACH(const BgpFamilyAttributesConfig &family_config,
        config2->family_attributes_list()) {
        if (family_config.family == "inet") {
            EXPECT_EQ(4, family_config.loop_count);
        } else if (family_config.family == "inet-vpn") {
            EXPECT_EQ(2, family_config.loop_count);
        } else {
            EXPECT_TRUE(false);
        }
    }

    boost::replace_all(content_b, "<config>", "<delete>");
    boost::replace_all(content_b, "</config>", "</delete>");
    EXPECT_TRUE(parser_.Parse(content_b));
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(1, config_manager_->config()->instances().size());

    TASK_UTIL_EXPECT_EQ(0, db_graph_.edge_count());
    TASK_UTIL_EXPECT_EQ(0, db_graph_.vertex_count());
}

// Add and delete new sessions between existing ones.
TEST_F(BgpIfmapConfigManagerTest, MasterPeeringUpdate1) {
    const BgpIfmapPeeringConfig *peering;
    char full_name[1024];
    snprintf(full_name, sizeof(full_name), "attr(%s:%s,%s:%s)",
        BgpConfigManager::kMasterInstance, "local",
        BgpConfigManager::kMasterInstance, "remote");

    string content_a = FileRead(
        "controller/src/bgp/testdata/config_test_28a.xml");
    EXPECT_TRUE(parser_.Parse(content_a));
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(FindPeeringConfig(full_name) != NULL);
    peering = FindPeeringConfig(full_name);
    TASK_UTIL_EXPECT_EQ(2, peering->size());
    VerifyBgpSessionExists(peering, "1001");
    VerifyBgpSessionExists(peering, "1004");

    string content_b = FileRead(
        "controller/src/bgp/testdata/config_test_28b.xml");
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

    TASK_UTIL_EXPECT_EQ(1, config_manager_->config()->instances().size());

    TASK_UTIL_EXPECT_EQ(0, db_graph_.edge_count());
    TASK_UTIL_EXPECT_EQ(0, db_graph_.vertex_count());
}

// Add and delete new sessions after existing ones.
TEST_F(BgpIfmapConfigManagerTest, MasterPeeringUpdate2) {
    const BgpIfmapPeeringConfig *peering;
    char full_name[1024];
    snprintf(full_name, sizeof(full_name), "attr(%s:%s,%s:%s)",
        BgpConfigManager::kMasterInstance, "local",
        BgpConfigManager::kMasterInstance, "remote");

    string content_a = FileRead(
        "controller/src/bgp/testdata/config_test_29a.xml");
    EXPECT_TRUE(parser_.Parse(content_a));
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(FindPeeringConfig(full_name) != NULL);
    peering = FindPeeringConfig(full_name);
    TASK_UTIL_EXPECT_EQ(2, peering->size());
    VerifyBgpSessionExists(peering, "1001");
    VerifyBgpSessionExists(peering, "1002");

    string content_b = FileRead(
        "controller/src/bgp/testdata/config_test_29b.xml");
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

    TASK_UTIL_EXPECT_EQ(1, config_manager_->config()->instances().size());

    TASK_UTIL_EXPECT_EQ(0, db_graph_.edge_count());
    TASK_UTIL_EXPECT_EQ(0, db_graph_.vertex_count());
}

// Add and delete new sessions before existing ones.
TEST_F(BgpIfmapConfigManagerTest, MasterPeeringUpdate3) {
    const BgpIfmapPeeringConfig *peering;
    char full_name[1024];
    snprintf(full_name, sizeof(full_name), "attr(%s:%s,%s:%s)",
        BgpConfigManager::kMasterInstance, "local",
        BgpConfigManager::kMasterInstance, "remote");

    string content_a = FileRead(
        "controller/src/bgp/testdata/config_test_30a.xml");
    EXPECT_TRUE(parser_.Parse(content_a));
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(FindPeeringConfig(full_name) != NULL);
    peering = FindPeeringConfig(full_name);
    TASK_UTIL_EXPECT_EQ(2, peering->size());
    VerifyBgpSessionExists(peering, "1003");
    VerifyBgpSessionExists(peering, "1004");

    string content_b = FileRead(
        "controller/src/bgp/testdata/config_test_30b.xml");
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

    TASK_UTIL_EXPECT_EQ(1, config_manager_->config()->instances().size());

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

    for (size_t idx = 0; idx < session_mask.size(); ++idx) {
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

    for (size_t idx = 0; idx < session_mask.size(); ++idx) {
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
TEST_F(BgpIfmapConfigManagerTest, MasterPeeringUpdate4) {
    char full_name[1024];
    snprintf(full_name, sizeof(full_name), "attr(%s:%s,%s:%s)",
        BgpConfigManager::kMasterInstance, "local",
        BgpConfigManager::kMasterInstance, "remote");

    for (int idx = 1; idx <= 255; ++idx) {
        const BgpIfmapPeeringConfig *peering;

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

        TASK_UTIL_EXPECT_EQ(1, config_manager_->config()->instances().size());

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
TEST_F(BgpIfmapConfigManagerTest, MasterPeeringUpdate5) {
    char full_name[1024];
    snprintf(full_name, sizeof(full_name), "attr(%s:%s,%s:%s)",
        BgpConfigManager::kMasterInstance, "local",
        BgpConfigManager::kMasterInstance, "remote");

    for (int idx = 1; idx <= 254; ++idx) {
        const BgpIfmapPeeringConfig *peering;

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

        TASK_UTIL_EXPECT_EQ(1, config_manager_->config()->instances().size());

        TASK_UTIL_EXPECT_EQ(0, db_graph_.edge_count());
        TASK_UTIL_EXPECT_EQ(0, db_graph_.vertex_count());
    }
}

TEST_F(BgpIfmapConfigManagerTest, InstanceTargetExport1) {
    string content = FileRead("controller/src/bgp/testdata/config_test_7.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(2, config_manager_->config()->instances().size());
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

    TASK_UTIL_EXPECT_EQ(1, config_manager_->config()->instances().size());
    TASK_UTIL_EXPECT_EQ(0, db_graph_.vertex_count());
}

TEST_F(BgpIfmapConfigManagerTest, InstanceTargetExport2) {
    string content = FileRead("controller/src/bgp/testdata/config_test_8.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(2, config_manager_->config()->instances().size());
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

    TASK_UTIL_EXPECT_EQ(1, config_manager_->config()->instances().size());
    TASK_UTIL_EXPECT_EQ(0, db_graph_.vertex_count());
}

TEST_F(BgpIfmapConfigManagerTest, InstanceTargetImport1) {
    string content = FileRead("controller/src/bgp/testdata/config_test_9.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(2, config_manager_->config()->instances().size());
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

    TASK_UTIL_EXPECT_EQ(1, config_manager_->config()->instances().size());
    TASK_UTIL_EXPECT_EQ(0, db_graph_.vertex_count());
}

TEST_F(BgpIfmapConfigManagerTest, InstanceTargetImport2) {
    string content = FileRead("controller/src/bgp/testdata/config_test_10.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(2, config_manager_->config()->instances().size());
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

    TASK_UTIL_EXPECT_EQ(1, config_manager_->config()->instances().size());
    TASK_UTIL_EXPECT_EQ(0, db_graph_.vertex_count());
}

TEST_F(BgpIfmapConfigManagerTest, VirtualNetwork1) {
    string content = FileRead("controller/src/bgp/testdata/config_test_20.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(3, config_manager_->config()->instances().size());

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

    TASK_UTIL_EXPECT_EQ(1, config_manager_->config()->instances().size());
    TASK_UTIL_EXPECT_EQ(0, db_graph_.vertex_count());
}

TEST_F(BgpIfmapConfigManagerTest, VirtualNetwork2) {
    string content_a = FileRead(
        "controller/src/bgp/testdata/config_test_21a.xml");
    EXPECT_TRUE(parser_.Parse(content_a));
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(3, config_manager_->config()->instances().size());

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

    TASK_UTIL_EXPECT_EQ(1, config_manager_->config()->instances().size());
    TASK_UTIL_EXPECT_EQ(0, db_graph_.vertex_count());
}

TEST_F(BgpIfmapConfigManagerTest, VirtualNetwork3) {
    string content_a = FileRead(
        "controller/src/bgp/testdata/config_test_22a.xml");
    EXPECT_TRUE(parser_.Parse(content_a));
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(3, config_manager_->config()->instances().size());

    const BgpInstanceConfig *blue = FindInstanceConfig("blue");
    TASK_UTIL_ASSERT_TRUE(blue != NULL);
    TASK_UTIL_EXPECT_EQ("", blue->virtual_network());
    TASK_UTIL_EXPECT_EQ(0, blue->virtual_network_index());

    const BgpInstanceConfig *red = FindInstanceConfig("red");
    TASK_UTIL_ASSERT_TRUE(red != NULL);
    TASK_UTIL_EXPECT_EQ("", red->virtual_network());
    TASK_UTIL_EXPECT_EQ(0, red->virtual_network_index());

    string content_b = FileRead(
        "controller/src/bgp/testdata/config_test_22b.xml");
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

    TASK_UTIL_EXPECT_EQ(1, config_manager_->config()->instances().size());
    TASK_UTIL_EXPECT_EQ(0, db_graph_.vertex_count());
}

// Config parser sets network-id using virtual-network-properties property.
// Override network-id using virtual-network-id property and verify.
TEST_F(BgpIfmapConfigManagerTest, VirtualNetwork4) {
    string content = FileRead("controller/src/bgp/testdata/config_test_20.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(3, config_manager_->config()->instances().size());

    const BgpInstanceConfig *blue = FindInstanceConfig("blue");
    TASK_UTIL_ASSERT_TRUE(blue != NULL);
    TASK_UTIL_EXPECT_EQ("blue-vn", blue->virtual_network());
    TASK_UTIL_EXPECT_EQ(101, blue->virtual_network_index());
    autogen::VirtualNetwork::NtProperty *blue_vni =
        new autogen::VirtualNetwork::NtProperty;
    blue_vni->data = 201;
    ifmap_test_util::IFMapMsgPropertyAdd(&db_, "virtual-network", "blue-vn",
        "virtual-network-network-id", blue_vni);
    TASK_UTIL_EXPECT_EQ(201, blue->virtual_network_index());
    ifmap_test_util::IFMapMsgPropertyDelete(&db_, "virtual-network", "blue-vn",
        "virtual-network-network-id");
    TASK_UTIL_EXPECT_EQ(101, blue->virtual_network_index());

    const BgpInstanceConfig *red = FindInstanceConfig("red");
    TASK_UTIL_ASSERT_TRUE(red != NULL);
    TASK_UTIL_EXPECT_EQ("red-vn", red->virtual_network());
    TASK_UTIL_EXPECT_EQ(102, red->virtual_network_index());
    autogen::VirtualNetwork::NtProperty *red_vni =
        new autogen::VirtualNetwork::NtProperty;
    red_vni->data = 202;
    ifmap_test_util::IFMapMsgPropertyAdd(&db_, "virtual-network", "red-vn",
        "virtual-network-network-id", red_vni);
    TASK_UTIL_EXPECT_EQ(202, red->virtual_network_index());
    ifmap_test_util::IFMapMsgPropertyDelete(&db_, "virtual-network", "red-vn",
        "virtual-network-network-id");
    TASK_UTIL_EXPECT_EQ(102, red->virtual_network_index());

    boost::replace_all(content, "<config>", "<delete>");
    boost::replace_all(content, "</config>", "</delete>");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(1, config_manager_->config()->instances().size());
    TASK_UTIL_EXPECT_EQ(0, db_graph_.vertex_count());
}

TEST_F(BgpIfmapConfigManagerTest, Instances) {
    string content = FileRead("controller/src/bgp/testdata/config_test_2.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(4, GetInstanceCount());

    boost::replace_all(content, "<config>", "<delete>");
    boost::replace_all(content, "</config>", "</delete>");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, config_manager_->config()->instances().size());
    TASK_UTIL_EXPECT_EQ(0, db_graph_.vertex_count());
}

TEST_F(BgpIfmapConfigManagerTest, InstanceNeighbors) {
    string content = FileRead("controller/src/bgp/testdata/config_test_3.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    const BgpInstanceConfig *rti = FindInstanceConfig("test");
    ASSERT_TRUE(rti);
    EXPECT_EQ(1, config_manager_->NeighborCount("test"));

    const BgpNeighborConfig *peer = NULL;
    typedef std::pair<std::string, const BgpNeighborConfig *> iter_t;
    BOOST_FOREACH(iter_t iter, config_manager_->NeighborMapItems("test")) {
        if (iter.first.find("test:ce") == 0) {
            peer = iter.second;
            break;
        }
    }
    ASSERT_TRUE(peer);
    TASK_UTIL_EXPECT_EQ(2, peer->peer_as());

    string update = FileRead("controller/src/bgp/testdata/config_test_4.xml");
    EXPECT_TRUE(parser_.Parse(update));
    task_util::WaitForIdle();
    EXPECT_EQ(2, config_manager_->NeighborCount("test"));

    typedef std::pair<std::string, const BgpNeighborConfig *> iter_t;
    BOOST_FOREACH(iter_t iter, config_manager_->NeighborMapItems("test")) {
        EXPECT_EQ(3, iter.second->peer_as());
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
    EXPECT_EQ(1, config_manager_->NeighborCount("test"));

    boost::replace_all(content, "<config>", "<delete>");
    boost::replace_all(content, "</config>", "</delete>");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, config_manager_->config()->instances().size());
    TASK_UTIL_EXPECT_EQ(0, db_graph_.vertex_count());
}

TEST_F(BgpIfmapConfigManagerTest, BGPaaSNeighbors1) {
    string content;
    content = FileRead("controller/src/bgp/testdata/config_test_36a.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    const BgpInstanceConfig *rti = FindInstanceConfig("test");
    EXPECT_TRUE(rti != NULL);
    EXPECT_EQ(2, config_manager_->NeighborCount("test"));

    const BgpNeighborConfig *nbr_config1;
    nbr_config1 = config_manager_->FindNeighbor("test", "test:vm1:0");
    EXPECT_TRUE(nbr_config1 != NULL);
    EXPECT_EQ("bgpaas-client", nbr_config1->router_type());
    EXPECT_EQ(64512, nbr_config1->local_as());
    EXPECT_EQ("192.168.1.1", nbr_config1->local_identifier_string());
    EXPECT_EQ(65001, nbr_config1->peer_as());
    EXPECT_EQ("10.0.0.1", nbr_config1->peer_address_string());
    EXPECT_EQ(1024, nbr_config1->source_port());

    const BgpNeighborConfig *nbr_config2;
    nbr_config2 = config_manager_->FindNeighbor("test", "test:vm2:0");
    EXPECT_TRUE(nbr_config2 != NULL);
    EXPECT_EQ("bgpaas-client", nbr_config2->router_type());
    EXPECT_EQ(64512, nbr_config2->local_as());
    EXPECT_EQ("192.168.1.1", nbr_config2->local_identifier_string());
    EXPECT_EQ(65002, nbr_config2->peer_as());
    EXPECT_EQ("10.0.0.2", nbr_config2->peer_address_string());
    EXPECT_EQ(1025, nbr_config2->source_port());

    // Change asn and identifier for master.
    content = FileRead("controller/src/bgp/testdata/config_test_36b.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    // Verify that instance neighbors use the new values.
    nbr_config1 = config_manager_->FindNeighbor("test", "test:vm1:0");
    EXPECT_TRUE(nbr_config1 != NULL);
    EXPECT_EQ(64513, nbr_config1->local_as());
    EXPECT_EQ("192.168.1.2", nbr_config1->local_identifier_string());
    nbr_config2 = config_manager_->FindNeighbor("test", "test:vm2:0");
    EXPECT_TRUE(nbr_config2 != NULL);
    EXPECT_EQ(64513, nbr_config2->local_as());
    EXPECT_EQ("192.168.1.2", nbr_config1->local_identifier_string());

    // Cleanup.
    content = FileRead("controller/src/bgp/testdata/config_test_36a.xml");
    boost::replace_all(content, "<config>", "<delete>");
    boost::replace_all(content, "</config>", "</delete>");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, config_manager_->config()->instances().size());
    TASK_UTIL_EXPECT_EQ(0, config_manager_->NeighborCount("test"));
    TASK_UTIL_EXPECT_EQ(0, db_graph_.vertex_count());
}

TEST_F(BgpIfmapConfigManagerTest, BGPaaSNeighbors2) {
    string content;
    content = FileRead("controller/src/bgp/testdata/config_test_36a.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    const BgpInstanceConfig *rti = FindInstanceConfig("test");
    EXPECT_TRUE(rti != NULL);
    EXPECT_EQ(2, config_manager_->NeighborCount("test"));

    // Verify that the port is set for test:vm1.
    const BgpNeighborConfig *nbr_config1;
    nbr_config1 = config_manager_->FindNeighbor("test", "test:vm1:0");
    EXPECT_EQ(1024, nbr_config1->source_port());

    // Verify that the port is set for test:vm2.
    const BgpNeighborConfig *nbr_config2;
    nbr_config2 = config_manager_->FindNeighbor("test", "test:vm2:0");
    EXPECT_EQ(1025, nbr_config2->source_port());

    // Update port numbers.
    content = FileRead("controller/src/bgp/testdata/config_test_36d.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    // Verify that the port is updated for test:vm1.
    nbr_config1 = config_manager_->FindNeighbor("test", "test:vm1:0");
    EXPECT_EQ(1025, nbr_config1->source_port());

    // Verify that the port is updated for test:vm2.
    nbr_config2 = config_manager_->FindNeighbor("test", "test:vm2:0");
    EXPECT_EQ(1024, nbr_config2->source_port());

    // Cleanup.
    boost::replace_all(content, "<config>", "<delete>");
    boost::replace_all(content, "</config>", "</delete>");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, config_manager_->config()->instances().size());
    TASK_UTIL_EXPECT_EQ(0, config_manager_->NeighborCount("test"));
    TASK_UTIL_EXPECT_EQ(0, db_graph_.vertex_count());
}

TEST_F(BgpIfmapConfigManagerShowTest, ShowInstances1) {
    string content = FileRead(
        "controller/src/bgp/testdata/config_test_26a.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(4, ConfigInstanceCount(config_manager_));

    BgpSandeshContext sandesh_context;
    sandesh_context.bgp_server = &server_;
    Sandesh::set_client_context(&sandesh_context);

    const char *instance_name_list[] = {
        "blue", "green", "red", BgpConfigManager::kMasterInstance
    };
    vector<ShowBgpInstanceConfig> instance_list;
    BOOST_FOREACH(const char *instance_name, instance_name_list) {
        const BgpInstanceConfig *config =
                config_manager_->FindInstance(instance_name);
        ASSERT_TRUE(config != NULL);
        ShowBgpInstanceConfig instance;
        instance.set_name(config->name());
        instance.set_virtual_network(config->virtual_network());
        instance.set_virtual_network_index(config->virtual_network_index());
        instance_list.push_back(instance);
    }

    bool validate_done = false;
    Sandesh::set_response_callback(
        boost::bind(ValidateShowInstanceResponse, _1, &validate_done,
                    instance_list));

    ShowBgpInstanceConfigReq *show_req = new ShowBgpInstanceConfigReq;
    show_req->HandleRequest();
    show_req->Release();
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(validate_done);

    // Match "".
    validate_done = false;
    Sandesh::set_response_callback(
        boost::bind(ValidateShowInstanceResponse, _1, &validate_done,
                    instance_list));
    show_req = new ShowBgpInstanceConfigReq;
    show_req->set_search_string("");
    show_req->HandleRequest();
    show_req->Release();
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(validate_done);

    // Match "e".
    validate_done = false;
    Sandesh::set_response_callback(
        boost::bind(ValidateShowInstanceResponse, _1, &validate_done,
                    instance_list));
    show_req = new ShowBgpInstanceConfigReq;
    show_req->set_search_string("e");
    show_req->HandleRequest();
    show_req->Release();
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(validate_done);

    // Match "green".
    // Set instance list to contain just green.
    validate_done = false;
    instance_list[0] = instance_list[1];
    instance_list.resize(1);
    Sandesh::set_response_callback(
        boost::bind(ValidateShowInstanceResponse, _1, &validate_done,
                    instance_list));
    show_req = new ShowBgpInstanceConfigReq;
    show_req->set_search_string("green");
    show_req->HandleRequest();
    show_req->Release();
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(validate_done);

    boost::replace_all(content, "<config>", "<delete>");
    boost::replace_all(content, "</config>", "</delete>");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, config_manager_->config()->instances().size());
    TASK_UTIL_EXPECT_EQ(0, db_graph_.vertex_count());
}

TEST_F(BgpIfmapConfigManagerShowTest, ShowInstances2) {
    string content = FileRead(
        "controller/src/bgp/testdata/config_test_26b.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(3, ConfigInstanceCount(config_manager_));

    BgpSandeshContext sandesh_context;
    sandesh_context.bgp_server = &server_;
    Sandesh::set_client_context(&sandesh_context);

    const char *instance_name_list[] = {
        "blue-to-red", "red-to-blue", BgpConfigManager::kMasterInstance
    };
    vector<ShowBgpInstanceConfig> instance_list;
    BOOST_FOREACH(const char *instance_name, instance_name_list) {
        const BgpInstanceConfig *config =
                config_manager_->FindInstance(instance_name);
        ASSERT_TRUE(config != NULL);
        ShowBgpInstanceConfig instance;
        instance.set_name(config->name());
        instance.set_virtual_network(config->virtual_network());
        instance.set_virtual_network_index(config->virtual_network_index());
        instance_list.push_back(instance);
    }

    bool validate_done = false;
    Sandesh::set_response_callback(
        boost::bind(ValidateShowInstanceResponse, _1, &validate_done,
                    instance_list));

    ShowBgpInstanceConfigReq *show_req = new ShowBgpInstanceConfigReq;
    show_req->HandleRequest();
    show_req->Release();
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(validate_done);

    boost::replace_all(content, "<config>", "<delete>");
    boost::replace_all(content, "</config>", "</delete>");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, config_manager_->config()->instances().size());
    TASK_UTIL_EXPECT_EQ(0, db_graph_.vertex_count());
}

TEST_F(BgpIfmapConfigManagerShowTest, ShowInstances3) {
    string content = FileRead(
        "controller/src/bgp/testdata/config_test_26c.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(2, ConfigInstanceCount(config_manager_));

    BgpSandeshContext sandesh_context;
    sandesh_context.bgp_server = &server_;
    Sandesh::set_client_context(&sandesh_context);

    const char *instance_name_list[] = {
        "blue-to-nat", BgpConfigManager::kMasterInstance
    };
    vector<ShowBgpInstanceConfig> instance_list;
    BOOST_FOREACH(const char *instance_name, instance_name_list) {
        const BgpInstanceConfig *config =
                config_manager_->FindInstance(instance_name);
        ASSERT_TRUE(config != NULL);
        ShowBgpInstanceConfig instance;
        instance.set_name(config->name());
        instance.set_virtual_network(config->virtual_network());
        instance.set_virtual_network_index(config->virtual_network_index());
        instance_list.push_back(instance);
    }

    bool validate_done = false;
    Sandesh::set_response_callback(
        boost::bind(ValidateShowInstanceResponse, _1, &validate_done,
                    instance_list));

    ShowBgpInstanceConfigReq *show_req = new ShowBgpInstanceConfigReq;
    show_req->HandleRequest();
    show_req->Release();
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(validate_done);

    boost::replace_all(content, "<config>", "<delete>");
    boost::replace_all(content, "</config>", "</delete>");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, config_manager_->config()->instances().size());
    TASK_UTIL_EXPECT_EQ(0, db_graph_.vertex_count());
}

TEST_F(BgpIfmapConfigManagerShowTest, ShowNeighbors) {
    string content = FileRead("controller/src/bgp/testdata/config_test_27.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(4, ConfigInstanceCount(config_manager_));

    BgpSandeshContext sandesh_context;
    sandesh_context.bgp_server = &server_;
    Sandesh::set_client_context(&sandesh_context);

    const char *neighbor_name_list[] = { "remote1", "remote2", "remote3" };
    vector<ShowBgpNeighborConfig> neighbor_list;
    BOOST_FOREACH(const char *neighbor_name, neighbor_name_list) {
        string full_name(BgpConfigManager::kMasterInstance);
        full_name += ":";
        full_name += neighbor_name;
        const BgpNeighborConfig *config =
                config_manager_->FindNeighbor(
                    BgpConfigManager::kMasterInstance, full_name);
        ASSERT_TRUE(config != NULL);
        ShowBgpNeighborConfig neighbor;
        neighbor.set_name(config->name());
        neighbor.set_instance_name(BgpConfigManager::kMasterInstance);
        neighbor.set_autonomous_system(config->peer_as());
        neighbor.set_identifier(
            BgpIdentifierToString(config->peer_identifier()));
        neighbor.set_address(config->peer_address().to_string());
        neighbor_list.push_back(neighbor);
    }

    bool validate_done = false;
    Sandesh::set_response_callback(
        boost::bind(ValidateShowNeighborResponse, _1, &validate_done,
                    neighbor_list));

    ShowBgpNeighborConfigReq *show_req = new ShowBgpNeighborConfigReq;
    show_req->HandleRequest();
    show_req->Release();
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(validate_done);

    boost::replace_all(content, "<config>", "<delete>");
    boost::replace_all(content, "</config>", "</delete>");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, config_manager_->config()->instances().size());
    TASK_UTIL_EXPECT_EQ(0, db_graph_.vertex_count());
}

TEST_F(BgpIfmapConfigManagerShowTest, ShowBGPaaSNeighbors) {
    string content = FileRead("controller/src/bgp/testdata/config_test_36a.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    BgpSandeshContext sandesh_context;
    sandesh_context.bgp_server = &server_;
    Sandesh::set_client_context(&sandesh_context);

    const char *neighbor_name_list[] = { "vm1:0", "vm2:0" };
    vector<ShowBgpNeighborConfig> neighbor_list;
    BOOST_FOREACH(const char *neighbor_name, neighbor_name_list) {
        string full_name("test");
        full_name += ":";
        full_name += neighbor_name;
        const BgpNeighborConfig *config =
            config_manager_->FindNeighbor("test", full_name);
        ASSERT_TRUE(config != NULL);
        ShowBgpNeighborConfig neighbor;
        neighbor.set_name(config->name());
        neighbor.set_instance_name("test");
        neighbor.set_autonomous_system(config->peer_as());
        neighbor.set_identifier(config->peer_identifier_string());
        neighbor.set_address(config->peer_address_string());
        neighbor_list.push_back(neighbor);
    }

    bool validate_done = false;
    Sandesh::set_response_callback(
        boost::bind(ValidateShowNeighborResponse, _1, &validate_done,
                    neighbor_list));

    ShowBgpNeighborConfigReq *show_req = new ShowBgpNeighborConfigReq;
    show_req->HandleRequest();
    show_req->Release();
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(validate_done);

    boost::replace_all(content, "<config>", "<delete>");
    boost::replace_all(content, "</config>", "</delete>");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, config_manager_->config()->instances().size());
    TASK_UTIL_EXPECT_EQ(0, db_graph_.vertex_count());
}

TEST_F(BgpIfmapConfigManagerShowTest, ShowPeerings1) {
    string content = FileRead("controller/src/bgp/testdata/config_test_27.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(3, config_manager_->config()->PeeringCount());

    BgpSandeshContext sandesh_context;
    RegisterSandeshShowIfmapHandlers(&sandesh_context);
    sandesh_context.bgp_server = &server_;
    Sandesh::set_client_context(&sandesh_context);

    const char *peering_name_list[] = { "remote1", "remote2", "remote3" };
    vector<ShowBgpPeeringConfig> peering_list;
    BOOST_FOREACH(const char *peering_name, peering_name_list) {
        char full_name[1024];
        snprintf(full_name, sizeof(full_name), "attr(%s:%s,%s:%s)",
            BgpConfigManager::kMasterInstance, "local",
            BgpConfigManager::kMasterInstance, peering_name);
        const BgpIfmapPeeringConfig *config = FindPeeringConfig(full_name);
        ASSERT_TRUE(config != NULL);
        ShowBgpPeeringConfig peering;
        peering.set_name(config->name());
        peering.set_instance_name(BgpConfigManager::kMasterInstance);
        peering.set_neighbor_count(config->size());
        peering_list.push_back(peering);
    }

    bool validate_done = false;
    Sandesh::set_response_callback(
        boost::bind(ValidateShowPeeringResponse, _1, &validate_done,
                    peering_list));

    ShowBgpPeeringConfigReq *show_req = new ShowBgpPeeringConfigReq;
    show_req->HandleRequest();
    show_req->Release();
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(validate_done);

    boost::replace_all(content, "<config>", "<delete>");
    boost::replace_all(content, "</config>", "</delete>");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, config_manager_->config()->instances().size());
    TASK_UTIL_EXPECT_EQ(0, db_graph_.vertex_count());
}

TEST_F(BgpIfmapConfigManagerShowTest, ShowPeerings2) {
    char full_name[1024];
    snprintf(full_name, sizeof(full_name), "attr(%s:%s,%s:%s)",
        BgpConfigManager::kMasterInstance, "local",
        BgpConfigManager::kMasterInstance, "remote");

    bitset<8> session_mask(7);
    string content = GeneratePeeringConfig(session_mask);
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, config_manager_->config()->PeeringCount());
    TASK_UTIL_EXPECT_TRUE(FindPeeringConfig(full_name) != NULL);
    const BgpIfmapPeeringConfig *config = FindPeeringConfig(full_name);
    TASK_UTIL_EXPECT_EQ(session_mask.count(), config->size());
    VerifyBgpSessions(config, session_mask);

    BgpSandeshContext sandesh_context;
    RegisterSandeshShowIfmapHandlers(&sandesh_context);
    sandesh_context.bgp_server = &server_;
    Sandesh::set_client_context(&sandesh_context);

    vector<ShowBgpPeeringConfig> peering_list;
    ShowBgpPeeringConfig peering;
    peering.set_name(config->name());
    peering.set_instance_name(BgpConfigManager::kMasterInstance);
    peering.set_neighbor_count(3);
    peering_list.push_back(peering);

    bool validate_done = false;
    Sandesh::set_response_callback(
        boost::bind(ValidateShowPeeringResponse, _1, &validate_done,
                    peering_list));

    ShowBgpPeeringConfigReq *show_req = new ShowBgpPeeringConfigReq;
    show_req->HandleRequest();
    show_req->Release();
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(validate_done);

    boost::replace_all(content, "<config>", "<delete>");
    boost::replace_all(content, "</config>", "</delete>");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, config_manager_->config()->instances().size());
    TASK_UTIL_EXPECT_EQ(0, db_graph_.vertex_count());
}

TEST_F(BgpIfmapConfigManagerShowTest, ShowBGPaaSPeerings) {
    string content;
    content = FileRead("controller/src/bgp/testdata/config_test_36a.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(2, config_manager_->config()->PeeringCount());
    vector<ShowBgpPeeringConfig> peering_list;
    for (int idx = 1; idx <= 2; ++idx) {
        char full_name[1024];
        snprintf(full_name, sizeof(full_name), "attr(%s:%s,%s:%s%d)",
            "test", "bgpaas-server", "test", "vm", idx);
        TASK_UTIL_EXPECT_TRUE(FindPeeringConfig(full_name) != NULL);
        const BgpIfmapPeeringConfig *config = FindPeeringConfig(full_name);
        TASK_UTIL_EXPECT_EQ(1, config->size());

        ShowBgpPeeringConfig peering;
        peering.set_name(config->name());
        peering.set_instance_name("test");
        peering.set_neighbor_count(1);
        peering_list.push_back(peering);
    }

    BgpSandeshContext sandesh_context;
    RegisterSandeshShowIfmapHandlers(&sandesh_context);
    sandesh_context.bgp_server = &server_;
    Sandesh::set_client_context(&sandesh_context);

    bool validate_done = false;
    Sandesh::set_response_callback(
        boost::bind(ValidateShowPeeringResponse, _1, &validate_done,
                    peering_list));

    ShowBgpPeeringConfigReq *show_req = new ShowBgpPeeringConfigReq;
    show_req->HandleRequest();
    show_req->Release();
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(validate_done);

    boost::replace_all(content, "<config>", "<delete>");
    boost::replace_all(content, "</config>", "</delete>");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, config_manager_->config()->instances().size());
    TASK_UTIL_EXPECT_EQ(0, db_graph_.vertex_count());
}

TEST_F(BgpIfmapConfigManagerTest, AddBgpPeeringBeforeInstanceBgpRouterLink1) {
    // Add client bgp-router with parameters.
    string bgp_router_id1 = string("test") + ":client";
    autogen::BgpRouterParams *params1 = new autogen::BgpRouterParams;
    params1->Clear();
    params1->autonomous_system = 100;
    params1->identifier = "10.1.1.100";
    params1->address = "127.0.0.100";
    params1->router_type = "bgpaas-client";
    ifmap_test_util::IFMapMsgPropertyAdd(&db_, "bgp-router", bgp_router_id1,
        "bgp-router-parameters", params1);
    task_util::WaitForIdle();

    // Add server bgp-router with parameters.
    string bgp_router_id2 = string("test") + ":server";
    autogen::BgpRouterParams *params2 = new autogen::BgpRouterParams;
    params2->Clear();
    params2->autonomous_system = 100;
    params2->router_type = "bgpaas-server";
    ifmap_test_util::IFMapMsgPropertyAdd(&db_, "bgp-router", bgp_router_id2,
        "bgp-router-parameters", params2);
    task_util::WaitForIdle();

    // Now add a bgp-peering link between the bgp-routers.
    // Verify that's there's no peering config created as there's no parent
    // routing instance.
    string peering = "attr(" + bgp_router_id1 + "," + bgp_router_id2 + ")";
    ifmap_test_util::IFMapMsgLink(&db_,
        "bgp-router", bgp_router_id1, "bgp-router", bgp_router_id2,
        "bgp-peering", 0, new autogen::BgpPeeringAttributes());
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(0, GetPeeringCount());
    TASK_UTIL_EXPECT_TRUE(FindPeeringConfig(peering) == NULL);

    // Now add a link between the bgp-routers and the routing-instance.
    // Verify that's the peering config gets created due to notification
    // of the instance-bgp-router links.
    ifmap_test_util::IFMapMsgLink(&db_, "routing-instance", "test",
        "bgp-router", bgp_router_id1, "instance-bgp-router");
    ifmap_test_util::IFMapMsgLink(&db_, "routing-instance", "test",
        "bgp-router", bgp_router_id2, "instance-bgp-router");
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, GetPeeringCount());
    TASK_UTIL_EXPECT_TRUE(FindPeeringConfig(peering) != NULL);
}

TEST_F(BgpIfmapConfigManagerTest, AddBgpPeeringBeforeInstanceBgpRouterLink2) {
    // Add client bgp-router with parameters.
    string bgp_router_id1 = string("test") + ":client";
    autogen::BgpRouterParams *params1 = new autogen::BgpRouterParams;
    params1->Clear();
    params1->autonomous_system = 100;
    params1->identifier = "10.1.1.100";
    params1->address = "127.0.0.100";
    params1->router_type = "bgpaas-client";
    ifmap_test_util::IFMapMsgPropertyAdd(&db_, "bgp-router", bgp_router_id1,
        "bgp-router-parameters", params1);
    task_util::WaitForIdle();

    // Add server bgp-router with parameters.
    string bgp_router_id2 = string("test") + ":server";
    autogen::BgpRouterParams *params2 = new autogen::BgpRouterParams;
    params2->Clear();
    params2->autonomous_system = 100;
    params2->router_type = "bgpaas-server";
    ifmap_test_util::IFMapMsgPropertyAdd(&db_, "bgp-router", bgp_router_id2,
        "bgp-router-parameters", params2);
    task_util::WaitForIdle();

    // Now add a bgp-peering link between the bgp-routers.
    // Verify that's there's no peering config created as there's no parent
    // routing instance.
    string peering = "attr(" + bgp_router_id1 + "," + bgp_router_id2 + ")";
    ifmap_test_util::IFMapMsgLink(&db_,
        "bgp-router", bgp_router_id1, "bgp-router", bgp_router_id2,
        "bgp-peering", 0, new autogen::BgpPeeringAttributes());
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(0, GetPeeringCount());
    TASK_UTIL_EXPECT_TRUE(FindPeeringConfig(peering) == NULL);

    // Now add the parent routing-instance.
    // Verify that's there's no peering config created sine there's no
    // notification to re-evaluate the bgp-peering.
    ifmap_test_util::IFMapMsgNodeAdd(&db_, "routing-instance", "test");
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(0, GetPeeringCount());
    TASK_UTIL_EXPECT_TRUE(FindPeeringConfig(peering) == NULL);

    // Now add a link between the bgp-routers and the routing-instance.
    // Verify that's the peering config gets created due to notification
    // of the instance-bgp-router links.
    ifmap_test_util::IFMapMsgLink(&db_, "routing-instance", "test",
        "bgp-router", bgp_router_id1, "instance-bgp-router");
    ifmap_test_util::IFMapMsgLink(&db_, "routing-instance", "test",
        "bgp-router", bgp_router_id2, "instance-bgp-router");
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, GetPeeringCount());
    TASK_UTIL_EXPECT_TRUE(FindPeeringConfig(peering) != NULL);
}

TEST_F(BgpIfmapConfigManagerTest, AddBgpRouterBeforeParentLink) {
    string bgp_router_id = string(BgpConfigManager::kMasterInstance) + ":local";
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
    const BgpProtocolConfig *protocol =
            config_manager_->GetProtocolConfig(
                BgpConfigManager::kMasterInstance);
    TASK_UTIL_EXPECT_EQ(100, protocol->autonomous_system());
    TASK_UTIL_EXPECT_EQ("10.1.1.100",
                        BgpIdentifierToString(protocol->identifier()));
    //TASK_UTIL_EXPECT_EQ("127.0.0.100", protocol->address());
}

TEST_F(BgpIfmapConfigManagerTest, RemoveParentLinkBeforeBgpRouter) {
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
    const BgpProtocolConfig *protocol =
            config_manager_->GetProtocolConfig(
                BgpConfigManager::kMasterInstance);
    TASK_UTIL_EXPECT_EQ(100, protocol->autonomous_system());
    TASK_UTIL_EXPECT_EQ("10.1.1.100",
                        BgpIdentifierToString(protocol->identifier()));
    //TASK_UTIL_EXPECT_EQ("127.0.0.100", router_params.address);
}

//
// Add and delete routing policy config
// Validate the BgpRoutingPolicyConfig
//
TEST_F(BgpIfmapConfigManagerTest, RoutingPolicyAddDelete) {
    string content_a = FileRead("controller/src/bgp/testdata/routing_policy_0a.xml");
    EXPECT_TRUE(parser_.Parse(content_a));
    task_util::WaitForIdle();

    const BgpRoutingPolicyConfig *policy_cfg =
            config_manager_->FindRoutingPolicy("basic");
    ASSERT_TRUE(policy_cfg != NULL);

    ASSERT_EQ(policy_cfg->terms().size(), 1);
    boost::replace_all(content_a, "<config>", "<delete>");
    boost::replace_all(content_a, "</config>", "</delete>");
    EXPECT_TRUE(parser_.Parse(content_a));
    task_util::WaitForIdle();
}

//
// Update routing policy config
// Validate the BgpRoutingPolicyConfig
//
TEST_F(BgpIfmapConfigManagerTest, RoutingPolicyUpdate) {
    string content_a = FileRead("controller/src/bgp/testdata/routing_policy_0a.xml");
    EXPECT_TRUE(parser_.Parse(content_a));
    task_util::WaitForIdle();

    const BgpRoutingPolicyConfig *policy_cfg =
            config_manager_->FindRoutingPolicy("basic");
    ASSERT_TRUE(policy_cfg != NULL);
    ASSERT_EQ(policy_cfg->terms().size(), 1);

    // Update the routing policy with additional term
    string content_b = FileRead("controller/src/bgp/testdata/routing_policy_0b.xml");
    EXPECT_TRUE(parser_.Parse(content_b));
    task_util::WaitForIdle();
    policy_cfg = config_manager_->FindRoutingPolicy("basic");
    ASSERT_TRUE(policy_cfg != NULL);
    ASSERT_EQ(policy_cfg->terms().size(), 2);

    boost::replace_all(content_a, "<config>", "<delete>");
    boost::replace_all(content_a, "</config>", "</delete>");
    EXPECT_TRUE(parser_.Parse(content_a));
    task_util::WaitForIdle();
}

//
// Create routing instance and routing policy
// Associate routing-policy to routing-instance
// Validate the routing instance and associate policy in BgpInstanceConfig
// Add one more routing policy link to routing-instance
// Validate the routing instance and associate policy in BgpInstanceConfig
//
TEST_F(BgpIfmapConfigManagerTest, RoutingInstanceRoutingPolicy_0) {
    string content_a = FileRead("controller/src/bgp/testdata/routing_policy_3.xml");
    EXPECT_TRUE(parser_.Parse(content_a));
    task_util::WaitForIdle();

    const BgpRoutingPolicyConfig *policy_cfg =
            config_manager_->FindRoutingPolicy("basic_0");
    ASSERT_TRUE(policy_cfg != NULL);

    const BgpInstanceConfig *test_ri = FindInstanceConfig("test");
    ASSERT_TRUE(test_ri != NULL);
    ASSERT_TRUE(test_ri->routing_policy_list().size() == 1);
    ASSERT_TRUE(test_ri->routing_policy_list().front().routing_policy_ == "basic_0");
    ASSERT_TRUE(test_ri->routing_policy_list().front().sequence_ == "1.0");

    // Update the routing instance with two route policy
    string content_b = FileRead("controller/src/bgp/testdata/routing_policy_3d.xml");
    EXPECT_TRUE(parser_.Parse(content_b));
    task_util::WaitForIdle();
    policy_cfg = config_manager_->FindRoutingPolicy("basic_0");
    ASSERT_TRUE(policy_cfg != NULL);
    policy_cfg = config_manager_->FindRoutingPolicy("basic_1");
    ASSERT_TRUE(policy_cfg != NULL);

    test_ri = FindInstanceConfig("test");
    ASSERT_TRUE(test_ri != NULL);
    ASSERT_TRUE(test_ri->routing_policy_list().size() == 2);
    RoutingPolicyConfigList list = test_ri->routing_policy_list();

    vector<string> expect_list = list_of("basic_0")("basic_1");
    vector<string> current_list;
    BOOST_FOREACH(RoutingPolicyAttachInfo info, list) {
        current_list.push_back(info.routing_policy_);
    }

    ASSERT_TRUE(current_list.size() == expect_list.size());
    ASSERT_TRUE(std::equal(expect_list.begin(), expect_list.end(),
                           current_list.begin()));


    boost::replace_all(content_b, "<config>", "<delete>");
    boost::replace_all(content_b, "</config>", "</delete>");
    EXPECT_TRUE(parser_.Parse(content_b));
    task_util::WaitForIdle();
    test_ri = FindInstanceConfig("test");
    ASSERT_TRUE(test_ri == NULL);
    policy_cfg = config_manager_->FindRoutingPolicy("basic_0");
    ASSERT_TRUE(policy_cfg == NULL);
    policy_cfg = config_manager_->FindRoutingPolicy("basic_1");
    ASSERT_TRUE(policy_cfg == NULL);
}

//
// Create routing instance and two routing policies
// Associate routing-policies to routing-instance
// Validate the routing instance and associate policy in BgpInstanceConfig
// Update the sequence of routing policies on routing instance
// Validate the routing instance and associate policy in BgpInstanceConfig
//
TEST_F(BgpIfmapConfigManagerTest, RoutingInstanceRoutingPolicy_1) {
    string content_a = FileRead("controller/src/bgp/testdata/routing_policy_4.xml");
    EXPECT_TRUE(parser_.Parse(content_a));
    task_util::WaitForIdle();

    const BgpInstanceConfig *test_ri = FindInstanceConfig("test");
    ASSERT_TRUE(test_ri != NULL);

    const BgpRoutingPolicyConfig *policy_cfg =
            config_manager_->FindRoutingPolicy("basic_0");
    ASSERT_TRUE(policy_cfg != NULL);
    policy_cfg = config_manager_->FindRoutingPolicy("basic_1");
    ASSERT_TRUE(policy_cfg != NULL);

    ASSERT_TRUE(test_ri->routing_policy_list().size() == 2);
    RoutingPolicyConfigList list = test_ri->routing_policy_list();

    vector<string> expect_list = list_of("basic_0")("basic_1");
    vector<string> current_list;
    BOOST_FOREACH(RoutingPolicyAttachInfo info, list) {
        current_list.push_back(info.routing_policy_);
    }

    ASSERT_TRUE(current_list.size() == expect_list.size());
    ASSERT_TRUE(std::equal(expect_list.begin(), expect_list.end(),
                           current_list.begin()));


    // Update the routing instance to update the order of routing policy
    string content_b = FileRead("controller/src/bgp/testdata/routing_policy_4a.xml");
    EXPECT_TRUE(parser_.Parse(content_b));
    task_util::WaitForIdle();
    test_ri = FindInstanceConfig("test");
    ASSERT_TRUE(test_ri != NULL);
    ASSERT_TRUE(test_ri->routing_policy_list().size() == 2);
    list = test_ri->routing_policy_list();

    expect_list = list_of("basic_1")("basic_0");
    current_list.clear();
    BOOST_FOREACH(RoutingPolicyAttachInfo info, list) {
        current_list.push_back(info.routing_policy_);
    }

    ASSERT_TRUE(current_list.size() == expect_list.size());
    ASSERT_TRUE(std::equal(expect_list.begin(), expect_list.end(),
                           current_list.begin()));

    boost::replace_all(content_b, "<config>", "<delete>");
    boost::replace_all(content_b, "</config>", "</delete>");
    EXPECT_TRUE(parser_.Parse(content_b));
    task_util::WaitForIdle();
    test_ri = FindInstanceConfig("test");
    ASSERT_TRUE(test_ri == NULL);
    policy_cfg = config_manager_->FindRoutingPolicy("basic_0");
    ASSERT_TRUE(policy_cfg == NULL);
    policy_cfg = config_manager_->FindRoutingPolicy("basic_1");
    ASSERT_TRUE(policy_cfg == NULL);
}

//
// Create routing instance and routing policy
// Associate routing-policy to routing-instance
// Validate the routing instance and associate policy in BgpInstanceConfig
// Remove the routing policy from routing instance
// Validate that routing instance is not associated any routing instance
//
TEST_F(BgpIfmapConfigManagerTest, RoutingInstanceRoutingPolicy_2) {
    string content_a = FileRead("controller/src/bgp/testdata/routing_policy_3.xml");
    EXPECT_TRUE(parser_.Parse(content_a));
    task_util::WaitForIdle();

    const BgpRoutingPolicyConfig *policy_cfg =
            config_manager_->FindRoutingPolicy("basic_0");
    ASSERT_TRUE(policy_cfg != NULL);

    const BgpInstanceConfig *test_ri = FindInstanceConfig("test");
    ASSERT_TRUE(test_ri != NULL);
    ASSERT_TRUE(test_ri->routing_policy_list().size() == 1);
    ASSERT_TRUE(test_ri->routing_policy_list().front().routing_policy_ == "basic_0");
    ASSERT_TRUE(test_ri->routing_policy_list().front().sequence_ == "1.0");

    // Remove the link between the routing-instance and the routing-policy.
    ifmap_test_util::IFMapMsgUnlink(&db_,
        "routing-instance", "test",
        "routing-policy", "basic_0", "routing-policy-routing-instance");
    task_util::WaitForIdle();

    test_ri = FindInstanceConfig("test");
    ASSERT_TRUE(test_ri != NULL);
    ASSERT_TRUE(test_ri->routing_policy_list().size() == 0);

    boost::replace_all(content_a, "<config>", "<delete>");
    boost::replace_all(content_a, "</config>", "</delete>");
    EXPECT_TRUE(parser_.Parse(content_a));
    task_util::WaitForIdle();
    test_ri = FindInstanceConfig("test");
    ASSERT_TRUE(test_ri == NULL);
    policy_cfg = config_manager_->FindRoutingPolicy("basic_0");
    ASSERT_TRUE(policy_cfg == NULL);
}

//
// Create routing instance and routing policy
// Associate routing-policy to routing-instance
// Validate the routing instance and associate policy in BgpInstanceConfig
// Remove the routing policy from routing instance
// Validate that routing instance is not associated any routing instance
//
TEST_F(BgpIfmapConfigManagerTest, RoutingInstanceRoutingPolicy_3) {
    string content_a = FileRead("controller/src/bgp/testdata/routing_policy_3.xml");
    EXPECT_TRUE(parser_.Parse(content_a));
    task_util::WaitForIdle();

    const BgpRoutingPolicyConfig *policy_cfg =
            config_manager_->FindRoutingPolicy("basic_0");
    ASSERT_TRUE(policy_cfg != NULL);

    const BgpInstanceConfig *test_ri = FindInstanceConfig("test");
    ASSERT_TRUE(test_ri != NULL);
    ASSERT_TRUE(test_ri->routing_policy_list().size() == 1);
    ASSERT_TRUE(test_ri->routing_policy_list().front().routing_policy_ == "basic_0");
    ASSERT_TRUE(test_ri->routing_policy_list().front().sequence_ == "1.0");

    string content_b = FileRead("controller/src/bgp/testdata/routing_policy_3c.xml");
    EXPECT_TRUE(parser_.Parse(content_b));
    task_util::WaitForIdle();
    test_ri = FindInstanceConfig("test");
    ASSERT_TRUE(test_ri != NULL);
    ASSERT_TRUE(test_ri->routing_policy_list().size() == 0);
    boost::replace_all(content_a, "<config>", "<delete>");
    boost::replace_all(content_a, "</config>", "</delete>");
    EXPECT_TRUE(parser_.Parse(content_a));
    task_util::WaitForIdle();
    test_ri = FindInstanceConfig("test");
    ASSERT_TRUE(test_ri == NULL);
    policy_cfg = config_manager_->FindRoutingPolicy("basic_0");
    ASSERT_TRUE(policy_cfg == NULL);
}

//
// Create routing instance and routing policy
// Validate the introspect for instance config and policy config
//
TEST_F(BgpIfmapConfigManagerShowTest, RoutingPolicy_Show_0) {
    string content_a = FileRead("controller/src/bgp/testdata/routing_policy_3.xml");
    EXPECT_TRUE(parser_.Parse(content_a));
    task_util::WaitForIdle();

    const BgpRoutingPolicyConfig *policy_cfg =
            config_manager_->FindRoutingPolicy("basic_0");
    ASSERT_TRUE(policy_cfg != NULL);

    const BgpInstanceConfig *test_ri = FindInstanceConfig("test");
    ASSERT_TRUE(test_ri != NULL);
    ASSERT_TRUE(test_ri->routing_policy_list().size() == 1);
    ASSERT_TRUE(test_ri->routing_policy_list().front().routing_policy_ == "basic_0");
    ASSERT_TRUE(test_ri->routing_policy_list().front().sequence_ == "1.0");

    BgpSandeshContext sandesh_context;
    sandesh_context.bgp_server = &server_;
    Sandesh::set_client_context(&sandesh_context);

    const char *instance_name_list[] = {
        "test", BgpConfigManager::kMasterInstance
    };
    vector<ShowBgpInstanceConfig> instance_list;
    BOOST_FOREACH(const char *instance_name, instance_name_list) {
        const BgpInstanceConfig *config = FindInstanceConfig(instance_name);
        ASSERT_TRUE(config != NULL);
        ShowBgpInstanceConfig instance;
        instance.set_name(config->name());
        instance.set_virtual_network(config->virtual_network());
        instance.set_virtual_network_index(config->virtual_network_index());
        vector<ShowBgpInstanceRoutingPolicyConfig> routing_policy_list;
        BOOST_FOREACH(const RoutingPolicyAttachInfo &policy_config,
                      config->routing_policy_list()) {
            ShowBgpInstanceRoutingPolicyConfig sbirpc;
            sbirpc.set_policy_name(policy_config.routing_policy_);
            sbirpc.set_sequence(policy_config.sequence_);
            routing_policy_list.push_back(sbirpc);
        }
        instance.set_routing_policies(routing_policy_list);
        instance_list.push_back(instance);
    }

    bool validate_done = false;
    Sandesh::set_response_callback(
        boost::bind(ValidateShowInstanceResponse, _1, &validate_done,
                    instance_list));

    ShowBgpInstanceConfigReq *show_req = new ShowBgpInstanceConfigReq;
    show_req->HandleRequest();
    show_req->Release();
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(validate_done);

    vector<ShowBgpRoutingPolicyConfig> policy_list;
    const char *policy_name_list[] = {
        "basic_0"
    };
    BOOST_FOREACH(const char *policy_name, policy_name_list) {
        const BgpRoutingPolicyConfig *config =
            FindRoutingPolicyConfig(policy_name);
        ASSERT_TRUE(config != NULL);
        ShowBgpRoutingPolicyConfig policy;
        policy.set_name(config->name());
        std::vector<ShowBgpRoutingPolicyTermConfig> terms_list;
        BOOST_FOREACH(const RoutingPolicyTerm &term, config->terms()) {
            ShowBgpRoutingPolicyTermConfig sbrptc;
            sbrptc.set_match(term.match.ToString());
            sbrptc.set_action(term.action.ToString());
            terms_list.push_back(sbrptc);
        }
        policy.set_terms(terms_list);
        policy_list.push_back(policy);
    }
    validate_done = false;
    Sandesh::set_response_callback(
        boost::bind(ValidateShowRoutingPolicyResponse, _1, &validate_done,
                    policy_list));
    ShowBgpRoutingPolicyConfigReq *show_policy_req = new ShowBgpRoutingPolicyConfigReq;
    show_policy_req->HandleRequest();
    show_policy_req->Release();
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(validate_done);

    boost::replace_all(content_a, "<config>", "<delete>");
    boost::replace_all(content_a, "</config>", "</delete>");
    EXPECT_TRUE(parser_.Parse(content_a));
    task_util::WaitForIdle();
    test_ri = FindInstanceConfig("test");
    ASSERT_TRUE(test_ri == NULL);
    policy_cfg = config_manager_->FindRoutingPolicy("basic_0");
    ASSERT_TRUE(policy_cfg == NULL);
}

//
// Create routing instance and routing policy
// Validate the introspect for instance config and policy config
// Test with more complex match and action
//
TEST_F(BgpIfmapConfigManagerShowTest, RoutingPolicy_Show_1) {
    string content_a = FileRead("controller/src/bgp/testdata/routing_policy_6c.xml");
    EXPECT_TRUE(parser_.Parse(content_a));
    task_util::WaitForIdle();

    const BgpRoutingPolicyConfig *policy_cfg =
            config_manager_->FindRoutingPolicy("basic");
    ASSERT_TRUE(policy_cfg != NULL);

    const BgpInstanceConfig *test_ri = FindInstanceConfig("test");
    ASSERT_TRUE(test_ri != NULL);
    ASSERT_TRUE(test_ri->routing_policy_list().size() == 1);
    ASSERT_TRUE(test_ri->routing_policy_list().front().routing_policy_ == "basic");
    ASSERT_TRUE(test_ri->routing_policy_list().front().sequence_ == "1.0");

    BgpSandeshContext sandesh_context;
    sandesh_context.bgp_server = &server_;
    Sandesh::set_client_context(&sandesh_context);

    const char *instance_name_list[] = {
        "test", BgpConfigManager::kMasterInstance
    };
    vector<ShowBgpInstanceConfig> instance_list;
    BOOST_FOREACH(const char *instance_name, instance_name_list) {
        const BgpInstanceConfig *config = FindInstanceConfig(instance_name);
        ASSERT_TRUE(config != NULL);
        ShowBgpInstanceConfig instance;
        instance.set_name(config->name());
        instance.set_virtual_network(config->virtual_network());
        instance.set_virtual_network_index(config->virtual_network_index());
        vector<ShowBgpInstanceRoutingPolicyConfig> routing_policy_list;
        BOOST_FOREACH(const RoutingPolicyAttachInfo &policy_config,
                      config->routing_policy_list()) {
            ShowBgpInstanceRoutingPolicyConfig sbirpc;
            sbirpc.set_policy_name(policy_config.routing_policy_);
            sbirpc.set_sequence(policy_config.sequence_);
            routing_policy_list.push_back(sbirpc);
        }
        instance.set_routing_policies(routing_policy_list);

        instance_list.push_back(instance);
    }

    bool validate_done = false;
    Sandesh::set_response_callback(
        boost::bind(ValidateShowInstanceResponse, _1, &validate_done,
                    instance_list));

    ShowBgpInstanceConfigReq *show_req = new ShowBgpInstanceConfigReq;
    show_req->HandleRequest();
    show_req->Release();
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(validate_done);

    vector<ShowBgpRoutingPolicyConfig> policy_list;
    const char *policy_name_list[] = {
        "basic"
    };
    BOOST_FOREACH(const char *policy_name, policy_name_list) {
        const BgpRoutingPolicyConfig *config =
            FindRoutingPolicyConfig(policy_name);
        ASSERT_TRUE(config != NULL);
        ShowBgpRoutingPolicyConfig policy;
        policy.set_name(config->name());
        std::vector<ShowBgpRoutingPolicyTermConfig> terms_list;
        BOOST_FOREACH(const RoutingPolicyTerm &term, config->terms()) {
            ShowBgpRoutingPolicyTermConfig sbrptc;
            sbrptc.set_match(term.match.ToString());
            sbrptc.set_action(term.action.ToString());
            terms_list.push_back(sbrptc);
        }
        policy.set_terms(terms_list);
        policy_list.push_back(policy);
    }
    validate_done = false;
    Sandesh::set_response_callback(
        boost::bind(ValidateShowRoutingPolicyResponse, _1, &validate_done,
                    policy_list));
    ShowBgpRoutingPolicyConfigReq *show_policy_req = new ShowBgpRoutingPolicyConfigReq;
    show_policy_req->HandleRequest();
    show_policy_req->Release();
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(validate_done);

    boost::replace_all(content_a, "<config>", "<delete>");
    boost::replace_all(content_a, "</config>", "</delete>");
    EXPECT_TRUE(parser_.Parse(content_a));
    task_util::WaitForIdle();
    test_ri = FindInstanceConfig("test");
    ASSERT_TRUE(test_ri == NULL);
    policy_cfg = config_manager_->FindRoutingPolicy("basic");
    ASSERT_TRUE(policy_cfg == NULL);
}

//
// Validate the config object with one route-aggregate linked to routing instance
//
TEST_F(BgpIfmapConfigManagerTest, RouteAggregate_Basic) {
    string content_a = FileRead("controller/src/bgp/testdata/route_aggregate_0.xml");
    EXPECT_TRUE(parser_.Parse(content_a));
    task_util::WaitForIdle();

    const BgpInstanceConfig *test_ri = FindInstanceConfig("test");
    ASSERT_TRUE(test_ri != NULL);

    ASSERT_TRUE(test_ri->aggregate_routes(Address::INET).size() == 1);
    BgpInstanceConfig::AggregateRouteList list =
        test_ri->aggregate_routes(Address::INET);

    set<string_pair_t> expect_list = list_of<string_pair_t>
        ("2.2.0.0/16", "1.1.1.1");
    set<string_pair_t> current_list;
    BOOST_FOREACH(AggregateRouteConfig info, list) {
        ostringstream oss;
        oss << info.aggregate.to_string() << "/" << info.prefix_length;
        string first = oss.str();
        oss.str("");
        oss << info.nexthop.to_string();
        string second = oss.str();
        current_list.insert(make_pair(first, second));
    }

    ASSERT_TRUE(current_list.size() == expect_list.size());
    ASSERT_TRUE(std::equal(expect_list.begin(), expect_list.end(),
                           current_list.begin()));

    boost::replace_all(content_a, "<config>", "<delete>");
    boost::replace_all(content_a, "</config>", "</delete>");
    EXPECT_TRUE(parser_.Parse(content_a));
    task_util::WaitForIdle();
    test_ri = FindInstanceConfig("test");
    ASSERT_TRUE(test_ri == NULL);
}

//
// Validate the config object with mutliple route-aggregate objects
// linked to routing instance. One route-aggregate object has inet and other one
// has inet6 prefix
//
TEST_F(BgpIfmapConfigManagerTest, RouteAggregate_Basic_v4v6) {
    string content_a = FileRead("controller/src/bgp/testdata/route_aggregate_1.xml");
    EXPECT_TRUE(parser_.Parse(content_a));
    task_util::WaitForIdle();

    const BgpInstanceConfig *test_ri = FindInstanceConfig("test");
    ASSERT_TRUE(test_ri != NULL);

    ASSERT_TRUE(test_ri->aggregate_routes(Address::INET).size() == 1);
    ASSERT_TRUE(test_ri->aggregate_routes(Address::INET6).size() == 1);

    set<string_pair_t> expect_list = list_of<string_pair_t>
        ("2.2.0.0/16", "1.1.1.1");
    set<string_pair_t> current_list;
    BgpInstanceConfig::AggregateRouteList list =
        test_ri->aggregate_routes(Address::INET);
    BOOST_FOREACH(AggregateRouteConfig info, list) {
        ostringstream oss;
        oss << info.aggregate.to_string() << "/" << info.prefix_length;
        string first = oss.str();
        oss.str("");
        oss << info.nexthop.to_string();
        string second = oss.str();
        current_list.insert(make_pair(first, second));
    }

    ASSERT_TRUE(current_list.size() == expect_list.size());
    ASSERT_TRUE(std::equal(expect_list.begin(), expect_list.end(),
                           current_list.begin()));

    set<string_pair_t> expect_list_inet6 = list_of<string_pair_t>
        ("2001:db8:85a3::/64", "2002:db8:85a3::8a2e:370:7334");
    set<string_pair_t> current_list_inet6;
    list = test_ri->aggregate_routes(Address::INET6);
    BOOST_FOREACH(AggregateRouteConfig info, list) {
        ostringstream oss;
        oss << info.aggregate.to_string() << "/" << info.prefix_length;
        string first = oss.str();
        oss.str("");
        oss << info.nexthop.to_string();
        string second = oss.str();
        current_list_inet6.insert(make_pair(first, second));
    }

    ASSERT_TRUE(current_list_inet6.size() == expect_list_inet6.size());
    ASSERT_TRUE(std::equal(expect_list_inet6.begin(), expect_list_inet6.end(),
                           current_list_inet6.begin()));
    boost::replace_all(content_a, "<config>", "<delete>");
    boost::replace_all(content_a, "</config>", "</delete>");
    EXPECT_TRUE(parser_.Parse(content_a));
    task_util::WaitForIdle();
    test_ri = FindInstanceConfig("test");
    ASSERT_TRUE(test_ri == NULL);
}

//
// Validate the route-aggregate config object on BgpInstanceConfig object after
// unlinking routing-instance and route-aggregate object
//
TEST_F(BgpIfmapConfigManagerTest, RouteAggregate_Unlink) {
    string content_a = FileRead("controller/src/bgp/testdata/route_aggregate_1.xml");
    EXPECT_TRUE(parser_.Parse(content_a));
    task_util::WaitForIdle();

    const BgpInstanceConfig *test_ri = FindInstanceConfig("test");
    ASSERT_TRUE(test_ri != NULL);

    ASSERT_TRUE(test_ri->aggregate_routes(Address::INET).size() == 1);
    ASSERT_TRUE(test_ri->aggregate_routes(Address::INET6).size() == 1);

    // unlink routing instance and route aggregate
    ifmap_test_util::IFMapMsgUnlink(&db_, "routing-instance", "test",
        "route-aggregate", "vn_subnet_0", "route-aggregate-routing-instance");
    task_util::WaitForIdle();

    ASSERT_TRUE(test_ri->aggregate_routes(Address::INET).size() == 0);
    ASSERT_TRUE(test_ri->aggregate_routes(Address::INET6).size() == 1);

    // Link routing instance and route aggregate
    ifmap_test_util::IFMapMsgLink(&db_, "routing-instance", "test",
        "route-aggregate", "vn_subnet_0", "route-aggregate-routing-instance");
    task_util::WaitForIdle();

    ASSERT_TRUE(test_ri->aggregate_routes(Address::INET).size() == 1);
    ASSERT_TRUE(test_ri->aggregate_routes(Address::INET6).size() == 1);

    boost::replace_all(content_a, "<config>", "<delete>");
    boost::replace_all(content_a, "</config>", "</delete>");
    EXPECT_TRUE(parser_.Parse(content_a));
    task_util::WaitForIdle();
    test_ri = FindInstanceConfig("test");
    ASSERT_TRUE(test_ri == NULL);
}

TEST_F(BgpIfmapConfigManagerTest, RouteAggregate_UpdateNexthop) {
    string content_a = FileRead("controller/src/bgp/testdata/route_aggregate_0.xml");
    EXPECT_TRUE(parser_.Parse(content_a));
    task_util::WaitForIdle();

    const BgpInstanceConfig *test_ri = FindInstanceConfig("test");
    ASSERT_TRUE(test_ri != NULL);

    ASSERT_TRUE(test_ri->aggregate_routes(Address::INET).size() == 1);

    content_a = FileRead("controller/src/bgp/testdata/route_aggregate_0a.xml");
    EXPECT_TRUE(parser_.Parse(content_a));
    task_util::WaitForIdle();

    BgpInstanceConfig::AggregateRouteList list =
        test_ri->aggregate_routes(Address::INET);

    set<string_pair_t> expect_list = list_of<string_pair_t>
        ("2.2.0.0/16", "1.1.1.254");
    set<string_pair_t> current_list;
    BOOST_FOREACH(AggregateRouteConfig info, list) {
        ostringstream oss;
        oss << info.aggregate.to_string() << "/" << info.prefix_length;
        string first = oss.str();
        oss.str("");
        oss << info.nexthop.to_string();
        string second = oss.str();
        current_list.insert(make_pair(first, second));
    }

    ASSERT_TRUE(current_list.size() == expect_list.size());
    ASSERT_TRUE(std::equal(expect_list.begin(), expect_list.end(),
                           current_list.begin()));

    boost::replace_all(content_a, "<config>", "<delete>");
    boost::replace_all(content_a, "</config>", "</delete>");
    EXPECT_TRUE(parser_.Parse(content_a));
    task_util::WaitForIdle();
    test_ri = FindInstanceConfig("test");
    ASSERT_TRUE(test_ri == NULL);
}

TEST_F(BgpIfmapConfigManagerTest, RouteAggregate_UpdatePrefix) {
    string content_a = FileRead("controller/src/bgp/testdata/route_aggregate_0.xml");
    EXPECT_TRUE(parser_.Parse(content_a));
    task_util::WaitForIdle();

    const BgpInstanceConfig *test_ri = FindInstanceConfig("test");
    ASSERT_TRUE(test_ri != NULL);

    ASSERT_TRUE(test_ri->aggregate_routes(Address::INET).size() == 1);

    content_a = FileRead("controller/src/bgp/testdata/route_aggregate_0b.xml");
    EXPECT_TRUE(parser_.Parse(content_a));
    task_util::WaitForIdle();

    BgpInstanceConfig::AggregateRouteList list =
        test_ri->aggregate_routes(Address::INET);

    set<string_pair_t> expect_list = list_of<string_pair_t>
        ("3.3.0.0/16", "1.1.1.254");
    set<string_pair_t> current_list;
    BOOST_FOREACH(AggregateRouteConfig info, list) {
        ostringstream oss;
        oss << info.aggregate.to_string() << "/" << info.prefix_length;
        string first = oss.str();
        oss.str("");
        oss << info.nexthop.to_string();
        string second = oss.str();
        current_list.insert(make_pair(first, second));
    }

    ASSERT_TRUE(current_list.size() == expect_list.size());
    ASSERT_TRUE(std::equal(expect_list.begin(), expect_list.end(),
                           current_list.begin()));

    boost::replace_all(content_a, "<config>", "<delete>");
    boost::replace_all(content_a, "</config>", "</delete>");
    EXPECT_TRUE(parser_.Parse(content_a));
    task_util::WaitForIdle();
    test_ri = FindInstanceConfig("test");
    ASSERT_TRUE(test_ri == NULL);
}

TEST_F(BgpIfmapConfigManagerTest, RouteAggregate_UpdatePrefixLen) {
    string content_a = FileRead("controller/src/bgp/testdata/route_aggregate_0.xml");
    EXPECT_TRUE(parser_.Parse(content_a));
    task_util::WaitForIdle();

    const BgpInstanceConfig *test_ri = FindInstanceConfig("test");
    ASSERT_TRUE(test_ri != NULL);

    ASSERT_TRUE(test_ri->aggregate_routes(Address::INET).size() == 1);

    content_a = FileRead("controller/src/bgp/testdata/route_aggregate_0c.xml");
    EXPECT_TRUE(parser_.Parse(content_a));
    task_util::WaitForIdle();

    BgpInstanceConfig::AggregateRouteList list =
        test_ri->aggregate_routes(Address::INET);

    set<string_pair_t> expect_list = list_of<string_pair_t>
        ("2.2.0.0/24", "1.1.1.254");
    set<string_pair_t> current_list;
    BOOST_FOREACH(AggregateRouteConfig info, list) {
        ostringstream oss;
        oss << info.aggregate.to_string() << "/" << info.prefix_length;
        string first = oss.str();
        oss.str("");
        oss << info.nexthop.to_string();
        string second = oss.str();
        current_list.insert(make_pair(first, second));
    }

    ASSERT_TRUE(current_list.size() == expect_list.size());
    ASSERT_TRUE(std::equal(expect_list.begin(), expect_list.end(),
                           current_list.begin()));

    boost::replace_all(content_a, "<config>", "<delete>");
    boost::replace_all(content_a, "</config>", "</delete>");
    EXPECT_TRUE(parser_.Parse(content_a));
    task_util::WaitForIdle();
    test_ri = FindInstanceConfig("test");
    ASSERT_TRUE(test_ri == NULL);
}

TEST_F(BgpIfmapConfigManagerTest, RouteAggregate_MultipleInet) {
    string content_a = FileRead("controller/src/bgp/testdata/route_aggregate_3.xml");
    EXPECT_TRUE(parser_.Parse(content_a));
    task_util::WaitForIdle();

    const BgpInstanceConfig *test_ri = FindInstanceConfig("test");
    ASSERT_TRUE(test_ri != NULL);

    ASSERT_TRUE(test_ri->aggregate_routes(Address::INET).size() == 3);

    BgpInstanceConfig::AggregateRouteList list =
        test_ri->aggregate_routes(Address::INET);

    set<string_pair_t> expect_list = list_of<string_pair_t>
        ("2.2.0.0/16", "1.1.1.1")
        ("3.3.0.0/16", "1.1.1.1")
        ("4.0.0.0/8", "1.1.1.1");
    set<string_pair_t> current_list;
    BOOST_FOREACH(AggregateRouteConfig info, list) {
        ostringstream oss;
        oss << info.aggregate.to_string() << "/" << info.prefix_length;
        string first = oss.str();
        oss.str("");
        oss << info.nexthop.to_string();
        string second = oss.str();
        current_list.insert(make_pair(first, second));
    }

    ASSERT_TRUE(current_list.size() == expect_list.size());
    ASSERT_TRUE(std::equal(expect_list.begin(), expect_list.end(),
                           current_list.begin()));

    boost::replace_all(content_a, "<config>", "<delete>");
    boost::replace_all(content_a, "</config>", "</delete>");
    EXPECT_TRUE(parser_.Parse(content_a));
    task_util::WaitForIdle();
    test_ri = FindInstanceConfig("test");
    ASSERT_TRUE(test_ri == NULL);
}

TEST_F(BgpIfmapConfigManagerTest, RouteAggregate_MultipleInet_Unlink) {
    string content_a = FileRead("controller/src/bgp/testdata/route_aggregate_3.xml");
    EXPECT_TRUE(parser_.Parse(content_a));
    task_util::WaitForIdle();

    const BgpInstanceConfig *test_ri = FindInstanceConfig("test");
    ASSERT_TRUE(test_ri != NULL);

    ASSERT_TRUE(test_ri->aggregate_routes(Address::INET).size() == 3);

    BgpInstanceConfig::AggregateRouteList list =
        test_ri->aggregate_routes(Address::INET);

    set<string_pair_t> expect_list = list_of<string_pair_t>
        ("2.2.0.0/16", "1.1.1.1")
        ("3.3.0.0/16", "1.1.1.1")
        ("4.0.0.0/8", "1.1.1.1");
    set<string_pair_t> current_list;
    BOOST_FOREACH(AggregateRouteConfig info, list) {
        ostringstream oss;
        oss << info.aggregate.to_string() << "/" << info.prefix_length;
        string first = oss.str();
        oss.str("");
        oss << info.nexthop.to_string();
        string second = oss.str();
        current_list.insert(make_pair(first, second));
    }

    ASSERT_TRUE(current_list.size() == expect_list.size());
    ASSERT_TRUE(std::equal(expect_list.begin(), expect_list.end(),
                           current_list.begin()));

    // unlink routing instance and route aggregate
    ifmap_test_util::IFMapMsgUnlink(&db_, "routing-instance", "test",
        "route-aggregate", "vn_subnet_0", "route-aggregate-routing-instance");
    ifmap_test_util::IFMapMsgUnlink(&db_, "routing-instance", "test",
        "route-aggregate", "vn_subnet_1", "route-aggregate-routing-instance");
    task_util::WaitForIdle();

    ASSERT_TRUE(test_ri->aggregate_routes(Address::INET).size() == 1);

    list = test_ri->aggregate_routes(Address::INET);

    expect_list = list_of<string_pair_t>("4.0.0.0/8", "1.1.1.1");
    current_list.clear();
    BOOST_FOREACH(AggregateRouteConfig info, list) {
        ostringstream oss;
        oss << info.aggregate.to_string() << "/" << info.prefix_length;
        string first = oss.str();
        oss.str("");
        oss << info.nexthop.to_string();
        string second = oss.str();
        current_list.insert(make_pair(first, second));
    }

    ASSERT_TRUE(current_list.size() == expect_list.size());
    ASSERT_TRUE(std::equal(expect_list.begin(), expect_list.end(),
                           current_list.begin()));

    boost::replace_all(content_a, "<config>", "<delete>");
    boost::replace_all(content_a, "</config>", "</delete>");
    EXPECT_TRUE(parser_.Parse(content_a));
    task_util::WaitForIdle();
    test_ri = FindInstanceConfig("test");
    ASSERT_TRUE(test_ri == NULL);
}

//
// Validate the introspect for route aggregate
//
TEST_F(BgpIfmapConfigManagerShowTest, RouteAggregate_Show) {
    string content_a = FileRead("controller/src/bgp/testdata/route_aggregate_3.xml");
    EXPECT_TRUE(parser_.Parse(content_a));
    task_util::WaitForIdle();

    const BgpInstanceConfig *test_ri = FindInstanceConfig("test");
    ASSERT_TRUE(test_ri != NULL);

    ASSERT_TRUE(test_ri->aggregate_routes(Address::INET).size() == 3);
    BgpInstanceConfig::AggregateRouteList list =
        test_ri->aggregate_routes(Address::INET);

    set<string_pair_t> expect_list = list_of<string_pair_t>
        ("2.2.0.0/16", "1.1.1.1")
        ("3.3.0.0/16", "1.1.1.1")
        ("4.0.0.0/8", "1.1.1.1");
    set<string_pair_t> current_list;
    BOOST_FOREACH(AggregateRouteConfig info, list) {
        ostringstream oss;
        oss << info.aggregate.to_string() << "/" << info.prefix_length;
        string first = oss.str();
        oss.str("");
        oss << info.nexthop.to_string();
        string second = oss.str();
        current_list.insert(make_pair(first, second));
    }

    ASSERT_TRUE(current_list.size() == expect_list.size());
    ASSERT_TRUE(std::equal(expect_list.begin(), expect_list.end(),
                           current_list.begin()));

    BgpSandeshContext sandesh_context;
    sandesh_context.bgp_server = &server_;
    Sandesh::set_client_context(&sandesh_context);

    const char *instance_name_list[] = {
        "test", BgpConfigManager::kMasterInstance
    };
    vector<ShowBgpInstanceConfig> instance_list;
    BOOST_FOREACH(const char *instance_name, instance_name_list) {
        const BgpInstanceConfig *config = FindInstanceConfig(instance_name);
        ASSERT_TRUE(config != NULL);
        ShowBgpInstanceConfig instance;
        instance.set_name(config->name());
        instance.set_virtual_network(config->virtual_network());
        instance.set_virtual_network_index(config->virtual_network_index());
        if (config->name() == "test") {
            vector<ShowBgpRouteAggregateConfig> aggregate_route_list;
            vector<Address::Family> families =
                list_of(Address::INET)(Address::INET6);
            BOOST_FOREACH(Address::Family family, families) {
                BOOST_FOREACH(const AggregateRouteConfig &aggregate_rt_config,
                              config->aggregate_routes(family)) {
                    ShowBgpRouteAggregateConfig sbarc;
                    string prefix =
                        aggregate_rt_config.aggregate.to_string() + "/";
                    prefix +=
                        integerToString(aggregate_rt_config.prefix_length);
                    sbarc.set_prefix(prefix);
                    sbarc.set_nexthop(aggregate_rt_config.nexthop.to_string());
                    aggregate_route_list.push_back(sbarc);
                }
            }
            instance.set_aggregate_routes(aggregate_route_list);
        }

        instance_list.push_back(instance);
    }

    bool validate_done = false;
    Sandesh::set_response_callback(
        boost::bind(ValidateShowInstanceResponse, _1, &validate_done,
                    instance_list));

    ShowBgpInstanceConfigReq *show_req = new ShowBgpInstanceConfigReq;
    show_req->HandleRequest();
    show_req->Release();
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(validate_done);

    boost::replace_all(content_a, "<config>", "<delete>");
    boost::replace_all(content_a, "</config>", "</delete>");
    EXPECT_TRUE(parser_.Parse(content_a));
    task_util::WaitForIdle();
    test_ri = FindInstanceConfig("test");
    ASSERT_TRUE(test_ri == NULL);
}

int main(int argc, char **argv) {
    bgp_log_test::init();
    ControlNode::SetDefaultSchedulingPolicy();
    BgpObjectFactory::Register<BgpConfigManager>(
        boost::factory<BgpIfmapConfigManager *>());
    ::testing::InitGoogleTest(&argc, argv);
    int error = RUN_ALL_TESTS();
    TaskScheduler::GetInstance()->Terminate();
    return error;
}
