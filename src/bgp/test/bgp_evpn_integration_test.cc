/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/foreach.hpp>
#include <boost/format.hpp>

#include "bgp/bgp_config.h"
#include "bgp/bgp_config_parser.h"
#include "bgp/bgp_factory.h"
#include "bgp/bgp_multicast.h"
#include "bgp/bgp_evpn.h"
#include "bgp/bgp_sandesh.h"
#include "bgp/bgp_session_manager.h"
#include "bgp/bgp_xmpp_channel.h"
#include "bgp/inet/inet_table.h"
#include "bgp/ermvpn/ermvpn_table.h"
#include "bgp/evpn/evpn_table.h"
#include "bgp/test/bgp_server_test_util.h"
#include "bgp/xmpp_message_builder.h"
#include "io/test/event_manager_test.h"
#include "control-node/control_node.h"
#include "control-node/test/network_agent_mock.h"
#include "sandesh/sandesh.h"
#include "sandesh/sandesh_server.h"
#include "schema/xmpp_enet_types.h"
#include "schema/xmpp_multicast_types.h"
#include "xmpp/xmpp_factory.h"
#include "xmpp/test/xmpp_test_util.h"

using namespace autogen;
using namespace std;
using namespace test;
using boost::format;
// enable it for introspect of 2nd controller
#define SANDESH_Y false

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

class BgpEvpnIntegrationTest {
public:
    BgpEvpnIntegrationTest() :
        thread_(&evm_), xs_x_(NULL), red_(NULL), green_(NULL),
        red_inet_(NULL), green_inet_(NULL), red_ermvpn_(NULL) {
    }

    virtual void SetUp() {
        bs_x_.reset(new BgpServerTest(&evm_, "X"));
        xs_x_ = new XmppServer(&evm_, XmppDocumentMock::kControlNodeJID);
        bs_x_->session_manager()->Initialize(0);
        xs_x_->Initialize(0, false);
        bcm_x_.reset(new BgpXmppChannelManager(xs_x_, bs_x_.get()));

#if !SANDESH_Y
        SandeshStartup();
        sandesh_context_->bgp_server = bs_x_.get();
        sandesh_context_->xmpp_peer_manager = bcm_x_.get();
#endif
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

        if (red_)
            delete[] red_;
        if (green_)
            delete[] green_;
        if (red_inet_)
            delete[] red_inet_;
        if (green_inet_)
            delete[] green_inet_;
        if (red_ermvpn_)
            delete[] red_ermvpn_;

#if !SANDESH_Y
        SandeshShutdown();
        task_util::WaitForIdle();
        sandesh_context_.reset();
#endif
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
        for (size_t i = 1; i <= instance_count_; i++) {
            ostringstream r;
            r << net << i;
            agent_xa_->SubscribeAll(r.str(), id);
            agent_xb_->SubscribeAll(r.str(), id);
            agent_xc_->SubscribeAll(r.str(), id);
            task_util::WaitForIdle();
        }
    }

    string getRouteTarget (int i, string suffix) const {
        ostringstream os;
        os << "target:127.0.0.1:1" << format("%|03|")%i << suffix;
        return os.str();
    }

