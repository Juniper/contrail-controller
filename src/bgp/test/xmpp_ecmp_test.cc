/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/foreach.hpp>
#include <boost/assign/list_of.hpp>
#include <boost/assign/std/vector.hpp>
#include <fstream>

#include "base/logging.h"
#include "base/util.h"
#include "base/test/task_test_util.h"
#include "bgp/test/bgp_server_test_util.h"
#include "bgp/routing-instance/routing_instance.h"
#include "control-node/control_node.h"
#include "control-node/test/control_node_test.h"
#include "control-node/test/network_agent_mock.h"
#include "ifmap/ifmap_server.h"
#include "ifmap/ifmap_server_parser.h"
#include "ifmap/test/ifmap_test_util.h"
#include "io/event_manager.h"
#include "schema/xmpp_unicast_types.h"
#include "schema/xmpp_enet_types.h"
#include "schema/bgp_schema_types.h"
#include "schema/vnc_cfg_types.h"
#include "testing/gunit.h"

using namespace std;
using namespace test;
using boost::assign::list_of;

// 2 control-nodes and 4 agents are used in this test. control-nodes exchange
// routes over BGP. 2 mock agents form xmpp session to each of the control-node
// and send routes with multiple next-hops. Those routes (and next-hops) are
// advertised back to local agents as well as remote agents (through other
// control-node).
class XmppEcmpTest : public ::testing::Test {
protected:
    static const int kTimeoutSeconds = 15;
    static const char *config_tmpl;
    string net_1_;
    string net_2_;
    bool inet_;
    bool enet_;

    XmppEcmpTest()
        : node_a_(new test::ControlNodeTest(&evm_, "A")),
          node_b_(new test::ControlNodeTest(&evm_, "B")) {
        // net_1_= "default-domain:b47d0eacc9c446eabc9b4eea3d6f6133:vn1:vn1";
        // net_2_ = "default-domain:b47d0eacc9c446eabc9b4eea3d6f6133:vn2:vn2";
        net_1_= "red";
        net_2_ = "blue";
        inet_ = false;
        enet_ = false;
    }

    virtual void SetUp() {
        char config[4096];
        snprintf(config, sizeof(config), config_tmpl,
                 node_a_->bgp_port(), node_b_->bgp_port());
        node_a_->BgpConfig(config);
        node_b_->BgpConfig(config);
        Initialize();
    }
    virtual void TearDown() {
        agent_a_->Delete();
        agent_b_->Delete();
        agent_c_->Delete();
        agent_d_->Delete();
    }

    bool SessionsEstablished() {
        return ((node_a_->BgpEstablishedCount() == 1) &&
                (node_b_->BgpEstablishedCount() == 1) &&
                agent_a_->IsEstablished() &&
                agent_b_->IsEstablished() &&
                agent_c_->IsEstablished() &&
                agent_d_->IsEstablished());
    }

    bool AgentRouteCount(test::NetworkAgentMock *agent, int count) {
        return agent->RouteCount() == count;
    }
    bool AgentEnetRouteCount(test::NetworkAgentMock *agent, int count) {
        return agent->EnetRouteCount() == count;
    }
    void WaitForEstablished() {
        task_util::WaitForCondition(
            &evm_,
            boost::bind(&XmppEcmpTest::SessionsEstablished, this),
            kTimeoutSeconds);
        ASSERT_TRUE(SessionsEstablished())
            << "bgp sessions: (" << node_a_->BgpEstablishedCount()
            << ", " << node_b_->BgpEstablishedCount()
            << ", agent A: "
            << (agent_a_->IsEstablished() ? "Established" : "Down")
            << ", agent B: "
            << (agent_b_->IsEstablished() ? "Established" : "Down")
            << ", agent C: "
            << (agent_c_->IsEstablished() ? "Established" : "Down")
            << ", agent D: "
            << (agent_d_->IsEstablished() ? "Established" : "Down");
    }

    NextHops AddNextHops2(NextHops n1, NextHops n2) {
        NextHops sum = n1;
        BOOST_FOREACH(NextHop n, n2) {
            if (std::find(sum.begin(), sum.end(), n) == sum.end()) {
                sum.push_back(n);
            }
        }
        return sum;
    }

    NextHops AddNextHops3(NextHops &n1, NextHops &n2, NextHops &n3) {
        return AddNextHops2(n1, AddNextHops2(n2, n3));
    }

    NextHops AddNextHops4(NextHops &n1, NextHops &n2, NextHops &n3,
                          NextHops &n4) {
        return AddNextHops2(AddNextHops2(n1, n2), AddNextHops2(n3, n4));
    }

    bool IsAgentUp(test::NetworkAgentMock *agent) {
        return agent->IsSessionEstablished();
    }
    bool IsAgentDown(test::NetworkAgentMock *agent) {
        return !agent->IsSessionEstablished();
    }

