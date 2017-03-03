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
#include "ifmap/client/config_amqp_client.h"
#include "ifmap/client/config_cass2json_adapter.h"
#include "ifmap/client/config_cassandra_client.h"
#include "ifmap/client/config_client_manager.h"
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
              &map_server_, "localhost", "config-test", config_options_)) {
        config_cassandra_client_=dynamic_cast<ConfigCassandraClientTest *>(
            config_client_manager_->config_db_client());
    }

    virtual void SetUp() {
        ConfigCass2JsonAdapter::set_assert_on_parse_error(false);
        vnc_cfg_JsonParserInit(config_client_manager_->config_json_parser());
        bgp_schema_JsonParserInit(config_client_manager_->config_json_parser());
        task_util::WaitForIdle();
    }

    virtual void TearDown() {
        bgp_server_.Shutdown();
        task_util::WaitForIdle();
        map_server_.Shutdown();
        task_util::WaitForIdle();
        config_client_manager_->config_json_parser()->MetadataClear("vnc_cfg");
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
    const IFMapConfigOptions config_options_;
    IFMapServer map_server_;
    boost::scoped_ptr<ConfigClientManager> config_client_manager_;
    ConfigCassandraClientTest *config_cassandra_client_;
};

TEST_F(NetworkConfigTest, SoapMessage1) {
    SCOPED_TRACE(__FUNCTION__);
    ParseEventsJson("controller/src/bgp/testdata/network_test_1.json");
    FeedEventsJson();
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

// MiniSystemTest
// 2 control-nodes and 2 agents.
class MiniSystemTest : public ::testing::Test {
protected:
    static const int kTimeoutSeconds = 45;
    static const char *config_tmpl;
    MiniSystemTest()
        : node_a_(new test::ControlNodeTest(&evm_, "A")),
          node_b_(new test::ControlNodeTest(&evm_, "B")) {
    }

    virtual void SetUp() {
        char config[4096];
        snprintf(config, sizeof(config), config_tmpl,
                 node_a_->bgp_port(), node_b_->bgp_port());
        node_a_->BgpConfig(config);
        node_b_->BgpConfig(config);
        task_util::WaitForIdle();
    }
    virtual void TearDown() {
        agent_a_->Delete();
        agent_b_->Delete();
    }

    bool SessionsEstablished() {
        return ((node_a_->BgpEstablishedCount() == 1) &&
                (node_b_->BgpEstablishedCount() == 1) &&
                agent_a_->IsEstablished() &&
                agent_b_->IsEstablished());
    }

    bool AgentRouteCount(test::NetworkAgentMock *agent, int count) {
        return agent->RouteCount() == count;
    }
    void WaitForEstablished() {
        task_util::WaitForCondition(
            &evm_,
            boost::bind(&MiniSystemTest::SessionsEstablished, this),
            kTimeoutSeconds);
        ASSERT_TRUE(SessionsEstablished())
            << "bgp sessions: (" << node_a_->BgpEstablishedCount()
            << ", " << node_b_->BgpEstablishedCount()
            << ", agent A: "
            << (agent_a_->IsEstablished() ? "Established" : "Down")
            << ", agent B: "
            << (agent_b_->IsEstablished() ? "Established" : "Down");
    }

    void WaitForRouteCount(test::NetworkAgentMock *agent, int count) {
        SCOPED_TRACE("WaitForRouteCount");
        task_util::WaitForCondition(
            &evm_,
            boost::bind(&MiniSystemTest::AgentRouteCount, this, agent, count),
            kTimeoutSeconds);
    }

    EventManager evm_;
    boost::scoped_ptr<test::ControlNodeTest> node_a_;
    boost::scoped_ptr<test::ControlNodeTest> node_b_;
    boost::scoped_ptr<test::NetworkAgentMock> agent_a_;
    boost::scoped_ptr<test::NetworkAgentMock> agent_b_;
};

const char *MiniSystemTest::config_tmpl = "\
<config>\
    <bgp-router name=\'A\'>\
        <identifier>192.168.0.1</identifier>\
        <address>127.0.0.1</address>\
        <port>%d</port>\
        <session to=\'B\'>\
            <address-families>\
            <family>inet-vpn</family>\
            <family>erm-vpn</family>\
            <family>route-target</family>\
            </address-families>\
        </session>\
    </bgp-router>\
    <bgp-router name=\'B\'>\
        <identifier>192.168.0.2</identifier>\
        <address>127.0.0.1</address>\
        <port>%d</port>\
        <session to=\'A\'>\
            <address-families>\
            <family>inet-vpn</family>\
            <family>erm-vpn</family>\
            <family>route-target</family>\
            </address-families>\
        </session>\
    </bgp-router>\
</config>\
";

TEST_F(MiniSystemTest, DifferentNodes) {
    SCOPED_TRACE(__FUNCTION__);
    const char *net_1 = "default-domain:b859ddbabe3d4c4dba8402084831e6fe:vn1";
    const char *net_2 = "default-domain:b859ddbabe3d4c4dba8402084831e6fe:vn2";

    node_a_->IFMapMessage("controller/src/bgp/testdata/network_test_1.json");
    node_b_->IFMapMessage("controller/src/bgp/testdata/network_test_1.json");

    const char *ri_1 = "default-domain:b859ddbabe3d4c4dba8402084831e6fe:vn1";
    const char *ri_2 = "default-domain:b859ddbabe3d4c4dba8402084831e6fe:vn2";
    node_a_->VerifyRoutingInstance(ri_1);
    node_a_->VerifyRoutingInstance(ri_2);
    node_b_->VerifyRoutingInstance(ri_1);
    node_b_->VerifyRoutingInstance(ri_2);

    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, "phys-host-1", node_a_->xmpp_port(),
                                   "127.0.0.1"));
    agent_b_.reset(
        new test::NetworkAgentMock(&evm_, "phys-host-2", node_b_->xmpp_port(),
                                   "127.0.0.2"));
    WaitForEstablished();

    agent_a_->Subscribe(net_1, 1);
    agent_a_->AddRoute(net_1, "10.0.1.1/32");
    agent_b_->Subscribe(net_2, 1);
    agent_b_->AddRoute(net_2, "20.0.1.1/32");

    WaitForRouteCount(agent_a_.get(), 2);
    TASK_UTIL_ASSERT_TRUE(agent_a_->RouteLookup(net_1, "10.0.1.1/32") != NULL);
    const test::NetworkAgentMock::RouteEntry *rt1 =
        agent_a_->RouteLookup(net_1, "10.0.1.1/32");
    ASSERT_TRUE(rt1 != NULL);
    EXPECT_EQ(net_1, rt1->entry.virtual_network);

    TASK_UTIL_ASSERT_TRUE(agent_a_->RouteLookup(net_1, "20.0.1.1/32") != NULL);
    const test::NetworkAgentMock::RouteEntry *rt2 =
        agent_a_->RouteLookup(net_1, "20.0.1.1/32");
    ASSERT_TRUE(rt2 != NULL);
    EXPECT_EQ(net_2, rt2->entry.virtual_network);

}