    const string GetConfig() const {
        ostringstream os;
            os <<
"<?xml version='1.0' encoding='utf-8'?>"
"<config>"
"   <bgp-router name=\"X\">"
"       <identifier>192.168.0.101</identifier>"
"       <address>127.0.0.101</address>"
"   </bgp-router>"
"";

        os <<
"  <virtual-network name='default-domain:default-project:ip-fabric:ip-fabric'>"
"      <network-id>101</network-id>"
"  </virtual-network>"
"  <routing-instance name='default-domain:default-project:ip-fabric:ip-fabric'>"
"       <virtual-network>default-domain:default-project:ip-fabric:ip-fabric"
       "</virtual-network>"
"       <vrf-target>target:127.0.0.1:60000</vrf-target>"
"   </routing-instance>";

        for (size_t i = 1; i <= instance_count_; i++) {
            os <<
"  <virtual-network name='red" << i << "'>"
"      <network-id>" << 200+i << "</network-id>"
"  </virtual-network>"
"   <routing-instance name='red" << i << "'>"
"       <virtual-network>red" << i << "</virtual-network>"
"       <vrf-target>" << getRouteTarget(i, "1") << "</vrf-target>"
"       <vrf-target>"
"           <import-export>import</import-export>" << getRouteTarget(i, "3") <<
"       </vrf-target>"
"   </routing-instance>"
"  <virtual-network name='green" << i << "'>"
"      <network-id>" << 400+i << "</network-id>"
"  </virtual-network>"
"   <routing-instance name='green" << i << "'>"
"       <virtual-network>green" << i << "</virtual-network>"
"       <vrf-target>" << getRouteTarget(i, "3") << "</vrf-target>"
"       <vrf-target>"
"           <import-export>import</import-export>" << getRouteTarget(i, "1") <<
"       </vrf-target>"
"       <vrf-target>"
"           <import-export>import</import-export>" << getRouteTarget(i, "2") <<
"       </vrf-target>"
"   </routing-instance>"
            ;
        }

        os << "</config>";
        return os.str();
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
            const string &net, const string &prefix) {
        const NetworkAgentMock::McastRouteEntry *ermvpn_rt =
            agent->McastRouteLookup(net, prefix);
        if (ermvpn_rt == NULL)
            return false;
        const OlistType &olist = ermvpn_rt->entry.olist;
        return olist.next_hop.size();
    }

    bool CheckOListElem(boost::shared_ptr<const NetworkAgentMock> agent,
        const string &net, const string &prefix, size_t olist_size,
        const string &address) {

        const NetworkAgentMock::EnetRouteEntry *evpn_rt =
            agent->EnetRouteLookup(net, prefix);

        if (!evpn_rt)
            return false;
        const EnetOlistType &olist = evpn_rt->entry.olist;
        if (olist.next_hop.size() != olist_size)
            return false;
        BOOST_FOREACH(const EnetNextHopType &nh, olist.next_hop) {
            if (nh.address == address) {
                bool found_et = false;
                BOOST_FOREACH(const string &tet,
                        nh.tunnel_encapsulation_list.tunnel_encapsulation) {
                    if (tet == "vxlan")
                        found_et = true;
                }
                if (!found_et)
                    return false;
                return true;
            }
        }
        return false;
    }

    void VerifyOList(boost::shared_ptr<const NetworkAgentMock> agent,
        const string &net, const string &prefix,
        size_t olist_size, const string &address) {
        TASK_UTIL_EXPECT_TRUE(
            CheckOListElem(agent, net, prefix, olist_size, address));
    }

    void SandeshStartup();
    void SandeshShutdown();

    void TableInit() {
        master_ = static_cast<BgpTable *>(
            bs_x_->database()->FindTable("bgp.evpn.0"));
        for (size_t i = 1; i <= instance_count_; i++) {
            ostringstream r, g, ri, gi, re;
            r << "red" << i << ".evpn.0";
            g << "green" << i << ".evpn.0";
            ri << "red" << i << ".inet.0";
            gi << "green" << i << ".inet.0";
            re << "red" << i << ".ermvpn.0";
            TASK_UTIL_EXPECT_NE(static_cast<BgpTable *>(NULL),
                                bs_x_->database()->FindTable(r.str()));
            TASK_UTIL_EXPECT_NE(static_cast<BgpTable *>(NULL),
                                bs_x_->database()->FindTable(g.str()));

            red_[i-1] = static_cast<EvpnTable *>(
                bs_x_->database()->FindTable(r.str()));
            green_[i-1] = static_cast<EvpnTable *>(
                bs_x_->database()->FindTable(g.str()));
            red_inet_[i-1] = static_cast<InetTable *>(
                bs_x_->database()->FindTable(ri.str()));
            green_inet_[i-1] = static_cast<InetTable *>(
                bs_x_->database()->FindTable(gi.str()));
            red_ermvpn_[i-1] = static_cast<ErmVpnTable *>(
                bs_x_->database()->FindTable(re.str()));
        }
    }

