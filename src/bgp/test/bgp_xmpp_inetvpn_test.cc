/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <sstream>
#include <boost/assign/list_of.hpp>

#include "base/test/task_test_util.h"
#include "bgp/bgp_factory.h"
#include "bgp/bgp_session_manager.h"
#include "bgp/bgp_xmpp_channel.h"
#include "bgp/security_group/security_group.h"
#include "bgp/test/bgp_server_test_util.h"
#include "bgp/xmpp_message_builder.h"
#include "control-node/control_node.h"
#include "control-node/test/network_agent_mock.h"
#include "io/test/event_manager_test.h"
#include "ifmap/test/ifmap_test_util.h"
#include "schema/xmpp_unicast_types.h"
#include "testing/gunit.h"
#include "xmpp/xmpp_factory.h"
#include "xmpp/xmpp_server.h"

using namespace std;
using boost::assign::list_of;

static const char *config_2_control_nodes_4vns = "\
<config>\
    <bgp-router name=\'X\'>\
        <identifier>192.168.0.1</identifier>\
        <address>127.0.0.1</address>\
        <port>%d</port>\
        <session to=\'Y\'>\
            <address-families>\
                <family>route-target</family>\
                <family>inet-vpn</family>\
            </address-families>\
        </session>\
    </bgp-router>\
    <bgp-router name=\'Y\'>\
        <identifier>192.168.0.2</identifier>\
        <address>127.0.0.2</address>\
        <port>%d</port>\
        <session to=\'X\'>\
            <address-families>\
                <family>route-target</family>\
                <family>inet-vpn</family>\
            </address-families>\
        </session>\
    </bgp-router>\
    <virtual-network name='blue'>\
        <network-id>1</network-id>\
    </virtual-network>\
    <routing-instance name='blue'>\
        <virtual-network>blue</virtual-network>\
        <vrf-target>target:1:1</vrf-target>\
    </routing-instance>\
    <virtual-network name='pink'>\
        <network-id>1</network-id>\
    </virtual-network>\
    <routing-instance name='pink'>\
        <virtual-network>pink</virtual-network>\
        <vrf-target>target:1:2</vrf-target>\
    </routing-instance>\
    <virtual-network name='green'>\
        <network-id>1</network-id>\
    </virtual-network>\
    <routing-instance name='green'>\
        <virtual-network>green</virtual-network>\
        <vrf-target>target:1:3</vrf-target>\
    </routing-instance>\
    <virtual-network name='black'>\
        <network-id>1</network-id>\
    </virtual-network>\
    <routing-instance name='black'>\
        <virtual-network>black</virtual-network>\
        <vrf-target>target:1:4</vrf-target>\
    </routing-instance>\
</config>\
";

static const char *config_2_control_nodes = "\
<config>\
    <bgp-router name=\'X\'>\
        <identifier>192.168.0.1</identifier>\
        <address>127.0.0.1</address>\
        <port>%d</port>\
        <session to=\'Y\'>\
            <address-families>\
                <family>route-target</family>\
                <family>inet-vpn</family>\
            </address-families>\
        </session>\
    </bgp-router>\
    <bgp-router name=\'Y\'>\
        <identifier>192.168.0.2</identifier>\
        <address>127.0.0.2</address>\
        <port>%d</port>\
        <session to=\'X\'>\
            <address-families>\
                <family>route-target</family>\
                <family>inet-vpn</family>\
            </address-families>\
        </session>\
    </bgp-router>\
    <virtual-network name='blue'>\
        <network-id>1</network-id>\
    </virtual-network>\
    <routing-instance name='blue'>\
        <virtual-network>blue</virtual-network>\
        <vrf-target>target:1:1</vrf-target>\
    </routing-instance>\
</config>\
";

static const char *config_2_control_nodes_no_vn_info = "\
<config>\
    <bgp-router name=\'X\'>\
        <identifier>192.168.0.1</identifier>\
        <address>127.0.0.1</address>\
        <port>%d</port>\
        <session to=\'Y\'>\
            <address-families>\
                <family>route-target</family>\
                <family>inet-vpn</family>\
            </address-families>\
        </session>\
    </bgp-router>\
    <bgp-router name=\'Y\'>\
        <identifier>192.168.0.2</identifier>\
        <address>127.0.0.2</address>\
        <port>%d</port>\
        <session to=\'X\'>\
            <address-families>\
                <family>route-target</family>\
                <family>inet-vpn</family>\
            </address-families>\
        </session>\
    </bgp-router>\
    <routing-instance name='blue'>\
        <vrf-target>target:1:1</vrf-target>\
    </routing-instance>\
</config>\
";

static const char *config_2_control_nodes_different_asn = "\
<config>\
    <bgp-router name=\'X\'>\
        <autonomous-system>64511</autonomous-system>\
        <identifier>192.168.0.1</identifier>\
        <address>127.0.0.1</address>\
        <port>%d</port>\
        <session to=\'Y\'>\
            <address-families>\
                <family>route-target</family>\
                <family>inet-vpn</family>\
            </address-families>\
        </session>\
    </bgp-router>\
    <bgp-router name=\'Y\'>\
        <autonomous-system>64512</autonomous-system>\
        <identifier>192.168.0.2</identifier>\
        <address>127.0.0.2</address>\
        <port>%d</port>\
        <session to=\'X\'>\
            <address-families>\
                <family>route-target</family>\
                <family>inet-vpn</family>\
            </address-families>\
        </session>\
    </bgp-router>\
    <virtual-network name='blue'>\
        <network-id>1</network-id>\
    </virtual-network>\
    <routing-instance name='blue'>\
        <virtual-network>blue</virtual-network>\
        <vrf-target>target:1:1</vrf-target>\
    </routing-instance>\
</config>\
";

