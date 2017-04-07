/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <sstream>
#include <boost/assign/list_of.hpp>
#include <boost/foreach.hpp>

#include "base/task_annotations.h"
#include "bgp/bgp_factory.h"
#include "bgp/bgp_session_manager.h"
#include "bgp/bgp_update_sender.h"
#include "bgp/bgp_xmpp_channel.h"
#include "bgp/security_group/security_group.h"
#include "bgp/test/bgp_server_test_util.h"
#include "bgp/xmpp_message_builder.h"
#include "control-node/control_node.h"
#include "control-node/test/network_agent_mock.h"
#include "io/test/event_manager_test.h"
#include "ifmap/test/ifmap_test_util.h"

#include "schema/bgp_schema_types.h"

#include "xmpp/xmpp_factory.h"

using namespace std;
using std::auto_ptr;
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

static const char *config_1_control_node_2_vns = "\
<config>\
    <bgp-router name=\'X\'>\
        <identifier>192.168.0.1</identifier>\
        <address>127.0.0.1</address>\
        <port>%d</port>\
    </bgp-router>\
    <virtual-network name='blue'>\
        <network-id>1</network-id>\
    </virtual-network>\
    <virtual-network name='pink'>\
        <network-id>2</network-id>\
    </virtual-network>\
    <routing-instance name='blue'>\
        <virtual-network>blue</virtual-network>\
        <vrf-target>target:1:1</vrf-target>\
    </routing-instance>\
    <routing-instance name='pink'>\
        <virtual-network>pink</virtual-network>\
        <vrf-target>target:1:2</vrf-target>\
    </routing-instance>\
</config>\
";

static const char *config_2_control_nodes_2_vns = "\
<config>\
    <bgp-router name=\'X\'>\
        <identifier>192.168.0.1</identifier>\
        <address>127.0.0.1</address>\
        <port>%d</port>\
        <session to=\'Y\'>\
            <address-families>\
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
                <family>inet-vpn</family>\
            </address-families>\
        </session>\
    </bgp-router>\
    <virtual-network name='blue'>\
        <network-id>1</network-id>\
    </virtual-network>\
    <virtual-network name='pink'>\
        <network-id>2</network-id>\
    </virtual-network>\
    <routing-instance name='blue'>\
        <virtual-network>blue</virtual-network>\
        <vrf-target>target:1:1</vrf-target>\
    </routing-instance>\
    <routing-instance name='pink'>\
        <virtual-network>pink</virtual-network>\
        <vrf-target>target:1:2</vrf-target>\
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
    <virtual-network name='default-domain:default-project:ip-fabric'>\
        <network-id>100</network-id>\
    </virtual-network>\
    <routing-instance name='default-domain:default-project:ip-fabric:ip-fabric'>\
        <virtual-network>default-domain:default-project:ip-fabric</virtual-network>\
        <vrf-target>target:1:100</vrf-target>\
    </routing-instance>\
    <virtual-network name='blue'>\
        <network-id>1</network-id>\
    </virtual-network>\
    <routing-instance name='blue'>\
        <virtual-network>blue</virtual-network>\
        <vrf-target>target:1:1</vrf-target>\
    </routing-instance>\
</config>\
";

static const char *config_2_control_nodes_no_rtf = "\
<config>\
    <bgp-router name=\'X\'>\
        <identifier>192.168.0.1</identifier>\
        <address>127.0.0.1</address>\
        <port>%d</port>\
        <session to=\'Y\'>\
            <address-families>\
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
                <family>inet-vpn</family>\
            </address-families>\
        </session>\
    </bgp-router>\
    <virtual-network name='blue'>\
        <network-id>1</network-id>\
    </virtual-network>\
    <virtual-network name='pink'>\
        <network-id>2</network-id>\
    </virtual-network>\
    <routing-instance name='blue'>\
        <virtual-network>blue</virtual-network>\
        <vrf-target>target:1:1</vrf-target>\
    </routing-instance>\
    <routing-instance name='pink'>\
        <virtual-network>pink</virtual-network>\
        <vrf-target>target:1:2</vrf-target>\
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
        <router-type>external-control-node</router-type>\
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
        <router-type>external-control-node</router-type>\
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

static const char *config_2_control_nodes_route_aggregate = "\
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
    <route-aggregate name='vn_subnet'>\
        <aggregate-route-entries>\
            <route>2.2.0.0/16</route>\
        </aggregate-route-entries>\
        <nexthop>2.2.1.1</nexthop>\
    </route-aggregate>\
    <routing-instance name='blue'>\
        <virtual-network>blue</virtual-network>\
        <route-aggregate to='vn_subnet'/>\
        <vrf-target>target:1:1</vrf-target>\
    </routing-instance>\
</config>\
";