    EventManager evm_;
    ServerThread thread_;
    BgpServerTestPtr bs_x_;
    XmppServer *xs_x_;
    boost::scoped_ptr<BgpXmppChannelManager> bcm_x_;
    boost::shared_ptr<NetworkAgentMock> agent_xa_;
    boost::shared_ptr<NetworkAgentMock> agent_xb_;
    boost::shared_ptr<NetworkAgentMock> agent_xc_;
    BgpTable *master_;
    EvpnTable **red_;
    EvpnTable **green_;
    InetTable **red_inet_;
    InetTable **green_inet_;
    ErmVpnTable **red_ermvpn_;
    SandeshServerTest *sandesh_bs_x_;
    boost::scoped_ptr<BgpSandeshContext> sandesh_context_;

    size_t instance_count_;
};

void BgpEvpnIntegrationTest::SandeshStartup() {
    sandesh_context_.reset(new BgpSandeshContext());
    // Initialize SandeshServer.
    sandesh_bs_x_ = new SandeshServerTest(&evm_);
    sandesh_bs_x_->Initialize(0);

    boost::system::error_code error;
    string hostname(boost::asio::ip::host_name(error));
    Sandesh::InitGenerator("BgpUnitTestSandeshClient", hostname,
                           "BgpTest", "Test", &evm_,
                            0, sandesh_context_.get());
    Sandesh::ConnectToCollector("127.0.0.1",
                                sandesh_bs_x_->GetPort());
    task_util::WaitForIdle();
    cout << "Introspect at http://localhost:" << Sandesh::http_port() << endl;
}

void BgpEvpnIntegrationTest::SandeshShutdown() {
    Sandesh::Uninit();
    task_util::WaitForIdle();
    sandesh_bs_x_->Shutdown();
    task_util::WaitForIdle();
    TcpServerManager::DeleteServer(sandesh_bs_x_);
    sandesh_bs_x_ = NULL;
    task_util::WaitForIdle();
}

class BgpEvpnOneControllerTest : public BgpEvpnIntegrationTest,
                                 public ::testing::TestWithParam<int> {
public:
    static const int kTimeoutSeconds = 15;
    virtual void SetUp() {
        BgpEvpnIntegrationTest::SetUp();

        instance_count_ = GetParam();
        red_ = new EvpnTable *[instance_count_];
        green_ = new EvpnTable *[instance_count_];
        red_inet_ = new InetTable *[instance_count_];
        green_inet_ = new InetTable *[instance_count_];
        red_ermvpn_ = new ErmVpnTable *[instance_count_];

        string config = GetConfig();
        Configure(config.c_str());
        task_util::WaitForIdle();

        BgpEvpnIntegrationTest::TableInit();
        BgpEvpnIntegrationTest::SessionUp();
    }

    virtual void TearDown() {
        BgpEvpnIntegrationTest::SessionDown();
        BgpEvpnIntegrationTest::TearDown();
    }

    string MulticastMac(int i, int j) {
        ostringstream sg;
        sg << "01:00:5e:01:02:03,";
        sg << "224." << i << "." << instance_count_ << ".3,192.168.1.1,10.10.10.1/32";
        return sg.str();
    }

    test::NextHop BuildNextHop(const string &nexthop) {
        return test::NextHop(nexthop, 1, "vxlan");
    }
};