//
// Control Nodes X and Y.
// Agents A and B.
//
class BgpXmppInetvpn2ControlNodeTest : public ::testing::Test {
protected:
    static const int kRouteCount = 512;

    BgpXmppInetvpn2ControlNodeTest()
        : thread_(&evm_), xs_x_(NULL), xs_y_(NULL) {
    }

    virtual void SetUp() {
        bs_x_.reset(new BgpServerTest(&evm_, "X"));
        bs_x_->session_manager()->Initialize(0);
        LOG(DEBUG, "Created BGP server at port: " <<
            bs_x_->session_manager()->GetPort());
        xs_x_ = new XmppServer(&evm_, test::XmppDocumentMock::kControlNodeJID);
        xs_x_->Initialize(0, false);
        LOG(DEBUG, "Created XMPP server at port: " <<
            xs_x_->GetPort());
        cm_x_.reset(new BgpXmppChannelManager(xs_x_, bs_x_.get()));

        bs_y_.reset(new BgpServerTest(&evm_, "Y"));
        bs_y_->session_manager()->Initialize(0);
        LOG(DEBUG, "Created BGP server at port: " <<
            bs_y_->session_manager()->GetPort());
        xs_y_ = new XmppServer(&evm_, test::XmppDocumentMock::kControlNodeJID);
        xs_y_->Initialize(0, false);
        LOG(DEBUG, "Created XMPP server at port: " <<
            xs_y_->GetPort());
        cm_y_.reset(new BgpXmppChannelManager(xs_y_, bs_y_.get()));

        thread_.Start();
    }

    virtual void TearDown() {
        xs_x_->Shutdown();
        task_util::WaitForIdle();
        bs_x_->Shutdown();
        task_util::WaitForIdle();
        cm_x_.reset();

        xs_y_->Shutdown();
        task_util::WaitForIdle();
        bs_y_->Shutdown();
        task_util::WaitForIdle();
        cm_y_.reset();

        TcpServerManager::DeleteServer(xs_x_);
        xs_x_ = NULL;
        TcpServerManager::DeleteServer(xs_y_);
        xs_y_ = NULL;

        if (agent_a_) { agent_a_->Delete(); }
        if (agent_b_) { agent_b_->Delete(); }

        evm_.Shutdown();
        thread_.Join();
        task_util::WaitForIdle();
    }

    void Configure(BgpServerTestPtr server, const char *cfg_template) {
        char config[4096];
        snprintf(config, sizeof(config), cfg_template,
                 server->session_manager()->GetPort());
        server->Configure(config);
    }

    void Configure(const char *cfg_template = config_2_control_nodes) {
        char config[4096];
        snprintf(config, sizeof(config), cfg_template,
                 bs_x_->session_manager()->GetPort(),
                 bs_y_->session_manager()->GetPort());
        bs_x_->Configure(config);
        bs_y_->Configure(config);
    }

    void AddRouteTarget(BgpServerTestPtr server, const string name,
        const string target) {
        TASK_UTIL_EXPECT_NE(static_cast<RoutingInstance *>(NULL),
            server->routing_instance_mgr()->GetRoutingInstance(name));

        ifmap_test_util::IFMapMsgLink(server->config_db(),
                                      "routing-instance", name,
                                      "route-target", target,
                                      "instance-target");
    }

    void RemoveRouteTarget(BgpServerTestPtr server, const string name,
        const string target) {
        TASK_UTIL_EXPECT_NE(static_cast<RoutingInstance *>(NULL),
            server->routing_instance_mgr()->GetRoutingInstance(name));

        ifmap_test_util::IFMapMsgUnlink(server->config_db(),
                                      "routing-instance", name,
                                      "route-target", target,
                                      "instance-target");
    }

    void AddConnection(BgpServerTestPtr server,
        const string &name1, const string &name2) {
        ifmap_test_util::IFMapMsgLink(server->config_db(),
                                      "routing-instance", name1,
                                      "routing-instance", name2,
                                      "connection");
    }

    void RemoveConnection(BgpServerTestPtr server,
        const string &name1, const string &name2) {
        ifmap_test_util::IFMapMsgUnlink(server->config_db(),
                                        "routing-instance", name1,
                                        "routing-instance", name2,
                                        "connection");
    }

    size_t GetExportRouteTargetListSize(BgpServerTestPtr server,
        const string &instance) {
        TASK_UTIL_EXPECT_NE(static_cast<RoutingInstance *>(NULL),
            server->routing_instance_mgr()->GetRoutingInstance(instance));
        RoutingInstance *rti =
            server->routing_instance_mgr()->GetRoutingInstance(instance);
        task_util::WaitForIdle();
        return rti->GetExportList().size();
    }

    size_t GetImportRouteTargetListSize(BgpServerTestPtr server,
        const string &instance) {
        TASK_UTIL_EXPECT_NE(static_cast<RoutingInstance *>(NULL),
            server->routing_instance_mgr()->GetRoutingInstance(instance));
        RoutingInstance *rti =
            server->routing_instance_mgr()->GetRoutingInstance(instance);
        task_util::WaitForIdle();
        return rti->GetImportList().size();
    }

    bool CheckEncap(autogen::TunnelEncapsulationListType &rt_encap,
        const string &encap) {
        if (rt_encap.tunnel_encapsulation.size() != 1)
            return false;
        return (rt_encap.tunnel_encapsulation[0] == encap);
    }

