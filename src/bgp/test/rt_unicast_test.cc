/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <fstream>

#include <boost/assign/list_of.hpp>
#include <boost/foreach.hpp>

#include "base/logging.h"
#include "bgp/bgp_session_manager.h"
#include "base/test/task_test_util.h"
#include "bgp/bgp_table.h"
#include "bgp/bgp_xmpp_channel.h"
#include "bgp/inet/inet_table.h"
#include "bgp/l3vpn/inetvpn_route.h"
#include "bgp/l3vpn/inetvpn_table.h"
#include "bgp/test/bgp_server_test_util.h"
#include "bgp/test/bgp_test_util.h"
#include "bgp/routing-instance/routing_instance.h"
#include "control-node/control_node.h"
#include "control-node/test/control_node_test.h"
#include "control-node/test/network_agent_mock.h"
#include "ifmap/test/ifmap_test_util.h"
#include "io/event_manager.h"

#include "schema/bgp_schema_types.h"
#include "schema/vnc_cfg_types.h"
#include "testing/gunit.h"
#include "xmpp/xmpp_connection.h"
#include "xmpp/xmpp_factory.h"
#include "xmpp/xmpp_server.h"

using namespace boost::assign;
using boost::asio::ip::tcp;
using namespace std;

class XmppServerConnectionTest : public XmppServerConnection {
public:
    typedef boost::function<tcp::endpoint(const XmppServerConnection *)>
        EndpointLookupFn;

    XmppServerConnectionTest(
            XmppServer *server, const XmppChannelConfig *config)
        : XmppServerConnection(server, config) {
    }
    virtual tcp::endpoint endpoint() const {
        if (endpoint_lookup_) {
            return endpoint_lookup_(this);
        }
        return XmppServerConnection::endpoint();
    }
    static void SetEndpointOverride(EndpointLookupFn endpoint_lookup) {
        endpoint_lookup_ = endpoint_lookup;
    }
    static void ClearEndpointOverride() {
        endpoint_lookup_ = 0;
    }
private:
    static EndpointLookupFn endpoint_lookup_;
};

XmppServerConnectionTest::EndpointLookupFn
    XmppServerConnectionTest::endpoint_lookup_;

class RtUnicastTest : public ::testing::Test {
  protected:
    static const int kTimeoutSeconds = 15;
    RtUnicastTest() {
        const char *names[] = {"A", "B", "C", "D"};
        size_t arraysize = sizeof(names) / sizeof(char *);
        for (size_t i = 0; i < arraysize; ++i) {
            control_nodes_.push_back(
                    new test::ControlNodeTest(&evm_, names[i]));
        }
    }
    ~RtUnicastTest() {
        STLDeleteValues(&control_nodes_);
    }

    virtual void SetUp() {
        string config(BgpConfigGenerate());
        for (size_t i = 0; i < control_nodes_.size(); ++i) {
            control_nodes_[i]->BgpConfig(config);
        }
        WaitForBgpEstablished();
    }

    bool BgpSessionsEstablished() {
        for (size_t i = 0; i < control_nodes_.size(); ++i) {
            if (control_nodes_[i]->BgpEstablishedCount() < 3) {
                return false;
            }
        }
        return true;
    }

    void WaitForBgpEstablished() {
        SCOPED_TRACE("WaitForBgpEstablished");
        task_util::WaitForCondition(
            &evm_,
            boost::bind(&RtUnicastTest::BgpSessionsEstablished, this),
            kTimeoutSeconds);
        ASSERT_TRUE(BgpSessionsEstablished())
                << "[" << control_nodes_[0]->BgpEstablishedCount() << ", "
                << control_nodes_[1]->BgpEstablishedCount() << ", "
                << control_nodes_[2]->BgpEstablishedCount() << ", "
                << control_nodes_[3]->BgpEstablishedCount() << "]";
    }

    // Create the BGP peer mesh.
    std::string BgpConfigGenerate() {
        ostringstream oss;
        oss << "<config>";
        for (size_t i = 0; i < control_nodes_.size(); ++i) {
            test::ControlNodeTest *node = control_nodes_[i];
            oss << "<bgp-router name=\'" << node->localname() << "\'>"
                << "<identifier>192.168.0."<< i + 1 << "</identifier>"
                << "<port>" << node->bgp_port() << "</port>";
            for (size_t j = 0; j < control_nodes_.size(); ++j) {
                if (j == i) {
                    continue;
                }
                oss << "<session to=\'"
                    << control_nodes_[j]->localname() << "\'>"
                    << "<address-families>"
                    << "<family>inet-vpn</family>"
                    << "<family>erm-vpn</family>"
                    << "<family>route-target</family>"
                    << "</address-families>"
                    << "</session>";                
            }
            oss << "</bgp-router>";
        }
        oss << "</config>";
        return oss.str();
    }

    void ApplyNetworkConfiguration() {
        string netconf(
            bgp_util::NetworkConfigGenerate(instance_names_, connections_));
        for (size_t i = 0; i < control_nodes_.size(); ++i) {
            control_nodes_[i]->IFMapMessage(netconf);
        }
        task_util::WaitForIdle();
    }

    void AddConnection(const string &network1, const string &network2) {
        for (size_t i = 0; i < control_nodes_.size(); ++i) {
            ifmap_test_util::IFMapMsgLink(control_nodes_[i]->config_db(),
                                          "routing-instance", network1,
                                          "routing-instance", network2,
                                          "connection");
        }
    }

    void DeleteConnection(const string &network1, const string &network2) {
        for (size_t i = 0; i < control_nodes_.size(); ++i) {
            ifmap_test_util::IFMapMsgUnlink(control_nodes_[i]->config_db(),
                                            "routing-instance", network1,
                                            "routing-instance", network2,
                                            "connection");
        }
    }

    void AddRouteTarget(const string &network) {
        ostringstream target;
        target << "target:64496:" << GetInstanceId(network);
        for (size_t i = 0; i < control_nodes_.size(); ++i) {
            ifmap_test_util::IFMapMsgLink(control_nodes_[i]->config_db(),
                                          "routing-instance", network,
                                          "route-target", target.str(),
                                          "instance-target");
        }
    }

