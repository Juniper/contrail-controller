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
#include "controller/controller_route_path.h"

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
        AgentXmppChannel(Agent::GetInstance(), channel, xs, "0", xs_idx), 
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
        xs = new XmppServer(&evm_, XmppInit::kControlNodeJID);
        xc = new XmppClient(&evm_);
        xmpp_init = new XmppInit();

        xs->Initialize(0, false);
        
        thread_.Start();
    }

    virtual void TearDown() {
        ASSERT_TRUE(agent_->controller_xmpp_channel(0) != NULL);

        xs->Shutdown();
        bgp_peer.reset(); 
        client->WaitForIdle();
        xc->Shutdown();
        client->WaitForIdle();

        agent_->set_controller_ifmap_xmpp_client(NULL, 0);
        agent_->set_controller_ifmap_xmpp_init(NULL, 0);
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

    static void ValidateSandeshResponse(Sandesh *sandesh, vector<string> &result) {
#if 0
        AgentXmppConnectionStatus *resp =
                dynamic_cast<AgentXmppConnectionStatus *>(sandesh);

        std::vector<AgentXmppData> &list =
                const_cast<std::vector<AgentXmppData>&>(resp->get_peer());

        cout << "*******************************************************"<<endl;
        for (size_t i = 0; i < list.size(); i++) {

            if (result.size() >= 1) {
                EXPECT_STREQ(list[i].controller_ip.c_str(), result[i].c_str());
            }
            if (result.size() >= 2) {
                EXPECT_STREQ(list[i].cfg_controller.c_str(), result[i+1].c_str());
            }
            if (result.size() >= 3) {
                EXPECT_STREQ(list[i].state.c_str(), result[i+2].c_str());
            }

            cout << "Controller-IP:" << list[i].controller_ip << endl;
            cout << "Cfg-Controller-IP:" << list[i].cfg_controller << endl;
            cout << "State:" << list[i].state << endl;
        }
        cout << "*******************************************************"<<endl;
#endif       
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
        msg.append_attribute("from") = XmppInit::kAgentNodeJID; 
        string str(XmppInit::kControlNodeJID);
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
    
    void SendRouteDeleteMessage(ControlNodeMockBgpXmppPeer *peer, std::string vrf) {
        xml_document xdoc;
        xml_node xitems = MessageHeader(&xdoc, vrf);
        xml_node node = xitems.append_child("retract");
        node.append_attribute("id") = "1.1.1.1/32";

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
    agent_->set_controller_xmpp_channel(bgp_peer.get(), 0);
    agent_->set_controller_ifmap_xmpp_client(xc, 0);
    agent_->set_controller_ifmap_xmpp_init(xmpp_init, 0);

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
    XmppInit *xmpp_init;

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

    //Mock Sandesh request
    AgentXmppConnectionStatusReq  *xmpp_req = new AgentXmppConnectionStatusReq();
    std::vector<std::string> result_beg;
    result_beg.push_back("127.0.0.1");
    Sandesh::set_response_callback(boost::bind(ValidateSandeshResponse, _1, result_beg)); 
    xmpp_req->HandleRequest();
    client->WaitForIdle();
    xmpp_req->Release();

    XmppConnectionSetUp();
    //wait for connection establishment
    WAIT_FOR(1000, 10000, (sconnection->GetStateMcState() == xmsm::ESTABLISHED));
    WAIT_FOR(1000, 10000, (cchannel->GetPeerState() == xmps::READY));

    //Mock Sandesh request
    xmpp_req = new AgentXmppConnectionStatusReq();
    std::vector<std::string> result;
    result.push_back("127.0.0.1");
    result.push_back("Yes");
    result.push_back("Established");
    Sandesh::set_response_callback(boost::bind(ValidateSandeshResponse, _1, result)); 
    xmpp_req->HandleRequest();
    client->WaitForIdle();
    xmpp_req->Release();
    // Create vm-port and vn
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
    };

    //expect subscribe for __default__ at the mock server
    WAIT_FOR(1000, 10000, (mock_peer.get()->Count() == 1));

    VxLanNetworkIdentifierMode(false);
    client->WaitForIdle();
    //Create vn,vrf,vm,vm-port and route entry in vrf1 
    CreateVmportEnv(input, 1);
    //expect subscribe message+route at the mock server
    WAIT_FOR(1000, 10000, (mock_peer.get()->Count() == 6));
    client->WaitForIdle();

    VrfAddReq("vrf2");
    VnAddReq(2, "vn2", 0, "vrf2");
    //expect subscribe message at the mock server
    WAIT_FOR(1000, 10000, (mock_peer.get()->Count() == 7));

    Ip4Address addr = Ip4Address::from_string("1.1.1.1");
    EXPECT_TRUE(VmPortActive(input, 0));
    EXPECT_TRUE(RouteFind("vrf1", addr, 32));
    Inet4UnicastRouteEntry *rt = RouteGet("vrf1", addr, 32);
    EXPECT_STREQ(rt->dest_vn_name().c_str(), "vn1");

    const struct ether_addr *mac = ether_aton("00:00:00:01:01:01");
    EXPECT_TRUE(L2RouteFind("vrf1", *mac));

    // Send route, back to vrf1
    SendRouteMessage(mock_peer.get(), "vrf1", "1.1.1.1/32", 
                     MplsTable::kStartLabel);
    SendL2RouteMessage(mock_peer.get(), "vrf1", "00:00:00:01:01:01", 
                     "1.1.1.1/24", MplsTable::kStartLabel + 1);
    // Route reflected to vrf1; Ipv4 and L2
    WAIT_FOR(1000, 10000, (bgp_peer.get()->Count() == 2));

    // Send route, leak to vrf2
    SendRouteMessage(mock_peer.get(), "vrf2", "1.1.1.1/32", 
                     MplsTable::kStartLabel);
    // Route reflected to vrf2; IPv4 route
    WAIT_FOR(1000, 10000, (bgp_peer.get()->Count() == 3));

    // Route leaked to vrf2, check entry in route-table
    WAIT_FOR(1000, 10000, (RouteFind("vrf2", addr, 32) == true));
    Inet4UnicastRouteEntry *rt2 = RouteGet("vrf2", addr, 32);
    WAIT_FOR(1000, 10000, (rt2->GetActivePath() != NULL));
    WAIT_FOR(1000, 10000, rt2->dest_vn_name().size() > 0);
    EXPECT_STREQ(rt2->dest_vn_name().c_str(), "vn1");

    // Verify service chan routes
    // Add service interface-1
    AddVrf("vrf2");
    AddVmPortVrf("ser1", "2.2.2.1", 1);
    AddLink("virtual-machine-interface-routing-instance", "ser1",
            "routing-instance", "vrf2");
    AddLink("virtual-machine-interface-routing-instance", "ser1",
            "virtual-machine-interface", "vnet1");
    // Validate service vlan route
    WAIT_FOR(1000, 10000, (RouteGet("vrf2", Ip4Address::from_string("2.2.2.1"),
                                    32) != NULL));
    rt = RouteGet("vrf2", Ip4Address::from_string("2.2.2.1"), 32);
    EXPECT_TRUE(rt != NULL);
    // Send route, back to vrf1
    SendRouteMessage(mock_peer.get(), "vrf1", "2.2.2.0/24", rt->GetMplsLabel(),
                     "TestVn");
    // Route reflected to vrf1
    WAIT_FOR(1000, 10000, (bgp_peer.get()->Count() == 4));
    rt = RouteGet("vrf1", Ip4Address::from_string("2.2.2.0"), 24);
    EXPECT_TRUE(rt != NULL);
    EXPECT_STREQ(rt->dest_vn_name().c_str(),"TestVn");

    DelLink("virtual-machine-interface-routing-instance", "ser1",
            "routing-instance", "vrf2");
    DelLink("virtual-machine-interface-routing-instance", "ser1",
            "virtual-machine-interface", "vnet1");
    DelVmPortVrf("ser1");
    client->WaitForIdle();

    //Delete vm-port and route entry in vrf1
    IntfCfgDel(input, 0);
    client->WaitForIdle();

    //Send route delete
    SendRouteDeleteMessage(mock_peer.get(), "vrf1");
    SendL2RouteDeleteMessage(mock_peer.get(), "00:00:00:01:01:01", 
                             "vrf1", "1.1.1.1");
    // Route delete for vrf1 
    WAIT_FOR(1000, 10000, (bgp_peer.get()->Count() == 6));

    //Send route delete
    SendRouteDeleteMessage(mock_peer.get(), "vrf2");
    // Route delete for vrf2 
    WAIT_FOR(1000, 10000, (bgp_peer.get()->Count() == 7));

    //Confirm route has been cleaned up
    WAIT_FOR(1000, 10000, (RouteFind("vrf1", addr, 32) == false));
    WAIT_FOR(1000, 10000, (RouteFind("vrf2", addr, 32) == false));
    WAIT_FOR(1000, 10000, (L2RouteFind("vrf1", *mac) == false));
    
    DeleteVmportEnv(input, 1, true);
    //Confirm Vmport is deleted
    EXPECT_FALSE(VmPortFind(input, 0));

    DelVrf("vrf2");
    client->WaitForIdle();

    EXPECT_EQ(1U, Agent::GetInstance()->vn_table()->Size());
    VnDelReq(2);
    client->WaitForIdle();
    EXPECT_EQ(0U, Agent::GetInstance()->vn_table()->Size());

    TaskScheduler::GetInstance()->Stop();
    Agent::GetInstance()->controller()->unicast_cleanup_timer().cleanup_timer_->Fire();
    TaskScheduler::GetInstance()->Start();
    client->WaitForIdle();

    EXPECT_FALSE(DBTableFind("vrf1.l2.route.0"));
    EXPECT_FALSE(DBTableFind("vrf1.uc.route.0"));
    EXPECT_FALSE(VrfFind("vrf1"));
    EXPECT_FALSE(DBTableFind("vrf2.uc.route.0"));
    EXPECT_FALSE(DBTableFind("vrf2.l2.route.0"));
    EXPECT_FALSE(VrfFind("vrf2"));

    xc->ConfigUpdate(new XmppConfigData());
    client->WaitForIdle(5);

}

TEST_F(AgentXmppUnitTest, Del_db_req_by_deleted_peer_non_hv) {

    client->Reset();
    client->WaitForIdle();
    if (agent_->headless_agent_mode())
        return;


    XmppConnectionSetUp();
    //wait for connection establishment
    WAIT_FOR(1000, 10000, (sconnection->GetStateMcState() == xmsm::ESTABLISHED));
    WAIT_FOR(1000, 10000, (cchannel->GetPeerState() == xmps::READY));

    bgp_peer.get()->stop_scheduler(true);
    // Create vm-port and vn
    struct PortInfo input[] = {
        {"vnet10", 10, "1.1.1.10", "00:00:00:01:01:10", 10, 10},
    };

    //expect subscribe for __default__ at the mock server
    WAIT_FOR(1000, 10000, (mock_peer.get()->Count() == 1));

    VxLanNetworkIdentifierMode(false);
    client->WaitForIdle();
    //Create vn,vrf,vm,vm-port and route entry in vrf1
    CreateVmportEnv(input, 1);

    Ip4Address addr = Ip4Address::from_string("1.1.1.10");
    WAIT_FOR(1000, 10000, (VmPortActive(input, 0)));
    WAIT_FOR(1000, 10000, (RouteFind("vrf10", addr, 32)));
    Inet4UnicastRouteEntry *rt = RouteGet("vrf10", addr, 32);
    const BgpPeer *old_bgp_peer = Agent::GetInstance()->controller_xmpp_channel(0)->
        bgp_peer_id();

    Agent *agent = Agent::GetInstance();
    //Firstly add the path for the peer
    DBRequest nh_req(DBRequest::DB_ENTRY_ADD_CHANGE);
    nh_req.key.reset(new TunnelNHKey(agent->fabric_vrf_name(),
                                     agent->router_id(),
                                     Ip4Address::from_string("8.8.8.8"), false,
                                     TunnelType::ComputeType(TunnelType::MplsType())));
    nh_req.data.reset(new TunnelNHData());

    Inet4TunnelRouteAdd(old_bgp_peer, "vrf10", addr, 32,
                        Ip4Address::from_string("8.8.8.8"),
                        TunnelType::ComputeType(TunnelType::MplsType()),
                        100, "vn10", SecurityGroupList(), PathPreference());
    client->WaitForIdle();
    EXPECT_TRUE(rt->GetPathList().size() == 2);

    //Now bring down the channel
    AgentXmppChannel::HandleAgentXmppClientChannelEvent(bgp_peer.get(),
                                                        xmps::NOT_READY);
    client->WaitForIdle();
    EXPECT_TRUE(rt->GetPathList().size() == 1);

    //Cleanup
    DeleteVmportEnv(input, 1, true);
    //Confirm Vmport is deleted
    WAIT_FOR(1000, 10000, (VmPortActive(input, 0) == false));
    WAIT_FOR(1000, 10000, (RouteFind("vrf10", addr, 32) == false));

    WAIT_FOR(1000, 10000, (DBTableFind("vrf10.uc.route.0") == false));
    WAIT_FOR(1000, 10000, (VrfFind("vrf10") == false));

    bgp_peer.get()->stop_scheduler(false);
    xc->ConfigUpdate(new XmppConfigData());
    client->WaitForIdle(5);
}

TEST_F(AgentXmppUnitTest, resync_db_req_by_deleted_peer_non_hv) {

    client->Reset();
    client->WaitForIdle();
    if (agent_->headless_agent_mode())
        return;


    XmppConnectionSetUp();
    //wait for connection establishment
    WAIT_FOR(1000, 10000, (sconnection->GetStateMcState() == xmsm::ESTABLISHED));
    WAIT_FOR(1000, 10000, (cchannel->GetPeerState() == xmps::READY));

    bgp_peer.get()->stop_scheduler(true);
    // Create vm-port and vn
    struct PortInfo input[] = {
        {"vnet10", 10, "1.1.1.10", "00:00:00:01:01:10", 10, 10},
    };

    //expect subscribe for __default__ at the mock server
    WAIT_FOR(1000, 10000, (mock_peer.get()->Count() == 1));

    VxLanNetworkIdentifierMode(false);
    client->WaitForIdle();
    //Create vn,vrf,vm,vm-port and route entry in vrf1
    CreateVmportEnv(input, 1);

    Ip4Address addr = Ip4Address::from_string("1.1.1.10");
    WAIT_FOR(1000, 10000, (VmPortActive(input, 0)));
    WAIT_FOR(1000, 10000, (RouteFind("vrf10", addr, 32)));
    Inet4UnicastRouteEntry *rt = RouteGet("vrf10", addr, 32);
    const BgpPeer *old_bgp_peer = Agent::GetInstance()->controller_xmpp_channel(0)->
        bgp_peer_id();
    const AgentXmppChannel *channel = old_bgp_peer->GetBgpXmppPeerConst();
    uint64_t sequence_number = channel->unicast_sequence_number();

    AgentXmppChannel::HandleAgentXmppClientChannelEvent(bgp_peer.get(),
                                                        xmps::NOT_READY);
    client->WaitForIdle();

    Agent *agent = Agent::GetInstance();
    ControllerVmRoute *data =
        ControllerVmRoute::MakeControllerVmRoute(old_bgp_peer,
                              Agent::GetInstance()->fabric_vrf_name(),
                              Agent::GetInstance()->router_id(), "vrf10",
                              addr, TunnelType::ComputeType(TunnelType::MplsType()),
                              100, "vn10", SecurityGroupList(),
                              PathPreference());
    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    Inet4UnicastRouteKey *key =
        new Inet4UnicastRouteKey(old_bgp_peer, "vrf10", addr, 32);
    key->sub_op_ = AgentKey::RESYNC;
    req.key.reset(key);
    req.data.reset(data);
    AgentRouteTable *table =
        agent->vrf_table()->GetInet4UnicastRouteTable("vrf10");
    if (table) {
        table->Enqueue(&req);
    }
    client->WaitForIdle();
    EXPECT_TRUE(rt->GetPathList().size() == 1);

    //Try adding local route with remote dead peer. SHould get ignored
    //and path count shud remain to local peer ie 1
    VmInterfaceKey intf_key(AgentKey::ADD_DEL_CHANGE,
                            MakeUuid(1), "");
    ControllerLocalVmRoute *local_vm_route =
        new ControllerLocalVmRoute(intf_key, 10, 100, false, "",
                                   InterfaceNHFlags::INET4,
                                   SecurityGroupList(),
                                   PathPreference(),
                                   sequence_number,
                                   channel);
    DBRequest localvm_req(DBRequest::DB_ENTRY_ADD_CHANGE);
    key = new Inet4UnicastRouteKey(old_bgp_peer, "vrf10", addr, 32);
    key->sub_op_ = AgentKey::RESYNC;
    localvm_req.key.reset(key);
    localvm_req.data.reset(local_vm_route);
    if (table) {
        table->Enqueue(&localvm_req);
    }
    client->WaitForIdle();
    EXPECT_TRUE(rt->GetPathList().size() == 1);

    // Add vlannhroute with old peer. It should be ignored.
    ControllerVlanNhRoute *vlan_rt_data =
        new ControllerVlanNhRoute(intf_key, 10, 11, "", SecurityGroupList(),
                                  PathPreference(), sequence_number, channel);
    DBRequest vlanrt_req(DBRequest::DB_ENTRY_ADD_CHANGE);
    key = new Inet4UnicastRouteKey(old_bgp_peer, "vrf10",
                                   Ip4Address::from_string("2.2.2.0"), 24);
    key->sub_op_ = AgentKey::RESYNC;
    vlanrt_req.key.reset(key);
    vlanrt_req.data.reset(vlan_rt_data);
    if (table) {
        table->Enqueue(&vlanrt_req);
    }
    client->WaitForIdle();
    EXPECT_TRUE(RouteGet("vrf1", Ip4Address::from_string("2.2.2.0"), 24) ==
                NULL);

    //Interface route
    InetInterfaceKey inet_intf_key("something");
    ControllerInetInterfaceRoute *inet_interface_route =
        new ControllerInetInterfaceRoute(inet_intf_key, 10,
                                         TunnelType::GREType(), "",
                                         sequence_number, channel);
    DBRequest inet_rt_req(DBRequest::DB_ENTRY_ADD_CHANGE);
    key = new Inet4UnicastRouteKey(old_bgp_peer, "vrf10",
                                   Ip4Address::from_string("3.3.3.3"), 32);
    key->sub_op_ = AgentKey::RESYNC;
    inet_rt_req.key.reset(key);
    inet_rt_req.data.reset(inet_interface_route);
    if (table) {
        table->Enqueue(&inet_rt_req);
    }
    client->WaitForIdle();
    EXPECT_TRUE(RouteGet("vrf1", Ip4Address::from_string("3.3.3.3"), 32) ==
                NULL);

    //Cleanup
    DeleteVmportEnv(input, 1, true);
    //Confirm Vmport is deleted
    WAIT_FOR(1000, 10000, (VmPortActive(input, 0) == false));
    WAIT_FOR(1000, 10000, (RouteFind("vrf10", addr, 32) == false));

    WAIT_FOR(1000, 10000, (DBTableFind("vrf10.uc.route.0") == false));
    WAIT_FOR(1000, 10000, (VrfFind("vrf10") == false));

    bgp_peer.get()->stop_scheduler(false);
    xc->ConfigUpdate(new XmppConfigData());
    client->WaitForIdle(5);
}

TEST_F(AgentXmppUnitTest, Add_db_inetinterface_req_by_deleted_peer_non_hv) {

    client->Reset();
    client->WaitForIdle();
    if (agent_->headless_agent_mode())
        return;


    XmppConnectionSetUp();
    //wait for connection establishment
    WAIT_FOR(1000, 10000, (sconnection->GetStateMcState() == xmsm::ESTABLISHED));
    WAIT_FOR(1000, 10000, (cchannel->GetPeerState() == xmps::READY));

    bgp_peer.get()->stop_scheduler(true);
    // Create vm-port and vn
    struct PortInfo input[] = {
        {"vnet10", 10, "1.1.1.10", "00:00:00:01:01:10", 10, 10},
    };

    //expect subscribe for __default__ at the mock server
    WAIT_FOR(1000, 10000, (mock_peer.get()->Count() == 1));

    VxLanNetworkIdentifierMode(false);
    client->WaitForIdle();
    //Create vn,vrf,vm,vm-port and route entry in vrf1
    CreateVmportEnv(input, 1);

    Ip4Address addr = Ip4Address::from_string("1.1.1.10");
    WAIT_FOR(1000, 10000, (VmPortActive(input, 0)));
    WAIT_FOR(1000, 10000, (RouteFind("vrf10", addr, 32)));
    Inet4UnicastRouteEntry *rt = RouteGet("vrf10", addr, 32);
    const BgpPeer *old_bgp_peer = Agent::GetInstance()->controller_xmpp_channel(0)->
        bgp_peer_id();
    const AgentXmppChannel *channel = old_bgp_peer->GetBgpXmppPeerConst();
    uint64_t sequence_number = channel->unicast_sequence_number();

    AgentXmppChannel::HandleAgentXmppClientChannelEvent(bgp_peer.get(),
                                                        xmps::NOT_READY);
    client->WaitForIdle();

    Agent *agent = Agent::GetInstance();

    //Try adding remote tunnel path for route 1.1.1.10 and it should
    //be ignored. Path remains 1
    DBRequest nh_req(DBRequest::DB_ENTRY_ADD_CHANGE);
    nh_req.key.reset(new TunnelNHKey(agent->fabric_vrf_name(),
                                     agent->router_id(),
                                     Ip4Address::from_string("8.8.8.8"), false,
                                     TunnelType::ComputeType(TunnelType::MplsType())));
    nh_req.data.reset(new TunnelNHData());

    Inet4TunnelRouteAdd(old_bgp_peer, "vrf10", addr, 32,
                        Ip4Address::from_string("8.8.8.8"),
                        TunnelType::ComputeType(TunnelType::MplsType()),
                        100, "vn10", SecurityGroupList(), PathPreference());
    client->WaitForIdle();
    EXPECT_TRUE(rt->GetPathList().size() == 1);

    //Try adding local route with remote dead peer. SHould get ignored
    //and path count shud remain to local peer ie 1
    VmInterfaceKey intf_key(AgentKey::ADD_DEL_CHANGE,
                            MakeUuid(1), "");
    ControllerLocalVmRoute *local_vm_route =
        new ControllerLocalVmRoute(intf_key, 10, 100, false, "",
                                   InterfaceNHFlags::INET4,
                                   SecurityGroupList(),
                                   PathPreference(),
                                   sequence_number,
                                   channel);
    agent->fabric_inet4_unicast_table()->AddLocalVmRouteReq(old_bgp_peer, "vrf1",
                                  addr, 32,
                                  static_cast<LocalVmRoute *>(local_vm_route));
    EXPECT_TRUE(rt->GetPathList().size() == 1);

    // Add vlannhroute with old peer. It should be ignored.
    ControllerVlanNhRoute *vlan_rt_data =
        new ControllerVlanNhRoute(intf_key, 10, 11, "", SecurityGroupList(),
                                  PathPreference(), sequence_number, channel);
    agent->fabric_inet4_unicast_table()->AddVlanNHRouteReq(old_bgp_peer,
           "vrf1", Ip4Address::from_string("2.2.2.0"), 24, vlan_rt_data);
    EXPECT_TRUE(RouteGet("vrf1", Ip4Address::from_string("2.2.2.0"), 24) ==
                NULL);

    //Interface route
    InetInterfaceKey inet_intf_key("something");
    ControllerInetInterfaceRoute *inet_interface_route =
        new ControllerInetInterfaceRoute(inet_intf_key, 10,
                                         TunnelType::GREType(), "",
                                         sequence_number, channel);
    agent->fabric_inet4_unicast_table()->AddInetInterfaceRouteReq(old_bgp_peer,
           "vrf1", Ip4Address::from_string("3.3.3.3"), 32, inet_interface_route);
    EXPECT_TRUE(RouteGet("vrf1", Ip4Address::from_string("3.3.3.3"), 32) ==
                NULL);

    //Cleanup
    DeleteVmportEnv(input, 1, true);
    //Confirm Vmport is deleted
    WAIT_FOR(1000, 10000, (VmPortActive(input, 0) == false));
    WAIT_FOR(1000, 10000, (RouteFind("vrf10", addr, 32) == false));

    WAIT_FOR(1000, 10000, (DBTableFind("vrf10.uc.route.0") == false));
    WAIT_FOR(1000, 10000, (VrfFind("vrf10") == false));

    bgp_peer.get()->stop_scheduler(false);
    xc->ConfigUpdate(new XmppConfigData());
    client->WaitForIdle(5);
}

TEST_F(AgentXmppUnitTest, CfgServerSelection) {

    client->Reset();
    client->WaitForIdle();

    XmppConnectionSetUp();
    //wait for connection establishment
    WAIT_FOR(1000, 10000, (sconnection->GetStateMcState() == xmsm::ESTABLISHED));
    WAIT_FOR(1000, 10000, (cchannel->GetPeerState() == xmps::READY));

    client->Reset();
    client->WaitForIdle(5);

    //Mock Sandesh request
    AgentXmppConnectionStatusReq *xmpp_req = new AgentXmppConnectionStatusReq();
    std::vector<string> result;
    result.push_back("127.0.0.1");
    result.push_back("Yes");
    result.push_back("Established");
    Sandesh::set_response_callback(boost::bind(ValidateSandeshResponse, _1, result)); 
    xmpp_req->HandleRequest();
    client->WaitForIdle();
    xmpp_req->Release();


    ASSERT_TRUE(Agent::GetInstance()->controller_ifmap_xmpp_server(0) == Agent::GetInstance()->ifmap_active_xmpp_server());

    //bring-down the channel
    bgp_peer.get()->HandleXmppChannelEvent(xmps::NOT_READY);
    client->WaitForIdle();
    ASSERT_TRUE(Agent::GetInstance()->ifmap_active_xmpp_server().empty() == 1);

    //Mock Sandesh request
    xmpp_req = new AgentXmppConnectionStatusReq();
    std::vector<string> result2;
    result2.push_back("127.0.0.1");
    result2.push_back("No");
    result2.push_back("Established");
    Sandesh::set_response_callback(boost::bind(ValidateSandeshResponse, _1, result2)); 
    xmpp_req->HandleRequest();
    client->WaitForIdle();
    xmpp_req->Release();

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

    //Mock Sandesh request
    AgentXmppConnectionStatusReq  *xmpp_req = new AgentXmppConnectionStatusReq();
    std::vector<string> result;
    result.push_back("127.0.0.1");
    result.push_back("Yes");
    result.push_back("Established");
    Sandesh::set_response_callback(boost::bind(ValidateSandeshResponse, _1, result)); 
    xmpp_req->HandleRequest();
    client->WaitForIdle();
    xmpp_req->Release();

    //expect subscribe for __default__ at the mock server
    WAIT_FOR(1000, 10000, (mock_peer.get()->Count() == 1));

    VxLanNetworkIdentifierMode(false);
    client->WaitForIdle();
    // Create vm-port in vn1 
    struct PortInfo input[] = {
        {"vnet2", 2, "1.1.1.2", "00:00:00:01:01:02", 1, 2},
    };

    //Create vn,vrf,vm,vm-port and route entry in vrf1 
    CreateVmportEnv(input, 1);
    //expect subscribe message+route at the mock server
    WAIT_FOR(1000, 10000, (mock_peer.get()->Count() == 6));
    client->WaitForIdle();

    Ip4Address addr = Ip4Address::from_string("1.1.1.2");
    EXPECT_TRUE(VmPortActive(input, 0));
    EXPECT_TRUE(RouteFind("vrf1", addr, 32));
    Inet4UnicastRouteEntry *rt = RouteGet("vrf1", addr, 32);
    EXPECT_STREQ(rt->dest_vn_name().c_str(), "vn1");

    const struct ether_addr *mac = ether_aton("00:00:00:01:01:02");
    EXPECT_TRUE(L2RouteFind("vrf1", *mac));
    Layer2RouteEntry *l2_rt = L2RouteGet("vrf1", *mac);

    //ensure active path is local-vm
    EXPECT_TRUE(rt->GetActivePath()->peer()->GetType() == Peer::LOCAL_VM_PORT_PEER);
    EXPECT_TRUE(l2_rt->GetActivePath()->peer()->GetType() == Peer::LOCAL_VM_PORT_PEER);

    // Send route, back to vrf1
    SendRouteMessage(mock_peer.get(), "vrf1", "1.1.1.2/32",
                     MplsTable::kStartLabel);
    SendL2RouteMessage(mock_peer.get(), "vrf1", "00:00:00:01:01:02",
                     "1.1.1.2/24", MplsTable::kStartLabel + 1);
    // Route reflected to vrf1
    WAIT_FOR(1000, 10000, (bgp_peer.get()->Count() == 2));

    //ensure active path is BGP
    EXPECT_TRUE(rt->GetActivePath()->peer()->GetType() == Peer::BGP_PEER);
    WAIT_FOR(1000, 10000, (l2_rt->GetActivePath()->peer()->GetType() == Peer::BGP_PEER));
    EXPECT_TRUE(l2_rt->GetActivePath()->peer()->GetType() == Peer::BGP_PEER);

    AgentXmppChannel *ch = static_cast<AgentXmppChannel *>(bgp_peer.get());
    BgpPeer *bgp_peer_id = static_cast<BgpPeer *>(ch->bgp_peer_id());
    EXPECT_TRUE(rt->FindPath(bgp_peer_id) != NULL);
        WAIT_FOR(1000, 10000, !(rt->FindPath(bgp_peer_id)->is_stale()));

    //bring-down the channel
    bgp_peer.get()->HandleXmppChannelEvent(xmps::NOT_READY);

    //ensure route learnt via control-node is deleted
    if (Agent::GetInstance()->headless_agent_mode()) {
        WAIT_FOR(1000, 10000, (rt->FindPath(bgp_peer_id)->is_stale()));
    } else {
        WAIT_FOR(1000, 10000, (rt->FindPath(bgp_peer_id) == NULL));
    }
    client->WaitForIdle();

    //Mock Sandesh request
    xmpp_req = new AgentXmppConnectionStatusReq();
    std::vector<string> result2;
    result2.push_back("127.0.0.1");
    result2.push_back("No");
    result2.push_back("Established");
    Sandesh::set_response_callback(boost::bind(ValidateSandeshResponse, _1, result2)); 
    xmpp_req->HandleRequest();
    client->WaitForIdle();
    xmpp_req->Release();

    //bring up the channel
    bgp_peer.get()->HandleXmppChannelEvent(xmps::READY);
    client->WaitForIdle(5);

    //expect subscribe for __default__, vrf1,route
    //at the mock server
    WAIT_FOR(1000, 10000, (mock_peer.get()->Count() == 12));

    VxLanNetworkIdentifierMode(false);
    client->WaitForIdle();
    //Add vm-port in vn 1
    struct PortInfo input2[] = {
        {"vnet3", 3, "1.1.1.3", "00:00:00:02:01:03", 1, 3},
    };
    CreateVmportEnv(input2, 1);
    client->WaitForIdle();

    Ip4Address addr2 = Ip4Address::from_string("1.1.1.3");
    EXPECT_TRUE(VmPortActive(input2, 0));
    EXPECT_TRUE(RouteFind("vrf1", addr2, 32));
    Inet4UnicastRouteEntry *rt2 = RouteGet("vrf1", addr2, 32);
    EXPECT_STREQ(rt2->dest_vn_name().c_str(), "vn1");

    const struct ether_addr *mac2 = ether_aton("00:00:00:02:01:03");
    EXPECT_TRUE(L2RouteFind("vrf1", *mac2));
    Layer2RouteEntry *l2_rt2 = L2RouteGet("vrf1", *mac2);

    //ensure active path is local-vm
    EXPECT_TRUE(rt2->GetActivePath()->peer()->GetType() == Peer::LOCAL_VM_PORT_PEER);
    EXPECT_TRUE(l2_rt2->GetActivePath()->peer()->GetType() == Peer::LOCAL_VM_PORT_PEER);

    //Delete vm-port and route entry in vrf1
    DeleteVmportEnv(input, 1, false);
    client->WaitForIdle();
    //Confirm Vmport is deleted
    EXPECT_FALSE(VmPortFind(input, 0)); 

    //Delete vm-port and route entry in vrf1
    DeleteVmportEnv(input2, 1, true);
    client->WaitForIdle();
    //Confirm Vmport is deleted
    EXPECT_FALSE(VmPortFind(input2, 0)); 

    EXPECT_EQ(0U, Agent::GetInstance()->vn_table()->Size());

    TaskScheduler::GetInstance()->Stop();
    Agent::GetInstance()->controller()->unicast_cleanup_timer().cleanup_timer_->Fire();
    TaskScheduler::GetInstance()->Start();
    client->WaitForIdle();

    WAIT_FOR(1000, 10000, (Agent::GetInstance()->vrf_table()->Size() == 1));
    EXPECT_FALSE(DBTableFind("vrf1.uc.route.0"));
    EXPECT_FALSE(DBTableFind("vrf1.l2.route.0"));
    EXPECT_FALSE(VrfFind("vrf1"));

    xc->ConfigUpdate(new XmppConfigData());
    client->WaitForIdle(5);

}

TEST_F(AgentXmppUnitTest, ConnectionUpDown_DecomissionedPeers) {

    client->Reset();
    client->WaitForIdle();

    XmppConnectionSetUp();
    //wait for connection establishment
    WAIT_FOR(1000, 10000, (sconnection->GetStateMcState() == xmsm::ESTABLISHED));
    WAIT_FOR(1000, 10000, (cchannel->GetPeerState() == xmps::READY));

    ASSERT_TRUE(agent_->controller()->DecommissionedPeerListSize()
                == 0);

    //bring-down the channel
    bgp_peer.get()->HandleXmppChannelEvent(xmps::NOT_READY);
    client->WaitForIdle();

    if (Agent::GetInstance()->headless_agent_mode()) {
        ASSERT_TRUE(agent_->controller()->DecommissionedPeerListSize()
                == 1);
    } else {
        ASSERT_TRUE(agent_->controller()->DecommissionedPeerListSize()
                == 0);
    }

    //bring up the channel
    bgp_peer.get()->HandleXmppChannelEvent(xmps::READY);
    client->WaitForIdle();

    if (Agent::GetInstance()->headless_agent_mode()) {
        ASSERT_TRUE(agent_->controller()->DecommissionedPeerListSize()
                == 1);
    } else {
        ASSERT_TRUE(agent_->controller()->DecommissionedPeerListSize()
                == 0);
    }

    //bring-down the channel
    bgp_peer.get()->HandleXmppChannelEvent(xmps::NOT_READY);
    client->WaitForIdle();

    if (Agent::GetInstance()->headless_agent_mode()) {
        ASSERT_TRUE(agent_->controller()->DecommissionedPeerListSize()
                == 2);
    } else {
        ASSERT_TRUE(agent_->controller()->DecommissionedPeerListSize()
                == 0);
    }

    xc->ConfigUpdate(new XmppConfigData());
    client->WaitForIdle(5);
}

TEST_F(AgentXmppUnitTest, SgList) {

    client->Reset();
    client->WaitForIdle();

    XmppConnectionSetUp();
    //wait for connection establishment
    WAIT_FOR(1000, 10000, (sconnection->GetStateMcState() == xmsm::ESTABLISHED));
    WAIT_FOR(1000, 10000, (cchannel->GetPeerState() == xmps::READY));

    // Create vm-port and vn
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
    };

    //expect subscribe for __default__ at the mock server
    WAIT_FOR(1000, 10000, (mock_peer.get()->Count() == 1));

    VxLanNetworkIdentifierMode(false);
    client->WaitForIdle();
    //Create vn,vrf,vm,vm-port and route entry in vrf1 
    CreateVmportEnv(input, 1);
    //expect subscribe message+route at the mock server
    //Message expected
    //1> VRF subscribe
    //2> IP route add
    //3> Layer 2 route add
    //4> All broadcast route
    //5> Broadcast layer 2 route
    WAIT_FOR(1000, 10000, (mock_peer.get()->Count() == 6));
    client->WaitForIdle();

    VrfAddReq("vrf2");
    VnAddReq(2, "vn2", 0, "vrf2");
    //expect subscribe message at the mock server
    WAIT_FOR(1000, 10000, (mock_peer.get()->Count() == 7));

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

    // Send route, leak to vrf2
    SendRouteMessageSg(mock_peer.get(), "vrf2", "1.1.1.1/32", 
                       MplsTable::kStartLabel);
    // Route leaked to vrf2
    WAIT_FOR(1000, 10000, (bgp_peer.get()->Count() == 2));

    // Route leaked to vrf2, check entry in route-table
    WAIT_FOR(1000, 10000, (RouteFind("vrf2", addr, 32) == true));
    Inet4UnicastRouteEntry *rt2 = RouteGet("vrf2", addr, 32);
    WAIT_FOR(1000, 10000, (rt2->GetActivePath() != NULL));
    WAIT_FOR(1000, 10000, rt2->dest_vn_name().size() > 0);
    EXPECT_STREQ(rt2->dest_vn_name().c_str(), "vn1");
    const SecurityGroupList sglist = rt2->GetActivePath()->sg_list();
    EXPECT_TRUE(sglist.size() == 2);
    

    //Delete vm-port and route entry in vrf1
    IntfCfgDel(input, 0);
    client->WaitForIdle();

    //Send route delete
    SendRouteDeleteMessage(mock_peer.get(), "vrf1");
    // Route delete for vrf1 
    WAIT_FOR(1000, 10000, (bgp_peer.get()->Count() == 3));

    //Send route delete
    SendRouteDeleteMessage(mock_peer.get(), "vrf2");
    // Route delete for vrf2 
    WAIT_FOR(1000, 10000, (bgp_peer.get()->Count() == 4));
    //WAIT_FOR(1000, 10000, (bgp_peer.get()->Count() == 5));

    //Confirm route has been cleaned up
    WAIT_FOR(1000, 10000, (RouteFind("vrf1", addr, 32) == false));
    WAIT_FOR(1000, 10000, (RouteFind("vrf2", addr, 32) == false));
    
    DeleteVmportEnv(input, 1, true);
    //Confirm Vmport is deleted
    EXPECT_FALSE(VmPortFind(input, 0));

    VrfDelReq("vrf2");
    client->WaitForIdle();

    EXPECT_EQ(1U, Agent::GetInstance()->vn_table()->Size());
    VnDelReq(2);
    client->WaitForIdle();
    EXPECT_EQ(0U, Agent::GetInstance()->vn_table()->Size());

    TaskScheduler::GetInstance()->Stop();
    Agent::GetInstance()->controller()->unicast_cleanup_timer().cleanup_timer_->Fire();
    TaskScheduler::GetInstance()->Start();
    client->WaitForIdle();

    EXPECT_FALSE(DBTableFind("vrf1.uc.route.0"));
    EXPECT_FALSE(DBTableFind("vrf1.l2.route.0"));
    EXPECT_FALSE(VrfFind("vrf1"));
    EXPECT_FALSE(DBTableFind("vrf2.uc.route.0"));
    EXPECT_FALSE(DBTableFind("vrf2.l2.route.0"));
    EXPECT_FALSE(VrfFind("vrf2"));

    xc->ConfigUpdate(new XmppConfigData());
    client->WaitForIdle(5);
}