    bool CheckRoute(test::NetworkAgentMockPtr agent, string net,
        string prefix, string nexthop, int local_pref, const string &encap,
        const string &origin_vn, const vector<int> sgids) {
        const autogen::ItemType *rt = agent->RouteLookup(net, prefix);
        if (!rt)
            return false;
        if (rt->entry.next_hops.next_hop[0].address != nexthop)
            return false;
        if (local_pref && rt->entry.local_preference != local_pref)
            return false;
        if (!origin_vn.empty() && rt->entry.virtual_network != origin_vn)
            return false;
        if (!sgids.empty() &&
            rt->entry.security_group_list.security_group != sgids)
            return false;

        autogen::TunnelEncapsulationListType rt_encap =
            rt->entry.next_hops.next_hop[0].tunnel_encapsulation_list;
        if (!encap.empty() && !CheckEncap(rt_encap, encap))
            return false;
        return true;
    }

    void VerifyRouteExists(test::NetworkAgentMockPtr agent, string net,
        string prefix, string nexthop, int local_pref = 100,
        const string &encap = string(), const string &origin_vn = string(),
        const vector<int> sgids = vector<int>()) {
        TASK_UTIL_EXPECT_TRUE(CheckRoute(
            agent, net, prefix, nexthop, local_pref, encap, origin_vn, sgids));
    }

    void VerifyRouteExists(test::NetworkAgentMockPtr agent, string net,
        string prefix, string nexthop, const vector<int> sgids) {
        TASK_UTIL_EXPECT_TRUE(CheckRoute(
            agent, net, prefix, nexthop, 0, string(), string(), sgids));
    }

    void VerifyRouteNoExists(test::NetworkAgentMockPtr agent, string net,
        string prefix) {
        TASK_UTIL_EXPECT_TRUE(agent->RouteLookup(net, prefix) == NULL);
    }

    const BgpPeer *VerifyPeerExists(BgpServerTestPtr s1, BgpServerTestPtr s2) {
        const char *master = BgpConfigManager::kMasterInstance;
        string name = string(master) + ":" + s2->localname();
        TASK_UTIL_EXPECT_TRUE(s1->FindMatchingPeer(master, name) != NULL);
        return s1->FindMatchingPeer(master, name);
    }

    EventManager evm_;
    ServerThread thread_;
    BgpServerTestPtr bs_x_;
    BgpServerTestPtr bs_y_;
    XmppServer *xs_x_;
    XmppServer *xs_y_;
    test::NetworkAgentMockPtr agent_a_;
    test::NetworkAgentMockPtr agent_b_;
    boost::scoped_ptr<BgpXmppChannelManager> cm_x_;
    boost::scoped_ptr<BgpXmppChannelManager> cm_y_;
};

static string BuildPrefix(uint32_t idx) {
    assert(idx <= 65535);
    string prefix = string("10.1.") +
        integerToString(idx / 255) + "." + integerToString(idx % 255) + "/32";
    return prefix;
}

//
// Added route has expected local preference.
//
TEST_F(BgpXmppInetvpn2ControlNodeTest, RouteAddLocalPref) {
    Configure();
    task_util::WaitForIdle();

    // Create XMPP Agent A connected to XMPP server X.
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, "agent-a", xs_x_->GetPort(),
            "127.0.0.1", "127.0.0.1"));
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    // Create XMPP Agent B connected to XMPP server Y.
    agent_b_.reset(
        new test::NetworkAgentMock(&evm_, "agent-b", xs_y_->GetPort(),
            "127.0.0.2", "127.0.0.2"));
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());

    // Register to blue instance
    agent_a_->Subscribe("blue", 1);
    agent_b_->Subscribe("blue", 1);

    // Add route from agent A.
    stringstream route_a;
    route_a << "10.1.1.1/32";
    agent_a_->AddRoute("blue", route_a.str(), "192.168.1.1", 200);
    task_util::WaitForIdle();

    // Verify that route showed up on agents A and B.
    VerifyRouteExists(agent_a_, "blue", route_a.str(), "192.168.1.1", 200);
    VerifyRouteExists(agent_b_, "blue", route_a.str(), "192.168.1.1", 200);

    // Delete route from agent A.
    agent_a_->DeleteRoute("blue", route_a.str());
    task_util::WaitForIdle();

    // Verify that route is deleted at agents A and B.
    VerifyRouteNoExists(agent_a_, "blue", route_a.str());
    VerifyRouteNoExists(agent_b_, "blue", route_a.str());

    // Close the sessions.
    agent_a_->SessionDown();
    agent_b_->SessionDown();
}

//
// Changed route has expected local preference.
//
TEST_F(BgpXmppInetvpn2ControlNodeTest, RouteChangeLocalPref) {
    Configure();
    task_util::WaitForIdle();

    // Create XMPP Agent A connected to XMPP server X.
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, "agent-a", xs_x_->GetPort(),
            "127.0.0.1", "127.0.0.1"));
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    // Create XMPP Agent B connected to XMPP server Y.
    agent_b_.reset(
        new test::NetworkAgentMock(&evm_, "agent-b", xs_y_->GetPort(),
            "127.0.0.2", "127.0.0.2"));
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());

    // Register to blue instance
    agent_a_->Subscribe("blue", 1);
    agent_b_->Subscribe("blue", 1);

    // Add route from agent A.
    stringstream route_a;
    route_a << "10.1.1.1/32";
    agent_a_->AddRoute("blue", route_a.str(), "192.168.1.1", 200);
    task_util::WaitForIdle();

    // Verify that route showed up on agents A and B.
    VerifyRouteExists(agent_a_, "blue", route_a.str(), "192.168.1.1", 200);
    VerifyRouteExists(agent_b_, "blue", route_a.str(), "192.168.1.1", 200);

    // Change route from agent A.
    agent_a_->AddRoute("blue", route_a.str(), "192.168.1.1", 300);
    task_util::WaitForIdle();

    // Verify that route is updated up on agents A and B.
    VerifyRouteExists(agent_a_, "blue", route_a.str(), "192.168.1.1", 300);
    VerifyRouteExists(agent_b_, "blue", route_a.str(), "192.168.1.1", 300);

    // Change route from agent A so that it falls back to default local pref.
    agent_a_->AddRoute("blue", route_a.str(), "192.168.1.1");
    task_util::WaitForIdle();

    // Verify that route is updated up on agents A and B.
    VerifyRouteExists(agent_a_, "blue", route_a.str(), "192.168.1.1", 100);
    VerifyRouteExists(agent_b_, "blue", route_a.str(), "192.168.1.1", 100);

    // Delete route from agent A.
    agent_a_->DeleteRoute("blue", route_a.str());
    task_util::WaitForIdle();

    // Verify that route is deleted at agents A and B.
    VerifyRouteNoExists(agent_a_, "blue", route_a.str());
    VerifyRouteNoExists(agent_b_, "blue", route_a.str());

    // Close the sessions.
    agent_a_->SessionDown();
    agent_b_->SessionDown();
}