static const char *config_2_control_nodes_routing_policy = "\
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
    <routing-policy name='p1'>\
        <term>\
            <term-match-condition>\
                <community>11:13</community>\
            </term-match-condition>\
            <term-action-list>\
                <update>\
                    <local-pref>9999</local-pref>\
                </update>\
            </term-action-list>\
        </term>\
    </routing-policy>\
    <routing-policy name='p2'>\
        <term>\
            <term-match-condition>\
                <community>22:13</community>\
            </term-match-condition>\
            <term-action-list>\
                <update>\
                    <local-pref>8888</local-pref>\
                </update>\
            </term-action-list>\
        </term>\
    </routing-policy>\
    <routing-instance name='blue'>\
        <virtual-network>blue</virtual-network>\
        <routing-policy to='p1'>\
            <sequence>1.0</sequence>\
        </routing-policy>\
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

    void AddRouteTarget(BgpServerTestPtr server, const string &name,
        const string &target) {
        TASK_UTIL_EXPECT_NE(static_cast<RoutingInstance *>(NULL),
            server->routing_instance_mgr()->GetRoutingInstance(name));

        ifmap_test_util::IFMapMsgLink(server->config_db(),
                                      "routing-instance", name,
                                      "route-target", target,
                                      "instance-target");
    }

    void RemoveRouteTarget(BgpServerTestPtr server, const string &name,
        const string &target) {
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
        string prefix, string nexthop, int local_pref, int med, int seq,
        const string &encap, const string &origin_vn, const vector<int> sgids,
        const LoadBalance &loadBalance, const vector<string> communities) {
        const autogen::ItemType *rt = agent->RouteLookup(net, prefix);
        if (!rt)
            return false;
        if (rt->entry.next_hops.next_hop[0].address != nexthop)
            return false;
        if (local_pref && rt->entry.local_preference != local_pref)
            return false;
        if (med && rt->entry.med != med)
            return false;
        if (seq && rt->entry.sequence_number != seq)
            return false;
        if (!origin_vn.empty() && rt->entry.virtual_network != origin_vn)
            return false;
        if (!sgids.empty() &&
            rt->entry.security_group_list.security_group != sgids)
            return false;
        if (!communities.empty() &&
            rt->entry.community_tag_list.community_tag != communities)
            return false;
        if (LoadBalance(rt->entry.load_balance) != loadBalance) {
            return false;
        }

        autogen::TunnelEncapsulationListType rt_encap =
            rt->entry.next_hops.next_hop[0].tunnel_encapsulation_list;
        if (!encap.empty() && !CheckEncap(rt_encap, encap))
            return false;
        return true;
    }

    void VerifyRouteExists(test::NetworkAgentMockPtr agent, string net,
        string prefix, string nexthop, int local_pref, int med) {
        TASK_UTIL_EXPECT_TRUE(CheckRoute(agent, net, prefix, nexthop,
            local_pref, med, 0, string(), string(), vector<int>(),
            LoadBalance(), vector<string>()));
    }

    void VerifyRouteExists(test::NetworkAgentMockPtr agent, string net,
        string prefix, string nexthop, int local_pref = 0,
        const string &encap = string(), const string &origin_vn = string(),
        const vector<int> sgids = vector<int>(),
        const LoadBalance lb = LoadBalance()) {
        TASK_UTIL_EXPECT_TRUE(CheckRoute(
            agent, net, prefix, nexthop, local_pref, 0, 0, encap, origin_vn,
            sgids, lb, vector<string>()));
    }

    void VerifyRouteExists(test::NetworkAgentMockPtr agent, string net,
        string prefix, string nexthop, const vector<int> sgids) {
        TASK_UTIL_EXPECT_TRUE(CheckRoute(
            agent, net, prefix, nexthop, 0, 0, 0, string(), string(), sgids,
            LoadBalance(), vector<string>()));
    }

    void VerifyRouteExists(test::NetworkAgentMockPtr agent, string net,
        string prefix, string nexthop, int local_pref, int seq,
        const vector<int> sgids) {
        TASK_UTIL_EXPECT_TRUE(CheckRoute(
            agent, net, prefix, nexthop, local_pref, 0, seq, string(), string(),
            sgids, LoadBalance(), vector<string>()));
    }

    void VerifyRouteExists(test::NetworkAgentMockPtr agent, string net,
        string prefix, string nexthop, const LoadBalance &lb) {
        TASK_UTIL_EXPECT_TRUE(CheckRoute(
            agent, net, prefix, nexthop, 0, 0, 0, string(), string(),
            vector<int>(), lb, vector<string>()));
    }


    void VerifyRouteExists(test::NetworkAgentMockPtr agent, string net,
        string prefix, string nexthop, const vector<string> &communities) {
        TASK_UTIL_EXPECT_TRUE(CheckRoute(
            agent, net, prefix, nexthop, 0, 0, 0, string(), string(),
            vector<int>(), LoadBalance(), communities));
    }

    void VerifyRouteNoExists(test::NetworkAgentMockPtr agent, string net,
        string prefix) {
        TASK_UTIL_EXPECT_TRUE(agent->RouteLookup(net, prefix) == NULL);
    }

    bool CheckPath(test::NetworkAgentMockPtr agent, const string &net,
        const string &prefix, const string &nexthop, const string &origin_vn) {
        task_util::TaskSchedulerLock lock;
        const autogen::ItemType *rt = agent->RouteLookup(net, prefix);
        if (!rt)
            return false;
        BOOST_FOREACH(const autogen::NextHopType &nh, rt->entry.next_hops) {
            if (nh.address == nexthop) {
                if (nh.virtual_network == origin_vn)
                    return true;
            }
        }
        return false;
    }

    void VerifyPath(test::NetworkAgentMockPtr agent, const string &net,
        const string &prefix, const string &nexthop, const string &origin_vn) {
        TASK_UTIL_EXPECT_TRUE(
            CheckPath(agent, net, prefix, nexthop, origin_vn));
    }

    const BgpPeer *VerifyPeerExists(BgpServerTestPtr s1, BgpServerTestPtr s2) {
        const char *master = BgpConfigManager::kMasterInstance;
        string name = string(master) + ":" + s2->localname();
        TASK_UTIL_EXPECT_TRUE(s1->FindMatchingPeer(master, name) != NULL);
        return s1->FindMatchingPeer(master, name);
    }

    const BgpXmppChannel *VerifyXmppChannelExists(
        BgpXmppChannelManager *cm, const string &name) {
        TASK_UTIL_EXPECT_TRUE(cm->FindChannel(name) != NULL);
        return cm->FindChannel(name);
    }

    void VerifyOutputQueueDepth(BgpServerTestPtr server, uint32_t depth) {
        ConcurrencyScope scope("bgp::Config");
        TASK_UTIL_EXPECT_EQ(depth, server->get_output_queue_depth());
    }

    BgpTable *VerifyTableExists(BgpServerTestPtr server, const string &name) {
        TASK_UTIL_EXPECT_TRUE(server->database()->FindTable(name) != NULL);
        return static_cast<BgpTable *>(server->database()->FindTable(name));
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
// Route added with community list
//
TEST_F(BgpXmppInetvpn2ControlNodeTest, RouteWithCommunity) {
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

    // Add route from agent A with NO_REORIGINATE community
    stringstream route_a;
    route_a << "10.1.1.1/32";

    vector<std::string> community_a;
    community_a.push_back("no-reoriginate");
    test::RouteAttributes attr_a(community_a);
    test::NextHop nexthop_a("192.168.1.1");
    agent_a_->AddRoute("blue", route_a.str(), nexthop_a, attr_a);
    task_util::WaitForIdle();

    // Verify that route showed up on agents A and B
    VerifyRouteExists(agent_a_, "blue", route_a.str(),
                      "192.168.1.1", community_a);
    VerifyRouteExists(agent_b_, "blue", route_a.str(),
                      "192.168.1.1", community_a);

    community_a.push_back("64521:9999");
    attr_a.SetCommunities(community_a);
    agent_a_->AddRoute("blue", route_a.str(), nexthop_a, attr_a);
    task_util::WaitForIdle();

    sort(community_a.begin(), community_a.end());
    // Verify that route showed up on agents A & B
    VerifyRouteExists(agent_a_, "blue", route_a.str(),
                      "192.168.1.1", community_a);
    VerifyRouteExists(agent_b_, "blue", route_a.str(),
                      "192.168.1.1", community_a);

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
// Route added with No-Export community. Validate the route advertisement in CN
//
TEST_F(BgpXmppInetvpn2ControlNodeTest, RouteWithNoExportCommunity) {
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

    // Add route from agent A with NO_EXPORT community
    stringstream route_a;
    route_a << "10.1.1.1/32";

    vector<std::string> community_a;
    community_a.push_back("no-export");
    test::RouteAttributes attr_a(community_a);
    test::NextHop nexthop_a("192.168.1.1");
    agent_a_->AddRoute("blue", route_a.str(), nexthop_a, attr_a);
    task_util::WaitForIdle();

    // Verify that route showed up on agent A
    VerifyRouteExists(agent_a_, "blue", route_a.str(),
                      "192.168.1.1", community_a);
    // Verify that route doesn't show up on agent B as it is tagged with
    // "no-export" community
    VerifyRouteNoExists(agent_b_, "blue", route_a.str());

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
// Route added with explicit med has expected med.
//
TEST_F(BgpXmppInetvpn2ControlNodeTest, RouteExplicitMed) {
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

    // Add route from agent A with med 500.
    stringstream route_a;
    route_a << "10.1.1.1/32";
    agent_a_->AddRoute("blue", route_a.str(), "192.168.1.1", 0, 500);
    task_util::WaitForIdle();

    // Verify that route showed up on agents A and B with med 500.
    VerifyRouteExists(agent_a_, "blue", route_a.str(), "192.168.1.1", 0, 500);
    VerifyRouteExists(agent_b_, "blue", route_a.str(), "192.168.1.1", 0, 500);

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
// Route added with local preference and no med has auto calculated med.
//
TEST_F(BgpXmppInetvpn2ControlNodeTest, RouteLocalPrefToMed) {
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

    // Add route from agent A with local preference 100.
    stringstream route_a;
    route_a << "10.1.1.1/32";
    agent_a_->AddRoute("blue", route_a.str(), "192.168.1.1", 100);
    task_util::WaitForIdle();

    // Verify that route showed up on agents A and B with med 200.
    VerifyRouteExists(agent_a_, "blue", route_a.str(), "192.168.1.1", 0, 200);
    VerifyRouteExists(agent_b_, "blue", route_a.str(), "192.168.1.1", 0, 200);

    // Change route from agent A to local preference 200.
    agent_a_->AddRoute("blue", route_a.str(), "192.168.1.1", 200);
    task_util::WaitForIdle();

    // Verify that route showed up on agents A and B with med 100.
    VerifyRouteExists(agent_a_, "blue", route_a.str(), "192.168.1.1", 0, 100);
    VerifyRouteExists(agent_b_, "blue", route_a.str(), "192.168.1.1", 0, 100);

    // Change route from agent A to local preference 400.
    agent_a_->AddRoute("blue", route_a.str(), "192.168.1.1", 400);
    task_util::WaitForIdle();

    // Verify that route showed up on agents A and B with med UINT32_MAX - 400.
    VerifyRouteExists(
        agent_a_, "blue", route_a.str(), "192.168.1.1", 0, 0xFFFFFFFF - 400);
    VerifyRouteExists(
        agent_b_, "blue", route_a.str(), "192.168.1.1", 0, 0xFFFFFFFF - 400);

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
// Route added with local preference has auto calculated local preference
// (via med) in the other AS.
//
TEST_F(BgpXmppInetvpn2ControlNodeTest, RouteLocalPrefToLocalPref) {
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

    // Add route from agent A with local preference 100.
    stringstream route_a;
    route_a << "10.1.1.1/32";
    agent_a_->AddRoute("blue", route_a.str(), "192.168.1.1", 100);
    task_util::WaitForIdle();

    // Verify that route showed up on agents A and B with local preference 100.
    VerifyRouteExists(agent_a_, "blue", route_a.str(), "192.168.1.1", 100);
    VerifyRouteExists(agent_b_, "blue", route_a.str(), "192.168.1.1", 100);

    // Change route from agent A to local preference 200.
    agent_a_->AddRoute("blue", route_a.str(), "192.168.1.1", 200);
    task_util::WaitForIdle();

    // Verify that route showed up on agents A and B with local preference 200.
    VerifyRouteExists(agent_a_, "blue", route_a.str(), "192.168.1.1", 200);
    VerifyRouteExists(agent_b_, "blue", route_a.str(), "192.168.1.1", 200);

    // Change route from agent A to local preference 400.
    agent_a_->AddRoute("blue", route_a.str(), "192.168.1.1", 400);
    task_util::WaitForIdle();

    // Verify that route showed up on agents A and B with local preference 400.
    VerifyRouteExists(agent_a_, "blue", route_a.str(), "192.168.1.1", 400);
    VerifyRouteExists(agent_b_, "blue", route_a.str(), "192.168.1.1", 400);

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
// Route is not advertised to agent that subscribed with no-ribout.
//
TEST_F(BgpXmppInetvpn2ControlNodeTest, SubscribeNoRibOut1) {
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

    // Register agent A to blue instance with no-ribout.
    // Register agent B to blue instance.
    agent_a_->Subscribe("blue", 1, true, true);
    agent_b_->Subscribe("blue", 1);

    // Add route from agent A.
    stringstream route_a;
    route_a << "10.1.1.1/32";
    agent_a_->AddRoute("blue", route_a.str(), "192.168.1.1", 200);
    task_util::WaitForIdle();

    // Verify that route showed up on agent B, but not on agent A.
    VerifyRouteExists(agent_b_, "blue", route_a.str(), "192.168.1.1", 200);
    VerifyRouteNoExists(agent_a_, "blue", route_a.str());

    // Unsubscribe agent B from blue instance and subscribe with no-ribout.
    agent_b_->Unsubscribe("blue");
    agent_b_->Subscribe("blue", 1, true, true);

    // Verify that route is not present at agents A and B.
    VerifyRouteNoExists(agent_a_, "blue", route_a.str());
    VerifyRouteNoExists(agent_b_, "blue", route_a.str());

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
// Route is not advertised to agent that subscribed with no-ribout.
//
TEST_F(BgpXmppInetvpn2ControlNodeTest, SubscribeNoRibOut2) {
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

    // Register agent A to blue instance with no-ribout.
    // Register agent B to blue instance.
    agent_a_->Subscribe("blue", 1, true, true);
    agent_b_->Subscribe("blue", 1);

    // Add route from agent B.
    stringstream route_b;
    route_b << "10.1.1.2/32";
    agent_b_->AddRoute("blue", route_b.str(), "192.168.1.2", 200);
    task_util::WaitForIdle();

    // Verify that route showed up on agent B, but not on agent A.
    VerifyRouteExists(agent_b_, "blue", route_b.str(), "192.168.1.2", 200);
    VerifyRouteNoExists(agent_a_, "blue", route_b.str());

    // Unsubscribe agent A from blue instance and subscribe without no-ribout.
    agent_a_->Unsubscribe("blue");
    agent_a_->Subscribe("blue", 1);

    // Verify that route is present at agents A and B.
    VerifyRouteExists(agent_a_, "blue", route_b.str(), "192.168.1.2", 200);
    VerifyRouteExists(agent_b_, "blue", route_b.str(), "192.168.1.2", 200);

    // Delete route from agent B.
    agent_b_->DeleteRoute("blue", route_b.str());
    task_util::WaitForIdle();

    // Verify that route is deleted at agents A and B.
    VerifyRouteNoExists(agent_a_, "blue", route_b.str());
    VerifyRouteNoExists(agent_b_, "blue", route_b.str());

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
    for (int idx = 0; idx < 128; ++idx) {
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
// Two agents in same VN on different CNs.
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
    for (int idx = 0; idx < 128; ++idx) {
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
// Agent flaps a route by adding and deleting it repeatedly.
// Two agents in same VN on same CN.
//
TEST_F(BgpXmppInetvpn2ControlNodeTest, RouteFlap3) {
    Configure(bs_x_, config_1_control_node_2_vns);
    task_util::WaitForIdle();

    // Create XMPP Agent A connected to XMPP server X.
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, "agent-a", xs_x_->GetPort(),
            "127.0.0.1", "127.0.0.1"));
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    // Create XMPP Agent B connected to XMPP server X.
    agent_b_.reset(
        new test::NetworkAgentMock(&evm_, "agent-b", xs_x_->GetPort(),
            "127.0.0.2", "127.0.0.1"));
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());

    // Register to blue instance
    agent_a_->Subscribe("blue", 1);
    agent_b_->Subscribe("blue", 1);

    // Add and delete route from agent A repeatedly.
    stringstream route_a;
    route_a << "10.1.1.1/32";
    for (int idx = 0; idx < 128; ++idx) {
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
// Agent flaps a route by adding and deleting it repeatedly.
// Two agents in different VNs on same CN.
//
TEST_F(BgpXmppInetvpn2ControlNodeTest, RouteFlap4) {
    // Configure and add connection between blue and pink on bgp server X.
    Configure(bs_x_, config_1_control_node_2_vns);
    AddConnection(bs_x_, "blue", "pink");
    task_util::WaitForIdle();

    // Create XMPP Agent A connected to XMPP server X.
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, "agent-a", xs_x_->GetPort(),
            "127.0.0.1", "127.0.0.1"));
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    // Create XMPP Agent B connected to XMPP server X.
    agent_b_.reset(
        new test::NetworkAgentMock(&evm_, "agent-b", xs_x_->GetPort(),
            "127.0.0.2", "127.0.0.1"));
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());

    // Register to blue instance
    agent_a_->Subscribe("blue", 1);
    agent_b_->Subscribe("pink", 2);

    // Add and delete route from agent A repeatedly.
    stringstream route_a;
    route_a << "10.1.1.1/32";
    for (int idx = 0; idx < 128; ++idx) {
        agent_a_->AddRoute("blue", route_a.str(), "192.168.1.1");
        usleep(5000);
        agent_a_->DeleteRoute("blue", route_a.str());
    }

    // Verify that route is deleted at agents A and B.
    VerifyRouteNoExists(agent_a_, "blue", route_a.str());
    VerifyRouteNoExists(agent_b_, "pink", route_a.str());

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
TEST_F(BgpXmppInetvpn2ControlNodeTest, MultipleRouteAddDelete1) {
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
// Multiple routes are exchanged correctly.
// Each route has unique attributes to force an update per route.
//
TEST_F(BgpXmppInetvpn2ControlNodeTest, MultipleRouteAddDelete2) {
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

    // Register to blue instance on agent A.
    agent_a_->Subscribe("blue", 1);

    // Add routes from agent A.
    for (int idx = 0; idx < kRouteCount; ++idx) {
        agent_a_->AddRoute("blue", BuildPrefix(idx), "192.168.1.1", 100 + idx);
    }

    // Verify that routes showed up on agent A.
    for (int idx = 0; idx < kRouteCount; ++idx) {
        VerifyRouteExists(
            agent_a_, "blue", BuildPrefix(idx), "192.168.1.1", 100 + idx);
    }

    // Register to blue instance on agent B.
    agent_b_->Subscribe("blue", 1);

    // Verify that routes showed up on agent B.
    for (int idx = 0; idx < kRouteCount; ++idx) {
        VerifyRouteExists(
            agent_b_, "blue", BuildPrefix(idx), "192.168.1.1", 100 + idx);
    }

    // Pause update generation on X.
    bs_x_->update_sender()->DisableProcessing();

    // Unregister to blue instance on agent B.
    agent_b_->Unsubscribe("blue");

    // Verify that routes got removed on agent B.
    for (int idx = 0; idx < kRouteCount; ++idx) {
        VerifyRouteNoExists(agent_b_, "blue", BuildPrefix(idx));
    }

    // Verify that bgp.l3vpn.0 output queues have built up on X.
    VerifyOutputQueueDepth(bs_x_, kRouteCount);
    task_util::WaitForIdle();

    // Resume update generation on X.
    bs_x_->update_sender()->EnableProcessing();
    task_util::WaitForIdle();

    // Delete routes from agent A.
    for (int idx = 0; idx < kRouteCount; ++idx) {
        agent_a_->DeleteRoute("blue", BuildPrefix(idx));
    }

    // Verify that routes are deleted at agent A.
    for (int idx = 0; idx < kRouteCount; ++idx) {
        VerifyRouteNoExists(agent_a_, "blue", BuildPrefix(idx));
    }

    // Unregister to blue instance on agent A.
    agent_a_->Unsubscribe("blue");

    // Verify bgp update and socket write counters.
    // X->Y : 2 route-target (1 advertise, 1 withdraw) +
    //        515 inet-vpn (512 advertise, 3 withdraw) +
    //        2 end-of-rib (1 route-target, 1 inet-vpn)
    // Y->X : 2 route-target (1 advertise, 1 withdraw) +
    //        2 end-of-rib (1 route-target, 1 inet-vpn)
    // Socket writes must be way fewer than kRouteCount
    // since we coalesce many updates into fewer socket
    // writes.
    const BgpPeer *peer_xy = VerifyPeerExists(bs_x_, bs_y_);
    const BgpPeer *peer_yx = VerifyPeerExists(bs_y_, bs_x_);
    TASK_UTIL_EXPECT_EQ(peer_xy->get_rx_update(), peer_yx->get_tx_update());
    TASK_UTIL_EXPECT_EQ(peer_xy->get_tx_update(), peer_yx->get_rx_update());
    TASK_UTIL_EXPECT_EQ(4 + 3 + kRouteCount, peer_xy->get_tx_update());
    TASK_UTIL_EXPECT_EQ(4, peer_yx->get_tx_update());
    TASK_UTIL_EXPECT_GE(32, peer_xy->get_socket_writes());

    // Close the sessions.
    agent_a_->SessionDown();
    agent_b_->SessionDown();
}

//
// Multiple routes are exchanged correctly.
// Force creation of large update messages.
//
TEST_F(BgpXmppInetvpn2ControlNodeTest, MultipleRouteAddDelete3) {
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

    // Verify table walk count for blue.inet.0.
    BgpTable *blue_x = VerifyTableExists(bs_x_, "blue.inet.0");
    BgpTable *blue_y = VerifyTableExists(bs_y_, "blue.inet.0");
    TASK_UTIL_EXPECT_EQ(0, blue_x->walk_complete_count());
    TASK_UTIL_EXPECT_EQ(0, blue_y->walk_complete_count());
    task_util::WaitForIdle();

    // Register to blue instance
    agent_a_->Subscribe("blue", 1);
    agent_b_->Subscribe("blue", 1);
    task_util::WaitForIdle();

    // Verify that subscribe completed.
    TASK_UTIL_EXPECT_EQ(1, blue_x->walk_complete_count());
    TASK_UTIL_EXPECT_EQ(1, blue_y->walk_complete_count());
    task_util::WaitForIdle();

    // Pause update generation on X.
    bs_x_->update_sender()->DisableProcessing();

    // Add routes from agent A.
    for (int idx = 0; idx < kRouteCount; ++idx) {
        agent_a_->AddRoute("blue", BuildPrefix(idx), "192.168.1.1");
    }
    task_util::WaitForIdle();

    // Verify that blue.inet.0 and bgp.l3vpn.0 output queues have built up on X.
    VerifyOutputQueueDepth(bs_x_, 2 * kRouteCount);
    task_util::WaitForIdle();

    // Pause update generation on Y.
    bs_y_->update_sender()->DisableProcessing();

    // Resume update generation on X.
    bs_x_->update_sender()->EnableProcessing();
    task_util::WaitForIdle();

    // Verify that blue.inet.0 and bgp.l3vpn.0 output queues are drained on X.
    VerifyOutputQueueDepth(bs_x_, 0);
    task_util::WaitForIdle();

    // Verify that blue.inet.0 output queues have built up on Y.
    VerifyOutputQueueDepth(bs_y_, kRouteCount);
    task_util::WaitForIdle();

    // Resume update generation on Y.
    bs_y_->update_sender()->EnableProcessing();
    task_util::WaitForIdle();

    // Verify that blue.inet.0 output queues are drained on Y.
    VerifyOutputQueueDepth(bs_y_, 0);
    task_util::WaitForIdle();

    // Verify that routes showed up on agents A and B.
    for (int idx = 0; idx < kRouteCount; ++idx) {
        VerifyRouteExists(agent_a_, "blue", BuildPrefix(idx), "192.168.1.1");
        VerifyRouteExists(agent_b_, "blue", BuildPrefix(idx), "192.168.1.1");
    }

    // Pause update generation on server X.
    bs_x_->update_sender()->DisableProcessing();

    // Delete routes from agent A.
    for (int idx = 0; idx < kRouteCount; ++idx) {
        agent_a_->DeleteRoute("blue", BuildPrefix(idx));
    }
    task_util::WaitForIdle();

    // Verify that blue.inet.0 and bgp.l3vpn.0 output queues have built up on X.
    VerifyOutputQueueDepth(bs_x_, 2 * kRouteCount);
    task_util::WaitForIdle();

    // Pause update generation on Y.
    bs_y_->update_sender()->DisableProcessing();

    // Resume update generation on server X.
    bs_x_->update_sender()->EnableProcessing();
    task_util::WaitForIdle();

    // Verify that blue.inet.0 and bgp.l3vpn.0 output queues are drained on X.
    VerifyOutputQueueDepth(bs_x_, 0);

    // Verify that blue.inet.0 output queues have built up on Y.
    VerifyOutputQueueDepth(bs_y_, kRouteCount);
    task_util::WaitForIdle();

    // Resume update generation on Y.
    bs_y_->update_sender()->EnableProcessing();
    task_util::WaitForIdle();

    // Verify that blue.inet.0 output queues are drained on Y.
    VerifyOutputQueueDepth(bs_y_, 0);

    // Verify that routes are deleted at agents A and B.
    for (int idx = 0; idx < kRouteCount; ++idx) {
        VerifyRouteNoExists(agent_a_, "blue", BuildPrefix(idx));
        VerifyRouteNoExists(agent_b_, "blue", BuildPrefix(idx));
    }

    // Unregister to blue instance
    agent_a_->Unsubscribe("blue");
    agent_b_->Unsubscribe("blue");

    // Verify bgp update counters.
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

    // Verify bgp reach/unreach, end-of-rib and total counts.
    // 1 route-target and kRouteCount inet-vpn routes.
    // 2 end-of-ribs - one for route-target and one for inet-vpn.
    TASK_UTIL_EXPECT_EQ(1 + kRouteCount, peer_xy->get_tx_route_reach());
    TASK_UTIL_EXPECT_EQ(1 + kRouteCount, peer_xy->get_tx_route_unreach());
    TASK_UTIL_EXPECT_EQ(2, peer_xy->get_tx_end_of_rib());
    TASK_UTIL_EXPECT_EQ(
        2 * (1 + kRouteCount) + 2, peer_xy->get_tx_route_total());

    // Verify bgp update message counters.
    // X->Y : 2 route-target (1 advertise, 1 withdraw) +
    //        6 inet-vpn (3 advertise, 3 withdraw) +
    //        2 end-of-rib (1 route-target, 1 inet-vpn)
    // Y->X : 2 route-target (1 advertise, 1 withdraw) +
    //        2 end-of-rib (1 route-target, 1 inet-vpn)
    TASK_UTIL_EXPECT_EQ(peer_xy->get_rx_update(), peer_yx->get_tx_update());
    TASK_UTIL_EXPECT_EQ(peer_xy->get_tx_update(), peer_yx->get_rx_update());
    TASK_UTIL_EXPECT_EQ(10, peer_xy->get_tx_update());
    TASK_UTIL_EXPECT_EQ(4, peer_yx->get_tx_update());

    // Verify xmpp update counters.
    const BgpXmppChannel *xc_a =
        VerifyXmppChannelExists(cm_x_.get(), "agent-a");
    const BgpXmppChannel *xc_b =
        VerifyXmppChannelExists(cm_y_.get(), "agent-b");
    TASK_UTIL_EXPECT_EQ(0 + kRouteCount, xc_a->get_rx_route_reach());
    TASK_UTIL_EXPECT_EQ(0 + kRouteCount, xc_a->get_tx_route_reach());
    TASK_UTIL_EXPECT_EQ(0 + kRouteCount, xc_a->get_rx_route_unreach());
    TASK_UTIL_EXPECT_EQ(0 + kRouteCount, xc_a->get_tx_route_unreach());
    TASK_UTIL_EXPECT_EQ(0, xc_b->get_rx_route_reach());
    TASK_UTIL_EXPECT_EQ(0 + kRouteCount, xc_b->get_tx_route_reach());
    TASK_UTIL_EXPECT_EQ(0, xc_b->get_rx_route_unreach());
    TASK_UTIL_EXPECT_EQ(0 + kRouteCount, xc_b->get_tx_route_unreach());

    // Verify xmpp update message counters.
    // agent-a: 16 (kRouteCount/BgpXmppMessage::kMaxReachCount) advertise +
    //           2 (kRouteCount/BgpXmppMessage::kMaxUnreachCount) withdraw
    // agent-b: 16 (kRouteCount/BgpXmppMessage::kMaxReachCount) advertise +
    //           2 (kRouteCount/BgpXmppMessage::kMaxUnreachCount) withdraw
    TASK_UTIL_EXPECT_EQ(18, xc_a->get_tx_update());
    TASK_UTIL_EXPECT_EQ(18, xc_b->get_tx_update());

    // Close the sessions.
    agent_a_->SessionDown();
    agent_b_->SessionDown();
}

//
// Multiple routes are exchanged correctly.
// Large xmpp update messages even though attributes for each route are
// different.
// Small bgp update messages since attributes for each route are different.
// Each route has a different local pref and med.
//
TEST_F(BgpXmppInetvpn2ControlNodeTest, MultipleRouteAddDelete4) {
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

    // Verify table walk count for blue.inet.0.
    BgpTable *blue_x = VerifyTableExists(bs_x_, "blue.inet.0");
    BgpTable *blue_y = VerifyTableExists(bs_y_, "blue.inet.0");
    TASK_UTIL_EXPECT_EQ(0, blue_x->walk_complete_count());
    TASK_UTIL_EXPECT_EQ(0, blue_y->walk_complete_count());
    task_util::WaitForIdle();

    // Register to blue instance
    agent_a_->Subscribe("blue", 1);
    agent_b_->Subscribe("blue", 1);
    task_util::WaitForIdle();

    // Verify that subscribe completed.
    TASK_UTIL_EXPECT_EQ(1, blue_x->walk_complete_count());
    TASK_UTIL_EXPECT_EQ(1, blue_y->walk_complete_count());
    task_util::WaitForIdle();

    // Pause update generation on X.
    bs_x_->update_sender()->DisableProcessing();

    // Add routes from agent A.
    for (int idx = 0; idx < kRouteCount; ++idx) {
        int lpref = idx + 1;
        int med = idx + 1;
        agent_a_->AddRoute("blue", BuildPrefix(idx), "192.168.1.1", lpref, med);
    }
    task_util::WaitForIdle();

    // Verify that blue.inet.0 and bgp.l3vpn.0 output queues have built up on X.
    VerifyOutputQueueDepth(bs_x_, 2 * kRouteCount);
    task_util::WaitForIdle();

    // Pause update generation on Y.
    bs_y_->update_sender()->DisableProcessing();

    // Resume update generation on X.
    bs_x_->update_sender()->EnableProcessing();
    task_util::WaitForIdle();

    // Verify that blue.inet.0 and bgp.l3vpn.0 output queues are drained on X.
    VerifyOutputQueueDepth(bs_x_, 0);
    task_util::WaitForIdle();

    // Verify that blue.inet.0 output queues have built up on Y.
    VerifyOutputQueueDepth(bs_y_, kRouteCount);
    task_util::WaitForIdle();

    // Resume update generation on Y.
    bs_y_->update_sender()->EnableProcessing();
    task_util::WaitForIdle();

    // Verify that blue.inet.0 output queues are drained on Y.
    VerifyOutputQueueDepth(bs_y_, 0);
    task_util::WaitForIdle();

    // Verify that routes showed up on agents A and B.
    for (int idx = 0; idx < kRouteCount; ++idx) {
        VerifyRouteExists(agent_a_, "blue", BuildPrefix(idx), "192.168.1.1");
        VerifyRouteExists(agent_b_, "blue", BuildPrefix(idx), "192.168.1.1");
    }

    // Pause update generation on server X.
    bs_x_->update_sender()->DisableProcessing();

    // Delete routes from agent A.
    for (int idx = 0; idx < kRouteCount; ++idx) {
        agent_a_->DeleteRoute("blue", BuildPrefix(idx));
    }
    task_util::WaitForIdle();

    // Verify that blue.inet.0 and bgp.l3vpn.0 output queues have built up on X.
    VerifyOutputQueueDepth(bs_x_, 2 * kRouteCount);
    task_util::WaitForIdle();

    // Pause update generation on Y.
    bs_y_->update_sender()->DisableProcessing();

    // Resume update generation on server X.
    bs_x_->update_sender()->EnableProcessing();
    task_util::WaitForIdle();

    // Verify that blue.inet.0 and bgp.l3vpn.0 output queues are drained on X.
    VerifyOutputQueueDepth(bs_x_, 0);

    // Verify that blue.inet.0 output queues have built up on Y.
    VerifyOutputQueueDepth(bs_y_, kRouteCount);
    task_util::WaitForIdle();

    // Resume update generation on Y.
    bs_y_->update_sender()->EnableProcessing();
    task_util::WaitForIdle();

    // Verify that blue.inet.0 output queues are drained on Y.
    VerifyOutputQueueDepth(bs_y_, 0);

    // Verify that routes are deleted at agents A and B.
    for (int idx = 0; idx < kRouteCount; ++idx) {
        VerifyRouteNoExists(agent_a_, "blue", BuildPrefix(idx));
        VerifyRouteNoExists(agent_b_, "blue", BuildPrefix(idx));
    }

    // Unregister to blue instance
    agent_a_->Unsubscribe("blue");
    agent_b_->Unsubscribe("blue");

    // Verify bgp update counters.
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

    // Verify bgp reach/unreach, end-of-rib and total counts.
    // 1 route-target and kRouteCount inet-vpn routes.
    // 2 end-of-ribs - one for route-target and one for inet-vpn.
    TASK_UTIL_EXPECT_EQ(1 + kRouteCount, peer_xy->get_tx_route_reach());
    TASK_UTIL_EXPECT_EQ(1 + kRouteCount, peer_xy->get_tx_route_unreach());
    TASK_UTIL_EXPECT_EQ(2, peer_xy->get_tx_end_of_rib());
    TASK_UTIL_EXPECT_EQ(
        2 * (1 + kRouteCount) + 2, peer_xy->get_tx_route_total());

    // Verify bgp update message counters.
    // X->Y : 2 route-target (1 advertise, 1 withdraw) +
    //        515 inet-vpn (512 advertise, 3 withdraw) +
    //        2 end-of-rib (1 route-target, 1 inet-vpn)
    // Y->X : 2 route-target (1 advertise, 1 withdraw) +
    //        2 end-of-rib (1 route-target, 1 inet-vpn)
    TASK_UTIL_EXPECT_EQ(peer_xy->get_rx_update(), peer_yx->get_tx_update());
    TASK_UTIL_EXPECT_EQ(peer_xy->get_tx_update(), peer_yx->get_rx_update());
    TASK_UTIL_EXPECT_EQ(4 + 3 + kRouteCount, peer_xy->get_tx_update());
    TASK_UTIL_EXPECT_EQ(4, peer_yx->get_tx_update());

    // Verify xmpp update counters.
    const BgpXmppChannel *xc_a =
        VerifyXmppChannelExists(cm_x_.get(), "agent-a");
    const BgpXmppChannel *xc_b =
        VerifyXmppChannelExists(cm_y_.get(), "agent-b");
    TASK_UTIL_EXPECT_EQ(0 + kRouteCount, xc_a->get_rx_route_reach());
    TASK_UTIL_EXPECT_EQ(0 + kRouteCount, xc_a->get_tx_route_reach());
    TASK_UTIL_EXPECT_EQ(0 + kRouteCount, xc_a->get_rx_route_unreach());
    TASK_UTIL_EXPECT_EQ(0 + kRouteCount, xc_a->get_tx_route_unreach());
    TASK_UTIL_EXPECT_EQ(0, xc_b->get_rx_route_reach());
    TASK_UTIL_EXPECT_EQ(0 + kRouteCount, xc_b->get_tx_route_reach());
    TASK_UTIL_EXPECT_EQ(0, xc_b->get_rx_route_unreach());
    TASK_UTIL_EXPECT_EQ(0 + kRouteCount, xc_b->get_tx_route_unreach());

    // Verify xmpp update message counters.
    // agent-a: 16 (kRouteCount/BgpXmppMessage::kMaxReachCount) advertise +
    //           2 (kRouteCount/BgpXmppMessage::kMaxUnreachCount) withdraw
    // agent-b: 16 (kRouteCount/BgpXmppMessage::kMaxReachCount) advertise +
    //           2 (kRouteCount/BgpXmppMessage::kMaxUnreachCount) withdraw
    TASK_UTIL_EXPECT_EQ(18, xc_a->get_tx_update());
    TASK_UTIL_EXPECT_EQ(18, xc_b->get_tx_update());

    // Close the sessions.
    agent_a_->SessionDown();
    agent_b_->SessionDown();
}

//
// Multiple routes are exchanged correctly.
// Reach and unreach items go in separate xmpp messages even though add and
// delete of routes is interleaved.
//
TEST_F(BgpXmppInetvpn2ControlNodeTest, MultipleRouteAddDelete5) {
    string nh_str("192.168.1.1");
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

    // Verify table walk count for blue.inet.0.
    BgpTable *blue_x = VerifyTableExists(bs_x_, "blue.inet.0");
    BgpTable *blue_y = VerifyTableExists(bs_y_, "blue.inet.0");
    TASK_UTIL_EXPECT_EQ(0, blue_x->walk_complete_count());
    TASK_UTIL_EXPECT_EQ(0, blue_y->walk_complete_count());
    task_util::WaitForIdle();

    // Register to blue instance
    agent_a_->Subscribe("blue", 1);
    agent_b_->Subscribe("blue", 1);
    task_util::WaitForIdle();

    // Verify that subscribe completed.
    TASK_UTIL_EXPECT_EQ(1, blue_x->walk_complete_count());
    TASK_UTIL_EXPECT_EQ(1, blue_y->walk_complete_count());
    task_util::WaitForIdle();

    // Pause update generation on Y.
    bs_y_->update_sender()->DisableProcessing();

    // Add even routes from agent A.
    for (int idx = 0; idx < kRouteCount; idx += 2) {
        int lpref = idx + 1;
        int med = idx + 1;
        agent_a_->AddRoute("blue", BuildPrefix(idx), nh_str, lpref, med);
    }
    task_util::WaitForIdle();

    // Verify that blue.inet.0 output queues have built up on Y.
    VerifyOutputQueueDepth(bs_y_, kRouteCount / 2);
    task_util::WaitForIdle();

    // Resume update generation on Y.
    bs_y_->update_sender()->EnableProcessing();
    task_util::WaitForIdle();

    // Verify that blue.inet.0 output queues are drained on Y.
    VerifyOutputQueueDepth(bs_y_, 0);

    // Verify that even routes showed up on agents A and B.
    for (int idx = 0; idx < kRouteCount; idx += 2) {
        VerifyRouteExists(agent_a_, "blue", BuildPrefix(idx), nh_str);
        VerifyRouteExists(agent_b_, "blue", BuildPrefix(idx), nh_str);
    }

    // Pause update generation on Y.
    bs_y_->update_sender()->DisableProcessing();

    // Add odd route and delete even routes from agent A.
    for (int idx = 0; idx < kRouteCount; ++idx) {
        if (idx % 2 == 1) {
            int lpref = idx + 1;
            int med = idx + 1;
            agent_a_->AddRoute("blue", BuildPrefix(idx), nh_str, lpref, med);
        } else {
            agent_a_->DeleteRoute("blue", BuildPrefix(idx));
        }
    }
    task_util::WaitForIdle();

    // Verify that blue.inet.0 output queues have built up on Y.
    VerifyOutputQueueDepth(bs_y_, kRouteCount);
    task_util::WaitForIdle();

    // Resume update generation on Y.
    bs_y_->update_sender()->EnableProcessing();
    task_util::WaitForIdle();

    // Verify that blue.inet.0 output queues are drained on Y.
    VerifyOutputQueueDepth(bs_y_, 0);

    // Verify odd routes exist and even routes are deleted at agents A and B.
    for (int idx = 0; idx < kRouteCount; ++idx) {
        if (idx % 2 == 1) {
            VerifyRouteExists(agent_a_, "blue", BuildPrefix(idx), nh_str);
            VerifyRouteExists(agent_b_, "blue", BuildPrefix(idx), nh_str);
        } else {
            VerifyRouteNoExists(agent_a_, "blue", BuildPrefix(idx));
            VerifyRouteNoExists(agent_b_, "blue", BuildPrefix(idx));
        }
    }

    // Pause update generation on Y.
    bs_y_->update_sender()->DisableProcessing();

    // Delete odd routes from agent A.
    for (int idx = 1; idx < kRouteCount; idx += 2) {
        agent_a_->DeleteRoute("blue", BuildPrefix(idx));
    }
    task_util::WaitForIdle();

    // Verify that blue.inet.0 output queues have built up on Y.
    VerifyOutputQueueDepth(bs_y_, kRouteCount / 2);
    task_util::WaitForIdle();

    // Resume update generation on Y.
    bs_y_->update_sender()->EnableProcessing();
    task_util::WaitForIdle();

    // Verify that blue.inet.0 output queues are drained on Y.
    VerifyOutputQueueDepth(bs_y_, 0);

    // Verify all routes are deleted at agents A and B.
    for (int idx = 0; idx < kRouteCount; ++idx) {
        VerifyRouteNoExists(agent_a_, "blue", BuildPrefix(idx));
        VerifyRouteNoExists(agent_b_, "blue", BuildPrefix(idx));
    }

    // Unregister to blue instance
    agent_a_->Unsubscribe("blue");
    agent_b_->Unsubscribe("blue");

    // Verify xmpp update counters.
    const BgpXmppChannel *xc_b =
        VerifyXmppChannelExists(cm_y_.get(), "agent-b");
    TASK_UTIL_EXPECT_EQ(0, xc_b->get_rx_route_reach());
    TASK_UTIL_EXPECT_EQ(0 + kRouteCount, xc_b->get_tx_route_reach());
    TASK_UTIL_EXPECT_EQ(0, xc_b->get_rx_route_unreach());
    TASK_UTIL_EXPECT_EQ(0 + kRouteCount, xc_b->get_tx_route_unreach());

    // Verify xmpp update message counters.
    // agent-b: 16 (kRouteCount/BgpXmppMessage::kMaxReachCount) advertise +
    //           2 (kRouteCount/BgpXmppMessage::kMaxUnreachCount) withdraw
    TASK_UTIL_EXPECT_EQ(18, xc_b->get_tx_update());

    // Close the sessions.
    agent_a_->SessionDown();
    agent_b_->SessionDown();
}

//
// Multiple routes are exchanged correctly.
// Large xmpp update messages even though attributes for each route are
// different.
// Small bgp update messages since attributes for each route are different.
// Each route has a different local pref and sequence number.
// Odd and even routes have different security groups.
//
TEST_F(BgpXmppInetvpn2ControlNodeTest, MultipleRouteAddDelete6) {
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

    // Verify table walk count for blue.inet.0.
    BgpTable *blue_x = VerifyTableExists(bs_x_, "blue.inet.0");
    BgpTable *blue_y = VerifyTableExists(bs_y_, "blue.inet.0");
    TASK_UTIL_EXPECT_EQ(0, blue_x->walk_complete_count());
    TASK_UTIL_EXPECT_EQ(0, blue_y->walk_complete_count());
    task_util::WaitForIdle();

    // Register to blue instance
    agent_a_->Subscribe("blue", 1);
    agent_b_->Subscribe("blue", 1);
    task_util::WaitForIdle();

    // Verify that subscribe completed.
    TASK_UTIL_EXPECT_EQ(1, blue_x->walk_complete_count());
    TASK_UTIL_EXPECT_EQ(1, blue_y->walk_complete_count());
    task_util::WaitForIdle();

    // Pause update generation on X.
    bs_x_->update_sender()->DisableProcessing();

    // Add routes from agent A.
    test::NextHop next_hop("192.168.1.1");
    vector<int> sgids1 = list_of(8000001)(8000003);
    vector<int> sgids2 = list_of(8000002)(8000004);
    for (int idx = 0; idx < kRouteCount; ++idx) {
        int lpref = idx + 1;
        int seq = idx + 1;
        if (idx % 2 == 0) {
            test::RouteAttributes attributes(lpref, seq, sgids2);
            agent_a_->AddRoute("blue", BuildPrefix(idx), next_hop, attributes);
        } else {
            test::RouteAttributes attributes(lpref, seq, sgids1);
            agent_a_->AddRoute("blue", BuildPrefix(idx), next_hop, attributes);
        }
    }
    task_util::WaitForIdle();

    // Verify that blue.inet.0 and bgp.l3vpn.0 output queues have built up on X.
    VerifyOutputQueueDepth(bs_x_, 2 * kRouteCount);
    task_util::WaitForIdle();

    // Pause update generation on Y.
    bs_y_->update_sender()->DisableProcessing();

    // Resume update generation on X.
    bs_x_->update_sender()->EnableProcessing();
    task_util::WaitForIdle();

    // Verify that blue.inet.0 and bgp.l3vpn.0 output queues are drained on X.
    VerifyOutputQueueDepth(bs_x_, 0);
    task_util::WaitForIdle();

    // Verify that blue.inet.0 output queues have built up on Y.
    VerifyOutputQueueDepth(bs_y_, kRouteCount);
    task_util::WaitForIdle();

    // Resume update generation on Y.
    bs_y_->update_sender()->EnableProcessing();
    task_util::WaitForIdle();

    // Verify that blue.inet.0 output queues are drained on Y.
    VerifyOutputQueueDepth(bs_y_, 0);
    task_util::WaitForIdle();

    // Verify that routes showed up on agents A and B.
    for (int idx = 0; idx < kRouteCount; ++idx) {
        int lpref = idx + 1;
        int seq = idx + 1;
        if (idx % 2 == 0) {
            VerifyRouteExists(agent_a_, "blue", BuildPrefix(idx), "192.168.1.1",
                lpref, seq, sgids2);
            VerifyRouteExists(agent_b_, "blue", BuildPrefix(idx), "192.168.1.1",
                lpref, seq, sgids2);
        } else {
            VerifyRouteExists(agent_a_, "blue", BuildPrefix(idx), "192.168.1.1",
                lpref, seq, sgids1);
            VerifyRouteExists(agent_b_, "blue", BuildPrefix(idx), "192.168.1.1",
                lpref, seq, sgids1);
        }
    }

    // Pause update generation on server X.
    bs_x_->update_sender()->DisableProcessing();

    // Delete routes from agent A.
    for (int idx = 0; idx < kRouteCount; ++idx) {
        agent_a_->DeleteRoute("blue", BuildPrefix(idx));
    }
    task_util::WaitForIdle();

    // Verify that blue.inet.0 and bgp.l3vpn.0 output queues have built up on X.
    VerifyOutputQueueDepth(bs_x_, 2 * kRouteCount);
    task_util::WaitForIdle();

    // Pause update generation on Y.
    bs_y_->update_sender()->DisableProcessing();

    // Resume update generation on server X.
    bs_x_->update_sender()->EnableProcessing();
    task_util::WaitForIdle();

    // Verify that blue.inet.0 and bgp.l3vpn.0 output queues are drained on X.
    VerifyOutputQueueDepth(bs_x_, 0);

    // Verify that blue.inet.0 output queues have built up on Y.
    VerifyOutputQueueDepth(bs_y_, kRouteCount);
    task_util::WaitForIdle();

    // Resume update generation on Y.
    bs_y_->update_sender()->EnableProcessing();
    task_util::WaitForIdle();

    // Verify that blue.inet.0 output queues are drained on Y.
    VerifyOutputQueueDepth(bs_y_, 0);

    // Verify that routes are deleted at agents A and B.
    for (int idx = 0; idx < kRouteCount; ++idx) {
        VerifyRouteNoExists(agent_a_, "blue", BuildPrefix(idx));
        VerifyRouteNoExists(agent_b_, "blue", BuildPrefix(idx));
    }

    // Unregister to blue instance
    agent_a_->Unsubscribe("blue");
    agent_b_->Unsubscribe("blue");

    // Verify bgp update counters.
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

    // Verify bgp reach/unreach, end-of-rib and total counts.
    // 1 route-target and kRouteCount inet-vpn routes.
    // 2 end-of-ribs - one for route-target and one for inet-vpn.
    TASK_UTIL_EXPECT_EQ(1 + kRouteCount, peer_xy->get_tx_route_reach());
    TASK_UTIL_EXPECT_EQ(1 + kRouteCount, peer_xy->get_tx_route_unreach());
    TASK_UTIL_EXPECT_EQ(2, peer_xy->get_tx_end_of_rib());
    TASK_UTIL_EXPECT_EQ(
        2 * (1 + kRouteCount) + 2, peer_xy->get_tx_route_total());

    // Verify bgp update message counters.
    // X->Y : 2 route-target (1 advertise, 1 withdraw) +
    //        515 inet-vpn (512 advertise, 3 withdraw) +
    //        2 end-of-rib (1 route-target, 1 inet-vpn)
    // Y->X : 2 route-target (1 advertise, 1 withdraw) +
    //        2 end-of-rib (1 route-target, 1 inet-vpn)
    TASK_UTIL_EXPECT_EQ(peer_xy->get_rx_update(), peer_yx->get_tx_update());
    TASK_UTIL_EXPECT_EQ(peer_xy->get_tx_update(), peer_yx->get_rx_update());
    TASK_UTIL_EXPECT_EQ(4 + 3 + kRouteCount, peer_xy->get_tx_update());
    TASK_UTIL_EXPECT_EQ(4, peer_yx->get_tx_update());

    // Verify xmpp update counters.
    const BgpXmppChannel *xc_a =
        VerifyXmppChannelExists(cm_x_.get(), "agent-a");
    const BgpXmppChannel *xc_b =
        VerifyXmppChannelExists(cm_y_.get(), "agent-b");
    TASK_UTIL_EXPECT_EQ(0 + kRouteCount, xc_a->get_rx_route_reach());
    TASK_UTIL_EXPECT_EQ(0 + kRouteCount, xc_a->get_tx_route_reach());
    TASK_UTIL_EXPECT_EQ(0 + kRouteCount, xc_a->get_rx_route_unreach());
    TASK_UTIL_EXPECT_EQ(0 + kRouteCount, xc_a->get_tx_route_unreach());
    TASK_UTIL_EXPECT_EQ(0, xc_b->get_rx_route_reach());
    TASK_UTIL_EXPECT_EQ(0 + kRouteCount, xc_b->get_tx_route_reach());
    TASK_UTIL_EXPECT_EQ(0, xc_b->get_rx_route_unreach());
    TASK_UTIL_EXPECT_EQ(0 + kRouteCount, xc_b->get_tx_route_unreach());

    // Verify xmpp update message counters.
    // agent-a: 16 (kRouteCount/BgpXmppMessage::kMaxReachCount) advertise +
    //           2 (kRouteCount/BgpXmppMessage::kMaxUnreachCount) withdraw
    // agent-b: 16 (kRouteCount/BgpXmppMessage::kMaxReachCount) advertise +
    //           2 (kRouteCount/BgpXmppMessage::kMaxUnreachCount) withdraw
    TASK_UTIL_EXPECT_EQ(18, xc_a->get_tx_update());
    TASK_UTIL_EXPECT_EQ(18, xc_b->get_tx_update());

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
    static const int kRouteTargetCount = 497;
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

//
// BgpUpdateSender WorkQueue processing code should not crash if a RibOut
// gets deleted while there's a WorkRibOut for the RibOut on the WorkQueue.
//
TEST_F(BgpXmppInetvpn2ControlNodeTest, UpdateSenderRibOutInvalidate) {
    Configure(config_2_control_nodes_no_rtf);
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

    // Verify table walk count for blue.inet.0.
    BgpTable *blue_x = VerifyTableExists(bs_x_, "blue.inet.0");
    BgpTable *pink_x = VerifyTableExists(bs_x_, "pink.inet.0");
    BgpTable *blue_y = VerifyTableExists(bs_y_, "blue.inet.0");
    BgpTable *pink_y = VerifyTableExists(bs_y_, "pink.inet.0");
    TASK_UTIL_EXPECT_EQ(0, blue_x->walk_complete_count());
    TASK_UTIL_EXPECT_EQ(0, pink_x->walk_complete_count());
    TASK_UTIL_EXPECT_EQ(0, blue_y->walk_complete_count());
    TASK_UTIL_EXPECT_EQ(0, pink_y->walk_complete_count());
    task_util::WaitForIdle();

    // Register to blue and pink instances.
    agent_a_->Subscribe("blue", 1);
    agent_a_->Subscribe("pink", 2);
    agent_b_->Subscribe("blue", 1);
    agent_b_->Subscribe("pink", 2);
    task_util::WaitForIdle();

    // Verify that subscribe completed.
    TASK_UTIL_EXPECT_EQ(1, blue_x->walk_complete_count());
    TASK_UTIL_EXPECT_EQ(1, pink_x->walk_complete_count());
    TASK_UTIL_EXPECT_EQ(1, blue_y->walk_complete_count());
    TASK_UTIL_EXPECT_EQ(1, pink_y->walk_complete_count());
    task_util::WaitForIdle();

    // Pause update generation on Y.
    bs_y_->update_sender()->DisableProcessing();

    // Add blue routes from agent A.
    for (int idx = 0; idx < 4; ++idx) {
        agent_a_->AddRoute("blue", BuildPrefix(idx), "192.168.1.1");
    }
    task_util::WaitForIdle();

    // Verify that blue.inet.0 output queues have built up on Y.
    VerifyOutputQueueDepth(bs_y_, 4);
    task_util::WaitForIdle();

    // Unregister to blue instance on B.
    agent_b_->Unsubscribe("blue");

    // Verify that blue.inet.0 output queues are cleaned up on Y.
    VerifyOutputQueueDepth(bs_y_, 0);
    task_util::WaitForIdle();

    // Resume update generation on Y.
    // Should not crash even though the XMPP RibOut for blue.inet.0 is gone.
    bs_y_->update_sender()->EnableProcessing();
    task_util::WaitForIdle();

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
    test::NextHop next_hop("192.168.1.1", 0, "udp");
    agent_a_->AddRoute("blue", route_a.str(), next_hop, 100);
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
    test::NextHop next_hop("192.168.1.1", 0, "udp");
    agent_a_->AddRoute("blue", route_a.str(), next_hop, 100);
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
    test::NextHop next_hop("192.168.1.1");
    vector<int> sgids = list_of
        (SecurityGroup::kMaxGlobalId - 1)(SecurityGroup::kMaxGlobalId + 1)
        (SecurityGroup::kMaxGlobalId - 2)(SecurityGroup::kMaxGlobalId + 2);
    test::RouteAttributes attributes(sgids);
    agent_a_->AddRoute("blue", route_a.str(), next_hop, attributes);
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
    test::NextHop next_hop("192.168.1.1");
    vector<int> sgids = list_of
        (SecurityGroup::kMaxGlobalId - 1)(SecurityGroup::kMaxGlobalId + 1)
        (SecurityGroup::kMaxGlobalId - 2)(SecurityGroup::kMaxGlobalId + 2);
    test::RouteAttributes attributes(sgids);
    agent_a_->AddRoute("blue", route_a.str(), next_hop, attributes);
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

TEST_F(BgpXmppInetvpn2ControlNodeTest, IpFabricVrf) {
    string ip_fabric_ri("default-domain:default-project:ip-fabric:ip-fabric");
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
    agent_a_->Subscribe(ip_fabric_ri, 1);
    agent_b_->Subscribe(ip_fabric_ri, 1);

    // Add route from agent A.
    stringstream route_a;
    route_a << "10.1.1.1/32";
    test::NextHop next_hop("192.168.1.1");
    vector<int> sgids = list_of
        (SecurityGroup::kMaxGlobalId - 1)(SecurityGroup::kMaxGlobalId + 1)
        (SecurityGroup::kMaxGlobalId - 2)(SecurityGroup::kMaxGlobalId + 2);
    test::RouteAttributes attributes(sgids);
    agent_a_->AddRoute(ip_fabric_ri, route_a.str(), next_hop, attributes);
    task_util::WaitForIdle();

    // Verify that route showed up on agents A and B with expected sgids.
    sort(sgids.begin(), sgids.end());
    VerifyRouteExists(
        agent_a_, ip_fabric_ri, route_a.str(), "192.168.1.1", sgids);
    VerifyRouteExists(
        agent_b_, ip_fabric_ri, route_a.str(), "192.168.1.1", sgids);

    // Verify that routes on agents A and B have correct origin VN.
    VerifyPath(agent_a_, ip_fabric_ri, route_a.str(), "192.168.1.1",
        "default-domain:default-project:ip-fabric");
    VerifyPath(agent_b_, ip_fabric_ri, route_a.str(), "192.168.1.1",
        "default-domain:default-project:ip-fabric");

    // Delete route from agent A.
    agent_a_->DeleteRoute(ip_fabric_ri, route_a.str());
    task_util::WaitForIdle();

    // Verify that route is deleted at agents A and B.
    VerifyRouteNoExists(agent_a_, ip_fabric_ri, route_a.str());
    VerifyRouteNoExists(agent_b_, ip_fabric_ri, route_a.str());

    // Close the sessions.
    agent_a_->SessionDown();
    agent_b_->SessionDown();
}

// Send default LoadBalance extended community
TEST_F(BgpXmppInetvpn2ControlNodeTest, LoadBalanceExtendedCommunity_1) {
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
    test::NextHop next_hop("192.168.1.1");

    // Use default LoadBalance attribute
    LoadBalance loadBalance;
    test::RouteAttributes attributes(loadBalance.ToAttribute());
    agent_a_->AddRoute("blue", route_a.str(), next_hop, attributes);
    task_util::WaitForIdle();

    // Verify that route showed up on agents A and B with expected lba.
    VerifyRouteExists(
        agent_a_, "blue", route_a.str(), "192.168.1.1", loadBalance);
    VerifyRouteExists(
        agent_b_, "blue", route_a.str(), "192.168.1.1", loadBalance);

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

// Send LoadBalance extended community with all options set
TEST_F(BgpXmppInetvpn2ControlNodeTest, LoadBalanceExtendedCommunity_2) {
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
    test::NextHop next_hop("192.168.1.1");

    // Use LoadBalance attribute with all boolean options set
    LoadBalance::bytes_type data =
        { { BgpExtendedCommunityType::Opaque,
              BgpExtendedCommunityOpaqueSubType::LoadBalance,
            0xF8, 0x00, 0x80, 0x00, 0x00, 0x00 } };
    LoadBalance loadBalance(data);
    test::RouteAttributes attributes(loadBalance.ToAttribute());
    agent_a_->AddRoute("blue", route_a.str(), next_hop, attributes);
    task_util::WaitForIdle();

    // Verify that route showed up on agents A and B with expected lba.
    VerifyRouteExists(
        agent_a_, "blue", route_a.str(), "192.168.1.1", loadBalance);
    VerifyRouteExists(
        agent_b_, "blue", route_a.str(), "192.168.1.1", loadBalance);

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

// Send LoadBalance extended community options set alternately
TEST_F(BgpXmppInetvpn2ControlNodeTest, LoadBalanceExtendedCommunity_3) {
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
    test::NextHop next_hop("192.168.1.1");

    // Use LoadBalance attribute with all boolean options reset
    // i.e, no loadBalance attribute
    LoadBalance::bytes_type data =
        { { BgpExtendedCommunityType::Opaque,
              BgpExtendedCommunityOpaqueSubType::LoadBalance,
            0xa8, 0x00, 0x80, 0x00, 0x00, 0x00 } };
    LoadBalance loadBalance(data);
    test::RouteAttributes attributes(loadBalance.ToAttribute());
    agent_a_->AddRoute("blue", route_a.str(), next_hop, attributes);
    task_util::WaitForIdle();

    // Verify that route showed up on agents A and B with expected lba.
    // Since no load-balance option was set, control-node would not send
    // LoadBalance attribute. Hence on reception, agent would infer default
    // attribute contents
    VerifyRouteExists(
        agent_a_, "blue", route_a.str(), "192.168.1.1", loadBalance);
    VerifyRouteExists(
        agent_b_, "blue", route_a.str(), "192.168.1.1", loadBalance);

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

// Send LoadBalance extended community wth all options reset
TEST_F(BgpXmppInetvpn2ControlNodeTest, LoadBalanceExtendedCommunity_4) {
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
    test::NextHop next_hop("192.168.1.1");

    // Use LoadBalance attribute with all boolean options reset.
    LoadBalance::bytes_type data =
        { { BgpExtendedCommunityType::Opaque,
              BgpExtendedCommunityOpaqueSubType::LoadBalance,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } };
    LoadBalance loadBalance(data);
    test::RouteAttributes attributes(loadBalance.ToAttribute());
    agent_a_->AddRoute("blue", route_a.str(), next_hop, attributes);
    task_util::WaitForIdle();

    // Even though the load-balance attribute sent was empty, we expect the
    // controller to default to standard 5 tuple values, the one produced by
    // the default LoadBalance() constructor.

    // Verify that route showed up on agents A and B with expected lba.
    VerifyRouteExists(
        agent_a_, "blue", route_a.str(), "192.168.1.1", LoadBalance());
    VerifyRouteExists(
        agent_b_, "blue", route_a.str(), "192.168.1.1", LoadBalance());

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

//
// Verify that each path/nexthop in an ECMP route has it's own virtual_network.
// Single control node, paths sourced in different VNs.
//
TEST_F(BgpXmppInetvpn2ControlNodeTest, EcmpOriginVn1) {
    // Configure and add connection between blue and pink on bgp server X.
    Configure(bs_x_, config_1_control_node_2_vns);
    AddConnection(bs_x_, "blue", "pink");
    task_util::WaitForIdle();

    // Create XMPP Agent A connected to XMPP server X.
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, "agent-a", xs_x_->GetPort(),
            "127.0.0.1", "127.0.0.1"));
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    // Create XMPP Agent B connected to XMPP server X.
    agent_b_.reset(
        new test::NetworkAgentMock(&evm_, "agent-b", xs_x_->GetPort(),
            "127.0.0.2", "127.0.0.1"));
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());

    // Register agent A to blue instance.
    // Register agent B to pink instance.
    agent_a_->Subscribe("blue", 1);
    agent_b_->Subscribe("pink", 2);

    // Add blue path from agent A.
    // Add pink path from agent B.
    stringstream route_a;
    route_a << "10.1.1.1/32";
    agent_a_->AddRoute("blue", route_a.str(), "192.168.1.1", 100);
    agent_b_->AddRoute("pink", route_a.str(), "192.168.1.2", 100);
    task_util::WaitForIdle();

    // Verify origin vn for all paths on agents A and B.
    VerifyPath(agent_a_, "blue", route_a.str(), "192.168.1.1", "blue");
    VerifyPath(agent_a_, "blue", route_a.str(), "192.168.1.2", "pink");
    VerifyPath(agent_b_, "pink", route_a.str(), "192.168.1.1", "blue");
    VerifyPath(agent_b_, "pink", route_a.str(), "192.168.1.2", "pink");

    // Delete blue path from agent A.
    // Delete pink path from agent B.
    agent_a_->DeleteRoute("blue", route_a.str());
    agent_b_->DeleteRoute("pink", route_a.str());
    task_util::WaitForIdle();

    // Verify that route is deleted at agents A and B.
    VerifyRouteNoExists(agent_a_, "blue", route_a.str());
    VerifyRouteNoExists(agent_b_, "pink", route_a.str());

    // Close the sessions.
    agent_a_->SessionDown();
    agent_b_->SessionDown();
}

//
// Verify that each path/nexthop in an ECMP route has it's own virtual_network.
// Single control node, paths sourced in same VN.
//
TEST_F(BgpXmppInetvpn2ControlNodeTest, EcmpOriginVn2) {
    // Configure bgp server X.
    Configure(bs_x_, config_1_control_node_2_vns);
    task_util::WaitForIdle();

    // Create XMPP Agent A connected to XMPP server X.
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, "agent-a", xs_x_->GetPort(),
            "127.0.0.1", "127.0.0.1"));
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    // Create XMPP Agent B connected to XMPP server X.
    agent_b_.reset(
        new test::NetworkAgentMock(&evm_, "agent-b", xs_x_->GetPort(),
            "127.0.0.2", "127.0.0.1"));
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());

    // Register agent A to blue instance.
    // Register agent B to blue instance.
    agent_a_->Subscribe("blue", 1);
    agent_b_->Subscribe("blue", 2);

    // Add blue path from agent A.
    // Add blue path from agent B.
    stringstream route_a;
    route_a << "10.1.1.1/32";
    agent_a_->AddRoute("blue", route_a.str(), "192.168.1.1", 100);
    agent_b_->AddRoute("blue", route_a.str(), "192.168.1.2", 100);
    task_util::WaitForIdle();

    // Verify origin vn for all paths on agents A and B.
    VerifyPath(agent_a_, "blue", route_a.str(), "192.168.1.1", "blue");
    VerifyPath(agent_a_, "blue", route_a.str(), "192.168.1.2", "blue");
    VerifyPath(agent_b_, "blue", route_a.str(), "192.168.1.1", "blue");
    VerifyPath(agent_b_, "blue", route_a.str(), "192.168.1.2", "blue");

    // Delete blue path from agent A.
    // Delete blue path from agent B.
    agent_a_->DeleteRoute("blue", route_a.str());
    agent_b_->DeleteRoute("blue", route_a.str());
    task_util::WaitForIdle();

    // Verify that route is deleted at agents A and B.
    VerifyRouteNoExists(agent_a_, "blue", route_a.str());
    VerifyRouteNoExists(agent_b_, "blue", route_a.str());

    // Close the sessions.
    agent_a_->SessionDown();
    agent_b_->SessionDown();
}

//
// Verify that each path/nexthop in an ECMP route has it's own virtual_network.
// Two control nodes, paths sourced in different VNs.
//
TEST_F(BgpXmppInetvpn2ControlNodeTest, EcmpOriginVn3) {
    // Configure and add connection between blue and pink on both bgp servers.
    Configure(config_2_control_nodes_2_vns);
    AddConnection(bs_x_, "blue", "pink");
    AddConnection(bs_y_, "blue", "pink");
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

    // Register agent A to blue instance.
    // Register agent B to pink instance.
    agent_a_->Subscribe("blue", 1);
    agent_b_->Subscribe("pink", 2);

    // Add blue path from agent A.
    // Add pink path from agent B.
    stringstream route_a;
    route_a << "10.1.1.1/32";
    agent_a_->AddRoute("blue", route_a.str(), "192.168.1.1", 100);
    agent_b_->AddRoute("pink", route_a.str(), "192.168.1.2", 100);
    task_util::WaitForIdle();

    // Verify origin vn for all paths on agents A and B.
    VerifyPath(agent_a_, "blue", route_a.str(), "192.168.1.1", "blue");
    VerifyPath(agent_a_, "blue", route_a.str(), "192.168.1.2", "pink");
    VerifyPath(agent_b_, "pink", route_a.str(), "192.168.1.1", "blue");
    VerifyPath(agent_b_, "pink", route_a.str(), "192.168.1.2", "pink");

    // Delete blue path from agent A.
    // Delete pink path from agent B.
    agent_a_->DeleteRoute("blue", route_a.str());
    agent_b_->DeleteRoute("pink", route_a.str());
    task_util::WaitForIdle();

    // Verify that route is deleted at agents A and B.
    VerifyRouteNoExists(agent_a_, "blue", route_a.str());
    VerifyRouteNoExists(agent_b_, "pink", route_a.str());

    // Close the sessions.
    agent_a_->SessionDown();
    agent_b_->SessionDown();
}

//
// Verify that each path/nexthop in an ECMP route has it's own virtual_network.
// Two control nodes, paths sourced in same VN.
//
TEST_F(BgpXmppInetvpn2ControlNodeTest, EcmpOriginVn4) {
    // Configure both bgp servers.
    Configure(config_2_control_nodes_2_vns);
    task_util::WaitForIdle();

    // Create XMPP Agent A connected to XMPP server X.
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, "agent-a", xs_x_->GetPort(),
            "127.0.0.1", "127.0.0.1"));
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    // Create XMPP Agent B connected to XMPP server X.
    agent_b_.reset(
        new test::NetworkAgentMock(&evm_, "agent-b", xs_y_->GetPort(),
            "127.0.0.2", "127.0.0.2"));
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());

    // Register agent A to blue instance.
    // Register agent B to blue instance.
    agent_a_->Subscribe("blue", 1);
    agent_b_->Subscribe("blue", 2);

    // Add blue path from agent A.
    // Add blue path from agent B.
    stringstream route_a;
    route_a << "10.1.1.1/32";
    agent_a_->AddRoute("blue", route_a.str(), "192.168.1.1", 100);
    agent_b_->AddRoute("blue", route_a.str(), "192.168.1.2", 100);
    task_util::WaitForIdle();

    // Verify origin vn for all paths on agents A and B.
    VerifyPath(agent_a_, "blue", route_a.str(), "192.168.1.1", "blue");
    VerifyPath(agent_a_, "blue", route_a.str(), "192.168.1.2", "blue");
    VerifyPath(agent_b_, "blue", route_a.str(), "192.168.1.1", "blue");
    VerifyPath(agent_b_, "blue", route_a.str(), "192.168.1.2", "blue");

    // Delete blue path from agent A.
    // Delete blue path from agent B.
    agent_a_->DeleteRoute("blue", route_a.str());
    agent_b_->DeleteRoute("blue", route_a.str());
    task_util::WaitForIdle();

    // Verify that route is deleted at agents A and B.
    VerifyRouteNoExists(agent_a_, "blue", route_a.str());
    VerifyRouteNoExists(agent_b_, "blue", route_a.str());

    // Close the sessions.
    agent_a_->SessionDown();
    agent_b_->SessionDown();
}

//
// Verify that with route aggregation enabled, contributing route is not
// published to xmpp agent
//
TEST_F(BgpXmppInetvpn2ControlNodeTest, RouteAggregation_NoExportContributing) {
    // Configure bgp server X.
    Configure(bs_x_, config_2_control_nodes_route_aggregate);
    task_util::WaitForIdle();

    // Create XMPP Agent A connected to XMPP server X.
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, "agent-a", xs_x_->GetPort(),
            "127.0.0.1", "127.0.0.1"));
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    // Create XMPP Agent B connected to XMPP server X.
    agent_b_.reset(
        new test::NetworkAgentMock(&evm_, "agent-b", xs_x_->GetPort(),
            "127.0.0.2", "127.0.0.1"));
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());

    // Register agent A to blue instance.
    // Register agent B to blue instance.
    agent_a_->Subscribe("blue", 1);
    agent_b_->Subscribe("blue", 2);

    // Add blue path from agent A.
    stringstream route_contributing;
    route_contributing << "2.2.2.1/32";
    agent_a_->AddRoute("blue", route_contributing.str(), "192.168.1.1", 100);
    stringstream route_nexthop;
    route_nexthop << "2.2.1.1/32";
    agent_a_->AddRoute("blue", route_nexthop.str(), "192.168.1.1", 100);
    task_util::WaitForIdle();

    stringstream route_aggregate;
    route_aggregate << "2.2.0.0/16";
    VerifyRouteExists(agent_a_, "blue", route_aggregate.str(), "192.168.1.1");
    VerifyRouteExists(agent_b_, "blue", route_aggregate.str(), "192.168.1.1");

    // Contributing route is not published to agents
    VerifyRouteNoExists(agent_b_, "blue", route_contributing.str());
    // Nexthop route is published to agents
    VerifyRouteExists(agent_b_, "blue", route_nexthop.str(), "192.168.1.1");

    // Delete blue path from agent A.
    agent_a_->DeleteRoute("blue", route_contributing.str());
    agent_a_->DeleteRoute("blue", route_nexthop.str());
    task_util::WaitForIdle();

    // Verify that route is deleted at agents A and B.
    VerifyRouteNoExists(agent_a_, "blue", route_aggregate.str());
    VerifyRouteNoExists(agent_b_, "blue", route_aggregate.str());

    // Close the sessions.
    agent_a_->SessionDown();
    agent_b_->SessionDown();
}

//
// Verify that with route aggregation enabled, contributing route is not
// published to bgp peers
//
TEST_F(BgpXmppInetvpn2ControlNodeTest, RouteAggregation_NoExportContributing_1) {
    // Configure bgp server X & Y.
    Configure(config_2_control_nodes_route_aggregate);
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

    // Register agent A to blue instance.
    // Register agent B to blue instance.
    agent_a_->Subscribe("blue", 1);
    agent_b_->Subscribe("blue", 2);

    // Add blue path from agent A.
    stringstream route_contributing;
    route_contributing << "2.2.2.1/32";
    agent_a_->AddRoute("blue", route_contributing.str(), "192.168.1.1", 100);
    stringstream route_nexthop;
    route_nexthop << "2.2.1.1/32";
    agent_a_->AddRoute("blue", route_nexthop.str(), "192.168.1.1", 100);
    task_util::WaitForIdle();

    stringstream route_aggregate;
    route_aggregate << "2.2.0.0/16";
    VerifyRouteExists(agent_a_, "blue", route_aggregate.str(), "192.168.1.1");
    VerifyRouteExists(agent_b_, "blue", route_aggregate.str(), "192.168.1.1");

    // Contributing route is not published to agents
    VerifyRouteNoExists(agent_b_, "blue", route_contributing.str());
    // Nexthop route is published to agents
    VerifyRouteExists(agent_b_, "blue", route_nexthop.str(), "192.168.1.1");

    // Delete blue path from agent A.
    agent_a_->DeleteRoute("blue", route_contributing.str());
    agent_a_->DeleteRoute("blue", route_nexthop.str());
    task_util::WaitForIdle();

    // Verify that route is deleted at agents A and B.
    VerifyRouteNoExists(agent_a_, "blue", route_aggregate.str());
    VerifyRouteNoExists(agent_b_, "blue", route_aggregate.str());

    // Close the sessions.
    agent_a_->SessionDown();
    agent_b_->SessionDown();
}

//
// Verify that contributing route is published when route-aggregation config is
// removed from the routing instance
//
TEST_F(BgpXmppInetvpn2ControlNodeTest, RouteAggregation_NoExportContributing_2) {
    // Configure bgp server X & Y.
    Configure(config_2_control_nodes_route_aggregate);
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

    // Register agent A to blue instance.
    // Register agent B to blue instance.
    agent_a_->Subscribe("blue", 1);
    agent_b_->Subscribe("blue", 2);

    // Add blue path from agent A.
    stringstream route_contributing;
    route_contributing << "2.2.2.1/32";
    agent_a_->AddRoute("blue", route_contributing.str(), "192.168.1.1", 100);
    stringstream route_nexthop;
    route_nexthop << "2.2.1.1/32";
    agent_a_->AddRoute("blue", route_nexthop.str(), "192.168.1.1", 100);
    task_util::WaitForIdle();

    stringstream route_aggregate;
    route_aggregate << "2.2.0.0/16";
    VerifyRouteExists(agent_a_, "blue", route_aggregate.str(), "192.168.1.1");
    VerifyRouteExists(agent_b_, "blue", route_aggregate.str(), "192.168.1.1");

    // Contributing route is not published to agents
    VerifyRouteNoExists(agent_b_, "blue", route_contributing.str());
    // Nexthop route is published to agents
    VerifyRouteExists(agent_b_, "blue", route_nexthop.str(), "192.168.1.1");


    ifmap_test_util::IFMapMsgUnlink(bs_x_->config_db(), "routing-instance",
    "blue", "route-aggregate", "vn_subnet", "route-aggregate-routing-instance");
    ifmap_test_util::IFMapMsgUnlink(bs_y_->config_db(), "routing-instance",
    "blue", "route-aggregate", "vn_subnet", "route-aggregate-routing-instance");
    task_util::WaitForIdle();

    // Contributing route is published to agents
    VerifyRouteExists(agent_b_, "blue", route_contributing.str(), "192.168.1.1");
    VerifyRouteNoExists(agent_b_, "blue", route_aggregate.str());
    VerifyRouteNoExists(agent_a_, "blue", route_aggregate.str());

    // Delete blue path from agent A.
    agent_a_->DeleteRoute("blue", route_contributing.str());
    agent_a_->DeleteRoute("blue", route_nexthop.str());
    task_util::WaitForIdle();

    // Verify that route is deleted at agents A and B.
    VerifyRouteNoExists(agent_a_, "blue", route_aggregate.str());
    VerifyRouteNoExists(agent_b_, "blue", route_aggregate.str());

    // Close the sessions.
    agent_a_->SessionDown();
    agent_b_->SessionDown();
}

//
// Verify local-pref update by routing policy
// Consider a route with community c1 and c2 and routing instance with policy p1
// Policy P1: { from community c1; then local-pref 9999 }
// Due to the routing policy the route has local-pref set 9999. The route in
// bgp.l3vpn.0 table has two paths (one replicated and other Bgp path) with
// local-pref set to 9999.
// Now attach routing instance to policy P2
// Policy P2: { from community c2; then local-pref 8888 }
// Since the route has community c1 and c2, it matches both policy and end
// result is path has local-pref set to 8888.
// When the new route is replicated to bgp-.l3vpn.0 table, new replicated route
// will have lower local-pref(8888) compared to bgp-path from other
// control-node(9999).
// Test is to verify that after the policy update on routing instance, the route
// has local-pref as per the policy action update(i.e. in this example 8888)
//
TEST_F(BgpXmppInetvpn2ControlNodeTest, RoutingPolicy_UpdateLocalPref) {
    // Configure bgp server X.
    Configure(config_2_control_nodes_routing_policy);
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

    // Register agent A to blue instance.
    // Register agent B to blue instance.
    agent_a_->Subscribe("blue", 1);
    agent_b_->Subscribe("blue", 1);

    vector<std::string> community_a;
    stringstream route_a;
    route_a << "10.1.1.1/32";
    community_a.push_back("11:13");
    community_a.push_back("22:13");
    test::RouteAttributes attr_a(community_a);
    test::NextHop nexthop_a("192.168.1.1");
    agent_a_->AddRoute("blue", route_a.str(), nexthop_a, attr_a);
    agent_b_->AddRoute("blue", route_a.str(), nexthop_a, attr_a);
    task_util::WaitForIdle();

    VerifyRouteExists(agent_a_, "blue", route_a.str(), "192.168.1.1", 9999);
    VerifyRouteExists(agent_b_, "blue", route_a.str(), "192.168.1.1", 9999);


    auto_ptr<autogen::RoutingPolicyType> p_a(new autogen::RoutingPolicyType());
    p_a->sequence = "2.0";
    ifmap_test_util::IFMapMsgLink(bs_x_->config_db(), "routing-instance",
                      "blue", "routing-policy", "p2",
                      "routing-policy-routing-instance", 0, p_a.release());
    task_util::WaitForIdle();

    auto_ptr<autogen::RoutingPolicyType> p_b(new autogen::RoutingPolicyType());
    p_b->sequence = "2.0";
    ifmap_test_util::IFMapMsgLink(bs_y_->config_db(), "routing-instance",
                          "blue", "routing-policy", "p2",
                          "routing-policy-routing-instance", 0, p_b.release());
    task_util::WaitForIdle();


    VerifyRouteExists(agent_a_, "blue", route_a.str(), "192.168.1.1", 8888);
    VerifyRouteExists(agent_b_, "blue", route_a.str(), "192.168.1.1", 8888);

    // Delete blue path from agent A.
    agent_a_->DeleteRoute("blue", route_a.str());
    agent_b_->DeleteRoute("blue", route_a.str());
    task_util::WaitForIdle();

    // Close the sessions.
    agent_a_->SessionDown();
    agent_b_->SessionDown();
}

class BgpXmppInetvpnJoinLeaveTest :
    public BgpXmppInetvpn2ControlNodeTest,
    public ::testing::WithParamInterface<bool> {

protected:
    BgpXmppInetvpnJoinLeaveTest() : unique_prefs_(false) {
    }

    virtual void SetUp() {
        unique_prefs_ = GetParam();
        BgpXmppInetvpn2ControlNodeTest::SetUp();
        Configure();
        task_util::WaitForIdle();
        agent_a_.reset(
            new test::NetworkAgentMock(&evm_, "agent-a", xs_x_->GetPort(),
                "127.0.0.1", "127.0.0.1"));
        TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());
        agent_b_.reset(
            new test::NetworkAgentMock(&evm_, "agent-b", xs_y_->GetPort(),
                "127.0.0.2", "127.0.0.2"));
        TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());
    }

    virtual void TearDown() {
        agent_a_->SessionDown();
        agent_b_->SessionDown();
        BgpXmppInetvpn2ControlNodeTest::TearDown();
    }

    uint32_t GetLocalPreference(uint32_t idx) {
        return (unique_prefs_ ? (100 + idx) : 100);
    }

private:
    bool unique_prefs_;
};

