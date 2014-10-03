/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/test/task_test_util.h"
#include "bgp/bgp_factory.h"
#include "bgp/bgp_session_manager.h"
#include "bgp/bgp_xmpp_channel.h"
#include "bgp/test/bgp_server_test_util.h"
#include "control-node/control_node.h"
#include "control-node/test/network_agent_mock.h"
#include "io/test/event_manager_test.h"
#include "schema/xmpp_enet_types.h"
#include "testing/gunit.h"
#include "xmpp/xmpp_server.h"

using namespace std;

class BgpXmppChannelMock : public BgpXmppChannel {
public:
    BgpXmppChannelMock(XmppChannel *channel, BgpServer *server, 
            BgpXmppChannelManager *manager) : 
        BgpXmppChannel(channel, server, manager), count_(0) {
            bgp_policy_ = RibExportPolicy(BgpProto::XMPP,
                                          RibExportPolicy::XMPP, -1, 0);
    }

    virtual void ReceiveUpdate(const XmppStanza::XmppMessage *msg) {
        count_ ++;
        BgpXmppChannel::ReceiveUpdate(msg);
    }

    size_t Count() const { return count_; }
    void ResetCount() { count_ = 0; }
    virtual ~BgpXmppChannelMock() { }

private:
    size_t count_;
};

class BgpXmppChannelManagerMock : public BgpXmppChannelManager {
public:
    BgpXmppChannelManagerMock(XmppServer *x, BgpServer *b) :
        BgpXmppChannelManager(x, b) { }

    virtual void XmppHandleChannelEvent(XmppChannel *channel,
                                        xmps::PeerState state) {
         BgpXmppChannelManager::XmppHandleChannelEvent(channel, state);
    }

    virtual BgpXmppChannel *CreateChannel(XmppChannel *channel) {
        BgpXmppChannel *mock_channel =
            new BgpXmppChannelMock(channel, bgp_server_, this);
        return mock_channel;
    }
};

class BgpXmppEvpnMcastTestBase : public ::testing::Test {
protected:

    static const int kAgentCount = 8;
    static const int kMxCount = 4;

    BgpXmppEvpnMcastTestBase() : thread_(&evm_) {
        mx_params_.edge_replication_not_supported = true;
    }

    virtual void SetUp() {
        bs_x_.reset(new BgpServerTest(&evm_, "X"));
        xs_x_ = new XmppServer(&evm_, test::XmppDocumentMock::kControlNodeJID);
        bs_x_->session_manager()->Initialize(0);
        LOG(DEBUG, "Created server at port: " <<
            bs_x_->session_manager()->GetPort());
        xs_x_->Initialize(0, false);
        bgp_channel_manager_x_.reset(
            new BgpXmppChannelManagerMock(xs_x_, bs_x_.get()));

        bs_y_.reset(new BgpServerTest(&evm_, "Y"));
        xs_y_ = new XmppServer(&evm_, test::XmppDocumentMock::kControlNodeJID);
        bs_y_->session_manager()->Initialize(0);
        LOG(DEBUG, "Created server at port: " <<
            bs_y_->session_manager()->GetPort());
        xs_y_->Initialize(0, false);
        bgp_channel_manager_y_.reset(
            new BgpXmppChannelManagerMock(xs_y_, bs_y_.get()));

        thread_.Start();
        task_util::WaitForIdle();
    }

    virtual void TearDown() {
        xs_x_->Shutdown();
        task_util::WaitForIdle();
        bs_x_->Shutdown();
        task_util::WaitForIdle();
        bgp_channel_manager_x_.reset();
        TcpServerManager::DeleteServer(xs_x_);
        xs_x_ = NULL;

        xs_y_->Shutdown();
        task_util::WaitForIdle();
        bs_y_->Shutdown();
        task_util::WaitForIdle();
        bgp_channel_manager_y_.reset();
        TcpServerManager::DeleteServer(xs_y_);
        xs_y_ = NULL;

        DeleteAgents();
        DeleteMxs();
        evm_.Shutdown();
        thread_.Join();
        task_util::WaitForIdle();
        STLDeleteValues(&agents_);
        STLDeleteValues(&mxs_);
    }

