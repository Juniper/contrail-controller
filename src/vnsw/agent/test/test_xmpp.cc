/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <base/logging.h>
#include <boost/bind.hpp>
#include <tbb/task.h>
#include <base/task.h>
#include "io/test/event_manager_test.h"

#include <cmn/agent_cmn.h>
#include "base/test/task_test_util.h"

#include "cfg/init_config.h"
#include "oper/operdb_init.h"
#include "controller/controller_init.h"
#include "controller/controller_ifmap.h"
#include "pkt/pkt_init.h"
#include "services/services_init.h"
#include "ksync/ksync_init.h"
#include "oper/interface.h"
#include "oper/nexthop.h"
#include "route/route.h"
#include "oper/vrf.h"
#include "oper/mpls.h"
#include "oper/vm.h"
#include "oper/vn.h"
#include "oper/peer.h"
#include "openstack/instance_service_server.h"
#include "cfg/interface_cfg.h"
#include "cfg/init_config.h"
#include "test_cmn_util.h"
#include "xmpp/xmpp_init.h"
#include "xmpp/test/xmpp_test_util.h"
#include "cfg/init_config.h"
#include "vr_types.h"

#include "xml/xml_pugi.h"

#include "controller/controller_peer.h" 
#include "controller/controller_export.h" 
#include "controller/controller_vrf_export.h" 

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


void RouterIdDepInit() {
}

class AgentBgpXmppPeerTest : public AgentXmppChannel {
public:
    AgentBgpXmppPeerTest(XmppChannel *channel, std::string xs, uint8_t xs_idx) :
        AgentXmppChannel(channel, xs, "0", xs_idx), rx_count_(0),
        rx_channel_event_queue_(
            TaskScheduler::GetInstance()->GetTaskId("xmpp::StateMachine"), 0,
            boost::bind(&AgentBgpXmppPeerTest::ProcessChannelEvent, this, _1)) {
    }

    virtual void ReceiveUpdate(const XmppStanza::XmppMessage *msg) {
        rx_count_++;
        AgentXmppChannel::ReceiveUpdate(msg);
    }