    void DeleteRouteTarget(const string &network) {
        ostringstream target;
        target << "target:64496:" << GetInstanceId(network);
        for (size_t i = 0; i < control_nodes_.size(); ++i) {
            ifmap_test_util::IFMapMsgUnlink(control_nodes_[i]->config_db(),
                                            "routing-instance", network,
                                            "route-target", target.str(),
                                            "instance-target");
        }
    }

    bool TestRouteCount(const string &network, size_t target) {
        for (size_t i = 0; i < control_nodes_.size(); ++i) {
            BgpServer *server = control_nodes_[i]->bgp_server();
            if (server->session_manager()->HasSessionReadAvailable()) {
                return false;
            }
            XmppServer *xmpp = control_nodes_[i]->xmpp_server();
            if (xmpp->HasSessionReadAvailable()) {
                return false;
            }
            RoutingInstance *rti = 
                    server->routing_instance_mgr()->GetRoutingInstance(network);
            if (rti == NULL) {
                return false;
            }
            BgpTable *table = rti->GetTable(Address::INET);
            if (table == NULL || table->Size() != target) {
                return false;
            }
        }
        return true;
    }

    void WaitForRouteCount(const string &network, size_t count) {
        task_util::WaitForCondition(
            &evm_,
            boost::bind(&RtUnicastTest::TestRouteCount, this, network, count),
            kTimeoutSeconds);
        EXPECT_TRUE(TestRouteCount(network, count));
    }

    bool TestVPNRouteCount(size_t target) {
        for (size_t i = 0; i < control_nodes_.size(); ++i) {
            BgpServer *server = control_nodes_[i]->bgp_server();
            if (server->session_manager()->HasSessionReadAvailable()) {
                return false;
            }
            XmppServer *xmpp = control_nodes_[i]->xmpp_server();
            if (xmpp->HasSessionReadAvailable()) {
                return false;
            }
            BgpTable *table = static_cast<BgpTable *>(
                    server->database()->FindTable("bgp.l3vpn.0"));
            if (table == NULL || table->Size() != target) {
                return false;
            }
        }
        return true;
    }

    void WaitForVPNRouteCount(size_t count) {
        task_util::WaitForCondition(
            &evm_,
            boost::bind(&RtUnicastTest::TestVPNRouteCount, this, count),
            kTimeoutSeconds);
        EXPECT_TRUE(TestVPNRouteCount(count));
    }

    const BgpRoute *VPNRouteLookup(int node_index, const string &prefix) {
        BgpServer *server = control_nodes_[node_index]->bgp_server();
        BgpTable *table = static_cast<BgpTable *>(
            server->database()->FindTable("bgp.l3vpn.0"));
        EXPECT_TRUE(table != NULL);
        if (table == NULL) {
            return NULL;
        }
        boost::system::error_code error;
        InetVpnPrefix nlri = InetVpnPrefix::FromString(prefix, &error);
        EXPECT_FALSE(error);
        InetVpnTable::RequestKey key(nlri, NULL);
        BgpRoute *rt = static_cast<BgpRoute *>(table->Find(&key));
        return rt;
    }

    const BgpRoute *RouteLookup(int node_index, const string &network,
                                const string &address, int prefixlen) {
        BgpServer *server = control_nodes_[node_index]->bgp_server();
        RoutingInstance *rti =
                server->routing_instance_mgr()->GetRoutingInstance(network);
        if (rti == NULL) {
            return NULL;
        }
        BgpTable *table = rti->GetTable(Address::INET);
        if (table == NULL) {
            return NULL;
        }
        boost::system::error_code ec;
        Ip4Prefix rt_prefix(Ip4Address::from_string(address, ec), prefixlen);
        InetTable::RequestKey key(rt_prefix, NULL);
        return static_cast<BgpRoute *>(table->Find(&key));
    }

    int GetInstanceId(const string &network) {
        vector<string>::iterator it =
            find(instance_names_.begin(), instance_names_.end(), network);
        if (it == instance_names_.end()) {
            return 0;
        } else {
            return (it - instance_names_.begin() + 1);
        }
    }

    void VerifyInstanceExists(const string &network) {
        for (size_t i = 0; i < control_nodes_.size(); ++i) {
            BgpServer *server = control_nodes_[i]->bgp_server();
            RoutingInstance *rti =
                    server->routing_instance_mgr()->GetRoutingInstance(network);
            ASSERT_TRUE(rti != NULL);
        }
    }

    void VerifyInstanceNoExists(const string &network) {
        for (size_t i = 0; i < control_nodes_.size(); ++i) {
            BgpServer *server = control_nodes_[i]->bgp_server();
            TASK_UTIL_EXPECT_EQ(static_cast<RoutingInstance *>(NULL),
                 server->routing_instance_mgr()->GetRoutingInstance(network));
        }
    }

    void VerifyRouteExists(const string &network, const string &address,
            int prefixlen) {
        for (size_t i = 0; i < control_nodes_.size(); ++i) {
            const BgpRoute *rt = RouteLookup(i, network, address, prefixlen);
            ASSERT_TRUE(rt != NULL);
            const BgpPath *rt_entry = rt->BestPath();
            EXPECT_TRUE(rt_entry != NULL);
        }
    }

    void VerifyRouteNoExists(const string &network, const string &address,
            int prefixlen) {
        for (size_t i = 0; i < control_nodes_.size(); ++i) {
            const BgpRoute *rt = RouteLookup(i, network, address, prefixlen);
            ASSERT_TRUE(rt == NULL);
        }
    }

    int RouteAdvertiseCount(int node_index, const string &tablename,
                            const BgpRoute *rt) {
        BgpServer *server = control_nodes_[node_index]->bgp_server();
        BgpTable *table = static_cast<BgpTable *>(
            server->database()->FindTable(tablename));
        if (table == NULL) {
            return -1;
        }
        int count = 0;
        for (BgpTable::RibOutMap::const_iterator iter =
                table->ribout_map().begin();
             iter != table->ribout_map().end(); ++iter) {
            count += iter->second->RouteAdvertiseCount(rt);
        }
        return count;
    }

    EventManager evm_;
    std::vector<test::ControlNodeTest *> control_nodes_;
    vector<string> instance_names_;
    multimap<string, string> connections_;
};

