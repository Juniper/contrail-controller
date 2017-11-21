/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/foreach.hpp>

#include "bgp/bgp_config.h"
#include "bgp/bgp_config_parser.h"
#include "bgp/bgp_factory.h"
#include "bgp/bgp_multicast.h"
#include "bgp/bgp_mvpn.h"
#include "bgp/bgp_sandesh.h"
#include "bgp/bgp_session_manager.h"
#include "bgp/bgp_xmpp_channel.h"
#include "bgp/inet/inet_table.h"
#include "bgp/ermvpn/ermvpn_table.h"
#include "bgp/mvpn/mvpn_table.h"
#include "bgp/test/bgp_server_test_util.h"
#include "bgp/xmpp_message_builder.h"
#include "io/test/event_manager_test.h"
#include "control-node/control_node.h"
#include "control-node/test/network_agent_mock.h"
#include "sandesh/sandesh.h"
#include "sandesh/sandesh_server.h"
#include "schema/xmpp_multicast_types.h"
#include "schema/xmpp_mvpn_types.h"
#include "xmpp/xmpp_factory.h"
#include "xmpp/test/xmpp_test_util.h"

using namespace autogen;
using namespace std;
using namespace test;

class SandeshServerTest : public SandeshServer {
public:
    SandeshServerTest(EventManager *evm) :
            SandeshServer(evm, SandeshConfig()) { }
    virtual ~SandeshServerTest() { }
    virtual bool ReceiveSandeshMsg(SandeshSession *session,
                       const SandeshMessage *msg, bool rsc) {
        return true;
    }

private:
};

class BgpMvpnIntegrationTest : public ::testing::Test {
public:
    BgpMvpnIntegrationTest() : thread_(&evm_), xs_x_(NULL) { }

    virtual void SetUp() {
        SandeshStartup();
        bs_x_.reset(new BgpServerTest(&evm_, "X"));
        bs_x_->set_mvpn_ipv4_enable(true);
        xs_x_ = new XmppServer(&evm_, XmppDocumentMock::kControlNodeJID);
        bs_x_->session_manager()->Initialize(0);
        xs_x_->Initialize(0, false);
        bcm_x_.reset(new BgpXmppChannelManager(xs_x_, bs_x_.get()));

        sandesh_context_->bgp_server = bs_x_.get();
        sandesh_context_->xmpp_peer_manager = bcm_x_.get();
        thread_.Start();
    }

    virtual void TearDown() {
        xs_x_->Shutdown();
        task_util::WaitForIdle();
        bs_x_->Shutdown();
        task_util::WaitForIdle();
        bcm_x_.reset();
        TcpServerManager::DeleteServer(xs_x_);
        xs_x_ = NULL;

        agent_xa_->Delete();
        agent_xb_->Delete();
        agent_xc_->Delete();

        SandeshShutdown();
        task_util::WaitForIdle();
        sandesh_context_.reset();
        evm_.Shutdown();
        thread_.Join();
        task_util::WaitForIdle();
    }

    virtual void SessionUp() {
        agent_xa_.reset(new NetworkAgentMock(
            &evm_, "agent-xa", xs_x_->GetPort(), "127.0.0.1", "127.0.0.101"));
        TASK_UTIL_EXPECT_TRUE(agent_xa_->IsEstablished());
        agent_xb_.reset(new NetworkAgentMock(
            &evm_, "agent-xb", xs_x_->GetPort(), "127.0.0.2", "127.0.0.101"));
        TASK_UTIL_EXPECT_TRUE(agent_xb_->IsEstablished());
        agent_xc_.reset(new NetworkAgentMock(
            &evm_, "agent-xc", xs_x_->GetPort(), "127.0.0.3", "127.0.0.101"));
        TASK_UTIL_EXPECT_TRUE(agent_xc_->IsEstablished());
    }

    virtual void SessionDown() {
        agent_xa_->SessionDown();
        agent_xb_->SessionDown();
        agent_xc_->SessionDown();
        task_util::WaitForIdle();
    }

    virtual void Subscribe(const string net, int id) {
        agent_xa_->SubscribeAll(net, id);
        agent_xb_->SubscribeAll(net, id);
        agent_xc_->SubscribeAll(net, id);
        task_util::WaitForIdle();
    }

    virtual void Configure(const char *config_tmpl) {
        char config[8192];
        snprintf(config, sizeof(config), config_tmpl,
            bs_x_->session_manager()->GetPort());
        bs_x_->Configure(config);
    }

    int GetLabel(const NetworkAgentMock *agent,
        const string &net, const string &prefix) {
        const NetworkAgentMock::McastRouteEntry *rt =
            agent->McastRouteLookup(net, prefix);
        return (rt ? rt->entry.nlri.source_label : 0);
    }

    int CheckErmvpnOListSize(
            boost::shared_ptr<const NetworkAgentMock> agent,
        const string &prefix) {
        const NetworkAgentMock::McastRouteEntry *ermvpn_rt =
            agent->McastRouteLookup(BgpConfigManager::kFabricInstance, prefix);
        if (ermvpn_rt == NULL)
            return false;
        const OlistType &olist = ermvpn_rt->entry.olist;
        return olist.next_hop.size();
    }

