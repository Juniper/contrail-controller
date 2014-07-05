/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <unistd.h>
#include <fstream>
#include <sstream>
#include <boost/algorithm/string.hpp>
#include <boost/assign/list_of.hpp>
#include <boost/bind.hpp>

#include <pugixml/pugixml.hpp>

#include "base/util.h"
#include "base/test/task_test_util.h"

#include "sandesh/sandesh_types.h"
#include "sandesh/sandesh.h"

#include "bgp/bgp_attr.h"
#include "bgp/bgp_config.h"
#include "bgp/bgp_config_parser.h"
#include "bgp/bgp_path.h"
#include "bgp/bgp_peer_membership.h"
#include "bgp/bgp_peer_types.h"
#include "bgp/bgp_proto.h"
#include "bgp/bgp_sandesh.h"
#include "bgp/bgp_server.h"
#include "bgp/bgp_session_manager.h"
#include "bgp/bgp_xmpp_channel.h"
#include "bgp/inet/inet_table.h"
#include "bgp/routing-instance/routing_instance.h"
#include "bgp/test/bgp_server_test_util.h"
#include "control-node/control_node.h"
#include "io/test/event_manager_test.h"

#include "schema/xmpp_unicast_types.h"

#include "xmpp/xmpp_client.h"
#include "xmpp/xmpp_init.h"
#include "xmpp/xmpp_server.h"
#include "xmpp/xmpp_state_machine.h"

#include "xml/xml_pugi.h"

#include "testing/gunit.h"

using namespace autogen;
using namespace boost::asio;
using namespace boost::assign;
using namespace std;

#define SUB_ADDR "agent@vnsw.contrailsystems.com"
#define PUB_ADDR "bgp@bgp-client.contrailsystems.com"
#define XMPP_CONTROL_SERV   "bgp.contrail.com"
#define PUBSUB_NODE_ADDR "bgp-node.contrail.com"

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

class XmppVnswBgpMockChannel : public BgpXmppChannel {
public:
    XmppVnswBgpMockChannel(XmppChannel *channel, BgpServer *bgp_server,
                           BgpXmppChannelManager *channel_manager)
        : BgpXmppChannel(channel, bgp_server, channel_manager), count_(0) { }

    virtual void ReceiveUpdate(const XmppStanza::XmppMessage *msg) {
        count_ ++;
    }
    size_t Count() const { return count_; }
    void ResetCount() { count_ = 0; }
    virtual ~XmppVnswBgpMockChannel() { }

private:
    size_t count_;
};

class BgpXmppChannelManagerMock : public BgpXmppChannelManager {
public:
    BgpXmppChannelManagerMock(XmppServer *x, BgpServer *b) :
        BgpXmppChannelManager(x, b), count_(0), channel_(NULL) { }

    // virtual ~BgpXmppChannelManagerMock() {
        // delete channel_;
    // }

    virtual void XmppHandleChannelEvent(XmppChannel *channel,
                                        xmps::PeerState state) {
         BgpXmppChannelManager::XmppHandleChannelEvent(channel, state);
    }

    virtual BgpXmppChannel *CreateChannel(XmppChannel *channel) {
        channel_ = new BgpXmppChannelMock(channel, bgp_server_, this);
        return channel_;
    }

    void XmppVisit(BgpXmppChannel *channel) {
        count_++;
    }
    int Count() {
        count_ = 0;
        VisitChannels(boost::bind(&BgpXmppChannelManagerMock::XmppVisit,
                      this, _1));
        return count_;
    }
    int count_;
    BgpXmppChannelMock *channel_;
};

class BgpXmppUnitTest : public ::testing::Test {
protected:
    static bool validate_done_;
    static const char *config_tmpl;

    BgpXmppUnitTest() : thread_(&evm_) { }

    string FileRead(const string &filename) {
        ifstream file(filename.c_str());
        string content((istreambuf_iterator<char>(file)),
                       istreambuf_iterator<char>());
        return content;
    }