//
// There's a full mesh of connections between red, blue and yellow networks.
// Delete the red network.
//
TEST_F(RtUnicastTest, InstanceDelete) {
    instance_names_ = list_of("red")("blue")("yellow");
    connections_ =
        map_list_of("red", "blue")("red", "yellow")("blue", "yellow");

    ApplyNetworkConfiguration();

    // Delete all connections for red.
    BOOST_FOREACH(string network, instance_names_) {
        if (network == "red")
            continue;
        DeleteConnection("red", network);
    }

    // Delete the route target for red.
    DeleteRouteTarget("red");

    // Make sure the instance is gone.
    task_util::WaitForIdle();
    VerifyInstanceNoExists("red");
}

//
// There's a full mesh of connections between red and blue networks.
// Add the yellow network and make it part of the mesh.
//
TEST_F(RtUnicastTest, InstanceAdd) {
    instance_names_ = list_of("red")("blue");
    connections_ =
        map_list_of("red", "blue");

    ApplyNetworkConfiguration();

    instance_names_.push_back("yellow");
    AddRouteTarget("yellow");

    BOOST_FOREACH(string network, instance_names_) {
        if (network == "yellow")
            continue;
        AddConnection("yellow", network);
    }

    // Make sure the instance got created.
    task_util::WaitForIdle();
    VerifyInstanceExists("yellow");
}

// Simulate a dual connected agent.
class MultihomedAgentTest : public RtUnicastTest {
  protected:
    typedef boost::shared_ptr<test::NetworkAgentMock> AgentMockPtr;

    MultihomedAgentTest() : RtUnicastTest() {
    }

    virtual void SetUp() {
        RtUnicastTest::SetUp();

        XmppServerConnectionTest::SetEndpointOverride(
            boost::bind(&MultihomedAgentTest::EndpointOverride, this, _1));
        agent_a_.reset(
            new test::NetworkAgentMock(&evm_, "agent_ab",
                                       control_nodes_[0]->xmpp_port()));
        agent_b_.reset(
            new test::NetworkAgentMock(&evm_, "agent_ab",
                                       control_nodes_[1]->xmpp_port()));
        agents_.push_back(agent_a_);
        agents_.push_back(agent_b_);
        agent_a_->set_localaddr("192.0.2.1");
        agent_b_->set_localaddr("192.0.2.1");

        WaitForXmppEstablished();
    }

    virtual void TearDown() {
        XmppServerConnectionTest::ClearEndpointOverride();
    }

    tcp::endpoint EndpointOverride(const XmppServerConnection *connection) {
        tcp::endpoint remote;
        BOOST_FOREACH(AgentMockPtr agent, agents_) {
            if (connection->ToString() == agent->hostname()) {
                boost::system::error_code ec;
                remote.address(Ip4Address::from_string(agent->localaddr(), ec));
                return remote;
            }
        }
        boost::system::error_code ec;
        remote.address(Ip4Address::from_string("127.0.0.1", ec));
        return remote;
    }

    bool XmppSessionsEstablished() {
        for (size_t i = 0; i < agents_.size(); ++i) {
            if (!agents_[i]->IsEstablished()) {
                return false;
            }
        }
        return true;
    }

    void WaitForXmppEstablished() {
        SCOPED_TRACE("WaitForXmppEstablished");
        task_util::WaitForCondition(
            &evm_,
            boost::bind(&MultihomedAgentTest::XmppSessionsEstablished, this),
            kTimeoutSeconds);
        ASSERT_TRUE(XmppSessionsEstablished());
    }

    bool TestAgentRouteCount(const test::NetworkAgentMock *agent,
            size_t target) {
        return ((agent->RouteCount() != (int) target) ? false : true);
    }

    bool TestXmppUpdateCount(int count) {
        for (size_t i = 0; i < control_nodes_.size(); i++) {
            BgpXmppChannelManager *xmanager =
                    control_nodes_[i]->xmpp_channel_manager();
            for (BgpXmppChannelManager::XmppChannelMap::const_iterator iter =
                         xmanager->channel_map().begin();
                 iter != xmanager->channel_map().end(); ++iter) {
                if (iter->second->rx_stats().rt_updates < count) {
                    return false;
                }
            }
        }
        return true;
    }

    void WaitForAgentRouteCount(const test::NetworkAgentMock *agent,
            size_t count) {
        task_util::WaitForCondition(
            &evm_,
            boost::bind(&MultihomedAgentTest::TestAgentRouteCount, this,
                agent, count),
            kTimeoutSeconds);
        EXPECT_TRUE(TestAgentRouteCount(agent, count));
    }

    void WaitForXmppUpdate(int count) {
        task_util::WaitForCondition(
            &evm_,
            boost::bind(&MultihomedAgentTest::TestXmppUpdateCount, this, count),
            kTimeoutSeconds);
        EXPECT_TRUE(TestXmppUpdateCount(count));
    }

    AgentMockPtr agent_a_, agent_b_;
    vector<AgentMockPtr> agents_;
};

// attributes: next-hop and label.
TEST_F(MultihomedAgentTest, Attributes) {
    instance_names_ = list_of("red")("blue")("yellow")("green");
    connections_ = map_list_of("red", "blue");

    ApplyNetworkConfiguration();

    const char *net_1 = "red";
    agent_a_->Subscribe(net_1, 1);
    agent_a_->AddRoute(net_1, "10.0.1.1/32");
    agent_b_->Subscribe(net_1, 1);
    agent_b_->AddRoute(net_1, "10.0.1.1/32");
    task_util::BusyWork(&evm_, 1);

    WaitForRouteCount(net_1, 1);

    for (size_t i = 0;  i < control_nodes_.size(); ++i) {
        const BgpRoute *rt = RouteLookup(i, net_1, "10.0.1.1", 32);
        ASSERT_TRUE(rt != NULL);
        const BgpPath *rt_entry = rt->BestPath();
        EXPECT_TRUE(rt_entry != NULL);
        EXPECT_EQ("192.0.2.1", rt_entry->GetAttr()->nexthop().to_string());
        EXPECT_EQ(10000, rt_entry->GetLabel());
        if (i < 2) {
            EXPECT_EQ(1, rt->count());
            const IPeer *peer = rt_entry->GetPeer();
            EXPECT_TRUE(peer->IsXmppPeer())
                << "node " << i << " peer: " << peer->bgp_identifier();
            const BgpRoute *rt_vpn =
                VPNRouteLookup(i, "192.0.2.1:1:10.0.1.1/32");
            ASSERT_TRUE(rt_vpn != NULL);
            EXPECT_EQ(3, RouteAdvertiseCount(i, "bgp.l3vpn.0", rt_vpn));
        } else {
            EXPECT_EQ(1, rt->count());
            ASSERT_TRUE(rt_entry->IsReplicated());
            const BgpSecondaryPath *spath =
                static_cast<const BgpSecondaryPath *>(rt_entry);
            const BgpRoute *rt_primary = spath->src_rt();
            EXPECT_EQ(2, rt_primary->count())
                << "best-path from: "
                << rt_primary->BestPath()->GetPeer()->ToString();
            boost::system::error_code ec;
            Ip4Address originator = Ip4Address::from_string("192.0.2.1", ec);
            RouteDistinguisher expect_rd(originator.to_ulong(), 1);
            const InetVpnRoute *rt_vpn = static_cast<const InetVpnRoute *>(
                rt_primary);
            EXPECT_EQ(expect_rd, rt_vpn->GetPrefix().route_distinguisher());
        }
    }
}