    bool CheckOListElem(boost::shared_ptr<const NetworkAgentMock> agent,
        const string &net, const string &prefix, size_t olist_size,
        const string &address, const string &encap,
        const string &source_address, const NetworkAgentMock *other_agent) {

        const NetworkAgentMock::MvpnRouteEntry *mvpn_rt =
            agent->MvpnRouteLookup(net, prefix);
        if (mvpn_rt == NULL)
            return false;

        const NetworkAgentMock::McastRouteEntry *ermvpn_rt =
            other_agent->McastRouteLookup(
                BgpConfigManager::kFabricInstance, prefix);
        if (ermvpn_rt == NULL)
            return false;

        if (!(ermvpn_rt->entry.nlri.source_address == source_address))
            return false;
        int label = GetLabel(other_agent, BgpConfigManager::kFabricInstance,
                             prefix);
        if (label == 0)
            return false;

        vector<string> tunnel_encapsulation;
        if (encap == "all") {
            tunnel_encapsulation.push_back("gre");
            tunnel_encapsulation.push_back("udp");
        } else if (!encap.empty()) {
            tunnel_encapsulation.push_back(encap);
        }
        sort(tunnel_encapsulation.begin(), tunnel_encapsulation.end());

        const MvpnOlistType &olist = mvpn_rt->entry.olist;
        if (olist.next_hop.size() != olist_size)
            return false;
        for (MvpnOlistType::const_iterator it = olist.begin();
            it != olist.end(); ++it) {
            if (it->address == address) {
                if (it->tunnel_encapsulation_list.tunnel_encapsulation !=
                    tunnel_encapsulation)
                    return false;
                if (it->label != label)
                    return false;
                return true;
            }
        }

        return false;
    }

    void VerifyOListAndSource(boost::shared_ptr<const NetworkAgentMock> agent,
        const string &net, const string &prefix,
        size_t olist_size, const string &address, const string source_address,
        boost::shared_ptr<NetworkAgentMock> other_agent,
        const string &encap = "") {
        TASK_UTIL_EXPECT_TRUE(
            CheckOListElem(agent, net, prefix, olist_size, address, encap,
                source_address, other_agent.get()));
    }

    void SandeshStartup();
    void SandeshShutdown();

    EventManager evm_;
    ServerThread thread_;
    BgpServerTestPtr bs_x_;
    XmppServer *xs_x_;
    boost::scoped_ptr<BgpXmppChannelManager> bcm_x_;
    boost::shared_ptr<NetworkAgentMock> agent_xa_;
    boost::shared_ptr<NetworkAgentMock> agent_xb_;
    boost::shared_ptr<NetworkAgentMock> agent_xc_;
    BgpTable *master_;
    ErmVpnTable *fabric_ermvpn_;
    MvpnTable *red_;
    MvpnTable *blue_;
    MvpnTable *green_;
    MvpnTable *fabric_mvpn_;
    InetTable *red_inet_;
    InetTable *green_inet_;
    SandeshServerTest *sandesh_server_;
    boost::scoped_ptr<BgpSandeshContext> sandesh_context_;

    static int validate_done_;
};

void BgpMvpnIntegrationTest::SandeshStartup() {
    sandesh_context_.reset(new BgpSandeshContext());
    // Initialize SandeshServer.
    sandesh_server_ = new SandeshServerTest(&evm_);
    sandesh_server_->Initialize(0);

    boost::system::error_code error;
    string hostname(boost::asio::ip::host_name(error));
    Sandesh::InitGenerator("BgpUnitTestSandeshClient", hostname,
                           "BgpTest", "Test", &evm_,
                            0, sandesh_context_.get());
    Sandesh::ConnectToCollector("127.0.0.1",
                                sandesh_server_->GetPort());
    task_util::WaitForIdle();
    cout << "Introspect at http://localhost:" << Sandesh::http_port() << endl;
}

void BgpMvpnIntegrationTest::SandeshShutdown() {
    Sandesh::Uninit();
    task_util::WaitForIdle();
    sandesh_server_->Shutdown();
    task_util::WaitForIdle();
    TcpServerManager::DeleteServer(sandesh_server_);
    sandesh_server_ = NULL;
    task_util::WaitForIdle();
}

int BgpMvpnIntegrationTest::validate_done_;

static const char *config_tmpl1 = "\
<config>\
    <bgp-router name=\'X\'>\
        <identifier>192.168.0.101</identifier>\
        <address>127.0.0.101</address>\
        <port>%d</port>\
    </bgp-router>\
    <virtual-network name='blue'>\
        <network-id>3</network-id>\
    </virtual-network>\
    <virtual-network name='green'>\
        <network-id>2</network-id>\
    </virtual-network>\
    <virtual-network name='red'>\
        <network-id>1</network-id>\
    </virtual-network>\
    <virtual-network name='default-domain:default-project:ip-fabric'>\
        <network-id>1000</network-id>\
    </virtual-network>\
    <routing-instance name='default-domain:default-project:ip-fabric:ip-fabric'>\
        <vrf-target>target:127.0.0.1:1005</vrf-target>\
    </routing-instance>\
    <routing-instance name='red'>\
        <vrf-target>target:127.0.0.1:1001</vrf-target>\
        <vrf-target>\
            target:127.0.0.1:1003\
            <import-export>import</import-export>\
        </vrf-target>\
    </routing-instance>\
    <routing-instance name='blue'>\
        <virtual-network>blue</virtual-network>\
        <vrf-target>target:127.0.0.1:1002</vrf-target>\
    </routing-instance>\
    <routing-instance name='green'>\
        <virtual-network>green</virtual-network>\
        <vrf-target>target:127.0.0.1:1003</vrf-target>\
        <vrf-target>\
            target:127.0.0.1:1001\
            <import-export>import</import-export>\
        </vrf-target>\
        <vrf-target>\
            target:127.0.0.1:1002\
            <import-export>import</import-export>\
        </vrf-target>\
    </routing-instance>\
</config>\
";