//
// Agent flaps a route by changing it repeatedly.
//
TEST_F(BgpXmppInetvpn2ControlNodeTest, RouteFlap1) {
    Configure();
    task_util::WaitForIdle();

    // Create XMPP Agent A connected to XMPP server X.
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, "agent-a", xs_x_->GetPort(),
            "127.0.0.1", "127.0.0.1"));
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    // Create XMPP Agent B connected to XMPP server Y.
    agent_b_.reset(
        new test::NetworkAgentMock(&evm_, "agent-b", xs_y_->GetPort(),
            "127.0.0.2", "127.0.0.2"));
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());

    // Register to blue instance
    agent_a_->Subscribe("blue", 1);
    agent_b_->Subscribe("blue", 1);

    // Add route from agent A and change it repeatedly.
    stringstream route_a;
    route_a << "10.1.1.1/32";
    for (int idx = 0; idx < 1024; ++idx) {
        agent_a_->AddRoute("blue", route_a.str(), "192.168.1.1");
        agent_a_->AddRoute("blue", route_a.str(), "192.168.1.2");
    }

    // Delete route from agent A.
    agent_a_->DeleteRoute("blue", route_a.str());
    task_util::WaitForIdle();

    // Verify that route is deleted at agents A and B.
    VerifyRouteNoExists(agent_a_, "blue", route_a.str());
    VerifyRouteNoExists(agent_b_, "blue", route_a.str());

    // Close the sessions.
    agent_a_->SessionDown();
    agent_b_->SessionDown();
}

//
// Agent flaps a route by adding and deleting it repeatedly.
//
TEST_F(BgpXmppInetvpn2ControlNodeTest, RouteFlap2) {
    Configure();
    task_util::WaitForIdle();

    // Create XMPP Agent A connected to XMPP server X.
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, "agent-a", xs_x_->GetPort(),
            "127.0.0.1", "127.0.0.1"));
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    // Create XMPP Agent B connected to XMPP server Y.
    agent_b_.reset(
        new test::NetworkAgentMock(&evm_, "agent-b", xs_y_->GetPort(),
            "127.0.0.2", "127.0.0.2"));
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());

    // Register to blue instance
    agent_a_->Subscribe("blue", 1);
    agent_b_->Subscribe("blue", 1);

    // Add and delete route from agent A repeatedly.
    stringstream route_a;
    route_a << "10.1.1.1/32";
    for (int idx = 0; idx < 1024; ++idx) {
        agent_a_->AddRoute("blue", route_a.str(), "192.168.1.1");
        usleep(5000);
        agent_a_->DeleteRoute("blue", route_a.str());
    }

    // Verify that route is deleted at agents A and B.
    VerifyRouteNoExists(agent_a_, "blue", route_a.str());
    VerifyRouteNoExists(agent_b_, "blue", route_a.str());

    // Close the sessions.
    agent_a_->SessionDown();
    agent_b_->SessionDown();
}

//
// Agent flaps routes with same prefix on two connected VRFs repeatedly.
//
TEST_F(BgpXmppInetvpn2ControlNodeTest, RouteFlap_ConnectedVRF) {
    Configure(config_2_control_nodes_4vns);
    AddConnection(bs_x_, "blue", "pink");
    AddConnection(bs_x_, "blue", "green");
    AddConnection(bs_x_, "pink", "black");

    AddConnection(bs_y_, "blue", "pink");
    AddConnection(bs_y_, "blue", "green");
    AddConnection(bs_y_, "pink", "black");
    task_util::WaitForIdle();

    // Create XMPP Agent A connected to XMPP server X.
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, "agent-a", xs_x_->GetPort(),
            "127.0.0.1", "127.0.0.1"));
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    // Create XMPP Agent B connected to XMPP server Y.
    agent_b_.reset(
        new test::NetworkAgentMock(&evm_, "agent-b", xs_y_->GetPort(),
            "127.0.0.2", "127.0.0.2"));
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());

    // Register to blue instance
    agent_a_->Subscribe("blue", 1);
    agent_b_->Subscribe("blue", 1);
    agent_a_->Subscribe("pink", 2);
    agent_b_->Subscribe("pink", 2);
    agent_a_->Subscribe("green", 3);
    agent_b_->Subscribe("green", 3);
    agent_a_->Subscribe("black", 4);
    agent_b_->Subscribe("black", 4);

    // Add and delete route from agent A repeatedly.
    stringstream route_a;
    route_a << "10.1.1.1/32";
    for (int idx = 0; idx < 64; ++idx) {
        agent_a_->AddRoute("blue", route_a.str(), "192.168.1.1");
        agent_a_->AddRoute("pink", route_a.str(), "192.168.1.1");
        usleep(500);
        agent_a_->DeleteRoute("blue", route_a.str());
        agent_a_->DeleteRoute("pink", route_a.str());
    }

    // Verify that route is deleted at agents A and B.
    VerifyRouteNoExists(agent_a_, "blue", route_a.str());
    VerifyRouteNoExists(agent_b_, "blue", route_a.str());
    VerifyRouteNoExists(agent_a_, "pink", route_a.str());
    VerifyRouteNoExists(agent_b_, "pink", route_a.str());


    agent_a_->Unsubscribe("blue");
    agent_b_->Unsubscribe("blue");
    agent_a_->Unsubscribe("pink");
    agent_b_->Unsubscribe("pink");
    agent_a_->Unsubscribe("green");
    agent_b_->Unsubscribe("green");
    agent_a_->Unsubscribe("black");
    agent_b_->Unsubscribe("black");

    // Close the sessions.
    agent_a_->SessionDown();
    agent_b_->SessionDown();

    TASK_UTIL_EXPECT_FALSE(agent_a_->IsEstablished());
    TASK_UTIL_EXPECT_FALSE(agent_b_->IsEstablished());
}

