/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <vector>
#include <string>
#include <base/logging.h>
#include <boost/bind.hpp>
#include <tbb/task.h>
#include <base/task.h>
#include "io/test/event_manager_test.h"
#include <net/bgp_af.h>

#include <cmn/agent_cmn.h>
#include "base/test/task_test_util.h"

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

#include "xml/xml_pugi.h"

#include "controller/controller_peer.h" 
#include "controller/controller_export.h" 
#include "controller/controller_vrf_export.h" 
#include "controller/controller_types.h" 

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

class AgentBgpXmppPeerTest : public AgentXmppChannel {
public:
    AgentBgpXmppPeerTest(XmppChannel *channel, std::string xs, uint8_t xs_idx) :
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
        LOG(DEBUG, "Mock control-node rx_count:" << rx_count_);
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
                                            const string &from, const string &to,
                                            bool isclient) {
        XmppChannelConfig *cfg = new XmppChannelConfig(isclient);
        cfg->endpoint.address(boost::asio::ip::address::from_string("127.0.0.1"));
        cfg->endpoint.port(port);
        cfg->ToAddr = to;
        cfg->FromAddr = from;
        return cfg;
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

    xml_node MessageHeader(xml_document *xdoc, std::string vrf, std::string family="v4") {
        xml_node msg = xdoc->append_child("message");
        msg.append_attribute("type") = "set";
        msg.append_attribute("from") = XmppInit::kAgentNodeJID; 
        string str(XmppInit::kControlNodeJID);
        str += "/";
        str += XmppInit::kBgpPeer;
        msg.append_attribute("to") = str.c_str();

        xml_node event = msg.append_child("event");
        event.append_attribute("xmlns") = "http://jabber.org/protocol/pubsub";
        xml_node xitems = event.append_child("items");
        stringstream node;
        if (family.compare("v6") == 0) {
            node << BgpAf::IPv6 << "/" << BgpAf::Unicast << "/" << vrf.c_str();
        } else {
            node << BgpAf::IPv4 << "/" << BgpAf::Unicast << "/" << vrf.c_str();
        }
        xitems.append_attribute("node") = node.str().c_str();
        return(xitems);
    }

    xml_node L2MessageHeader(xml_document *xdoc, std::string vrf) {
        xml_node msg = xdoc->append_child("message");
        msg.append_attribute("type") = "set";
        msg.append_attribute("from") = XmppInit::kAgentNodeJID; 
        string str(XmppInit::kControlNodeJID);
        str += "/";
        str += XmppInit::kBgpPeer;
        msg.append_attribute("to") = str.c_str();

        xml_node event = msg.append_child("event");
        event.append_attribute("xmlns") = "http://jabber.org/protocol/pubsub";
        xml_node xitems = event.append_child("items");
        stringstream ss;
        ss << "25" << "/" << "242" << "/" << vrf.c_str();
        std::string node_str(ss.str());
        xitems.append_attribute("node") = node_str.c_str();
        return(xitems);
    }

    void SendRouteMessage(ControlNodeMockBgpXmppPeer *peer, std::string vrf,
                          std::string address, int label, 
                          const char *vn = "vn1") {
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
        item.entry.virtual_network = vn;

        xml_node node = xitems.append_child("item");
        node.append_attribute("id") = address.c_str();
        item.Encode(&node);

        SendDocument(xdoc, peer);
    }

    void SendRouteV6Message(ControlNodeMockBgpXmppPeer *peer, std::string vrf,
                            std::string address, int label, 
                            const char *vn = "vn1") {
        xml_document xdoc;
        xml_node xitems = MessageHeader(&xdoc, vrf, "v6");

        autogen::NextHopType item_nexthop;
        item_nexthop.af = BgpAf::IPv4;
        item_nexthop.address = Agent::GetInstance()->router_id().to_string();;
        item_nexthop.label = label;
        
        autogen::ItemType item;
        item.entry.next_hops.next_hop.push_back(item_nexthop);
        item.entry.nlri.af = BgpAf::IPv6;
        item.entry.nlri.safi = BgpAf::Unicast;
        item.entry.nlri.address = address.c_str();
        item.entry.version = 1;
        item.entry.virtual_network = vn;

        xml_node node = xitems.append_child("item");
        node.append_attribute("id") = address.c_str();
        item.Encode(&node);

        SendDocument(xdoc, peer);
    }

    void SendRouteMessageSg(ControlNodeMockBgpXmppPeer *peer, std::string vrf,
                          std::string address, int label, 
                          const char *vn = "vn1") {
        xml_document xdoc;
        xml_node xitems = MessageHeader(&xdoc, vrf);

        autogen::NextHopType item_nexthop;
        item_nexthop.af = BgpAf::IPv4;
        item_nexthop.address = Agent::GetInstance()->router_id().to_string();;
        item_nexthop.label = label;
        
        autogen::ItemType item;
        item.entry.nlri.af = BgpAf::IPv4;
        item.entry.nlri.safi = BgpAf::Unicast;
        item.entry.nlri.address = address.c_str();
        item.entry.version = 1;
        item.entry.virtual_network = vn;
        item.entry.next_hops.next_hop.push_back(item_nexthop);
        item.entry.security_group_list.security_group.push_back(1);
        item.entry.security_group_list.security_group.push_back(2);

        xml_node node = xitems.append_child("item");
        node.append_attribute("id") = address.c_str();
        item.Encode(&node);

        SendDocument(xdoc, peer);
    }

    void SendL2RouteMessage(ControlNodeMockBgpXmppPeer *peer, std::string vrf,
                          std::string mac_string, std::string address, int label, 
                          const char *vn = "vn1", bool is_vxlan = false) {
        xml_document xdoc;
        xml_node xitems = L2MessageHeader(&xdoc, vrf);

        autogen::EnetNextHopType item_nexthop;
        item_nexthop.af = 1;
        item_nexthop.address = Agent::GetInstance()->router_id().to_string();
        item_nexthop.label = label;
        if (is_vxlan) {
            item_nexthop.tunnel_encapsulation_list.tunnel_encapsulation.push_back("vxlan");
        } else {
            item_nexthop.tunnel_encapsulation_list.tunnel_encapsulation.push_back("gre");
            item_nexthop.tunnel_encapsulation_list.tunnel_encapsulation.push_back("udp");
        }
        
        autogen::EnetItemType item;
        item.entry.nlri.af = 25;
        item.entry.nlri.safi = 242;
        item.entry.nlri.mac = mac_string.c_str();
        item.entry.nlri.address = address.c_str();

        item.entry.next_hops.next_hop.push_back(item_nexthop);

        xml_node node = xitems.append_child("item");
        stringstream ss;
        ss << mac_string.c_str() << "," << address.c_str(); 
        string node_str(ss.str());
        node.append_attribute("id") = node_str.c_str();
        item.Encode(&node);

        SendDocument(xdoc, peer);
    }
    
    void SendRouteDeleteMessage(ControlNodeMockBgpXmppPeer *peer,
                                std::string address, std::string vrf,
                                std::string family="v4") {
        xml_document xdoc;
        xml_node xitems = MessageHeader(&xdoc, vrf, family);
        xml_node node = xitems.append_child("retract");
        node.append_attribute("id") = address.c_str();

        SendDocument(xdoc, peer);
    }

    void SendL2RouteDeleteMessage(ControlNodeMockBgpXmppPeer *peer,
                                  std::string mac_string, std::string vrf,
                                  std::string address) {
        xml_document xdoc;
        xml_node xitems = L2MessageHeader(&xdoc, vrf);
        xml_node node = xitems.append_child("retract");
        stringstream ss;
        ss << mac_string.c_str() << "," << address.c_str(); 
        string node_str(ss.str());
        node.append_attribute("id") = node_str.c_str();

        SendDocument(xdoc, peer);
    }


    void XmppConnectionSetUp() {

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
					xs->GetPort(),
					XmppInit::kAgentNodeJID, 
					XmppInit::kControlNodeJID, true));
	xc->ConfigUpdate(xmppc_cfg);

	// client connection
	cchannel = xc->FindChannel(XmppInit::kControlNodeJID);
	//Create agent bgp peer
        bgp_peer.reset(new AgentBgpXmppPeerTest(cchannel,
                       Agent::GetInstance()->controller_ifmap_xmpp_server(0), 0));
	xc->RegisterConnectionEvent(xmps::BGP,
	    boost::bind(&AgentBgpXmppPeerTest::HandleXmppChannelEvent, 
			bgp_peer.get(), _2));
	Agent::GetInstance()->set_controller_xmpp_channel(bgp_peer.get(), 0);

        // server connection
        WAIT_FOR(1000, 10000,
            ((sconnection = xs->FindConnection(XmppInit::kAgentNodeJID)) != NULL));
        assert(sconnection);
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
    Agent *agent_;
};