class BgpMvpnOneControllerTest : public BgpMvpnIntegrationTest {
public:
    static const int kTimeoutSeconds = 15;
    virtual void SetUp() {
        BgpMvpnIntegrationTest::SetUp();

        Configure(config_tmpl1);
        task_util::WaitForIdle();
        master_ = static_cast<BgpTable *>(
            bs_x_->database()->FindTable("bgp.mvpn.0"));
        red_ = static_cast<MvpnTable *>(
            bs_x_->database()->FindTable("red.mvpn.0"));
        blue_ = static_cast<MvpnTable *>(
            bs_x_->database()->FindTable("blue.mvpn.0"));
        green_ = static_cast<MvpnTable *>(
            bs_x_->database()->FindTable("green.mvpn.0"));
        fabric_mvpn_ = static_cast<MvpnTable *>(
            bs_x_->database()->FindTable(
                string(BgpConfigManager::kFabricInstance) + ".mvpn.0"));
        fabric_ermvpn_ = static_cast<ErmVpnTable *>(
            bs_x_->database()->FindTable(
                string(BgpConfigManager::kFabricInstance) + ".ermvpn.0"));
        red_inet_ = static_cast<InetTable *>(
            bs_x_->database()->FindTable("red.inet.0"));
        green_inet_ = static_cast<InetTable *>(
            bs_x_->database()->FindTable("green.inet.0"));

        BgpMvpnIntegrationTest::SessionUp();
    }

    virtual void TearDown() {
        BgpMvpnIntegrationTest::SessionDown();
        BgpMvpnIntegrationTest::TearDown();
    }

};

TEST_F(BgpMvpnOneControllerTest, Basic) {
    TASK_UTIL_EXPECT_EQ(2, red_->Size()); // 1 type1 from red and 1 from green
    TASK_UTIL_EXPECT_NE(static_cast<MvpnRoute *>(NULL),
                        red_->FindType1ADRoute());

    TASK_UTIL_EXPECT_EQ(1, blue_->Size());
    TASK_UTIL_EXPECT_NE(static_cast<MvpnRoute *>(NULL),
                        blue_->FindType1ADRoute());

    TASK_UTIL_EXPECT_EQ(3, green_->Size()); // 1 type1 from red, blue and green

    TASK_UTIL_EXPECT_NE(static_cast<MvpnRoute *>(NULL),
                        green_->FindType1ADRoute());

    TASK_UTIL_EXPECT_EQ(1, fabric_mvpn_->Size());
    TASK_UTIL_EXPECT_NE(static_cast<MvpnRoute *>(NULL),
                        fabric_mvpn_->FindType1ADRoute());

    // red, blue, green, BgpConfigManager::kFabricInstance
    TASK_UTIL_EXPECT_EQ(4, master_->Size());

    const char *mroute = "224.1.2.3,192.168.1.1";

    // Register agent a and add a source active mvpn route
    Subscribe("red", 1);
    Subscribe("green", 2);
    Subscribe(BgpConfigManager::kFabricInstance, 1000);
    string tunnel;
    RouteAttributes attr;
    NextHop nexthop_red("10.1.1.2", 11, tunnel, "red");
    agent_xa_->AddRoute("red", "192.168.1.1/32", nexthop_red, attr);
    task_util::WaitForIdle();
    agent_xa_->AddType5MvpnRoute("red", mroute, "10.1.1.2");
    task_util::WaitForIdle();

    // Verify that the route gets added
    TASK_UTIL_EXPECT_EQ(3, red_->Size());
    TASK_UTIL_EXPECT_EQ(1, blue_->Size());
    TASK_UTIL_EXPECT_EQ(5, master_->Size());
    TASK_UTIL_EXPECT_EQ(4, green_->Size());
    TASK_UTIL_EXPECT_EQ(1, red_inet_->Size());
    TASK_UTIL_EXPECT_EQ(1, green_inet_->Size());

    agent_xb_->AddType7MvpnRoute("green", mroute, "10.1.1.2", "30-40");
    TASK_UTIL_EXPECT_EQ(3, fabric_ermvpn_->Size());

    TASK_UTIL_EXPECT_EQ(7, green_->Size());
    TASK_UTIL_EXPECT_EQ(1, blue_->Size());
    TASK_UTIL_EXPECT_EQ(8, master_->Size());
    TASK_UTIL_EXPECT_EQ(6, red_->Size());
    // Verify that sender should have received a route
    TASK_UTIL_EXPECT_EQ(0, agent_xa_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_xb_->McastRouteCount()); // Receiver
    TASK_UTIL_EXPECT_EQ(1, agent_xa_->MvpnRouteCount()); // Sender
    TASK_UTIL_EXPECT_EQ(0, agent_xb_->MvpnRouteCount());
    VerifyOListAndSource(agent_xa_, "red", mroute, 1, "10.1.1.2",
            "192.168.0.101", agent_xb_);
    TASK_UTIL_EXPECT_EQ(0, CheckErmvpnOListSize(agent_xb_, mroute));

    // Add a receiver in red
    agent_xa_->AddMcastRoute(BgpConfigManager::kFabricInstance, mroute,
                             "10.1.1.3", "50-60");
    // ermvpn route olist size will increase
    TASK_UTIL_EXPECT_EQ(1, CheckErmvpnOListSize(agent_xb_, mroute));
    // No change in mvpn route olist
    VerifyOListAndSource(agent_xa_, "red", mroute, 1, "10.1.1.2",
                         "192.168.0.101", agent_xb_);
}

static const char *config_tmpl3 = "\
<config>\
    <routing-instance name='red'>\
        <virtual-network>red</virtual-network>\
        <vrf-target>target:127.0.0.1:1001</vrf-target>\
        <vrf-target>\
            target:127.0.0.1:1003\
            <import-export>import</import-export>\
        </vrf-target>\
    </routing-instance>\
    <routing-instance name='green'>\
        <virtual-network>green</virtual-network>\
        <vrf-target>target:127.0.0.1:1003</vrf-target>\
        <vrf-target>\
            target:127.0.0.1:1001\
            <import-export>import</import-export>\
        </vrf-target>\
        <vrf-target>\
            target:127.0.0.1:1002\
            <import-export>import</import-export>\
        </vrf-target>\
    </routing-instance>\
</config>\
";