    void WaitForInetRouteCount(test::NetworkAgentMock *agent, int count) {
        SCOPED_TRACE("WaitForInetRouteCount");
        if (inet_) {
            task_util::WaitForCondition(
                &evm_,
                boost::bind(&XmppEcmpTest::AgentRouteCount, this, agent, count),
                kTimeoutSeconds);
        }
    }

    void WaitForEnetRouteCount(test::NetworkAgentMock *agent, int count) {
        SCOPED_TRACE("WaitForEnetRouteCount");
        if (enet_) {
            task_util::WaitForCondition(
                &evm_,
                boost::bind(&XmppEcmpTest::AgentEnetRouteCount, this, agent,
                            count),
                kTimeoutSeconds);
        }
    }
    void WaitForInetRouteCount(size_t count);
    void WaitForEnetRouteCount(size_t count,
                               std::vector<test::NetworkAgentMock *> agents);
    void Initialize();
    void DeleteRoutesAndVerify(bool flap);
    void AddRoutesAndVerify();
    void UpdateRoutesAndVerify();
    void VerifyRoute(string prefix, NextHops nexthops);
    void VerifyEnetRoute(std::string prefix, string net,
                         vector<test::NetworkAgentMock *> agents,
                         NextHops nexthops);
    bool CheckRoute(test::NetworkAgentMock *agent, string net, string prefix,
                    NextHops &nexthops);
    bool CheckEnetRoute(test::NetworkAgentMock *agent, string net,
                        std::string prefix, NextHops &nexthops);
    void VerifyNoRoute(string prefix);
    void VerifyNoEnetRoute(string enet_prefix, string net,
                           vector<test::NetworkAgentMock *> agents);
    bool CheckNoRoute(test::NetworkAgentMock *agent, std::string net,
                      std::string prefix);
    bool CheckNoEnetRoute(test::NetworkAgentMock *agent, std::string net,
                          std::string prefix);
    void InitializeNextHops();
    void Subscribe();
    bool CheckNextHop(const autogen::NextHopType &nexthop, NextHops &nexthops);

    EventManager evm_;
    boost::scoped_ptr<test::ControlNodeTest> node_a_;
    boost::scoped_ptr<test::ControlNodeTest> node_b_;

    boost::scoped_ptr<test::NetworkAgentMock> agent_a_;
    boost::scoped_ptr<test::NetworkAgentMock> agent_b_;
    boost::scoped_ptr<test::NetworkAgentMock> agent_c_;
    boost::scoped_ptr<test::NetworkAgentMock> agent_d_;

    NextHops nexthops_a, nexthops_b, nexthops_c, nexthops_d;
    NextHops start_nexthops;
};

const char *XmppEcmpTest::config_tmpl = "\
<config>\
    <bgp-router name=\'A\'>\
        <identifier>192.168.0.1</identifier>\
        <address>127.0.0.1</address>\
        <port>%d</port>\
        <autonomous-system>64512</autonomous-system>\
        <session to=\'B\'>\
            <address-families>\
                <family>e-vpn</family>\
                <family>route-target</family>\
                <family>inet-vpn</family>\
            </address-families>\
        </session>\
    </bgp-router>\
    <bgp-router name=\'B\'>\
        <identifier>192.168.0.2</identifier>\
        <address>127.0.0.1</address>\
        <port>%d</port>\
        <autonomous-system>64512</autonomous-system>\
        <session to=\'A\'>\
            <address-families>\
                <family>e-vpn</family>\
                <family>route-target</family>\
                <family>inet-vpn</family>\
            </address-families>\
        </session>\
    </bgp-router>\
    <virtual-network name='red-vn'>\
        <network-id>101</network-id>\
    </virtual-network>\
    <virtual-network name='blue-vn'>\
        <network-id>102</network-id>\
    </virtual-network>\
    <routing-instance name='red'>\
        <virtual-network>red-vn</virtual-network>\
        <vrf-target>target:1:1</vrf-target>\
        <vrf-target>\
            target:1:2\
            <import-export>import</import-export>\
        </vrf-target>\
    </routing-instance>\
    <routing-instance name='blue'>\
        <virtual-network>blue-vn</virtual-network>\
        <vrf-target>target:1:2</vrf-target>\
        <vrf-target>\
            target:1:1\
            <import-export>import</import-export>\
        </vrf-target>\
    </routing-instance>\
</config>\
";