TEST_P(BgpEvpnOneControllerTest, Basic) {
    // Register agents and add source active mvpn routes
    Subscribe("red", 1);
    Subscribe("green", 2);

    for (size_t i = 1; i <= instance_count_; i++) {
        ostringstream sg;
        sg << "224." << i << "." << instance_count_ << ".3,192.168.1.1";

        string tunnel;
        RouteAttributes attr;
        ostringstream red;
        red << "red" << i;
        NextHop nexthop_red("10.1.1.2", 11, "vxlan");
        agent_xa_->AddRoute(red.str(), "192.168.1.0/24", nexthop_red, attr);
        agent_xa_->AddMcastRoute(red.str(), sg.str(), "10.1.1.3", "10-20");
        task_util::WaitForIdle();
        agent_xa_->AddEnetRoute(red.str(), MulticastMac(i, instance_count_),
                                nexthop_red);
        task_util::WaitForIdle();

        // Verify that smet route gets added
        TASK_UTIL_EXPECT_EQ(1U, red_[i - 1]->Size());
        TASK_UTIL_EXPECT_EQ(0U, green_[i - 1]->Size());
        TASK_UTIL_EXPECT_EQ(i, master_->Size());

        agent_xb_->AddMcastRoute(red.str(), sg.str(), "10.1.1.5", "30-40");
        agent_xb_->AddEnetRoute(red.str(), MulticastMac(i, instance_count_),
                                nexthop_red);

        // another smet route should generate
        TASK_UTIL_EXPECT_EQ(1U, red_[i - 1]->Size());
        TASK_UTIL_EXPECT_EQ(i, master_->Size());
        //VerifyOList(agent_xa_, red.str(), sg.str(), 1, "10.1.1.3",
            //"192.168.0.101");
        TASK_UTIL_EXPECT_EQ((int)i, agent_xa_->McastRouteCount());
        TASK_UTIL_EXPECT_EQ(1, CheckErmvpnOListSize(agent_xa_, red.str(),
                                                    sg.str()));
#if 0
        // Verify that sender should have received a route
        TASK_UTIL_EXPECT_EQ((int)i, agent_xb_->McastRouteCount()); // Receiver
        TASK_UTIL_EXPECT_EQ((int)i, agent_xa_->EnetRouteCount()); // Sender
        TASK_UTIL_EXPECT_EQ((int)i, agent_xb_->EnetRouteCount());
        VerifyOList(agent_xa_, red.str(), sg.str(), 1, "10.1.1.3",
            "192.168.0.101", agent_xb_);

        // Add a receiver in red
        agent_xa_->AddMcastRoute(red.str(), sg.str(),
                             "10.1.1.3", "50-60");
        // ermvpn route olist size will increase
        TASK_UTIL_EXPECT_EQ(1, CheckErmvpnOListSize(agent_xb_, sg.str()));
        // No change in mvpn route olist
        VerifyOList(agent_xa_, red.str(), sg.str(), 1, "10.1.1.3",
                         "192.168.0.101", agent_xb_);
        agent_xa_->DeleteMcastRoute(red.str(), sg.str());
#endif
    }
}