//
// Multiple routes are exchanged correctly.
//
TEST_F(BgpXmppInetvpn2ControlNodeTest, MultipleRouteAdd) {
    Configure();
    task_util::WaitForIdle();

    // Create XMPP Agent A connected to XMPP server X.
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, "agent-a", xs_x_->GetPort(),
            "127.0.0.1", "127.0.0.1"));
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    // Create XMPP Agent B connected to XMPP server Y.
    agent_b_.reset(
        new test::NetworkAgentMock(&evm_, "agent-b", xs_y_->GetPort(),
            "127.0.0.2", "127.0.0.2"));
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());

    // Register to blue instance
    agent_a_->Subscribe("blue", 1);
    agent_b_->Subscribe("blue", 1);

    // Add routes from agent A.
    for (int idx = 0; idx < kRouteCount; ++idx) {
        agent_a_->AddRoute("blue", BuildPrefix(idx), "192.168.1.1");
    }

    // Verify that routes showed up on agents A and B.
    for (int idx = 0; idx < kRouteCount; ++idx) {
        VerifyRouteExists(agent_a_, "blue", BuildPrefix(idx), "192.168.1.1");
        VerifyRouteExists(agent_b_, "blue", BuildPrefix(idx), "192.168.1.1");
    }

    // Delete routes from agent A.
    for (int idx = 0; idx < kRouteCount; ++idx) {
        agent_a_->DeleteRoute("blue", BuildPrefix(idx));
    }

    // Verify that routes are deleted at agents A and B.
    for (int idx = 0; idx < kRouteCount; ++idx) {
        VerifyRouteNoExists(agent_a_, "blue", BuildPrefix(idx));
        VerifyRouteNoExists(agent_b_, "blue", BuildPrefix(idx));
    }

    // Unregister to blue instance
    agent_a_->Unsubscribe("blue");
    agent_b_->Unsubscribe("blue");

    // Verify update counters on X and Y.
    const BgpPeer *peer_xy = VerifyPeerExists(bs_x_, bs_y_);
    const BgpPeer *peer_yx = VerifyPeerExists(bs_y_, bs_x_);
    TASK_UTIL_EXPECT_EQ(
        peer_xy->get_rx_route_reach(), peer_yx->get_tx_route_reach());
    TASK_UTIL_EXPECT_EQ(
        peer_xy->get_tx_route_reach(), peer_yx->get_rx_route_reach());
    TASK_UTIL_EXPECT_EQ(
        peer_xy->get_rx_route_unreach(), peer_yx->get_tx_route_unreach());
    TASK_UTIL_EXPECT_EQ(
        peer_xy->get_tx_route_unreach(), peer_yx->get_rx_route_unreach());
    TASK_UTIL_EXPECT_EQ(
        peer_xy->get_rx_end_of_rib(), peer_yx->get_tx_end_of_rib());
    TASK_UTIL_EXPECT_EQ(
        peer_xy->get_tx_end_of_rib(), peer_yx->get_rx_end_of_rib());
    TASK_UTIL_EXPECT_EQ(
        peer_xy->get_rx_route_total(), peer_yx->get_tx_route_total());
    TASK_UTIL_EXPECT_EQ(
        peer_xy->get_tx_route_total(), peer_yx->get_rx_route_total());

    // Verify reach/unreach, end-of-rib and total counts.
    // 1 route-target and kRouteCount inet-vpn routes.
    // 2 end-of-ribs - one for route-target and one for inet-vpn.
    TASK_UTIL_EXPECT_EQ(1 + kRouteCount, peer_xy->get_tx_route_reach());
    TASK_UTIL_EXPECT_EQ(1 + kRouteCount, peer_xy->get_tx_route_unreach());
    TASK_UTIL_EXPECT_EQ(2, peer_xy->get_tx_end_of_rib());
    TASK_UTIL_EXPECT_EQ(
        2 * (1 + kRouteCount) + 2, peer_xy->get_tx_route_total());

    // Close the sessions.
    agent_a_->SessionDown();
    agent_b_->SessionDown();
}

