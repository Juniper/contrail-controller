/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <vector>
#include <string>
#include <base/logging.h>
#include <boost/bind.hpp>
#include <tbb/task.h>
#include "io/test/event_manager_test.h"

#include <cmn/agent_cmn.h>
#include "base/test/task_test_util.h"

#include "pkt/pkt_init.h"
#include "services/services_init.h"
#include "test_cmn_util.h"
#include "xmpp/xmpp_init.h"
#include "xmpp/test/xmpp_test_util.h"
#include "vr_types.h"

#include "xml/xml_pugi.h"

using namespace pugi;

void RouterIdDepInit(Agent *agent) {
}

class AgentBgpXmppPeerTest : public AgentXmppChannel {
public:
    AgentBgpXmppPeerTest(std::string xs, uint8_t xs_idx) :
        AgentXmppChannel(Agent::GetInstance(), xs, "0", xs_idx),
        rx_count_(0), stop_scheduler_(false), rx_channel_event_queue_(
            TaskScheduler::GetInstance()->GetTaskId("xmpp::StateMachine"), 0,
            boost::bind(&AgentBgpXmppPeerTest::ProcessChannelEvent, this, _1)) {
    }

    virtual void ReceiveUpdate(const XmppStanza::XmppMessage *msg) {
        rx_count_++;
        AgentXmppChannel::ReceiveUpdate(msg);
    }

    bool ProcessChannelEvent(xmps::PeerState state) {
        AgentXmppChannel::HandleAgentXmppClientChannelEvent(static_cast<AgentXmppChannel *>(this), state);
        return true;
    }

    void HandleXmppChannelEvent(xmps::PeerState state) {
        rx_channel_event_queue_.Enqueue(state);
    }

    size_t Count() const { return rx_count_; }
    virtual ~AgentBgpXmppPeerTest() { }
    void stop_scheduler(bool stop) {stop_scheduler_ = stop;}

private:
    size_t rx_count_;
    bool stop_scheduler_;
    WorkQueue<xmps::PeerState> rx_channel_event_queue_;
};

class ControlNodeMockBgpXmppPeer {
public:
    ControlNodeMockBgpXmppPeer() : channel_ (NULL), rx_count_(0) {
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
            channel->UnRegisterReceive(xmps::BGP);
            channel_ = NULL;
        } else {
            if (channel_) {
                assert(channel == channel_);
            }
            //assert(channel_ && channel == channel_);
            channel->RegisterReceive(xmps::BGP,
                    boost::bind(&ControlNodeMockBgpXmppPeer::ReceiveUpdate,
                                this, _1));
            channel_ = channel;
        }
    }

    bool SendUpdate(uint8_t *msg, size_t size) {
        if (channel_ && 
            (channel_->GetPeerState() == xmps::READY)) {
            return channel_->Send(msg, size, xmps::BGP,
                   boost::bind(&ControlNodeMockBgpXmppPeer::WriteReadyCb, this, _1));
        }
        return false;
    }

    void WriteReadyCb(const boost::system::error_code &ec) {
    }

    size_t Count() const { return rx_count_; }
    virtual ~ControlNodeMockBgpXmppPeer() {
    }
private:
    XmppChannel *channel_;
    size_t rx_count_;
};


class AgentXmppUnitTest : public ::testing::Test { 
protected:
    AgentXmppUnitTest() : thread_(&evm_), agent_(Agent::GetInstance()) {}
 
    virtual void SetUp() {
        xs1 = new XmppServer(&evm_, XmppInit::kControlNodeJID);
        xs2 = new XmppServer(&evm_, XmppInit::kControlNodeJID);
        xs3 = new XmppServer(&evm_, XmppInit::kControlNodeJID);
        xs4 = new XmppServer(&evm_, XmppInit::kControlNodeJID);

        xs1->Initialize(0, false);
        xs2->Initialize(0, false);
        xs3->Initialize(0, false);
        xs4->Initialize(0, false);
        
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

        TcpServerManager::DeleteServer(xs1);
        TcpServerManager::DeleteServer(xs2);
        TcpServerManager::DeleteServer(xs3);
        TcpServerManager::DeleteServer(xs4);

        evm_.Shutdown();
        thread_.Join();
        client->WaitForIdle();
    }

    void XmppServerConnectionInit() {

        //Init VNController for running tests
        Agent::GetInstance()->controller()->Connect();

        //Create control-node bgp mock peer 
        mock_peer1.reset(new ControlNodeMockBgpXmppPeer());
	xs1->RegisterConnectionEvent(xmps::BGP,
	    boost::bind(&ControlNodeMockBgpXmppPeer::HandleXmppChannelEvent, 
                        mock_peer1.get(), _1, _2));

        //Create control-node bgp mock peer 
        mock_peer2.reset(new ControlNodeMockBgpXmppPeer());
	xs2->RegisterConnectionEvent(xmps::BGP,
	    boost::bind(&ControlNodeMockBgpXmppPeer::HandleXmppChannelEvent, 
                        mock_peer2.get(), _1, _2));

        //Create control-node bgp mock peer 
        mock_peer3.reset(new ControlNodeMockBgpXmppPeer());
	xs3->RegisterConnectionEvent(xmps::BGP,
	    boost::bind(&ControlNodeMockBgpXmppPeer::HandleXmppChannelEvent, 
                        mock_peer3.get(), _1, _2));

        //Create control-node bgp mock peer 
        mock_peer4.reset(new ControlNodeMockBgpXmppPeer());
	xs4->RegisterConnectionEvent(xmps::BGP,
	    boost::bind(&ControlNodeMockBgpXmppPeer::HandleXmppChannelEvent, 
                        mock_peer4.get(), _1, _2));
    }

