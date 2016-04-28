/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/os.h"
#include <vector>
#include <string>
#include <base/logging.h>
#include <boost/bind.hpp>
#include <tbb/task.h>
#include "io/test/event_manager_test.h"

#include <cmn/agent_cmn.h>
#include "base/test/task_test_util.h"

#include "test_cmn_util.h"
#include "xmpp/xmpp_init.h"
#include "xmpp/test/xmpp_test_util.h"

#include "xml/xml_pugi.h"

#include "controller/controller_peer.h"
#include "controller/controller_dns.h"
#include "controller/controller_export.h"
#include "controller/controller_vrf_export.h"
#include "controller/controller_types.h"

using namespace pugi;

void RouterIdDepInit(Agent *agent) {
}

class ControlNodeMockDnsXmppPeer {
public:
    ControlNodeMockDnsXmppPeer() : channel_ (NULL), rx_count_(0) {
    }

    void ReceiveUpdate(const XmppStanza::XmppMessage *msg) {
        rx_count_++;
    }

    void HandleXmppChannelEvent(XmppChannel *channel,
                                xmps::PeerState state) {
        if (!channel_ && state == xmps::NOT_READY) {
            return;
        }
        if (state != xmps::READY) {
            assert(channel_ && channel == channel_);
            channel->UnRegisterReceive(xmps::DNS);
            channel_ = NULL;
        } else {
            if (channel_) {
                assert(channel == channel_);
            }
            channel->RegisterReceive(xmps::DNS,
                    boost::bind(&ControlNodeMockDnsXmppPeer::ReceiveUpdate,
                                this, _1));
            channel_ = channel;
        }
    }

    bool SendUpdate(uint8_t *msg, size_t size) {
        if (channel_ &&
            (channel_->GetPeerState() == xmps::READY)) {
            return channel_->Send(msg, size, xmps::DNS,
                   boost::bind(&ControlNodeMockDnsXmppPeer::WriteReadyCb, this, _1));
        }
        return false;
    }

    void WriteReadyCb(const boost::system::error_code &ec) {
    }

    size_t Count() const { return rx_count_; }
    virtual ~ControlNodeMockDnsXmppPeer() {
    }
private:
    XmppChannel *channel_;
    size_t rx_count_;
};


class AgentXmppUnitTest : public ::testing::Test {
protected:
    AgentXmppUnitTest() : thread_(&evm_), agent_(Agent::GetInstance()) {}

    virtual void SetUp() {

        //TestInit initializes xmpp connection to 127.0.0.1, so disconnect that
        //and again spawn a new one. Its required since the receive path
        //is overridden by mock class.
        //TODO later use the agent initializer
        agent_->controller()->Cleanup();
        client->WaitForIdle();
        agent_->controller()->DisConnect();
        client->WaitForIdle();

        xs1 = new XmppServer(&evm_, XmppInit::kDnsNodeJID);
        xs2 = new XmppServer(&evm_, XmppInit::kDnsNodeJID);
        xs3 = new XmppServer(&evm_, XmppInit::kDnsNodeJID);
        xs4 = new XmppServer(&evm_, XmppInit::kDnsNodeJID);
        xs5 = new XmppServer(&evm_, XmppInit::kDnsNodeJID);
        xs6 = new XmppServer(&evm_, XmppInit::kDnsNodeJID);

        xs1->Initialize(0, false);
        xs2->Initialize(0, false);
        xs3->Initialize(0, false);
        xs4->Initialize(0, false);
        xs5->Initialize(0, false);
        xs6->Initialize(0, false);

        thread_.Start();
    }