namespace {

TEST_F(AgentXmppUnitTest, Connection) {

    client->Reset();
    client->WaitForIdle();

    XmppConnectionSetUp();
    //wait for connection establishment
    WAIT_FOR(1000, 10000, (sconnection->GetStateMcState() == xmsm::ESTABLISHED));
    WAIT_FOR(1000, 10000, (cchannel->GetPeerState() == xmps::READY));

    // Create vm-port and vn
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1, "fd12::1"},
    };

    //expect subscribe for __default__ at the mock server
    WAIT_FOR(1000, 10000, (mock_peer.get()->Count() == 1));

    VxLanNetworkIdentifierMode(false);
    client->WaitForIdle();
    //Create vn,vrf,vm,vm-port and route entry in vrf1 
    CreateVmportEnv(input, 1);
    //expect subscribe message+route at the mock server
    WAIT_FOR(1000, 10000, (mock_peer.get()->Count() == 7));
    client->WaitForIdle();

    AddVrf("vrf2");
    VnAddReq(2, "vn2", 0, "vrf2");
    //expect subscribe message at the mock server
    WAIT_FOR(1000, 10000, (mock_peer.get()->Count() == 8));

    Ip4Address addr = Ip4Address::from_string("1.1.1.1");
    EXPECT_TRUE(VmPortActive(input, 0));
    EXPECT_TRUE(RouteFind("vrf1", addr, 32));
    Inet4UnicastRouteEntry *rt = RouteGet("vrf1", addr, 32);
    EXPECT_STREQ(rt->dest_vn_name().c_str(), "vn1");

    const struct ether_addr *mac = ether_aton("00:00:00:01:01:01");
    EXPECT_TRUE(L2RouteFind("vrf1", *mac));

    Ip6Address addr6 = Ip6Address::from_string("fd12::1");
    EXPECT_TRUE(RouteFindV6("vrf1", addr6, 128));
    Inet6UnicastRouteEntry *rt6 = RouteGetV6("vrf1", addr6, 128);
    EXPECT_STREQ(rt6->dest_vn_name().c_str(), "vn1");

    // Send route, back to vrf1
    SendRouteMessage(mock_peer.get(), "vrf1", "1.1.1.1/32", 
                     MplsTable::kStartLabel);
    SendRouteV6Message(mock_peer.get(), "vrf1", "fd12::1/128", 
                       MplsTable::kStartLabel);
    SendL2RouteMessage(mock_peer.get(), "vrf1", "00:00:00:01:01:01", 
                     "1.1.1.1/24", MplsTable::kStartLabel + 1);
    // Route reflected to vrf1; Ipv4 and L2
    WAIT_FOR(1000, 10000, (bgp_peer.get()->Count() == 3));

    // Send route, leak to vrf2
    SendRouteMessage(mock_peer.get(), "vrf2", "1.1.1.1/32", 
                     MplsTable::kStartLabel);
    // Route reflected to vrf2; IPv4 route
    WAIT_FOR(1000, 10000, (bgp_peer.get()->Count() == 4));

    // Send route, leak to vrf2
    SendRouteV6Message(mock_peer.get(), "vrf2", "fd12::1/128", 
                       MplsTable::kStartLabel);
    // Route reflected to vrf2; IPv6 route
    WAIT_FOR(1000, 10000, (bgp_peer.get()->Count() == 5));

    // Route leaked to vrf2, check entry in route-table
    WAIT_FOR(1000, 10000, (RouteFind("vrf2", addr, 32) == true));
    Inet4UnicastRouteEntry *rt2 = RouteGet("vrf2", addr, 32);
    WAIT_FOR(1000, 10000, (rt2->GetActivePath() != NULL));
    WAIT_FOR(1000, 10000, rt2->dest_vn_name().size() > 0);
    EXPECT_STREQ(rt2->dest_vn_name().c_str(), "vn1");

    // v6 Route leaked to vrf2, check entry in route-table
    WAIT_FOR(1000, 10000, (RouteFindV6("vrf2", addr6, 128) == true));
    Inet6UnicastRouteEntry *rt2_v6 = RouteGetV6("vrf2", addr6, 128);
    WAIT_FOR(1000, 10000, (rt2_v6->GetActivePath() != NULL));
    WAIT_FOR(1000, 10000, rt2_v6->dest_vn_name().size() > 0);
    EXPECT_STREQ(rt2_v6->dest_vn_name().c_str(), "vn1");

    // Delete vm-port and route entry in vrf1
    DeleteVmportEnv(input, 1, true);
    client->WaitForIdle();

    // Dissociate messages at the mock control-node for 
    // ipv4, ipv6, multicast and l2 routes
    // unsubscribe from vrf1
    WAIT_FOR(1000, 10000, (mock_peer.get()->Count() == 14));

    //Send route delete
    SendRouteDeleteMessage(mock_peer.get(), "1.1.1.1/32", "vrf1");
    SendRouteDeleteMessage(mock_peer.get(), "fd12::1/128", "vrf1", "v6");
    SendL2RouteDeleteMessage(mock_peer.get(), "00:00:00:01:01:01", 
                             "vrf1", "1.1.1.1");
    // Route delete for vrf1 
    WAIT_FOR(1000, 10000, (bgp_peer.get()->Count() == 8));

    //Send route delete for leaked routes
    SendRouteDeleteMessage(mock_peer.get(), "1.1.1.1/32", "vrf2");
    SendRouteDeleteMessage(mock_peer.get(), "fd12::1/128", "vrf2", "v6");
    // Route delete for vrf2 
    WAIT_FOR(1000, 10000, (bgp_peer.get()->Count() == 10));

    //Confirm route has been cleaned up
    WAIT_FOR(1000, 10000, (RouteFind("vrf1", addr, 32) == false));
    WAIT_FOR(1000, 10000, (RouteFind("vrf2", addr, 32) == false));
    WAIT_FOR(1000, 10000, (L2RouteFind("vrf1", *mac) == false));
    WAIT_FOR(1000, 10000, (RouteFindV6("vrf1", addr6, 128) == false));
    WAIT_FOR(1000, 10000, (RouteFindV6("vrf2", addr6, 128) == false));
    WAIT_FOR(1000, 10000, (L2RouteFind("vrf2", *mac) == false));
    
    //Confirm Vmport is deleted
    EXPECT_FALSE(VmPortFind(input, 0));

    DelVrf("vrf2");
    client->WaitForIdle();

    //unsubscribe message for vrf2
    WAIT_FOR(1000, 10000, (mock_peer.get()->Count() == 15));

    EXPECT_EQ(1U, Agent::GetInstance()->vn_table()->Size());
    VnDelReq(2);
    client->WaitForIdle();
    EXPECT_EQ(0U, Agent::GetInstance()->vn_table()->Size());

    TaskScheduler::GetInstance()->Stop();
    Agent::GetInstance()->controller()->unicast_cleanup_timer().cleanup_timer_->Fire();
    TaskScheduler::GetInstance()->Start();
    client->WaitForIdle();

    EXPECT_FALSE(DBTableFind("vrf1.l2.route.0"));
    EXPECT_FALSE(DBTableFind("vrf1.uc.route6.0"));
    EXPECT_FALSE(DBTableFind("vrf1.uc.route.0"));
    EXPECT_FALSE(VrfFind("vrf1"));
    EXPECT_FALSE(DBTableFind("vrf2.uc.route.0"));
    EXPECT_FALSE(DBTableFind("vrf2.uc.route6.0"));
    EXPECT_FALSE(DBTableFind("vrf2.l2.route.0"));
    EXPECT_FALSE(VrfFind("vrf2"));

    xc->ConfigUpdate(new XmppConfigData());
    client->WaitForIdle(5);

}