    virtual void SetUp() {
        a_.reset(new BgpServerTest(&evm_, "A"));
        b_.reset(new BgpServerTest(&evm_, "B"));
        xs_a_ = new XmppServer(&evm_, XMPP_CONTROL_SERV);
        xc_a_ = new XmppClient(&evm_);

        a_->session_manager()->Initialize(0);
        BGP_DEBUG_UT("Created server at port: " <<
                a_->session_manager()->GetPort());
        b_->session_manager()->Initialize(0);
        BGP_DEBUG_UT("Created server at port: " <<
                b_->session_manager()->GetPort());
        xs_a_->Initialize(0, false);

        bgp_channel_manager_ = new BgpXmppChannelManagerMock(xs_a_, a_.get());

        thread_.Start();
    }

    virtual void TearDown() {
        a_->Shutdown();
        b_->Shutdown();
        task_util::WaitForIdle();

        //
        // Delete the channel first
        //
        delete xmpp_cchannel_;
        task_util::WaitForIdle();
        xmpp_cchannel_ = NULL;

        // This will trigger cleanup of all clients
        xs_a_->Shutdown();
        task_util::WaitForIdle();
        xc_a_->Shutdown();
        task_util::WaitForIdle();

        //
        // Now delete the channel manager
        //
        delete bgp_channel_manager_;
        bgp_channel_manager_ = NULL;
        task_util::WaitForIdle();


        //
        // Finally delete the xmpp servers
        //
        TcpServerManager::DeleteServer(xs_a_);
        xs_a_ = NULL;
        TcpServerManager::DeleteServer(xc_a_);
        xc_a_ = NULL;

        evm_.Shutdown();
        thread_.Join();
        task_util::WaitForIdle();
    }

    void Configure() {
        char config[4096];
        snprintf(config, sizeof(config), config_tmpl,
                 a_->session_manager()->GetPort(),
                 b_->session_manager()->GetPort());
        a_->Configure(config);
        b_->Configure(config);
    }

    XmppChannelConfig *CreateXmppChannelCfg(const char *address, int port,
                                            const string &from,
                                            const string &to,
                                            bool isClient) {
        XmppChannelConfig *cfg = new XmppChannelConfig(isClient);
        boost::system::error_code ec;
        cfg->endpoint.address(ip::address::from_string(address, ec));
        cfg->endpoint.port(port);
        cfg->ToAddr = to;
        cfg->FromAddr = from;
        if (!isClient) cfg->NodeAddr = PUBSUB_NODE_ADDR;
        return cfg;
    }

    static void ValidateRoutingInstanceResponse(Sandesh *sandesh,
            vector<size_t> &result) {
        ShowRoutingInstanceResp *resp =
                dynamic_cast<ShowRoutingInstanceResp *>(sandesh);
        TASK_UTIL_EXPECT_NE((ShowRoutingInstanceResp *)NULL, resp);

        TASK_UTIL_EXPECT_EQ(result.size(), resp->get_instances().size());

        cout << "*******************************************************"<<endl;
        for (size_t i = 0; i < resp->get_instances().size(); i++) {
            TASK_UTIL_EXPECT_EQ(result[i], resp->get_instances()[i].tables.size());
            cout << resp->get_instances()[i].name << endl;
            for (size_t j = 0; j < resp->get_instances()[i].tables.size();
                    j++) {
                 cout << "\t" <<
                     resp->get_instances()[i].tables[j].name << endl;
                 size_t k = 0;
                 for (; k < resp->get_instances()[i].tables[j].peers.size();
                         k++) {
                     cout << "\t\t" <<
                         resp->get_instances()[i].tables[j].peers[k] << endl;
                 }
            }
        }
        cout << "*******************************************************"<<endl;
        validate_done_ = true;
    }

    static void ValidateNeighborResponse(Sandesh *sandesh,
                                         vector<size_t> &result) {
        BgpNeighborListResp *resp =
                dynamic_cast<BgpNeighborListResp *>(sandesh);
        TASK_UTIL_EXPECT_NE((BgpNeighborListResp *)NULL, resp);

        TASK_UTIL_EXPECT_EQ(result.size(), resp->get_neighbors().size());

        cout << "*******************************************************"<<endl;
        for (size_t i = 0; i < resp->get_neighbors().size(); i++) {
            TASK_UTIL_EXPECT_EQ(result[i],
                      resp->get_neighbors()[i].routing_tables.size());
            cout << resp->get_neighbors()[i].peer << " "
                 << resp->get_neighbors()[i].encoding << endl;
            size_t j = 0;
            for (; j < resp->get_neighbors()[i].routing_tables.size(); j++) {
                 cout << "\t" <<
                     resp->get_neighbors()[i].routing_tables[j].name << endl;
            }
        }
        cout << "*******************************************************"<<endl;
        validate_done_ = true;
    }