//
// Multiple routes are advertised/withdrawn properly on Join/Leave.
//
TEST_P(BgpXmppInetvpnJoinLeaveTest, JoinLeave1) {
    // Register to blue instance
    agent_a_->Subscribe("blue", 1);

    // Add routes from agent A.
    for (int idx = 0; idx < kRouteCount; ++idx) {
        agent_a_->AddRoute(
            "blue", BuildPrefix(idx), "192.168.1.1", GetLocalPreference(idx));
    }

    // Verify that routes showed up on agent A.
    for (int idx = 0; idx < kRouteCount; ++idx) {
        VerifyRouteExists(agent_a_, "blue",
            BuildPrefix(idx), "192.168.1.1", GetLocalPreference(idx));
    }

    // Register to blue instance
    agent_b_->Subscribe("blue", 1);
    task_util::WaitForIdle();

    // Verify that routes showed up on agent B.
    for (int idx = 0; idx < kRouteCount; ++idx) {
        VerifyRouteExists(agent_b_, "blue",
            BuildPrefix(idx), "192.168.1.1", GetLocalPreference(idx));
    }

    // Unregister to blue instance
    agent_b_->Unsubscribe("blue");
    task_util::WaitForIdle();

    // Verify that routes are deleted at agent B.
    for (int idx = 0; idx < kRouteCount; ++idx) {
        VerifyRouteNoExists(agent_b_, "blue", BuildPrefix(idx));
    }

    // Delete routes from agent A.
    for (int idx = 0; idx < kRouteCount; ++idx) {
        agent_a_->DeleteRoute("blue", BuildPrefix(idx));
    }

    // Verify that routes are deleted at agent A.
    for (int idx = 0; idx < kRouteCount; ++idx) {
        VerifyRouteNoExists(agent_a_, "blue", BuildPrefix(idx));
    }

    // Unregister to blue instance
    agent_a_->Unsubscribe("blue");
}