TEST_F(AgentXmppUnitTest, TransparentSISgList) {

    client->Reset();
    client->WaitForIdle();

    XmppConnectionSetUp();
    //wait for connection establishment
    WAIT_FOR(1000, 10000, (sconnection->GetStateMcState() == xmsm::ESTABLISHED));
    WAIT_FOR(1000, 10000, (cchannel->GetPeerState() == xmps::READY));

    // Create vm-port and vn
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
    };

    //expect subscribe for __default__ at the mock server
    WAIT_FOR(1000, 10000, (mock_peer.get()->Count() == 1));

    VxLanNetworkIdentifierMode(false);
    client->WaitForIdle();
    //Create vn,vrf,vm,vm-port and route entry in vrf1
    CreateVmportEnv(input, 1);
    //expect subscribe message+route at the mock server
    WAIT_FOR(1000, 10000, (mock_peer.get()->Count() == 6));
    client->WaitForIdle();

    AddVrf("vrf2");
    AddVn("vn2", 2);
    //expect subscribe message at the mock server
    WAIT_FOR(1000, 10000, (mock_peer.get()->Count() == 7));

    AddVmPortVrf("ser1", "11.1.1.1", 1);
    AddLink("virtual-machine-interface-routing-instance", "ser1",
            "routing-instance", "vrf2");
    AddLink("virtual-machine-interface-routing-instance", "ser1",
            "virtual-machine-interface", "vnet1");
    client->WaitForIdle();
    //expect route at the mock server
    WAIT_FOR(1000, 10000, (mock_peer.get()->Count() == 8));

    Ip4Address addr = Ip4Address::from_string("11.1.1.1");
    EXPECT_TRUE(VmPortActive(input, 0));
    EXPECT_TRUE(RouteFind("vrf2", addr, 32));
    Inet4UnicastRouteEntry *rt = RouteGet("vrf2", addr, 32);
    EXPECT_STREQ(rt->dest_vn_name().c_str(), "vn1");

    // Send route, back to vrf2
    SendRouteMessageSg(mock_peer.get(), "vrf2", "11.1.1.1/32",
                       MplsTable::kStartLabel + 2);
    // Route reflected to vrf2
    WAIT_FOR(1000, 10000, (bgp_peer.get()->Count() == 1));

    // Route leaked to vrf2, check entry in route-table
    WAIT_FOR(1000, 10000, (RouteFind("vrf2", addr, 32) == true));
    Inet4UnicastRouteEntry *rt2 = RouteGet("vrf2", addr, 32);
    WAIT_FOR(1000, 10000, (rt2->GetActivePath() != NULL));
    WAIT_FOR(1000, 10000, rt2->dest_vn_name().size() > 0);
    EXPECT_STREQ(rt2->dest_vn_name().c_str(), "vn1");
    const SecurityGroupList sglist = rt2->GetActivePath()->sg_list();
    EXPECT_TRUE(sglist.size() == 2);

    //Delete vm-port and route entry in vrf1
    DelVrf("vrf2");
    DelVn("vn2");
    DeleteVmportEnv(input, 1, true);
    client->WaitForIdle();

    //Confirm route has been cleaned up
    WAIT_FOR(1000, 10000, (RouteFind("vrf2", addr, 32) == false));
    //Confirm Vmport is deleted
    EXPECT_FALSE(VmPortFind(input, 0));
    EXPECT_FALSE(DBTableFind("vrf1.uc.route.0"));
    EXPECT_FALSE(DBTableFind("vrf1.l2.route.0"));
    EXPECT_FALSE(VrfFind("vrf1"));
    EXPECT_FALSE(DBTableFind("vrf2.uc.route.0"));
    EXPECT_FALSE(DBTableFind("vrf2.l2.route.0"));
    EXPECT_FALSE(VrfFind("vrf2"));

    xc->ConfigUpdate(new XmppConfigData());
    client->WaitForIdle(5);
}