static const char *config_tmpl2 = "\
<config>\
    <bgp-router name=\'X\'>\
        <identifier>192.168.0.101</identifier>\
        <address>127.0.0.101</address>\
        <port>%d</port>\
        <address-families>\
            <family>inet</family>\
            <family>inet-mvpn</family>\
            <family>inet-vpn</family>\
            <family>erm-vpn</family>\
            <family>route-target</family>\
        </address-families>\
        <session to=\'Y\'>\
            <address-families>\
                <family>inet</family>\
                <family>inet-vpn</family>\
                <family>inet-mvpn</family>\
                <family>erm-vpn</family>\
                <family>route-target</family>\
            </address-families>\
        </session>\
    </bgp-router>\
    <bgp-router name=\'Y\'>\
        <identifier>192.168.0.102</identifier>\
        <address>127.0.0.102</address>\
        <port>%d</port>\
        <address-families>\
            <family>inet</family>\
            <family>inet-mvpn</family>\
            <family>inet-vpn</family>\
            <family>erm-vpn</family>\
            <family>route-target</family>\
        </address-families>\
        <session to=\'X\'>\
            <address-families>\
                <family>inet</family>\
                <family>inet-vpn</family>\
                <family>inet-mvpn</family>\
                <family>erm-vpn</family>\
                <family>route-target</family>\
            </address-families>\
        </session>\
    </bgp-router>\
    <virtual-network name='blue'>\
        <network-id>3</network-id>\
    </virtual-network>\
    <virtual-network name='green'>\
        <network-id>2</network-id>\
    </virtual-network>\
    <virtual-network name='red'>\
        <network-id>1</network-id>\
    </virtual-network>\
    <virtual-network name='default-domain:default-project:ip-fabric'>\
        <network-id>1000</network-id>\
    </virtual-network>\
    <routing-instance name='default-domain:default-project:ip-fabric:ip-fabric'>\
        <vrf-target>target:127.0.0.1:1005</vrf-target>\
    </routing-instance>\
    <routing-instance name='red'>\
        <virtual-network>red</virtual-network>\
        <vrf-target>target:127.0.0.1:1001</vrf-target>\
    </routing-instance>\
    <routing-instance name='blue'>\
        <virtual-network>blue</virtual-network>\
        <vrf-target>target:127.0.0.1:1002</vrf-target>\
    </routing-instance>\
    <routing-instance name='green'>\
        <virtual-network>green</virtual-network>\
        <vrf-target>target:127.0.0.1:1003</vrf-target>\
    </routing-instance>\
</config>\
";
class BgpMvpnTwoControllerTest : public BgpMvpnIntegrationTest {
protected:
    virtual void Configure(const char *config_tmpl) {
        char config[8192];
        snprintf(config, sizeof(config), config_tmpl,
            bs_x_->session_manager()->GetPort(),
            bs_y_->session_manager()->GetPort());
        bs_x_->Configure(config);
        bs_y_->Configure(config);
        task_util::WaitForIdle();
    }

    virtual void SessionUp() {
        BgpMvpnIntegrationTest::SessionUp();

        agent_ya_.reset(new NetworkAgentMock(
            &evm_, "agent-ya", xs_y_->GetPort(), "127.0.0.4", "127.0.0.102"));
        TASK_UTIL_EXPECT_TRUE(agent_ya_->IsEstablished());
        agent_yb_.reset(new NetworkAgentMock(
            &evm_, "agent-yb", xs_y_->GetPort(), "127.0.0.5", "127.0.0.102"));
        TASK_UTIL_EXPECT_TRUE(agent_yb_->IsEstablished());
        agent_yc_.reset(new NetworkAgentMock(
            &evm_, "agent-yc", xs_y_->GetPort(), "127.0.0.6", "127.0.0.102"));
        TASK_UTIL_EXPECT_TRUE(agent_yc_->IsEstablished());
    }