//
// Multiple routes are advertised/withdrawn properly on Join/Leave.
// New test agents join/leave the instance after the bgp server has routes.
//
TEST_P(BgpXmppInetvpnJoinLeaveTest, JoinLeave2) {
    // Register to blue instance
    agent_a_->Subscribe("blue", 1);
    agent_b_->Subscribe("blue", 1);
    task_util::WaitForIdle();

    // Add routes from agent A.
    for (int idx = 0; idx < kRouteCount; ++idx) {
        agent_a_->AddRoute(
            "blue", BuildPrefix(idx), "192.168.1.1", GetLocalPreference(idx));
    }

    // Verify that routes showed up on agents A and B.
    for (int idx = 0; idx < kRouteCount; ++idx) {
        VerifyRouteExists(agent_a_, "blue",
            BuildPrefix(idx), "192.168.1.1", GetLocalPreference(idx));
        VerifyRouteExists(agent_b_, "blue",
            BuildPrefix(idx), "192.168.1.1", GetLocalPreference(idx));
    }

    // Create a bunch of test agents.
    vector<test::NetworkAgentMockPtr> agent_list;
    for (int idx = 0; idx <= 8; ++idx) {
        string name = string("agent") + integerToString(idx);
        test::NetworkAgentMockPtr agent;
        agent.reset(new test::NetworkAgentMock(
            &evm_, name, xs_y_->GetPort(), "127.0.0.2", "127.0.0.2"));
        agent_list.push_back(agent);
        TASK_UTIL_EXPECT_TRUE(agent->IsEstablished());
    }

    // Register all test agents to blue instance.
    BOOST_FOREACH(test::NetworkAgentMockPtr agent, agent_list) {
        agent->Subscribe("blue", 1);
    }

    // Verify that routes showed up on all test agents.
    for (int idx = 0; idx < kRouteCount; ++idx) {
        BOOST_FOREACH(test::NetworkAgentMockPtr agent, agent_list) {
            VerifyRouteExists(agent, "blue",
                BuildPrefix(idx), "192.168.1.1", GetLocalPreference(idx));
        }
    }

    // Unregister all test agents to blue instance.
    BOOST_FOREACH(test::NetworkAgentMockPtr agent, agent_list) {
        agent->Unsubscribe("blue");
    }
    task_util::WaitForIdle();

    // Delete all test agents.
    BOOST_FOREACH(test::NetworkAgentMockPtr agent, agent_list) {
        agent->Delete();
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
}

//
// Multiple routes are advertised/withdrawn properly on Join/Leave.
// New test agents join/leave the instance while the bgp server is receiving
// updates/withdraws.
//
TEST_P(BgpXmppInetvpnJoinLeaveTest, JoinLeave3) {
    // Register to blue instance
    agent_a_->Subscribe("blue", 1);
    agent_b_->Subscribe("blue", 1);
    task_util::WaitForIdle();

    // Create a bunch of test agents.
    vector<test::NetworkAgentMockPtr> agent_list;
    for (int idx = 0; idx <= 8; ++idx) {
        string name = string("agent") + integerToString(idx);
        test::NetworkAgentMockPtr agent;
        agent.reset(new test::NetworkAgentMock(
            &evm_, name, xs_y_->GetPort(), "127.0.0.2", "127.0.0.2"));
        agent_list.push_back(agent);
        TASK_UTIL_EXPECT_TRUE(agent->IsEstablished());
    }

    // Add routes from agent A.
    for (int idx = 0; idx < kRouteCount; ++idx) {
        agent_a_->AddRoute(
            "blue", BuildPrefix(idx), "192.168.1.1", GetLocalPreference(idx));
    }

    // Register all test agents to blue instance.
    BOOST_FOREACH(test::NetworkAgentMockPtr agent, agent_list) {
        agent->Subscribe("blue", 1);
    }

    // Verify that routes showed up on all test agents.
    for (int idx = 0; idx < kRouteCount; ++idx) {
        BOOST_FOREACH(test::NetworkAgentMockPtr agent, agent_list) {
            VerifyRouteExists(agent, "blue",
                BuildPrefix(idx), "192.168.1.1", GetLocalPreference(idx));
        }
    }

    // Verify that routes showed up on agents A and B.
    for (int idx = 0; idx < kRouteCount; ++idx) {
        VerifyRouteExists(agent_a_, "blue",
            BuildPrefix(idx), "192.168.1.1", GetLocalPreference(idx));
        VerifyRouteExists(agent_b_, "blue",
            BuildPrefix(idx), "192.168.1.1", GetLocalPreference(idx));
    }

    // Unregister all test agents to blue instance.
    BOOST_FOREACH(test::NetworkAgentMockPtr agent, agent_list) {
        agent->Unsubscribe("blue");
    }

    // Delete routes from agent A.
    for (int idx = 0; idx < kRouteCount; ++idx) {
        agent_a_->DeleteRoute("blue", BuildPrefix(idx));
    }

    // Delete all test agents.
    BOOST_FOREACH(test::NetworkAgentMockPtr agent, agent_list) {
        agent->Delete();
    }

    // Verify that routes are deleted at agents A and B.
    for (int idx = 0; idx < kRouteCount; ++idx) {
        VerifyRouteNoExists(agent_a_, "blue", BuildPrefix(idx));
        VerifyRouteNoExists(agent_b_, "blue", BuildPrefix(idx));
    }
}

//
// Multiple routes are advertised/withdrawn properly on Join/Leave.
// New test agents simultaneously join/leave the instance after the bgp server
// has routes.
//
TEST_P(BgpXmppInetvpnJoinLeaveTest, JoinLeave4) {
    // Register to blue instance
    agent_a_->Subscribe("blue", 1);
    agent_b_->Subscribe("blue", 1);
    task_util::WaitForIdle();

    // Add routes from agent A.
    for (int idx = 0; idx < kRouteCount; ++idx) {
        agent_a_->AddRoute(
            "blue", BuildPrefix(idx), "192.168.1.1", GetLocalPreference(idx));
    }
    task_util::WaitForIdle();

    // Verify that routes showed up on agents A and B.
    for (int idx = 0; idx < kRouteCount; ++idx) {
        VerifyRouteExists(agent_a_, "blue",
            BuildPrefix(idx), "192.168.1.1", GetLocalPreference(idx));
        VerifyRouteExists(agent_b_, "blue",
            BuildPrefix(idx), "192.168.1.1", GetLocalPreference(idx));
    }

    // Create a bunch of test agents.
    vector<test::NetworkAgentMockPtr> agent_list;
    for (int idx = 0; idx <= 8; ++idx) {
        string name = string("agent") + integerToString(idx);
        test::NetworkAgentMockPtr agent;
        agent.reset(new test::NetworkAgentMock(
            &evm_, name, xs_y_->GetPort(), "127.0.0.2", "127.0.0.2"));
        agent_list.push_back(agent);
        TASK_UTIL_EXPECT_TRUE(agent->IsEstablished());
    }

    // Register odd test agents to blue instance.
    int agent_idx = 0;
    BOOST_FOREACH(test::NetworkAgentMockPtr agent, agent_list) {
        if (agent_idx++ % 2 != 0) {
            agent->Subscribe("blue", 1);
        }
    }
    task_util::WaitForIdle();

    // Verify that routes showed up on odd test agents.
    for (int idx = 0; idx < kRouteCount; ++idx) {
        agent_idx = 0;
        BOOST_FOREACH(test::NetworkAgentMockPtr agent, agent_list) {
            if (agent_idx++ % 2 == 0) {
                VerifyRouteNoExists(agent, "blue", BuildPrefix(idx));
            } else {
                VerifyRouteExists(agent, "blue",
                    BuildPrefix(idx), "192.168.1.1", GetLocalPreference(idx));
            }
        }
    }

    // Register even and unregister odd test agents to blue instance.
    agent_idx = 0;
    BOOST_FOREACH(test::NetworkAgentMockPtr agent, agent_list) {
        if (agent_idx++ % 2 == 0) {
            agent->Subscribe("blue", 1);
        } else {
            agent->Unsubscribe("blue");
        }
    }
    task_util::WaitForIdle();

    // Verify that routes showed up on even test agents.
    for (int idx = 0; idx < kRouteCount; ++idx) {
        agent_idx = 0;
        BOOST_FOREACH(test::NetworkAgentMockPtr agent, agent_list) {
            if (agent_idx++ % 2 == 0) {
                VerifyRouteExists(agent, "blue",
                    BuildPrefix(idx), "192.168.1.1", GetLocalPreference(idx));
            } else {
                VerifyRouteNoExists(agent, "blue", BuildPrefix(idx));
            }
        }
    }

    // Unregister even test agents to blue instance.
    BOOST_FOREACH(test::NetworkAgentMockPtr agent, agent_list) {
        agent_idx = 0;
        if (agent_idx++ % 2 == 0) {
            agent->Unsubscribe("blue");
        }
    }
    task_util::WaitForIdle();

    // Delete all test agents.
    BOOST_FOREACH(test::NetworkAgentMockPtr agent, agent_list) {
        agent->Delete();
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
}

INSTANTIATE_TEST_CASE_P(Test, BgpXmppInetvpnJoinLeaveTest, ::testing::Bool());

class TestEnvironment : public ::testing::Environment {
    virtual ~TestEnvironment() { }
    virtual void SetUp() {
    }
    virtual void TearDown() {
    }
};

static void SetUp() {
    BgpServer::Initialize();
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
    BgpServer::Terminate();
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    scheduler->Terminate();
}

int main(int argc, char **argv) {
    bgp_log_test::init();
    ::testing::InitGoogleTest(&argc, argv);
    ::testing::AddGlobalTestEnvironment(new TestEnvironment());
    DB::SetPartitionCount(1);
    SetUp();
    int result = RUN_ALL_TESTS();
    TearDown();

    return result;
}