    string BroadcastMac() {
        return integerToString(tag_) + "-" + "ff:ff:ff:ff:ff:ff,0.0.0.0/32";
    }

    string BroadcastMacXmppId() {
        if (tag_ == 0)
            return "ff:ff:ff:ff:ff:ff,0.0.0.0/32";
        return BroadcastMac();
    }

    bool VerifyVrouterInOList(test::NetworkAgentMock *vrouter, bool is_mx,
        int idx, const autogen::EnetItemType *rt) {
        char nh_addr[32];
        if (is_mx) {
            snprintf(nh_addr, sizeof(nh_addr), "192.168.1.2%d", idx);
        } else {
            snprintf(nh_addr, sizeof(nh_addr), "192.168.1.1%d", idx);
        }
        const autogen::EnetOlistType &olist = rt->entry.olist;
        if (olist.next_hop.size() == 0)
            return false;

        bool found = false;
        BOOST_FOREACH(const autogen::EnetNextHopType &nh, olist.next_hop) {
            if (nh.address == nh_addr) {
                EXPECT_FALSE(found);
                found = true;
            }
        }
        return found;
    }

    bool VerifyVrouterNotInOList(test::NetworkAgentMock *vrouter, bool is_mx,
        int idx, const autogen::EnetItemType *rt) {
        if (!rt)
            return false;

        const autogen::EnetOlistType &olist = rt->entry.olist;
        if (olist.next_hop.size() == 0)
            return false;

        char nh_addr[32];
        if (is_mx) {
            snprintf(nh_addr, sizeof(nh_addr), "192.168.1.2%d", idx);
        } else {
            snprintf(nh_addr, sizeof(nh_addr), "192.168.1.1%d", idx);
        }
        bool found = false;
        BOOST_FOREACH(const autogen::EnetNextHopType &nh, olist.next_hop) {
            if (nh.address == nh_addr)
                return false;
        }
        return true;
    }

    bool VerifyOListCommon(test::NetworkAgentMock *vrouter,
        bool odd, bool even, bool include_agents,
        bool odd_agents = true, bool even_agents = true) {
        const autogen::EnetItemType *rt =
            vrouter->EnetRouteLookup("blue", BroadcastMacXmppId());

        size_t count = 0;
        int mx_idx = 0;
        BOOST_FOREACH(test::NetworkAgentMock *mx, mxs_) {
            if (vrouter == mx) {
                mx_idx++;
                continue;
            }
            if ((odd && mx_idx % 2 != 0) || (even && mx_idx % 2 == 0)) {
                if (!rt)
                    return false;
                if (!VerifyVrouterInOList(mx, true, mx_idx, rt))
                    return false;
                count++;
            } else {
                if (!VerifyVrouterNotInOList(mx, true, mx_idx, rt))
                    return false;
            }
            mx_idx++;
        }

        int agent_idx = 0;
        if (include_agents) {
            BOOST_FOREACH(test::NetworkAgentMock *agent, agents_) {
                if (!rt)
                    return false;
                if ((odd_agents && agent_idx % 2 != 0) ||
                    (even_agents && agent_idx % 2 == 0)) {
                    if (!VerifyVrouterInOList(agent, false, agent_idx, rt))
                        return false;
                    count++;
                } else {
                    if (!VerifyVrouterNotInOList(agent, false, agent_idx, rt))
                        return false;
                }
                agent_idx++;
            }
        }

        const autogen::EnetOlistType &olist = rt->entry.olist;
        if (olist.next_hop.size() != count)
            return false;

        return true;
    }