// Initialize mock agents and from xmpp sessions with the control-node.
void XmppEcmpTest::Initialize() {
    const char *ri_1 = "red";
    const char *ri_2 = "blue";

    node_a_->VerifyRoutingInstance(ri_1, false);
    node_a_->VerifyRoutingInstance(ri_2, false);
    node_b_->VerifyRoutingInstance(ri_1, false);
    node_b_->VerifyRoutingInstance(ri_2, false);

    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, "agent-a",
                                   node_a_->xmpp_port(), "127.0.0.1"));
    agent_b_.reset(
        new test::NetworkAgentMock(&evm_, "agent-b",
                                   node_a_->xmpp_port(), "127.0.0.2"));
    agent_c_.reset(
        new test::NetworkAgentMock(&evm_, "agent-c",
                                   node_b_->xmpp_port(), "127.0.0.1"));
    agent_d_.reset(
        new test::NetworkAgentMock(&evm_, "agent-d",
                                   node_b_->xmpp_port(), "127.0.0.2"));
    WaitForEstablished();

    start_nexthops.clear();
    start_nexthops.push_back(NextHop("1.1.1.1", 1, "all"));
    start_nexthops.push_back(NextHop("1.1.1.2", 2, "all"));
    start_nexthops.push_back(NextHop("1.1.1.3", 3, "all"));
    start_nexthops.push_back(NextHop("1.1.1.4", 4, "all"));
    start_nexthops.push_back(NextHop("1.1.1.5", 5, "all"));
}

void XmppEcmpTest::Subscribe() {
    if (inet_) {
        agent_a_->Subscribe(net_1_, 1);
        agent_b_->Subscribe(net_2_, 2);
        agent_c_->Subscribe(net_1_, 1);
        agent_d_->Subscribe(net_2_, 2);
    }

    if (enet_) {
        agent_a_->EnetSubscribe(net_1_, 1);
        agent_b_->EnetSubscribe(net_2_, 2);
        agent_c_->EnetSubscribe(net_1_, 1);
        agent_d_->EnetSubscribe(net_2_, 2);
    }
}

bool XmppEcmpTest::CheckNextHop(const autogen::NextHopType &nexthop,
                                NextHops &nexthops) {
    for (NextHops::iterator i = nexthops.begin(); i < nexthops.end(); i++) {
        NextHop n = *i;

        if (nexthop.address != n.address_) continue;
        if (nexthop.label != n.label_) continue;
        if (nexthop.tunnel_encapsulation_list.tunnel_encapsulation.size() !=
                n.tunnel_encapsulations_.size()) continue;
        autogen::TunnelEncapsulationListType::const_iterator j;
        bool found = true;
        for (j = nexthop.tunnel_encapsulation_list.begin();
                j != nexthop.tunnel_encapsulation_list.end(); j++) {
            if (std::find(n.tunnel_encapsulations_.begin(),
                          n.tunnel_encapsulations_.end(), *j) ==
                    n.tunnel_encapsulations_.end()) {
                found = false;
                break;
            }
        }

        if (found) return true;
    }

    return false;
}

// Verify that routes are properly received by the agents. Nexthop address,
// label and tunnel encapsulation information received are also verified.
bool XmppEcmpTest::CheckRoute(test::NetworkAgentMock *agent, string net,
                                string prefix, NextHops &nexthops) {
    const NetworkAgentMock::RouteEntry *rt = agent->RouteLookup(net, prefix);

    if (rt == NULL) return false;

    if (nexthops.size() != rt->entry.next_hops.next_hop.size()) return false;

    // Next-hops can be received in any order. Hence look for each received
    // next-hop in the list of expected nexthops.
    for (size_t i = 0; i < rt->entry.next_hops.next_hop.size(); i++) {
        if (!CheckNextHop(rt->entry.next_hops.next_hop[i], nexthops)) {
            return false;
        }
    }

    return true;
}

// Verify that routes are properly received by the agents. Nexthop address,
// label and tunnel encapsulation information received are also verified.
bool XmppEcmpTest::CheckEnetRoute(test::NetworkAgentMock *agent, string net,
                                  string prefix, NextHops &nexthops) {
    const NetworkAgentMock::EnetRouteEntry *rt =
        agent->EnetRouteLookup(net, prefix);

    if (rt == NULL) return false;
    if (nexthops.size() != rt->entry.next_hops.next_hop.size()) return false;

    for (size_t i = 0; i < nexthops.size(); i++) {
        size_t j;

        // Next-hops can be received in any order. For the right next-hop
        // to match for, using the address part of the nexthop structure.
        for (j = 0; j < rt->entry.next_hops.next_hop.size(); j++) {
            if (nexthops[i].address_ ==
                    rt->entry.next_hops.next_hop[j].address) {
                break;
            }
        }
        if (j == rt->entry.next_hops.next_hop.size()) return false;

        // Check label and tunnel encapsulation.
        if (nexthops[i].label_ != rt->entry.next_hops.next_hop[j].label) {
            return false;
        }

        if (nexthops[i].tunnel_encapsulations_ !=
                rt->entry.next_hops.next_hop[j].tunnel_encapsulation_list.tunnel_encapsulation) {
            return false;
        }
    }

    return true;
}

