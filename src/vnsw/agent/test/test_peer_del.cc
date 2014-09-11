/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <cfg/cfg_init.h>
#include <cfg/cfg_interface.h>
#include <oper/operdb_init.h>
#include <controller/controller_init.h>
#include <pkt/pkt_init.h>
#include <services/services_init.h>
#include <ksync/ksync_init.h>
#include <cmn/agent_cmn.h>
#include <base/task.h>
#include <io/event_manager.h>
#include <base/util.h>
#include <ifmap/ifmap_agent_parser.h>
#include <ifmap/ifmap_agent_table.h>
#include <oper/vn.h>
#include <oper/vm.h>
#include <oper/interface_common.h>
#include <oper/peer.h>

#include "testing/gunit.h"
#include "test_cmn_util.h"

VrfEntry *vrf1, *vrf2;

void RouterIdDepInit(Agent *agent) {
}

class AgentPeerDelete : public ::testing::Test {
public:
    void VrfCreated(DBTablePartBase *partition, DBEntryBase *e, BgpPeer *peer) {
        VrfEntry *vrf = static_cast<VrfEntry *>(e);
        // state is created for each peer
        DBState *State = vrf->GetState(partition->parent(), peer->GetVrfExportListenerId()); 
        VrfExport::State *state = static_cast<VrfExport::State *>(State); 
        if (vrf->IsDeleted()) {
            if (state == NULL) {
                return;
            }
            vrf->ClearState(partition->parent(), peer->GetVrfExportListenerId());
            delete state;
            return;
        }

        if (state == NULL) {
            state = new VrfExport::State();
            vrf->SetState(partition->parent(), peer->GetVrfExportListenerId(), state); 
        }
    }

    void PeerDelDone() {
    }

    void DeletedVrfPeerDelDone() {
    }

    void AddRt(Peer *peer, std::string &vrf_name, IpAddress &sip,
               IpAddress &dip, int label, Inet4UnicastAgentRouteTable *table) {
        Ip4Address s = sip.to_v4();
        Ip4Address d = dip.to_v4();
        Inet4TunnelRouteAdd(peer, vrf_name, s, 32, d, 
                            TunnelType::AllType(), label, "",
                            SecurityGroupList(), PathPreference());
    }
};

TEST_F(AgentPeerDelete, peer_test_1) {
    IpAddress ip1, ip2, ip3, ip4, ip5, ip6;
    ip1 = IpAddress::from_string("192.168.27.1");
    ip2 = IpAddress::from_string("10.11.12.1");
    ip3 = IpAddress::from_string("135.25.1.1");
    ip4 = IpAddress::from_string("45.25.2.1");
    ip5 = IpAddress::from_string("78.25.2.1");
    ip6 = IpAddress::from_string("67.25.2.1");

    BgpPeer *peer1, *peer2; 
    AgentXmppChannel *channel1, *channel2;
    XmppChannelMock xmpp_channel;
    Agent::GetInstance()->set_controller_ifmap_xmpp_server("0.0.0.1", 0);
    Agent::GetInstance()->set_controller_ifmap_xmpp_server("0.0.0.2", 1);
    channel1 = new AgentXmppChannel(Agent::GetInstance(),
                                   "XMPP Server 1", "", 0);
    channel1->RegisterXmppChannel(&xmpp_channel);
    channel2 = new AgentXmppChannel(Agent::GetInstance(),
                                   "XMPP Server 2", "", 1);
    channel2->RegisterXmppChannel(&xmpp_channel);
    AgentXmppChannel::HandleAgentXmppClientChannelEvent(channel1,
                                                        xmps::READY);
    AgentXmppChannel::HandleAgentXmppClientChannelEvent(channel2,
                                                        xmps::READY);
    client->WaitForIdle();
    peer1 = channel1->bgp_peer_id();
    peer2 = channel2->bgp_peer_id();

    std::string vrf1_name("test_vrf1"), vrf2_name("test_vrf2");
    AddVrf("test_vrf1");
    AddVrf("test_vrf2");
    client->WaitForIdle();

    VrfEntryRef vrf1 = VrfGet("test_vrf1");
    VrfEntryRef vrf2 = VrfGet("test_vrf2");

    Inet4UnicastAgentRouteTable *rt_table1, *rt_table2;
    rt_table1 = static_cast<Inet4UnicastAgentRouteTable *>
        (vrf1->GetInet4UnicastRouteTable());
    rt_table2 = static_cast<Inet4UnicastAgentRouteTable *>
        (vrf2->GetInet4UnicastRouteTable());
    std::size_t old_rt1_entries, old_rt2_entries;
    old_rt1_entries = rt_table1->Size();
    old_rt2_entries = rt_table2->Size();

    AddRt(peer1, vrf1_name, ip1, ip2, 99, rt_table1);
    AddRt(peer1, vrf1_name, ip3, ip4, 75, rt_table1);
    AddRt(peer1, vrf1_name, ip5, ip6, 50, rt_table1);
    AddRt(peer2, vrf1_name, ip6, ip2, 25, rt_table1);
    AddRt(peer1, vrf2_name, ip2, ip4, 20, rt_table2);
    AddRt(peer2, vrf2_name, ip2, ip1, 30, rt_table2);

    client->WaitForIdle();
    EXPECT_EQ(rt_table1->Size(), old_rt1_entries + 4);
    EXPECT_EQ(rt_table2->Size(), old_rt2_entries + 1);

    peer1->DelPeerRoutes(boost::bind(&AgentPeerDelete::PeerDelDone, this));
    peer2->DelPeerRoutes(boost::bind(&AgentPeerDelete::PeerDelDone, this));
    client->WaitForIdle();

    EXPECT_EQ(rt_table1->Size(), old_rt1_entries);
    EXPECT_EQ(rt_table2->Size(), old_rt2_entries);

    DelVrf("test_vrf1");
    client->WaitForIdle();
    DelVrf("test_vrf2");
    client->WaitForIdle();

    AgentXmppChannel::HandleAgentXmppClientChannelEvent(channel1,
                                                        xmps::NOT_READY);
    AgentXmppChannel::HandleAgentXmppClientChannelEvent(channel2,
                                                        xmps::NOT_READY);
    TaskScheduler::GetInstance()->Stop();
    Agent::GetInstance()->controller()->unicast_cleanup_timer().cleanup_timer_->
        Fire();
    Agent::GetInstance()->controller()->multicast_cleanup_timer().cleanup_timer_->
        Fire();
    Agent::GetInstance()->controller()->config_cleanup_timer().cleanup_timer_->
        Fire();
    TaskScheduler::GetInstance()->Start();
    client->WaitForIdle();
    Agent::GetInstance()->controller()->Cleanup();
    client->WaitForIdle();


    delete channel1;
    delete channel2;

    peer1 = NULL;
    peer2 = NULL;
    channel1 = NULL;
    channel2 = NULL;

    client->WaitForIdle();
}