    static void ValidateShowRouteResponse(Sandesh *sandesh, vector<size_t> &result) {
        ShowRouteResp *resp = dynamic_cast<ShowRouteResp *>(sandesh);
        TASK_UTIL_EXPECT_NE((ShowRouteResp *)NULL, resp);

        TASK_UTIL_EXPECT_EQ(result.size(), resp->get_tables().size());

        cout << "*******************************************************"<<endl;
        for (size_t i = 0; i < resp->get_tables().size(); i++) {
            TASK_UTIL_EXPECT_EQ(result[i], resp->get_tables()[i].routes.size());
            cout << resp->get_tables()[i].routing_instance << " "
                 << resp->get_tables()[i].routing_table_name << endl;
            for (size_t j = 0; j < resp->get_tables()[i].routes.size(); j++) {
                cout << resp->get_tables()[i].routes[j].prefix << " "
                        << resp->get_tables()[i].routes[j].paths.size() << endl;
            }
        }
        cout << "*******************************************************"<<endl;
        validate_done_ = true;
    }

    static void ValidateShowXmppServerResponse(Sandesh *sandesh) {
        ShowXmppServerResp *resp = dynamic_cast<ShowXmppServerResp *>(sandesh);
        EXPECT_TRUE(resp != NULL);
        const TcpServerSocketStats &rx_stats = resp->get_rx_socket_stats();
        const TcpServerSocketStats &tx_stats = resp->get_tx_socket_stats();

        cout << "****************************************************" << endl;
        cout << "RX: calls=" << rx_stats.calls << " bytes=" << rx_stats.bytes
             << " average bytes=" << rx_stats.average_bytes << endl;
        cout << "TX: calls=" << tx_stats.calls << " bytes=" << tx_stats.bytes
             << " average bytes=" << tx_stats.average_bytes << endl;
        cout << "****************************************************" << endl;

        EXPECT_NE(0, rx_stats.calls);
        EXPECT_NE(0, rx_stats.bytes);
        EXPECT_NE(0, rx_stats.average_bytes);
        EXPECT_NE(0, tx_stats.calls);
        EXPECT_NE(0, tx_stats.bytes);
        EXPECT_NE(0, tx_stats.average_bytes);
        validate_done_ = true;
    }

    EventManager evm_;
    ServerThread thread_;
    auto_ptr<BgpServerTest> a_;
    auto_ptr<BgpServerTest> b_;
    XmppServer *xs_a_;
    XmppClient *xc_a_;
    BgpXmppChannelManagerMock *bgp_channel_manager_;
    XmppVnswBgpMockChannel *xmpp_cchannel_;
};

bool BgpXmppUnitTest::validate_done_;

const char *BgpXmppUnitTest::config_tmpl = "\
<config>\
    <bgp-router name=\'A\'>\
        <identifier>192.168.0.1</identifier>\
        <autonomous-system>64512</autonomous-system>\
        <address>127.0.0.1</address>\
        <port>%d</port>\
        <session to=\'B\'>\
            <address-families>\
            <family>inet-vpn</family>\
            <family>route-target</family>\
            </address-families>\
        </session>\
    </bgp-router>\
    <bgp-router name=\'B\'>\
        <identifier>192.168.0.2</identifier>\
        <autonomous-system>64512</autonomous-system>\
        <address>127.0.0.1</address>\
        <port>%d</port>\
        <session to=\'A\'>\
            <address-families>\
            <family>inet-vpn</family>\
            <family>route-target</family>\
            </address-families>\
        </session>\
    </bgp-router>\
    <routing-instance name='blue'>\
        <vrf-target>target:1:1</vrf-target>\
    </routing-instance>\
    <routing-instance name='red'>\
        <vrf-target>target:1:1</vrf-target>\
    </routing-instance>\
</config>\
";

#define WAIT_EQ(expected, actual) \
    TASK_UTIL_EXPECT_EQ(expected, actual)
#define WAIT_NE(expected, actual) \
    TASK_UTIL_EXPECT_NE(expected, actual)

