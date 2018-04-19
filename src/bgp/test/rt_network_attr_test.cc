/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <fstream>

#include "bgp/bgp_factory.h"
#include "bgp/test/bgp_server_test_util.h"
#include "bgp/xmpp_message_builder.h"
#include "control-node/control_node.h"
#include "control-node/test/control_node_test.h"
#include "control-node/test/network_agent_mock.h"
#include "config-client-mgr/config_amqp_client.h"
#include "config-client-mgr/config_cass2json_adapter.h"
#include "config-client-mgr/config_cassandra_client.h"
#include "config-client-mgr/config_client_manager.h"
#include "config-client-mgr/config_factory.h"
#include "ifmap/client/config_json_parser.h"
#include "ifmap/ifmap_factory.h"
#include "ifmap/ifmap_link_table.h"
#include "ifmap/ifmap_server.h"
#include "ifmap/test/config_cassandra_client_test.h"
#include "ifmap/test/ifmap_test_util.h"
#include "rapidjson/document.h"
#include "schema/bgp_schema_types.h"
#include "schema/vnc_cfg_types.h"

using namespace std;

class NetworkConfigTest : public ::testing::Test {
protected:
    NetworkConfigTest()
        : bgp_server_(&evm_, "localhost"),
          map_server_(bgp_server_.config_db(), bgp_server_.config_graph(),
                      evm_.io_service()),
          config_client_manager_(new ConfigClientManager(&evm_,
              "localhost", "config-test", config_options_)) {
        config_cassandra_client_=dynamic_cast<ConfigCassandraClientTest *>(
            config_client_manager_->config_db_client());
    }

    virtual void SetUp() {
        ConfigCass2JsonAdapter::set_assert_on_parse_error(false);
        ConfigJsonParser *config_json_parser_ =
          static_cast<ConfigJsonParser *>(config_client_manager_->config_json_parser());
        config_json_parser_->ifmap_server_set(&map_server_);
        vnc_cfg_JsonParserInit(config_json_parser_);
        bgp_schema_JsonParserInit(config_json_parser_);
        task_util::WaitForIdle();
    }

    virtual void TearDown() {
        bgp_server_.Shutdown();
        task_util::WaitForIdle();
        map_server_.Shutdown();
        task_util::WaitForIdle();
        ConfigJsonParser *config_json_parser_ =
          static_cast<ConfigJsonParser *>(config_client_manager_->config_json_parser());
        config_json_parser_->MetadataClear("vnc_cfg");
        evm_.Shutdown();
        task_util::WaitForIdle();
    }

    void ParseEventsJson (string events_file) {
        ConfigCassandraClientTest::ParseEventsJson(config_client_manager_.get(),
                events_file);
    }

    void FeedEventsJson () {
        ConfigCassandraClientTest::FeedEventsJson(config_client_manager_.get());
    }

    EventManager evm_;
    BgpServerTest bgp_server_;
    const ConfigClientOptions config_options_;
    IFMapServer map_server_;
    boost::scoped_ptr<ConfigClientManager> config_client_manager_;
    ConfigCassandraClientTest *config_cassandra_client_;
};