TEST_F(AgentXmppUnitTest, vxlan_peer_l2route_add) {

    client->Reset();
    AddEncapList("VXLAN", "MPLSoGRE", "MPLSoUDP");
    client->WaitForIdle();

    XmppConnectionSetUp();
    //wait for connection establishment
    WAIT_FOR(1000, 10000, (sconnection->GetStateMcState() == xmsm::ESTABLISHED));
    WAIT_FOR(1000, 10000, (cchannel->GetPeerState() == xmps::READY));

    //Mock Sandesh request
    AgentXmppConnectionStatusReq  *xmpp_req = new AgentXmppConnectionStatusReq();
    std::vector<string> result;
    result.push_back("127.0.0.1");
    result.push_back("Yes");
    result.push_back("Established");
    Sandesh::set_response_callback(boost::bind(ValidateSandeshResponse, _1, result)); 
    xmpp_req->HandleRequest();
    client->WaitForIdle();
    xmpp_req->Release();

    //expect subscribe for __default__ at the mock server
    WAIT_FOR(1000, 10000, (mock_peer.get()->Count() == 1));

    VxLanNetworkIdentifierMode(false);
    client->WaitForIdle();
    // Create vm-port in vn1 
    struct PortInfo input[] = {
        {"vnet2", 2, "1.1.1.2", "00:00:00:01:01:02", 1, 2},
    };

    //Create vn,vrf,vm,vm-port and route entry in vrf1
    CreateVmportEnv(input, 1);
    //expect subscribe message+route at the mock server
    WAIT_FOR(1000, 10000, (mock_peer.get()->Count() == 6));
    client->WaitForIdle();

    Ip4Address addr = Ip4Address::from_string("1.1.1.2");
    EXPECT_TRUE(VmPortActive(input, 0));
    EXPECT_TRUE(RouteFind("vrf1", addr, 32));
    Inet4UnicastRouteEntry *rt = RouteGet("vrf1", addr, 32);
    EXPECT_STREQ(rt->dest_vn_name().c_str(), "vn1");

    const struct ether_addr *mac = ether_aton("00:00:00:01:01:02");
    EXPECT_TRUE(L2RouteFind("vrf1", *mac));
    Layer2RouteEntry *l2_rt = L2RouteGet("vrf1", *mac);

    //ensure active path is local-vm
    EXPECT_TRUE(rt->GetActivePath()->peer()->GetType() == Peer::LOCAL_VM_PORT_PEER);
    EXPECT_TRUE(l2_rt->GetActivePath()->peer()->GetType() == Peer::LOCAL_VM_PORT_PEER);
    uint32_t vxlan_id = l2_rt->GetActivePath()->vxlan_id();
    EXPECT_TRUE(vxlan_id != VxLanTable::kInvalidvxlan_id);

    // Send route, back to vrf1
    SendL2RouteMessage(mock_peer.get(), "vrf1", "00:00:00:01:01:02",
                     "1.1.1.2/24", l2_rt->GetActivePath()->vxlan_id(),
                     "vn1", true);
    // Route reflected to vrf1
    WAIT_FOR(1000, 1000, (l2_rt->GetPathList().size() == 2));
    client->WaitForIdle();
    int path_count = 0;
    for (Route::PathList::const_iterator it = l2_rt->GetPathList().begin();
         it != l2_rt->GetPathList().end(); it++) {
        path_count++;
        const AgentPath *path = static_cast<const AgentPath *>(it.operator->());
        EXPECT_TRUE(path != NULL);
        EXPECT_TRUE(path->vxlan_id() == vxlan_id);
    }
    ASSERT_TRUE(path_count == 2);
 
    client->WaitForIdle(5);

    //Delete vm-port and route entry in vrf1
    DeleteVmportEnv(input, 1, true);
    WAIT_FOR(1000, 1000, !RouteFind("vrf1", addr, 32));
    WAIT_FOR(1000, 1000, (VmPortFind(input, 0) == false));
    client->WaitForIdle();
    DelEncapList();
    client->WaitForIdle();

    //Confirm Vmport is deleted
    EXPECT_FALSE(VmPortFind(input, 0)); 

    TaskScheduler::GetInstance()->Stop();
    Agent::GetInstance()->controller()->unicast_cleanup_timer().cleanup_timer_->Fire();
    TaskScheduler::GetInstance()->Start();
    client->WaitForIdle();

    WAIT_FOR(1000, 1000, (agent_->vn_table()->Size() == 0));

    EXPECT_FALSE(DBTableFind("vrf1.uc.route.0"));
    EXPECT_FALSE(DBTableFind("vrf1.l2.route.0"));
    EXPECT_FALSE(VrfFind("vrf1"));
    xc->ConfigUpdate(new XmppConfigData());
    client->WaitForIdle(5);
}