//
// Generate big update message.
// Each route has a very large number of attributes such that only a single
// route fits into each update.
//
TEST_F(BgpXmppInetvpn2ControlNodeTest, BigUpdateMessage1) {
    Configure();
    task_util::WaitForIdle();

    // Add a bunch of route targets to blue instance.
    static const int kRouteTargetCount = 498;
    for (int idx = 0; idx < kRouteTargetCount; ++idx) {
        string target = string("target:1:") + integerToString(10000 + idx);
        AddRouteTarget(bs_x_, "blue", target);
        task_util::WaitForIdle();
    }
    TASK_UTIL_EXPECT_EQ(1 + kRouteTargetCount,
        GetExportRouteTargetListSize(bs_x_, "blue"));
    TASK_UTIL_EXPECT_EQ(1 + kRouteTargetCount,
        GetImportRouteTargetListSize(bs_x_, "blue"));
    task_util::WaitForIdle();

    // Create XMPP Agent A connected to XMPP server X.
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, "agent-a", xs_x_->GetPort(),
            "127.0.0.1", "127.0.0.1"));
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    // Create XMPP Agent B connected to XMPP server Y.
    agent_b_.reset(
        new test::NetworkAgentMock(&evm_, "agent-b", xs_y_->GetPort(),
            "127.0.0.2", "127.0.0.2"));
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());

    // Register to blue instance
    agent_a_->Subscribe("blue", 1);
    agent_b_->Subscribe("blue", 1);

    // Add routes from agent A.
    for (int idx = 0; idx < 4; ++idx) {
        agent_a_->AddRoute("blue", BuildPrefix(idx), "192.168.1.1");
    }

    // Verify that routes showed up on agents A and B.
    for (int idx = 0; idx < 4; ++idx) {
        VerifyRouteExists(agent_a_, "blue", BuildPrefix(idx), "192.168.1.1");
        VerifyRouteExists(agent_b_, "blue", BuildPrefix(idx), "192.168.1.1");
    }

    // Delete routes from agent A.
    for (int idx = 0; idx < 4; ++idx) {
        agent_a_->DeleteRoute("blue", BuildPrefix(idx));
    }

    // Verify that routes are deleted at agents A and B.
    for (int idx = 0; idx < 4; ++idx) {
        VerifyRouteNoExists(agent_a_, "blue", BuildPrefix(idx));
        VerifyRouteNoExists(agent_b_, "blue", BuildPrefix(idx));
    }

    // Unregister to blue instance
    agent_a_->Unsubscribe("blue");
    agent_b_->Unsubscribe("blue");

    // Close the sessions.
    agent_a_->SessionDown();
    agent_b_->SessionDown();

    TASK_UTIL_EXPECT_EQ(0, bs_x_->message_build_error());
}

//
// Generate big update message.
// Each route has a very large number of attributes such that even a single
// route doesn't fit into an update.
//
TEST_F(BgpXmppInetvpn2ControlNodeTest, BigUpdateMessage2) {
    Configure();
    task_util::WaitForIdle();

    // Add extra route targets to blue instance.
    static const int kRouteTargetCount = 512;
    for (int idx = 0; idx < kRouteTargetCount; ++idx) {
        string target = string("target:1:") + integerToString(10000 + idx);
        AddRouteTarget(bs_x_, "blue", target);
        task_util::WaitForIdle();
    }
    TASK_UTIL_EXPECT_EQ(1 + kRouteTargetCount,
        GetExportRouteTargetListSize(bs_x_, "blue"));
    TASK_UTIL_EXPECT_EQ(1 + kRouteTargetCount,
        GetImportRouteTargetListSize(bs_x_, "blue"));
    task_util::WaitForIdle();

    // Create XMPP Agent A connected to XMPP server X.
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, "agent-a", xs_x_->GetPort(),
            "127.0.0.1", "127.0.0.1"));
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    // Create XMPP Agent B connected to XMPP server Y.
    agent_b_.reset(
        new test::NetworkAgentMock(&evm_, "agent-b", xs_y_->GetPort(),
            "127.0.0.2", "127.0.0.2"));
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());

    // Register to blue instance
    agent_a_->Subscribe("blue", 1);
    agent_b_->Subscribe("blue", 1);
    task_util::WaitForIdle();

    // Add routes from agent A.
    for (int idx = 0; idx < 4; ++idx) {
        agent_a_->AddRoute("blue", BuildPrefix(idx), "192.168.1.1");
    }

    // Verify that we're unable to build the messages.
    TASK_UTIL_EXPECT_EQ(4, bs_x_->message_build_error());

    // Verify that routes showed up on agent A, but not on B.
    for (int idx = 0; idx < 4; ++idx) {
        VerifyRouteExists(agent_a_, "blue", BuildPrefix(idx), "192.168.1.1");
        VerifyRouteNoExists(agent_b_, "blue", BuildPrefix(idx));
    }

    // Remove extra route targets from blue instance.
    for (int idx = 0; idx < kRouteTargetCount; ++idx) {
        string target = string("target:1:") + integerToString(10000 + idx);
        RemoveRouteTarget(bs_x_, "blue", target);
        task_util::WaitForIdle();
    }
    TASK_UTIL_EXPECT_EQ(1, GetExportRouteTargetListSize(bs_x_, "blue"));
    TASK_UTIL_EXPECT_EQ(1, GetImportRouteTargetListSize(bs_x_, "blue"));
    task_util::WaitForIdle();

    // Verify that routes showed up on agents A and B.
    for (int idx = 0; idx < 4; ++idx) {
        VerifyRouteExists(agent_a_, "blue", BuildPrefix(idx), "192.168.1.1");
        VerifyRouteExists(agent_b_, "blue", BuildPrefix(idx), "192.168.1.1");
    }

    // Delete routes from agent A.
    for (int idx = 0; idx < 4; ++idx) {
        agent_a_->DeleteRoute("blue", BuildPrefix(idx));
    }

    // Verify that routes are deleted at agents A and B.
    for (int idx = 0; idx < 4; ++idx) {
        VerifyRouteNoExists(agent_a_, "blue", BuildPrefix(idx));
        VerifyRouteNoExists(agent_b_, "blue", BuildPrefix(idx));
    }

    // Unregister to blue instance
    agent_a_->Unsubscribe("blue");
    agent_b_->Unsubscribe("blue");

    // Close the sessions.
    agent_a_->SessionDown();
    agent_b_->SessionDown();
}