namespace {

TEST_F(BgpXmppUnitTest, Connection) {
    Configure();
    task_util::WaitForIdle();

    string uuid = BgpConfigParser::session_uuid("A", "B", 1);
    BgpPeer *channel_a = a_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                            uuid);
    ASSERT_TRUE(channel_a != NULL);
    BGP_WAIT_FOR_PEER_STATE(channel_a, StateMachine::ESTABLISHED);

    BgpPeer *channel_b = b_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                            uuid);
    ASSERT_TRUE(channel_b != NULL);
    BGP_WAIT_FOR_PEER_STATE(channel_b, StateMachine::ESTABLISHED);

    //create an XMPP server and client in server A
    XmppConfigData *xmppc_cfg_a = new XmppConfigData;
    BGP_DEBUG_UT("Create Xmpp client");
    xmppc_cfg_a->AddXmppChannelConfig(CreateXmppChannelCfg("127.0.0.1",
                                      xs_a_->GetPort(),
                                      SUB_ADDR, XMPP_CONTROL_SERV, true));
    xc_a_->ConfigUpdate(xmppc_cfg_a);

    // Wait upto 1 sec
    WAIT_EQ(1, bgp_channel_manager_->Count());
    BGP_DEBUG_UT("-- Executing --");
    WAIT_NE(static_cast<BgpXmppChannelMock *>(NULL),
            bgp_channel_manager_->channel_);

    // client channel
    WAIT_NE(static_cast<XmppChannel *>(NULL),
            xc_a_->FindChannel(XMPP_CONTROL_SERV));
    XmppChannel *cchannel = xc_a_->FindChannel(XMPP_CONTROL_SERV);
    WAIT_EQ(xmps::READY, cchannel->GetPeerState());
    xmpp_cchannel_ = new XmppVnswBgpMockChannel(cchannel, a_.get(),
                                       bgp_channel_manager_);

    //send subscribe message to vrf=__default__ from agent to bgp
    string data = FileRead("controller/src/bgp/testdata/pubsub_sub3.xml");
    uint8_t buf[4096];
    bzero(buf, sizeof(buf));
    memcpy(buf, data.data(), data.size());
    xmpp_cchannel_->Peer()->SendUpdate(buf, data.size());
    BGP_DEBUG_UT("Sent bytes: " << data.size());
    WAIT_EQ(1, bgp_channel_manager_->channel_->Count());
    BGP_DEBUG_UT("Received subscribe message 1 at Server \n ");

    //send subscribe message to vrf=blue from agent to bgp
    data = FileRead("controller/src/bgp/testdata/pubsub_sub.xml");
    bzero(buf, sizeof(buf));
    memcpy(buf, data.data(), data.size());
    xmpp_cchannel_->Peer()->SendUpdate(buf, data.size());
    BGP_DEBUG_UT("Sent bytes: " << data.size());
    WAIT_EQ(2, bgp_channel_manager_->channel_->Count());
    BGP_DEBUG_UT("Received subscribe message 2 at Server \n ");

    //send subscribe message to vrf=red from agent to bgp
    data = FileRead("controller/src/bgp/testdata/pubsub_sub2.xml");
    bzero(buf, sizeof(buf));
    memcpy(buf, data.data(), data.size());
    xmpp_cchannel_->Peer()->SendUpdate(buf, data.size());
    BGP_DEBUG_UT("Sent bytes: " << data.size());
    WAIT_EQ(3, bgp_channel_manager_->channel_->Count());
    BGP_DEBUG_UT("Received subscribe message 3 at Server \n ");

    //send publish  message to vrf=blue from agent to bgp
    data = FileRead("controller/src/bgp/testdata/pubsub_pub.xml");
    bzero(buf, sizeof(buf));
    memcpy(buf, data.data(), data.size());
    xmpp_cchannel_->Peer()->SendUpdate(buf, data.size());
    BGP_DEBUG_UT("Sent bytes: " << data.size());
    WAIT_EQ(4, bgp_channel_manager_->channel_->Count());
    BGP_DEBUG_UT("Received publish message at Server \n ");

    // route reflected back to the Xmpp vnsw client + route leaked to red vrf.
    WAIT_EQ(2, xmpp_cchannel_->Count());
    BGP_DEBUG_UT("Received route message at Client \n ");

    BgpSandeshContext sandesh_context;
    sandesh_context.bgp_server = a_.get();
    sandesh_context.xmpp_peer_manager = bgp_channel_manager_;
    Sandesh::set_client_context(&sandesh_context);
    std::vector<size_t> result = list_of(5)(3)(3); // inet, ermvpn, enet
    Sandesh::set_response_callback(boost::bind(ValidateRoutingInstanceResponse,
                                   _1, result));
    ShowRoutingInstanceReq *req = new ShowRoutingInstanceReq;
    validate_done_ = false;
    req->HandleRequest();
    req->Release();
    WAIT_EQ(true, validate_done_);

    result = list_of(2)(7); // inet, ermvpn, enet
    Sandesh::set_response_callback(boost::bind(ValidateNeighborResponse,
                                   _1, result));
    BgpNeighborReq *nbr_req = new BgpNeighborReq;
    validate_done_ = false;
    nbr_req->HandleRequest();
    nbr_req->Release();
    WAIT_EQ(true, validate_done_);

    //show route
    result = list_of(1)(1)(1)(1);
    Sandesh::set_response_callback(boost::bind(ValidateShowRouteResponse, _1,
                                   result));
    ShowRouteReq *show_req = new ShowRouteReq;
    validate_done_ = false;
    show_req->HandleRequest();
    show_req->Release();
    task_util::WaitForIdle();
    WAIT_EQ(true, validate_done_);
   
    //show route for a table
    result = list_of(1);
    Sandesh::set_response_callback(boost::bind(ValidateShowRouteResponse, _1,
                                   result));
    show_req = new ShowRouteReq;
    show_req->set_routing_table("bgp.l3vpn.0");
    validate_done_ = false;
    show_req->HandleRequest();
    show_req->Release();
    task_util::WaitForIdle();
    WAIT_EQ(true, validate_done_);

    //show route for a routing instance
    result = list_of(1)(1);
    Sandesh::set_response_callback(boost::bind(ValidateShowRouteResponse, _1,
                                   result));
    show_req = new ShowRouteReq;
    show_req->set_routing_instance("default-domain:default-project:ip-fabric:__default__");
    validate_done_ = false;
    show_req->HandleRequest();
    show_req->Release();
    task_util::WaitForIdle();
    WAIT_EQ(true, validate_done_);

    //send publish route dissociate to vrf=blue from agent to bgp
    data = FileRead("controller/src/bgp/testdata/pubsub_dis.xml");
    bzero(buf, sizeof(buf));
    memcpy(buf, data.data(), data.size());
    xmpp_cchannel_->Peer()->SendUpdate(buf, data.size());
    BGP_DEBUG_UT("Sent bytes: " << data.size());
    WAIT_EQ(5, bgp_channel_manager_->channel_->Count());
    BGP_DEBUG_UT("Received route dissociate at Server \n ");

    // route reflected back to the Xmpp vnsw client + route leaked to red vrf.
    WAIT_EQ(4, xmpp_cchannel_->Count());
    BGP_DEBUG_UT("Received reflected route dissociate at Client \n ");

    //send unsubscribe message to vrf=blue from agent to bgp
    data = FileRead("controller/src/bgp/testdata/pubsub_usub.xml");
    bzero(buf, sizeof(buf));
    memcpy(buf, data.data(), data.size());
    xmpp_cchannel_->Peer()->SendUpdate(buf, data.size());
    BGP_DEBUG_UT("Sent bytes: " << data.size());
    WAIT_EQ(6, bgp_channel_manager_->channel_->Count());
    BGP_DEBUG_UT("Received unsubscribe message 1 at Server \n ");

    //send unsubscribe message to vrf=red from agent to bgp
    data = FileRead("controller/src/bgp/testdata/pubsub_usub2.xml");
    bzero(buf, sizeof(buf));
    memcpy(buf, data.data(), data.size());
    xmpp_cchannel_->Peer()->SendUpdate(buf, data.size());
    BGP_DEBUG_UT("Sent bytes: " << data.size());
    WAIT_EQ(7, bgp_channel_manager_->channel_->Count());
    BGP_DEBUG_UT("Received unsubscribe message 2 at Server \n ");

    //send unsubscribe message to vrf=__default__ from agent to bgp
    data = FileRead("controller/src/bgp/testdata/pubsub_usub3.xml");
    bzero(buf, sizeof(buf));
    memcpy(buf, data.data(), data.size());
    xmpp_cchannel_->Peer()->SendUpdate(buf, data.size());
    BGP_DEBUG_UT("Sent bytes: " << data.size());
    WAIT_EQ(8, bgp_channel_manager_->channel_->Count());
    BGP_DEBUG_UT("Received unsubscribe message 3 at Server \n ");

    //show route
    result.resize(0);
    ShowRouteReq *show_req2 = new ShowRouteReq;
    Sandesh::set_response_callback(boost::bind(ValidateShowRouteResponse, _1,
                                               result));
    validate_done_ = false;
    show_req2->HandleRequest();
    show_req2->Release();
    task_util::WaitForIdle();
    WAIT_EQ(true, validate_done_);

    // trigger EvTcpClose on server, which will result in xmpp channel
    // deletion on the server via Peer()->Close()
    xmpp_cchannel_->Peer()->Close();
    task_util::WaitForIdle();
    usleep(2000);
}