    void CreateAgents() {
        for (int idx = 0; idx < kAgentCount; ++idx) {
            char name[32];
            snprintf(name, sizeof(name), "agent-%d", idx);
            char local_addr[32];
            snprintf(local_addr, sizeof(local_addr), "127.0.0.1%d", idx);
            string remote_addr;
            int port;
            if (single_server_) {
                remote_addr = "127.0.0.1";
                port = xs_x_->GetPort();
            } else {
                remote_addr = (idx % 2 == 0) ? "127.0.0.1" : "127.0.0.2";
                port = (idx % 2 == 0) ? xs_x_->GetPort() : xs_y_->GetPort();
            }
            test::NetworkAgentMock *agent = new test::NetworkAgentMock(
                &evm_, name, port, local_addr, remote_addr);
            agents_.push_back(agent);
            TASK_UTIL_EXPECT_TRUE(agent->IsEstablished());
        }
    }

    void DeleteAgents() {
        BOOST_FOREACH(test::NetworkAgentMock *agent, agents_) {
            agent->Delete();
        }
    }

    void SubscribeAgents() {
        BOOST_FOREACH(test::NetworkAgentMock *agent, agents_) {
            agent->EnetSubscribe("blue", 1);
            task_util::WaitForIdle();
        }
    }

    void UnsubscribeAgents() {
        BOOST_FOREACH(test::NetworkAgentMock *agent, agents_) {
            agent->EnetUnsubscribe("blue");
            task_util::WaitForIdle();
        }
    }

    void AddAgentsBroadcastMacRouteCommon(bool odd, bool even) {
        int idx = 0;
        BOOST_FOREACH(test::NetworkAgentMock *agent, agents_) {
            char nh_addr[32];
            snprintf(nh_addr, sizeof(nh_addr), "192.168.1.1%d", idx);
            if ((odd && idx % 2 != 0) || (even && idx % 2 == 0)) {
                agent->AddEnetRoute("blue", BroadcastMac(), nh_addr);
                task_util::WaitForIdle();
            }
            idx++;
        }
    }

    void AddOddAgentsBroadcastMacRoute() {
        AddAgentsBroadcastMacRouteCommon(true, false);
    }

    void AddEvenAgentsBroadcastMacRoute() {
        AddAgentsBroadcastMacRouteCommon(false, true);
    }

    void AddAllAgentsBroadcastMacRoute() {
        AddAgentsBroadcastMacRouteCommon(true, true);
    }

    void DelAgentsBroadcastMacRouteCommon(bool odd, bool even) {
        int idx = 0;
        BOOST_FOREACH(test::NetworkAgentMock *agent, agents_) {
            if ((odd && idx % 2 != 0) || (even && idx % 2 == 0)) {
                agent->DeleteEnetRoute("blue", BroadcastMac());
                task_util::WaitForIdle();
            }
            idx++;
        }
    }

    void DelOddAgentsBroadcastMacRoute() {
        DelAgentsBroadcastMacRouteCommon(true, false);
    }

    void DelEvenAgentsBroadcastMacRoute() {
        DelAgentsBroadcastMacRouteCommon(false, true);
    }
    void DelAllAgentsBroadcastMacRoute() {
        DelAgentsBroadcastMacRouteCommon(true, true);
    }

    void VerifyAllAgentsOddMxsOlist() {
        BOOST_FOREACH(test::NetworkAgentMock *agent, agents_) {
            TASK_UTIL_EXPECT_TRUE(
                VerifyOListCommon(agent, true, false, false));
        }
    }

    void VerifyAllAgentsEvenMxsOlist() {
        BOOST_FOREACH(test::NetworkAgentMock *agent, agents_) {
            TASK_UTIL_EXPECT_TRUE(
                VerifyOListCommon(agent, false, true, false));
        }
    }

    void VerifyAgentsOlistCommon(bool odd, bool even) {
        int idx = 0;
        BOOST_FOREACH(test::NetworkAgentMock *agent, agents_) {
            if ((odd && idx % 2 != 0) || (even && idx % 2 == 0)) {
                TASK_UTIL_EXPECT_TRUE(
                    VerifyOListCommon(agent, true, true, false));
            } else {
                TASK_UTIL_EXPECT_TRUE(
                    agent->EnetRouteLookup("blue", BroadcastMacXmppId()) == NULL);
            }
            idx++;
        }
    }