TEST_F(AgentXmppUnitTest, mpls_peer_l2route_add) {

    client->Reset();

    XmppConnectionSetUp();
    //wait for connection establishment
    WAIT_FOR(1000, 10000, (sconnection->GetStateMcState() == xmsm::ESTABLISHED));
    WAIT_FOR(1000, 10000, (cchannel->GetPeerState() == xmps::READY));

    //Mock Sandesh request
    AgentXmppConnectionStatusReq  *xmpp_req = new AgentXmppConnectionStatusReq();
    std::vector<string> result;
    result.push_back("127.0.0.1");
    result.push_back("Yes");
    result.push_back("Established");
    Sandesh::set_response_callback(boost::bind(ValidateSandeshResponse, _1, result)); 
    xmpp_req->HandleRequest();
    client->WaitForIdle();
    xmpp_req->Release();

    //expect subscribe for __default__ at the mock server
    WAIT_FOR(1000, 10000, (mock_peer.get()->Count() == 1));

    VxLanNetworkIdentifierMode(false);
    client->WaitForIdle();
    // Create vm-port in vn1 
    struct PortInfo input[] = {
        {"vnet2", 2, "1.1.1.2", "00:00:00:01:01:02", 1, 2},
    };

    //Create vn,vrf,vm,vm-port and route entry in vrf1 
    CreateVmportEnv(input, 1);
    //expect subscribe message+route at the mock server
    WAIT_FOR(1000, 10000, (mock_peer.get()->Count() == 6));
    client->WaitForIdle();

    Ip4Address addr = Ip4Address::from_string("1.1.1.2");
    EXPECT_TRUE(VmPortActive(input, 0));
    EXPECT_TRUE(RouteFind("vrf1", addr, 32));
    Inet4UnicastRouteEntry *rt = RouteGet("vrf1", addr, 32);
    EXPECT_STREQ(rt->dest_vn_name().c_str(), "vn1");

    const struct ether_addr *mac = ether_aton("00:00:00:01:01:02");
    EXPECT_TRUE(L2RouteFind("vrf1", *mac));
    Layer2RouteEntry *l2_rt = L2RouteGet("vrf1", *mac);

    //ensure active path is local-vm
    EXPECT_TRUE(rt->GetActivePath()->peer()->GetType() == Peer::LOCAL_VM_PORT_PEER);
    EXPECT_TRUE(l2_rt->GetActivePath()->peer()->GetType() == Peer::LOCAL_VM_PORT_PEER);
    uint32_t label= l2_rt->GetActivePath()->label();
    EXPECT_TRUE(label != MplsTable::kInvalidLabel);

    // Send route, back to vrf1
    SendL2RouteMessage(mock_peer.get(), "vrf1", "00:00:00:01:01:02",
                     "1.1.1.2/24", l2_rt->GetActivePath()->label(), 
                     "vn1", false);
    // Route reflected to vrf1
    WAIT_FOR(1000, 1000, (l2_rt->GetPathList().size() == 2));
    client->WaitForIdle();
    int path_count = 0;
    for (Route::PathList::const_iterator it = l2_rt->GetPathList().begin();
         it != l2_rt->GetPathList().end(); it++) {
        path_count++;
        const AgentPath *path = static_cast<const AgentPath *>(it.operator->());
        EXPECT_TRUE(path != NULL);
        EXPECT_TRUE(path->label() == label);
    }
    ASSERT_TRUE(path_count == 2);
 
    client->WaitForIdle(5);

    //Delete vm-port and route entry in vrf1
    DeleteVmportEnv(input, 1, true);
    client->WaitForIdle();

    //Confirm Vmport is deleted
    EXPECT_FALSE(VmPortFind(input, 0)); 

    WAIT_FOR(1000, 1000, (agent_->vn_table()->Size() == 0));

    TaskScheduler::GetInstance()->Stop();
    Agent::GetInstance()->controller()->unicast_cleanup_timer().cleanup_timer_->Fire();
    TaskScheduler::GetInstance()->Start();
    client->WaitForIdle();

    EXPECT_FALSE(DBTableFind("vrf1.uc.route.0"));
    EXPECT_FALSE(DBTableFind("vrf1.l2.route.0"));
    EXPECT_FALSE(VrfFind("vrf1"));
    xc->ConfigUpdate(new XmppConfigData());
    client->WaitForIdle(5);
}

}