TEST_F(BgpXmppUnitTest, ShowXmppServer) {
    Configure();
    task_util::WaitForIdle();

    //create an XMPP server and client in server A
    XmppConfigData *xmppc_cfg_a = new XmppConfigData;
    BGP_DEBUG_UT("Create Xmpp client");
    xmppc_cfg_a->AddXmppChannelConfig(CreateXmppChannelCfg("127.0.0.1",
                                      xs_a_->GetPort(),
                                      SUB_ADDR, XMPP_CONTROL_SERV, true));
    xc_a_->ConfigUpdate(xmppc_cfg_a);

    // Wait upto 1 sec
    WAIT_EQ(1, bgp_channel_manager_->Count());
    WAIT_NE(static_cast<BgpXmppChannelMock *>(NULL),
            bgp_channel_manager_->channel_);

    // client channel
    WAIT_NE(static_cast<XmppChannel *>(NULL),
            xc_a_->FindChannel(XMPP_CONTROL_SERV));
    XmppChannel *cchannel = xc_a_->FindChannel(XMPP_CONTROL_SERV);
    WAIT_EQ(xmps::READY, cchannel->GetPeerState());
    xmpp_cchannel_ = new XmppVnswBgpMockChannel(cchannel, a_.get(),
                                       bgp_channel_manager_);

    //send subscribe message to vrf=red from agent to bgp
    string data = FileRead("controller/src/bgp/testdata/pubsub_sub2.xml");
    uint8_t buf[4096];
    bzero(buf, sizeof(buf));
    memcpy(buf, data.data(), data.size());
    xmpp_cchannel_->Peer()->SendUpdate(buf, data.size());
    BGP_DEBUG_UT("Sent bytes: " << data.size());
    WAIT_EQ(1, bgp_channel_manager_->channel_->Count());
    BGP_DEBUG_UT("Received subscribe message 1 at Server");

    //send unsubscribe message to vrf=red from agent to bgp
    data = FileRead("controller/src/bgp/testdata/pubsub_usub2.xml");
    bzero(buf, sizeof(buf));
    memcpy(buf, data.data(), data.size());
    xmpp_cchannel_->Peer()->SendUpdate(buf, data.size());
    BGP_DEBUG_UT("Sent bytes: " << data.size());
    WAIT_EQ(1, bgp_channel_manager_->channel_->Count());
    BGP_DEBUG_UT("Received unsubscribe message 1 at Server");

    BgpSandeshContext sandesh_context;
    sandesh_context.bgp_server = a_.get();
    sandesh_context.xmpp_peer_manager = bgp_channel_manager_;
    Sandesh::set_client_context(&sandesh_context);
    Sandesh::set_response_callback(
        boost::bind(ValidateShowXmppServerResponse, _1));
    ShowXmppServerReq *show_req = new ShowXmppServerReq;
    validate_done_ = false;
    show_req->HandleRequest();
    show_req->Release();
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(validate_done_);

    // Trigger EvTcpClose on server, which will result in xmpp channel
    // deletion on the server via Peer()->Close().
    xmpp_cchannel_->Peer()->Close();
    task_util::WaitForIdle();
    usleep(2000);
}

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
