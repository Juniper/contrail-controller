/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "testing/gunit.h"

#include <cmn/agent_cmn.h>
#include <pugixml/pugixml.hpp>

#include <base/logging.h>
#include <boost/bind.hpp>
#include <tbb/task.h>
#include <base/task.h>
#include "io/test/event_manager_test.h"
#include <net/bgp_af.h>

#include "test_cmn_util.h"
#include "xmpp/xmpp_init.h"
#include "xmpp/test/xmpp_test_util.h"
#include "vr_types.h"

#include "xml/xml_pugi.h"

#include "controller/controller_peer.h" 
#include "controller/controller_export.h" 
#include "controller/controller_vrf_export.h" 
#include "controller/controller_types.h"

using namespace pugi;

void RouterIdDepInit(Agent *agent) {
}

class AgentBgpXmppPeerTest : public AgentXmppChannel {
public:
    AgentBgpXmppPeerTest(std::string xs, uint8_t xs_idx) :
        AgentXmppChannel(Agent::GetInstance(), xs, "0", xs_idx),
        rx_count_(0), rx_channel_event_queue_(
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

private:
    size_t rx_count_;
    WorkQueue<xmps::PeerState> rx_channel_event_queue_;
};

class ControlNodeMockBgpXmppPeer {
public:
    ControlNodeMockBgpXmppPeer() : channel_(NULL), rx_count_(0) {
    }

    void HandleXmppChannelEvent(XmppChannel *channel,
                                xmps::PeerState state) {
        if (state != xmps::READY) {
            // assert(channel_ && channel == channel_);
            channel->UnRegisterReceive(xmps::BGP);
            channel_ = NULL;
        } else {
            channel->RegisterReceive(xmps::BGP,
                    boost::bind(&ControlNodeMockBgpXmppPeer::ReceiveUpdate,
                                this, _1));
            channel_ = channel;
        }
    }

    void ReceiveUpdate(const XmppStanza::XmppMessage *msg) {
        rx_count_++;
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
    virtual ~ControlNodeMockBgpXmppPeer() { }
private:
    XmppChannel *channel_;
    size_t rx_count_;
};


class AgentXmppUnitTest : public ::testing::Test { 
protected:
    AgentXmppUnitTest() : thread_(&evm_) {}
 
    virtual void SetUp() {
        xs_p = new XmppServer(&evm_, XmppInit::kControlNodeJID);
        xs_s = new XmppServer(&evm_, XmppInit::kControlNodeJID);
        xc_p = new XmppClient(&evm_);
        xc_s = new XmppClient(&evm_);

        xs_p->Initialize(0, false);
        xs_s->Initialize(0, false);
        
        thread_.Start();
        XmppConnectionSetUp();
    }

    virtual void TearDown() {
        xc_p->ConfigUpdate(new XmppConfigData());
        xc_s->ConfigUpdate(new XmppConfigData());
        client->WaitForIdle();
        bgp_peer.reset(); 
        bgp_peer_s.reset(); 
        client->WaitForIdle();
        xc_p->Shutdown();
        xc_s->Shutdown();
        client->WaitForIdle();
        xs_p->Shutdown();
        client->WaitForIdle();
        xs_s->Shutdown();
        client->WaitForIdle();

        ShutdownAgentController(Agent::GetInstance());
        TcpServerManager::DeleteServer(xs_p);
        TcpServerManager::DeleteServer(xs_s);
        TcpServerManager::DeleteServer(xc_p);
        TcpServerManager::DeleteServer(xc_s);
        evm_.Shutdown();
        thread_.Join();
    }

    XmppChannelConfig *CreateXmppChannelCfg(const char *address, int port,
                                            const string &from, const string &to,
                                            bool isclient) {
        XmppChannelConfig *cfg = new XmppChannelConfig(isclient);
        cfg->endpoint.address(boost::asio::ip::address::from_string(address));
        cfg->endpoint.port(port);
        cfg->ToAddr = to;
        cfg->FromAddr = from;
        return cfg;
    }

    static void ValidateSandeshResponse(Sandesh *sandesh, vector<string> &result) {
        AgentXmppConnectionStatus *resp =
                dynamic_cast<AgentXmppConnectionStatus *>(sandesh);

        std::vector<AgentXmppData> &list =
                const_cast<std::vector<AgentXmppData>&>(resp->get_peer());

        cout << "*******************************************************"<<endl;
        for (size_t i = 0; i < list.size(); i++) {

            EXPECT_STREQ(list[i].controller_ip.c_str(), result[i*3].c_str());
            EXPECT_STREQ(list[i].cfg_controller.c_str(), result[(i*3)+1].c_str());
            EXPECT_STREQ(list[i].state.c_str(), result[(i*3)+2].c_str());

            cout << "Controller-IP:" << list[i].controller_ip << endl;
            cout << "Cfg-Controller-IP:" << list[i].cfg_controller << endl;
            cout << "State:" << list[i].state << endl;
        }
        cout << "*******************************************************"<<endl;

    }

