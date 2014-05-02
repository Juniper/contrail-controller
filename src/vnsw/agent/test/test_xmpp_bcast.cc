/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */
#include "testing/gunit.h"

#include <pugixml/pugixml.hpp>

#include <net/bgp_af.h>
#include "io/test/event_manager_test.h"
#include "test_cmn_util.h"
#include "xmpp/xmpp_init.h"
#include "xmpp/test/xmpp_test_util.h"
#include "vr_types.h"
#include "openstack/instance_service_server.h"

#include "xmpp_multicast_types.h"
#include "xml/xml_pugi.h"

#include "controller/controller_init.h"
#include "controller/controller_ifmap.h"
#include "controller/controller_peer.h" 

using namespace pugi;

void WaitForIdle2(int wait_seconds = 30) {
    static const int kTimeoutUsecs = 1000;
    static int envWaitTime;

    if (!envWaitTime) {
        if (getenv("WAIT_FOR_IDLE")) {
            envWaitTime = atoi(getenv("WAIT_FOR_IDLE"));
        } else {
            envWaitTime = wait_seconds;
        }
    }

    if (envWaitTime > wait_seconds) wait_seconds = envWaitTime;

    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    for (int i = 0; i < ((wait_seconds * 1000000)/kTimeoutUsecs); i++) {
        if (scheduler->IsEmpty()) {
            return;
        }
        usleep(kTimeoutUsecs);
    }
    EXPECT_TRUE(scheduler->IsEmpty());
}


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
#if 0
        if (Agent::GetInstance()->headless_agent_mode()) {
            AgentXmppChannel::HandleHeadlessAgentXmppClientChannelEvent(static_cast<AgentXmppChannel *>(this), state);
        } else {
            AgentXmppChannel::HandleXmppClientChannelEvent(static_cast<AgentXmppChannel *>(this), state);
        }