    virtual void TearDown() {

        TaskScheduler::GetInstance()->Stop();
        Agent::GetInstance()->controller()->unicast_cleanup_timer().cleanup_timer_->Fire();
        Agent::GetInstance()->controller()->multicast_cleanup_timer().cleanup_timer_->Fire();
        Agent::GetInstance()->controller()->config_cleanup_timer().cleanup_timer_->Fire();
        TaskScheduler::GetInstance()->Start();
        client->WaitForIdle();
        Agent::GetInstance()->controller()->Cleanup();
        client->WaitForIdle();
        WAIT_FOR(1000, 1000, (Agent::GetInstance()->controller()->
                 DecommissionedPeerListSize() == 0));

        TcpServerManager::DeleteServer(xs1);
        TcpServerManager::DeleteServer(xs2);
        TcpServerManager::DeleteServer(xs3);
        TcpServerManager::DeleteServer(xs4);
        TcpServerManager::DeleteServer(xs5);
        TcpServerManager::DeleteServer(xs6);

        evm_.Shutdown();
        thread_.Join();
        client->WaitForIdle();
    }

    void XmppServerConnectionInit() {

        //Init VNController for running tests
        agent_->controller()->Connect();

        //Create control-node dns mock peer
        mock_peer1.reset(new ControlNodeMockDnsXmppPeer());
	xs1->RegisterConnectionEvent(xmps::DNS,
	    boost::bind(&ControlNodeMockDnsXmppPeer::HandleXmppChannelEvent,
                        mock_peer1.get(), _1, _2));

        //Create control-node dns mock peer
        mock_peer2.reset(new ControlNodeMockDnsXmppPeer());
	xs2->RegisterConnectionEvent(xmps::DNS,
	    boost::bind(&ControlNodeMockDnsXmppPeer::HandleXmppChannelEvent,
                        mock_peer2.get(), _1, _2));

        //Create control-node dns mock peer
        mock_peer3.reset(new ControlNodeMockDnsXmppPeer());
	xs3->RegisterConnectionEvent(xmps::DNS,
	    boost::bind(&ControlNodeMockDnsXmppPeer::HandleXmppChannelEvent,
                        mock_peer3.get(), _1, _2));

        //Create control-node dns mock peer
        mock_peer4.reset(new ControlNodeMockDnsXmppPeer());
	xs4->RegisterConnectionEvent(xmps::DNS,
	    boost::bind(&ControlNodeMockDnsXmppPeer::HandleXmppChannelEvent,
                        mock_peer4.get(), _1, _2));

        //Create control-node dns mock peer
        mock_peer5.reset(new ControlNodeMockDnsXmppPeer());
	xs5->RegisterConnectionEvent(xmps::DNS,
	    boost::bind(&ControlNodeMockDnsXmppPeer::HandleXmppChannelEvent,
                        mock_peer5.get(), _1, _2));

        //Create control-node dns mock peer
        mock_peer6.reset(new ControlNodeMockDnsXmppPeer());
	xs6->RegisterConnectionEvent(xmps::DNS,
	    boost::bind(&ControlNodeMockDnsXmppPeer::HandleXmppChannelEvent,
                        mock_peer6.get(), _1, _2));

    }

    EventManager evm_;
    ServerThread thread_;

    XmppServer *xs1;
    XmppServer *xs2;
    XmppServer *xs3;
    XmppServer *xs4;
    XmppServer *xs5;
    XmppServer *xs6;

    auto_ptr<ControlNodeMockDnsXmppPeer> mock_peer1;
    auto_ptr<ControlNodeMockDnsXmppPeer> mock_peer2;
    auto_ptr<ControlNodeMockDnsXmppPeer> mock_peer3;
    auto_ptr<ControlNodeMockDnsXmppPeer> mock_peer4;
    auto_ptr<ControlNodeMockDnsXmppPeer> mock_peer5;
    auto_ptr<ControlNodeMockDnsXmppPeer> mock_peer6;
    Agent *agent_;
};