    EventManager evm_;
    ServerThread thread_;

    XmppServer *xs1;
    XmppServer *xs2;
    XmppServer *xs3;
    XmppServer *xs4;

    auto_ptr<ControlNodeMockBgpXmppPeer> mock_peer1;
    auto_ptr<ControlNodeMockBgpXmppPeer> mock_peer2;
    auto_ptr<ControlNodeMockBgpXmppPeer> mock_peer3;
    auto_ptr<ControlNodeMockBgpXmppPeer> mock_peer4;
    Agent *agent_;
};


namespace {

TEST_F(AgentXmppUnitTest, XmppConnection_Discovery) {

    client->Reset();
    client->WaitForIdle();

    XmppServerConnectionInit();

    // Simulate Discovery response for Xmpp Server
    std::vector<DSResponse> ds_response;
    DSResponse resp;
    resp.ep.address(boost::asio::ip::address::from_string("127.0.0.1"));
    resp.ep.port(xs1->GetPort());
    ds_response.push_back(resp);

    agent_->controller()->ApplyDiscoveryXmppServices(ds_response);
    client->WaitForIdle();

    //wait for connection establishment
    EXPECT_TRUE(agent_->controller_ifmap_xmpp_port(0) == xs1->GetPort()); 
    WAIT_FOR(1000, 10000, 
             agent_->controller_xmpp_channel(0)->GetXmppChannel()->GetPeerState() == xmps::READY); 
    client->WaitForIdle();

    //Bring down Xmpp Server 
    xs1->Shutdown();
    WAIT_FOR(1000, 10000, 
             agent_->controller_xmpp_channel(0)->GetXmppChannel()->GetPeerState() == xmps::NOT_READY);

    //Discovery indicating new Xmpp Server 
    ds_response.clear();
    resp.ep.address(boost::asio::ip::address::from_string("127.0.0.2"));
    resp.ep.port(xs2->GetPort());
    ds_response.push_back(resp);

    agent_->controller()->ApplyDiscoveryXmppServices(ds_response);
    client->WaitForIdle();

    //wait for connection establishment
    EXPECT_TRUE(agent_->controller_ifmap_xmpp_port(1) == xs2->GetPort()); 
    WAIT_FOR(1000, 10000, 
             agent_->controller_xmpp_channel(1)->GetXmppChannel()->GetPeerState() == xmps::READY); 
    client->WaitForIdle();

    //Bring down Xmpp Server
    xs2->Shutdown();
    WAIT_FOR(1000, 10000, 
             agent_->controller_xmpp_channel(1)->GetXmppChannel()->GetPeerState() == xmps::NOT_READY);

    //Discovery indicating new Xmpp Server
    ds_response.clear();
    resp.ep.address(boost::asio::ip::address::from_string("127.0.0.3"));
    resp.ep.port(xs3->GetPort());
    ds_response.push_back(resp);

    agent_->controller()->ApplyDiscoveryXmppServices(ds_response);
    client->WaitForIdle();

    //wait for connection establishment
    EXPECT_TRUE(agent_->controller_ifmap_xmpp_port(0) == xs3->GetPort()); 
    WAIT_FOR(1000, 10000, 
             agent_->controller_xmpp_channel(0)->GetXmppChannel()->GetPeerState() == xmps::READY); 
    // Wait until older XmppClient, XmppChannel is cleaned
    client->WaitForIdle();

    //Discovery indicating new Xmpp Server
    ds_response.clear();
    resp.ep.address(boost::asio::ip::address::from_string("127.0.0.3"));
    resp.ep.port(xs3->GetPort());
    ds_response.push_back(resp);
    resp.ep.address(boost::asio::ip::address::from_string("127.0.0.4"));
    resp.ep.port(xs4->GetPort());
    ds_response.push_back(resp);

    agent_->controller()->ApplyDiscoveryXmppServices(ds_response);
    client->WaitForIdle();

    //wait for connection establishment
    EXPECT_TRUE(agent_->controller_ifmap_xmpp_port(0) == xs3->GetPort()); 
    WAIT_FOR(1000, 10000, 
             agent_->controller_xmpp_channel(0)->GetXmppChannel()->GetPeerState() == xmps::READY); 

    EXPECT_TRUE(agent_->controller_ifmap_xmpp_port(1) == xs4->GetPort()); 
    WAIT_FOR(1000, 10000, 
             agent_->controller_xmpp_channel(1)->GetXmppChannel()->GetPeerState() == xmps::READY); 
    // Wait until older XmppClient, XmppChannel is cleaned
    client->WaitForIdle();

    xs3->Shutdown();
    client->WaitForIdle();

    xs4->Shutdown();
    client->WaitForIdle();
}

}

int main(int argc, char **argv) {
    GETUSERARGS();
   
    LoggingInit();
    Sandesh::SetLocalLogging(true);
    Sandesh::SetLoggingLevel(SandeshLevel::UT_DEBUG);

    client = TestInit(init_file, ksync_init);
    Agent::GetInstance()->set_controller_ifmap_xmpp_server("", 0);  
    Agent::GetInstance()->set_controller_ifmap_xmpp_server("", 1);  

    Agent::GetInstance()->set_dns_server("", 0);  
    Agent::GetInstance()->set_dns_server("", 1);  

    int ret = RUN_ALL_TESTS();

    TestShutdown();
    delete client;
    return ret; 
}