bool XmppEcmpTest::CheckNoRoute(NetworkAgentMock *agent, string net,
                                string prefix) {
    return agent->RouteLookup(net, prefix) == NULL;
}

bool XmppEcmpTest::CheckNoEnetRoute(NetworkAgentMock *agent, string net,
                                    string prefix) {
    return agent->EnetRouteLookup(net, prefix) == NULL;
}

void XmppEcmpTest::VerifyRoute(string prefix, NextHops nexthops) {
    if (!inet_) return;

    if (agent_a_->IsSessionEstablished()) {
        task_util::WaitForCondition(&evm_,
                boost::bind(&XmppEcmpTest::CheckRoute, this,
                            agent_a_.get(), net_1_, prefix, nexthops),
                kTimeoutSeconds);
    }

    if (agent_b_->IsSessionEstablished()) {
        task_util::WaitForCondition(&evm_,
                boost::bind(&XmppEcmpTest::CheckRoute, this,
                            agent_b_.get(), net_2_, prefix, nexthops),
                kTimeoutSeconds);
    }

    if (agent_c_->IsSessionEstablished()) {
        task_util::WaitForCondition(&evm_,
                boost::bind(&XmppEcmpTest::CheckRoute, this,
                            agent_c_.get(), net_1_, prefix, nexthops),
                kTimeoutSeconds);
    }

    if (agent_d_->IsSessionEstablished()) {
        task_util::WaitForCondition(&evm_,
                boost::bind(&XmppEcmpTest::CheckRoute, this,
                            agent_d_.get(), net_2_, prefix, nexthops),
                kTimeoutSeconds);
    }
}

void XmppEcmpTest::VerifyEnetRoute(string prefix, string net,
                                   vector<test::NetworkAgentMock *> agents,
                                   NextHops nexthops) {
    if (!enet_) return;

    BOOST_FOREACH(test::NetworkAgentMock *agent, agents) {
        if (!agent->IsSessionEstablished()) continue;
        task_util::WaitForCondition(&evm_,
                boost::bind(&XmppEcmpTest::CheckEnetRoute, this, agent, net,
                            prefix, nexthops),
                kTimeoutSeconds);
    }
}

void XmppEcmpTest::VerifyNoRoute(string prefix) {
    if (!inet_) return;

    task_util::WaitForCondition(&evm_,
            boost::bind(&XmppEcmpTest::CheckNoRoute, this, agent_a_.get(),
                        net_1_, prefix), kTimeoutSeconds);
    task_util::WaitForCondition(&evm_,
            boost::bind(&XmppEcmpTest::CheckNoRoute, this, agent_b_.get(),
                        net_2_, prefix), kTimeoutSeconds);
    task_util::WaitForCondition(&evm_,
            boost::bind(&XmppEcmpTest::CheckNoRoute, this, agent_c_.get(),
                        net_1_, prefix), kTimeoutSeconds);
    task_util::WaitForCondition(&evm_,
            boost::bind(&XmppEcmpTest::CheckNoRoute, this, agent_d_.get(),
                        net_2_, prefix), kTimeoutSeconds);
}

void XmppEcmpTest::VerifyNoEnetRoute(string prefix, string net,
                                     vector<test::NetworkAgentMock *> agents) {
    if (!enet_) return;

    BOOST_FOREACH(test::NetworkAgentMock *agent, agents) {
        task_util::WaitForCondition(&evm_,
                boost::bind(&XmppEcmpTest::CheckNoEnetRoute, this, agent,
                            net, prefix), kTimeoutSeconds);
    }
}

void XmppEcmpTest::WaitForInetRouteCount(size_t count) {
    WaitForInetRouteCount(agent_a_.get(), count);
    WaitForInetRouteCount(agent_b_.get(), count);
    WaitForInetRouteCount(agent_c_.get(), count);
    WaitForInetRouteCount(agent_d_.get(), count);
}

void XmppEcmpTest::WaitForEnetRouteCount(size_t count,
                               std::vector<test::NetworkAgentMock *> agents) {
    BOOST_FOREACH(test::NetworkAgentMock *agent, agents) {
        WaitForEnetRouteCount(agent, count);
    }
}

