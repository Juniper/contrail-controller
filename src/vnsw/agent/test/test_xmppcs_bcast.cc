/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "testing/gunit.h"

#include <net/bgp_af.h>
#include <pugixml/pugixml.hpp>

#include <base/logging.h>
#include <boost/bind.hpp>
#include <tbb/task.h>
#include <base/task.h>
#include <base/util.h>
#include "io/test/event_manager_test.h"

#include <cmn/agent_cmn.h>

#include "cfg/cfg_init.h"
#include "cfg/cfg_interface.h"
#include "oper/operdb_init.h"
#include "controller/controller_init.h"
#include "controller/controller_ifmap.h"
#include "pkt/pkt_init.h"
#include "services/services_init.h"
#include "ksync/ksync_init.h"
#include "oper/interface_common.h"
#include "oper/nexthop.h"
#include "route/route.h"
#include "oper/vrf.h"
#include "oper/mpls.h"
#include "oper/vm.h"
#include "oper/vn.h"
#include "oper/peer.h"
#include "openstack/instance_service_server.h"
#include "test_cmn_util.h"
#include "xmpp/xmpp_init.h"
#include "xmpp/test/xmpp_test_util.h"
#include "vr_types.h"

#include "xmpp_multicast_types.h"
#include "xml/xml_pugi.h"

#include "controller/controller_peer.h" 
#include "controller/controller_export.h" 
#include "controller/controller_vrf_export.h" 

using namespace pugi;

void RouterIdDepInit(Agent *agent) {
}

// Create vm-port and vn
struct PortInfo input[] = {
    {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
    {"vnet2", 2, "1.1.1.2", "00:00:00:02:02:02", 1, 2},
};

class AgentBgpXmppPeerTest : public AgentXmppChannel {
public:
    AgentBgpXmppPeerTest(XmppChannel *channel, std::string xs, 
                         std::string lr, uint8_t xs_idx) :
        AgentXmppChannel(Agent::GetInstance(), channel, xs, lr, xs_idx), 
        rx_count_(0), rx_channel_event_queue_ (
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
        xs_p->Shutdown();
        bgp_peer.reset(); 
        client->WaitForIdle();
        xs_s->Shutdown();
        bgp_peer_s.reset(); 
        client->WaitForIdle();

        xc_p->Shutdown();
        client->WaitForIdle();
        xc_s->Shutdown();
        client->WaitForIdle();

        TaskScheduler::GetInstance()->Stop();
        Agent::GetInstance()->controller()->unicast_cleanup_timer().cleanup_timer_->Fire();
        TaskScheduler::GetInstance()->Start();
        client->WaitForIdle();
        Agent::GetInstance()->controller()->Cleanup();
        client->WaitForIdle();
 
        mock_peer.reset();
        mock_peer_s.reset();


        TcpServerManager::DeleteServer(xs_p);
        TcpServerManager::DeleteServer(xs_s);
        TcpServerManager::DeleteServer(xc_p);
        TcpServerManager::DeleteServer(xc_s);
        evm_.Shutdown();
        thread_.Join();
        client->WaitForIdle();
    }

    XmppChannelConfig *CreateXmppChannelCfg(const char *address, int port,
                                            const char *local_address,
                                            const string &from, const string &to,
                                            bool isclient) {
        XmppChannelConfig *cfg = new XmppChannelConfig(isclient);
        cfg->endpoint.address(boost::asio::ip::address::from_string(address));
        cfg->local_endpoint.address(boost::asio::ip::address::from_string(local_address));
        cfg->endpoint.port(port);
        cfg->ToAddr = to;
        cfg->FromAddr = from;
        return cfg;
    }