    virtual void SetUp() {
        bs_y_.reset(new BgpServerTest(&evm_, "Y"));
        bs_y_->set_mvpn_ipv4_enable(true);
        xs_y_ = new XmppServer(&evm_, XmppDocumentMock::kControlNodeJID);
        bs_y_->session_manager()->Initialize(0);
        xs_y_->Initialize(0, false);
        bcm_y_.reset(new BgpXmppChannelManager(xs_y_, bs_y_.get()));

        BgpMvpnIntegrationTest::SetUp();

        Configure(config_tmpl2);
        task_util::WaitForIdle();
        SessionUp();

        master_ = static_cast<BgpTable *>(
            bs_x_->database()->FindTable("bgp.mvpn.0"));
        red_ = static_cast<MvpnTable *>(
            bs_x_->database()->FindTable("red.mvpn.0"));
        blue_ = static_cast<MvpnTable *>(
            bs_x_->database()->FindTable("blue.mvpn.0"));
        green_ = static_cast<MvpnTable *>(
            bs_x_->database()->FindTable("green.mvpn.0"));
        fabric_ermvpn_ = static_cast<ErmVpnTable *>(
            bs_y_->database()->FindTable(
                string(BgpConfigManager::kFabricInstance) + ".ermvpn.0"));
        red_inet_ = static_cast<InetTable *>(
            bs_x_->database()->FindTable("red.inet.0"));
        green_inet_ = static_cast<InetTable *>(
            bs_x_->database()->FindTable("green.inet.0"));

        red_y_ = static_cast<MvpnTable *>(
            bs_y_->database()->FindTable("red.mvpn.0"));
        blue_y_ = static_cast<MvpnTable *>(
            bs_y_->database()->FindTable("blue.mvpn.0"));
        green_y_ = static_cast<MvpnTable *>(
            bs_y_->database()->FindTable("green.mvpn.0"));
        master_y_ = static_cast<BgpTable *>(
            bs_y_->database()->FindTable("bgp.mvpn.0"));

        string uuid = BgpConfigParser::session_uuid("X", "Y", 1);
        TASK_UTIL_EXPECT_NE(static_cast<BgpPeerTest *>(NULL),
            bs_x_->FindPeerByUuid(BgpConfigManager::kMasterInstance, uuid));
        peer_x_ = bs_x_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                        uuid);
        TASK_UTIL_EXPECT_NE(static_cast<BgpPeerTest *>(NULL),
            bs_y_->FindPeerByUuid(BgpConfigManager::kMasterInstance, uuid));
        peer_y_ = bs_y_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                        uuid);
        TASK_UTIL_EXPECT_TRUE(peer_x_->IsReady());
        TASK_UTIL_EXPECT_TRUE(peer_y_->IsReady());
    }

    virtual void SessionDown() {
        BgpMvpnIntegrationTest::SessionDown();

        agent_ya_->SessionDown();
        agent_yb_->SessionDown();
        agent_yc_->SessionDown();
        task_util::WaitForIdle();
    }

    virtual void TearDown() {
        SessionDown();
        xs_y_->Shutdown();
        task_util::WaitForIdle();
        bs_y_->Shutdown();
        task_util::WaitForIdle();
        bcm_y_.reset();
        TcpServerManager::DeleteServer(xs_y_);
        xs_y_ = NULL;

        agent_ya_->Delete();
        agent_yb_->Delete();
        agent_yc_->Delete();

        BgpMvpnIntegrationTest::TearDown();
    }

    virtual void Subscribe(const string net, int id) {
        BgpMvpnIntegrationTest::Subscribe(net, id);

        agent_ya_->SubscribeAll(net, id);
        agent_yb_->SubscribeAll(net, id);
        agent_yc_->SubscribeAll(net, id);
        task_util::WaitForIdle();
    }

    BgpServerTestPtr bs_y_;
    XmppServer *xs_y_;
    boost::scoped_ptr<BgpXmppChannelManager> bcm_y_;
    boost::shared_ptr<NetworkAgentMock> agent_ya_;
    boost::shared_ptr<NetworkAgentMock> agent_yb_;
    boost::shared_ptr<NetworkAgentMock> agent_yc_;
    BgpTable *master_y_;
    MvpnTable *red_y_;
    MvpnTable *blue_y_;
    MvpnTable *green_y_;
    BgpPeerTest *peer_x_;
    BgpPeerTest *peer_y_;

    static int validate_done_;
};

TEST_F(BgpMvpnTwoControllerTest, RedSenderGreenReceiver) {
    Configure(config_tmpl3);
    task_util::WaitForIdle();
    // Register agents and add a source active mvpn route
    Subscribe("red", 1);
    Subscribe("green", 2);
    Subscribe("blue", 3);
    Subscribe(BgpConfigManager::kFabricInstance, 1000);
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(4, red_->Size()); // 1 type1 each from A, B
    TASK_UTIL_EXPECT_EQ(4, red_y_->Size()); // 1 type1 each from A, B
    TASK_UTIL_EXPECT_NE(static_cast<MvpnRoute *>(NULL),
                        red_->FindType1ADRoute());
    TASK_UTIL_EXPECT_NE(static_cast<MvpnRoute *>(NULL),
                        red_y_->FindType1ADRoute());

    TASK_UTIL_EXPECT_EQ(2, blue_->Size()); // 1 type1 each from A, B
    TASK_UTIL_EXPECT_NE(static_cast<MvpnRoute *>(NULL),
                        blue_->FindType1ADRoute());

    TASK_UTIL_EXPECT_EQ(6, green_->Size()); // // 1 type1 each from A, B

    TASK_UTIL_EXPECT_NE(static_cast<MvpnRoute *>(NULL),
                        green_->FindType1ADRoute());
    TASK_UTIL_EXPECT_TRUE(peer_x_->IsReady());
    TASK_UTIL_EXPECT_TRUE(peer_y_->IsReady());
    // red, blue, green, BgpConfigManager::kFabricInstance from A, B
    TASK_UTIL_EXPECT_EQ(8, master_->Size());
    // red, blue, green, BgpConfigManager::kFabricInstance from A, B
    TASK_UTIL_EXPECT_EQ(8, master_y_->Size());

    string tunnel;
    RouteAttributes attr;
    NextHop nexthop_red("10.1.1.2", 11, tunnel, "red");
    agent_xa_->AddRoute("red", "192.168.1.1/32", nexthop_red, attr);
    task_util::WaitForIdle();

    const char *mroute = "224.1.2.3,192.168.1.1";
    agent_xa_->AddType5MvpnRoute("red", mroute, "10.1.1.2");

    // Verify that the type5 route gets added to red and master only
    TASK_UTIL_EXPECT_EQ(5, red_->Size());
    TASK_UTIL_EXPECT_EQ(2, blue_->Size());
    TASK_UTIL_EXPECT_EQ(9, master_->Size());
    TASK_UTIL_EXPECT_EQ(7, green_->Size());

    agent_yb_->AddType7MvpnRoute("green", mroute, "10.1.2.2", "30-40");
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(3, fabric_ermvpn_->Size());

    // verify that type7, type3, type4 primary routes get added to red, master
    TASK_UTIL_EXPECT_EQ(8, red_->Size());
    TASK_UTIL_EXPECT_EQ(8, green_->Size());
    TASK_UTIL_EXPECT_EQ(12, master_y_->Size());
    TASK_UTIL_EXPECT_EQ(12, master_->Size());
    TASK_UTIL_EXPECT_EQ(2, blue_->Size());
    // Verify that sender agent should have received a mvpn route
    TASK_UTIL_EXPECT_EQ(0, agent_xa_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_yb_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_xa_->MvpnRouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_yb_->MvpnRouteCount());
    VerifyOListAndSource(agent_xa_, "red", mroute, 1, "10.1.2.2",
            "192.168.0.101", agent_yb_);
    TASK_UTIL_EXPECT_EQ(0, CheckErmvpnOListSize(agent_yb_, mroute));
}