void XmppEcmpTest::InitializeNextHops() {
    nexthops_a.clear();
    nexthops_b.clear();
    nexthops_c.clear();
    nexthops_d.clear();

    // For each nexthop in the list, also add source specific nexthop.
    BOOST_FOREACH(NextHop n, start_nexthops) {

        // Prepend to first nibble to generate different nexthops.
        NextHop n2 = n;
        n2.address_ = "1" + n.address_;
        nexthops_a.push_back(n2);
        nexthops_a.push_back(n);

        n2.address_ = "2" + n.address_;
        nexthops_b.push_back(n2);
        nexthops_b.push_back(n);

        n2.address_ = "3" + n.address_;
        nexthops_c.push_back(n2);
        nexthops_c.push_back(n);

        n2.address_ = "4" + n.address_;
        nexthops_d.push_back(n2);
        nexthops_d.push_back(n);
    }

    return;
}

void XmppEcmpTest::AddRoutesAndVerify() {
    InitializeNextHops();

    if (inet_) {
        agent_a_->AddRoute(net_1_, "10.0.1.1/32", nexthops_a);
        WaitForInetRouteCount(1);
        VerifyRoute("10.0.1.1/32", nexthops_a);

        agent_b_->AddRoute(net_2_, "10.0.1.1/32", nexthops_b);
        VerifyRoute("10.0.1.1/32", AddNextHops2(nexthops_a, nexthops_b));

        agent_c_->AddRoute(net_1_, "10.0.1.1/32", nexthops_c);
        VerifyRoute("10.0.1.1/32", AddNextHops3(nexthops_a, nexthops_b,
                                                nexthops_c));

        agent_d_->AddRoute(net_2_, "10.0.1.1/32", nexthops_d);
        VerifyRoute("10.0.1.1/32", AddNextHops4(nexthops_a,  nexthops_b,
                                                nexthops_c, nexthops_d));
    }

    vector<test::NetworkAgentMock *> lac =
        list_of(agent_a_.get())(agent_c_.get());
    vector<test::NetworkAgentMock *> lbd =
        list_of(agent_b_.get())(agent_d_.get());

    if (enet_) {
        agent_a_->AddEnetRoute(net_1_, "aa:00:00:00:00:0a,10.0.1.1/32",
                               nexthops_a);
        WaitForEnetRouteCount(1, lac);
        VerifyEnetRoute("aa:00:00:00:00:0a,10.0.1.1/32", net_1_, lac,
                        nexthops_a);

        agent_c_->AddEnetRoute(net_1_, "aa:00:00:00:00:0a,10.0.1.1/32",
                               nexthops_c);
        VerifyEnetRoute("aa:00:00:00:00:0a,10.0.1.1/32", net_1_, lac,
                        AddNextHops2(nexthops_a, nexthops_c));


        agent_b_->AddEnetRoute(net_2_, "aa:00:00:00:00:0a,10.0.1.1/32",
                               nexthops_b);
        WaitForEnetRouteCount(1, lbd);
        VerifyEnetRoute("aa:00:00:00:00:0a,10.0.1.1/32", net_2_, lbd,
                        nexthops_b);

        agent_d_->AddEnetRoute(net_2_, "aa:00:00:00:00:0a,10.0.1.1/32",
                               nexthops_d);
        VerifyEnetRoute("aa:00:00:00:00:0a,10.0.1.1/32", net_2_, lbd,
                        AddNextHops2(nexthops_b, nexthops_d));
    }
}

void XmppEcmpTest::UpdateRoutesAndVerify() {
    NextHops nexthops_a_old = nexthops_a;
    NextHops nexthops_b_old = nexthops_b;
    NextHops nexthops_c_old = nexthops_c;
    NextHops nexthops_d_old = nexthops_d;

    InitializeNextHops();

    if (inet_) {
        agent_a_->AddRoute(net_1_, "10.0.1.1/32", nexthops_a);
        VerifyRoute("10.0.1.1/32", AddNextHops4(nexthops_a,
                                                nexthops_b_old,
                                                nexthops_c_old,
                                                nexthops_d_old));

        agent_b_->AddRoute(net_2_, "10.0.1.1/32", nexthops_b);
        VerifyRoute("10.0.1.1/32", AddNextHops4(nexthops_a,
                                                nexthops_b,
                                                nexthops_c_old,
                                                nexthops_d_old));

        agent_c_->AddRoute(net_1_, "10.0.1.1/32", nexthops_c);
        VerifyRoute("10.0.1.1/32", AddNextHops4(nexthops_a,
                                                nexthops_b,
                                                nexthops_c,
                                                nexthops_d_old));

        agent_d_->AddRoute(net_2_, "10.0.1.1/32", nexthops_d);
        VerifyRoute("10.0.1.1/32", AddNextHops4(nexthops_a,
                                                nexthops_b,
                                                nexthops_c,
                                                nexthops_d));
    }

    vector<test::NetworkAgentMock *> lac =
        list_of(agent_a_.get())(agent_c_.get());
    vector<test::NetworkAgentMock *> lbd =
        list_of(agent_b_.get())(agent_d_.get());

    if (enet_) {
        agent_a_->AddEnetRoute(net_1_, "aa:00:00:00:00:0a,10.0.1.1/32",
                               nexthops_a);
        VerifyEnetRoute("aa:00:00:00:00:0a,10.0.1.1/32", net_1_, lac,
                        AddNextHops2(nexthops_a, nexthops_c_old));

        agent_c_->AddEnetRoute(net_1_, "aa:00:00:00:00:0a,10.0.1.1/32",
                               nexthops_c);
        VerifyEnetRoute("aa:00:00:00:00:0a,10.0.1.1/32", net_1_, lac,
                        AddNextHops2(nexthops_a, nexthops_c));


        agent_b_->AddEnetRoute(net_2_, "aa:00:00:00:00:0a,10.0.1.1/32",
                               nexthops_b);
        VerifyEnetRoute("aa:00:00:00:00:0a,10.0.1.1/32", net_2_, lbd,
                        AddNextHops2(nexthops_b, nexthops_d_old));

        agent_d_->AddEnetRoute(net_2_, "aa:00:00:00:00:0a,10.0.1.1/32",
                               nexthops_d);
        VerifyEnetRoute("aa:00:00:00:00:0a,10.0.1.1/32", net_2_, lbd,
                        AddNextHops2(nexthops_b, nexthops_d));
    }
}