// attributes: next-hop and label.
TEST_F(MultihomedAgentTest, UpdateRoute) {
    instance_names_ = list_of("red")("blue")("yellow")("green");
    connections_ = map_list_of("red", "blue");

    ApplyNetworkConfiguration();

    const char *net_1 = "red";
    agent_a_->Subscribe(net_1, 1);
    agent_a_->AddRoute(net_1, "10.0.1.1/32");
    agent_b_->Subscribe(net_1, 1);
    agent_b_->AddRoute(net_1, "10.0.1.1/32");

    WaitForRouteCount(net_1, 1);

    // Change the label
    agent_a_->AddRoute(net_1, "10.0.1.1/32");
    agent_b_->AddRoute(net_1, "10.0.1.1/32");
    task_util::BusyWork(&evm_, 1);
    WaitForXmppUpdate(2);

    for (size_t i = 0;  i < control_nodes_.size(); ++i) {
        const BgpRoute *rt = RouteLookup(i, net_1, "10.0.1.1", 32);
        ASSERT_TRUE(rt != NULL);
        const BgpPath *rt_entry = rt->BestPath();
        EXPECT_TRUE(rt_entry != NULL);
        EXPECT_EQ("192.0.2.1", rt_entry->GetAttr()->nexthop().to_string());
        EXPECT_EQ(10001, rt_entry->GetLabel())
                << "control-node: " << i;
    }
}

// attributes: next-hop and label.
TEST_F(MultihomedAgentTest, DeleteRoute) {
    instance_names_ = list_of("red")("blue")("yellow")("green");
    connections_ = map_list_of("red", "blue");

    ApplyNetworkConfiguration();

    const char *net_1 = "red";
    agent_a_->Subscribe(net_1, 1);
    agent_a_->AddRoute(net_1, "10.0.1.1/32");
    agent_b_->Subscribe(net_1, 1);
    agent_b_->AddRoute(net_1, "10.0.1.1/32");

    WaitForRouteCount(net_1, 1);

    agent_a_->DeleteRoute(net_1, "10.0.1.1/32");
    agent_b_->DeleteRoute(net_1, "10.0.1.1/32");

    WaitForRouteCount(net_1, 0);

    // Change the label
    agent_a_->AddRoute(net_1, "10.0.1.1/32");
    agent_b_->AddRoute(net_1, "10.0.1.1/32");

    WaitForRouteCount(net_1, 1);

    for (size_t i = 0;  i < control_nodes_.size(); ++i) {
        const BgpRoute *rt = RouteLookup(i, net_1, "10.0.1.1", 32);
        ASSERT_TRUE(rt != NULL);
        const BgpPath *rt_entry = rt->BestPath();
        EXPECT_TRUE(rt_entry != NULL);
        EXPECT_EQ("192.0.2.1", rt_entry->GetAttr()->nexthop().to_string());
        EXPECT_EQ(10001, rt_entry->GetLabel());
    }
}

//
// Agent has added routes in red and blue networks but there's no connection
// between the networks.
// When the connection is added, the routes should get leaked and show up in
// both networks.
// When the connection is deleted, the leaked routes should get cleaned up.
//
TEST_F(MultihomedAgentTest, ConnectionAddDelete) {
    SCOPED_TRACE("ConnectionAddDelete");
    instance_names_ = list_of("red")("blue");

    ApplyNetworkConfiguration();

    // Subscribe to red and blue.
    int instance_id = 1;
    BOOST_FOREACH(string network, instance_names_) {
        agent_a_->Subscribe(network, instance_id);
        agent_b_->Subscribe(network, instance_id);
        instance_id++;
    }

    // Advertise route to red and make sure it got added.
    agent_a_->AddRoute("red", "10.0.1.1/32");
    agent_b_->AddRoute("red", "10.0.1.1/32");
    WaitForRouteCount("red", 1);
    VerifyRouteExists("red", "10.0.1.1", 32);

    // Advertise route to blue and make sure it got added.
    agent_a_->AddRoute("blue", "20.0.1.1/32");
    agent_b_->AddRoute("blue", "20.0.1.1/32");
    WaitForRouteCount("blue", 1);
    VerifyRouteExists("blue", "20.0.1.1", 32);

    // Add connection between red and blue.
    AddConnection("red", "blue");

    // Make sure routes got leaked.
    BOOST_FOREACH(string network, instance_names_) {
        WaitForRouteCount(network, 2);
        VerifyRouteExists(network, "10.0.1.1", 32);
        VerifyRouteExists(network, "20.0.1.1", 32);
    }

    // Delete connection between red and blue.
    DeleteConnection("red", "blue");

    // Make sure leaked routes got cleaned up.
    BOOST_FOREACH(string network, instance_names_) {
        WaitForRouteCount(network, 1);
    }
    VerifyRouteNoExists("red", "20.0.1.1", 32);
    VerifyRouteNoExists("blue", "10.0.1.1", 32);
}