TEST_F(BgpMvpnTwoControllerTest, RedSenderRedGreenReceiver) {
    // Register agents and add a source active mvpn route
    Subscribe("red", 1);
    Subscribe("green", 2);
    Subscribe("blue", 3);
    Subscribe(BgpConfigManager::kFabricInstance, 1000);
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(2, red_->Size()); // 1 type1 each from A, B
    TASK_UTIL_EXPECT_EQ(2, red_y_->Size()); // 1 type1 each from A, B
    TASK_UTIL_EXPECT_NE(static_cast<MvpnRoute *>(NULL),
                        red_->FindType1ADRoute());
    TASK_UTIL_EXPECT_NE(static_cast<MvpnRoute *>(NULL),
                        red_y_->FindType1ADRoute());

    TASK_UTIL_EXPECT_EQ(2, blue_->Size()); // 1 type1 each from A, B
    TASK_UTIL_EXPECT_NE(static_cast<MvpnRoute *>(NULL),
                        blue_->FindType1ADRoute());

    TASK_UTIL_EXPECT_EQ(2, green_->Size()); // // 1 type1 each from A, B

    TASK_UTIL_EXPECT_NE(static_cast<MvpnRoute *>(NULL),
                        green_->FindType1ADRoute());
    TASK_UTIL_EXPECT_TRUE(peer_x_->IsReady());
    TASK_UTIL_EXPECT_TRUE(peer_y_->IsReady());
    // red, blue, green, BgpConfigManager::kFabricInstance from A, B
    TASK_UTIL_EXPECT_EQ(8, master_->Size());
    // red, blue, green, BgpConfigManager::kFabricInstance from A, B
    TASK_UTIL_EXPECT_EQ(8, master_y_->Size());

    string tunnel;
    RouteAttributes attr;
    NextHop nexthop_red("10.1.1.2", 11, tunnel, "red");
    agent_xa_->AddRoute("red", "192.168.1.1/32", nexthop_red, attr);
    task_util::WaitForIdle();

    const char *mroute = "224.1.2.3,192.168.1.1";
    agent_xa_->AddType5MvpnRoute("red", mroute, "10.1.1.2");

    // Verify that the type5 route gets added to red and master only
    TASK_UTIL_EXPECT_EQ(3, red_->Size());
    TASK_UTIL_EXPECT_EQ(2, blue_->Size());
    TASK_UTIL_EXPECT_EQ(9, master_->Size());
    TASK_UTIL_EXPECT_EQ(2, green_->Size());

    agent_yb_->AddType7MvpnRoute("red", mroute, "10.1.2.2", "30-40");
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(3, fabric_ermvpn_->Size());

    // verify that type7, type3, type4 primary routes get added to red, master
    TASK_UTIL_EXPECT_EQ(6, red_->Size());
    TASK_UTIL_EXPECT_EQ(12, master_y_->Size());
    TASK_UTIL_EXPECT_EQ(12, master_->Size());
    TASK_UTIL_EXPECT_EQ(2, blue_->Size());
    // Verify that sender agent should have received a mvpn route
    TASK_UTIL_EXPECT_EQ(0, agent_xa_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_yb_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_xa_->MvpnRouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_yb_->MvpnRouteCount());
    VerifyOListAndSource(agent_xa_, "red", mroute, 1, "10.1.2.2",
            "192.168.0.101", agent_yb_);
    TASK_UTIL_EXPECT_EQ(0, CheckErmvpnOListSize(agent_yb_, mroute));

    Configure(config_tmpl3);
    task_util::WaitForIdle();
    agent_yb_->AddType7MvpnRoute("green", mroute, "10.1.1.3", "50-60");
    TASK_UTIL_EXPECT_EQ(8, red_->Size());
    TASK_UTIL_EXPECT_EQ(2, blue_->Size());
    TASK_UTIL_EXPECT_EQ(8, green_->Size());
    TASK_UTIL_EXPECT_EQ(12, master_y_->Size());
    TASK_UTIL_EXPECT_EQ(12, master_->Size());
    VerifyOListAndSource(agent_xa_, "red", mroute, 1, "10.1.2.2",
            "192.168.0.101", agent_yb_);
}