void XmppEcmpTest::DeleteRoutesAndVerify(bool flap) {
    InitializeNextHops();

    vector<test::NetworkAgentMock *> lac =
        list_of(agent_a_.get())(agent_c_.get());
    vector<test::NetworkAgentMock *> lbd =
        list_of(agent_b_.get())(agent_d_.get());

    // Bringdown agent_a or agent_a's routes.
    if (!flap) {
        if (inet_) {
            agent_a_->DeleteRoute(net_1_, "10.0.1.1/32", nexthops_a);
        }
        if (enet_) {
            agent_a_->DeleteEnetRoute(net_1_, "aa:00:00:00:00:0a,10.0.1.1/32",
                                      nexthops_a);
        }
    } else {
        agent_a_->SessionDown();
        task_util::WaitForCondition(&evm_,
                boost::bind(&XmppEcmpTest::IsAgentDown, this, agent_a_.get()),
                kTimeoutSeconds);
    }
    if (inet_) {
        VerifyRoute("10.0.1.1/32", AddNextHops3(nexthops_b, nexthops_c,
                                                nexthops_d));
    }
    if (enet_) {
        VerifyEnetRoute("aa:00:00:00:00:0a,10.0.1.1/32", net_1_, lac,
                        nexthops_c);
        VerifyEnetRoute("aa:00:00:00:00:0a,10.0.1.1/32", net_2_, lbd,
                        AddNextHops2(nexthops_b, nexthops_d));
    }

    // Bringdown agent_b or agent_b's routes.
    if (!flap) {
        if (inet_) {
            agent_b_->DeleteRoute(net_2_, "10.0.1.1/32", nexthops_b);
        }
        if (enet_) {
            agent_b_->DeleteEnetRoute(net_2_, "aa:00:00:00:00:0a,10.0.1.1/32",
                                      nexthops_b);
        }
    } else {
        agent_b_->SessionDown();
        task_util::WaitForCondition(&evm_,
                boost::bind(&XmppEcmpTest::IsAgentDown, this, agent_b_.get()),
                kTimeoutSeconds);
    }
    if (inet_) {
        VerifyRoute("10.0.1.1/32", AddNextHops2(nexthops_c, nexthops_d));
    }
    if (enet_) {
        VerifyEnetRoute("aa:00:00:00:00:0a,10.0.1.1/32", net_1_, lac,
                        nexthops_c);
        VerifyEnetRoute("aa:00:00:00:00:0a,10.0.1.1/32", net_2_, lbd,
                        nexthops_d);
    }

    // Bringdown agent_c or agent_c's routes.
    if (!flap) {
        if (inet_) {
            agent_c_->DeleteRoute(net_1_, "10.0.1.1/32", nexthops_c);
        }
        if (enet_) {
            agent_c_->DeleteEnetRoute(net_1_, "aa:00:00:00:00:0a,10.0.1.1/32",
                                      nexthops_c);
        }
    } else {
        agent_c_->SessionDown();
        task_util::WaitForCondition(&evm_,
                boost::bind(&XmppEcmpTest::IsAgentDown, this, agent_c_.get()),
                kTimeoutSeconds);
    }
    if (inet_) {
        VerifyRoute("10.0.1.1/32", nexthops_d);
    }
    if (enet_) {
        VerifyNoEnetRoute("aa:00:00:00:00:0a,10.0.1.1/32", net_1_, lac);
        VerifyEnetRoute("aa:00:00:00:00:0a,10.0.1.1/32", net_2_, lbd,
                        nexthops_d);
    }

    // Bringdown agent_d or agent_d's routes.
    if (!flap) {
        if (inet_) {
            agent_d_->DeleteRoute(net_2_, "10.0.1.1/32", nexthops_d);
        }
        if (enet_) {
            agent_d_->DeleteEnetRoute(net_2_, "aa:00:00:00:00:0a,10.0.1.1/32",
                                      nexthops_d);
        }
    } else {
        agent_d_->SessionDown();
        task_util::WaitForCondition(&evm_,
                boost::bind(&XmppEcmpTest::IsAgentDown, this, agent_d_.get()),
                kTimeoutSeconds);
    }
    if (inet_) {
        VerifyNoRoute("10.0.1.1/32");
    }
    if (enet_) {
        VerifyNoEnetRoute("aa:00:00:00:00:0a,10.0.1.1/32", net_1_, lac);
        VerifyNoEnetRoute("aa:00:00:00:00:0a,10.0.1.1/32", net_2_, lbd);
    }
}

