/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/foreach.hpp>
#include <boost/format.hpp>

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
using boost::format;
#define kFabricInstance BgpConfigManager::kFabricInstance
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

class BgpMvpnIntegrationTest {
public:
    BgpMvpnIntegrationTest() :
        thread_(&evm_), xs_x_(NULL), red_(NULL), blue_(NULL), green_(NULL),
        red_inet_(NULL), green_inet_(NULL) {
    }

    virtual void SetUp() {
        bs_x_.reset(new BgpServerTest(&evm_, "X"));
        bs_x_->set_mvpn_ipv4_enable(true);
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
        if (blue_)
            delete[] blue_;
        if (green_)
            delete[] green_;
        if (red_inet_)
            delete[] red_inet_;
        if (green_inet_)
            delete[] green_inet_;

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
        if (net == kFabricInstance) {
            agent_xa_->SubscribeAll(net, id);
            agent_xb_->SubscribeAll(net, id);
            agent_xc_->SubscribeAll(net, id);
            task_util::WaitForIdle();
            return;
        }
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
"  <virtual-network name='blue" << i << "'>"
"      <network-id>" << 300+i << "</network-id>"
"  </virtual-network>"
"   <routing-instance name='blue" << i << "'>"
"       <virtual-network>blue" << i << "</virtual-network>"
"       <vrf-target>" << getRouteTarget(i, "2") << "</vrf-target>"
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
        const string &prefix) {
        const NetworkAgentMock::McastRouteEntry *ermvpn_rt =
            agent->McastRouteLookup(kFabricInstance, prefix);
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
            other_agent->McastRouteLookup(kFabricInstance, prefix);
        if (ermvpn_rt == NULL)
            return false;

        if (!(ermvpn_rt->entry.nlri.source_address == source_address))
            return false;
        int label = GetLabel(other_agent, kFabricInstance, prefix);
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

    bool VerifyMvpnRouteType(boost::shared_ptr<const NetworkAgentMock> agent,
        const string &net, const string &prefix, int route_type =
                                             MvpnPrefix::SourceTreeJoinRoute) {
        const NetworkAgentMock::MvpnRouteEntry *mvpn_rt =
            agent->MvpnRouteLookup(net, prefix);
        if (mvpn_rt == NULL)
            return false;

        if (mvpn_rt->entry.nlri.route_type == route_type)
            return true;
        return false;
    }

    void SandeshStartup();
    void SandeshShutdown();

    void TableInit() {
        master_ = static_cast<BgpTable *>(
            bs_x_->database()->FindTable("bgp.mvpn.0"));
        ostringstream os;
        os << "default-domain:default-project:ip-fabric:ip-fabric";
        os << ".ermvpn.0";

        ostringstream os2;
        os2 << "default-domain:default-project:ip-fabric:ip-fabric";
        os2 << ".mvpn.0";

        fabric_ermvpn_ = dynamic_cast<ErmVpnTable *>(
                bs_x_->database()->FindTable(os.str()));
        fabric_mvpn_ = dynamic_cast<MvpnTable *>(
                bs_x_->database()->FindTable(os2.str()));

        for (size_t i = 1; i <= instance_count_; i++) {
            ostringstream r, b, g, ri, gi;
            r << "red" << i << ".mvpn.0";
            b << "blue" << i << ".mvpn.0";
            g << "green" << i << ".mvpn.0";
            ri << "red" << i << ".inet.0";
            gi << "green" << i << ".inet.0";
            TASK_UTIL_EXPECT_NE(static_cast<BgpTable *>(NULL),
                                bs_x_->database()->FindTable(r.str()));
            TASK_UTIL_EXPECT_NE(static_cast<BgpTable *>(NULL),
                                bs_x_->database()->FindTable(b.str()));
            TASK_UTIL_EXPECT_NE(static_cast<BgpTable *>(NULL),
                                bs_x_->database()->FindTable(g.str()));

            red_[i-1] = static_cast<MvpnTable *>(
                bs_x_->database()->FindTable(r.str()));
            blue_[i-1] = static_cast<MvpnTable *>(
                bs_x_->database()->FindTable(b.str()));
            green_[i-1] = static_cast<MvpnTable *>(
                bs_x_->database()->FindTable(g.str()));
            red_inet_[i-1] = static_cast<InetTable *>(
                bs_x_->database()->FindTable(ri.str()));
            green_inet_[i-1] = static_cast<InetTable *>(
                bs_x_->database()->FindTable(gi.str()));
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
    ErmVpnTable *fabric_ermvpn_;
    MvpnTable **red_;
    MvpnTable **blue_;
    MvpnTable **green_;
    MvpnTable *fabric_mvpn_;
    InetTable **red_inet_;
    InetTable **green_inet_;
    SandeshServerTest *sandesh_bs_x_;
    boost::scoped_ptr<BgpSandeshContext> sandesh_context_;

    size_t instance_count_;
};

void BgpMvpnIntegrationTest::SandeshStartup() {
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

void BgpMvpnIntegrationTest::SandeshShutdown() {
    Sandesh::Uninit();
    task_util::WaitForIdle();
    sandesh_bs_x_->Shutdown();
    task_util::WaitForIdle();
    TcpServerManager::DeleteServer(sandesh_bs_x_);
    sandesh_bs_x_ = NULL;
    task_util::WaitForIdle();
}

class BgpMvpnOneControllerTest : public BgpMvpnIntegrationTest,
                                 public ::testing::TestWithParam<int> {
public:
    static const int kTimeoutSeconds = 15;
    virtual void SetUp() {
        BgpMvpnIntegrationTest::SetUp();

        instance_count_ = GetParam();
        red_ = new MvpnTable *[instance_count_];
        blue_ = new MvpnTable *[instance_count_];
        green_ = new MvpnTable *[instance_count_];
        red_inet_ = new InetTable *[instance_count_];
        green_inet_ = new InetTable *[instance_count_];

        string config = GetConfig();
        Configure(config.c_str());
        task_util::WaitForIdle();

        BgpMvpnIntegrationTest::TableInit();
        BgpMvpnIntegrationTest::SessionUp();
    }

    virtual void TearDown() {
        BgpMvpnIntegrationTest::SessionDown();
        BgpMvpnIntegrationTest::TearDown();
    }

};

TEST_P(BgpMvpnOneControllerTest, Basic) {
    for (size_t i = 1; i <= instance_count_; i++) {
        // 1 type1 from red and 1 from green
        TASK_UTIL_EXPECT_EQ(2U, red_[i - 1]->Size());
        TASK_UTIL_EXPECT_NE(static_cast<MvpnRoute *>(NULL),
                        red_[i-1]->FindType1ADRoute());

        TASK_UTIL_EXPECT_EQ(1U, blue_[i - 1]->Size());
        TASK_UTIL_EXPECT_NE(static_cast<MvpnRoute *>(NULL),
                        blue_[i-1]->FindType1ADRoute());

        // 1 type1 from red, blue and green
        TASK_UTIL_EXPECT_EQ(3U, green_[i - 1]->Size());

        TASK_UTIL_EXPECT_NE(static_cast<MvpnRoute *>(NULL),
                        green_[i-1]->FindType1ADRoute());

        TASK_UTIL_EXPECT_EQ(1U, fabric_mvpn_->Size());
        TASK_UTIL_EXPECT_NE(static_cast<MvpnRoute *>(NULL),
                        fabric_mvpn_->FindType1ADRoute());
    }

    // red, blue, green, kFabricInstance
    TASK_UTIL_EXPECT_EQ(static_cast<size_t>(1 + 3 * instance_count_),
                        master_->Size());
    // Register agents and add source active mvpn routes
    Subscribe("red", 1);
    Subscribe("green", 2);
    Subscribe(kFabricInstance, 1000);

    for (size_t i = 1; i <= instance_count_; i++) {
        ostringstream sg;
        sg << "224." << i << "." << instance_count_ << ".3,192.168.1.1";

        string tunnel;
        RouteAttributes attr;
        ostringstream red;
        red << "red" << i;
        NextHop nexthop_red("10.1.1.2", 11, tunnel, red.str());
        agent_xa_->AddRoute(red.str(), "192.168.1.0/24", nexthop_red, attr);
        task_util::WaitForIdle();
        agent_xa_->AddType5MvpnRoute(red.str(), sg.str(), "10.1.1.2");
        task_util::WaitForIdle();

        // Verify that Type5 route gets added
        TASK_UTIL_EXPECT_EQ(3U, red_[i - 1]->Size());
        TASK_UTIL_EXPECT_EQ(1U, blue_[i - 1]->Size());
        TASK_UTIL_EXPECT_EQ(4U, green_[i - 1]->Size());
        TASK_UTIL_EXPECT_EQ(1U, red_inet_[i - 1]->Size());
        TASK_UTIL_EXPECT_EQ(1U, green_inet_[i - 1]->Size());
        TASK_UTIL_EXPECT_EQ(2 + 3*instance_count_ + 4*(i-1), master_->Size());

        ostringstream grn;
        grn << "green" << i;
        agent_xb_->AddType7MvpnRoute(grn.str(), sg.str(), "10.1.1.3", "30-40");

        // Type7, Type3, Type4 routes should have generated
        TASK_UTIL_EXPECT_EQ(3*i, fabric_ermvpn_->Size());
        TASK_UTIL_EXPECT_EQ(7U, green_[i - 1]->Size());
        TASK_UTIL_EXPECT_EQ(1U, blue_[i - 1]->Size());
        TASK_UTIL_EXPECT_EQ(6U, red_[i - 1]->Size());
        TASK_UTIL_EXPECT_EQ(1 + 3 * instance_count_ + 4 * i, master_->Size());
        // Verify that sender should have received a route
        TASK_UTIL_EXPECT_EQ(0, agent_xa_->McastRouteCount());
        TASK_UTIL_EXPECT_EQ((int)i, agent_xb_->McastRouteCount()); // Receiver
        TASK_UTIL_EXPECT_EQ((int)i, agent_xa_->MvpnRouteCount()); // Sender
        TASK_UTIL_EXPECT_EQ((int)i, agent_xb_->MvpnRouteCount());
        TASK_UTIL_EXPECT_TRUE(VerifyMvpnRouteType(
                    agent_xb_, grn.str(), sg.str()));
        VerifyOListAndSource(agent_xa_, red.str(), sg.str(), 1, "10.1.1.3",
            "192.168.0.101", agent_xb_);
        TASK_UTIL_EXPECT_EQ(0, CheckErmvpnOListSize(agent_xb_, sg.str()));

        // Add a receiver in red
        agent_xa_->AddMcastRoute(kFabricInstance, sg.str(),
                             "10.1.1.3", "50-60");
        // ermvpn route olist size will increase
        TASK_UTIL_EXPECT_EQ(1, CheckErmvpnOListSize(agent_xb_, sg.str()));
        // No change in mvpn route olist
        VerifyOListAndSource(agent_xa_, red.str(), sg.str(), 1, "10.1.1.3",
                         "192.168.0.101", agent_xb_);
        agent_xa_->DeleteMcastRoute(kFabricInstance, sg.str());
    }
}

class BgpMvpnTwoControllerTest : public BgpMvpnIntegrationTest,
                                 public ::testing::TestWithParam<int> {
protected:
    BgpMvpnTwoControllerTest() : BgpMvpnIntegrationTest(), red_y_(NULL),
        blue_y_(NULL), green_y_(NULL) {
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
"                <family>inet-mvpn</family>"
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
"                <family>inet-mvpn</family>"
"                <family>erm-vpn</family>"
"                <family>route-target</family>"
"            </address-families>"
"        </session>"
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
"  </routing-instance>";

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
"   <virtual-network name='blue" << i << "'>"
"       <network-id>" << 300+i << "</network-id>"
"   </virtual-network>"
"   <routing-instance name='blue" << i << "'>"
"       <virtual-network>blue" << i << "</virtual-network>"
"       <vrf-target>" << getRouteTarget(i, "2") << "</vrf-target>"
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
        BgpMvpnIntegrationTest::TableInit();
        master_y_ = static_cast<BgpTable *>(
            bs_y_->database()->FindTable("bgp.mvpn.0"));

        ostringstream os;
        os << "default-domain:default-project:ip-fabric:ip-fabric";
        os << ".ermvpn.0";
        fabric_ermvpn_ = dynamic_cast<ErmVpnTable *>(
                bs_y_->database()->FindTable(os.str()));
        for (size_t i = 1; i <= instance_count_; i++) {
            ostringstream r, b, g, ri, gi;
            r << "red" << i << ".mvpn.0";
            b << "blue" << i << ".mvpn.0";
            g << "green" << i << ".mvpn.0";

            red_y_[i-1] = static_cast<MvpnTable *>(
                bs_y_->database()->FindTable(r.str()));
            blue_y_[i-1] = static_cast<MvpnTable *>(
                bs_y_->database()->FindTable(b.str()));
            green_y_[i-1] = static_cast<MvpnTable *>(
                bs_y_->database()->FindTable(g.str()));
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
        bs_y_->set_mvpn_ipv4_enable(true);
        xs_y_ = new XmppServer(&evm_, XmppDocumentMock::kControlNodeJID);
        bs_y_->session_manager()->Initialize(0);
        xs_y_->Initialize(0, false);
        bcm_y_.reset(new BgpXmppChannelManager(xs_y_, bs_y_.get()));

        BgpMvpnIntegrationTest::SetUp();
#if SANDESH_Y
        SandeshYStartup();

        sandesh_context_y_->bgp_server = bs_y_.get();
        sandesh_context_y_->xmpp_peer_manager = bcm_y_.get();
#endif

        instance_count_ = GetParam();
        type1_routes_ = 2 + 6 * instance_count_;
        red_ = new MvpnTable *[instance_count_];
        blue_ = new MvpnTable *[instance_count_];
        green_ = new MvpnTable *[instance_count_];
        red_inet_ = new InetTable *[instance_count_];
        green_inet_ = new InetTable *[instance_count_];
        red_y_ = new MvpnTable *[instance_count_];
        blue_y_ = new MvpnTable *[instance_count_];
        green_y_ = new MvpnTable *[instance_count_];

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
        Subscribe("blue", 3);
        Subscribe(kFabricInstance, 1000);
        task_util::WaitForIdle();
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

#if SANDESH_Y
        SandeshYShutdown();
        task_util::WaitForIdle();
        sandesh_context_y_.reset();
#endif
        BgpMvpnIntegrationTest::TearDown();
        if (red_y_)
            delete[] red_y_;
        if (blue_y_)
            delete[] blue_y_;
        if (green_y_)
            delete[] green_y_;
    }

    virtual void Subscribe(const string net, int id) {
        BgpMvpnIntegrationTest::Subscribe(net, id);

        if (net == kFabricInstance) {
            agent_ya_->SubscribeAll(net, id);
            agent_yb_->SubscribeAll(net, id);
            agent_yc_->SubscribeAll(net, id);
            task_util::WaitForIdle();
            return;
        }
        for (size_t i = 1; i <= instance_count_; i++) {
            ostringstream r;
            r << net << i;
            agent_ya_->SubscribeAll(r.str(), id);
            agent_yb_->SubscribeAll(r.str(), id);
            agent_yc_->SubscribeAll(r.str(), id);
            task_util::WaitForIdle();
        }
    }

    int type1_routes_;
    BgpServerTestPtr bs_y_;
    XmppServer *xs_y_;
    boost::scoped_ptr<BgpXmppChannelManager> bcm_y_;
    boost::shared_ptr<NetworkAgentMock> agent_ya_;
    boost::shared_ptr<NetworkAgentMock> agent_yb_;
    boost::shared_ptr<NetworkAgentMock> agent_yc_;
    BgpTable *master_y_;
    MvpnTable **red_y_;
    MvpnTable **blue_y_;
    MvpnTable **green_y_;
    BgpPeerTest *peer_x_;
    BgpPeerTest *peer_y_;
    SandeshServerTest *sandesh_bs_y_;
    boost::scoped_ptr<BgpSandeshContext> sandesh_context_y_;
};

TEST_P(BgpMvpnTwoControllerTest, RedSenderGreenReceiver) {
    for (size_t i = 1; i <= instance_count_; i++) {
        TASK_UTIL_EXPECT_EQ(4U, red_[i - 1]->Size());  // 1 type1 each from A, B
        TASK_UTIL_EXPECT_EQ(4U,
                            red_y_[i - 1]->Size());  // 1 type1 each from A, B
        TASK_UTIL_EXPECT_NE(static_cast<MvpnRoute *>(NULL),
                            red_[i-1]->FindType1ADRoute());
        TASK_UTIL_EXPECT_NE(static_cast<MvpnRoute *>(NULL),
                            red_y_[i-1]->FindType1ADRoute());

        TASK_UTIL_EXPECT_EQ(2U,
                            blue_[i - 1]->Size());  // 1 type1 each from A, B
        TASK_UTIL_EXPECT_NE(static_cast<MvpnRoute *>(NULL),
                            blue_[i-1]->FindType1ADRoute());

        TASK_UTIL_EXPECT_EQ(6U,
                            green_[i - 1]->Size());  // 1 type1 each from A, B

        TASK_UTIL_EXPECT_NE(static_cast<MvpnRoute *>(NULL),
                            green_[i-1]->FindType1ADRoute());
        TASK_UTIL_EXPECT_TRUE(peer_x_->IsReady());
        TASK_UTIL_EXPECT_TRUE(peer_y_->IsReady());
    }
    // red, blue, green, kFabricInstance from A, B
    TASK_UTIL_EXPECT_EQ(2 + 6 * instance_count_, master_->Size());
    TASK_UTIL_EXPECT_EQ(2 + 6 * instance_count_, master_y_->Size());

    for (size_t i = 1; i <= instance_count_; i++) {
        ostringstream sg;
        sg << "224." << i << "." << instance_count_ << ".3,192.168.1.1";
        string tunnel;
        RouteAttributes attr;
        ostringstream red;
        red << "red" << i;
        ostringstream nh;
        nh << "10." << i << "." << instance_count_ << ".2";
        NextHop nexthop_red(nh.str(), 11, tunnel, red.str());
        agent_xa_->AddRoute(red.str(), "192.168.1.1/32", nexthop_red, attr);
        task_util::WaitForIdle();

        agent_xa_->AddType5MvpnRoute(red.str(), sg.str(), nh.str());

        // Verify that the type5 route gets added to red and master only
        TASK_UTIL_EXPECT_EQ(5U, red_[i - 1]->Size());
        TASK_UTIL_EXPECT_EQ(2U, blue_[i - 1]->Size());
        // For every i, 4 routes get added
        TASK_UTIL_EXPECT_EQ(3 + 6*instance_count_ + 4*(i-1), master_->Size());
        TASK_UTIL_EXPECT_EQ(7U, green_[i - 1]->Size());

        ostringstream grn;
        grn << "green" << i;
        agent_yb_->AddType7MvpnRoute(grn.str(), sg.str(), "10.1.2.3", "30-40");
        task_util::WaitForIdle();
        TASK_UTIL_EXPECT_EQ(3*i, fabric_ermvpn_->Size());

        // verify that t7, t3, t4 primary routes get added to red, master
        TASK_UTIL_EXPECT_EQ(8U, red_[i - 1]->Size());
        TASK_UTIL_EXPECT_EQ(8U, green_[i - 1]->Size());
        // total 3 more routes got added
        TASK_UTIL_EXPECT_EQ(3 + 6*instance_count_ + 4*i -1, master_y_->Size());
        TASK_UTIL_EXPECT_EQ(3 + 6*instance_count_ + 4*i -1, master_->Size());
        TASK_UTIL_EXPECT_EQ(2U, blue_[i - 1]->Size());
        // Verify that sender agent should have received a mvpn route
        TASK_UTIL_EXPECT_EQ(0, agent_xa_->McastRouteCount());
        TASK_UTIL_EXPECT_EQ((int)i, agent_yb_->McastRouteCount());
        TASK_UTIL_EXPECT_EQ((int)i, agent_xa_->MvpnRouteCount());
        TASK_UTIL_EXPECT_EQ((int)i, agent_yb_->MvpnRouteCount());
        TASK_UTIL_EXPECT_TRUE(VerifyMvpnRouteType(
                    agent_yb_, grn.str(), sg.str()));
        VerifyOListAndSource(agent_xa_, red.str(), sg.str(), 1, "10.1.2.3",
            "192.168.0.101", agent_yb_);
        TASK_UTIL_EXPECT_EQ(0, CheckErmvpnOListSize(agent_yb_, sg.str()));
    }
}

TEST_P(BgpMvpnTwoControllerTest, RedSenderRedGreenReceiver) {
    for (size_t i = 1; i <= instance_count_; i++) {
        string tunnel;
        RouteAttributes attr;
        ostringstream red;
        red << "red" << i;
        ostringstream nh;
        nh << "10." << i << "." << instance_count_ << ".2";
        NextHop nexthop_red(nh.str(), 11, tunnel, red.str());
        agent_xa_->AddRoute(red.str(), "192.168.1.1/32", nexthop_red, attr);
        task_util::WaitForIdle();

        ostringstream sg;
        sg << "224." << i << "." << instance_count_ << ".3,192.168.1.1";
        agent_xa_->AddType5MvpnRoute(red.str(), sg.str(), nh.str());

        ostringstream grn;
        grn << "green" << i;
        agent_ya_->AddType7MvpnRoute(grn.str(), sg.str(), "10.1.1.3", "50-60");

        agent_yb_->AddType7MvpnRoute(red.str(), sg.str(), "10.1.2.3", "30-40");
        task_util::WaitForIdle();
        TASK_UTIL_EXPECT_EQ(4*i, fabric_ermvpn_->Size());
        TASK_UTIL_EXPECT_EQ(8U, red_[i - 1]->Size());
        TASK_UTIL_EXPECT_EQ(2U, blue_[i - 1]->Size());
        TASK_UTIL_EXPECT_EQ(8U, green_[i - 1]->Size());
        // 2 + 6*instance_count_
        // For every i, 4 routes get added
        TASK_UTIL_EXPECT_EQ(type1_routes_ + 4*i, master_y_->Size());
        TASK_UTIL_EXPECT_EQ(type1_routes_ + 4*i, master_->Size());
        VerifyOListAndSource(agent_xa_, red.str(), sg.str(), 1, "10.1.2.3",
            "192.168.0.101", agent_yb_);

        // Add receivers on X and make sure that it also receives type7 route
        TASK_UTIL_EXPECT_EQ((int)i, agent_xa_->MvpnRouteCount());
        agent_xa_->AddType7MvpnRoute(grn.str(), sg.str(), "10.1.3.3", "70-80");
        TASK_UTIL_EXPECT_EQ((int)i+1, agent_xa_->MvpnRouteCount());
        TASK_UTIL_EXPECT_TRUE(VerifyMvpnRouteType(
                    agent_xa_, grn.str(), sg.str()));
        TASK_UTIL_EXPECT_EQ(9U, green_[i - 1]->Size());
        agent_xa_->DeleteMvpnRoute(grn.str(), sg.str(), 7);
        agent_xa_->DeleteMcastRoute(kFabricInstance, sg.str());
        TASK_UTIL_EXPECT_EQ((int)i, agent_xa_->MvpnRouteCount());
    }
}

TEST_P(BgpMvpnTwoControllerTest, MultipleReceivers) {
    for (size_t i = 1; i <= instance_count_; i++) {
        ostringstream sg;
        sg << "224." << i << "." << instance_count_ << ".3,192.168.1.1";
        string tunnel;
        RouteAttributes attr;
        ostringstream red;
        red << "red" << i;
        ostringstream nh;
        nh << "10." << i << "." << instance_count_ << ".2";
        NextHop nexthop_red(nh.str(), 11, tunnel, red.str());
        agent_xa_->AddRoute(red.str(), "192.168.1.1/32", nexthop_red, attr);
        task_util::WaitForIdle();

        agent_xa_->AddType5MvpnRoute(red.str(), sg.str(), nh.str());

        // Verify that the type5 route gets added to red, green and master only
        TASK_UTIL_EXPECT_EQ(5U, red_[i - 1]->Size());
        TASK_UTIL_EXPECT_EQ(2U, blue_[i - 1]->Size());
        TASK_UTIL_EXPECT_EQ(7U, green_[i - 1]->Size());
        TASK_UTIL_EXPECT_EQ(type1_routes_ + 4*(i-1) + 1, master_->Size());

        agent_ya_->AddType7MvpnRoute(red.str(), sg.str(), "10.1.2.2", "30-40");
        agent_yb_->AddType7MvpnRoute(red.str(), sg.str(), "10.1.2.3", "50-60");
        agent_yc_->AddType7MvpnRoute(red.str(), sg.str(), "10.1.2.4", "70-80");
        task_util::WaitForIdle();
        TASK_UTIL_EXPECT_EQ(5*i, fabric_ermvpn_->Size());

        // verify that t7, t3, t4 primary routes get added to red, master
        TASK_UTIL_EXPECT_EQ(8U, red_[i - 1]->Size());
        TASK_UTIL_EXPECT_EQ(type1_routes_ + 4*i, master_y_->Size());
        TASK_UTIL_EXPECT_EQ(type1_routes_ + 4*i, master_->Size());
        TASK_UTIL_EXPECT_EQ(2U, blue_[i - 1]->Size());
        // Verify that sender agent should have received a mvpn route
        TASK_UTIL_EXPECT_EQ(0, agent_xa_->McastRouteCount());
        TASK_UTIL_EXPECT_EQ((int)i, agent_yb_->McastRouteCount());
        TASK_UTIL_EXPECT_EQ((int)i, agent_xa_->MvpnRouteCount());
        TASK_UTIL_EXPECT_EQ((int)i, agent_ya_->MvpnRouteCount());
        TASK_UTIL_EXPECT_EQ((int)i, agent_yb_->MvpnRouteCount());
        TASK_UTIL_EXPECT_EQ((int)i, agent_yc_->MvpnRouteCount());
        TASK_UTIL_EXPECT_TRUE(VerifyMvpnRouteType(
                    agent_ya_, red.str(), sg.str()));
        VerifyOListAndSource(agent_xa_, red.str(), sg.str(), 1, "10.1.2.4",
            "192.168.0.101", agent_yc_);
        TASK_UTIL_EXPECT_EQ(2, CheckErmvpnOListSize(agent_ya_, sg.str()));
    }
}

TEST_P(BgpMvpnTwoControllerTest, RedSenderRedGreenReceiverGreenDown) {
    for (size_t i = 1; i <= instance_count_; i++) {
        ostringstream sg;
        sg << "224." << i << "." << instance_count_ << ".3,192.168.1.1";
        string tunnel;
        RouteAttributes attr;
        ostringstream red;
        red << "red" << i;
        ostringstream nh;
        nh << "10." << i << "." << instance_count_ << ".2";
        NextHop nexthop_red(nh.str(), 11, tunnel, red.str());
        agent_xa_->AddRoute(red.str(), "192.168.1.1/32", nexthop_red, attr);
        task_util::WaitForIdle();

        agent_xa_->AddType5MvpnRoute(red.str(), sg.str(), nh.str());

        agent_yb_->AddType7MvpnRoute(red.str(), sg.str(), "10.1.2.2", "30-40");
        task_util::WaitForIdle();

        VerifyOListAndSource(agent_xa_, red.str(), sg.str(), 1, "10.1.2.2",
            "192.168.0.101", agent_yb_);
        ostringstream green;
        green << "green" << i;
        agent_ya_->AddType7MvpnRoute(green.str(), sg.str(), "10.1.1.3", "50-60");
        task_util::WaitForIdle();
        TASK_UTIL_EXPECT_EQ(8U, red_[i - 1]->Size());
        TASK_UTIL_EXPECT_EQ(2U, blue_[i - 1]->Size());
        TASK_UTIL_EXPECT_EQ(8U, green_[i - 1]->Size());
        TASK_UTIL_EXPECT_EQ(type1_routes_ + 4 * i, master_y_->Size());
        TASK_UTIL_EXPECT_EQ(type1_routes_ + 4 * i, master_->Size());
        VerifyOListAndSource(agent_xa_, red.str(), sg.str(), 1, "10.1.2.2",
            "192.168.0.101", agent_yb_);
        TASK_UTIL_EXPECT_EQ(1, CheckErmvpnOListSize(agent_yb_, sg.str()));
    }

    for (size_t i = 1; i <= instance_count_; i++) {
        string delete_green = GetDeleteGreenConfig(i);
        Configure(delete_green.c_str());
        agent_ya_->McastUnsubscribe(kFabricInstance, 1000);
        task_util::WaitForIdle();
        TASK_UTIL_EXPECT_EQ(6U, red_[i - 1]->Size());
        TASK_UTIL_EXPECT_EQ(type1_routes_ + 4 * instance_count_ - 2 * i,
                master_->Size());
        ostringstream sg;
        sg << "224." << i << "." << instance_count_ << ".3,192.168.1.1";
        TASK_UTIL_EXPECT_EQ(0, CheckErmvpnOListSize(agent_yb_, sg.str()));
    }
}

TEST_P(BgpMvpnTwoControllerTest, Type5AfterType7) {
    for (size_t i = 1; i <= instance_count_; i++) {
        ostringstream sg;
        sg << "224." << i << "." << instance_count_ << ".3,192.168.1.1";
        ostringstream grn;
        grn << "green" << i;
        agent_yb_->AddType7MvpnRoute(grn.str(), sg.str(), "10.1.2.2", "30-40");
        TASK_UTIL_EXPECT_EQ(3*i, fabric_ermvpn_->Size());

        // verify that nothing changes since source is not resolvable
        // Only type7 route should get added to green_y_
        TASK_UTIL_EXPECT_EQ(6U, green_[i - 1]->Size());
        TASK_UTIL_EXPECT_EQ(7U, green_y_[i - 1]->Size());
        TASK_UTIL_EXPECT_EQ(type1_routes_ + 4 * (i - 1), master_y_->Size());

        ostringstream red;
        red << "red" << i;
        string tunnel;
        RouteAttributes attr;
        ostringstream nh;
        nh << "10." << i << "." << instance_count_ << ".2";
        NextHop nexthop_red(nh.str(), 11, tunnel, red.str());
        agent_xa_->AddRoute(red.str(), "192.168.1.1/32", nexthop_red, attr);

        // Verify that type7 route gets resolved and copied to red_ of sender
        // Howver, type3 route does not get generated since no type5 route
        TASK_UTIL_EXPECT_EQ(type1_routes_ + 4*(i-1) + 1, master_y_->Size());
        TASK_UTIL_EXPECT_EQ(type1_routes_ + 4*(i-1) + 1, master_->Size());
        TASK_UTIL_EXPECT_EQ(5U, red_[i - 1]->Size());

        agent_xa_->AddType5MvpnRoute(red.str(), sg.str(), nh.str());
        // verify that t5, t3, t4 primary routes get added to red, master
        TASK_UTIL_EXPECT_EQ(8U, red_[i - 1]->Size());
        TASK_UTIL_EXPECT_EQ(8U, green_[i - 1]->Size());
        TASK_UTIL_EXPECT_EQ(type1_routes_ + 4 * i, master_y_->Size());
        TASK_UTIL_EXPECT_EQ(type1_routes_ + 4 * i, master_->Size());
        TASK_UTIL_EXPECT_EQ(2U, blue_[i - 1]->Size());
        // Verify that sender agent should have received a mvpn route
        TASK_UTIL_EXPECT_EQ(0, agent_xa_->McastRouteCount());
        TASK_UTIL_EXPECT_EQ((int)i, agent_yb_->McastRouteCount());
        TASK_UTIL_EXPECT_EQ((int)i, agent_xa_->MvpnRouteCount());
        TASK_UTIL_EXPECT_EQ((int)i, agent_yb_->MvpnRouteCount());
        TASK_UTIL_EXPECT_TRUE(VerifyMvpnRouteType(
                    agent_yb_, grn.str(), sg.str()));
        VerifyOListAndSource(agent_xa_, red.str(), sg.str(), 1, "10.1.2.2",
            "192.168.0.101", agent_yb_);
        TASK_UTIL_EXPECT_EQ(0, CheckErmvpnOListSize(agent_yb_, sg.str()));
    }
}

TEST_P(BgpMvpnTwoControllerTest, MvpnWithoutErmvpnRoute) {
    for (size_t i = 1; i <= instance_count_; i++) {
        ostringstream sg;
        sg << "224." << i << "." << instance_count_ << ".3,192.168.1.1";
        string tunnel;
        RouteAttributes attr;
        ostringstream red;
        red << "red" << i;
        ostringstream nh;
        nh << "10." << i << "." << instance_count_ << ".2";
        NextHop nexthop_red(nh.str(), 11, tunnel, red.str());
        agent_xa_->AddRoute(red.str(), "192.168.1.1/32", nexthop_red, attr);
        agent_xa_->AddType5MvpnRoute(red.str(), sg.str(), nh.str());
        agent_yb_->AddType7MvpnRoute(red.str(), sg.str(), "10.1.2.2", "30-40");

        // Verify that tables get all mvpn routes
        TASK_UTIL_EXPECT_EQ(8U, red_[i - 1]->Size());
        TASK_UTIL_EXPECT_EQ(8U, green_[i - 1]->Size());
        TASK_UTIL_EXPECT_EQ(type1_routes_ + 4 * i, master_y_->Size());
        TASK_UTIL_EXPECT_EQ(3 * i, fabric_ermvpn_->Size());

        // Delete the ermvpn route
        agent_yb_->DeleteMcastRoute(kFabricInstance, sg.str());
        // Verify that type4 route gets deleted
        TASK_UTIL_EXPECT_EQ(3*(i-1), fabric_ermvpn_->Size());
        TASK_UTIL_EXPECT_EQ(7U, red_[i - 1]->Size());
        TASK_UTIL_EXPECT_EQ(type1_routes_ + 4*i - 1, master_y_->Size());
        TASK_UTIL_EXPECT_EQ(type1_routes_ + 4*i - 1, master_->Size());
        // Verify that type5 route is withdrawn since there are no receivers
        TASK_UTIL_EXPECT_EQ(static_cast<NetworkAgentMock::MvpnRouteEntry *>(NULL),
            agent_xa_->MvpnRouteLookup(red.str(), sg.str()));

        // Add the ermvpn receiver back
        agent_yb_->AddMcastRoute(kFabricInstance, sg.str(),
                             "10.1.2.2", "30-40", "");

        TASK_UTIL_EXPECT_EQ(3*i, fabric_ermvpn_->Size());
        // Verify that type4 route gets added back
        TASK_UTIL_EXPECT_EQ(8U, red_[i - 1]->Size());
        TASK_UTIL_EXPECT_EQ(type1_routes_ + 4 * i, master_y_->Size());
        // Verify that mvpn and ermvpn routes are ok
        VerifyOListAndSource(agent_xa_, red.str(), sg.str(), 1, "10.1.2.2",
            "192.168.0.101", agent_yb_);
    }
}

TEST_P(BgpMvpnTwoControllerTest, ReceiverSenderLeave) {
    for (size_t i = 1; i <= instance_count_; i++) {
        ostringstream sg;
        sg << "224." << i << "." << instance_count_ << ".3,192.168.1.1";
        string tunnel;
        RouteAttributes attr;
        ostringstream red;
        red << "red" << i;
        ostringstream nh;
        nh << "10." << i << "." << instance_count_ << ".2";
        NextHop nexthop_red(nh.str(), 11, tunnel, red.str());
        agent_xa_->AddRoute(red.str(), "192.168.1.1/32", nexthop_red, attr);
        agent_xa_->AddType5MvpnRoute(red.str(), sg.str(), nh.str());
        ostringstream green;
        green << "green" << i;
        agent_yb_->AddType7MvpnRoute(green.str(), sg.str(), "10.1.2.2", "30-40");

        // Verify that tables get all mvpn routes
        TASK_UTIL_EXPECT_EQ(8U, red_[i - 1]->Size());
        TASK_UTIL_EXPECT_EQ(8U, green_[i - 1]->Size());
        TASK_UTIL_EXPECT_EQ(type1_routes_ + 4 * i, master_y_->Size());

        // Delete the type7 join route
        agent_yb_->DeleteMvpnRoute(green.str(), sg.str(), 7);
        // Verify that type7, type3 and type4 routes get deleted
        TASK_UTIL_EXPECT_EQ(5U, red_[i - 1]->Size());
        TASK_UTIL_EXPECT_EQ(type1_routes_ + 4*(i-1) + 1, master_->Size());
        // Verify that type5 route is withdrawn since there are no receivers
        TASK_UTIL_EXPECT_EQ(static_cast<NetworkAgentMock::MvpnRouteEntry *>(NULL),
            agent_xa_->MvpnRouteLookup(red.str(), sg.str()));

        // Add the receiver back
        agent_yb_->AddType7MvpnRoute(green.str(), sg.str(), "10.1.2.2", "30-40");

        // Verify that type7, type3 and type4 routes get added back
        TASK_UTIL_EXPECT_EQ(8U, red_[i - 1]->Size());
        TASK_UTIL_EXPECT_EQ(3+ 6*instance_count_ + 4*i -1, master_->Size());
        // Verify that mvpn and ermvpn routes are ok
        VerifyOListAndSource(agent_xa_, red.str(), sg.str(), 1, "10.1.2.2",
            "192.168.0.101", agent_yb_);

        // Delete the type5 source active route
        agent_xa_->DeleteMvpnRoute(red.str(), sg.str(), 5);
        // Verify that type5, type3 and type4 routes get deleted
        TASK_UTIL_EXPECT_EQ(5U, red_[i - 1]->Size());
        TASK_UTIL_EXPECT_EQ(type1_routes_ + 4*(i -1) + 1, master_->Size());
        // Verify that type5 route is withdrawn since there are no receivers
        TASK_UTIL_EXPECT_EQ(static_cast<NetworkAgentMock::MvpnRouteEntry *>
                (NULL), agent_xa_->MvpnRouteLookup(red.str(), sg.str()));

        // Add the sender back
        agent_xa_->AddType5MvpnRoute(red.str(), sg.str(), nh.str());

        // Verify that type5, type3 and type4 routes get added back
        TASK_UTIL_EXPECT_EQ(8U, red_[i - 1]->Size());
        TASK_UTIL_EXPECT_EQ(type1_routes_ + 4 * i, master_->Size());
        // Verify that mvpn and ermvpn routes are ok
        VerifyOListAndSource(agent_xa_, red.str(), sg.str(), 1, "10.1.2.2",
            "192.168.0.101", agent_yb_);
    }
};

TEST_P(BgpMvpnTwoControllerTest, ChangeIdentifier) {
    for (size_t i = 1; i <= instance_count_; i++) {
        // Verify that tables get all mvpn routes
        TASK_UTIL_EXPECT_EQ(4U, red_[i - 1]->Size());
        TASK_UTIL_EXPECT_EQ(6U, green_[i - 1]->Size());
    }
    TASK_UTIL_EXPECT_EQ(8 + 6*(instance_count_-1), master_y_->Size());

    // Change the identifiers of routers
    string config = GetIdentifierChangeConfig();
    Configure(config.c_str());
    Subscribe("blue", 3);
    Subscribe("red", 1);
    Subscribe("green", 2);
    Subscribe(kFabricInstance, 1000);
    task_util::WaitForIdle();
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

    for (size_t i = 1; i <= instance_count_; i++) {
        // Verify that tables get all mvpn routes
        TASK_UTIL_EXPECT_EQ(4U, red_[i - 1]->Size());
        TASK_UTIL_EXPECT_EQ(6U, green_[i - 1]->Size());
    }
    TASK_UTIL_EXPECT_EQ(8 + 6*(instance_count_-1), master_y_->Size());
}

static int GetInstanceCount() {
    char *env = getenv("BGP_MVPN_TEST_INSTANCE_COUNT");
    int count = 4;
    if (!env)
        return count;
    stringToInteger(string(env), count);
    return count;
}

INSTANTIATE_TEST_CASE_P(BgpMvpnTestWithParams, BgpMvpnOneControllerTest,
    ::testing::Range(1, GetInstanceCount()));
INSTANTIATE_TEST_CASE_P(BgpMvpnTestWithParams, BgpMvpnTwoControllerTest,
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