    void VerifyOddAgentsOlist() {
        VerifyAgentsOlistCommon(true, false);
    }

    void VerifyEvenAgentsOlist() {
        VerifyAgentsOlistCommon(false, true);
    }

    void VerifyAllAgentsOlist() {
        VerifyAgentsOlistCommon(true, true);
    }

    void VerifyAllAgentsNoOlist() {
        VerifyAgentsOlistCommon(false, false);
    }

    void CreateMxs() {
        for (int idx = 0; idx < kMxCount; ++idx) {
            char name[32];
            snprintf(name, sizeof(name), "mx-%d", idx);
            char local_addr[32];
            snprintf(local_addr, sizeof(local_addr), "127.0.0.2%d", idx);
            string remote_addr;
            int port;
            if (single_server_) {
                remote_addr = "127.0.0.1";
                port = xs_x_->GetPort();
            } else {
                remote_addr = (idx % 2 == 0) ? "127.0.0.1" : "127.0.0.2";
                port = (idx % 2 == 0) ? xs_x_->GetPort() : xs_y_->GetPort();
            }
            test::NetworkAgentMock *mx = new test::NetworkAgentMock(
                &evm_, name, port, local_addr, remote_addr);
            mxs_.push_back(mx);
            TASK_UTIL_EXPECT_TRUE(mx->IsEstablished());
        }
    }

    void DeleteMxs() {
        BOOST_FOREACH(test::NetworkAgentMock *mx, mxs_) {
            mx->Delete();
        }
    }

    void SubscribeMxs() {
        BOOST_FOREACH(test::NetworkAgentMock *mx, mxs_) {
            mx->EnetSubscribe("blue", 1);
            task_util::WaitForIdle();
        }
    }

    void UnsubscribeMxs() {
        BOOST_FOREACH(test::NetworkAgentMock *mx, mxs_) {
            mx->EnetUnsubscribe("blue");
            task_util::WaitForIdle();
        }
    }

    void AddMxsBroadcastMacRouteCommon(bool odd, bool even) {
        int idx = 0;
        BOOST_FOREACH(test::NetworkAgentMock *mx, mxs_) {
            char nh_addr[32];
            snprintf(nh_addr, sizeof(nh_addr), "192.168.1.2%d", idx);
            if ((odd && idx % 2 != 0) || (even && idx % 2 == 0)) {
                mx->AddEnetRoute("blue", BroadcastMac(), nh_addr, &mx_params_);
                task_util::WaitForIdle();
            }
            idx++;
        }
    }

    void AddOddMxsBroadcastMacRoute() {
        AddMxsBroadcastMacRouteCommon(true, false);
    }

    void AddEvenMxsBroadcastMacRoute() {
        AddMxsBroadcastMacRouteCommon(false, true);
    }

    void AddAllMxsBroadcastMacRoute() {
        AddMxsBroadcastMacRouteCommon(true, true);
    }

    void DelMxsBroadcastMacRouteCommon(bool odd, bool even) {
        int idx = 0;
        BOOST_FOREACH(test::NetworkAgentMock *mx, mxs_) {
            if ((odd && idx % 2 != 0) || (even && idx % 2 == 0)) {
                mx->DeleteEnetRoute("blue", BroadcastMac());
                task_util::WaitForIdle();
            }
            idx++;
        }
    }

    void DelOddMxsBroadcastMacRoute() {
        DelMxsBroadcastMacRouteCommon(true, false);
    }

    void DelEvenMxsBroadcastMacRoute() {
        DelMxsBroadcastMacRouteCommon(false, true);
    }

    void DelAllMxsBroadcastMacRoute() {
        DelMxsBroadcastMacRouteCommon(true, true);
    }