    int GetStartLabel_XmppServer(uint8_t idx) {
        vector<int> entries;
        assert(stringToIntegerList(Agent::GetInstance()->multicast_label_range(idx), "-",
                                   entries));
        assert(entries.size() > 0);
        return entries[0];
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

    xml_node MessageHeader(xml_document *xdoc, std::string vrf, bool bcast) {
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
        stringstream ss;
        if (bcast) {
            ss << BgpAf::IPv4 << "/" << BgpAf::Mcast << "/" << vrf.c_str();
        } else {
            ss << BgpAf::IPv4 << "/" << BgpAf::Unicast << "/" << vrf.c_str();
        }
        std::string node_str(ss.str());
        xitems.append_attribute("node") = node_str.c_str();
        return(xitems);
    }

    void SendRouteMessage(ControlNodeMockBgpXmppPeer *peer, std::string vrf,
                          std::string address, int label) {
        xml_document xdoc;
        xml_node xitems = MessageHeader(&xdoc, vrf, false);

        autogen::NextHopType item_nexthop;
        item_nexthop.af = BgpAf::IPv4;
        item_nexthop.address = Agent::GetInstance()->router_id().to_string();;
        item_nexthop.label = label;
        
        autogen::ItemType item;
        item.entry.next_hops.next_hop.push_back(item_nexthop);
        item.entry.nlri.af = BgpAf::IPv4;
        item.entry.nlri.address = address.c_str();
        item.entry.version = 1;
        item.entry.virtual_network = "vn1";

        xml_node node = xitems.append_child("item");
        node.append_attribute("id") = address.c_str();
        item.Encode(&node);

        SendDocument(xdoc, peer);
    }

    void SendRouteDeleteMessage(ControlNodeMockBgpXmppPeer *peer, 
                                std::string address, std::string vrf) {
        xml_document xdoc;
        xml_node xitems = MessageHeader(&xdoc, vrf, false);
        xml_node node = xitems.append_child("retract");
        node.append_attribute("id") = address.c_str();

        SendDocument(xdoc, peer);
    }

    void SendBcastRouteMessage(ControlNodeMockBgpXmppPeer *peer, std::string vrf,
                               std::string subnet_addr, int src_label, 
                               std::string agent_addr, 
                               int dest_label) {
        xml_document xdoc;
        xml_node xitems = MessageHeader(&xdoc, vrf, true);

        autogen::McastItemType item;
        item.entry.nlri.af = BgpAf::IPv4;
        item.entry.nlri.safi = BgpAf::Mcast;
        item.entry.nlri.group = subnet_addr.c_str();
        item.entry.nlri.source = "0.0.0.0";
        item.entry.nlri.source_label = src_label; //label allocated by control-node

        autogen::McastNextHopType nh;
        nh.af = item.entry.nlri.af;
        nh.address = "127.0.0.2"; // agent-b, does not exist
        stringstream label;
        label << dest_label;
        nh.label = label.str(); 

        //Add to olist
        item.entry.olist.next_hop.push_back(nh);

        xml_node node = xitems.append_child("item");
        //Route-Distinguisher
        stringstream ss;
        ss << agent_addr.c_str() << ":" << "65535:" << subnet_addr.c_str(); 
        string node_str(ss.str());
        node.append_attribute("id") = node_str.c_str();
        item.Encode(&node);

        SendDocument(xdoc, peer);
    }

    void SendBcastRouteMessage(ControlNodeMockBgpXmppPeer *peer, std::string vrf,
                               std::string subnet_addr, int src_label, 
                               std::string agent_addr, 
                               int dest_label1, int dest_label2) {
        xml_document xdoc;
        xml_node xitems = MessageHeader(&xdoc, vrf, true);

        autogen::McastItemType item;
        item.entry.nlri.af = BgpAf::IPv4;
        item.entry.nlri.safi = BgpAf::Mcast;
        item.entry.nlri.group = subnet_addr.c_str();
        item.entry.nlri.source = "0.0.0.0";
        item.entry.nlri.source_label = src_label; //label allocated by control-node

        autogen::McastNextHopType nh;
        nh.af = item.entry.nlri.af;
        nh.address = "127.0.0.2"; // agent-b, does not exist
        stringstream label1;
        label1 << dest_label1;
        nh.label = label1.str(); 

        //Add to olist
        item.entry.olist.next_hop.push_back(nh);

        autogen::McastNextHopType nh2;
        nh2.af = item.entry.nlri.af;
        nh2.address = "127.0.0.3"; // agent-c, does not exist
        stringstream label2;
        label2 << dest_label2;
        nh2.label = label2.str(); 

        //Add to olist
        item.entry.olist.next_hop.push_back(nh2);

        xml_node node = xitems.append_child("item");
        //Route-Distinguisher
        stringstream ss;
        ss << agent_addr.c_str() << ":" << "65535:" << subnet_addr.c_str(); 
        string node_str(ss.str());
        node.append_attribute("id") = node_str.c_str();
        item.Encode(&node);

        SendDocument(xdoc, peer);
    }
    void SendBcastRouteDelete(ControlNodeMockBgpXmppPeer *peer, std::string vrf,
                              std::string addr, std::string agent_addr) {
        xml_document xdoc;
        xml_node xitems = MessageHeader(&xdoc, vrf, true);
        xml_node node = xitems.append_child("retract");
        //Route-Distinguisher
        stringstream ss;
        ss << agent_addr.c_str() << ":" << "65535:" << addr.c_str() 
                                        << "," << "0.0.0.0"; 
        string node_str(ss.str());
        node.append_attribute("id") = node_str.c_str();

        SendDocument(xdoc, peer);
    }


    void XmppConnectionSetUp() {

        Agent::GetInstance()->controller()->increment_multicast_sequence_number();
        Agent::GetInstance()->set_cn_mcast_builder(NULL);

        //Create control-node bgp mock peer 
	mock_peer.reset(new ControlNodeMockBgpXmppPeer());
	xs_p->RegisterConnectionEvent(xmps::BGP,
	    boost::bind(&ControlNodeMockBgpXmppPeer::HandleXmppChannelEvent, 
                        mock_peer.get(), _1, _2));
	//Create an xmpp client
	XmppConfigData *xmppc_p_cfg = new XmppConfigData;
	LOG(DEBUG, "Create an xmpp client connect to Server port " << xs_p->GetPort());
	xmppc_p_cfg->AddXmppChannelConfig(CreateXmppChannelCfg( 
					  "127.0.0.2", xs_p->GetPort(),
					  "127.0.0.1", "agent-a" ,
					  XmppInit::kControlNodeJID, true));
	xc_p->ConfigUpdate(xmppc_p_cfg);
        cchannel_p = xc_p->FindChannel(XmppInit::kControlNodeJID); 
        //Create agent bgp peer
	bgp_peer.reset(new AgentBgpXmppPeerTest(cchannel_p,
                       Agent::GetInstance()->controller_ifmap_xmpp_server(0), 
                       Agent::GetInstance()->multicast_label_range(0), 0));
	Agent::GetInstance()->set_controller_xmpp_channel(bgp_peer.get(), 0);
	xc_p->RegisterConnectionEvent(xmps::BGP,
	    boost::bind(&AgentBgpXmppPeerTest::HandleXmppChannelEvent, bgp_peer.get(), _2));
	
	// server connection
        WAIT_FOR(1000, 10000,
            ((sconnection = xs_p->FindConnection("agent-a")) != NULL));
        assert(sconnection);

	//Create control-node bgp mock peer 
	mock_peer_s.reset(new ControlNodeMockBgpXmppPeer());
	xs_s->RegisterConnectionEvent(xmps::BGP,
	    boost::bind(&ControlNodeMockBgpXmppPeer::HandleXmppChannelEvent, 
                        mock_peer_s.get(), _1, _2));
        //Create an xmpp client
	XmppConfigData *xmppc_s_cfg = new XmppConfigData;
	LOG(DEBUG, "Create an xmpp client connect to Server port " << xs_s->GetPort());
	xmppc_s_cfg->AddXmppChannelConfig(CreateXmppChannelCfg( 
		    	                  "127.0.0.1", xs_s->GetPort(),
					  "127.0.0.1", "agent-bb", 
					  XmppInit::kControlNodeJID, true));
	xc_s->ConfigUpdate(xmppc_s_cfg);
        cchannel_s = xc_s->FindChannel(XmppInit::kControlNodeJID);
        //Create agent bgp peer
	bgp_peer_s.reset(new AgentBgpXmppPeerTest(cchannel_s,
                         Agent::GetInstance()->controller_ifmap_xmpp_server(1), 
                         Agent::GetInstance()->multicast_label_range(1), 1));
	Agent::GetInstance()->set_controller_xmpp_channel(bgp_peer_s.get(), 1);
	xc_s->RegisterConnectionEvent(xmps::BGP,
	    boost::bind(&AgentBgpXmppPeerTest::HandleXmppChannelEvent, bgp_peer_s.get(), _2));

	// server connection
        WAIT_FOR(1000, 10000,
            ((sconnection_s = xs_s->FindConnection("agent-bb")) != NULL));
        assert(sconnection_s);

        XmppSubnetSetUp();
    }

    void XmppSubnetSetUp() {

	//wait for connection establishment
        WAIT_FOR(1000, 10000, (sconnection->GetStateMcState() == xmsm::ESTABLISHED));
        WAIT_FOR(1000, 10000, (cchannel_p->GetPeerState() == xmps::READY));
        WAIT_FOR(1000, 10000, (sconnection_s->GetStateMcState() == xmsm::ESTABLISHED));
        WAIT_FOR(1000, 10000, (cchannel_s->GetPeerState() == xmps::READY));

	//expect subscribe for __default__ at the mock server
	WAIT_FOR(1000, 10000, (mock_peer.get()->Count() == 1));
	WAIT_FOR(1000, 10000, (mock_peer_s.get()->Count() == 1));

	//IpamInfo for subnet address belonging to vn
	IpamInfo ipam_info[] = {
	    {"1.1.1.0", 24, "1.1.1.200", true}
	};
	
	client->Reset();

    VxLanNetworkIdentifierMode(false);
	client->WaitForIdle();

    CreateVmportEnv(input, 2, 0);
	client->WaitForIdle();

	client->Reset();
    AddIPAM("vn1", ipam_info, 1);
	client->WaitForIdle(5);

	// expect subscribe message + 2 VM v4 routes + 2 VM l2 routes + subnet bcast +
	// bcast route at the mock server
	WAIT_FOR(1000, 10000, (mock_peer_s.get()->Count() == 9));
	// expect subscribe message + 2 VM v4 routes at the mock server +
	// 2 VM l2 routes + subnet + bcast
	WAIT_FOR(1000, 10000, (mock_peer.get()->Count() == 6));

	Ip4Address addr = Ip4Address::from_string("1.1.1.1");
	EXPECT_TRUE(VmPortActive(input, 0));
	EXPECT_TRUE(RouteFind("vrf1", addr, 32));
	Inet4UnicastRouteEntry *rt = RouteGet("vrf1", addr, 32);
	EXPECT_STREQ(rt->dest_vn_name().c_str(), "vn1");

	// Send route, back to vrf1
	SendRouteMessage(mock_peer_s.get(), "vrf1", "1.1.1.1/32", 
			 MplsTable::kStartLabel);
	// Route reflected to vrf1
	WAIT_FOR(1000, 10000, (bgp_peer_s.get()->Count() == 1));

        // Unicast route will be reflected by mock_peer

	addr = Ip4Address::from_string("1.1.1.2");
	EXPECT_TRUE(VmPortActive(input, 1));
	EXPECT_TRUE(RouteFind("vrf1", addr, 32));
	rt = RouteGet("vrf1", addr, 32);
        WAIT_FOR(1000, 10000, (rt->GetActivePath() != NULL));
        WAIT_FOR(1000, 10000, rt->dest_vn_name().size() > 0);
	EXPECT_STREQ(rt->dest_vn_name().c_str(), "vn1");

	// Send route, back to vrf1
	SendRouteMessage(mock_peer_s.get(), "vrf1", "1.1.1.2/32", 
			 MplsTable::kStartLabel+1);
	// Route reflected to vrf1
	WAIT_FOR(1000, 10000, (bgp_peer_s.get()->Count() == 2));

        // Unicast route will be reflected by mock_peer 

	//verify presence of subnet broadcast route
	addr = Ip4Address::from_string("1.1.1.255");
	EXPECT_TRUE(RouteFind("vrf1", addr, 32));
	rt = RouteGet("vrf1", addr, 32);

	//Send bcast route with allocated label from control-node
	//Assume agent-b sent a group join, hence the route
	//with label allocated and olist
        int alloc_label = GetStartLabel_XmppServer(1);
	SendBcastRouteMessage(mock_peer_s.get(), "vrf1",
			      "1.1.1.255", alloc_label,  
                              "127.0.0.4", alloc_label+10);
	// Bcast Route with updated olist 
	WAIT_FOR(1000, 10000, (bgp_peer_s.get()->Count() == 3));

	NextHop *nh = const_cast<NextHop *>(rt->GetActiveNextHop());
	CompositeNH *cnh = static_cast<CompositeNH *>(nh);
        MulticastGroupObject *obj;
	ASSERT_TRUE(nh != NULL);
	ASSERT_TRUE(nh->GetType() == NextHop::COMPOSITE);
	obj = MulticastHandler::GetInstance()->FindGroupObject("vrf1", addr);
	WAIT_FOR(1000, 1000, (obj->GetSourceMPLSLabel() != 0));
    WAIT_FOR(1000, 10000, (cnh->ComponentNHCount() == 3));

	//Verify mpls table
	MplsLabel *mpls = 
	    Agent::GetInstance()->mpls_table()->FindMplsLabel(alloc_label);
	ASSERT_TRUE(mpls == NULL);
    WAIT_FOR(1000, 10000, (Agent::GetInstance()->mpls_table()->Size() == 5));
	ASSERT_TRUE(Agent::GetInstance()->mpls_table()->Size() == 5);

	// Verify presence of all broadcast route in mcast table
	addr = Ip4Address::from_string("255.255.255.255");
	ASSERT_TRUE(MCRouteFind("vrf1", addr));
	Inet4MulticastRouteEntry *rt_m = MCRouteGet("vrf1", addr);
	
	//Send All bcast route
	SendBcastRouteMessage(mock_peer_s.get(), "vrf1",
			      "255.255.255.255", alloc_label+1,  
                              "127.0.0.4", alloc_label + 11);
	// Bcast Route with updated olist
	WAIT_FOR(1000, 10000, (bgp_peer_s.get()->Count() == 4));

	nh = const_cast<NextHop *>(rt_m->GetActiveNextHop());
	ASSERT_TRUE(nh != NULL);
	ASSERT_TRUE(nh->GetType() == NextHop::COMPOSITE);
	cnh = static_cast<CompositeNH *>(nh);
	obj = MulticastHandler::GetInstance()->FindGroupObject("vrf1", addr);
	WAIT_FOR(1000, 1000, (obj->GetSourceMPLSLabel() != 0));
    ASSERT_TRUE(cnh->ComponentNHCount() == 3);

	//Verify mpls table
	WAIT_FOR(1000, 1000, (Agent::GetInstance()->mpls_table()->
                          FindMplsLabel(alloc_label+ 1) == NULL));
	WAIT_FOR(1000, 1000, (Agent::GetInstance()->mpls_table()->Size() == 6));
    }

    void XmppSubnetTearDown() {
        client->Reset();
        DelIPAM("vn1");
        client->WaitForIdle();
        client->Reset();
        DeleteVmportEnv(input, 2, 1, 0);
        client->WaitForIdle();
        client->Reset();
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

TEST_F(AgentXmppUnitTest, SubnetBcast_Test_FailOver) {

    client->Reset();
    client->WaitForIdle();
 
    AgentXmppChannel *ch = Agent::GetInstance()->mulitcast_builder();
    EXPECT_TRUE(ch != NULL);
    EXPECT_TRUE(ch->controller_ifmap_xmpp_server().size() != 0);
    EXPECT_STREQ(ch->controller_ifmap_xmpp_server().c_str(), "127.0.0.1");

    //bring-down the channel, which is the elected
    //multicast tree builder (i.e 127.0.0.1)
    bgp_peer_s.get()->AgentBgpXmppPeerTest::HandleXmppChannelEvent(xmps::NOT_READY); 
    WAIT_FOR(1000, 10000, (Agent::GetInstance()->
                           mulitcast_builder() != NULL));
    client->WaitForIdle();

    ch = Agent::GetInstance()->mulitcast_builder();
    EXPECT_TRUE(ch != NULL);
    EXPECT_TRUE(ch->controller_ifmap_xmpp_server().size() != 0);
    EXPECT_STREQ(ch->controller_ifmap_xmpp_server().c_str(), "127.0.0.2");

    //ensure route learnt via control-node is cleaned/updated 
    Ip4Address addr = Ip4Address::from_string("1.1.1.255");
    ASSERT_TRUE(RouteFind("vrf1", addr, 32));
    Inet4UnicastRouteEntry *rt=RouteGet("vrf1", addr, 32);
    NextHop *nh = const_cast<NextHop *>(rt->GetActiveNextHop());
    ASSERT_TRUE(nh != NULL);
    ASSERT_TRUE(nh->GetType() == NextHop::COMPOSITE);
    CompositeNH *cnh = static_cast<CompositeNH *>(nh);
    if (Agent::GetInstance()->headless_agent_mode()) {
        ASSERT_TRUE(cnh->ComponentNHCount() == 3);
    } else {
        ASSERT_TRUE(cnh->ComponentNHCount() == 2);
    }

    //ensure route learnt via control-node is cleaned/updated 
    addr = Ip4Address::from_string("255.255.255.255");
    ASSERT_TRUE(MCRouteFind("vrf1", addr));
    Inet4MulticastRouteEntry *rt_m = MCRouteGet("vrf1", addr);
    nh = const_cast<NextHop *>(rt_m->GetActiveNextHop());
    ASSERT_TRUE(nh != NULL);
    ASSERT_TRUE(nh->GetType() == NextHop::COMPOSITE);
    cnh = static_cast<CompositeNH *>(nh);
    if (Agent::GetInstance()->headless_agent_mode()) {
        ASSERT_TRUE(cnh->ComponentNHCount() == 3);
    } else {
        ASSERT_TRUE(cnh->ComponentNHCount() == 2);
    }

    //Verify label deallocated from Mpls Table
    if (Agent::GetInstance()->headless_agent_mode()) {
        EXPECT_TRUE(Agent::GetInstance()->mpls_table()->Size() == 6);
    } else {
        EXPECT_TRUE(Agent::GetInstance()->mpls_table()->Size() == 4);
    }
    // headless
    //EXPECT_TRUE(Agent::GetInstance()->mpls_table()->Size() == 4);

    //expect subnet and all braodcast routes to newly elected
    //multicast builder
    if (Agent::GetInstance()->headless_agent_mode()) {
        WAIT_FOR(1000, 10000, (mock_peer.get()->Count() == 9));
    } else {
        //3 notification for fabric member modification
        //3 notification for same route, during walk
        WAIT_FOR(1000, 10000, (mock_peer.get()->Count() == 12));
    }

    //we could add olists from mock_peer due to
    //local-vms on another agent attached to mock_peer


    //Now bring-up the multicast tree builder with
    //the lower-ip
    bgp_peer_s.get()->AgentBgpXmppPeerTest::HandleXmppChannelEvent(xmps::READY); 
    WAIT_FOR(1000, 10000, (Agent::GetInstance()->
                           mulitcast_builder() != NULL));
    client->WaitForIdle();

    ch = Agent::GetInstance()->mulitcast_builder();
    EXPECT_TRUE(ch != NULL);
    EXPECT_TRUE(ch->controller_ifmap_xmpp_server().size() != 0);
    EXPECT_STREQ(ch->controller_ifmap_xmpp_server().c_str(), "127.0.0.1");

    //expect dissociate to the older peer, 127.0.0.2
    if (Agent::GetInstance()->headless_agent_mode()) {
        WAIT_FOR(1000, 10000, (mock_peer.get()->Count() == 12));
    } else {
        WAIT_FOR(1000, 10000, (mock_peer.get()->Count() == 15));
    }

    //expect subscribe, 2VM routes, 
    //subnet and all braodcast routes to newly elected
    //multicast builder
    WAIT_FOR(1000, 10000, (mock_peer_s.get()->Count() == 18));

    //bring-down non multicast builder
    bgp_peer.get()->AgentBgpXmppPeerTest::HandleXmppChannelEvent(xmps::NOT_READY);
    WAIT_FOR(1000, 10000, (Agent::GetInstance()->
                           mulitcast_builder() != NULL));
    client->WaitForIdle();

    //multicast builder should be unchanged
    ch = Agent::GetInstance()->mulitcast_builder();
    EXPECT_TRUE(ch != NULL);
    EXPECT_TRUE(ch->controller_ifmap_xmpp_server().size() != 0);
    EXPECT_STREQ(ch->controller_ifmap_xmpp_server().c_str(), "127.0.0.1");

    //expect no messages except config subscribe
    //control-node as 127.0.0.2 came up first
    WAIT_FOR(1000, 10000, (mock_peer_s.get()->Count() == 18));


    //bring-up non multicast builder
    bgp_peer.get()->AgentBgpXmppPeerTest::HandleXmppChannelEvent(xmps::READY); 
    WAIT_FOR(1000, 10000, (Agent::GetInstance()->
                           mulitcast_builder() != NULL));
    client->WaitForIdle(5);

    //multicast builder should be unchanged
    ch = Agent::GetInstance()->mulitcast_builder();
    EXPECT_TRUE(ch != NULL);
    EXPECT_TRUE(ch->controller_ifmap_xmpp_server().size() != 0);
    EXPECT_STREQ(ch->controller_ifmap_xmpp_server().c_str(), "127.0.0.1");

    //expect subscribe + 2VM routes
    if (Agent::GetInstance()->headless_agent_mode()) {
        WAIT_FOR(1000, 10000, (mock_peer.get()->Count() == 18));
    } else {
        WAIT_FOR(1000, 10000, (mock_peer.get()->Count() == 21));
    }

    //cleanup all config links via config
    XmppSubnetTearDown();
    client->WaitForIdle();

    xc_p->ConfigUpdate(new XmppConfigData());
    client->WaitForIdle(5);
    xc_s->ConfigUpdate(new XmppConfigData());
    client->WaitForIdle(5);
}


TEST_F(AgentXmppUnitTest, SubnetBcast_DBWalk_Cancel) {

    client->Reset();
    client->WaitForIdle();

    //bring-down the channel, which is the elected
    //multicast tree builder (i.e 127.0.0.1)
    bgp_peer_s.get()->AgentBgpXmppPeerTest::HandleXmppChannelEvent(xmps::NOT_READY); 
    client->WaitForIdle();

    bgp_peer_s.get()->AgentBgpXmppPeerTest::HandleXmppChannelEvent(xmps::READY); 
    client->WaitForIdle();

    //cleanup all config links via config
    XmppSubnetTearDown();
    client->WaitForIdle(10);

    xc_p->ConfigUpdate(new XmppConfigData());
    client->WaitForIdle(5);
    xc_s->ConfigUpdate(new XmppConfigData());
    client->WaitForIdle(5);

}

}