//
// Agent has subscribed to red, blue and yellow networks and added route to
// the red network.  However there's no connections between the networks.
// When the connections are added, the route should get leaked and show up
// the other networks.
//
TEST_F(MultihomedAgentTest, ConnectionAddMultiple1) {
    SCOPED_TRACE("ConnectionAddMultiple1");
    instance_names_ = list_of("red")("blue")("yellow");

    ApplyNetworkConfiguration();

    // Subscribe to red, blue and yellow.
    int instance_id = 1;
    BOOST_FOREACH(string network, instance_names_) {
        agent_a_->Subscribe(network, instance_id);
        agent_b_->Subscribe(network, instance_id);
        instance_id++;
    }

    // Add route to red.
    agent_a_->AddRoute("red", "10.0.1.1/32");
    agent_b_->AddRoute("red", "10.0.1.1/32");

    // Make sure route is present in red, not in blue and yellow.
    WaitForRouteCount("red", 1);
    VerifyRouteExists("red", "10.0.1.1", 32);
    WaitForRouteCount("blue", 0);
    VerifyRouteNoExists("blue", "10.0.1.1", 32);
    WaitForRouteCount("yellow", 0);
    VerifyRouteNoExists("yellow", "10.0.1.1", 32);

    // Add connection between red and blue.
    AddConnection("red", "blue");

    // Make sure route is present in red and blue, not in yellow.
    WaitForRouteCount("red", 1);
    VerifyRouteExists("red", "10.0.1.1", 32);
    WaitForRouteCount("blue", 1);
    VerifyRouteExists("blue", "10.0.1.1", 32);
    WaitForRouteCount("yellow", 0);
    VerifyRouteNoExists("yellow", "10.0.1.1", 32);

    // Add connection between red and yellow.
    AddConnection("red", "yellow");

    // Make sure route is present in red, blue and yellow.
    WaitForRouteCount("red", 1);
    VerifyRouteExists("red", "10.0.1.1", 32);
    WaitForRouteCount("blue", 1);
    VerifyRouteExists("blue", "10.0.1.1", 32);
    WaitForRouteCount("yellow", 1);
    VerifyRouteExists("yellow", "10.0.1.1", 32);

    // Add connection between blue and yellow.
    AddConnection("blue", "yellow");

    // Make sure no new routes got manufactured.
    BOOST_FOREACH(string network, instance_names_) {
        WaitForRouteCount(network, 1);
    }
}

//
// Agent has subscribed to red, blue and yellow networks and added a route to
// each of the networks. However there's no connections between the networks.
// When the connections are added, the routes should get leaked and show up
// the other networks.
//
TEST_F(MultihomedAgentTest, ConnectionAddMultiple2) {
    SCOPED_TRACE("ConnectionAddMultiple2");
    instance_names_ = list_of("red")("blue")("yellow");

    ApplyNetworkConfiguration();

    // Subscribe to red, blue and yellow.
    int instance_id = 1;
    BOOST_FOREACH(string network, instance_names_) {
        agent_a_->Subscribe(network, instance_id);
        agent_b_->Subscribe(network, instance_id);
        instance_id++;
    }

    // Add routes to red, blue and yellow.
    agent_a_->AddRoute("red", "10.0.1.1/32");
    agent_b_->AddRoute("red", "10.0.1.1/32");
    agent_a_->AddRoute("blue", "20.0.1.1/32");
    agent_b_->AddRoute("blue", "20.0.1.1/32");
    agent_a_->AddRoute("yellow", "30.0.1.1/32");
    agent_b_->AddRoute("yellow", "30.0.1.1/32");

    // Make sure routes haven't been leaked anywhere.
    BOOST_FOREACH(string network, instance_names_) {
        WaitForRouteCount(network, 1);
    }

    // Add connections (red, blue) (red, yellow) and (blue, yellow).
    AddConnection("red", "blue");
    AddConnection("red", "yellow");
    AddConnection("blue", "yellow");

    // Make sure routes are present in red, blue and yellow.
    BOOST_FOREACH(string network, instance_names_) {
        WaitForRouteCount(network, 3);
        VerifyRouteExists(network, "10.0.1.1", 32);
        VerifyRouteExists(network, "20.0.1.1", 32);
        VerifyRouteExists(network, "30.0.1.1", 32);
    }
}

//
// There's a full mesh of connections between red, blue and yellow networks.
// Agent has subscribed to all the networks and added a route to the red
// network.
// When the connections are deleted, leaked routes should get deleted from
// the other networks.
//
TEST_F(MultihomedAgentTest, ConnectionDeleteMultiple1) {
    SCOPED_TRACE("ConnectionDeleteMultiple1");
    instance_names_ = list_of("red")("blue")("yellow");
    connections_ =
        map_list_of("red", "blue")("red", "yellow")("blue", "yellow");

    ApplyNetworkConfiguration();

    // Subscribe to red, blue and yellow.
    int instance_id = 1;
    BOOST_FOREACH(string network, instance_names_) {
        agent_a_->Subscribe(network, instance_id);
        agent_b_->Subscribe(network, instance_id);
        instance_id++;
    }

    // Add route to red.
    agent_a_->AddRoute("red", "10.0.1.1/32");
    agent_b_->AddRoute("red", "10.0.1.1/32");

    // Delete connection between blue and yellow - should have no effect.
    DeleteConnection("blue", "yellow");

    // Make sure route is present in red, blue and yellow.
    WaitForRouteCount("red", 1);
    VerifyRouteExists("red", "10.0.1.1", 32);
    WaitForRouteCount("blue", 1);
    VerifyRouteExists("blue", "10.0.1.1", 32);
    WaitForRouteCount("yellow", 1);
    VerifyRouteExists("yellow", "10.0.1.1", 32);

    // Delete connection between red and blue.
    DeleteConnection("red", "blue");

    // Make sure route is present in red and yellow, not in blue.
    WaitForRouteCount("red", 1);
    VerifyRouteExists("red", "10.0.1.1", 32);
    WaitForRouteCount("yellow", 1);
    VerifyRouteExists("yellow", "10.0.1.1", 32);
    WaitForRouteCount("blue", 0);
    VerifyRouteNoExists("blue", "10.0.1.1", 32);

    // Delete connection between red and yellow.
    DeleteConnection("red", "yellow");

    // Make sure route is present only in red.
    WaitForRouteCount("red", 1);
    VerifyRouteExists("red", "10.0.1.1", 32);
    WaitForRouteCount("blue", 0);
    VerifyRouteNoExists("blue", "10.0.1.1", 32);
    WaitForRouteCount("yellow", 0);
    VerifyRouteNoExists("yellow", "10.0.1.1", 32);
}