    void VerifyMxsOlistCommon(bool odd, bool even,
        bool odd_agents, bool even_agents) {
        int idx = 0;
        BOOST_FOREACH(test::NetworkAgentMock *mx, mxs_) {
            if ((odd && idx % 2 != 0) || (even && idx % 2 == 0)) {
                TASK_UTIL_EXPECT_TRUE(VerifyOListCommon(
                    mx, odd, even, true, odd_agents, even_agents));
            } else {
                TASK_UTIL_EXPECT_TRUE(
                    mx->EnetRouteLookup("blue", BroadcastMacXmppId()) == NULL);
            }
            idx++;
        }
    }

    void VerifyOddMxsOlist(bool odd_agents = true, bool even_agents = true) {
        VerifyMxsOlistCommon(true, false, odd_agents, even_agents);
    }

    void VerifyEvenMxsOlist(bool odd_agents = true, bool even_agents = true) {
        VerifyMxsOlistCommon(false, true, odd_agents, even_agents);
    }

    void VerifyAllMxsOlist(bool odd_agents = true, bool even_agents = true) {
        VerifyMxsOlistCommon(true, true, odd_agents, even_agents);
    }

    void VerifyAllMxsNoOlist() {
        VerifyMxsOlistCommon(false, false, false, false);
    }

    EventManager evm_;
    ServerThread thread_;
    BgpServerTestPtr bs_x_;
    BgpServerTestPtr bs_y_;
    XmppServer *xs_x_;
    XmppServer *xs_y_;
    boost::scoped_ptr<BgpXmppChannelManagerMock> bgp_channel_manager_x_;
    boost::scoped_ptr<BgpXmppChannelManagerMock> bgp_channel_manager_y_;
    vector<test::NetworkAgentMock *> agents_;
    vector<test::NetworkAgentMock *> mxs_;
    test::RouteParams mx_params_;
    bool single_server_;
    uint32_t tag_;
};

static const char *config_template = "\
<config>\
    <bgp-router name=\'X\'>\
        <identifier>192.168.0.1</identifier>\
        <address>127.0.0.1</address>\
        <port>%d</port>\
        <session to=\'Y\'>\
            <address-families>\
                <family>e-vpn</family>\
            </address-families>\
        </session>\
    </bgp-router>\
    <bgp-router name=\'Y\'>\
        <identifier>192.168.0.2</identifier>\
        <address>127.0.0.1</address>\
        <port>%d</port>\
        <session to=\'X\'>\
            <address-families>\
                <family>e-vpn</family>\
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

// Parameterize single vs. dual servers and the tag value.

typedef std::tr1::tuple<bool, uint32_t> TestParams;

//
// 2 Control Nodes X and Y.
//
class BgpXmppEvpnMcastTest :
    public BgpXmppEvpnMcastTestBase,
    public ::testing::WithParamInterface<TestParams> {

protected:
    virtual void SetUp() {
        single_server_ = std::tr1::get<0>(GetParam());
        tag_ = std::tr1::get<1>(GetParam());
        BgpXmppEvpnMcastTestBase::SetUp();
    }

    virtual void TearDown() {
        BgpXmppEvpnMcastTestBase::TearDown();
    }

    void Configure() {
        char config[8192];
        snprintf(config, sizeof(config), config_template,
            bs_x_->session_manager()->GetPort(),
            bs_y_->session_manager()->GetPort());
        bs_x_->Configure(config);
        bs_y_->Configure(config);
        task_util::WaitForIdle();
    }
};

TEST_P(BgpXmppEvpnMcastTest, Noop) {
    Configure();
}

TEST_P(BgpXmppEvpnMcastTest, Basic1) {
    Configure();
    CreateAgents();
    CreateMxs();
    SubscribeAgents();
    SubscribeMxs();

    AddAllAgentsBroadcastMacRoute();
    AddAllMxsBroadcastMacRoute();
    VerifyAllAgentsOlist();
    VerifyAllMxsOlist();

    DelAllAgentsBroadcastMacRoute();
    DelAllMxsBroadcastMacRoute();
    UnsubscribeAgents();
    UnsubscribeMxs();
}