// Add routes with multiple next-hops.
TEST_F(XmppEcmpTest, AddAllNextHops) {
    SCOPED_TRACE(__FUNCTION__);
    inet_ = true;
    enet_ = true;
    Subscribe();
    AddRoutesAndVerify();
}

// Remove all next-hops
TEST_F(XmppEcmpTest, DeleteAllNextHops) {
    SCOPED_TRACE(__FUNCTION__);
    inet_ = true;
    enet_ = true;
    Subscribe();
    AddRoutesAndVerify();
    DeleteRoutesAndVerify(false);
}

// Remove all next-hops
TEST_F(XmppEcmpTest, AgentDown) {
    SCOPED_TRACE(__FUNCTION__);
    inet_ = true;
    enet_ = true;
    Subscribe();
    AddRoutesAndVerify();
    DeleteRoutesAndVerify(true);
}

// After adding some routes with multiple next-hops, readvertise the same set
// of routes with completely different set of next-hops.
TEST_F(XmppEcmpTest, Inet_RemoveAllAndAddSomeNextHops) {
    SCOPED_TRACE(__FUNCTION__);
    inet_ = true;
    Subscribe();
    AddRoutesAndVerify();

    start_nexthops.clear();
    start_nexthops.push_back(NextHop("1.2.1.1", 6, "udp"));
    start_nexthops.push_back(NextHop("1.2.1.2", 7, "udp"));
    start_nexthops.push_back(NextHop("1.2.1.3", 8, "udp"));
    start_nexthops.push_back(NextHop("1.2.1.4", 9, "udp"));
    start_nexthops.push_back(NextHop("1.2.1.5", 10, "udp"));
    UpdateRoutesAndVerify();
}

// Readvertise route with partial next-hops
TEST_F(XmppEcmpTest, Inet_RemoveSomeNextHops) {
    SCOPED_TRACE(__FUNCTION__);
    inet_ = true;
    Subscribe();
    AddRoutesAndVerify();

    start_nexthops.clear();

    // TODO : Change all to udp
    start_nexthops.push_back(NextHop("1.1.1.3", 3, "all"));
    start_nexthops.push_back(NextHop("1.1.1.4", 4, "all"));
    start_nexthops.push_back(NextHop("1.1.1.5", 5, "all"));
    UpdateRoutesAndVerify();
}

// Remove some next-hops and add sme new ones during readvertisement.
TEST_F(XmppEcmpTest, Inet_RemoveSomeAndAddSomeNextHops) {
    SCOPED_TRACE(__FUNCTION__);
    inet_ = true;
    Subscribe();
    AddRoutesAndVerify();

    start_nexthops.clear();

    // TODO : Change all to udp
    start_nexthops.push_back(NextHop("1.1.1.1", 1, "udp"));
    start_nexthops.push_back(NextHop("1.1.1.2", 2, "udp"));
    start_nexthops.push_back(NextHop("1.2.1.3", 6, "udp"));
    start_nexthops.push_back(NextHop("1.2.1.4", 7, "udp"));
    start_nexthops.push_back(NextHop("1.2.1.5", 8, "udp"));
    UpdateRoutesAndVerify();
}

// Enet ecmp tests

// After adding some routes with multiple next-hops, readvertise the same set
// of routes with completely different set of next-hops.
TEST_F(XmppEcmpTest, Enet_RemoveAllAndAddSomeNextHops) {
    SCOPED_TRACE(__FUNCTION__);
    enet_ = true;
    Subscribe();
    AddRoutesAndVerify();

    start_nexthops.clear();

    // TODO : Change all to udp
    start_nexthops.push_back(NextHop("1.2.1.1", 6, "all"));
    start_nexthops.push_back(NextHop("1.2.1.2", 7, "all"));
    start_nexthops.push_back(NextHop("1.2.1.3", 8, "all"));
    start_nexthops.push_back(NextHop("1.2.1.4", 9, "all"));
    start_nexthops.push_back(NextHop("1.2.1.5", 10, "all"));
    UpdateRoutesAndVerify();
}