    void SendDocument(const pugi::xml_document &xdoc, ControlNodeMockBgpXmppPeer *peer) {
        ostringstream oss;
        xdoc.save(oss);
        string msg = oss.str();
        uint8_t buf[4096];
        bzero(buf, sizeof(buf));
        memcpy(buf, msg.data(), msg.size());
        peer->SendUpdate(buf, msg.size());
    }

    xml_node MessageHeader(xml_document *xdoc, std::string vrf) {
        xml_node msg = xdoc->append_child("message");
        msg.append_attribute("type") = "set";
        msg.append_attribute("from") = XmppInit::kControlNodeJID;
        string str(XmppInit::kAgentNodeJID);
        str += "/";
        str += XmppInit::kBgpPeer;
        msg.append_attribute("to") = str.c_str();

        xml_node event = msg.append_child("event");
        event.append_attribute("xmlns") = "http://jabber.org/protocol/pubsub";
        xml_node xitems = event.append_child("items");
        stringstream node;
        node << BgpAf::IPv4 << "/" << BgpAf::Unicast << "/" << vrf.c_str();
        xitems.append_attribute("node") = node.str().c_str();
        return(xitems);
    }

    void SendRouteMessage(ControlNodeMockBgpXmppPeer *peer, std::string vrf,
                          std::string address, int label) {
        xml_document xdoc;
        xml_node xitems = MessageHeader(&xdoc, vrf);

        autogen::NextHopType item_nexthop;
        item_nexthop.af = BgpAf::IPv4;
        item_nexthop.address = Agent::GetInstance()->router_id().to_string();;
        item_nexthop.label = label;

        autogen::ItemType item;
        item.entry.next_hops.next_hop.push_back(item_nexthop);
        item.entry.nlri.af = BgpAf::IPv4;
        item.entry.nlri.safi = BgpAf::Unicast;
        item.entry.nlri.address = address.c_str();
        item.entry.version = 1;
        item.entry.virtual_network = "vn1";

        xml_node node = xitems.append_child("item");
        node.append_attribute("id") = address.c_str();
        item.Encode(&node);

        SendDocument(xdoc, peer);
    }

    void SendRouteDeleteMessage(ControlNodeMockBgpXmppPeer *peer, std::string vrf) {
        xml_document xdoc;
        xml_node xitems = MessageHeader(&xdoc, vrf);
        xml_node node = xitems.append_child("retract");
        node.append_attribute("id") = "1.1.1.1/32";

        SendDocument(xdoc, peer);
    }