    bool ProcessChannelEvent(xmps::PeerState state) {
        AgentXmppChannel::HandleXmppClientChannelEvent(static_cast<AgentXmppChannel *>(this), state);
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
    AgentXmppUnitTest() : thread_(&evm_)  {}
 
    virtual void SetUp() {
        AgentIfMapVmExport::Init();
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
        TcpServerManager::DeleteServer(xs);
        TcpServerManager::DeleteServer(xc);
        AgentIfMapVmExport::Shutdown();
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
        xitems.append_attribute("node") = vrf.c_str();
        return(xitems);
    }

    void SendRouteMessage(ControlNodeMockBgpXmppPeer *peer, std::string vrf,
                          std::string address, int label, 
                          const char *vn = "vn1") {
        xml_document xdoc;
        xml_node xitems = MessageHeader(&xdoc, vrf);

        autogen::NextHopType item_nexthop;
        item_nexthop.af = Address::INET;
        item_nexthop.address = Agent::GetRouterId().to_string();;
        item_nexthop.label = label;
        
        autogen::ItemType item;
        item.entry.next_hops.next_hop.push_back(item_nexthop);
        item.entry.nlri.af = Address::INET;
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
        item_nexthop.af = 1; //BgpMpNlri::IPv4;
        item_nexthop.address = Agent::GetRouterId().to_string();;
        item_nexthop.label = label;
        
        autogen::ItemType item;
        item.entry.next_hops.next_hop.push_back(item_nexthop);
        item.entry.nlri.af = 1;
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

    void SendRouteDeleteMessage(ControlNodeMockBgpXmppPeer *peer, std::string vrf) {
        xml_document xdoc;
        xml_node xitems = MessageHeader(&xdoc, vrf);
        xml_node node = xitems.append_child("retract");
        node.append_attribute("id") = "1.1.1.1/32";

        SendDocument(xdoc, peer);
    }


    void XmppConnectionSetUp() {

        Agent::SetControlNodeMulticastBuilder(NULL);

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
                       Agent::GetXmppServer(0), 0));
	xc->RegisterConnectionEvent(xmps::BGP,
	    boost::bind(&AgentBgpXmppPeerTest::HandleXmppChannelEvent, 
			bgp_peer.get(), _2));
	Agent::SetAgentXmppChannel(bgp_peer.get(), 0);

        // server connection
        WAIT_FOR(100, 10000,
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
};


namespace {

TEST_F(AgentXmppUnitTest, Connection) {

    client->Reset();
    client->WaitForIdle();

    XmppConnectionSetUp();
    //wait for connection establishment
    WAIT_FOR(100, 10000, (sconnection->GetStateMcState() == xmsm::ESTABLISHED));
    WAIT_FOR(100, 10000, (cchannel->GetPeerState() == xmps::READY));

    // Create vm-port and vn
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
    };

    //expect subscribe for __default__ at the mock server
    WAIT_FOR(100, 10000, (mock_peer.get()->Count() == 1));

    //Create vn,vrf,vm,vm-port and route entry in vrf1 
    CreateVmportEnv(input, 1);
    client->WaitForIdle();
    //expect subscribe message+route at the mock server
    WAIT_FOR(100, 10000, (mock_peer.get()->Count() == 3));

    VrfAddReq("vrf2");
    VnAddReq(2, "vn2", 0, "vrf2");
    //expect subscribe message at the mock server
    WAIT_FOR(100, 10000, (mock_peer.get()->Count() == 4));

    Ip4Address addr = Ip4Address::from_string("1.1.1.1");
    EXPECT_TRUE(VmPortActive(input, 0));
    EXPECT_TRUE(RouteFind("vrf1", addr, 32));
    Inet4UcRoute *rt = RouteGet("vrf1", addr, 32);
    EXPECT_STREQ(rt->GetDestVnName().c_str(), "vn1");

    // Send route, back to vrf1
    SendRouteMessage(mock_peer.get(), "vrf1", "1.1.1.1/32", 
                     MplsTable::kStartLabel);
    // Route reflected to vrf1
    WAIT_FOR(100, 10000, (bgp_peer.get()->Count() == 1));

    // Send route, leak to vrf2
    SendRouteMessage(mock_peer.get(), "vrf2", "1.1.1.1/32", 
                     MplsTable::kStartLabel);
    // Route reflected to vrf2
    WAIT_FOR(100, 10000, (bgp_peer.get()->Count() == 2));

    // Route leaked to vrf2, check entry in route-table
    WAIT_FOR(100, 10000, (RouteFind("vrf2", addr, 32) == true));
    Inet4UcRoute *rt2 = RouteGet("vrf2", addr, 32);
    WAIT_FOR(100, 10000, (rt2->GetActivePath() != NULL));
    WAIT_FOR(100, 10000, rt2->GetDestVnName().size() > 0);
    EXPECT_STREQ(rt2->GetDestVnName().c_str(), "vn1");

    // Verify service chan routes
    // Add service interface-1
    AddVrf("vrf2");
    AddVmPortVrf("ser1", "2.2.2.1", 1);
    AddLink("virtual-machine-interface-routing-instance", "ser1",
            "routing-instance", "vrf2");
    AddLink("virtual-machine-interface-routing-instance", "ser1",
            "virtual-machine-interface", "vnet1");
    client->WaitForIdle();
    // Validate service vlan route
    rt = RouteGet("vrf2", Ip4Address::from_string("2.2.2.1"), 32);
    EXPECT_TRUE(rt != NULL);
    // Send route, back to vrf1
    SendRouteMessage(mock_peer.get(), "vrf1", "2.2.2.0/24", rt->GetMplsLabel(),
                     "TestVn");
    // Route reflected to vrf1
    WAIT_FOR(100, 10000, (bgp_peer.get()->Count() == 3));
    rt = RouteGet("vrf1", Ip4Address::from_string("2.2.2.0"), 24);
    EXPECT_TRUE(rt != NULL);
    EXPECT_STREQ(rt->GetDestVnName().c_str(),"TestVn");

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
    // Route delete for vrf1 
    WAIT_FOR(100, 10000, (bgp_peer.get()->Count() == 3));

    //Send route delete
    SendRouteDeleteMessage(mock_peer.get(), "vrf2");
    // Route delete for vrf2 
    WAIT_FOR(100, 10000, (bgp_peer.get()->Count() == 5));

    //Confirm route has been cleaned up
    WAIT_FOR(100, 10000, (RouteFind("vrf1", addr, 32) == false));
    WAIT_FOR(100, 10000, (RouteFind("vrf2", addr, 32) == false));
    
    DeleteVmportEnv(input, 1, true);
    //Confirm Vmport is deleted
    EXPECT_FALSE(VmPortFind(input, 0));

    DelVrf("vrf2");
    client->WaitForIdle();

    EXPECT_EQ(1U, Agent::GetVnTable()->Size());
    VnDelReq(2);
    client->WaitForIdle();
    EXPECT_EQ(0U, Agent::GetVnTable()->Size());

    EXPECT_FALSE(DBTableFind("vrf1.uc.route.0"));
    EXPECT_FALSE(VrfFind("vrf1"));
    EXPECT_FALSE(DBTableFind("vrf2.uc.route.0"));
    EXPECT_FALSE(VrfFind("vrf2"));

    xc->ConfigUpdate(new XmppConfigData());
    client->WaitForIdle(5);
}

TEST_F(AgentXmppUnitTest, CfgServerSelection) {

    client->Reset();
    client->WaitForIdle();

    XmppConnectionSetUp();
    //wait for connection establishment
    WAIT_FOR(100, 10000, (sconnection->GetStateMcState() == xmsm::ESTABLISHED));
    WAIT_FOR(100, 10000, (cchannel->GetPeerState() == xmps::READY));

    client->Reset();
    client->WaitForIdle(5);

    ASSERT_TRUE(Agent::GetXmppServer(0) == Agent::GetXmppCfgServer());

    //bring-down the channel
    bgp_peer.get()->HandleXmppChannelEvent(xmps::NOT_READY);
    client->WaitForIdle();
    ASSERT_TRUE(Agent::GetXmppCfgServer().empty() == 1);

    xc->ConfigUpdate(new XmppConfigData());
    client->WaitForIdle(5);
}

TEST_F(AgentXmppUnitTest, ConnectionUpDown) {

    client->Reset();
    client->WaitForIdle();

    XmppConnectionSetUp();
    //wait for connection establishment
    WAIT_FOR(100, 10000, (sconnection->GetStateMcState() == xmsm::ESTABLISHED));
    WAIT_FOR(100, 10000, (cchannel->GetPeerState() == xmps::READY));

    //expect subscribe for __default__ at the mock server
    WAIT_FOR(100, 10000, (mock_peer.get()->Count() == 1));

    // Create vm-port in vn1 
    struct PortInfo input[] = {
        {"vnet2", 2, "1.1.1.2", "00:00:00:01:01:02", 1, 2},
    };

    //Create vn,vrf,vm,vm-port and route entry in vrf1 
    CreateVmportEnv(input, 1);
    client->WaitForIdle();
    //expect subscribe message+route at the mock server
    WAIT_FOR(100, 10000, (mock_peer.get()->Count() == 3));

    Ip4Address addr = Ip4Address::from_string("1.1.1.2");
    EXPECT_TRUE(VmPortActive(input, 0));
    EXPECT_TRUE(RouteFind("vrf1", addr, 32));
    Inet4UcRoute *rt = RouteGet("vrf1", addr, 32);
    EXPECT_STREQ(rt->GetDestVnName().c_str(), "vn1");

    //ensure active path is local-vm
    EXPECT_TRUE(rt->GetActivePath()->GetPeer()->GetType() == Peer::LOCAL_VM_PEER);

    // Send route, back to vrf1
    SendRouteMessage(mock_peer.get(), "vrf1", "1.1.1.2/32",
                     MplsTable::kStartLabel);
    // Route reflected to vrf1
    WAIT_FOR(100, 10000, (bgp_peer.get()->Count() == 1));

    //ensure active path is BGP
    EXPECT_TRUE(rt->GetActivePath()->GetPeer()->GetType() == Peer::BGP_PEER);

    AgentXmppChannel *ch = static_cast<AgentXmppChannel *>(bgp_peer.get());
    EXPECT_TRUE(rt->FindPath(ch->GetBgpPeer()) != NULL);

    //bring-down the channel
    bgp_peer.get()->HandleXmppChannelEvent(xmps::NOT_READY);
    client->WaitForIdle();

    //ensure route learnt via control-node is deleted
    WAIT_FOR(100, 10000, (rt->FindPath(ch->GetBgpPeer()) == NULL));

    //bring up the channel
    bgp_peer.get()->HandleXmppChannelEvent(xmps::READY);
    client->WaitForIdle();

    //expect subscribe for __default__, vrf1,route
    //at the mock server
    WAIT_FOR(100, 10000, (mock_peer.get()->Count() == 6));

    //Add vm-port in vn 1
    struct PortInfo input2[] = {
        {"vnet3", 3, "1.1.1.3", "00:00:00:02:01:03", 1, 3},
    };
    CreateVmportEnv(input2, 1);
    client->WaitForIdle();

    Ip4Address addr2 = Ip4Address::from_string("1.1.1.3");
    EXPECT_TRUE(VmPortActive(input2, 0));
    EXPECT_TRUE(RouteFind("vrf1", addr2, 32));
    Inet4UcRoute *rt2 = RouteGet("vrf1", addr2, 32);
    EXPECT_STREQ(rt2->GetDestVnName().c_str(), "vn1");

    //ensure active path is local-vm
    EXPECT_TRUE(rt2->GetActivePath()->GetPeer()->GetType() == Peer::LOCAL_VM_PEER);

    xc->ConfigUpdate(new XmppConfigData());
    client->WaitForIdle(5);

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

    EXPECT_EQ(0U, Agent::GetVnTable()->Size());

    EXPECT_FALSE(DBTableFind("vrf1.uc.route.0"));
    EXPECT_FALSE(VrfFind("vrf1"));
}

TEST_F(AgentXmppUnitTest, DISABLED_SgList) {

    client->Reset();
    client->WaitForIdle();

    XmppConnectionSetUp();
    //wait for connection establishment
    WAIT_FOR(100, 10000, (sconnection->GetStateMcState() == xmsm::ESTABLISHED));
    WAIT_FOR(100, 10000, (cchannel->GetPeerState() == xmps::READY));

    // Create vm-port and vn
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
    };

    //expect subscribe for __default__ at the mock server
    WAIT_FOR(100, 10000, (mock_peer.get()->Count() == 1));

    //Create vn,vrf,vm,vm-port and route entry in vrf1 
    CreateVmportEnv(input, 1);
    client->WaitForIdle();
    //expect subscribe message+route at the mock server
    WAIT_FOR(100, 10000, (mock_peer.get()->Count() == 3));

    VrfAddReq("vrf2");
    VnAddReq(2, "vn2", 0, "vrf2");
    //expect subscribe message at the mock server
    WAIT_FOR(100, 10000, (mock_peer.get()->Count() == 4));

    Ip4Address addr = Ip4Address::from_string("1.1.1.1");
    EXPECT_TRUE(VmPortActive(input, 0));
    EXPECT_TRUE(RouteFind("vrf1", addr, 32));
    Inet4UcRoute *rt = RouteGet("vrf1", addr, 32);
    EXPECT_STREQ(rt->GetDestVnName().c_str(), "vn1");

    // Send route, back to vrf1
    SendRouteMessage(mock_peer.get(), "vrf1", "1.1.1.1/32", 
                     MplsTable::kStartLabel);
    // Route reflected to vrf1
    WAIT_FOR(100, 10000, (bgp_peer.get()->Count() == 1));

    // Send route, leak to vrf2
    SendRouteMessageSg(mock_peer.get(), "vrf2", "1.1.1.1/32", 
                       MplsTable::kStartLabel);
    // Route leaked to vrf2
    WAIT_FOR(100, 10000, (bgp_peer.get()->Count() == 2));

    // Route leaked to vrf2, check entry in route-table
    WAIT_FOR(100, 10000, (RouteFind("vrf2", addr, 32) == true));
    Inet4UcRoute *rt2 = RouteGet("vrf2", addr, 32);
    WAIT_FOR(100, 10000, (rt2->GetActivePath() != NULL));
    WAIT_FOR(100, 10000, rt2->GetDestVnName().size() > 0);
    EXPECT_STREQ(rt2->GetDestVnName().c_str(), "vn1");
    const SecurityGroupList sglist = rt2->GetActivePath()->GetSecurityGroupList();
    EXPECT_TRUE(sglist.size() == 2);
    

    //Delete vm-port and route entry in vrf1
    IntfCfgDel(input, 0);
    client->WaitForIdle();

    //Send route delete
    SendRouteDeleteMessage(mock_peer.get(), "vrf1");
    // Route delete for vrf1 
    WAIT_FOR(100, 10000, (bgp_peer.get()->Count() == 3));

    //Send route delete
    SendRouteDeleteMessage(mock_peer.get(), "vrf2");
    // Route delete for vrf2 
    WAIT_FOR(100, 10000, (bgp_peer.get()->Count() == 5));

    //Confirm route has been cleaned up
    WAIT_FOR(100, 10000, (RouteFind("vrf1", addr, 32) == false));
    WAIT_FOR(100, 10000, (RouteFind("vrf2", addr, 32) == false));
    
    DeleteVmportEnv(input, 1, true);
    //Confirm Vmport is deleted
    EXPECT_FALSE(VmPortFind(input, 0));

    DelVrf("vrf2");
    client->WaitForIdle();

    EXPECT_EQ(1U, Agent::GetVnTable()->Size());
    VnDelReq(2);
    client->WaitForIdle();
    EXPECT_EQ(0U, Agent::GetVnTable()->Size());

    EXPECT_FALSE(DBTableFind("vrf1.uc.route.0"));
    EXPECT_FALSE(VrfFind("vrf1"));
    EXPECT_FALSE(DBTableFind("vrf2.uc.route.0"));
    EXPECT_FALSE(VrfFind("vrf2"));

    xc->ConfigUpdate(new XmppConfigData());
    client->WaitForIdle(5);
}

}
int main(int argc, char **argv) {
    GETUSERARGS();
    client = TestInit(init_file, ksync_init);
    Agent::SetXmppServer("127.0.0.1", 0);

    int ret = RUN_ALL_TESTS();
    Agent::GetEventManager()->Shutdown();
    AsioStop();
    return ret;
}