// Readvertise route with partial next-hops
TEST_F(XmppEcmpTest, Enet_RemoveSomeNextHops) {
    SCOPED_TRACE(__FUNCTION__);
    enet_ = true;
    Subscribe();
    AddRoutesAndVerify();

    // TODO: Change to udp and fix.
    start_nexthops.clear();
    start_nexthops.push_back(NextHop("1.1.1.3", 3, "all"));
    start_nexthops.push_back(NextHop("1.1.1.4", 4, "all"));
    start_nexthops.push_back(NextHop("1.1.1.5", 5, "all"));
    UpdateRoutesAndVerify();
}

// Remove some next-hops and add sme new ones during readvertisement.
TEST_F(XmppEcmpTest, Enet_RemoveSomeAndAddSomeNextHops) {
    SCOPED_TRACE(__FUNCTION__);
    enet_ = true;
    Subscribe();
    AddRoutesAndVerify();

    // TODO: Change to udp and fix.
    start_nexthops.clear();
    start_nexthops.push_back(NextHop("1.1.1.1", 1, "all"));
    start_nexthops.push_back(NextHop("1.1.1.2", 2, "all"));
    start_nexthops.push_back(NextHop("1.2.1.3", 6, "all"));
    start_nexthops.push_back(NextHop("1.2.1.4", 7, "all"));
    start_nexthops.push_back(NextHop("1.2.1.5", 8, "all"));
    UpdateRoutesAndVerify();
}

// Send random sample set of next-hops for the same set of routes. Randomize
// nexthop address, label value as well as tunnel encapsulations.
TEST_F(XmppEcmpTest, RandomNextHopUpdates) {
    SCOPED_TRACE(__FUNCTION__);
    inet_ = true;
    enet_ = true;
    Subscribe();

    start_nexthops.clear();
    start_nexthops.push_back(NextHop("1.1.1.1", 1, "all"));
    start_nexthops.push_back(NextHop("1.1.1.2", 1, "all"));
    start_nexthops.push_back(NextHop("1.1.1.3", 1, "all"));
    start_nexthops.push_back(NextHop("1.1.1.4", 1, "all"));
    start_nexthops.push_back(NextHop("1.1.1.5", 1, "all"));
    AddRoutesAndVerify();

    srand(getpid());
    vector<string> addresses =
        list_of("1.1.1.1")("1.1.1.2")("1.1.1.3")("1.1.1.4")("1.1.1.5")
               ("1.2.1.1")("1.2.1.2")("1.2.1.3")("1.2.1.4")("1.2.1.5");

    // TODO: Vary types and labels. This does not work as not all identical
    // next-hops with varying label/tunnel-encap gets sent across bgp cloud
    // as RD is only based on address and instance-id.
    //
    // vector<int> labels = list_of(1)(2)(3)(4)(5)(6)(7)(8)(9)(10);
    // vector<string> encaps = list_of("all")("gre")("udp")("vxlan");
    vector<int> labels = list_of(1);
    vector<string> encaps = list_of("all");

    size_t count = 5;
    char *p = getenv("XMPP_ECMP_TEST_RANDOM_COUNT");
    if (p != NULL) stringToInteger(p, count);

    for (size_t i = 0; !count || i < count; i++) {
        start_nexthops.clear();
        size_t n_nexthops = (rand() % addresses.size()) + 1;
        for (size_t j = 0; j < n_nexthops; j++) {
            string address;

            // Select next-hop address randomly and avoid duplicates.
            while (true) {
                address = addresses[rand() % addresses.size()];
                bool present = false;
                for (NextHops::iterator iter = start_nexthops.begin();
                        iter != start_nexthops.end(); iter++) {
                    if ((*iter).address_ == address) {
                        present = true;
                        break;
                    }
                }
                if (!present) break;
            }
            start_nexthops.push_back(NextHop(address,
                                     labels[rand() % labels.size()],
                                     encaps[rand() % encaps.size()]));
         }

        ostringstream os;

        os << i << "/" << count << " :Feed "
           << start_nexthops.size() * 2 << " nexthops" << endl;
        LOG(DEBUG, os.str());
        UpdateRoutesAndVerify();
    }
}

class TestEnvironment : public ::testing::Environment {
    virtual ~TestEnvironment() { }
};

static void SetUp() {
    ControlNode::SetDefaultSchedulingPolicy();
    BgpServerTest::GlobalSetUp();
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