    void XmppConnectionSetUp() {

        Agent::GetInstance()->controller()->increment_multicast_sequence_number();
        Agent::GetInstance()->set_cn_mcast_builder(NULL);
	//Create an xmpp client
	XmppConfigData *xmppc_p_cfg = new XmppConfigData;
	LOG(DEBUG, "Create an xmpp client connect to Server port " << xs_p->GetPort());
	xmppc_p_cfg->AddXmppChannelConfig(CreateXmppChannelCfg("127.0.0.1", 
					  xs_p->GetPort(),
					  XmppInit::kAgentNodeJID, 
					  XmppInit::kControlNodeJID, true));
	xc_p->ConfigUpdate(xmppc_p_cfg);
        cchannel_p = xc_p->FindChannel(XmppInit::kControlNodeJID); 
        //Create agent bgp peer
	bgp_peer.reset(new AgentBgpXmppPeerTest(
                       Agent::GetInstance()->controller_ifmap_xmpp_server(0), 0));
    bgp_peer.get()->RegisterXmppChannel(cchannel_p);
	xc_p->RegisterConnectionEvent(xmps::BGP,
	    boost::bind(&AgentBgpXmppPeerTest::HandleXmppChannelEvent, bgp_peer.get(), _2));
	Agent::GetInstance()->set_controller_xmpp_channel(bgp_peer.get(), 0);


	
	//Create control-node bgp mock peer 
	mock_peer.reset(new ControlNodeMockBgpXmppPeer());
	xs_p->RegisterConnectionEvent(xmps::BGP,
	    boost::bind(&ControlNodeMockBgpXmppPeer::HandleXmppChannelEvent, 
                        mock_peer.get(), _1, _2));
	// server connection
        WAIT_FOR(1000, 10000,
            ((sconnection = xs_p->FindConnection(XmppInit::kAgentNodeJID)) != NULL));
        assert(sconnection);

	//Create control-node bgp mock peer 
	mock_peer_s.reset(new ControlNodeMockBgpXmppPeer());
	xs_s->RegisterConnectionEvent(xmps::BGP,
	    boost::bind(&ControlNodeMockBgpXmppPeer::HandleXmppChannelEvent, 
                        mock_peer_s.get(), _1, _2));

        //Create an xmpp client
	XmppConfigData *xmppc_s_cfg = new XmppConfigData;
	LOG(DEBUG, "Create an xmpp client connect to Server port " << xs_s->GetPort());
	xmppc_s_cfg->AddXmppChannelConfig(CreateXmppChannelCfg("127.0.0.2", 
		    	                  xs_s->GetPort(),
					  XmppInit::kAgentNodeJID, 
					  XmppInit::kControlNodeJID, true));
	xc_s->ConfigUpdate(xmppc_s_cfg);
        cchannel_s = xc_s->FindChannel(XmppInit::kControlNodeJID);
        //Create agent bgp peer
	bgp_peer_s.reset(new AgentBgpXmppPeerTest(
                         Agent::GetInstance()->controller_ifmap_xmpp_server(1), 1));
    bgp_peer_s.get()->RegisterXmppChannel(cchannel_s);
	Agent::GetInstance()->set_controller_xmpp_channel(bgp_peer_s.get(), 1);
	xc_s->RegisterConnectionEvent(xmps::BGP,
	    boost::bind(&AgentBgpXmppPeerTest::HandleXmppChannelEvent, bgp_peer_s.get(), _2));

	// server connection
        WAIT_FOR(1000, 10000,
            ((sconnection_s = xs_s->FindConnection(XmppInit::kAgentNodeJID)) != NULL));
        assert(sconnection_s);
    }

    EventManager evm_;
    ServerThread thread_;
    XmppServer *xs_p;
    XmppServer *xs_s;
    XmppClient *xc_p;
    XmppClient *xc_s;

    XmppConnection *sconnection;
    XmppConnection *sconnection_s;
    XmppChannel *cchannel_p;
    XmppChannel *cchannel_s;

    auto_ptr<AgentBgpXmppPeerTest> bgp_peer;
    auto_ptr<AgentBgpXmppPeerTest> bgp_peer_s;
    auto_ptr<ControlNodeMockBgpXmppPeer> mock_peer;
    auto_ptr<ControlNodeMockBgpXmppPeer> mock_peer_s;
};


namespace {

TEST_F(AgentXmppUnitTest, Connection) {

    //wait for connection establishment
    WAIT_FOR(1000, 10000, (sconnection->GetStateMcState() == xmsm::ESTABLISHED));
    WAIT_FOR(1000, 10000, (cchannel_p->GetPeerState() == xmps::READY));
    WAIT_FOR(1000, 10000, (sconnection_s->GetStateMcState() == xmsm::ESTABLISHED));
    WAIT_FOR(1000, 10000, (cchannel_s->GetPeerState() == xmps::READY));

    client->Reset();
    client->WaitForIdle();

    //Mock Sandesh request
    AgentXmppConnectionStatusReq *xmpp_req = new AgentXmppConnectionStatusReq();
    std::vector<std::string> result;
    result.push_back("127.0.0.1");
    result.push_back("Yes");
    result.push_back("Established");
    result.push_back("127.0.0.2");
    result.push_back("No");
    result.push_back("Established");
    Sandesh::set_response_callback(boost::bind(ValidateSandeshResponse, _1, result));
    xmpp_req->HandleRequest();
    client->WaitForIdle();
    xmpp_req->Release();

    VxLanNetworkIdentifierMode(false);
	client->WaitForIdle();
    // Create vm-port and vn
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
    };

    //expect subscribe to __default__
    uint8_t n = 1;
    uint8_t n_s = 1;
    WAIT_FOR(1000, 10000, (mock_peer.get()->Count() == n));
    WAIT_FOR(1000, 10000, (mock_peer_s.get()->Count() == n_s));

    VrfAddReq("vrf2");
    VnAddReq(2, "vn2", 0, "vrf2");
    n++;  
    n_s++; 
    //expect subscribe vrf2 message at the mock server
    WAIT_FOR(1000, 10000, (mock_peer.get()->Count() == n));
    //expect subscribe vrf2 message at the secondary mock server
    WAIT_FOR(1000, 10000, (mock_peer_s.get()->Count() == n_s));