//
// There's a full mesh of connections between red, blue and yellow networks.
// Agent has subscribed to red, blue and yellow networks and added a route to
// each of the networks.
// When the connections are deleted, leaked routes should get deleted from
// the other networks.
//
TEST_F(MultihomedAgentTest, ConnectionDeleteMultiple2) {
    SCOPED_TRACE("ConnectionDeleteMultiple2");
    instance_names_ = list_of("red")("blue")("yellow");
    connections_ =
        map_list_of("red", "blue")("red", "yellow")("blue", "yellow");

    ApplyNetworkConfiguration();

    // Subscribe to red, blue and yellow.
    int instance_id = 1;
    BOOST_FOREACH(string network, instance_names_) {
        agent_a_->Subscribe(network, instance_id);
        agent_b_->Subscribe(network, instance_id);
        instance_id++;
    }

    // Add routes to red, blue and yellow.
    agent_a_->AddRoute("red", "10.0.1.1/32");
    agent_b_->AddRoute("red", "10.0.1.1/32");
    agent_a_->AddRoute("blue", "20.0.1.1/32");
    agent_b_->AddRoute("blue", "20.0.1.1/32");
    agent_a_->AddRoute("yellow", "30.0.1.1/32");
    agent_b_->AddRoute("yellow", "30.0.1.1/32");

    // Make sure routes are present in red, blue and yellow.
    BOOST_FOREACH(string network, instance_names_) {
        WaitForRouteCount(network, 3);
        VerifyRouteExists(network, "10.0.1.1", 32);
        VerifyRouteExists(network, "20.0.1.1", 32);
        VerifyRouteExists(network, "30.0.1.1", 32);
    }

    // Delete connections (red, blue) (red, yellow) and (blue, yellow).
    DeleteConnection("red", "blue");
    DeleteConnection("red", "yellow");
    DeleteConnection("blue", "yellow");

    // Make sure routes are not leaked anymore.
    BOOST_FOREACH(string network, instance_names_) {
        WaitForRouteCount(network, 1);
    }
}

//
// There's a full mesh of connections between red, blue and yellow networks.
// Agent has subscribed and added routes to red but has not subscribed to the
// other networks.
// When the agent subscribes to the other networks later, it should receive
// the routes.
//
TEST_F(MultihomedAgentTest, Subscribe) {
    SCOPED_TRACE("Subscribe");
    instance_names_ = list_of("red")("blue")("yellow");
    connections_ =
        map_list_of("red", "blue")("red", "yellow")("blue", "yellow");

    ApplyNetworkConfiguration();

    // Subscribe and advertise routes to red.
    agent_a_->Subscribe("red", 1);
    agent_b_->Subscribe("red", 1);
    agent_a_->AddRoute("red", "10.0.1.1/32");
    agent_b_->AddRoute("red", "10.0.1.1/32");
    agent_a_->AddRoute("red", "10.0.1.2/32");
    agent_b_->AddRoute("red", "10.0.1.2/32");
    agent_a_->AddRoute("red", "10.0.1.3/32");
    agent_b_->AddRoute("red", "10.0.1.3/32");

    // Wait for the routes to be refelected back.
    WaitForAgentRouteCount(agent_a_.get(), 3);
    WaitForAgentRouteCount(agent_b_.get(), 3);

    // Subscribe to the other networks.
    int instance_id = 2;
    BOOST_FOREACH(string network, instance_names_) {
        if (network == "red")
            continue;
        agent_a_->Subscribe(network, instance_id);
        agent_b_->Subscribe(network, instance_id);
        instance_id++;
    }

    // Make sure we have all the routes.
    BOOST_FOREACH(string network, instance_names_) {
        WaitForAgentRouteCount(agent_a_.get(), 3 * instance_names_.size());
        WaitForAgentRouteCount(agent_b_.get(), 3 * instance_names_.size());
        EXPECT_TRUE(agent_a_->RouteLookup(network, "10.0.1.1/32") != NULL);
        EXPECT_TRUE(agent_b_->RouteLookup(network, "10.0.1.1/32") != NULL);
        EXPECT_TRUE(agent_a_->RouteLookup(network, "10.0.1.2/32") != NULL);
        EXPECT_TRUE(agent_b_->RouteLookup(network, "10.0.1.2/32") != NULL);
        EXPECT_TRUE(agent_a_->RouteLookup(network, "10.0.1.3/32") != NULL);
        EXPECT_TRUE(agent_b_->RouteLookup(network, "10.0.1.3/32") != NULL);
    }
}

//
// There's a full mesh of connections between red, blue and yellow networks.
// Agent has subscribed to all networks and added routes to red.
// When the agent unsubscribes from the red network, the leaked routes should
// disappear.
//
TEST_F(MultihomedAgentTest, Unsubscribe) {
    SCOPED_TRACE("Unsubscribe");
    instance_names_ = list_of("red")("blue")("yellow");
    connections_ =
        map_list_of("red", "blue")("red", "yellow")("blue", "yellow");

    ApplyNetworkConfiguration();

    // Subscribe to all the networks.
    int instance_id = 1;
    BOOST_FOREACH(string network, instance_names_) {
        agent_a_->Subscribe(network, instance_id);
        agent_b_->Subscribe(network, instance_id);
        instance_id++;
    }

    // Advertise routes to red.
    agent_a_->AddRoute("red", "10.0.1.1/32");
    agent_b_->AddRoute("red", "10.0.1.1/32");
    agent_a_->AddRoute("red", "10.0.1.2/32");
    agent_b_->AddRoute("red", "10.0.1.2/32");

    // Make sure routes got leaked.
    BOOST_FOREACH(string network, instance_names_) {
        WaitForRouteCount(network, 2);
    }

    // Wait for the routes to be reflected back.
    WaitForAgentRouteCount(agent_a_.get(), 2 * instance_names_.size());
    WaitForAgentRouteCount(agent_b_.get(), 2 * instance_names_.size());

    // Unsubscribe from the red network.
    agent_a_->Unsubscribe("red");
    agent_b_->Unsubscribe("red");

    // Make sure routes got un-leaked.
    BOOST_FOREACH(string network, instance_names_) {
        WaitForRouteCount(network, 0);
    }
}