TEST_P(BgpXmppEvpnMcastTest, Basic2) {
    Configure();
    CreateAgents();
    CreateMxs();
    SubscribeAgents();
    SubscribeMxs();

    AddAllMxsBroadcastMacRoute();
    AddAllAgentsBroadcastMacRoute();
    VerifyAllAgentsOlist();
    VerifyAllMxsOlist();

    DelAllMxsBroadcastMacRoute();
    DelAllAgentsBroadcastMacRoute();
    UnsubscribeAgents();
    UnsubscribeMxs();
}

TEST_P(BgpXmppEvpnMcastTest, Basic3) {
    Configure();
    CreateAgents();
    SubscribeAgents();
    AddAllAgentsBroadcastMacRoute();

    CreateMxs();
    SubscribeMxs();
    AddAllMxsBroadcastMacRoute();

    VerifyAllAgentsOlist();
    VerifyAllMxsOlist();

    DelAllAgentsBroadcastMacRoute();
    DelAllMxsBroadcastMacRoute();
    UnsubscribeAgents();
    UnsubscribeMxs();
}

TEST_P(BgpXmppEvpnMcastTest, Basic4) {
    Configure();

    CreateMxs();
    SubscribeMxs();
    AddAllMxsBroadcastMacRoute();

    CreateAgents();
    SubscribeAgents();
    AddAllAgentsBroadcastMacRoute();

    VerifyAllAgentsOlist();
    VerifyAllMxsOlist();

    DelAllMxsBroadcastMacRoute();
    DelAllAgentsBroadcastMacRoute();
    UnsubscribeAgents();
    UnsubscribeMxs();
}

TEST_P(BgpXmppEvpnMcastTest, AddMxs) {
    Configure();
    CreateAgents();
    CreateMxs();
    SubscribeAgents();
    SubscribeMxs();

    AddAllAgentsBroadcastMacRoute();
    AddOddMxsBroadcastMacRoute();
    VerifyAllAgentsOddMxsOlist();
    VerifyOddMxsOlist();

    AddEvenMxsBroadcastMacRoute();
    VerifyAllAgentsOlist();
    VerifyAllMxsOlist();

    DelAllAgentsBroadcastMacRoute();
    DelAllMxsBroadcastMacRoute();
    VerifyAllAgentsNoOlist();
    VerifyAllMxsNoOlist();

    UnsubscribeAgents();
    UnsubscribeMxs();
}

TEST_P(BgpXmppEvpnMcastTest, DelMxs) {
    Configure();
    CreateAgents();
    CreateMxs();
    SubscribeAgents();
    SubscribeMxs();

    AddAllAgentsBroadcastMacRoute();
    AddAllMxsBroadcastMacRoute();
    VerifyAllAgentsOlist();
    VerifyAllMxsOlist();

    DelOddMxsBroadcastMacRoute();
    VerifyAllAgentsEvenMxsOlist();
    VerifyEvenMxsOlist();

    DelEvenMxsBroadcastMacRoute();
    VerifyAllAgentsNoOlist();
    VerifyAllMxsNoOlist();

    DelAllAgentsBroadcastMacRoute();
    UnsubscribeAgents();
    UnsubscribeMxs();
}