    //Create vm-port and route entry in vrf1 
    CreateVmportEnv(input, 1);
    client->WaitForIdle();

    Ip4Address addr = Ip4Address::from_string("1.1.1.1");
    EXPECT_TRUE(VmPortActive(input, 0));
    EXPECT_TRUE(RouteFind("vrf1", addr, 32));
    Inet4UnicastRouteEntry *rt = RouteGet("vrf1", addr, 32);
    EXPECT_STREQ(rt->dest_vn_name().c_str(), "vn1");
    //ensure active path is local-vm
    EXPECT_TRUE(rt->GetActivePath()->peer()->GetType() 
                == Peer::LOCAL_VM_PORT_PEER);

    n++; n++; n++; n++; n++;
    n_s++; n_s++; n_s++;
    //expect subscribe vrf1 ,vm route at the mock server
    WAIT_FOR(1000, 10000, (mock_peer.get()->Count() == n));
    //expect subscribe vrf1, vm route at the mock secondary server
    WAIT_FOR(1000, 10000, (mock_peer_s.get()->Count() == n_s));


    // Send route-reflect, back to vrf1
    SendRouteMessage(mock_peer.get(), "vrf1", "1.1.1.1/32",
                     MplsTable::kStartLabel);
    // Route reflected to vrf1
    WAIT_FOR(1000, 10000, (bgp_peer.get()->Count() == 1));
    //ensure active path is local-vm
    EXPECT_TRUE(rt->GetActivePath()->peer()->GetType() 
                == Peer::BGP_PEER);

    // Send route, leak to vrf2
    SendRouteMessage(mock_peer.get(), "vrf2", "1.1.1.1/32",
                     MplsTable::kStartLabel);
    // Route reflected to vrf2
    WAIT_FOR(1000, 10000, (bgp_peer.get()->Count() == 2));

    // Route leaked to vrf2, check entry in route-table
    WAIT_FOR(1000, 10000, (RouteFind("vrf2", addr, 32) == true));
    Inet4UnicastRouteEntry *rt2 = RouteGet("vrf2", addr, 32);
    WAIT_FOR(1000, 10000, rt2->GetActivePath() != NULL);
    WAIT_FOR(1000, 10000, rt2->dest_vn_name().size() > 0);
    EXPECT_STREQ(rt2->dest_vn_name().c_str(), "vn1");
    //check paths
    ASSERT_TRUE(rt2->FindPath(bgp_peer->bgp_peer_id()) != NULL);


    // Send route-reflect, back to vrf1 from secondary control-node
    SendRouteMessage(mock_peer_s.get(), "vrf1", "1.1.1.1/32",
                     MplsTable::kStartLabel);
    // Route reflected to vrf1
    WAIT_FOR(1000, 10000, (bgp_peer_s.get()->Count() == 0));

    // Send route, leak to vrf2 from secondary control-node
    SendRouteMessage(mock_peer_s.get(), "vrf2", "1.1.1.1/32",
                     MplsTable::kStartLabel);
    // Route reflected to vrf2
    WAIT_FOR(1000, 10000, (bgp_peer_s.get()->Count() == 2));

    // Route leaked to vrf2, check entry in route-table, check paths
    WAIT_FOR(1000, 10000, (rt2->FindPath(bgp_peer_s->bgp_peer_id()) != NULL));
    client->WaitForIdle();
    
    //ensure active path is local-vm
    EXPECT_TRUE(rt->GetActivePath()->peer()->GetType() 
                == Peer::BGP_PEER);

    //Delete vm-port and route entry in vrf1
    IntfCfgDel(input, 0);
    VmDelReq(1);
    client->WaitForIdle();
    // Route delete   
    n++; n_s++; 
    n++; n_s++;
    n++;
    n++;
    WAIT_FOR(1000, 10000, (mock_peer.get()->Count() == n));
    WAIT_FOR(1000, 10000, (mock_peer_s.get()->Count() == n_s));

    //Send route-reflect delete
    SendRouteDeleteMessage(mock_peer.get(), "vrf1");
    // Route delete for vrf1 
    WAIT_FOR(1000, 10000, (bgp_peer.get()->Count() == 3));

    //Send route-leak delete
    SendRouteDeleteMessage(mock_peer.get(), "vrf2");
    // Route delete for vrf2 
    WAIT_FOR(1000, 10000, (bgp_peer.get()->Count() == 4));