TEST_F(AgentXmppUnitTest, ConnectionUpDown) {

    client->Reset();
    client->WaitForIdle();

    XmppConnectionSetUp();
    //wait for connection establishment
    WAIT_FOR(1000, 10000, (sconnection->GetStateMcState() == xmsm::ESTABLISHED));
    WAIT_FOR(1000, 10000, (cchannel->GetPeerState() == xmps::READY));

    //expect subscribe for __default__ at the mock server
    WAIT_FOR(1000, 10000, (mock_peer.get()->Count() == 1));

    VxLanNetworkIdentifierMode(false);
    client->WaitForIdle();
    // Create vm-port in vn1 
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.2", "00:00:00:01:01:02", 1, 1, "fd12::2"},
    };

    //Create vn,vrf,vm,vm-port and route entry in vrf1 
    CreateVmportEnv(input, 1);
    //expect vr subscribe, subscribe message default and vrf1
    //routes at the mock server
    WAIT_FOR(1000, 10000, (mock_peer.get()->Count() == 7));
    client->WaitForIdle();

    Ip4Address addr = Ip4Address::from_string("1.1.1.2");
    EXPECT_TRUE(VmPortActive(input, 0));
    EXPECT_TRUE(RouteFind("vrf1", addr, 32));
    Inet4UnicastRouteEntry *rt = RouteGet("vrf1", addr, 32);
    EXPECT_STREQ(rt->dest_vn_name().c_str(), "vn1");

    const struct ether_addr *mac = ether_aton("00:00:00:01:01:02");
    EXPECT_TRUE(L2RouteFind("vrf1", *mac));
    Layer2RouteEntry *l2_rt = L2RouteGet("vrf1", *mac);

    Ip6Address addr6 = Ip6Address::from_string("fd12::2");
    EXPECT_TRUE(RouteFindV6("vrf1", addr6, 128));
    Inet6UnicastRouteEntry *rt6 = RouteGetV6("vrf1", addr6, 128);
    EXPECT_STREQ(rt6->dest_vn_name().c_str(), "vn1");

    //ensure active path is local-vm
    EXPECT_TRUE(rt->GetActivePath()->peer()->GetType() == Peer::LOCAL_VM_PORT_PEER);
    EXPECT_TRUE(rt6->GetActivePath()->peer()->GetType() == Peer::LOCAL_VM_PORT_PEER);
    EXPECT_TRUE(l2_rt->GetActivePath()->peer()->GetType() == Peer::LOCAL_VM_PORT_PEER);

    // Send route, back to vrf1
    SendRouteMessage(mock_peer.get(), "vrf1", "1.1.1.2/32", 
                     MplsTable::kStartLabel);
    SendRouteV6Message(mock_peer.get(), "vrf1", "fd12::2/128", 
                       MplsTable::kStartLabel);
    SendL2RouteMessage(mock_peer.get(), "vrf1", "00:00:00:01:01:02", 
                     "1.1.1.2/24", MplsTable::kStartLabel + 1);
    // Route reflected to vrf1; Ipv4, Ipv6 and L2
    WAIT_FOR(1000, 10000, (bgp_peer.get()->Count() == 3));
    client->WaitForIdle(); 

    //ensure active path is BGP 
    EXPECT_TRUE(rt->GetActivePath()->peer()->GetType() == Peer::BGP_PEER);
    EXPECT_TRUE(rt6->GetActivePath()->peer()->GetType() == Peer::BGP_PEER);
    EXPECT_TRUE(l2_rt->GetActivePath()->peer()->GetType() == Peer::BGP_PEER);

    AgentXmppChannel *ch = static_cast<AgentXmppChannel *>(bgp_peer.get());
    BgpPeer *bgp_peer_id = static_cast<BgpPeer *>(ch->bgp_peer_id());
    EXPECT_TRUE(rt->FindPath(bgp_peer_id) != NULL);
    WAIT_FOR(1000, 10000, !(rt->FindPath(bgp_peer_id)->is_stale())); 
    WAIT_FOR(1000, 10000, !(rt6->FindPath(bgp_peer_id)->is_stale())); 

    //bring-down the channel 
    bgp_peer.get()->HandleXmppChannelEvent(xmps::NOT_READY);

    //ensure route learnt via control-node is deleted 
    if (Agent::GetInstance()->headless_agent_mode()) {
        WAIT_FOR(1000, 10000, (rt->FindPath(bgp_peer_id)->is_stale())); 
        WAIT_FOR(1000, 10000, (rt6->FindPath(bgp_peer_id)->is_stale())); 
    } else {
        WAIT_FOR(1000, 10000, (rt->FindPath(bgp_peer_id) == NULL)); 
        WAIT_FOR(1000, 10000, (rt6->FindPath(bgp_peer_id) == NULL)); 
    }
    client->WaitForIdle(); 

    //bring up the channel 
    bgp_peer.get()->HandleXmppChannelEvent(xmps::READY); 
    client->WaitForIdle(5);

    //expect subscribe for vr, __default__, vrf1 and routes 
    WAIT_FOR(1000, 10000, (mock_peer.get()->Count() == 14));  

    // Delete vm-port and route entry in vrf1
    DeleteVmportEnv(input, 1, true);
    client->WaitForIdle();

    // Dissociate messages at the mock control-node for 
    // ipv4, ipv6, multicast and l2 routes
    //unsubscribe message for vrf1
    WAIT_FOR(1000, 10000, (mock_peer.get()->Count() == 20));

    //Confirm route has been cleaned up
    WAIT_FOR(1000, 10000, (RouteFind("vrf1", addr, 32) == false));
    WAIT_FOR(1000, 10000, (L2RouteFind("vrf1", *mac) == false));
    WAIT_FOR(1000, 10000, (RouteFindV6("vrf1", addr6, 128) == false));

    //Confirm Vmport is deleted
    EXPECT_FALSE(VmPortFind(input, 0));

    EXPECT_EQ(0U, Agent::GetInstance()->vn_table()->Size()); 

    TaskScheduler::GetInstance()->Stop();
    Agent::GetInstance()->controller()->unicast_cleanup_timer().cleanup_timer_->Fire();
    TaskScheduler::GetInstance()->Start();
    client->WaitForIdle();

    WAIT_FOR(1000, 10000, (Agent::GetInstance()->vrf_table()->Size() == 1));  
    EXPECT_FALSE(DBTableFind("vrf1.uc.route.0"));
    EXPECT_FALSE(DBTableFind("vrf1.uc.route6.0"));
    EXPECT_FALSE(DBTableFind("vrf1.l2.route.0"));
    EXPECT_FALSE(VrfFind("vrf1")); 

    xc->ConfigUpdate(new XmppConfigData()); 
    client->WaitForIdle(5); 
}
}