class BgpEvpnTwoControllerTest : public BgpEvpnIntegrationTest,
                                 public ::testing::TestWithParam<int> {
protected:
    BgpEvpnTwoControllerTest() : BgpEvpnIntegrationTest(), red_y_(NULL),
        green_y_(NULL) {
    }

    virtual void Configure(const char *config_tmpl) {
        char config[8192];
        snprintf(config, sizeof(config), config_tmpl,
            bs_x_->session_manager()->GetPort(),
            bs_y_->session_manager()->GetPort());
        bs_x_->Configure(config);
        bs_y_->Configure(config);
        task_util::WaitForIdle();
    }

    virtual void ReConfigure(const char *config_tmpl) {
        bs_x_->Configure(config_tmpl);
        bs_y_->Configure(config_tmpl);
        task_util::WaitForIdle();
    }

    virtual void SessionUp() {
        BgpEvpnIntegrationTest::SessionUp();

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

    const string GetDeleteGreenConfig(size_t i) const {
        ostringstream os;
            os <<
"<?xml version='1.0' encoding='utf-8'?>"
"<delete>"
"   <routing-instance name='green" << i << "'>"
"       <vrf-target>" << getRouteTarget(i, "3") << "</vrf-target>"
"       <vrf-target>"
"           <import-export>import</import-export>" << getRouteTarget(i, "1") <<
"       </vrf-target>"
"       <vrf-target>"
"           <import-export>import</import-export>" << getRouteTarget(i, "2") <<
"       </vrf-target>"
"   </routing-instance>"
            ;
        os << "</delete>";
        return os.str();
    }

    const string GetIdentifierChangeConfig() const {
        ostringstream os;
            os <<
"<?xml version='1.0' encoding='utf-8'?>"
"<config>"
"   <bgp-router name=\"X\">"
"       <identifier>192.168.0.201</identifier>"
"        <port>%d</port>"
"   </bgp-router>"
"   <bgp-router name=\"Y\">"
"       <identifier>192.168.0.202</identifier>"
"        <port>%d</port>"
"   </bgp-router>"
            ;
        os << "</config>";
        return os.str();
    }

    const string GetConfig() const {
        ostringstream os;
            os <<
"<?xml version='1.0' encoding='utf-8'?>"
"<config>"
"   <bgp-router name=\"X\">"
"       <identifier>192.168.0.101</identifier>"
"       <address>127.0.0.101</address>"
"        <port>%d</port>"
"        <session to=\'Y\'>"
"            <address-families>"
"                <family>inet</family>"
"                <family>inet-vpn</family>"
"                <family>e-vpn</family>"
"                <family>erm-vpn</family>"
"                <family>route-target</family>"
"            </address-families>"
"        </session>"
"   </bgp-router>"
"   <bgp-router name=\"Y\">"
"       <identifier>192.168.0.102</identifier>"
"       <address>127.0.0.102</address>"
"        <port>%d</port>"
"        <session to=\'X\'>"
"            <address-families>"
"                <family>inet</family>"
"                <family>inet-vpn</family>"
"                <family>e-vpn</family>"
"                <family>erm-vpn</family>"
"                <family>route-target</family>"
"            </address-families>"
"        </session>"
"   </bgp-router>"
"";

        for (size_t i = 1; i <= instance_count_; i++) {
            os <<
"   <virtual-network name='red" << i << "'>"
"      <network-id>" << 200+i << "</network-id>"
"   </virtual-network>"
"   <routing-instance name='red" << i << "'>"
"       <virtual-network>red" << i << "</virtual-network>"
"       <vrf-target>" << getRouteTarget(i, "1") << "</vrf-target>"
"       <vrf-target>"
"           <import-export>import</import-export>" << getRouteTarget(i, "3") <<
"       </vrf-target>"
"   </routing-instance>"
"   <virtual-network name='green" << i << "'>"
"       <network-id>" << 400+i << "</network-id>"
"   </virtual-network>"
"   <routing-instance name='green" << i << "'>"
"       <virtual-network>green" << i << "</virtual-network>"
"       <vrf-target>" << getRouteTarget(i, "3") << "</vrf-target>"
"       <vrf-target>"
"           <import-export>import</import-export>" << getRouteTarget(i, "1") <<
"       </vrf-target>"
"       <vrf-target>"
"           <import-export>import</import-export>" << getRouteTarget(i, "2") <<
"       </vrf-target>"
"   </routing-instance>"
            ;
        }

        os << "</config>";
        return os.str();
    }

    void TableInit() {
        BgpEvpnIntegrationTest::TableInit();
        master_y_ = static_cast<BgpTable *>(
            bs_y_->database()->FindTable("bgp.evpn.0"));

        for (size_t i = 1; i <= instance_count_; i++) {
            ostringstream r, b, g, re;
            r << "red" << i << ".evpn.0";
            g << "green" << i << ".evpn.0";
            re << "red" << i << ".ermvpn.0";

            red_y_[i-1] = static_cast<EvpnTable *>(
                bs_y_->database()->FindTable(r.str()));
            green_y_[i-1] = static_cast<EvpnTable *>(
                bs_y_->database()->FindTable(g.str()));
            red_ermvpn_[i-1] = static_cast<ErmVpnTable *>(
                bs_y_->database()->FindTable(re.str()));
        }
    }

    void SandeshYStartup() {
        sandesh_context_y_.reset(new BgpSandeshContext());
        // Initialize SandeshServer.
        sandesh_bs_y_ = new SandeshServerTest(&evm_);
        sandesh_bs_y_->Initialize(0);

        boost::system::error_code error;
        string hostname(boost::asio::ip::host_name(error));
        Sandesh::InitGenerator("BgpUnitTestSandeshClient", hostname,
                               "BgpTest", "Test", &evm_,
                               0, sandesh_context_y_.get());
        Sandesh::ConnectToCollector("127.0.0.1",
                                    sandesh_bs_y_->GetPort());
        task_util::WaitForIdle();
        cout << "Introspect for Y at http://localhost:" << Sandesh::http_port()
             << endl;
    }

    void SandeshYShutdown() {
        Sandesh::Uninit();
        task_util::WaitForIdle();
        sandesh_bs_y_->Shutdown();
        task_util::WaitForIdle();
        TcpServerManager::DeleteServer(sandesh_bs_y_);
        sandesh_bs_y_ = NULL;
        task_util::WaitForIdle();
    }

    virtual void SetUp() {
        bs_y_.reset(new BgpServerTest(&evm_, "Y"));
        xs_y_ = new XmppServer(&evm_, XmppDocumentMock::kControlNodeJID);
        bs_y_->session_manager()->Initialize(0);
        xs_y_->Initialize(0, false);
        bcm_y_.reset(new BgpXmppChannelManager(xs_y_, bs_y_.get()));

        BgpEvpnIntegrationTest::SetUp();
#if SANDESH_Y
        SandeshYStartup();

        sandesh_context_y_->bgp_server = bs_y_.get();
        sandesh_context_y_->xmpp_peer_manager = bcm_y_.get();
#endif

        instance_count_ = GetParam();
        type1_routes_ = 2 + 6 * instance_count_;
        red_ = new EvpnTable *[instance_count_];
        green_ = new EvpnTable *[instance_count_];
        red_inet_ = new InetTable *[instance_count_];
        green_inet_ = new InetTable *[instance_count_];
        red_ermvpn_ = new ErmVpnTable *[instance_count_];
        red_y_ = new EvpnTable *[instance_count_];
        green_y_ = new EvpnTable *[instance_count_];

        string config = GetConfig();
        Configure(config.c_str());
        task_util::WaitForIdle();
        SessionUp();
        TableInit();

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
        // Register agents and add a source active mvpn route
        Subscribe("red", 1);
        Subscribe("green", 2);
        task_util::WaitForIdle();
    }

    virtual void SessionDown() {
        BgpEvpnIntegrationTest::SessionDown();

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

#if SANDESH_Y
        SandeshYShutdown();
        task_util::WaitForIdle();
        sandesh_context_y_.reset();
#endif
        BgpEvpnIntegrationTest::TearDown();
        if (red_y_)
            delete[] red_y_;
        if (green_y_)
            delete[] green_y_;
    }

    virtual void Subscribe(const string net, int id) {
        BgpEvpnIntegrationTest::Subscribe(net, id);

        for (size_t i = 1; i <= instance_count_; i++) {
            ostringstream r;
            r << net << i;
            agent_ya_->SubscribeAll(r.str(), id);
            agent_yb_->SubscribeAll(r.str(), id);
            agent_yc_->SubscribeAll(r.str(), id);
            task_util::WaitForIdle();
        }
    }

    string MulticastMac(int i, int j) {
        ostringstream sg;
        sg << "01:00:5e:01:02:03,";
        sg << "224." << i << "." << instance_count_ << ".3,192.168.1.1,10.10.10.1/32";
        return sg.str();
    }

    int type1_routes_;
    BgpServerTestPtr bs_y_;
    XmppServer *xs_y_;
    boost::scoped_ptr<BgpXmppChannelManager> bcm_y_;
    boost::shared_ptr<NetworkAgentMock> agent_ya_;
    boost::shared_ptr<NetworkAgentMock> agent_yb_;
    boost::shared_ptr<NetworkAgentMock> agent_yc_;
    BgpTable *master_y_;
    EvpnTable **red_y_;
    EvpnTable **green_y_;
    BgpPeerTest *peer_x_;
    BgpPeerTest *peer_y_;
    SandeshServerTest *sandesh_bs_y_;
    boost::scoped_ptr<BgpSandeshContext> sandesh_context_y_;
};

TEST_P(BgpEvpnTwoControllerTest, Basic) {
    for (size_t i = 1; i <= instance_count_; i++) {
        ostringstream sg;
        sg << "224." << i << "." << instance_count_ << ".3,192.168.1.1";
        RouteAttributes attr;
        ostringstream red;
        red << "red" << i;
        ostringstream nh;
        nh << "10." << i << "." << instance_count_ << ".2";
        NextHop nexthop_red(nh.str(), 10+i, "vxlan");
        agent_xa_->AddRoute(red.str(), "192.168.1.1/32", nexthop_red, attr);
        agent_xa_->AddMcastRoute(red.str(), sg.str(), "10.1.1.3", "100-200");
        task_util::WaitForIdle();

        agent_xa_->AddEnetRoute(red.str(), MulticastMac(i, instance_count_),
                                nexthop_red);
        task_util::WaitForIdle();

        // Verify that the smet route gets added to red and master tables
        TASK_UTIL_EXPECT_EQ(1U, red_[i - 1]->Size());
        TASK_UTIL_EXPECT_EQ(i, master_->Size());
        TASK_UTIL_EXPECT_EQ(i, master_y_->Size());
        TASK_UTIL_EXPECT_EQ(1U, red_y_[i - 1]->Size());

        // Add smet route from another agent, it should not propagate since it
        // is not forest node
        agent_ya_->AddMcastRoute(red.str(), sg.str(), "10.1.1.4", "200-300");
        agent_ya_->AddEnetRoute(red.str(), MulticastMac(i, instance_count_),
                                nexthop_red);
        task_util::WaitForIdle();

        // verify that smet route gets added to red_y_ table only
        TASK_UTIL_EXPECT_EQ(1U, red_[i - 1]->Size());
        TASK_UTIL_EXPECT_EQ(2U, red_y_[i - 1]->Size());
        TASK_UTIL_EXPECT_EQ(i, master_y_->Size());
        TASK_UTIL_EXPECT_EQ(i, master_->Size());
        // Verify that every agent should have eceived ermvpn route
        TASK_UTIL_EXPECT_EQ((int)i, agent_xa_->McastRouteCount());
        TASK_UTIL_EXPECT_EQ((int)i, agent_ya_->McastRouteCount());
        // There is no evpn route since there is no remote route
        TASK_UTIL_EXPECT_EQ(0, agent_xa_->EnetRouteCount());
        TASK_UTIL_EXPECT_EQ(0, agent_ya_->EnetRouteCount());
        TASK_UTIL_EXPECT_EQ(1, CheckErmvpnOListSize(agent_xa_, red.str(),
                                                    sg.str()));
    }
}

// Need to enable it once sender inside contrail is supported
// It is disabled since PMSI information is not sent with SMET route anymore
// and changes need to be made to get the same from correspongin IMET route
TEST_P(BgpEvpnTwoControllerTest, DISABLED_RemoteReceiver) {
    for (size_t i = 1; i <= instance_count_; i++) {
        ostringstream sg;
        sg << "224." << i << "." << instance_count_ << ".3,192.168.1.1";
        RouteAttributes attr;
        ostringstream red;
        red << "red" << i;
        ostringstream nh;
        nh << "10." << i << "." << instance_count_ << ".2";
        NextHop nexthop_red(nh.str(), 10+i, "vxlan");
        agent_xa_->AddRoute(red.str(), "192.168.1.1/32", nexthop_red, attr);
        agent_xa_->AddMcastRoute(red.str(), sg.str(), "10.1.1.3", "100-200");
        task_util::WaitForIdle();

        agent_xa_->AddEnetRoute(red.str(), MulticastMac(i, instance_count_),
                                nexthop_red);
        task_util::WaitForIdle();

        // Verify that the smet route gets added to red and master tables
        TASK_UTIL_EXPECT_EQ(1U, red_[i - 1]->Size());
        TASK_UTIL_EXPECT_EQ(2*i-1, master_->Size());
        TASK_UTIL_EXPECT_EQ(2*i-1, master_y_->Size());
        TASK_UTIL_EXPECT_EQ(1U, red_y_[i - 1]->Size());

        // add smet route from remote mx
        test::RouteParams mx_params;
        mx_params.edge_replication_not_supported = true;
        agent_yb_->AddMcastRoute(red.str(), sg.str(), "10.1.1.5", "300-400");
        agent_yb_->AddEnetRoute(red.str(), MulticastMac(i, instance_count_),
                                nexthop_red, &mx_params);
        task_util::WaitForIdle();
        char sg_mac[55];
        sprintf(sg_mac, "%02x:%02x:%02x:%02x:%02x:%02x,%s,%s",
                1, 0, 0x5e, (unsigned int)i,
                (unsigned int)instance_count_, 3,
                "192.168.0.101/32", sg.str().c_str());
        VerifyOList(agent_xa_, red.str(), sg_mac, 1, nh.str());
        TASK_UTIL_EXPECT_EQ(2U, red_[i - 1]->Size());
        TASK_UTIL_EXPECT_EQ(2U, red_y_[i - 1]->Size());
        TASK_UTIL_EXPECT_EQ(2*i, master_y_->Size());
        TASK_UTIL_EXPECT_EQ(2*i, master_->Size());
    }
}

TEST_P(BgpEvpnTwoControllerTest, EvpnWithoutErmvpnRoute) {
    for (size_t i = 1; i <= instance_count_; i++) {
        ostringstream sg;
        sg << "224." << i << "." << instance_count_ << ".3,192.168.1.1";
        RouteAttributes attr;
        ostringstream red;
        red << "red" << i;
        ostringstream nh;
        nh << "10." << i << "." << instance_count_ << ".2";
        NextHop nexthop_red(nh.str(), 10+i, "vxlan");
        agent_xa_->AddRoute(red.str(), "192.168.1.1/32", nexthop_red, attr);
        agent_xa_->AddMcastRoute(red.str(), sg.str(), "10.1.2.2", "100-200");
        agent_xa_->AddEnetRoute(red.str(), MulticastMac(i, instance_count_),
                                nexthop_red);
        task_util::WaitForIdle();

        // Verify that tables get the smet route
        TASK_UTIL_EXPECT_EQ(1U, red_[i - 1]->Size());
        TASK_UTIL_EXPECT_EQ(i, master_y_->Size());

        // Delete the ermvpn route
        agent_xa_->DeleteMcastRoute(red.str(), sg.str());
        // Verify that smet route gets deleted from master table
        // In red table, it will only be marked as invalid
        TASK_UTIL_EXPECT_EQ(1U, red_[i - 1]->Size());
        TASK_UTIL_EXPECT_EQ(i - 1, master_y_->Size());
        TASK_UTIL_EXPECT_EQ(i - 1, master_->Size());

        // Add the ermvpn receiver back
        agent_xa_->AddMcastRoute(red.str(), sg.str(), "10.1.2.2", "100-200");

        // Verify that smet route gets added back
        TASK_UTIL_EXPECT_EQ(1U, red_[i - 1]->Size());
        TASK_UTIL_EXPECT_EQ(i, master_->Size());
    }
}

static int GetInstanceCount() {
    char *env = getenv("BGP_EVPN_TEST_INSTANCE_COUNT");
    int count = 4;
    if (!env)
        return count;
    stringToInteger(string(env), count);
    return count;
}

INSTANTIATE_TEST_CASE_P(BgpEvpnTestWithParams, BgpEvpnOneControllerTest,
    ::testing::Range(1, GetInstanceCount()));
INSTANTIATE_TEST_CASE_P(BgpEvpnTestWithParams, BgpEvpnTwoControllerTest,
    ::testing::Values(1, 3, GetInstanceCount()));

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
    SetUp();
    int result = RUN_ALL_TESTS();
    TearDown();

    return result;
}