TEST_F(AgentPeerDelete, DeletePeerOnDeletedVrf) {
    XmppChannelMock xmpp_channel;
    BgpPeer *peer1, *peer2; 
    AgentXmppChannel *channel1;
    AgentXmppChannel *channel2;
    Agent::GetInstance()->set_controller_ifmap_xmpp_server("0.0.0.1", 0);
    Agent::GetInstance()->set_controller_ifmap_xmpp_server("0.0.0.2", 1);
    channel1 = new AgentXmppChannel(Agent::GetInstance(),
                                   "XMPP Server 1", "", 0);
    channel1->RegisterXmppChannel(&xmpp_channel);
    channel2 = new AgentXmppChannel(Agent::GetInstance(),
                                   "XMPP Server 2", "", 1);
    channel2->RegisterXmppChannel(&xmpp_channel);
    AgentXmppChannel::HandleAgentXmppClientChannelEvent(channel1,
                                                        xmps::READY);
    AgentXmppChannel::HandleAgentXmppClientChannelEvent(channel2,
                                                        xmps::READY);
    client->WaitForIdle();
    peer1 = channel1->bgp_peer_id();
    peer2 = channel2->bgp_peer_id();

    IpAddress ip1, ip2, fabric_ip1, fabric_ip2;
    std::string vrf_name("test_vrf3");

    ip1 = IpAddress::from_string("1.1.1.1");
    fabric_ip1 = Ip4Address::from_string("192.1.1.1");
    ip2 = IpAddress::from_string("2.2.2.2");
    fabric_ip2 = Ip4Address::from_string("192.1.1.3");

    AddVrf("test_vrf3");
    client->WaitForIdle();

    VrfEntryRef vrf = VrfGet("test_vrf3");
    Inet4UnicastAgentRouteTable *rt_table =
        static_cast<Inet4UnicastAgentRouteTable *>
        (vrf->GetInet4UnicastRouteTable());

    AddRt(peer1, vrf_name, ip1, fabric_ip1, 99, rt_table);
    AddRt(peer1, vrf_name, ip2, fabric_ip2, 75, rt_table);
    AddRt(peer2, vrf_name, ip1, fabric_ip1, 99, rt_table);
    AddRt(peer2, vrf_name, ip2, fabric_ip2, 75, rt_table);

    client->WaitForIdle();
    DelVrf("test_vrf3");
    client->WaitForIdle();

    peer1->DelPeerRoutes(boost::bind(&AgentPeerDelete::DeletedVrfPeerDelDone, 
                         this));
    peer2->DelPeerRoutes(boost::bind(&AgentPeerDelete::DeletedVrfPeerDelDone, 
                         this));
    client->WaitForIdle();

    AgentXmppChannel::HandleAgentXmppClientChannelEvent(channel1,
                                                        xmps::NOT_READY);
    AgentXmppChannel::HandleAgentXmppClientChannelEvent(channel2,
                                                        xmps::NOT_READY);
    client->WaitForIdle();
    TaskScheduler::GetInstance()->Stop();
    Agent::GetInstance()->controller()->unicast_cleanup_timer().cleanup_timer_->
        Fire();
    Agent::GetInstance()->controller()->multicast_cleanup_timer().cleanup_timer_->
        Fire();
    Agent::GetInstance()->controller()->config_cleanup_timer().cleanup_timer_->
        Fire();
    TaskScheduler::GetInstance()->Start();
    client->WaitForIdle();
    Agent::GetInstance()->controller()->Cleanup();
    client->WaitForIdle();

    delete channel1;
    delete channel2;
    client->WaitForIdle();

    peer1 = NULL;
    peer2 = NULL;
    channel1 = NULL;
    channel2 = NULL;
}

int main(int argc, char *argv[]) {
    GETUSERARGS();
    client = TestInit(init_file, ksync_init);

    int ret = RUN_ALL_TESTS();
    TestShutdown();
    delete client;
    return ret;
}