#endif
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
    ControlNodeMockBgpXmppPeer() : channel_ (NULL), rx_count_(0) {
    }

    void ReceiveUpdate(const XmppStanza::XmppMessage *msg) {
        rx_count_++;
    }    

    void HandleXmppChannelEvent(XmppChannel *channel,
                                xmps::PeerState state) {
        if (!channel_ && (state == xmps::NOT_READY))
            return;

        if (state != xmps::READY) {
            assert(channel_ && channel == channel_);
            channel->UnRegisterReceive(xmps::BGP);
            channel_ = NULL;
        } else {
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

    size_t Count() const { 
        return rx_count_; }
    virtual ~ControlNodeMockBgpXmppPeer() {
    }
private:
    XmppChannel *channel_;
    size_t rx_count_;
};


class AgentXmppUnitTest : public ::testing::Test { 
protected:
    AgentXmppUnitTest() : thread_(&evm_)  {}
 
    virtual void SetUp() {

        xs = new XmppServer(&evm_, XmppInit::kControlNodeJID);
        xc = new XmppClient(&evm_);

        xs->Initialize(0, false);
        
        thread_.Start();
    }

    virtual void TearDown() {
        xs->Shutdown();
        bgp_peer.reset(); 
        client->WaitForIdle();
        xc->Shutdown();
        client->WaitForIdle();

        TaskScheduler::GetInstance()->Stop();
        Agent::GetInstance()->controller()->unicast_cleanup_timer().cleanup_timer_->Fire();
        TaskScheduler::GetInstance()->Start();
        client->WaitForIdle();
        Agent::GetInstance()->controller()->Cleanup();
        client->WaitForIdle();

        TcpServerManager::DeleteServer(xs);
        TcpServerManager::DeleteServer(xc);
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
        cfg->endpoint.port(port);
        cfg->local_endpoint.address(boost::asio::ip::address::from_string(local_address));
        cfg->ToAddr = to;
        cfg->FromAddr = from;
        return cfg;
    }

    int GetStartLabel() {
        vector<int> entries;
        assert(stringToIntegerList(Agent::GetInstance()->GetAgentMcastLabelRange(0), "-",
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

    xml_node MessageHeaderInternal(xml_document *xdoc, std::string vrf, 
                                   bool bcast, bool l2) {
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
            if (l2) {
                ss << BgpAf::L2Vpn << "/" << BgpAf::Enet << "/" << vrf.c_str();
            } else {
                ss << BgpAf::IPv4 << "/" << BgpAf::Unicast << "/" << vrf.c_str();
            }
        }
        std::string node_str(ss.str());
        xitems.append_attribute("node") = node_str.c_str();
        return(xitems);
    }

    xml_node MessageHeader(xml_document *xdoc, std::string vrf, bool bcast) 
    {
        return MessageHeaderInternal(xdoc, vrf, bcast, false);
    }

    xml_node L2MessageHeader(xml_document *xdoc, std::string vrf, bool bcast) 
    {
        return MessageHeaderInternal(xdoc, vrf, bcast, true);
    }

    //Unicast
    void SendRouteMessage(ControlNodeMockBgpXmppPeer *peer, std::string vrf,
                          std::string address, int label) {
        xml_document xdoc;
        xml_node xitems = MessageHeader(&xdoc, vrf, false);

        autogen::NextHopType item_nexthop;
        item_nexthop.af = BgpAf::IPv4;
        item_nexthop.address = Agent::GetInstance()->GetRouterId().to_string();;
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
    
    void SendL2RouteMessage(ControlNodeMockBgpXmppPeer *peer, std::string vrf,
                          struct ether_addr *mac, std::string address,
                          int label) {
        xml_document xdoc;
        xml_node xitems = L2MessageHeader(&xdoc, vrf, false);

        autogen::EnetNextHopType item_nexthop;
        item_nexthop.af = BgpAf::IPv4;
        item_nexthop.address = Agent::GetInstance()->GetRouterId().to_string();;
        item_nexthop.label = label;
        
        autogen::EnetItemType item;
        item.entry.next_hops.next_hop.push_back(item_nexthop);

        ostringstream str;
        str << (ether_ntoa ((struct ether_addr *)&mac));
        item.entry.nlri.af = BgpAf::L2Vpn;
        item.entry.nlri.safi = BgpAf::Enet;
        item.entry.nlri.mac = str.str();
        item.entry.nlri.address = address;

        xml_node node = xitems.append_child("item");
        ostringstream item_id;
        item_id << str << "," << address;
        node.append_attribute("id") = item_id.str().c_str();
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

    void Send2BcastRouteDelete(ControlNodeMockBgpXmppPeer *peer, std::string vrf,
                               std::string subnet_addr, std::string allbcast_addr,
                               std::string agent_addr) {
        xml_document xdoc;
        xml_node xitems = MessageHeader(&xdoc, vrf, true);
        xml_node node = xitems.append_child("retract");
        //Route-Distinguisher
        stringstream ss;
        ss << agent_addr.c_str() << ":" << "65535:" << subnet_addr.c_str() 
                                        << "," << "0.0.0.0"; 
        string node_str(ss.str());
        node.append_attribute("id") = node_str.c_str();

        node = xitems.append_child("retract");
        stringstream ss2;
        ss2 << agent_addr.c_str() << ":" << "65535:" << allbcast_addr.c_str() 
                                        << "," << "0.0.0.0"; 
        string node2_str(ss2.str());
        node.append_attribute("id") = node2_str.c_str();

        SendDocument(xdoc, peer);
    }


    void XmppConnectionSetUp(bool l2_l3_forwarding_mode) {

        Agent::GetInstance()->controller()->increment_multicast_sequence_number();
        Agent::GetInstance()->set_cn_mcast_builder(NULL);

        //Create control-node bgp mock peer 
        mock_peer.reset(new ControlNodeMockBgpXmppPeer());
	xs->RegisterConnectionEvent(xmps::BGP,
	    boost::bind(&ControlNodeMockBgpXmppPeer::HandleXmppChannelEvent, 
                        mock_peer.get(), _1, _2));

        LOG(DEBUG, "Create xmpp agent client");
	xmppc_cfg = new XmppConfigData;
	xmppc_cfg->AddXmppChannelConfig(CreateXmppChannelCfg("127.0.0.1", 
					xs->GetPort(), "127.0.0.1",
                                        "agent-a",
					XmppInit::kControlNodeJID, true));
	xc->ConfigUpdate(xmppc_cfg);

	// client connection
	cchannel = xc->FindChannel(XmppInit::kControlNodeJID);
	//Create agent bgp peer
        bgp_peer.reset(new AgentBgpXmppPeerTest(cchannel,
                       Agent::GetInstance()->GetXmppServer(0), 
                       Agent::GetInstance()->GetAgentMcastLabelRange(0), 0));
	xc->RegisterConnectionEvent(xmps::BGP,
	    boost::bind(&AgentBgpXmppPeerTest::HandleXmppChannelEvent, 
			bgp_peer.get(), _2));
	Agent::GetInstance()->SetAgentXmppChannel(bgp_peer.get(), 0);

        // server connection
        WAIT_FOR(1000, 10000,
            ((sconnection = xs->FindConnection("agent-a")) != NULL));
        assert(sconnection);

        if (l2_l3_forwarding_mode) {
            XmppSubnetSetUp();
        } else {
            XmppL2SetUp();
        }
    }

    void XmppSubnetSetUp() {

	//wait for connection establishment
	WAIT_FOR(1000, 10000, (sconnection->GetStateMcState() == xmsm::ESTABLISHED));
	WAIT_FOR(1000, 10000, (cchannel->GetPeerState() == xmps::READY));

	//expect subscribe for __default__ at the mock server
	WAIT_FOR(1000, 10000, (mock_peer.get()->Count() == 1));

	//IpamInfo for subnet address belonging to vn
	IpamInfo ipam_info[] = {
	    {"1.1.1.0", 24, "1.1.1.200"}
	};
	
	client->Reset();

    VxLanNetworkIdentifierMode(false);
	client->WaitForIdle();

    CreateVmportEnv(input, 2, 0);
    WAIT_FOR(1000, 10000, (Agent::GetInstance()->GetInterfaceTable()->Size() == 5));

	client->Reset();
    AddIPAM("vn1", ipam_info, 1);
	// expect subscribe message + 2 VM routes+ subnet bcast +
	// v4 bcast route at the mock server + 1/2 l2 uc routes 
    // For broadcast request from IPv4 and L2 will be treated as 
    // one export and not two. 
	WAIT_FOR(1000, 10000, (mock_peer.get()->Count() == 9));

	Ip4Address addr = Ip4Address::from_string("1.1.1.1");
	EXPECT_TRUE(VmPortActive(input, 0));
	EXPECT_TRUE(RouteFind("vrf1", addr, 32));
	Inet4UnicastRouteEntry *rt = RouteGet("vrf1", addr, 32);
	EXPECT_STREQ(rt->dest_vn_name().c_str(), "vn1");

	// Send route, back to vrf1
	SendRouteMessage(mock_peer.get(), "vrf1", "1.1.1.1/32", 
			 MplsTable::kStartLabel);
	// Route reflected to vrf1
	WAIT_FOR(1000, 10000, (bgp_peer.get()->Count() == 1));

	addr = Ip4Address::from_string("1.1.1.2");
	EXPECT_TRUE(VmPortActive(input, 1));
	EXPECT_TRUE(RouteFind("vrf1", addr, 32));
	rt = RouteGet("vrf1", addr, 32);
        WAIT_FOR(1000, 10000, (rt->GetActivePath() != NULL));
        WAIT_FOR(1000, 10000, rt->dest_vn_name().size() > 0);
	EXPECT_STREQ(rt->dest_vn_name().c_str(), "vn1");

	// Send route, back to vrf1
	SendRouteMessage(mock_peer.get(), "vrf1", "1.1.1.2/32", 
			 MplsTable::kStartLabel+1);
	// Route reflected to vrf1
	WAIT_FOR(1000, 10000, (bgp_peer.get()->Count() == 2));

	//verify presence of subnet broadcast route
	addr = Ip4Address::from_string("1.1.1.255");
	EXPECT_TRUE(RouteFind("vrf1", addr, 32));
	rt = RouteGet("vrf1", addr, 32);

	//Send bcast route with allocated label from control-node
	//Assume agent-b sent a group join, hence the route
	//with label allocated and olist
        int alloc_label = GetStartLabel();
	SendBcastRouteMessage(mock_peer.get(), "vrf1",
			      "1.1.1.255", alloc_label,  
                              "127.0.0.1", alloc_label+10);
	// Bcast Route with updated olist 
	WAIT_FOR(1000, 10000, (bgp_peer.get()->Count() == 3));
    client->CompositeNHWait(7);
    WAIT_FOR(1000, 10000, (client->CompositeNHCount() == 5));
    client->MplsWait(5);

	NextHop *nh = const_cast<NextHop *>(rt->GetActiveNextHop());
	CompositeNH *cnh = static_cast<CompositeNH *>(nh);
    MulticastGroupObject *obj;
    ASSERT_TRUE(nh != NULL);
	ASSERT_TRUE(nh->GetType() == NextHop::COMPOSITE);
	obj = MulticastHandler::GetInstance()->FindGroupObject(cnh->vrf_name(),
			cnh->GetGrpAddr());
	ASSERT_TRUE(obj->GetSourceMPLSLabel() > 0);
    ASSERT_TRUE(cnh->ComponentNHCount() == 3);
    //Check if tunnel NH is programmed with correct label
    CompositeNH::ComponentNHList::const_iterator component_nh_it =
        cnh->begin();
    ComponentNH *component = *component_nh_it;
    CompositeNH *fabric_cnh = ((CompositeNH *)component->GetNH());
    CompositeNH::ComponentNHList::const_iterator fabric_component_nh_it =
        fabric_cnh->begin();
    ComponentNH *fabric_component = *fabric_component_nh_it;
    ASSERT_TRUE(fabric_component->label() == (uint32_t) alloc_label+10);

	//Verify mpls table
	MplsLabel *mpls = 
	    Agent::GetInstance()->GetMplsTable()->FindMplsLabel(alloc_label);
    //Verifying mpls label for mcast does not get stored in UC table
	ASSERT_TRUE(mpls == NULL); 
    // 2 v4 unicast label + 1 mc label + 2 l2 uc
	ASSERT_TRUE(Agent::GetInstance()->GetMplsTable()->Size() == 5);

	// Verify presence of all broadcast route in mcast table
	addr = Ip4Address::from_string("255.255.255.255");
	ASSERT_TRUE(MCRouteFind("vrf1", addr));
	Inet4MulticastRouteEntry *rt_m = MCRouteGet("vrf1", addr);
	
	//Send All bcast route
	SendBcastRouteMessage(mock_peer.get(), "vrf1",
			      "255.255.255.255", alloc_label+1,  
                              "127.0.0.1", alloc_label + 11);
	// Bcast Route with updated olist
	WAIT_FOR(1000, 10000, (bgp_peer.get()->Count() == 4));
    client->CompositeNHWait(11);
    WAIT_FOR(1000, 10000, (client->CompositeNHCount() == 6));
    client->MplsWait(6);

	nh = const_cast<NextHop *>(rt_m->GetActiveNextHop());
	ASSERT_TRUE(nh != NULL);
	ASSERT_TRUE(nh->GetType() == NextHop::COMPOSITE);
	cnh = static_cast<CompositeNH *>(nh);
	obj = MulticastHandler::GetInstance()->FindGroupObject(cnh->vrf_name(),
			cnh->GetGrpAddr());
	ASSERT_TRUE(obj->GetSourceMPLSLabel() > 0);
        ASSERT_TRUE(cnh->ComponentNHCount() == 3);
	
	//Verify mpls table
	mpls = Agent::GetInstance()->GetMplsTable()->FindMplsLabel(alloc_label+ 1);
	ASSERT_TRUE(mpls == NULL);
	ASSERT_TRUE(Agent::GetInstance()->GetMplsTable()->Size() == 6);
    }

    void XmppL2SetUp() {

        //wait for connection establishment
        WAIT_FOR(1000, 10000, (sconnection->GetStateMcState() == 
                              xmsm::ESTABLISHED));
        WAIT_FOR(1000, 10000, (cchannel->GetPeerState() == xmps::READY));

        //expect subscribe for __default__ at the mock server
        WAIT_FOR(1000, 10000, (mock_peer.get()->Count() == 1));

        client->Reset();
        AddL2Vn("vn1", 1);
        AddVrf("vrf1");
        AddLink("virtual-network", "vn1", "routing-instance", "vrf1");
        client->WaitForIdle();

        client->Reset();
        CreateL2VmportEnv(input, 2, 0);

        // 2 VM route + subscribe + l2 bcast route
        WAIT_FOR(1000, 10000, (mock_peer.get()->Count() == 5));

        EXPECT_TRUE(VmPortL2Active(input, 0));
        struct ether_addr *local_vm_mac;
        local_vm_mac = (struct ether_addr *)malloc(sizeof(struct ether_addr));
        memcpy (local_vm_mac, ether_aton("00:00:00:01:01:01"), 
                sizeof(struct ether_addr));
        EXPECT_TRUE(L2RouteFind("vrf1", *local_vm_mac));

        // Send route, back to vrf1
        SendL2RouteMessage(mock_peer.get(), "vrf1", local_vm_mac, "1.1.1.1/32", 
                         MplsTable::kStartLabel);
        // Route reflected to vrf1
        WAIT_FOR(1000, 10000, (bgp_peer.get()->Count() == 1));

        EXPECT_TRUE(VmPortL2Active(input, 1));
        struct ether_addr *local_vm_mac_2;
        local_vm_mac_2 = (struct ether_addr *)malloc(sizeof(struct ether_addr));
        memcpy (local_vm_mac_2, ether_aton("00:00:00:02:02:02"), 
                sizeof(struct ether_addr));
        EXPECT_TRUE(L2RouteFind("vrf1", *local_vm_mac_2));

        // Send route, back to vrf1
        SendL2RouteMessage(mock_peer.get(), "vrf1", 
                           local_vm_mac_2, "1.1.1.2/32", 
                           MplsTable::kStartLabel + 1);
        // Route reflected to vrf1
        WAIT_FOR(1000, 10000, (bgp_peer.get()->Count() == 2));

        //verify presence of L2 broadcast route
        struct ether_addr *broadcast_mac;
        broadcast_mac = (struct ether_addr *)malloc(sizeof(struct ether_addr));
        memcpy (broadcast_mac, ether_aton("ff:ff:ff:ff:ff:ff"), 
                sizeof(struct ether_addr));
        EXPECT_TRUE(L2RouteFind("vrf1", *broadcast_mac));
        Layer2RouteEntry *rt_m = L2RouteGet("vrf1", *broadcast_mac);

        int alloc_label = GetStartLabel();
        //Send All bcast route
        SendBcastRouteMessage(mock_peer.get(), "vrf1",
                              "255.255.255.255", alloc_label+1,  
                              "127.0.0.1", alloc_label + 11);
        // Bcast Route with updated olist
        WAIT_FOR(1000, 10000, (bgp_peer.get()->Count() == 3));
        client->MplsWait(3);

        NextHop *nh = const_cast<NextHop *>(rt_m->GetActiveNextHop());
        ASSERT_TRUE(nh != NULL);
        ASSERT_TRUE(nh->GetType() == NextHop::COMPOSITE);
        CompositeNH *cnh = static_cast<CompositeNH *>(nh);
        MulticastGroupObject *obj = 
            MulticastHandler::GetInstance()->FindGroupObject(cnh->vrf_name(),
                                                               cnh->GetGrpAddr());
        ASSERT_TRUE(obj->GetSourceMPLSLabel() > 0);
        ASSERT_TRUE(cnh->ComponentNHCount() == 3);

        //Verify mpls table
        MplsLabel *mpls = 
            Agent::GetInstance()->GetMplsTable()->FindMplsLabel(alloc_label+ 1);
        ASSERT_TRUE(mpls == NULL);
        ASSERT_TRUE(Agent::GetInstance()->GetMplsTable()->Size() == 3);
    }


    void XmppSubnetTearDown(int cnh_del_cnt) {
        client->Reset();
        DelIPAM("vn1");
        client->WaitForIdle();
        client->Reset();
        DeleteVmportEnv(input, 2, 1, 0);

        TaskScheduler::GetInstance()->Stop();
        Agent::GetInstance()->controller()->multicast_cleanup_timer().cleanup_timer_->Fire();
        Agent::GetInstance()->controller()->unicast_cleanup_timer().cleanup_timer_->Fire();
        TaskScheduler::GetInstance()->Start();
        client->WaitForIdle();

        WAIT_FOR(1000, 1000, (client->CompositeNHDelWait(cnh_del_cnt) == true));
        WAIT_FOR(1000, 10000, (client->CompositeNHCount() == 0));
        WAIT_FOR(1000, 1000, 
                 (Agent::GetInstance()->GetMplsTable()->Size() == 0));

        WAIT_FOR(1000, 1000, (VrfFind("vrf1") == false)); 
        WAIT_FOR(1000, 1000, (VrfFind("vrf2") == false)); 
        client->NextHopReset();
        client->MplsReset();
    }

    void XmppSubnetTearDown() {
        XmppSubnetTearDown(6);
    }

    EventManager evm_;
    ServerThread thread_;

    XmppConfigData *xmpps_cfg;
    XmppConfigData *xmppc_cfg;

    XmppServer *xs;
    XmppClient *xc;

    XmppConnection *sconnection;
    XmppChannel *cchannel;

    auto_ptr<AgentBgpXmppPeerTest> bgp_peer;
    auto_ptr<ControlNodeMockBgpXmppPeer> mock_peer;
};


namespace {

// Local VM Deactivate Test
TEST_F(AgentXmppUnitTest, SubnetBcast_Test_VmDeActivate) {
    const NextHop *nh;
    const CompositeNH *cnh;

    XmppConnectionSetUp(true);

    EXPECT_TRUE(Agent::GetInstance()->GetControlNodeMulticastBuilder() != NULL);
    EXPECT_STREQ(Agent::GetInstance()->GetControlNodeMulticastBuilder()->GetXmppServer().c_str(),
                 "127.0.0.1");

    //Delete vm-port and route entry in vrf1
    IntfCfgDel(input, 0);
    client->CompositeNHWait(16);
    WAIT_FOR(1000, 10000, (client->CompositeNHCount() == 6));
    // Route delete send to control-node 
    WAIT_FOR(1000, 10000, (mock_peer.get()->Count() == 11));

    //Send route delete
    SendRouteDeleteMessage(mock_peer.get(), "1.1.1.1/32", "vrf1");
    // Route delete for vrf1 
    client->MplsDelWait(2);
    WAIT_FOR(1000, 10000, (bgp_peer.get()->Count() == 5));

    //Verify label deallocated from Mpls Table
    ASSERT_TRUE(Agent::GetInstance()->GetMplsTable()->Size() == 4);

    //Delete vm-port and route entry in vrf1
    IntfCfgDel(input, 1);
    // Route delete for vm + braodcast
    WAIT_FOR(1000, 10000, (mock_peer.get()->Count() == 15));

    //Send route delete
    SendRouteDeleteMessage(mock_peer.get(), "1.1.1.2/32", "vrf1");
    // Route delete for vrf1 
    WAIT_FOR(1000, 10000, (bgp_peer.get()->Count() == 6));
    client->MplsDelWait(6);
    client->CompositeNHDelWait(4);

    //Confirm route has been cleaned up
    WAIT_FOR(1000, 10000, (RouteFind("vrf1", "1.1.1.1", 32) == false));
    WAIT_FOR(1000, 10000, (RouteFind("vrf1", "1.1.1.2", 32) == false));

    //Verify label deallocated from Mpls Table
    WAIT_FOR(1000, 10000, (Agent::GetInstance()->GetMplsTable()->Size() == 0));

    // ensure subnet broadcast route is deleted 
    Ip4Address sb_addr = Ip4Address::from_string("1.1.1.255");
    if(RouteFind("vrf1", sb_addr, 32) == true) { 
	Inet4UnicastRouteEntry *rt = RouteGet("vrf1", sb_addr, 32);
        nh = rt->GetActiveNextHop();
        cnh = static_cast<const CompositeNH *>(nh);
        ASSERT_TRUE(cnh->ComponentNHCount() == 1);
    }
    
    // send route-delete even though route-deleted at agent
    SendBcastRouteDelete(mock_peer.get(), "vrf1", "1.1.1.255", "127.0.0.1");
    // Subnet Bcast Route delete for vrf1 
    WAIT_FOR(1000, 10000, (bgp_peer.get()->Count() == 7));
    client->CompositeNHDelWait(4);

    //ensure all broadcast route  is deleted
    sb_addr = Ip4Address::from_string("255.255.255.255");
    if(MCRouteFind("vrf1", sb_addr) == true) { 
	Inet4MulticastRouteEntry *rt = MCRouteGet("vrf1", sb_addr);
        nh = rt->GetActiveNextHop();
        cnh = static_cast<const CompositeNH *>(nh);
        ASSERT_TRUE(cnh->ComponentNHCount() == 0);
    }

    // send route-delete even though route-deleted at agent
    SendBcastRouteDelete(mock_peer.get(), "vrf1", "255.255.255.255", "127.0.0.1");
    // Subnet Bcast Route delete for vrf1 
    WAIT_FOR(1000, 10000, (bgp_peer.get()->Count() == 8));
    
    //Verify label deallocated from Mpls Table
    WAIT_FOR(1000, 10000, (Agent::GetInstance()->GetMplsTable()->Size() == 0));

    //cleanup all config links via config 
    XmppSubnetTearDown();

    client->WaitForIdle();
    xc->ConfigUpdate(new XmppConfigData());
    client->WaitForIdle(5);
}

TEST_F(AgentXmppUnitTest, L2OnlyBcast_Test_SessionDownUp) {

    client->Reset();
    client->WaitForIdle();
 
    XmppConnectionSetUp(false);

    EXPECT_TRUE(Agent::GetInstance()->GetControlNodeMulticastBuilder() != NULL);
    EXPECT_STREQ(Agent::GetInstance()->GetControlNodeMulticastBuilder()->
                 GetXmppServer().c_str(),
                 "127.0.0.1");

    //bring-down the channel
    bgp_peer.get()->HandleXmppChannelEvent(xmps::NOT_READY);
    client->WaitForIdle();
    //Channel going down should not flush olist and mpls label and keep them as
    //stale entries until next peer comes up.
    if (Agent::GetInstance()->headless_agent_mode()) {
        WAIT_FOR(1000, 10000, (Agent::GetInstance()->GetMplsTable()->Size() == 3));
    } else {
        WAIT_FOR(1000, 10000, (Agent::GetInstance()->GetMplsTable()->Size() == 2));
    }

    EXPECT_TRUE(Agent::GetInstance()->GetControlNodeMulticastBuilder() == NULL);
    struct ether_addr *mac_1;
    struct ether_addr *mac_2;
    struct ether_addr *broadcast_mac;
    broadcast_mac = (struct ether_addr *)malloc(sizeof(struct ether_addr));
    mac_1 = (struct ether_addr *)malloc(sizeof(struct ether_addr));
    mac_2 = (struct ether_addr *)malloc(sizeof(struct ether_addr));
    memcpy (mac_1, ether_aton("00:00:00:01:01:01"), 
            sizeof(struct ether_addr));
    memcpy (mac_2, ether_aton("00:00:00:02:02:02"), 
            sizeof(struct ether_addr));
    memcpy (broadcast_mac, ether_aton("ff:ff:ff:ff:ff:ff"), 
            sizeof(struct ether_addr));

    //ensure route learnt via control-node, path is updated 
    Layer2RouteEntry *rt_1 = L2RouteGet("vrf1", *mac_1);
    Layer2RouteEntry *rt_2 = L2RouteGet("vrf1", *mac_2);
    Layer2RouteEntry *rt_b = L2RouteGet("vrf1", *broadcast_mac);

	client->WaitForIdle();

    //bring-up the channel
    bgp_peer.get()->HandleXmppChannelEvent(xmps::READY);
    client->WaitForIdle();

    EXPECT_TRUE(Agent::GetInstance()->GetControlNodeMulticastBuilder() != NULL);
    EXPECT_STREQ(Agent::GetInstance()->GetControlNodeMulticastBuilder()->
                 GetXmppServer().c_str(),
                 "127.0.0.1");

    WAIT_FOR(1000, 10000, (mock_peer.get()->Count() == 10));

    // Send route, back to vrf1
    SendL2RouteMessage(mock_peer.get(), "vrf1", mac_1, "1.1.1.1/32", 
		     MplsTable::kStartLabel);
    // Route reflected to vrf1
    WAIT_FOR(1000, 10000, (bgp_peer.get()->Count() == 4));

    // Send route, back to vrf1
    SendL2RouteMessage(mock_peer.get(), "vrf1", mac_2, "1.1.1.2/32", 
		     MplsTable::kStartLabel+1);
    // Route reflected to vrf1
    WAIT_FOR(1000, 10000, (bgp_peer.get()->Count() == 5));

    //Send All bcast route

    int alloc_label = GetStartLabel();
    SendBcastRouteMessage(mock_peer.get(), "vrf1",
			  "255.255.255.255", alloc_label+1,  
                          "127.0.0.1", alloc_label+10);
    // Bcast Route with updated olist
    WAIT_FOR(1000, 10000, (bgp_peer.get()->Count() == 6));
    if (Agent::GetInstance()->headless_agent_mode()) {
        client->CompositeNHWait(7);
        WAIT_FOR(1000, 10000, (client->CompositeNHCount() == 3));
    } else {
        client->CompositeNHWait(11);
        WAIT_FOR(1000, 10000, (client->CompositeNHCount() == 3));
        client->MplsWait(5);
    }

    //Verify mpls table
    MplsLabel *mpls = 
        Agent::GetInstance()->GetMplsTable()->FindMplsLabel(alloc_label+ 1);
    ASSERT_TRUE(mpls == NULL);
    //Verify mpls table size
    ASSERT_TRUE(Agent::GetInstance()->GetMplsTable()->Size() == 3);

    DeleteVmportEnv(input, 2, 1, 0);
    client->WaitForIdle();

    IntfCfgDel(input, 0);
    IntfCfgDel(input, 1);
    client->WaitForIdle(5);

    //cleanup all config links via config 
   
    if (Agent::GetInstance()->headless_agent_mode()) {
        XmppSubnetTearDown(3);
    } else {
        XmppSubnetTearDown(4);
    }

    xc->ConfigUpdate(new XmppConfigData());
    client->WaitForIdle(5);
    client->NextHopReset();
    client->MplsReset();
}

TEST_F(AgentXmppUnitTest, SubnetBcast_Test_SessionDownUp) {

    client->Reset();
    client->WaitForIdle();
 
    XmppConnectionSetUp(true);

    EXPECT_TRUE(Agent::GetInstance()->GetControlNodeMulticastBuilder() != NULL);
    EXPECT_STREQ(Agent::GetInstance()->GetControlNodeMulticastBuilder()->GetXmppServer().c_str(),
                 "127.0.0.1");

    //bring-down the channel
    AgentXmppChannel *ch = static_cast<AgentXmppChannel *>(bgp_peer.get());
    Peer *bgp_peer_id = ch->bgp_peer_id();
    bgp_peer.get()->HandleXmppChannelEvent(xmps::NOT_READY);
    client->WaitForIdle();
    if (!Agent::GetInstance()->headless_agent_mode()) {
        client->MplsDelWait(2);
    }

    EXPECT_TRUE(Agent::GetInstance()->GetControlNodeMulticastBuilder() == NULL);

    //ensure route learnt via control-node, path is updated 
    Ip4Address addr = Ip4Address::from_string("1.1.1.1");
    Inet4UnicastRouteEntry *rt = RouteGet("vrf1", addr, 32);
    if (Agent::GetInstance()->headless_agent_mode()) {
        WAIT_FOR(1000, 10000, (rt->FindPath(bgp_peer_id)->is_stale()));
    } else {
        WAIT_FOR(1000, 10000, (rt->FindPath(bgp_peer_id) == NULL));
    }

    //ensure route learnt via control-node, path is updated 
    addr = Ip4Address::from_string("1.1.1.2");
    rt = RouteGet("vrf1", addr, 32);
    if (Agent::GetInstance()->headless_agent_mode()) {
        WAIT_FOR(1000, 10000, (rt->FindPath(bgp_peer_id)->is_stale()));
        WAIT_FOR(1000, 10000, (client->CompositeNHCount() == 6)); 
        client->CompositeNHWait(11);
    } else {
        WAIT_FOR(1000, 10000, (rt->FindPath(bgp_peer_id) == NULL));
        WAIT_FOR(1000, 10000, (client->CompositeNHCount() == 5)); 
        client->CompositeNHWait(17);
    }

    //ensure route learnt via control-node is updated
    addr = Ip4Address::from_string("1.1.1.255");
    ASSERT_TRUE(RouteFind("vrf1", addr, 32));
    rt=RouteGet("vrf1", addr, 32);
    NextHop *nh = const_cast<NextHop *>(rt->GetActiveNextHop());
    ASSERT_TRUE(nh != NULL);
    ASSERT_TRUE(nh->GetType() == NextHop::COMPOSITE);
    CompositeNH *cnh = static_cast<CompositeNH *>(nh);
    ASSERT_TRUE(cnh->ComponentNHCount() == 3);

    //ensure route learnt via control-node is updated 
    addr = Ip4Address::from_string("255.255.255.255");
    ASSERT_TRUE(MCRouteFind("vrf1", addr));
    Inet4MulticastRouteEntry *rt_m = MCRouteGet("vrf1", addr);
    nh = const_cast<NextHop *>(rt_m->GetActiveNextHop());
    ASSERT_TRUE(nh != NULL);
    ASSERT_TRUE(nh->GetType() == NextHop::COMPOSITE);
    cnh = static_cast<CompositeNH *>(nh);
    ASSERT_TRUE(cnh->ComponentNHCount() == 3);

    //Label is not deallocated and retained as stale
    if (Agent::GetInstance()->headless_agent_mode()) {
        EXPECT_TRUE(Agent::GetInstance()->GetMplsTable()->Size() == 6);
    } else {
        EXPECT_TRUE(Agent::GetInstance()->GetMplsTable()->Size() == 4);
    }

    //bring-up the channel
    bgp_peer.get()->HandleXmppChannelEvent(xmps::READY);
    client->WaitForIdle();

    EXPECT_TRUE(Agent::GetInstance()->GetControlNodeMulticastBuilder() != NULL);
    EXPECT_STREQ(Agent::GetInstance()->GetControlNodeMulticastBuilder()->GetXmppServer().c_str(),
                 "127.0.0.1");

    // expect subscribe message <default,vrf> + 2 VM routes+ subnet bcast +
    // bcast route at the mock server
    WAIT_FOR(1000, 10000, (mock_peer.get()->Count() == 18));

    // Send route, back to vrf1
    SendRouteMessage(mock_peer.get(), "vrf1", "1.1.1.1/32", 
		     MplsTable::kStartLabel);
    // Route reflected to vrf1
    WAIT_FOR(1000, 10000, (bgp_peer.get()->Count() == 5));

    // Send route, back to vrf1
    SendRouteMessage(mock_peer.get(), "vrf1", "1.1.1.2/32", 
		     MplsTable::kStartLabel+1);
    // Route reflected to vrf1
    WAIT_FOR(1000, 10000, (bgp_peer.get()->Count() == 6));

    //Send bcast route with allocated label from control-node
    //Assume agent-b sent a group join, hence the route
    //with label allocated and olist
    int alloc_label = GetStartLabel();
    SendBcastRouteMessage(mock_peer.get(), "vrf1",
			  "1.1.1.255", alloc_label,  
                          "127.0.0.1", alloc_label+10);
    // Bcast Route with updated olist 
    WAIT_FOR(1000, 10000, (bgp_peer.get()->Count() == 7));
    if (Agent::GetInstance()->headless_agent_mode()) {
        WAIT_FOR(1000, 10000, (client->CompositeNHCount() == 6));
        client->CompositeNHWait(13);
        client->MplsWait(6);
    } else {
        WAIT_FOR(1000, 10000, (client->CompositeNHCount() == 5));
        client->CompositeNHWait(19);
        client->MplsWait(9);
    }

    addr = Ip4Address::from_string("1.1.1.255");
    ASSERT_TRUE(RouteFind("vrf1", addr, 32));
    rt=RouteGet("vrf1", addr, 32);
    nh = const_cast<NextHop *>(rt->GetActiveNextHop());
    ASSERT_TRUE(nh != NULL);
    ASSERT_TRUE(nh->GetType() == NextHop::COMPOSITE);
    cnh = static_cast<CompositeNH *>(nh);
    MulticastGroupObject *obj;
    obj = MulticastHandler::GetInstance()->FindGroupObject(cnh->vrf_name(),
		    cnh->GetGrpAddr());
    ASSERT_TRUE(obj->GetSourceMPLSLabel() > 0);
    ASSERT_TRUE(cnh->ComponentNHCount() == 3);

    //Verify mpls table
    MplsLabel *mpls = 
	Agent::GetInstance()->GetMplsTable()->FindMplsLabel(alloc_label);
    ASSERT_TRUE(mpls == NULL);
    if (Agent::GetInstance()->headless_agent_mode()) {
        ASSERT_TRUE(Agent::GetInstance()->GetMplsTable()->Size() == 6);
    } else {
        ASSERT_TRUE(Agent::GetInstance()->GetMplsTable()->Size() == 5);
    }

    //Send All bcast route
    SendBcastRouteMessage(mock_peer.get(), "vrf1",
			  "255.255.255.255", alloc_label+1,  
                          "127.0.0.1", alloc_label+10);
    // Bcast Route with updated olist
    WAIT_FOR(1000, 10000, (bgp_peer.get()->Count() == 8));
    WAIT_FOR(1000, 10000, (client->CompositeNHCount() == 6));
    if (Agent::GetInstance()->headless_agent_mode()) {
        client->CompositeNHWait(16);
        client->MplsWait(6);
    } else {
        client->CompositeNHWait(23);
        client->MplsWait(10);
    }

    //ensure route learnt via control-node is updated 
    addr = Ip4Address::from_string("255.255.255.255");
    ASSERT_TRUE(MCRouteFind("vrf1", addr));
    rt_m = MCRouteGet("vrf1", addr);
    nh = const_cast<NextHop *>(rt_m->GetActiveNextHop());
    ASSERT_TRUE(nh != NULL);
    ASSERT_TRUE(nh->GetType() == NextHop::COMPOSITE);
    cnh = static_cast<CompositeNH *>(nh);
    ASSERT_TRUE(cnh->ComponentNHCount() == 3);


    client->WaitForIdle();
    WAIT_FOR(1000, 10000, (Agent::GetInstance()->GetMplsTable()->
                            FindMplsLabel(alloc_label+ 1) == NULL));
    //Verify mpls table size
    WAIT_FOR(1000, 10000, (Agent::GetInstance()->GetMplsTable()->Size() == 6));

    if (Agent::GetInstance()->headless_agent_mode()) {
        XmppSubnetTearDown();
    } else {
        XmppSubnetTearDown(7);
    }

    IntfCfgDel(input, 0);
    IntfCfgDel(input, 1);
    client->WaitForIdle(5);

    xc->ConfigUpdate(new XmppConfigData());
    client->WaitForIdle(5);
}

TEST_F(AgentXmppUnitTest, Test_mcast_peer_identifier) {
    client->Reset();
    client->WaitForIdle();
 
    XmppConnectionSetUp(true);

    EXPECT_TRUE(Agent::GetInstance()->GetControlNodeMulticastBuilder() != NULL);
    EXPECT_STREQ(Agent::GetInstance()->GetControlNodeMulticastBuilder()->
                 GetXmppServer().c_str(), "127.0.0.1");

	IpamInfo ipam_info[] = {
	    {"1.1.1.0", 24, "1.1.1.200"}
	};
	
    AddIPAM("vn1", ipam_info, 1);

    Ip4Address addr = Ip4Address::from_string("1.1.1.255");
    WAIT_FOR(1000, 10000, (RouteFind("vrf1", addr, 32)));
    Inet4UnicastRouteEntry *rt = static_cast<Inet4UnicastRouteEntry *>
        (RouteGet("vrf1", addr, 32));
    NextHop *nh = const_cast<NextHop *>(rt->GetActiveNextHop());
    WAIT_FOR(1000, 10000, (nh != NULL));
    ASSERT_TRUE(nh->GetType() == NextHop::COMPOSITE);
    CompositeNH *cnh = static_cast<CompositeNH *>(nh);
    MulticastGroupObject *obj;
    obj = MulticastHandler::GetInstance()->FindGroupObject(cnh->vrf_name(),
		    cnh->GetGrpAddr());
    EXPECT_TRUE(obj != NULL);
    uint64_t peer_identifier_1 = obj->peer_identifier();

    //bring-down the channel
    bgp_peer.get()->HandleXmppChannelEvent(xmps::NOT_READY);
    client->WaitForIdle();

    WAIT_FOR(1000, 10000, (RouteFind("vrf1", addr, 32)));
    obj = MulticastHandler::GetInstance()->FindGroupObject("vrf1", addr);
    EXPECT_TRUE(obj != NULL);
    CompositeNHKey *key = new CompositeNHKey(obj->vrf_name(), 
                                             obj->GetGroupAddress(),
                                             obj->GetSourceAddress(), false,
                                             Composite::L3COMP); 
    cnh = static_cast<CompositeNH *>(Agent::GetInstance()->
                                     GetNextHopTable()->FindActiveEntry(key));
    ASSERT_TRUE(cnh != NULL);
    obj = MulticastHandler::GetInstance()->FindGroupObject(cnh->vrf_name(),
		    cnh->GetGrpAddr());
    EXPECT_TRUE(obj != NULL);
    EXPECT_TRUE(obj->peer_identifier() == peer_identifier_1);
    EXPECT_TRUE(obj->peer_identifier() != Agent::GetInstance()->controller()->
                multicast_sequence_number());

    //bring-up the channel
    bgp_peer.get()->HandleXmppChannelEvent(xmps::READY);
    client->WaitForIdle();

    SendBcastRouteMessage(mock_peer.get(), "vrf1",
			  "1.1.1.255", 9000, "127.0.0.1", 9012, 9013);
    EXPECT_TRUE(RouteFind("vrf1", addr, 32));
    obj = MulticastHandler::GetInstance()->FindGroupObject("vrf1", addr);
    EXPECT_TRUE(obj != NULL);

    WAIT_FOR(1000, 1000, obj->GetSourceMPLSLabel() == 9000);
    TaskScheduler::GetInstance()->Stop();
    Agent::GetInstance()->controller()->multicast_cleanup_timer().cleanup_timer_->Fire();
    TaskScheduler::GetInstance()->Start();
    client->WaitForIdle();

    obj = MulticastHandler::GetInstance()->FindGroupObject("vrf1", addr);
    EXPECT_TRUE(obj != NULL);
    key = new CompositeNHKey(obj->vrf_name(), 
                             obj->GetGroupAddress(),
                             obj->GetSourceAddress(), false,
                             Composite::L3COMP); 
    cnh = static_cast<CompositeNH *>(Agent::GetInstance()->
                                     GetNextHopTable()->FindActiveEntry(key));
    ASSERT_TRUE(cnh != NULL);
    obj = MulticastHandler::GetInstance()->FindGroupObject(cnh->vrf_name(),
		    cnh->GetGrpAddr());
    EXPECT_TRUE(obj != NULL);
    WAIT_FOR(1000, 1000, obj->GetSourceMPLSLabel() == 9000);
    EXPECT_TRUE(obj->peer_identifier() == (peer_identifier_1 + 2));
    EXPECT_TRUE(obj->peer_identifier() == Agent::GetInstance()->controller()->
                multicast_sequence_number());

    XmppSubnetTearDown();

    xc->ConfigUpdate(new XmppConfigData());
    WAIT_FOR(1000, 1000, (VrfGet("vrf1") == NULL));
    client->WaitForIdle(5);
}

// Remote VM Deactivate Test, resuting in retracts
TEST_F(AgentXmppUnitTest, SubnetBcast_MultipleRetracts) {

    client->Reset();
    client->WaitForIdle();
 
    XmppConnectionSetUp(true);

    // remote-VM deactivated resulting in
    // route-delete for subnet and all broadcast route 
    Send2BcastRouteDelete(mock_peer.get(), "vrf1",
                          "1.1.1.255", "255.255.255.255",
                          "127.0.0.1");
    client->MplsDelWait(2);
    WAIT_FOR(1000, 10000, (bgp_peer.get()->Count() == 5));
    WAIT_FOR(1000, 1000, (Agent::GetInstance()->GetMplsTable()->Size() == 4));

    //ensure route learnt via control-node, path is updated 
    Ip4Address addr = Ip4Address::from_string("1.1.1.255");
    ASSERT_TRUE(RouteFind("vrf1", addr, 32));
    Inet4UnicastRouteEntry *rt = RouteGet("vrf1", addr, 32);

    NextHop *nh = const_cast<NextHop *>(rt->GetActiveNextHop());
    ASSERT_TRUE(nh != NULL);
    ASSERT_TRUE(nh->GetType() == NextHop::COMPOSITE);
    CompositeNH *cnh = static_cast<CompositeNH *>(nh);
    MulticastGroupObject *obj;
    obj = MulticastHandler::GetInstance()->FindGroupObject(cnh->vrf_name(),
		    cnh->GetGrpAddr());
    ASSERT_TRUE(obj->GetSourceMPLSLabel() == 0);

    addr = Ip4Address::from_string("255.255.255.255");
    ASSERT_TRUE(MCRouteFind("vrf1", addr));
    Inet4MulticastRouteEntry *rt_m = MCRouteGet("vrf1", addr);

    nh = const_cast<NextHop *>(rt_m->GetActiveNextHop());
    ASSERT_TRUE(nh != NULL);
    ASSERT_TRUE(nh->GetType() == NextHop::COMPOSITE);
    cnh = static_cast<CompositeNH *>(nh);
    obj = MulticastHandler::GetInstance()->FindGroupObject(cnh->vrf_name(),
		    cnh->GetGrpAddr());
    ASSERT_TRUE(obj->GetSourceMPLSLabel() == 0);

    XmppSubnetTearDown();

    IntfCfgDel(input, 0);
    IntfCfgDel(input, 1);
    client->WaitForIdle(5);

    xc->ConfigUpdate(new XmppConfigData());
    client->WaitForIdle(5);
}

// Update olist label and src-label, olist size remains same
// Expect src-lable update on comp-nh, sub-nh deleted/add with updated olist label
// Expect no mpls label leaks
TEST_F(AgentXmppUnitTest, Test_Update_Olist_Src_Label) {

    client->Reset();
    client->WaitForIdle();
 
    XmppConnectionSetUp(true);

    //Verify sub-nh count
    int alloc_label = GetStartLabel();
    Ip4Address addr = Ip4Address::from_string("1.1.1.255");
    EXPECT_TRUE(RouteFind("vrf1", addr, 32));
    Inet4UnicastRouteEntry *rt = RouteGet("vrf1", addr, 32);
    NextHop *nh = const_cast<NextHop *>(rt->GetActiveNextHop());
    ASSERT_TRUE(nh != NULL);
    ASSERT_TRUE(nh->GetType() == NextHop::COMPOSITE);
    CompositeNH *cnh = static_cast<CompositeNH *>(nh);
    ASSERT_TRUE(cnh->ComponentNHCount() == 3);
    MulticastGroupObject *obj;
    obj = MulticastHandler::GetInstance()->FindGroupObject(cnh->vrf_name(),
               		                    cnh->GetGrpAddr());

    ASSERT_TRUE(obj->GetSourceMPLSLabel() == static_cast<uint>(alloc_label));
    //Verify mpls table
    MplsLabel *mpls = 
	Agent::GetInstance()->GetMplsTable()->FindMplsLabel(alloc_label);
    ASSERT_TRUE(mpls == NULL);
    ASSERT_TRUE(Agent::GetInstance()->GetMplsTable()->Size() == 6);


    //Send Updated olist label, src-nh label
    SendBcastRouteMessage(mock_peer.get(), "vrf1",
			  "1.1.1.255", alloc_label + 2,  
                          "127.0.0.1", alloc_label + 12);
    // Bcast Route with updated olist 
    WAIT_FOR(1000, 10000, (bgp_peer.get()->Count() == 5));
    client->MplsWait(8);
    WAIT_FOR(1000, 10000, (client->CompositeNHCount() == 6));
    client->CompositeNHWait(13);

    //verify sub-nh list count
    ASSERT_TRUE(cnh->ComponentNHCount() == 3);
    ASSERT_TRUE(obj->GetSourceMPLSLabel() == static_cast<uint>(alloc_label+2));

    //Verify mpls table
    mpls = Agent::GetInstance()->GetMplsTable()->FindMplsLabel(alloc_label+2);
    ASSERT_TRUE(mpls == NULL);
    // Detect mpls label leaks
    WAIT_FOR(1000, 1000, (Agent::GetInstance()->GetMplsTable()->Size() == 6));

    // Verify presence of all broadcast route in mcast table
    addr = Ip4Address::from_string("255.255.255.255");
    ASSERT_TRUE(MCRouteFind("vrf1", addr));
    Inet4MulticastRouteEntry *rt_m = MCRouteGet("vrf1", addr);
    nh = const_cast<NextHop *>(rt_m->GetActiveNextHop());
    ASSERT_TRUE(nh != NULL);
    ASSERT_TRUE(nh->GetType() == NextHop::COMPOSITE);
    cnh = static_cast<CompositeNH *>(nh);
    ASSERT_TRUE(cnh->ComponentNHCount() == 3);
    obj = MulticastHandler::GetInstance()->FindGroupObject(cnh->vrf_name(),
               		                    cnh->GetGrpAddr());
    ASSERT_TRUE(obj->GetSourceMPLSLabel() == static_cast<uint>(alloc_label+1));
    //Verify mpls table
    mpls = Agent::GetInstance()->GetMplsTable()->FindMplsLabel(alloc_label+1);
    ASSERT_TRUE(mpls == NULL);
    WAIT_FOR(1000, 1000, (Agent::GetInstance()->GetMplsTable()->Size() == 6));
    
    //Send All bcast route
    SendBcastRouteMessage(mock_peer.get(), "vrf1",
			  "255.255.255.255", alloc_label + 3,  
                          "127.0.0.1", alloc_label+13);
    // Bcast Route with updated olist
    WAIT_FOR(1000, 10000, (client->CompositeNHCount() == 6));
    client->CompositeNHWait(17);
    client->MplsWait(10);
    WAIT_FOR(1000, 10000, (bgp_peer.get()->Count() == 6));

    nh = const_cast<NextHop *>(rt_m->GetActiveNextHop());
    ASSERT_TRUE(nh != NULL);
    ASSERT_TRUE(nh->GetType() == NextHop::COMPOSITE);
    ASSERT_TRUE(cnh->ComponentNHCount() == 3);
    ASSERT_TRUE(obj->GetSourceMPLSLabel() == static_cast<uint>(alloc_label+3));
    //Verify mpls table
    mpls = Agent::GetInstance()->GetMplsTable()->FindMplsLabel(alloc_label+3);
    ASSERT_TRUE(mpls == NULL);
    cout << "MSDEBUG mplstable size " << Agent::GetInstance()->GetMplsTable()->Size() << endl;

    WAIT_FOR(1000, 1000, 
             (Agent::GetInstance()->GetMplsTable()->Size() == 6));

    XmppSubnetTearDown();

    IntfCfgDel(input, 0);
    IntfCfgDel(input, 1);
    client->WaitForIdle(5);

    xc->ConfigUpdate(new XmppConfigData());
    client->WaitForIdle(5);
}

// Test to check olist changes
// Test no sub-nh leaks
TEST_F(AgentXmppUnitTest, Test_Olist_change) {

    client->Reset();
    client->WaitForIdle();
 
    XmppConnectionSetUp(true);

    //Verify sub-nh count
    int alloc_label = GetStartLabel();
    Ip4Address addr = Ip4Address::from_string("1.1.1.255");
    EXPECT_TRUE(RouteFind("vrf1", addr, 32));
    Inet4UnicastRouteEntry *rt = RouteGet("vrf1", addr, 32);
    NextHop *nh = const_cast<NextHop *>(rt->GetActiveNextHop());
    ASSERT_TRUE(nh != NULL);
    ASSERT_TRUE(nh->GetType() == NextHop::COMPOSITE);
    CompositeNH *cnh = static_cast<CompositeNH *>(nh);
    ASSERT_TRUE(cnh->ComponentNHCount() == 3); //2 local VMs + tunnel-nh
    MulticastGroupObject *obj;
    obj = MulticastHandler::GetInstance()->FindGroupObject(cnh->vrf_name(),
               		                    cnh->GetGrpAddr());

    ASSERT_TRUE(obj->GetSourceMPLSLabel() == static_cast<uint>(alloc_label));
    //Verify mpls table
    MplsLabel *mpls = 
	Agent::GetInstance()->GetMplsTable()->FindMplsLabel(alloc_label);
    ASSERT_TRUE(mpls == NULL);
    WAIT_FOR(1000, 10000, (Agent::GetInstance()->GetMplsTable()->Size() == 6));

    //Send Updated olist label, src-nh label
    SendBcastRouteMessage(mock_peer.get(), "vrf1",
			  "1.1.1.255", alloc_label,  
                          "127.0.0.1", 
                          alloc_label + 12,
                          alloc_label + 13);
    // Bcast Route with updated olist 
    WAIT_FOR(1000, 10000, (bgp_peer.get()->Count() == 5));
    WAIT_FOR(1000, 10000, (client->CompositeNHCount() == 6));
    client->CompositeNHWait(13);  
    client->MplsWait(6);

    //verify sub-nh list count ( 2 local-VMs + 1 fabric member in olist )
    ASSERT_TRUE(cnh->ComponentNHCount() == 3);
    ASSERT_TRUE(obj->GetSourceMPLSLabel() == static_cast<uint>(alloc_label));

    //Verify mpls table
    mpls = Agent::GetInstance()->GetMplsTable()->FindMplsLabel(alloc_label);
    ASSERT_TRUE(mpls == NULL);
    // Detect mpls label leaks
    WAIT_FOR(1000, 10000, (Agent::GetInstance()->GetMplsTable()->Size() == 6));

    //Send Updated olist label, src-nh label
    SendBcastRouteMessage(mock_peer.get(), "vrf1",
			  "1.1.1.255", alloc_label,  
                          "127.0.0.1", alloc_label + 14);
    // Bcast Route with updated olist 
    WAIT_FOR(1000, 10000, (client->CompositeNHCount() == 6));
    client->CompositeNHWait(15);
    client->MplsWait(6);
    WAIT_FOR(1000, 10000, (bgp_peer.get()->Count() == 6));

    //verify sub-nh list count ( 2 local-VMs + 1 members in olist )
    ASSERT_TRUE(cnh->ComponentNHCount() == 3);
    ASSERT_TRUE(obj->GetSourceMPLSLabel() == static_cast<uint>(alloc_label));

    //Verify mpls table
    mpls = Agent::GetInstance()->GetMplsTable()->FindMplsLabel(alloc_label);
    ASSERT_TRUE(mpls == NULL);
    // Detect mpls label leaks
    WAIT_FOR(1000, 10000, (Agent::GetInstance()->GetMplsTable()->Size() == 6));


    //Verify all-broadcast
    addr = Ip4Address::from_string("255.255.255.255");
    EXPECT_TRUE(MCRouteFind("vrf1", addr));
    Inet4MulticastRouteEntry *rt_m = MCRouteGet("vrf1", addr);
    nh = const_cast<NextHop *>(rt_m->GetActiveNextHop());
    ASSERT_TRUE(nh != NULL);
    ASSERT_TRUE(nh->GetType() == NextHop::COMPOSITE);
    cnh = static_cast<CompositeNH *>(nh);
    ASSERT_TRUE(cnh->ComponentNHCount() == 3); //2 local VMs + tunnel-nh
    obj = MulticastHandler::GetInstance()->FindGroupObject(cnh->vrf_name(),
               		                    cnh->GetGrpAddr());
    ASSERT_TRUE(obj->GetSourceMPLSLabel() == static_cast<uint>(alloc_label+1));

    //Verify mpls table
    mpls = Agent::GetInstance()->GetMplsTable()->FindMplsLabel(alloc_label+1);
    ASSERT_TRUE(mpls == NULL);
    WAIT_FOR(1000, 10000, (Agent::GetInstance()->GetMplsTable()->Size() == 6));

    //Send Updated olist label, src-nh label
    SendBcastRouteMessage(mock_peer.get(), "vrf1",
			  "255.255.255.255", alloc_label+1,  
                          "127.0.0.1", 
                          alloc_label + 15,
                          alloc_label + 16);
    // Bcast Route with updated olist 
    WAIT_FOR(1000, 10000, (bgp_peer.get()->Count() == 7));
    WAIT_FOR(1000, 10000, (client->CompositeNHCount() == 6));
    client->CompositeNHWait(18);
    client->MplsWait(6);

    //verify sub-nh list count ( 2 local-VMs + 2 members in olist )
    ASSERT_TRUE(cnh->ComponentNHCount() == 3);
    ASSERT_TRUE(obj->GetSourceMPLSLabel() == static_cast<uint>(alloc_label+1));

    //Verify mpls table
    mpls = Agent::GetInstance()->GetMplsTable()->FindMplsLabel(alloc_label+1);
    ASSERT_TRUE(mpls == NULL);
    // Detect mpls label leaks
    WAIT_FOR(1000, 10000, (Agent::GetInstance()->GetMplsTable()->Size() == 6));

    //Send Updated olist label, src-nh label
    SendBcastRouteMessage(mock_peer.get(), "vrf1",
			  "255.255.255.255", alloc_label+1,  
                          "127.0.0.1", alloc_label + 17);
    // Bcast Route with updated olist 
    WAIT_FOR(1000, 10000, (bgp_peer.get()->Count() == 8));
    WAIT_FOR(1000, 10000, (client->CompositeNHCount() == 6));
    client->CompositeNHWait(21);
    client->MplsWait(6);

    //verify sub-nh list count ( 2 local-VMs + 1 members in olist )
    ASSERT_TRUE(cnh->ComponentNHCount() == 3);
    ASSERT_TRUE(obj->GetSourceMPLSLabel() == static_cast<uint>(alloc_label+1));

    //Verify mpls table
    mpls = Agent::GetInstance()->GetMplsTable()->FindMplsLabel(alloc_label+1);
    ASSERT_TRUE(mpls == NULL);
    // Detect mpls label leaks
    WAIT_FOR(1000, 10000, (Agent::GetInstance()->GetMplsTable()->Size() == 6));
     

    XmppSubnetTearDown();

    IntfCfgDel(input, 0);
    IntfCfgDel(input, 1);
    client->WaitForIdle(5);

    xc->ConfigUpdate(new XmppConfigData());
    client->WaitForIdle(5);
}


// Test to check olist changes, with same labels
TEST_F(AgentXmppUnitTest, Test_Olist_change_with_same_label) {

    client->Reset();
    client->WaitForIdle();
 
    XmppConnectionSetUp(true);

    //Verify sub-nh count
    int alloc_label = GetStartLabel();
    Ip4Address addr = Ip4Address::from_string("1.1.1.255");
    EXPECT_TRUE(RouteFind("vrf1", addr, 32));
    Inet4UnicastRouteEntry *rt = RouteGet("vrf1", addr, 32);
    NextHop *nh = const_cast<NextHop *>(rt->GetActiveNextHop());
    ASSERT_TRUE(nh != NULL);
    ASSERT_TRUE(nh->GetType() == NextHop::COMPOSITE);
    CompositeNH *cnh = static_cast<CompositeNH *>(nh);
    ASSERT_TRUE(cnh->ComponentNHCount() == 3); //2 local VMs + tunnel-nh
    MulticastGroupObject *obj;
    obj = MulticastHandler::GetInstance()->FindGroupObject(cnh->vrf_name(),
               		                    cnh->GetGrpAddr());

    ASSERT_TRUE(obj->GetSourceMPLSLabel() == static_cast<uint>(alloc_label));
    //Verify mpls table
    MplsLabel *mpls = 
	Agent::GetInstance()->GetMplsTable()->FindMplsLabel(alloc_label);
    ASSERT_TRUE(mpls == NULL);
    WAIT_FOR(1000, 10000, (Agent::GetInstance()->GetMplsTable()->Size() == 6));

    //Send Updated olist label, src-nh label
    SendBcastRouteMessage(mock_peer.get(), "vrf1",
			  "1.1.1.255", alloc_label+40,  
                          "127.0.0.1", 
                          alloc_label + 12,
                          alloc_label + 12);
    // Bcast Route with updated olist 
    WAIT_FOR(1000, 10000, (bgp_peer.get()->Count() == 5));
    WAIT_FOR(1000, 10000, (client->CompositeNHCount() == 6));
    client->CompositeNHWait(13);
    client->MplsWait(8);

    //verify sub-nh list count ( 2 local-VMs + 2 members in olist )
    ASSERT_TRUE(cnh->ComponentNHCount() == 3);
    ASSERT_TRUE(obj->GetSourceMPLSLabel() == static_cast<uint>(alloc_label+40));

    //Verify mpls table
    mpls = Agent::GetInstance()->GetMplsTable()->FindMplsLabel(alloc_label+40);
    ASSERT_TRUE(mpls == NULL);
    // Detect mpls label leaks
    WAIT_FOR(1000, 10000, (Agent::GetInstance()->GetMplsTable()->Size() == 6));

    //Send Updated olist label, src-nh label
    SendBcastRouteMessage(mock_peer.get(), "vrf1",
			  "1.1.1.255", alloc_label + 41,  
                          "127.0.0.1", alloc_label + 14);
    // Bcast Route with updated olist 
    WAIT_FOR(1000, 10000, (bgp_peer.get()->Count() == 6));
    WAIT_FOR(1000, 10000, (client->CompositeNHCount() == 6));
    client->CompositeNHWait(15);
    client->MplsWait(10);

    //verify sub-nh list count ( 2 local-VMs + 1 members in olist )
    ASSERT_TRUE(cnh->ComponentNHCount() == 3);
    ASSERT_TRUE(obj->GetSourceMPLSLabel() == static_cast<uint>(alloc_label+41));

    //Verify mpls table
    mpls = Agent::GetInstance()->GetMplsTable()->FindMplsLabel(alloc_label+41);
    ASSERT_TRUE(mpls == NULL);
    // Detect mpls label leaks
    WAIT_FOR(1000, 10000, (Agent::GetInstance()->GetMplsTable()->Size() == 6));


    //Verify all-broadcast
    addr = Ip4Address::from_string("255.255.255.255");
    EXPECT_TRUE(MCRouteFind("vrf1", addr));
    Inet4MulticastRouteEntry *rt_m = MCRouteGet("vrf1", addr);
    nh = const_cast<NextHop *>(rt_m->GetActiveNextHop());
    ASSERT_TRUE(nh != NULL);
    ASSERT_TRUE(nh->GetType() == NextHop::COMPOSITE);
    cnh = static_cast<CompositeNH *>(nh);
    ASSERT_TRUE(cnh->ComponentNHCount() == 3); //2 local VMs + tunnel-nh
    obj = MulticastHandler::GetInstance()->FindGroupObject(cnh->vrf_name(),
               		                    cnh->GetGrpAddr());
    ASSERT_TRUE(obj->GetSourceMPLSLabel() == static_cast<uint>(alloc_label+1));

    //Verify mpls table
    mpls = Agent::GetInstance()->GetMplsTable()->FindMplsLabel(alloc_label+1);
    ASSERT_TRUE(mpls == NULL);
    WAIT_FOR(1000, 10000, (Agent::GetInstance()->GetMplsTable()->Size() == 6));

    //Send Updated olist label, src-nh label
    SendBcastRouteMessage(mock_peer.get(), "vrf1",
			  "255.255.255.255", alloc_label+50,  
                          "127.0.0.1", 
                          alloc_label + 15,
                          alloc_label + 15);
    // Bcast Route with updated olist 
    WAIT_FOR(1000, 10000, (bgp_peer.get()->Count() == 7));
    WAIT_FOR(1000, 10000, (client->CompositeNHCount() == 6));
    client->CompositeNHWait(19);
    client->MplsWait(12);

    //verify sub-nh list count ( 2 local-VMs + 2 members in olist )
    ASSERT_TRUE(cnh->ComponentNHCount() == 3);
    ASSERT_TRUE(obj->GetSourceMPLSLabel() == static_cast<uint>(alloc_label+50));

    //Verify mpls table
    mpls = Agent::GetInstance()->GetMplsTable()->FindMplsLabel(alloc_label+50);
    ASSERT_TRUE(mpls == NULL);
    // Detect mpls label leaks
    WAIT_FOR(1000, 10000, (Agent::GetInstance()->GetMplsTable()->Size() == 6));

    //Send Updated olist label, src-nh label
    SendBcastRouteMessage(mock_peer.get(), "vrf1",
			  "255.255.255.255", alloc_label+51,  
                          "127.0.0.1", alloc_label + 17);
    // Bcast Route with updated olist 
    WAIT_FOR(1000, 10000, (bgp_peer.get()->Count() == 8));
    WAIT_FOR(1000, 10000, (client->CompositeNHCount() == 6));
    client->CompositeNHWait(23);
    client->MplsWait(14);

    //verify sub-nh list count ( 2 local-VMs + 1 members in olist )
    ASSERT_TRUE(cnh->ComponentNHCount() == 3);
    ASSERT_TRUE(obj->GetSourceMPLSLabel() == static_cast<uint>(alloc_label+51));

    //Verify mpls table
    mpls = Agent::GetInstance()->GetMplsTable()->FindMplsLabel(alloc_label+51);
    ASSERT_TRUE(mpls == NULL);
    // Detect mpls label leaks
    WAIT_FOR(1000, 10000, (Agent::GetInstance()->GetMplsTable()->Size() == 6));

    XmppSubnetTearDown();

    IntfCfgDel(input, 0);
    IntfCfgDel(input, 1);
    client->WaitForIdle(5);

    xc->ConfigUpdate(new XmppConfigData());
    client->WaitForIdle(5);
}

TEST_F(AgentXmppUnitTest, SubnetBcast_Retract_from_non_mcast_tree_builder) {

    Agent::GetInstance()->SetXmppServer("127.0.0.2", 1);
    Agent::GetInstance()->SetAgentMcastLabelRange(1);
    client->Reset();
    XmppConnectionSetUp(true);
    client->WaitForIdle();
 
    XmppServer *xs_s;
    XmppClient *xc_s;
    XmppConnection *sconnection_s;
    XmppChannel *cchannel_s;
    auto_ptr<AgentBgpXmppPeerTest> bgp_peer_s;
    auto_ptr<ControlNodeMockBgpXmppPeer> mock_peer_s;

    xs_s = new XmppServer(&evm_, XmppInit::kControlNodeJID);
    xs_s->Initialize(0, false);
    xc_s = new XmppClient(&evm_);

    //Create control-node bgp mock peer 
    mock_peer_s.reset(new ControlNodeMockBgpXmppPeer());
	xs_s->RegisterConnectionEvent(xmps::BGP,
	    boost::bind(&ControlNodeMockBgpXmppPeer::HandleXmppChannelEvent, 
                        mock_peer_s.get(), _1, _2));

    //Create an xmpp client
    XmppConfigData *xmppc_s_cfg = new XmppConfigData;
	LOG(DEBUG, "Create an xmpp client connect to Server port " << xs_s->GetPort());
	xmppc_s_cfg->AddXmppChannelConfig(CreateXmppChannelCfg("127.0.0.2", 
                                      xs_s->GetPort(), "127.0.0.2", "agent-a",
                                      XmppInit::kControlNodeJID, true));
	xc_s->ConfigUpdate(xmppc_s_cfg);
    cchannel_s = xc_s->FindChannel(XmppInit::kControlNodeJID);
    //Create agent bgp peer
	bgp_peer_s.reset(new AgentBgpXmppPeerTest(cchannel_s,
                         Agent::GetInstance()->GetXmppServer(1), 
                         Agent::GetInstance()->GetAgentMcastLabelRange(1), 1));
	Agent::GetInstance()->SetAgentXmppChannel(bgp_peer_s.get(), 1);
	xc_s->RegisterConnectionEvent(xmps::BGP,
	    boost::bind(&AgentBgpXmppPeerTest::HandleXmppChannelEvent, 
                    bgp_peer_s.get(), _2));

	// server connection
    WAIT_FOR(1000, 10000,
             ((sconnection_s = xs_s->FindConnection("agent-a")) != NULL));
    assert(sconnection_s);

    // remote-VM deactivated resulting in
    // route-delete for subnet and all broadcast route 

    int alloc_label = GetStartLabel();
    Send2BcastRouteDelete(mock_peer_s.get(), "vrf1",
                          "1.1.1.255", "255.255.255.255",
                          "127.0.0.1");
    client->WaitForIdle();
	//Verify mpls table, shud not be deleted when retract message comes from
    //non multicast tree builder peer
	ASSERT_TRUE(Agent::GetInstance()->GetMplsTable()->Size() == 6);
    client->WaitForIdle();
    XmppSubnetTearDown();

    IntfCfgDel(input, 0);
    IntfCfgDel(input, 1);
    client->WaitForIdle(5);

    xc_s->ConfigUpdate(new XmppConfigData());
    client->WaitForIdle();
    xc->ConfigUpdate(new XmppConfigData());
    client->WaitForIdle(5);
    bgp_peer_s.reset();
    client->WaitForIdle();
    xc_s->Shutdown();
    client->WaitForIdle();
    xs_s->Shutdown();
    client->WaitForIdle();

    TcpServerManager::DeleteServer(xs_s);
    TcpServerManager::DeleteServer(xc_s);
    client->WaitForIdle();
    Agent::GetInstance()->SetXmppServer("", 1);
    Agent::GetInstance()->SetAgentXmppChannel(NULL, 1);
}

// Let subnet ipam go off. Bring down peer. Dont delete interface in sequence.
TEST_F(AgentXmppUnitTest, SubnetBcast_Test_sessiondown_after_ipam_del) {

    client->Reset();
    client->WaitForIdle();
 
    XmppConnectionSetUp(true);

    EXPECT_TRUE(Agent::GetInstance()->GetControlNodeMulticastBuilder() != NULL);
    EXPECT_STREQ(Agent::GetInstance()->GetControlNodeMulticastBuilder()->
                 GetXmppServer().c_str(), "127.0.0.1");

	IpamInfo ipam_info[] = {
	    {"1.1.1.0", 24, "1.1.1.200"}
	};
	
    AddIPAM("vn1", ipam_info, 1);
    client->WaitForIdle();

    Ip4Address addr = Ip4Address::from_string("1.1.1.255");
    ASSERT_TRUE(RouteFind("vrf1", addr, 32));
    Inet4UnicastRouteEntry *rt = static_cast<Inet4UnicastRouteEntry *>
        (RouteGet("vrf1", addr, 32));
    NextHop *nh = const_cast<NextHop *>(rt->GetActiveNextHop());
    ASSERT_TRUE(nh != NULL);
    ASSERT_TRUE(nh->GetType() == NextHop::COMPOSITE);
    CompositeNH *cnh = static_cast<CompositeNH *>(nh);
    MulticastGroupObject *obj;
    obj = MulticastHandler::GetInstance()->FindGroupObject(cnh->vrf_name(),
		    cnh->GetGrpAddr());
    EXPECT_TRUE(obj != NULL);

    //Delete IPAM 
    DelIPAM("vn1");
    WAIT_FOR(1000, 1000, (RouteFind("vrf1", addr, 32) == false));

    //bring-down the channel
    bgp_peer.get()->HandleXmppChannelEvent(xmps::NOT_READY);
    client->WaitForIdle();

    EXPECT_TRUE(RouteFind("vrf1", addr, 32) == false);
    obj = MulticastHandler::GetInstance()->FindGroupObject("vrf1", addr);
    EXPECT_TRUE(obj != NULL);
    CompositeNHKey *key = new CompositeNHKey(obj->vrf_name(), 
                                             obj->GetGroupAddress(),
                                             obj->GetSourceAddress(), false,
                                             Composite::L3COMP); 
    cnh = static_cast<CompositeNH *>(Agent::GetInstance()->
                                     GetNextHopTable()->FindActiveEntry(key));
    ASSERT_TRUE(cnh == NULL);

    XmppSubnetTearDown();

    WAIT_FOR(1000, 10000, (Agent::GetInstance()->GetVrfTable()->Size() == 1));

    xc->ConfigUpdate(new XmppConfigData());
    client->WaitForIdle(5);
}

TEST_F(AgentXmppUnitTest, SubnetBcast_Test_sessiondown_after_vn_vrf_link_del) {

    client->Reset();
    client->WaitForIdle();
 
    XmppConnectionSetUp(true);

    EXPECT_TRUE(Agent::GetInstance()->GetControlNodeMulticastBuilder() != NULL);
    EXPECT_STREQ(Agent::GetInstance()->GetControlNodeMulticastBuilder()->
                 GetXmppServer().c_str(), "127.0.0.1");

	IpamInfo ipam_info[] = {
	    {"1.1.1.0", 24, "1.1.1.200"}
	};
	
    AddIPAM("vn1", ipam_info, 1);
    client->WaitForIdle();

    Ip4Address addr = Ip4Address::from_string("1.1.1.255");
    ASSERT_TRUE(RouteFind("vrf1", addr, 32));
    Inet4UnicastRouteEntry *rt = static_cast<Inet4UnicastRouteEntry *>
        (RouteGet("vrf1", addr, 32));
    NextHop *nh = const_cast<NextHop *>(rt->GetActiveNextHop());
    ASSERT_TRUE(nh != NULL);
    ASSERT_TRUE(nh->GetType() == NextHop::COMPOSITE);
    CompositeNH *cnh = static_cast<CompositeNH *>(nh);
    MulticastGroupObject *obj;
    obj = MulticastHandler::GetInstance()->FindGroupObject(cnh->vrf_name(),
		    cnh->GetGrpAddr());
    EXPECT_TRUE(obj != NULL);

    //Delete VRF VN link
    DelLink("virtual-network", "vn1", "routing-instance", "vrf1");
    WAIT_FOR(1000, 1000, (RouteFind("vrf1", addr, 32) == false));

    //bring-down the channel
    bgp_peer.get()->HandleXmppChannelEvent(xmps::NOT_READY);
    client->WaitForIdle();

    EXPECT_TRUE(RouteFind("vrf1", addr, 32) == false);
    obj = MulticastHandler::GetInstance()->FindGroupObject("vrf1", addr);
    EXPECT_TRUE(obj != NULL);
    CompositeNHKey *key = new CompositeNHKey(obj->vrf_name(), 
                                             obj->GetGroupAddress(),
                                             obj->GetSourceAddress(), false,
                                             Composite::L3COMP); 
    cnh = static_cast<CompositeNH *>(Agent::GetInstance()->
                                     GetNextHopTable()->FindActiveEntry(key));
    ASSERT_TRUE(cnh == NULL);

    XmppSubnetTearDown();

    WAIT_FOR(1000, 10000, (Agent::GetInstance()->GetVrfTable()->Size() == 1));

    xc->ConfigUpdate(new XmppConfigData());
    client->WaitForIdle(5);
}

}