TEST_F(BgpMvpnTwoControllerTest, Type5AfterType7) {
    Configure(config_tmpl3);
    task_util::WaitForIdle();
    // Register agents and add a source active mvpn route
    Subscribe("red", 1);
    Subscribe("green", 2);
    Subscribe("blue", 3);
    Subscribe(BgpConfigManager::kFabricInstance, 1000);
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(4, red_->Size()); // 1 type1 each from A, B
    TASK_UTIL_EXPECT_EQ(4, red_y_->Size()); // 1 type1 each from A, B
    TASK_UTIL_EXPECT_NE(static_cast<MvpnRoute *>(NULL),
                        red_->FindType1ADRoute());
    TASK_UTIL_EXPECT_NE(static_cast<MvpnRoute *>(NULL),
                        red_y_->FindType1ADRoute());

    TASK_UTIL_EXPECT_EQ(2, blue_->Size()); // 1 type1 each from A, B
    TASK_UTIL_EXPECT_NE(static_cast<MvpnRoute *>(NULL),
                        blue_->FindType1ADRoute());

    TASK_UTIL_EXPECT_EQ(6, green_->Size()); // // 1 type1 each from A, B

    TASK_UTIL_EXPECT_NE(static_cast<MvpnRoute *>(NULL),
                        green_->FindType1ADRoute());
    TASK_UTIL_EXPECT_TRUE(peer_x_->IsReady());
    TASK_UTIL_EXPECT_TRUE(peer_y_->IsReady());
    // red, blue, green, BgpConfigManager::kFabricInstance from A, B
    TASK_UTIL_EXPECT_EQ(8, master_->Size());
    TASK_UTIL_EXPECT_EQ(8, master_y_->Size());

    const char *mroute = "224.1.2.3,192.168.1.1";
    agent_yb_->AddType7MvpnRoute("green", mroute, "10.1.2.2", "30-40");
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(3, fabric_ermvpn_->Size());

    // verify that nothing changes since source is not resolvable
    // Only type7 route should get added to green_y_
    TASK_UTIL_EXPECT_EQ(6, green_->Size());
    TASK_UTIL_EXPECT_EQ(7, green_y_->Size());
    TASK_UTIL_EXPECT_EQ(8, master_y_->Size());

    string tunnel;
    RouteAttributes attr;
    NextHop nexthop_red("10.1.1.2", 11, tunnel, "red");
    agent_xa_->AddRoute("red", "192.168.1.1/32", nexthop_red, attr);
    task_util::WaitForIdle();

    // Verify that type7 route gets resolved and copied to red_ of sender
    // Howver, type3 route does not get generated since no type5 route
    TASK_UTIL_EXPECT_EQ(9, master_y_->Size());
    TASK_UTIL_EXPECT_EQ(9, master_->Size());
    TASK_UTIL_EXPECT_EQ(5, red_->Size());

    agent_xa_->AddType5MvpnRoute("red", mroute, "10.1.1.2");
    // verify that type5, type3, type4 primary routes get added to red, master
    TASK_UTIL_EXPECT_EQ(8, red_->Size());
    TASK_UTIL_EXPECT_EQ(8, green_->Size());
    TASK_UTIL_EXPECT_EQ(12, master_y_->Size());
    TASK_UTIL_EXPECT_EQ(12, master_->Size());
    TASK_UTIL_EXPECT_EQ(2, blue_->Size());
    // Verify that sender agent should have received a mvpn route
    TASK_UTIL_EXPECT_EQ(0, agent_xa_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_yb_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_xa_->MvpnRouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_yb_->MvpnRouteCount());
    VerifyOListAndSource(agent_xa_, "red", mroute, 1, "10.1.2.2",
            "192.168.0.101", agent_yb_);
    TASK_UTIL_EXPECT_EQ(0, CheckErmvpnOListSize(agent_yb_, mroute));
}

TEST_F(BgpMvpnTwoControllerTest, MvpnWithoutErmvpnRoute) {
    // Register agents
    Subscribe("blue", 3);
    Subscribe("red", 1);
    Subscribe("green", 2);
    task_util::WaitForIdle();
    Subscribe(BgpConfigManager::kFabricInstance, 1000);

    const char *mroute = "224.1.2.3,192.168.1.1";
    string tunnel;
    RouteAttributes attr;
    NextHop nexthop_red("10.1.1.2", 11, tunnel, "red");
    agent_xa_->AddRoute("red", "192.168.1.1/32", nexthop_red, attr);
    agent_xa_->AddType5MvpnRoute("red", mroute, "10.1.1.2");
    agent_yb_->AddType7MvpnRoute("red", mroute, "10.1.2.2", "30-40");
    task_util::WaitForIdle();

    // Verify that tables get all mvpn routes
    TASK_UTIL_EXPECT_EQ(6, red_->Size());
    TASK_UTIL_EXPECT_EQ(2, green_->Size());
    TASK_UTIL_EXPECT_EQ(12, master_y_->Size());

    // Delete the ermvpn route
    agent_yb_->DeleteMcastRoute(BgpConfigManager::kFabricInstance, mroute);
    task_util::WaitForIdle();
    // Verify that type4 route gets deleted
    TASK_UTIL_EXPECT_EQ(0, fabric_ermvpn_->Size());
    TASK_UTIL_EXPECT_EQ(5, red_->Size());
    TASK_UTIL_EXPECT_EQ(11, master_y_->Size());
    TASK_UTIL_EXPECT_EQ(11, master_->Size());
    // Verify that type5 route is withdrawn since there are no receivers
    TASK_UTIL_EXPECT_EQ(static_cast<NetworkAgentMock::MvpnRouteEntry *>(NULL),
            agent_xa_->MvpnRouteLookup("red", mroute));

    // Add the ermvpn receiver back
    agent_yb_->AddMcastRoute(BgpConfigManager::kFabricInstance, mroute,
                             "10.1.2.2", "30-40", "");
    task_util::WaitForIdle();

    // Verify that type4 route gets added back
    TASK_UTIL_EXPECT_EQ(6, red_->Size());
    TASK_UTIL_EXPECT_EQ(12, master_y_->Size());
    // Verify that mvpn and ermvpn routes are ok
    VerifyOListAndSource(agent_xa_, "red", mroute, 1, "10.1.2.2",
            "192.168.0.101", agent_yb_);
}