TEST_F(BgpXmppInetvpn2ControlNodeTest, TunnelEncap) {
    Configure();
    task_util::WaitForIdle();

    // Create XMPP Agent A connected to XMPP server X.
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, "agent-a", xs_x_->GetPort(),
            "127.0.0.1", "127.0.0.1"));
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    // Create XMPP Agent B connected to XMPP server Y.
    agent_b_.reset(
        new test::NetworkAgentMock(&evm_, "agent-b", xs_y_->GetPort(),
            "127.0.0.2", "127.0.0.2"));
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());

    // Register to blue instance
    agent_a_->Subscribe("blue", 1);
    agent_b_->Subscribe("blue", 1);

    // Add route from agent A.
    stringstream route_a;
    route_a << "10.1.1.1/32";
    test::NextHops next_hops;
    test::NextHop next_hop("192.168.1.1", 0, "udp");
    next_hops.push_back(next_hop);
    agent_a_->AddRoute("blue", route_a.str(), next_hops, 100);
    task_util::WaitForIdle();

    // Verify that route showed up on agents A and B.
    VerifyRouteExists(
        agent_a_, "blue", route_a.str(), "192.168.1.1", 100, "udp");
    VerifyRouteExists(
        agent_b_, "blue", route_a.str(), "192.168.1.1", 100, "udp");

    // Delete route from agent A.
    agent_a_->DeleteRoute("blue", route_a.str());
    task_util::WaitForIdle();

    // Verify that route is deleted at agents A and B.
    VerifyRouteNoExists(agent_a_, "blue", route_a.str());
    VerifyRouteNoExists(agent_b_, "blue", route_a.str());

    // Close the sessions.
    agent_a_->SessionDown();
    agent_b_->SessionDown();
}

TEST_F(BgpXmppInetvpn2ControlNodeTest, VirtualNetworkIndexChange) {
    Configure(config_2_control_nodes_no_vn_info);
    task_util::WaitForIdle();

    // Create XMPP Agent A connected to XMPP server X.
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, "agent-a", xs_x_->GetPort(),
            "127.0.0.1", "127.0.0.1"));
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    // Create XMPP Agent B connected to XMPP server Y.
    agent_b_.reset(
        new test::NetworkAgentMock(&evm_, "agent-b", xs_y_->GetPort(),
            "127.0.0.2", "127.0.0.2"));
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());

    // Register to blue instance
    agent_a_->Subscribe("blue", 1);
    agent_b_->Subscribe("blue", 1);

    // Add route from agent A.
    stringstream route_a;
    route_a << "10.1.1.1/32";
    test::NextHops next_hops;
    test::NextHop next_hop("192.168.1.1", 0, "udp");
    next_hops.push_back(next_hop);
    agent_a_->AddRoute("blue", route_a.str(), next_hops, 100);
    task_util::WaitForIdle();

    // Verify the origin VN on agents A and B.
    VerifyRouteExists(
        agent_a_, "blue", route_a.str(), "192.168.1.1", 100, "udp", "blue");
    VerifyRouteExists(
        agent_b_, "blue", route_a.str(), "192.168.1.1", 100, "udp", "unresolved");

    // Update the config to include VN index.
    Configure(config_2_control_nodes);
    task_util::WaitForIdle();

    // Verify the origin VN on agents A and B.
    VerifyRouteExists(
        agent_a_, "blue", route_a.str(), "192.168.1.1", 100, "udp", "blue");
    VerifyRouteExists(
        agent_b_, "blue", route_a.str(), "192.168.1.1", 100, "udp", "blue");

    // Delete route from agent A.
    agent_a_->DeleteRoute("blue", route_a.str());
    task_util::WaitForIdle();

    // Close the sessions.
    agent_a_->SessionDown();
    agent_b_->SessionDown();
}

TEST_F(BgpXmppInetvpn2ControlNodeTest, SecurityGroupsSameAsn) {
    Configure();
    task_util::WaitForIdle();

    // Create XMPP Agent A connected to XMPP server X.
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, "agent-a", xs_x_->GetPort(),
            "127.0.0.1", "127.0.0.1"));
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    // Create XMPP Agent B connected to XMPP server Y.
    agent_b_.reset(
        new test::NetworkAgentMock(&evm_, "agent-b", xs_y_->GetPort(),
            "127.0.0.2", "127.0.0.2"));
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());

    // Register to blue instance
    agent_a_->Subscribe("blue", 1);
    agent_b_->Subscribe("blue", 1);

    // Add route from agent A.
    stringstream route_a;
    route_a << "10.1.1.1/32";
    test::NextHops next_hops;
    test::NextHop next_hop("192.168.1.1");
    next_hops.push_back(next_hop);
    vector<int> sgids = list_of
        (SecurityGroup::kMaxGlobalId - 1)(SecurityGroup::kMaxGlobalId + 1)
        (SecurityGroup::kMaxGlobalId - 2)(SecurityGroup::kMaxGlobalId + 2);
    test::RouteAttributes attributes(sgids);
    agent_a_->AddRoute("blue", route_a.str(), next_hops, attributes);
    task_util::WaitForIdle();

    // Verify that route showed up on agents A and B with expected sgids.
    sort(sgids.begin(), sgids.end());
    VerifyRouteExists(
        agent_a_, "blue", route_a.str(), "192.168.1.1", sgids);
    VerifyRouteExists(
        agent_b_, "blue", route_a.str(), "192.168.1.1", sgids);

    // Delete route from agent A.
    agent_a_->DeleteRoute("blue", route_a.str());
    task_util::WaitForIdle();

    // Verify that route is deleted at agents A and B.
    VerifyRouteNoExists(agent_a_, "blue", route_a.str());
    VerifyRouteNoExists(agent_b_, "blue", route_a.str());

    // Close the sessions.
    agent_a_->SessionDown();
    agent_b_->SessionDown();
}