    Inet4UnicastRouteEntry *rt4 = RouteGet("vrf1", addr, 32);
    EXPECT_STREQ(rt4->dest_vn_name().c_str(), "vn1");
    //check paths
    ASSERT_TRUE(rt4->FindPath(bgp_peer->bgp_peer_id()) == NULL);

    //Send route-reflect delete from seconday control-node
    SendRouteDeleteMessage(mock_peer_s.get(), "vrf1");
    // Route delete for vrf1 
    WAIT_FOR(1000, 10000, (bgp_peer_s.get()->Count() == 3));

    //Send route-leak delete
    SendRouteDeleteMessage(mock_peer_s.get(), "vrf2");
    // Route delete for vrf2 
    WAIT_FOR(1000, 10000, (bgp_peer_s.get()->Count() == 4));

    client->WaitForIdle();

    //No more delete messages to control-node
    EXPECT_TRUE(mock_peer.get()->Count() == n);
    EXPECT_TRUE(mock_peer_s.get()->Count() == n_s);

    //Confirm route has been cleaned up
    WAIT_FOR(1000, 10000, (RouteFind("vrf1", addr, 32) == false));
    WAIT_FOR(1000, 10000, (RouteFind("vrf2", addr, 32) == false));
    //Confirm Vmport is deleted
    EXPECT_FALSE(VmPortFind(input, 0));

    DeleteVmportEnv(input, 1, true);
    client->WaitForIdle();
    VrfDelReq("vrf2");
    client->WaitForIdle();

    VnDelReq(1);
    client->WaitForIdle();
    EXPECT_EQ(1U, Agent::GetInstance()->vn_table()->Size());
    VnDelReq(2);
    client->WaitForIdle();
    EXPECT_EQ(0U, Agent::GetInstance()->vn_table()->Size());

    EXPECT_FALSE(DBTableFind("vrf1.route.0"));
    EXPECT_FALSE(VrfFind("vrf1"));
    EXPECT_FALSE(DBTableFind("vrf2.route.0"));
    EXPECT_FALSE(VrfFind("vrf2"));

    xc_p->ConfigUpdate(new XmppConfigData());
    client->WaitForIdle();
    xc_s->ConfigUpdate(new XmppConfigData());
    client->WaitForIdle();

}

TEST_F(AgentXmppUnitTest, CfgServerSelection) {

    //wait for connection establishment
    WAIT_FOR(1000, 10000, (sconnection->GetStateMcState() == xmsm::ESTABLISHED));
    WAIT_FOR(1000, 10000, (cchannel_p->GetPeerState() == xmps::READY));
    WAIT_FOR(1000, 10000, (sconnection_s->GetStateMcState() == xmsm::ESTABLISHED));
    WAIT_FOR(1000, 10000, (cchannel_s->GetPeerState() == xmps::READY));

    client->Reset();
    client->WaitForIdle();

    WAIT_FOR(1000, 10000, (Agent::GetInstance()->ifmap_active_xmpp_server().empty() == false));
    if (Agent::GetInstance()->ifmap_active_xmpp_server().compare(Agent::GetInstance()->controller_ifmap_xmpp_server(0)) == 0) {
        //bring-down the channel
        bgp_peer.get()->HandleXmppChannelEvent(xmps::NOT_READY);
        client->WaitForIdle();
        EXPECT_TRUE(Agent::GetInstance()->ifmap_active_xmpp_server().
                    compare(Agent::GetInstance()->controller_ifmap_xmpp_server(1)) == 0);
    } else {
        //bring-down the channel
        bgp_peer_s.get()->HandleXmppChannelEvent(xmps::NOT_READY);
        client->WaitForIdle();
        EXPECT_TRUE(Agent::GetInstance()->ifmap_active_xmpp_server().
                    compare(Agent::GetInstance()->controller_ifmap_xmpp_server(0)) == 0);
    }

    //Mock Sandesh request
    AgentXmppConnectionStatusReq *xmpp_req = new AgentXmppConnectionStatusReq();
    std::vector<std::string> result;
    result.push_back("127.0.0.1");
    result.push_back("No");
    result.push_back("Established");
    result.push_back("127.0.0.2");
    result.push_back("Yes");
    result.push_back("Established");
    Sandesh::set_response_callback(boost::bind(ValidateSandeshResponse, _1, result));
    xmpp_req->HandleRequest();
    client->WaitForIdle();
    xmpp_req->Release();

 
    xc_p->ConfigUpdate(new XmppConfigData());
    client->WaitForIdle();
    xc_s->ConfigUpdate(new XmppConfigData());
    client->WaitForIdle();

}
}