TEST_F(MultihomedAgentTest, SessionBounce) {
    SCOPED_TRACE("SessionBounce");
    instance_names_ = list_of("red")("blue")("yellow");
    connections_ =
        map_list_of("red", "blue")("red", "yellow")("blue", "yellow");

    ApplyNetworkConfiguration();

    // Subscribe to all the networks.
    int instance_id = 1;
    BOOST_FOREACH(string network, instance_names_) {
        agent_a_->Subscribe(network, instance_id);
        agent_b_->Subscribe(network, instance_id);
        instance_id++;
    }

    // Advertise routes to red.
    agent_a_->AddRoute("red", "10.0.1.1/32");
    agent_b_->AddRoute("red", "10.0.1.1/32");
    agent_a_->AddRoute("red", "10.0.1.2/32");
    agent_b_->AddRoute("red", "10.0.1.2/32");

    // Make sure routes got leaked.
    BOOST_FOREACH(string network, instance_names_) {
        WaitForRouteCount(network, 2);
    }

    // Wait for the routes to be reflected back.
    WaitForAgentRouteCount(agent_a_.get(), 2 * instance_names_.size());
    WaitForAgentRouteCount(agent_b_.get(), 2 * instance_names_.size());

    // Bring down one of the sessions.
    agent_a_->SessionDown();
    task_util::BusyWork(&evm_, 1);

    // Make sure routes are still present.
    BOOST_FOREACH(string network, instance_names_) {
        WaitForRouteCount(network, 2);
        WaitForAgentRouteCount(agent_b_.get(), 2 * instance_names_.size());
    }

    // Bring the session up.
    agent_a_->SessionUp();
    WaitForXmppEstablished();
    task_util::BusyWork(&evm_, 1);

    // Subscribe to all the networks.
    instance_id = 1;
    BOOST_FOREACH(string network, instance_names_) {
        agent_a_->Subscribe(network, instance_id);
        instance_id++;
    }
    task_util::BusyWork(&evm_, 1);
    WaitForAgentRouteCount(agent_a_.get(), 2 * instance_names_.size());
}

//
// There's a full mesh of connections between red, blue and yellow networks.
// Agent has subscribed has subscribed to all networks and added routes to red.
// A new dual homed agent connects to a different set of control nodes and
// subscribes to all the other networks. It should receive all the routes.
//
TEST_F(MultihomedAgentTest, ConnectLater) {
    SCOPED_TRACE("ConnectLater");
    instance_names_ = list_of("red")("blue")("yellow");
    connections_ =
        map_list_of("red", "blue")("red", "yellow")("blue", "yellow");

    ApplyNetworkConfiguration();

    // Subscribe to all networks.
    int instance_id = 1;
    BOOST_FOREACH(string network, instance_names_) {
        agent_a_->Subscribe(network, instance_id);
        agent_b_->Subscribe(network, instance_id);
        instance_id++;
    }

    // Add routes to red.
    agent_a_->AddRoute("red", "10.0.1.1/32");
    agent_b_->AddRoute("red", "10.0.1.1/32");
    agent_a_->AddRoute("red", "10.0.1.2/32");
    agent_b_->AddRoute("red", "10.0.1.2/32");

    // Wait for the routes to be refelected back.
    WaitForAgentRouteCount(agent_a_.get(), 2 * instance_names_.size());
    WaitForAgentRouteCount(agent_b_.get(), 2 * instance_names_.size());

    // Create a new dual homed agent that connects to control nodes 1 and 2.
    AgentMockPtr agent_x, agent_y;
    agent_x.reset(
        new test::NetworkAgentMock(&evm_, "agent_xy",
                                   control_nodes_[1]->xmpp_port()));
    agent_y.reset(
        new test::NetworkAgentMock(&evm_, "agent_xy",
                                   control_nodes_[2]->xmpp_port()));
    agents_.push_back(agent_x);
    agents_.push_back(agent_y);
    agent_x->set_localaddr("192.0.2.2");
    agent_y->set_localaddr("192.0.2.2");

    // Wait for the the new agent to establish connections.
    WaitForXmppEstablished();

    // Subscribe the new agent to all networks.
    instance_id = 1;
    BOOST_FOREACH(string network, instance_names_) {
        agent_x->Subscribe(network, instance_id);
        agent_y->Subscribe(network, instance_id);
        instance_id++;
    }

    // Wait for the routes to be refelected back to the new agent.
    WaitForAgentRouteCount(agent_x.get(), 2 * instance_names_.size());
    WaitForAgentRouteCount(agent_y.get(), 2 * instance_names_.size());

    // Make sure we have all the routes.
    BOOST_FOREACH(string network, instance_names_) {
        EXPECT_TRUE(agent_x->RouteLookup(network, "10.0.1.1/32") != NULL);
        EXPECT_TRUE(agent_y->RouteLookup(network, "10.0.1.1/32") != NULL);
        EXPECT_TRUE(agent_x->RouteLookup(network, "10.0.1.2/32") != NULL);
        EXPECT_TRUE(agent_y->RouteLookup(network, "10.0.1.2/32") != NULL);
    }
}

//
// There's a full mesh of connections between red, blue and yellow networks.
// Agent has advertised routes in the red network.
// Delete the red network.
//
TEST_F(MultihomedAgentTest, InstanceDelete) {
    SCOPED_TRACE("InstanceDelete");
    instance_names_ = list_of("red")("blue")("yellow");
    connections_ =
        map_list_of("red", "blue")("red", "yellow")("blue", "yellow");

    ApplyNetworkConfiguration();

    // Subscribe to all networks.
    int instance_id = 1;
    BOOST_FOREACH(string network, instance_names_) {
        agent_a_->Subscribe(network, instance_id);
        agent_b_->Subscribe(network, instance_id);
        instance_id++;
    }

    // Add routes to red.
    BOOST_FOREACH(AgentMockPtr agent, agents_) {
        agent->AddRoute("red", "10.0.1.1/32");
        agent->AddRoute("red", "10.0.1.2/32");
    }

    // Wait for the routes to be refelected back.
    BOOST_FOREACH(AgentMockPtr agent, agents_) {
        WaitForAgentRouteCount(agent.get(), 2 * instance_names_.size());
    }

    // Delete all connections for red.
    BOOST_FOREACH(string network, instance_names_) {
        if (network == "red")
            continue;
        DeleteConnection("red", network);
    }

    // Delete the route target for red.
    DeleteRouteTarget("red");

    // Unsubscribe all agents from the red network.
    BOOST_FOREACH(AgentMockPtr agent, agents_) {
        agent->Unsubscribe("red");
    }
    task_util::BusyWork(&evm_, 1);

    // Make sure the instance is gone.
    VerifyInstanceNoExists("red");

    // Make sure routes got un-leaked.
    BOOST_FOREACH(string network, instance_names_) {
        if (network == "red")
            continue;
        WaitForRouteCount(network, 0);
    }

    // Wait for the routes to be retracted.
    // WaitForAgentRouteCount(agent_a_.get(), 2);
    // WaitForAgentRouteCount(agent_b_.get(), 2);
}