TEST_P(BgpXmppEvpnMcastTest, AddDelMxs) {
    Configure();
    CreateAgents();
    CreateMxs();
    SubscribeAgents();
    SubscribeMxs();
    AddAllAgentsBroadcastMacRoute();

    AddOddMxsBroadcastMacRoute();
    VerifyAllAgentsOddMxsOlist();
    VerifyOddMxsOlist();

    AddEvenMxsBroadcastMacRoute();
    VerifyAllAgentsOlist();
    VerifyAllMxsOlist();

    DelOddMxsBroadcastMacRoute();
    VerifyAllAgentsEvenMxsOlist();
    VerifyEvenMxsOlist();

    AddOddMxsBroadcastMacRoute();
    VerifyAllAgentsOlist();
    VerifyAllMxsOlist();

    DelEvenMxsBroadcastMacRoute();
    VerifyAllAgentsOddMxsOlist();
    VerifyOddMxsOlist();

    AddEvenMxsBroadcastMacRoute();
    VerifyAllAgentsOlist();
    VerifyAllMxsOlist();

    DelAllAgentsBroadcastMacRoute();
    DelAllMxsBroadcastMacRoute();
    VerifyAllAgentsNoOlist();
    VerifyAllMxsNoOlist();

    UnsubscribeAgents();
    UnsubscribeMxs();
}

TEST_P(BgpXmppEvpnMcastTest, AddAgents) {
    Configure();
    CreateAgents();
    CreateMxs();
    SubscribeAgents();
    SubscribeMxs();

    AddOddAgentsBroadcastMacRoute();
    AddAllMxsBroadcastMacRoute();
    VerifyOddAgentsOlist();
    VerifyAllMxsOlist(true, false);

    AddEvenAgentsBroadcastMacRoute();
    VerifyAllAgentsOlist();
    VerifyAllMxsOlist(true, true);

    DelAllAgentsBroadcastMacRoute();
    DelAllMxsBroadcastMacRoute();
    VerifyAllAgentsNoOlist();
    VerifyAllMxsNoOlist();

    UnsubscribeAgents();
    UnsubscribeMxs();
}

TEST_P(BgpXmppEvpnMcastTest, DelAgents) {
    Configure();
    CreateAgents();
    CreateMxs();
    SubscribeAgents();
    SubscribeMxs();

    AddAllAgentsBroadcastMacRoute();
    AddAllMxsBroadcastMacRoute();
    VerifyAllAgentsOlist();
    VerifyAllMxsOlist(true, true);

    DelOddAgentsBroadcastMacRoute();
    VerifyEvenAgentsOlist();
    VerifyAllMxsOlist(false, true);

    DelEvenAgentsBroadcastMacRoute();
    VerifyAllAgentsNoOlist();
    VerifyAllMxsOlist(false, false);

    DelAllMxsBroadcastMacRoute();
    VerifyAllAgentsNoOlist();
    VerifyAllMxsNoOlist();

    UnsubscribeAgents();
    UnsubscribeMxs();
}

TEST_P(BgpXmppEvpnMcastTest, AddDelAgents) {
    Configure();
    CreateAgents();
    CreateMxs();
    SubscribeAgents();
    SubscribeMxs();
    AddAllMxsBroadcastMacRoute();

    AddOddAgentsBroadcastMacRoute();
    VerifyOddAgentsOlist();
    VerifyAllMxsOlist(true, false);

    AddEvenAgentsBroadcastMacRoute();
    VerifyAllAgentsOlist();
    VerifyAllMxsOlist(true, true);

    DelOddAgentsBroadcastMacRoute();
    VerifyEvenAgentsOlist();
    VerifyAllMxsOlist(false, true);

    AddOddAgentsBroadcastMacRoute();
    VerifyAllAgentsOlist();
    VerifyAllMxsOlist(true, true);

    DelEvenAgentsBroadcastMacRoute();
    VerifyOddAgentsOlist();
    VerifyAllMxsOlist(true, false);

    AddEvenAgentsBroadcastMacRoute();
    VerifyAllAgentsOlist();
    VerifyAllMxsOlist(true, true);

    DelAllAgentsBroadcastMacRoute();
    DelAllMxsBroadcastMacRoute();
    VerifyAllAgentsNoOlist();
    VerifyAllMxsNoOlist();

    UnsubscribeAgents();
    UnsubscribeMxs();
}

INSTANTIATE_TEST_CASE_P(Default, BgpXmppEvpnMcastTest,
    ::testing::Combine(::testing::Bool(), ::testing::Values(0, 4094)));

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