TEST_F(NetworkConfigTest, SoapMessage1) {
    SCOPED_TRACE(__FUNCTION__);
    ParseEventsJson("controller/src/bgp/testdata/network_test_1.json");
    FeedEventsJson();
    task_util::WaitForIdle();
    RoutingInstanceMgr *manager = bgp_server_.routing_instance_mgr();

    RouteTarget tgt1 = RouteTarget::FromString("target:1:84");
    TASK_UTIL_EXPECT_NE(static_cast<const RoutingInstance *>(NULL),
                        manager->GetInstanceByTarget(tgt1));
    const RoutingInstance *rti = manager->GetInstanceByTarget(tgt1);
    if (rti != NULL) {
        TASK_UTIL_WAIT_NE_NO_MSG(rti->virtual_network_index(),
            0, 1000, 10000, "Wait for vn index..");
        string netname = rti->GetVirtualNetworkName();
        size_t colon = netname.rfind(":");
        if (colon != string::npos) {
            netname = netname.substr(colon + 1);
        }
        EXPECT_EQ("vn1", netname);
        EXPECT_EQ(1, rti->virtual_network_index());
    }

    RouteTarget tgt2 = RouteTarget::FromString("target:1:85");
    TASK_UTIL_EXPECT_NE(static_cast<const RoutingInstance *>(NULL),
                        manager->GetInstanceByTarget(tgt2));
    rti = manager->GetInstanceByTarget(tgt2);
    if (rti != NULL) {
        TASK_UTIL_WAIT_NE_NO_MSG(rti->virtual_network_index(),
            0, 1000, 10000, "Wait for vn index..");
        string netname = rti->GetVirtualNetworkName();
        size_t colon = netname.rfind(":");
        if (colon != string::npos) {
            netname = netname.substr(colon + 1);
        }
        EXPECT_EQ("vn2", netname);
        EXPECT_EQ(2, rti->virtual_network_index());
    }
}

TEST_F(NetworkConfigTest, SoapMessage2) {
    SCOPED_TRACE(__FUNCTION__);
    ParseEventsJson("controller/src/bgp/testdata/network_test_2.json");
    FeedEventsJson();
    task_util::WaitForIdle();
    RoutingInstanceMgr *manager = bgp_server_.routing_instance_mgr();

    RouteTarget tgt1 = RouteTarget::FromString("target:1:6");
    TASK_UTIL_EXPECT_NE(static_cast<const RoutingInstance *>(NULL),
                        manager->GetInstanceByTarget(tgt1));
    const RoutingInstance *rti = manager->GetInstanceByTarget(tgt1);
    if (rti != NULL) {
        TASK_UTIL_WAIT_NE_NO_MSG(rti->virtual_network_index(),
            0, 1000, 10000, "Wait for vn index..");
        string netname = rti->GetVirtualNetworkName();
        size_t colon = netname.rfind(":");
        if (colon != string::npos) {
            netname = netname.substr(colon + 1);
        }
        EXPECT_EQ("vn1", netname);
        EXPECT_EQ(1, rti->virtual_network_index());
    }
}

TEST_F(NetworkConfigTest, Dependency) {
    SCOPED_TRACE(__FUNCTION__);
    DB *db = bgp_server_.config_db();
    ifmap_test_util::IFMapMsgLink(db, "routing-instance", "blue",
                                  "route-target", "target:1:1",
                                  "instance-target");
    task_util::WaitForIdle();
    RoutingInstanceMgr *manager = bgp_server_.routing_instance_mgr();
    RoutingInstance *blue = manager->GetRoutingInstance("blue");
    ASSERT_TRUE(blue != NULL);

    ifmap_test_util::IFMapMsgLink(db, "routing-instance", "blue",
                                  "virtual-network", "color",
                                  "virtual-network-routing-instance");
    task_util::WaitForIdle();
    EXPECT_EQ("color", blue->GetVirtualNetworkName());

    RouteTarget tgt1 = RouteTarget::FromString("target:1:1");
    const RoutingInstance *rti = manager->GetInstanceByTarget(tgt1);
    EXPECT_EQ(blue, rti);
}

class TestEnvironment : public ::testing::Environment {
    virtual ~TestEnvironment() { }
};

static void SetUp() {
    BgpServer::Initialize();
    ControlNode::SetDefaultSchedulingPolicy();
    BgpServerTest::GlobalSetUp();
    BgpObjectFactory::Register<BgpXmppMessageBuilder>(
        boost::factory<BgpXmppMessageBuilder *>());
    ConfigFactory::Register<ConfigCassandraClient>(
        boost::factory<ConfigCassandraClientTest *>());
    ConfigFactory::Register<ConfigJsonParserBase>(
        boost::factory<ConfigJsonParser *>());
}

static void TearDown() {
    BgpServer::Terminate();
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