namespace {

TEST_F(AgentXmppUnitTest, XmppConnection_Dns_Discovery) {

    client->Reset();
    client->WaitForIdle();

    XmppServerConnectionInit();

    // Simulate Discovery response for Xmpp Server
    std::vector<DSResponse> ds_response;
    DSResponse resp;
    resp.ep.address(boost::asio::ip::address::from_string("127.0.0.1"));
    resp.ep.port(xs1->GetPort());
    ds_response.push_back(resp);
    resp.ep.address(boost::asio::ip::address::from_string("127.0.0.2"));
    resp.ep.port(xs2->GetPort());
    ds_response.push_back(resp);

    agent_->controller()->ApplyDiscoveryDnsXmppServices(ds_response);
    client->WaitForIdle();

    //wait for connection establishment
    EXPECT_TRUE(agent_->dns_server_port(0) ==
                (uint32_t)xs1->GetPort());
    EXPECT_TRUE(agent_->dns_server_port(1) ==
                (uint32_t)xs2->GetPort());
    WAIT_FOR(1000, 10000,
        agent_->dns_xmpp_channel(0)->GetXmppChannel()->GetPeerState()
        == xmps::READY);
    WAIT_FOR(1000, 10000,
        agent_->dns_xmpp_channel(1)->GetXmppChannel()->GetPeerState()
        == xmps::READY);
    client->WaitForIdle();

    //Discovery indicating new Dns Xmpp Server
    ds_response.clear();
    resp.ep.address(boost::asio::ip::address::from_string("127.0.0.3"));
    resp.ep.port(xs3->GetPort());
    ds_response.push_back(resp);
    resp.ep.address(boost::asio::ip::address::from_string("127.0.0.4"));
    resp.ep.port(xs4->GetPort());
    ds_response.push_back(resp);

    agent_->controller()->ApplyDiscoveryDnsXmppServices(ds_response);
    client->WaitForIdle();

    //wait for connection establishment
    EXPECT_TRUE(agent_->dns_server_port(0) ==
                (uint32_t)xs3->GetPort());
    EXPECT_TRUE(agent_->dns_server_port(1) ==
                (uint32_t)xs4->GetPort());
    WAIT_FOR(1000, 10000,
        agent_->dns_xmpp_channel(0)->GetXmppChannel()->GetPeerState()
        == xmps::READY);
    WAIT_FOR(1000, 10000,
        agent_->dns_xmpp_channel(1)->GetXmppChannel()->GetPeerState()
        == xmps::READY);
    client->WaitForIdle();

    //Discovery indicating new Dns Xmpp Server
    ds_response.clear();
    resp.ep.address(boost::asio::ip::address::from_string("127.0.0.5"));
    resp.ep.port(xs5->GetPort());
    ds_response.push_back(resp);
    resp.ep.address(boost::asio::ip::address::from_string("127.0.0.6"));
    resp.ep.port(xs6->GetPort());
    ds_response.push_back(resp);

    agent_->controller()->ApplyDiscoveryDnsXmppServices(ds_response);
    client->WaitForIdle();

    //wait for connection establishment
    EXPECT_TRUE(agent_->dns_server_port(0) ==
                (uint32_t)xs5->GetPort());
    EXPECT_TRUE(agent_->dns_server_port(1) ==
                (uint32_t)xs6->GetPort());
    WAIT_FOR(1000, 10000,
        agent_->dns_xmpp_channel(0)->GetXmppChannel()->GetPeerState()
        == xmps::READY);
    WAIT_FOR(1000, 10000,
        agent_->dns_xmpp_channel(1)->GetXmppChannel()->GetPeerState()
        == xmps::READY);
    client->WaitForIdle();

    xs1->Shutdown();
    client->WaitForIdle();
    xs2->Shutdown();
    client->WaitForIdle();
    xs3->Shutdown();
    client->WaitForIdle();
    xs4->Shutdown();
    client->WaitForIdle();
    xs5->Shutdown();
    client->WaitForIdle();
    xs6->Shutdown();
    client->WaitForIdle();
}


TEST_F(AgentXmppUnitTest, XmppConnection_DnsDown_Discovery) {

    client->Reset();
    client->WaitForIdle();

    XmppServerConnectionInit();

    // Simulate Discovery response for Xmpp Server
    std::vector<DSResponse> ds_response;
    DSResponse resp;
    resp.ep.address(boost::asio::ip::address::from_string("127.0.0.1"));
    resp.ep.port(xs1->GetPort());
    ds_response.push_back(resp);

    agent_->controller()->ApplyDiscoveryDnsXmppServices(ds_response);
    client->WaitForIdle();

    //wait for connection establishment
    EXPECT_TRUE(agent_->dns_server_port(0) ==
                (uint32_t)xs1->GetPort());
    WAIT_FOR(1000, 10000,
        agent_->dns_xmpp_channel(0)->GetXmppChannel()->GetPeerState()
        == xmps::READY);
    client->WaitForIdle();

    //Bring down Xmpp Server
    xs1->Shutdown();
    WAIT_FOR(1000, 10000,
        agent_->dns_xmpp_channel(0)->GetXmppChannel()->GetPeerState()
        == xmps::NOT_READY);

    //Discovery indicating new Dns Xmpp Server
    ds_response.clear();
    resp.ep.address(boost::asio::ip::address::from_string("127.0.0.2"));
    resp.ep.port(xs2->GetPort());
    ds_response.push_back(resp);

    agent_->controller()->ApplyDiscoveryDnsXmppServices(ds_response);
    client->WaitForIdle();

    //wait for connection establishment
    EXPECT_TRUE(agent_->dns_server_port(0) ==
                (uint32_t)xs2->GetPort());
    WAIT_FOR(1000, 10000,
        agent_->dns_xmpp_channel(0)->GetXmppChannel()->GetPeerState()
        == xmps::READY);
    client->WaitForIdle();
    EXPECT_TRUE(agent_->dns_xmpp_channel(1) == NULL);

    //Bring down Xmpp Server
    xs2->Shutdown();
    WAIT_FOR(1000, 10000,
        agent_->dns_xmpp_channel(0)->GetXmppChannel()->GetPeerState()
        == xmps::NOT_READY);
    client->WaitForIdle();
    EXPECT_TRUE(agent_->dns_xmpp_channel(1) == NULL);

    //Discovery indicating new Xmpp Server
    ds_response.clear();
    resp.ep.address(boost::asio::ip::address::from_string("127.0.0.3"));
    resp.ep.port(xs3->GetPort());
    ds_response.push_back(resp);

    agent_->controller()->ApplyDiscoveryDnsXmppServices(ds_response);
    client->WaitForIdle();

    //wait for connection establishment
    EXPECT_TRUE(agent_->dns_server_port(0) ==
                (uint32_t)xs3->GetPort());
    WAIT_FOR(1000, 10000,
        agent_->dns_xmpp_channel(0)->GetXmppChannel()->GetPeerState()
        == xmps::READY);
    // Wait until older XmppClient, XmppChannel is cleaned
    client->WaitForIdle();
    EXPECT_TRUE(agent_->dns_xmpp_channel(1) == NULL);

    //Discovery indicating new Xmpp Server
    ds_response.clear();
    resp.ep.address(boost::asio::ip::address::from_string("127.0.0.3"));
    resp.ep.port(xs3->GetPort());
    ds_response.push_back(resp);
    resp.ep.address(boost::asio::ip::address::from_string("127.0.0.4"));
    resp.ep.port(xs4->GetPort());
    ds_response.push_back(resp);

    agent_->controller()->ApplyDiscoveryDnsXmppServices(ds_response);
    client->WaitForIdle();

    //wait for connection establishment
    EXPECT_TRUE(agent_->dns_server_port(0) ==
                (uint32_t)xs3->GetPort());
    WAIT_FOR(1000, 10000,
        agent_->dns_xmpp_channel(0)->GetXmppChannel()->GetPeerState()
        == xmps::READY);

    EXPECT_TRUE(agent_->dns_server_port(1) == (uint32_t)xs4->GetPort());
    WAIT_FOR(1000, 10000,
        agent_->dns_xmpp_channel(1)->GetXmppChannel()->GetPeerState()
        == xmps::READY);
    // Wait until older XmppClient, XmppChannel is cleaned
    client->WaitForIdle();

    xs3->Shutdown();
    client->WaitForIdle();

    xs4->Shutdown();
    client->WaitForIdle();

    //Discovery indicating new Xmpp Server
    ds_response.clear();
    resp.ep.address(boost::asio::ip::address::from_string("127.0.0.5"));
    resp.ep.port(xs5->GetPort());
    ds_response.push_back(resp);
    resp.ep.address(boost::asio::ip::address::from_string("127.0.0.6"));
    resp.ep.port(xs6->GetPort());
    ds_response.push_back(resp);

    agent_->controller()->ApplyDiscoveryDnsXmppServices(ds_response);
    client->WaitForIdle();

    //wait for connection establishment
    EXPECT_TRUE(agent_->dns_server_port(0) ==
                (uint32_t)xs5->GetPort());
    WAIT_FOR(1000, 10000,
        agent_->dns_xmpp_channel(0)->GetXmppChannel()->GetPeerState()
        == xmps::READY);

    EXPECT_TRUE(agent_->dns_server_port(1) ==
                (uint32_t)xs6->GetPort());
    WAIT_FOR(1000, 10000,
        agent_->dns_xmpp_channel(1)->GetXmppChannel()->GetPeerState()
        == xmps::READY);
    // Wait until older XmppClient, XmppChannel is cleaned
    client->WaitForIdle();

    xs5->Shutdown();
    client->WaitForIdle();

    xs6->Shutdown();
    client->WaitForIdle();
}

TEST_F(AgentXmppUnitTest, XmppConnection_Apply_3_DiscoveryDnsServers) {

    client->Reset();
    client->WaitForIdle();

    XmppServerConnectionInit();

    // Simulate Discovery response for Xmpp Server
    std::vector<DSResponse> ds_response;
    DSResponse resp;
    resp.ep.address(boost::asio::ip::address::from_string("127.0.0.1"));
    resp.ep.port(xs1->GetPort());
    ds_response.push_back(resp);
    resp.ep.address(boost::asio::ip::address::from_string("127.0.0.2"));
    resp.ep.port(xs2->GetPort());
    ds_response.push_back(resp);
    resp.ep.address(boost::asio::ip::address::from_string("127.0.0.3"));
    resp.ep.port(xs3->GetPort());
    ds_response.push_back(resp);

    agent_->controller()->ApplyDiscoveryDnsXmppServices(ds_response);
    client->WaitForIdle();

    //wait for connection establishment
    EXPECT_TRUE(agent_->dns_server_port(0) ==
                (uint32_t)xs1->GetPort());
    WAIT_FOR(1000, 10000,
        agent_->dns_xmpp_channel(0)->GetXmppChannel()->GetPeerState()
        == xmps::READY);

    EXPECT_TRUE(agent_->dns_server_port(1) ==
                (uint32_t)xs2->GetPort());
    WAIT_FOR(1000, 10000,
        agent_->dns_xmpp_channel(1)->GetXmppChannel()->GetPeerState()
        == xmps::READY);
    // Wait until older XmppClient, XmppChannel is cleaned
    client->WaitForIdle();

    xs1->Shutdown();
    client->WaitForIdle();

    xs2->Shutdown();
    client->WaitForIdle();

    xs3->Shutdown();
    client->WaitForIdle();

    xs4->Shutdown();
    client->WaitForIdle();

    xs5->Shutdown();
    client->WaitForIdle();

    xs6->Shutdown();
    client->WaitForIdle();
}
}