static const char *change_identifier= "\
<config>\
    <bgp-router name=\'X\'>\
        <identifier>192.168.0.201</identifier>\
        <address>127.0.0.101</address>\
        <port>%d</port>\
        <address-families>\
            <family>inet</family>\
            <family>inet-mvpn</family>\
            <family>inet-vpn</family>\
            <family>erm-vpn</family>\
            <family>route-target</family>\
        </address-families>\
        <session to=\'Y\'>\
            <address-families>\
                <family>inet</family>\
                <family>inet-vpn</family>\
                <family>inet-mvpn</family>\
                <family>erm-vpn</family>\
                <family>route-target</family>\
            </address-families>\
        </session>\
    </bgp-router>\
    <bgp-router name=\'Y\'>\
        <identifier>192.168.0.202</identifier>\
        <address>127.0.0.102</address>\
        <port>%d</port>\
        <address-families>\
            <family>inet</family>\
            <family>inet-mvpn</family>\
            <family>inet-vpn</family>\
            <family>erm-vpn</family>\
            <family>route-target</family>\
        </address-families>\
        <session to=\'X\'>\
            <address-families>\
                <family>inet</family>\
                <family>inet-vpn</family>\
                <family>inet-mvpn</family>\
                <family>erm-vpn</family>\
                <family>route-target</family>\
            </address-families>\
        </session>\
    </bgp-router>\
</config>\
";

TEST_F(BgpMvpnTwoControllerTest, ReceiverSenderLeave) {
    Configure(config_tmpl3);
    task_util::WaitForIdle();
    // Register agents
    Subscribe("blue", 3);
    Subscribe("red", 1);
    Subscribe("green", 2);
    task_util::WaitForIdle();
    Subscribe(BgpConfigManager::kFabricInstance, 1000);

    const char *mroute = "224.1.2.3,192.168.1.1";
    string tunnel;
    RouteAttributes attr;
    NextHop nexthop_red("10.1.1.2", 11, tunnel, "red");
    agent_xa_->AddRoute("red", "192.168.1.1/32", nexthop_red, attr);
    agent_xa_->AddType5MvpnRoute("red", mroute, "10.1.1.2");
    agent_yb_->AddType7MvpnRoute("green", mroute, "10.1.2.2", "30-40");
    task_util::WaitForIdle();

    // Verify that tables get all mvpn routes
    TASK_UTIL_EXPECT_EQ(8, red_->Size());
    TASK_UTIL_EXPECT_EQ(8, green_->Size());
    TASK_UTIL_EXPECT_EQ(12, master_y_->Size());

    // Delete the type7 join route
    agent_yb_->DeleteMvpnRoute("green", mroute, 7);
    task_util::WaitForIdle();
    // Verify that type7, type3 and type4 routes get deleted
    TASK_UTIL_EXPECT_EQ(5, red_->Size());
    TASK_UTIL_EXPECT_EQ(9, master_->Size());
    // Verify that type5 route is withdrawn since there are no receivers
    TASK_UTIL_EXPECT_EQ(static_cast<NetworkAgentMock::MvpnRouteEntry *>(NULL),
            agent_xa_->MvpnRouteLookup("red", mroute));

    // Add the receiver back
    agent_yb_->AddType7MvpnRoute("green", mroute, "10.1.2.2", "30-40");
    task_util::WaitForIdle();

    // Verify that type7, type3 and type4 routes get added back
    TASK_UTIL_EXPECT_EQ(8, red_->Size());
    TASK_UTIL_EXPECT_EQ(12, master_->Size());
    // Verify that mvpn and ermvpn routes are ok
    VerifyOListAndSource(agent_xa_, "red", mroute, 1, "10.1.2.2",
            "192.168.0.101", agent_yb_);

    // Delete the type5 source active route
    agent_xa_->DeleteMvpnRoute("red", mroute, 5);
    task_util::WaitForIdle();
    // Verify that type5, type3 and type4 routes get deleted
    TASK_UTIL_EXPECT_EQ(5, red_->Size());
    TASK_UTIL_EXPECT_EQ(9, master_->Size());
    // Verify that type5 route is withdrawn since there are no receivers
    TASK_UTIL_EXPECT_EQ(static_cast<NetworkAgentMock::MvpnRouteEntry *>(NULL),
            agent_xa_->MvpnRouteLookup("red", mroute));

    // Add the sender back
    agent_xa_->AddType5MvpnRoute("red", mroute, "10.1.1.2");
    task_util::WaitForIdle();

    // Verify that type5, type3 and type4 routes get added back
    TASK_UTIL_EXPECT_EQ(8, red_->Size());
    TASK_UTIL_EXPECT_EQ(12, master_->Size());
    // Verify that mvpn and ermvpn routes are ok
    VerifyOListAndSource(agent_xa_, "red", mroute, 1, "10.1.2.2",
            "192.168.0.101", agent_yb_);
};

TEST_F(BgpMvpnTwoControllerTest, ChangeIdentifier) {
    // Register agents
    Subscribe("blue", 3);
    Subscribe("red", 1);
    Subscribe("green", 2);
    task_util::WaitForIdle();
    Subscribe(BgpConfigManager::kFabricInstance, 1000);

    // Verify that tables get all mvpn routes
    TASK_UTIL_EXPECT_EQ(2, red_->Size());
    TASK_UTIL_EXPECT_EQ(2, green_->Size());
    TASK_UTIL_EXPECT_EQ(8, master_y_->Size());

    // Change the identifiers of routers
    Configure(change_identifier);
    Subscribe("blue", 3);
    Subscribe("red", 1);
    Subscribe("green", 2);
    Subscribe(BgpConfigManager::kFabricInstance, 1000);
    task_util::WaitForIdle();

    // Verify that tables get all mvpn routes
    TASK_UTIL_EXPECT_EQ(2, red_->Size());
    TASK_UTIL_EXPECT_EQ(2, green_->Size());
    TASK_UTIL_EXPECT_EQ(8, master_y_->Size());
}

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
    SetUp();
    int result = RUN_ALL_TESTS();
    TearDown();

    return result;
}