static string network_name(const char *rti_name) {
    string instance(rti_name);
    size_t loc = instance.rfind(':');
    if (loc != string::npos) {
        return instance.substr(0, loc);
    }
    return "<nil>";
}

TEST_F(MiniSystemTest, SameNode) {
    SCOPED_TRACE(__FUNCTION__);
    const char *net_1 = "default-domain:b47d0eacc9c446eabc9b4eea3d6f6133:vn1:vn1";
    const char *net_2 = "default-domain:b47d0eacc9c446eabc9b4eea3d6f6133:vn2:vn2";
    node_a_->IFMapMessage("controller/src/bgp/testdata/network_test_2.json");
    node_b_->IFMapMessage("controller/src/bgp/testdata/network_test_2.json");

    const char *ri_1 = "default-domain:b47d0eacc9c446eabc9b4eea3d6f6133:vn1:vn1";
    const char *ri_2 = "default-domain:b47d0eacc9c446eabc9b4eea3d6f6133:vn2:vn2";
    node_a_->VerifyRoutingInstance(ri_1);
    node_a_->VerifyRoutingInstance(ri_2);
    node_b_->VerifyRoutingInstance(ri_1);
    node_b_->VerifyRoutingInstance(ri_2);

    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, "host-187",
                                   node_a_->xmpp_port(), "127.0.0.1"));
    agent_b_.reset(
        new test::NetworkAgentMock(&evm_, "user-X9SCL-X9SCM",
                                   node_a_->xmpp_port(), "127.0.0.2"));
    WaitForEstablished();

    agent_a_->Subscribe(net_1, 1);
    agent_a_->AddRoute(net_1, "10.0.1.1/32");
    agent_b_->Subscribe(net_2, 1);
    agent_b_->AddRoute(net_2, "20.0.1.1/32");

    WaitForRouteCount(agent_a_.get(), 2);
    const test::NetworkAgentMock::RouteEntry *rt1 =
    agent_a_->RouteLookup(net_1, "10.0.1.1/32");
    ASSERT_TRUE(rt1 != NULL);
    EXPECT_EQ(network_name(net_1), rt1->entry.virtual_network);

    const test::NetworkAgentMock::RouteEntry *rt2 =
    agent_a_->RouteLookup(net_1, "20.0.1.1/32");
    ASSERT_TRUE(rt2 != NULL);
    EXPECT_EQ(network_name(net_2), rt2->entry.virtual_network);
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
    IFMapFactory::Register<ConfigCassandraClient>(
        boost::factory<ConfigCassandraClientTest *>());
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