//
// There's a full mesh of connections between red and blue networks.
// Agent has advertised routes in the red network.
// Add the yellow network and make it part of the mesh.
//
TEST_F(MultihomedAgentTest, InstanceAdd) {
    SCOPED_TRACE("InstanceAdd");
    instance_names_ = list_of("red")("blue");
    connections_ =
        map_list_of("red", "blue");

    ApplyNetworkConfiguration();

    // Subscribe to all networks.
    int instance_id = 1;
    BOOST_FOREACH(string network, instance_names_) {
        agent_a_->Subscribe(network, instance_id);
        agent_b_->Subscribe(network, instance_id);
        instance_id++;
    }

    // Add routes to red.
    agent_a_->AddRoute("red", "10.0.1.1/32");
    agent_b_->AddRoute("red", "10.0.1.1/32");
    agent_a_->AddRoute("red", "10.0.1.2/32");
    agent_b_->AddRoute("red", "10.0.1.2/32");

    // Wait for the routes to be refelected back.
    WaitForAgentRouteCount(agent_a_.get(), 2 * instance_names_.size());
    WaitForAgentRouteCount(agent_b_.get(), 2 * instance_names_.size());

    // Add yellow and connect it to the other networks.
    instance_names_.push_back("yellow");
    AddRouteTarget("yellow");
    BOOST_FOREACH(string network, instance_names_) {
        if (network == "yellow")
            continue;
        AddConnection("yellow", network);
    }

    // Make sure the instance got created.
    task_util::WaitForIdle();
    VerifyInstanceExists("yellow");

    // Subscribe to yellow.
    agent_a_->Subscribe("yellow", instance_id);
    agent_b_->Subscribe("yellow", instance_id);

    // Wait for the routes to be refelected back.
    WaitForAgentRouteCount(agent_a_.get(), 2 * instance_names_.size());
    WaitForAgentRouteCount(agent_b_.get(), 2 * instance_names_.size());
}

// Simulate 2 dual connected agents.
class Multihomed2AgentTest : public MultihomedAgentTest {
  protected:
    Multihomed2AgentTest() : MultihomedAgentTest() {
    }

    virtual void SetUp() {
        MultihomedAgentTest::SetUp();

        agent_x_.reset(
            new test::NetworkAgentMock(&evm_, "agent_xy",
                                       control_nodes_[2]->xmpp_port()));
        agent_y_.reset(
            new test::NetworkAgentMock(&evm_, "agent_xy",
                                       control_nodes_[3]->xmpp_port()));
        agents_.push_back(agent_x_);
        agents_.push_back(agent_y_);
        agent_x_->set_localaddr("192.0.2.2");
        agent_y_->set_localaddr("192.0.2.2");

        WaitForXmppEstablished();
    }

    virtual void TearDown() {
        MultihomedAgentTest::TearDown();
    }

    AgentMockPtr agent_x_, agent_y_;
};

// Advertise the same route from agent_ab and agent_xy.
// Make sure that these show up as 2 different VPN routes.
TEST_F(Multihomed2AgentTest, Basic) {
    SCOPED_TRACE("Basic");
    instance_names_ = list_of("red");

    ApplyNetworkConfiguration();

    // Subscribe and add the same route from all agents.
    BOOST_FOREACH(AgentMockPtr agent, agents_) {
        agent->Subscribe("red", 1);
        agent->AddRoute("red", "10.0.1.1/32");
    }

    // Wait for the routes to be refelected back.
    BOOST_FOREACH(AgentMockPtr agent, agents_) {
        WaitForAgentRouteCount(agent.get(), 1);
    }

    // Make sure there are 2 VPN routes.
    WaitForVPNRouteCount(2);
}

// Advertise the same route from agent_ab and agent_xy.
// Make sure that these show up as 2 different VPN routes.
// Bring the sessions on the agents down and and verify the VPN routes.
TEST_F(Multihomed2AgentTest, SessionDown) {
    SCOPED_TRACE("SessionDown");
    instance_names_ = list_of("red");

    ApplyNetworkConfiguration();

    // Subscribe and add the same route from all agents.
    BOOST_FOREACH(AgentMockPtr agent, agents_) {
        agent->Subscribe("red", 1);
        agent->AddRoute("red", "10.0.1.1/32");
    }
    task_util::BusyWork(&evm_, 1);

    // Wait for the routes to be refelected back.
    BOOST_FOREACH(AgentMockPtr agent, agents_) {
        WaitForAgentRouteCount(agent.get(), 1);
    }

    // Make sure there are 2 VPN routes.
    WaitForVPNRouteCount(2);

    // Bring down 1 session on each of the 2 multihomed agents and make sure
    // we still have 2 VPN routes.
    agent_a_->SessionDown();
    agent_x_->SessionDown();
    task_util::BusyWork(&evm_, 1);
    WaitForVPNRouteCount(2);

    // Now bring down the other session on one of the agents and make sure we
    // have only 1 VPN route.
    agent_b_->SessionDown();
    task_util::BusyWork(&evm_, 1);
    WaitForVPNRouteCount(1);

    // Bring down the last session and make sure all VPN routes are gone.
    agent_y_->SessionDown();
    WaitForVPNRouteCount(0);
}

class RandomEventTest : public ::testing::Test {
};

class TestEnvironment : public ::testing::Environment {
    virtual ~TestEnvironment() { }
};

static void SetUp() {
    ControlNode::SetDefaultSchedulingPolicy();
    BgpServerTest::GlobalSetUp();
    XmppObjectFactory::Register<XmppServerConnection>(
        boost::factory<XmppServerConnectionTest *>());
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