TEST_F(BgpXmppInetvpn2ControlNodeTest, SecurityGroupsDifferentAsn) {
    Configure(config_2_control_nodes_different_asn);
    task_util::WaitForIdle();

    // Create XMPP Agent A connected to XMPP server X.
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, "agent-a", xs_x_->GetPort(),
            "127.0.0.1", "127.0.0.1"));
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    // Create XMPP Agent B connected to XMPP server Y.
    agent_b_.reset(
        new test::NetworkAgentMock(&evm_, "agent-b", xs_y_->GetPort(),
            "127.0.0.2", "127.0.0.2"));
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());

    // Register to blue instance
    agent_a_->Subscribe("blue", 1);
    agent_b_->Subscribe("blue", 1);

    // Add route from agent A.
    stringstream route_a;
    route_a << "10.1.1.1/32";
    test::NextHops next_hops;
    test::NextHop next_hop("192.168.1.1");
    next_hops.push_back(next_hop);
    vector<int> sgids = list_of
        (SecurityGroup::kMaxGlobalId - 1)(SecurityGroup::kMaxGlobalId + 1)
        (SecurityGroup::kMaxGlobalId - 2)(SecurityGroup::kMaxGlobalId + 2);
    test::RouteAttributes attributes(sgids);
    agent_a_->AddRoute("blue", route_a.str(), next_hops, attributes);
    task_util::WaitForIdle();

    // Verify that route showed up on agents A and B with expected sgids.
    vector<int> global_sgids = list_of
        (SecurityGroup::kMaxGlobalId - 1)(SecurityGroup::kMaxGlobalId - 2);
    sort(sgids.begin(), sgids.end());
    sort(global_sgids.begin(), global_sgids.end());
    VerifyRouteExists(
        agent_a_, "blue", route_a.str(), "192.168.1.1", sgids);
    VerifyRouteExists(
        agent_b_, "blue", route_a.str(), "192.168.1.1", global_sgids);

    // Delete route from agent A.
    agent_a_->DeleteRoute("blue", route_a.str());
    task_util::WaitForIdle();

    // Verify that route is deleted at agents A and B.
    VerifyRouteNoExists(agent_a_, "blue", route_a.str());
    VerifyRouteNoExists(agent_b_, "blue", route_a.str());

    // Close the sessions.
    agent_a_->SessionDown();
    agent_b_->SessionDown();
}

//
// Peer should not be deleted till references from replicator are gone.
//
TEST_F(BgpXmppInetvpn2ControlNodeTest, PeerDelete) {
    Configure();
    task_util::WaitForIdle();

    // Create XMPP Agent A connected to XMPP server X.
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, "agent-a", xs_x_->GetPort(),
            "127.0.0.1", "127.0.0.1"));
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    // Create XMPP Agent B connected to XMPP server Y.
    agent_b_.reset(
        new test::NetworkAgentMock(&evm_, "agent-b", xs_y_->GetPort(),
            "127.0.0.2", "127.0.0.2"));
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());

    // Register to blue instance
    agent_a_->Subscribe("blue", 1);
    agent_b_->Subscribe("blue", 1);

    // Add route from agent A.
    stringstream route_a;
    route_a << "10.1.1.1/32";
    agent_a_->AddRoute("blue", route_a.str(), "192.168.1.1");
    task_util::WaitForIdle();

    // Verify that route showed up on agents A and B.
    VerifyRouteExists(agent_a_, "blue", route_a.str(), "192.168.1.1");
    VerifyRouteExists(agent_b_, "blue", route_a.str(), "192.168.1.1");

    // Disable all DB partition queue processing on server X.
    task_util::WaitForIdle();
    bs_x_->database()->SetQueueDisable(true);

    // Close the session from agent A.
    agent_a_->SessionDown();

    // Verify that route is deleted at agent A but not at B.
    VerifyRouteNoExists(agent_a_, "blue", route_a.str());
    VerifyRouteExists(agent_b_, "blue", route_a.str(), "192.168.1.1");

    // Enable all DB partition queue processing on server X.
    bs_x_->database()->SetQueueDisable(false);

    // Verify that route is deleted at agents A and B.
    VerifyRouteNoExists(agent_a_, "blue", route_a.str());
    VerifyRouteNoExists(agent_b_, "blue", route_a.str());

    // Close the session from agent B.
    agent_b_->SessionDown();
}

class TestEnvironment : public ::testing::Environment {
    virtual ~TestEnvironment() { }
    virtual void SetUp() {
    }
    virtual void TearDown() {
    }
};

static void SetUp() {
    ControlNode::SetDefaultSchedulingPolicy();
    BgpServerTest::GlobalSetUp();
    BgpObjectFactory::Register<StateMachine>(
        boost::factory<StateMachineTest *>());
    BgpObjectFactory::Register<BgpXmppMessageBuilder>(
        boost::factory<BgpXmppMessageBuilder *>());
    XmppObjectFactory::Register<XmppStateMachine>(
        boost::factory<XmppStateMachineTest *>());
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
