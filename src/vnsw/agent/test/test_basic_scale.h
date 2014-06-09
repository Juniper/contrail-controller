/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
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

#define MAX_CHANNEL 10 
#define MAX_CONTROL_PEER 2
#define MAX_INTERFACES 250
#define DEFAULT_WALKER_YIELD 1024

using namespace pugi;
int num_vns = 1;
int num_vms_per_vn = 1;
int num_ctrl_peers = 1;
int walker_wait_usecs = 0;
int walker_yield = 0;
int num_remote = 0;

void SetWalkerYield(int yield) {
    char iterations_env[80];
    sprintf(iterations_env, "DB_ITERATION_TO_YIELD=%d", yield);
    putenv(iterations_env);
}

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

xml_node MulticastMessageHeader(xml_document *xdoc, std::string vrf) {
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
    ss << "1" << "/" << "241" << "/" << vrf.c_str();
    std::string node_str(ss.str());
    xitems.append_attribute("node") = node_str.c_str();
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

void RouterIdDepInit(Agent *agent) {
}

Ip4Address IncrementIpAddress(Ip4Address base_addr) {
    return Ip4Address(base_addr.to_ulong() + 1);
}

void InitXmppServers() {
    Ip4Address addr = Ip4Address::from_string("127.0.0.0");

    for (int i = 0; i < num_ctrl_peers; i++) {
        addr = IncrementIpAddress(addr);
        Agent::GetInstance()->set_controller_ifmap_xmpp_server(addr.to_string(), i);
    }
}

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
        if (GetXmppChannel() && GetXmppChannel()->GetPeerState() != xmps::READY) {
            return;
        }

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
    ControlNodeMockBgpXmppPeer() : channel_ (NULL), rx_count_(0),
    label1_(1000), label2_(5000) {
        peer_skip_route_list_.clear();
    }

    void ReflectIpv4Route(string vrf_name, const pugi::xml_node &node, 
                          bool add_change) {
        autogen::ItemType item;
        item.Clear();

        EXPECT_TRUE(item.XmlParse(node));
        EXPECT_TRUE(item.entry.nlri.af == BgpAf::IPv4);
        uint32_t label = 0;
        if (peer_skip_route_list_.find(item.entry.nlri.address) !=
           peer_skip_route_list_.end()) {
            return;
        }
        if (add_change) {
            EXPECT_TRUE(!item.entry.next_hops.next_hop.empty());
            //Assuming one interface NH has come
            SendRouteMessage(vrf_name, item.entry.nlri.address, 
                             item.entry.next_hops.next_hop[0].label, false);
        } else {
            SendRouteDeleteMessage(vrf_name, item.entry.nlri.address);
        }
    }

    void ReflectMulticast(string vrf_name, const pugi::xml_node &node, bool add_change) {
        autogen::McastItemType item;
        item.Clear();

        EXPECT_TRUE(item.XmlParse(node));
        EXPECT_TRUE(item.entry.nlri.af == BgpAf::IPv4);
        EXPECT_TRUE(item.entry.nlri.safi == BgpAf::Mcast);
        EXPECT_TRUE(!item.entry.nlri.group.empty());

        if (peer_skip_route_list_.find(item.entry.nlri.group) !=
           peer_skip_route_list_.end()) {
            return;
        }
        if (add_change) {
            uint32_t src_label = label1_++;
            uint32_t tunnel_label_1 = label1_++;
            uint32_t tunnel_label_2 = label1_++;
            SendBcastRouteMessage(vrf_name, item.entry.nlri.group, src_label, 
                                  "127.0.0.1", tunnel_label_1, tunnel_label_2);
        } else {
            SendBcastRouteDelete(vrf_name, item.entry.nlri.group, "127.0.0.1");
        }
    }

    void ReflectEnetRoute(string vrf_name, const pugi::xml_node &node, bool add_change) {
        autogen::EnetItemType item;
        item.Clear();

        EXPECT_TRUE(item.XmlParse(node));
        EXPECT_TRUE(item.entry.nlri.af == BgpAf::L2Vpn);
        uint32_t label = 0;
        if (peer_skip_route_list_.find(item.entry.nlri.mac) !=
           peer_skip_route_list_.end()) {
            return;
        }
        if (add_change) {
            EXPECT_TRUE(!item.entry.next_hops.next_hop.empty());
            //Assuming one interface NH has come
            SendL2RouteMessage(vrf_name, item.entry.nlri.mac, 
                               item.entry.nlri.address, 
                               item.entry.next_hops.next_hop[0].label);
        } else {
            //TODO check why retract parsing fails
            //SendL2RouteDeleteMessage(item.entry.nlri.mac, vrf_name, 
            //                         item.entry.nlri.address);
        }
    }

    void SendDocument(const pugi::xml_document &xdoc) {
        ostringstream oss;
        xdoc.save(oss);
        string msg = oss.str();
        uint8_t buf[4096];
        bzero(buf, sizeof(buf));
        memcpy(buf, msg.data(), msg.size());
        SendUpdate(buf, msg.size());
    }

    void SendRouteMessage(std::string vrf, std::string address, int label, bool remote,
                          const char *vn = "vn1") {
        xml_document xdoc;
        xml_node xitems = MessageHeader(&xdoc, vrf);

        autogen::NextHopType item_nexthop;
        item_nexthop.af = BgpAf::IPv4;
        if (remote) {
            item_nexthop.address = "127.127.127.127";
        } else {
            item_nexthop.address = Agent::GetInstance()->router_id().to_string();
        }
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

        SendDocument(xdoc);
    }

    void SendL2RouteMessage(std::string vrf, std::string mac_string, std::string address, int label, 
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

        SendDocument(xdoc);
    }

    void SendRouteDeleteMessage(std::string vrf, std::string address) {
        xml_document xdoc;
        xml_node xitems = MessageHeader(&xdoc, vrf);
        xml_node node = xitems.append_child("retract");
        stringstream ss;
        ss << address.c_str() << "/32"; 
        string node_str(ss.str());
        node.append_attribute("id") = node_str.c_str();

        SendDocument(xdoc);
    }

    void SendL2RouteDeleteMessage(std::string mac_string, std::string vrf,
                                  std::string address) {
        xml_document xdoc;
        xml_node xitems = L2MessageHeader(&xdoc, vrf);
        xml_node node = xitems.append_child("retract");
        stringstream ss;
        ss << mac_string.c_str() << "," << address.c_str(); 
        string node_str(ss.str());
        node.append_attribute("id") = node_str.c_str();

        SendDocument(xdoc);
    }

    void SendBcastRouteMessage(std::string vrf, std::string subnet_addr, 
                               int src_label, std::string agent_addr, 
                               int dest_label1, int dest_label2) {
        xml_document xdoc;
        xml_node xitems = MulticastMessageHeader(&xdoc, vrf);

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
        ss << agent_addr.c_str() << ":" << "65535:" << subnet_addr.c_str() << ",0.0.0.0"; 
        string node_str(ss.str());
        node.append_attribute("id") = node_str.c_str();
        item.Encode(&node);

        SendDocument(xdoc);
    }
    void SendBcastRouteDelete(std::string vrf, std::string addr, 
                              std::string agent_addr) {
        xml_document xdoc;
        xml_node xitems = MulticastMessageHeader(&xdoc, vrf);
        xml_node node = xitems.append_child("retract");
        //Route-Distinguisher
        stringstream ss;
        ss << agent_addr.c_str() << ":" << "65535:" << addr.c_str() 
                                        << "," << "0.0.0.0"; 
        string node_str(ss.str());
        node.append_attribute("id") = node_str.c_str();

        SendDocument(xdoc);
    }

    void ReceiveUpdate(const XmppStanza::XmppMessage *msg) {
        rx_count_++;

        if (!channel_ ||
            (channel_->GetPeerState() != xmps::READY)) {
            return;
        }

        if (msg && msg->type == XmppStanza::IQ_STANZA) {
            const XmppStanza::XmppMessageIq *iq =
                static_cast<const XmppStanza::XmppMessageIq *>(msg);
            XmlPugi *pugi = NULL;
            std::string vrf_name = iq->node;
            if (iq->iq_type.compare("set") == 0) {
                if (iq->action.compare("subscribe") == 0) {
                    pugi = reinterpret_cast<XmlPugi *>(iq->dom.get());
                    xml_node options = pugi->FindNode("options");
                    for (xml_node node = options.first_child(); node;
                         node = node.next_sibling()) {
                        if (strcmp(node.name(), "instance-id") == 0) {
                            //TODO Some verification
                        }
                    }
                } else if (iq->action.compare("unsubscribe") == 0) {
                    //TODO unsubscribe came
                } else if (iq->action.compare("publish") == 0) {
                    XmlBase *impl = msg->dom.get();
                    XmlPugi *pugi = reinterpret_cast<XmlPugi *>(impl);
                    for (xml_node item = pugi->FindNode("item"); item;
                         item = item.next_sibling()) {
                        if (strcmp(item.name(), "item") != 0) continue;
                        std::string id(iq->as_node.c_str());
                        char *str = const_cast<char *>(id.c_str());
                        char *saveptr;
                        char *af = strtok_r(str, "/", &saveptr);
                        char *safi = strtok_r(NULL, "/", &saveptr);

                        if (atoi(af) == BgpAf::IPv4 && atoi(safi) == BgpAf::Unicast) {
                            ReflectIpv4Route(iq->node, item, iq->is_as_node);
                        } else if (atoi(af) == BgpAf::IPv4 && atoi(safi) == BgpAf::Mcast) {
                            ReflectMulticast(iq->node, item, iq->is_as_node);
                        } else if (atoi(af) == BgpAf::L2Vpn && atoi(safi) == BgpAf::Enet) {
                            ReflectEnetRoute(iq->node, item, iq->is_as_node);
                        }
                    }
                }
            }
        }
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

    //TODO need to be moved to use gateway
    void AddRemoteV4Routes(int num_routes, std::string vrf_name, std::string vn_name, 
                           std::string ip_prefix) {
        uint32_t label = 6000;
        Ip4Address addr = Ip4Address::from_string(ip_prefix);
        for (int i = 0; i < num_routes; i++) {
            addr = IncrementIpAddress(addr);
            stringstream addr_str;
            addr_str << addr.to_string() << "/32";
            SendRouteMessage(vrf_name, addr_str.str(), label++, true, vn_name.c_str());
        }
    }

    void DeleteRemoteV4Routes(int num_routes, std::string vrf_name, std::string ip_prefix) {
        Ip4Address addr = Ip4Address::from_string(ip_prefix);
        for (int i = 0; i < num_routes; i++) {
            addr = IncrementIpAddress(addr);
            SendRouteDeleteMessage(vrf_name, addr.to_string()); 
        }
    }

    void WriteReadyCb(const boost::system::error_code &ec) {
    }

    void SkipRoute(std::string addr) {
        peer_skip_route_list_.insert(addr);
    }

    size_t Count() const { return rx_count_; }
    virtual ~ControlNodeMockBgpXmppPeer() {
    }
private:
    XmppChannel *channel_;
    size_t rx_count_;
    uint32_t label1_;
    uint32_t label2_;
    std::set<string> peer_skip_route_list_;
};


class AgentBasicScaleTest : public ::testing::Test {
protected:
    AgentBasicScaleTest() : thread_(&evm_), agent_(Agent::GetInstance())  {
    }
 
    virtual void SetUp() {
        for (int i = 0; i < num_ctrl_peers; i++) {
            xs[i] = new XmppServer(&evm_, XmppInit::kControlNodeJID);
            xc[i] = new XmppClient(&evm_);

            xs[i]->Initialize(0, false);
        }
        
        thread_.Start();
    }

    virtual void TearDown() {
        TaskScheduler::GetInstance()->Stop();
        Agent::GetInstance()->controller()->
            unicast_cleanup_timer().cleanup_timer_->Fire();
        TaskScheduler::GetInstance()->Start();
        client->WaitForIdle();

        for (int i = 0; i < num_ctrl_peers; i++) {
            Agent::GetInstance()->set_controller_xmpp_channel(NULL, i);
            xc[i]->ConfigUpdate(new XmppConfigData());
            client->WaitForIdle(5);
            xs[i]->Shutdown();
            bgp_peer[i].reset(); 
            client->WaitForIdle();
            xc[i]->Shutdown();
            client->WaitForIdle();
        }

        TaskScheduler::GetInstance()->Stop();
        Agent::GetInstance()->controller()->unicast_cleanup_timer().cleanup_timer_->Fire();
        Agent::GetInstance()->controller()->multicast_cleanup_timer().cleanup_timer_->Fire();
        TaskScheduler::GetInstance()->Start();
        client->WaitForIdle();
        Agent::GetInstance()->controller()->Cleanup();
        client->WaitForIdle();
        WAIT_FOR(1000, 1000, (Agent::GetInstance()->controller()->DecommissionedPeerListSize() 
                              == 0));

        for (int i = 0; i < num_ctrl_peers; i++) {
            TcpServerManager::DeleteServer(xs[i]);
            TcpServerManager::DeleteServer(xc[i]);
        }
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

    void VerifyConnections(int i, int mock_peer_count = 1) {
        XmppServer *xs_l = xs[i];
        XmppClient *xc_l = xc[i];

        XmppConnection *sconnection_l = sconnection[i];

        WAIT_FOR(100, 10000,
                 ((sconnection_l = xs_l->FindConnection(XmppInit::kAgentNodeJID)) 
                  != NULL));
        assert(sconnection_l);
        //wait for connection establishment
        WAIT_FOR(100, 10000, (sconnection_l->GetStateMcState() == xmsm::ESTABLISHED));

        XmppChannel *cchannel_l = cchannel[i];;
        WAIT_FOR(100, 10000, (cchannel_l->GetPeerState() == xmps::READY));
        if (mock_peer_count == 1)
            client->WaitForIdle();
        //expect subscribe for __default__ at the mock server

        ControlNodeMockBgpXmppPeer *mock_peer_l = mock_peer[i].get();
        WAIT_FOR(1000, 10000, (mock_peer_l->Count() == mock_peer_count));
    }

    void BuildControlPeers() {
        Ip4Address addr = Ip4Address::from_string("127.0.0.0");
        for (int i = 0; i < num_ctrl_peers; i++) {
            addr = IncrementIpAddress(addr);
            //Create control node mock
            mock_peer[i].reset(new ControlNodeMockBgpXmppPeer());
            xs[i]->RegisterConnectionEvent(xmps::BGP,
                   boost::bind(&ControlNodeMockBgpXmppPeer::HandleXmppChannelEvent,
                               mock_peer[i].get(), _1, _2));                        
            LOG(DEBUG, "Create xmpp agent clien - t" << i);
            //New config data for this channel and peer
            xmppc_cfg[i] = new XmppConfigData;
            xmppc_cfg[i]->AddXmppChannelConfig(CreateXmppChannelCfg(addr.to_string().c_str(),
                                               xs[i]->GetPort(), addr.to_string().c_str(),
                                               XmppInit::kAgentNodeJID, 
                                               XmppInit::kControlNodeJID, true));
            xc[i]->ConfigUpdate(xmppc_cfg[i]);
            cchannel[i] = xc[i]->FindChannel(XmppInit::kControlNodeJID);
            //New bgp peer from agent
            bgp_peer[i].reset(new AgentBgpXmppPeerTest(cchannel[i],
                                  Agent::GetInstance()->controller_ifmap_xmpp_server(i),
                                  Agent::GetInstance()->multicast_label_range(i), i));
            xc[i]->RegisterConnectionEvent(xmps::BGP,
                           boost::bind(&AgentBgpXmppPeerTest::HandleXmppChannelEvent, 
                                       bgp_peer[i].get(), _2));
            Agent::GetInstance()->set_controller_xmpp_channel(bgp_peer[i].get(), i);

            // server connection
            VerifyConnections(i);
        }
    }

    void BuildVmPortEnvironment() {
        //TODO take vxlan state from command line, keep default as false
        VxLanNetworkIdentifierMode(false);
        client->WaitForIdle();

        uint32_t num_entries = (num_vns * num_vms_per_vn);
        //Build the VM port list
        input = (struct PortInfo *)malloc(sizeof(struct PortInfo) * num_entries);
        int intf_id = 1;
        for (int vn_cnt = 1; vn_cnt <= num_vns; vn_cnt++) {
            stringstream ip_addr;
            ip_addr << vn_cnt << ".1.1.0";
            Ip4Address addr = 
                IncrementIpAddress(Ip4Address::from_string(ip_addr.str()));
            for (int vm_cnt = 0; vm_cnt < num_vms_per_vn; vm_cnt++) {
                stringstream name;
                stringstream mac;
                int cnt = intf_id - 1;
                name << "vnet" << intf_id;
                mac << "00:00:00:00:" << std::hex << vn_cnt << ":" << 
                    std::hex << (vm_cnt + 1);
                memcpy(&(input[cnt].name), name.str().c_str(), 32);
                input[cnt].intf_id = intf_id;
                memcpy(&(input[cnt].addr), addr.to_string().c_str(), 32);
                memcpy(&(input[cnt].mac), mac.str().c_str(), 32);
                input[cnt].vn_id = vn_cnt;
                input[cnt].vm_id = intf_id;
                intf_id++;
                addr = IncrementIpAddress(addr);
            }
        }
        //Create vn,vrf,vm,vm-port and route entry in vrf1 
        CreateVmportEnv(input, (intf_id - 1));
        WAIT_FOR(10000, 10000, (agent_->interface_table()->Size() == 
                                ((num_vns * num_vms_per_vn) + 3)));
        VerifyVmPortActive(true);
        VerifyRoutes(false);
    }

    void VerifyVmPortActive(bool active) {
        int intf_id;
        for (int i = 0; i < (num_vns * num_vms_per_vn); i++) {
            intf_id = input[i].intf_id;
            if (active) {
                WAIT_FOR(1000, 1000, VmPortActive(intf_id));
            } else {
                WAIT_FOR(1000, 1000, !VmPortActive(intf_id));
            }
        }
    }

    void DeleteVmPortEnvironment() {
        char vrf_name[80];
        PortInfo *del_input = NULL;
        int input_id = 0;
        int intf_count = agent_->interface_table()->Size();
        for (int vn_cnt = 1; vn_cnt <= num_vns; vn_cnt++) {
            for (int j = 0; j < num_vms_per_vn; j++) {
                del_input = &input[input_id];
                DeleteVmportEnv(del_input, 1, 1);
                input_id++;
            }
            sprintf(vrf_name, "vrf%d", vn_cnt);
            WAIT_FOR(10000, 10000, !VnFind(vn_cnt));
            WAIT_FOR(10000, 10000, !VrfFind(vrf_name));
        }
        WAIT_FOR(10000, 10000, (agent_->interface_table()->Size() == 3));
        VerifyRoutes(true);
        VerifyVmPortActive(false);
        TaskScheduler::GetInstance()->Stop();
        agent_->controller()->unicast_cleanup_timer().cleanup_timer_->Fire();
        agent_->controller()->multicast_cleanup_timer().cleanup_timer_->Fire();
        TaskScheduler::GetInstance()->Start();
        client->WaitForIdle();
        WAIT_FOR(10000, 10000, (Agent::GetInstance()->vrf_table()->Size() == 1));
        WAIT_FOR(1000, 1000, (Agent::GetInstance()->vn_table()->Size() == 0));
    }

    void VerifyRoutes(bool deleted) {
        int intf_id = 1;
        struct ether_addr *flood_mac = 
            (struct ether_addr *)malloc(sizeof(struct ether_addr));
        stringstream flood_mac_str;
        flood_mac_str << "ff:ff:ff:ff:ff:ff";
        memcpy (flood_mac, ether_aton(flood_mac_str.str().c_str()), 
                sizeof(struct ether_addr));
        struct ether_addr *local_vm_mac = 
            (struct ether_addr *)malloc(sizeof(struct ether_addr));
        for (int vn_cnt = 1; vn_cnt <= num_vns; vn_cnt++) {
            stringstream ip_addr;
            ip_addr << vn_cnt << ".1.1.0";
            Ip4Address addr = 
                IncrementIpAddress(Ip4Address::from_string(ip_addr.str()));
            stringstream name;
            name << "vrf" << intf_id;
            for (int vm_cnt = 0; vm_cnt < num_vms_per_vn; vm_cnt++) {
                stringstream mac;
                mac << "00:00:00:00:" << std::hex << vn_cnt << ":" << 
                    std::hex << (vm_cnt + 1);
                memcpy (local_vm_mac, ether_aton(mac.str().c_str()), 
                        sizeof(struct ether_addr));
                if (deleted) {
                    WAIT_FOR(1000, 10000, !(RouteFind(name.str(), addr.to_string(), 32)));
                    WAIT_FOR(1000, 10000, !(L2RouteFind(name.str(), *local_vm_mac)));
                } else {
                    WAIT_FOR(1000, 10000, (RouteFind(name.str(), addr.to_string(), 32)));
                    WAIT_FOR(1000, 10000, (L2RouteFind(name.str(), *local_vm_mac)));
                }
                addr = IncrementIpAddress(addr);
            }
            if (deleted) {
                WAIT_FOR(1000, 10000, !(MCRouteFind(name.str(), 
                                                   Ip4Address::from_string("255.255.255.255"))));
                WAIT_FOR(1000, 10000, !(L2RouteFind(name.str(), *flood_mac)));
            } else {
                WAIT_FOR(1000, 10000, (MCRouteFind(name.str(), 
                                                  Ip4Address::from_string("255.255.255.255"))));
                WAIT_FOR(1000, 10000, (L2RouteFind(name.str(), *flood_mac)));
            }
            intf_id++;
        }
        delete flood_mac;
        delete local_vm_mac;
    }

    void XmppConnectionSetUp() {
        Agent::GetInstance()->controller()->increment_multicast_sequence_number();
        Agent::GetInstance()->set_cn_mcast_builder(NULL);
        BuildControlPeers();
    }

    EventManager evm_;
    ServerThread thread_;

    XmppConfigData *xmpps_cfg[MAX_CHANNEL]; //Not in use currently
    XmppConfigData *xmppc_cfg[MAX_CHANNEL];

    XmppServer *xs[MAX_CHANNEL];
    XmppClient *xc[MAX_CHANNEL];

    XmppConnection *sconnection[MAX_CHANNEL];
    XmppChannel *cchannel[MAX_CHANNEL];

    auto_ptr<AgentBgpXmppPeerTest> bgp_peer[MAX_CHANNEL];
    auto_ptr<ControlNodeMockBgpXmppPeer> mock_peer[MAX_CHANNEL];
    Agent *agent_;
    struct PortInfo *input;
};

#define GETSCALEARGS()                          \
    bool ksync_init = false;                    \
    char init_file[1024];                       \
    memset(init_file, '\0', sizeof(init_file)); \
    ::testing::InitGoogleTest(&argc, argv);     \
    namespace opt = boost::program_options;     \
    opt::options_description desc("Options");   \
    opt::variables_map vm;                      \
    desc.add_options()                          \
        ("help", "Print help message")          \
        ("config", opt::value<string>(), "Specify Init config file")  \
        ("kernel", "Run with vrouter")         \
        ("vn", opt::value<int>(), "Number of VN")                   \
        ("vm", opt::value<int>(), "Number of VM per VN")            \
        ("control", opt::value<int>(), "Number of control peer")     \
        ("remote", opt::value<int>(), "Number of remote routes")     \
        ("wait_usecs", opt::value<int>(), "Walker Wait in msecs") \
        ("yield", opt::value<int>(), "Walker yield (default 1)");\
    opt::store(opt::parse_command_line(argc, argv, desc), vm); \
    opt::notify(vm);                            \
    if (vm.count("help")) {                     \
        cout << "Test Help" << endl << desc << endl; \
        exit(0);                                \
    }                                           \
    if (vm.count("kernel")) {                   \
        ksync_init = true;                      \
    }                                           \
    if (vm.count("vn")) {                      \
        num_vns = vm["vn"].as<int>();          \
    }                                           \
    if (vm.count("vm")) {                      \
        num_vms_per_vn = vm["vm"].as<int>();   \
    }                                           \
    if (vm.count("control")) {                      \
        num_ctrl_peers = vm["control"].as<int>();   \
    }                                           \
    if (vm.count("remote")) {                   \
        num_remote = vm["remote"].as<int>();    \
    }                                           \
    if (vm.count("wait_usecs")) {                      \
        walker_wait_usecs = vm["wait_usecs"].as<int>();   \
    }                                           \
    if (vm.count("yield")) {                      \
        walker_yield = vm["yield"].as<int>();   \
    }                                           \
    if (vm.count("config")) {                   \
        strncpy(init_file, vm["config"].as<string>().c_str(), (sizeof(init_file) - 1) ); \
    } else {                                    \
        strcpy(init_file, DEFAULT_VNSW_CONFIG_FILE); \
    }                                           
