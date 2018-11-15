//
//  test_llgr.cc
//  vnsw/agent/test
//
#include "base/os.h"
#include <base/logging.h>
#include <boost/shared_ptr.hpp>
#include <io/event_manager.h>
#include <tbb/task.h>
#include <base/task.h>

#include <cmn/agent_cmn.h>

#include "cfg/cfg_init.h"
#include "oper/operdb_init.h"
#include "controller/controller_init.h"
#include "pkt/pkt_init.h"
#include "services/services_init.h"
#include "vrouter/ksync/ksync_init.h"
#include "oper/interface_common.h"
#include "oper/nexthop.h"
#include "oper/tunnel_nh.h"
#include "route/route.h"
#include "oper/vrf.h"
#include "oper/mpls.h"
#include "oper/vm.h"
#include "oper/vn.h"
#include "filter/acl.h"
#include "test_cmn_util.h"
#include "kstate/test/test_kstate_util.h"
#include "vr_types.h"

#include <controller/controller_export.h>
#include <controller/controller_peer.h>
#include <controller/controller_ifmap.h>
#include <controller/controller_vrf_export.h>
#include <boost/assign/list_of.hpp>
using namespace boost::assign;
std::string eth_itf;
bool dhcp_external;

struct PortInfo input[] = {
    {"vnet1", 1, "1.1.1.10", "00:00:01:01:01:10", 1, 1},
};

IpamInfo ipam_info_dhcp_enable[] = {
    {"1.1.1.0", 24, "1.1.1.200", true},
};

IpamInfo ipam_info_dhcp_disable[] = {
    {"1.1.1.0", 24, "1.1.1.200", false},
};

void RouterIdDepInit(Agent *agent) {
}

static void ValidateSandeshResponse(Sandesh *sandesh, vector<int> &result) {
    //TBD
    //Validate the response by the expectation
}

class LlgrTest : public ::testing::Test {
public:

protected:
    LlgrTest() {
    }

    ~LlgrTest() {
    }

    virtual void SetUp() {
        vrf_name_ = "vrf1";
        eth_name_ = eth_itf;
        default_dest_ip_ = Ip4Address::from_string("0.0.0.0");
        agent_ = Agent::GetInstance();
        bgp_peer_ = NULL;
        end_of_rib_rx_seen_ = false;

        if (agent_->router_id_configured()) {
            vhost_ip_ = agent_->router_id();
        } else {
            vhost_ip_ = Ip4Address::from_string("10.1.1.10");
        }
        server1_ip_ = Ip4Address::from_string("10.1.1.11");

        strcpy(local_vm_mac_str_, "00:00:01:01:01:10");
        local_vm_mac_ = MacAddress::FromString(local_vm_mac_str_);
        strcpy(local_vm_ip4_str_, "1.1.1.10");
        local_vm_ip4_ = Ip4Address::from_string(local_vm_ip4_str_);
        strcpy(local_vm_ip6_str_, "fdff::10");
        local_vm_ip6_ = Ip6Address::from_string(local_vm_ip6_str_);

        strcpy(remote_vm_mac_str_, "00:00:01:01:01:11");
        remote_vm_mac_ = MacAddress::FromString(remote_vm_mac_str_);
        strcpy(remote_vm_ip4_str_, "1.1.1.11");
        remote_vm_ip4_ = Ip4Address::from_string(remote_vm_ip4_str_);
        strcpy(remote_vm_ip6_str_, "fdff::11");
        remote_vm_ip6_ = Ip6Address::from_string(remote_vm_ip6_str_);

        client->Reset();
        //Create a VRF
        VrfAddReq(vrf_name_.c_str());
        PhysicalInterface::CreateReq(agent_->interface_table(),
                                eth_name_,
                                agent_->fabric_vrf_name(),
                                PhysicalInterface::FABRIC,
                                PhysicalInterface::ETHERNET, false,
                                boost::uuids::nil_uuid(), Ip4Address(0),
                                Interface::TRANSPORT_ETHERNET);
        AddResolveRoute(server1_ip_, 24);
        client->WaitForIdle();
        bgp_peer_ = CreateBgpPeer("127.0.0.1", "remote");
        client->WaitForIdle();
        AgentXmppChannel *channel = bgp_peer_->GetAgentXmppChannel();
        AgentIfMapXmppChannel *ifmap_channel =
            new AgentIfMapXmppChannel(agent_, channel->GetXmppChannel(),
                                      channel->GetXmppServerIdx());
        agent_->set_ifmap_xmpp_channel(ifmap_channel,
                                       channel->GetXmppServerIdx());
        agent_->set_ifmap_active_xmpp_server("127.0.0.1",
                                             channel->GetXmppServerIdx());
    }

    virtual void TearDown() {
        VrfDelReq(vrf_name_.c_str());
        client->WaitForIdle();
        WAIT_FOR(100, 100, (VrfFind(vrf_name_.c_str()) != true));
        DeleteBgpPeer(bgp_peer_);
        client->WaitForIdle();
    }

    void AddRemoteVmRoute(MacAddress &remote_vm_mac, const Ip4Address &ip_addr,
                          const Ip4Address &server_ip,
                          uint32_t label, TunnelType::TypeBmap bmap) {
        //Use any other peer than localvmpeer
        Inet4TunnelRouteAdd(bgp_peer_, vrf_name_, ip_addr, 32, server_ip,
                            bmap, label+1, vrf_name_,
                            SecurityGroupList(), TagList(), PathPreference());
        client->WaitForIdle();
        BridgeTunnelRouteAdd(bgp_peer_, vrf_name_, bmap, server_ip,
                             label, remote_vm_mac, ip_addr, 32);
        client->WaitForIdle();
    }

    void AddResolveRoute(const Ip4Address &server_ip, uint32_t plen) {
        VmInterfaceKey vhost_key(AgentKey::ADD_DEL_CHANGE,
                                 boost::uuids::nil_uuid(),
                                 agent_->vhost_interface()->name());
        agent_->fabric_inet4_unicast_table()->AddResolveRoute(
                agent_->local_peer(),
                agent_->fabric_vrf_name(), server_ip, plen, vhost_key,
                0, false, "", SecurityGroupList(), TagList());
        client->WaitForIdle();
    }

    void DeleteRoute(const Peer *peer, const std::string &vrf_name,
                     MacAddress &remote_vm_mac, const IpAddress &ip_addr) {
        const BgpPeer *bgp_peer = static_cast<const BgpPeer *>(peer);
        if (bgp_peer) {
            EvpnAgentRouteTable::DeleteReq(peer, vrf_name_, remote_vm_mac,
                                           ip_addr, 32, 0,
                                           (new ControllerVmRoute(bgp_peer)));
        } else {
            EvpnAgentRouteTable::DeleteReq(peer, vrf_name_, remote_vm_mac,
                                           ip_addr, 32, 0, NULL);
        }
        client->WaitForIdle();
        WAIT_FOR(1000, 1000, (L2RouteFind(vrf_name, remote_vm_mac, ip_addr) ==
                 false));
        if (bgp_peer) {
            agent_->fabric_inet4_unicast_table()->DeleteReq(peer, vrf_name,
                                                  ip_addr, 32,
                                                  (new ControllerVmRoute(bgp_peer)));
        } else {
            agent_->fabric_inet4_unicast_table()->DeleteReq(peer, vrf_name,
                                                            ip_addr, 32, NULL);
        }
        client->WaitForIdle();
    }

    void SetupSingleVmEnvironment() {
        client->Reset();
        //Add Ipam
        AddIPAM("vn1", ipam_info_dhcp_enable, 1);
        client->WaitForIdle();
        CreateVmportEnv(input, 1);
        client->WaitForIdle();

        end_of_rib_rx_seen_ = false;
        //Expect l3vpn or evpn route irrespective of forwarding mode.
        AddRemoteVmRoute(remote_vm_mac_, remote_vm_ip4_, server1_ip_,
                         200, (TunnelType::AllType()));
        client->WaitForIdle();
    }

    void DeleteSingleVmEnvironment() {
        //Time for deletion
        DelIPAM("vn1");
        client->WaitForIdle();
        DeleteVmportEnv(input, 1, true);
        client->WaitForIdle();
        if (bgp_peer_)
            DeleteRoute(bgp_peer_, vrf_name_, remote_vm_mac_, remote_vm_ip4_);
        client->WaitForIdle();

        WAIT_FOR(1000, 100,
                 (RouteFind(vrf_name_, local_vm_ip4_, 32) == false));
        WAIT_FOR(1000, 100,
                 (L2RouteFind(vrf_name_, local_vm_mac_, local_vm_ip4_) == false));
        EXPECT_FALSE(VmPortFind(input, 0));
        client->WaitForIdle();
    }

    void EndOfRibRx() {
        end_of_rib_rx_seen_ = true;
    }

    void WaitForChannelStateChangeRequestProcessed() {
        WAIT_FOR(1000, 1000, (agent_->controller()->IsWorkQueueEmpty() ==
                              true));
    }

    void NotReady(BgpPeer *bgp_peer) {
        AgentXmppChannel *channel = bgp_peer->GetAgentXmppChannel();
        AgentIfMapXmppChannel *ifmap_channel =
            agent_->ifmap_xmpp_channel(agent_->ifmap_active_xmpp_server_index());
        uint64_t sequence_number = channel->sequence_number();
        //Check for sequence number, should not change.
        AgentXmppChannel::XmppClientChannelEvent(bgp_peer_->GetAgentXmppChannel(),
                                                 xmps::NOT_READY);
        WaitForChannelStateChangeRequestProcessed();
        //All EORTx, EORRx, EOC, Config timers are not running.
        WAIT_FOR(1000, 1000, (channel->end_of_rib_tx_timer()->running() ==
                              false));
        WAIT_FOR(1000, 1000, (channel->end_of_rib_rx_timer()->running() == false));
        WAIT_FOR(1000, 1000, (ifmap_channel->config_cleanup_timer()->running()
                              == false));
        WAIT_FOR(1000, 1000, (ifmap_channel->end_of_config_timer()->running() ==
                              false));

        //Sequence number intact
        EXPECT_TRUE(channel->sequence_number() == sequence_number);
        //Verify all routes
        //Local VM routes, no bgp peer.
        InetUnicastRouteEntry* local_vm_rt = RouteGet("vrf1", local_vm_ip4_,
                                                       32);
        BridgeRouteEntry* local_vm_l2_rt = L2RouteGet("vrf1", local_vm_mac_);
        EvpnRouteEntry *local_evpn_rt = EvpnRouteGet("vrf1", local_vm_mac_,
                                                     local_vm_ip4_, 0);
        EXPECT_TRUE(local_vm_rt != NULL);
        EXPECT_TRUE(local_vm_l2_rt != NULL);
        EXPECT_TRUE(local_evpn_rt != NULL);
        EXPECT_TRUE(local_vm_rt->GetActivePath()->peer_sequence_number() == 0);
        EXPECT_TRUE(local_vm_l2_rt->GetActivePath()->peer_sequence_number() == 0);
        EXPECT_TRUE(local_evpn_rt->GetActivePath()->peer_sequence_number() == 0);

        //Remote VM routes with bgp peer.
        InetUnicastRouteEntry *remote_vm_rt = RouteGet("vrf1", remote_vm_ip4_, 32);
        BridgeRouteEntry *remote_vm_l2_rt = L2RouteGet("vrf1", remote_vm_mac_);
        EvpnRouteEntry *remote_evpn_rt = EvpnRouteGet("vrf1", remote_vm_mac_,
                                                      remote_vm_ip4_, 0);
        EXPECT_TRUE(remote_vm_rt != NULL);
        EXPECT_TRUE(remote_vm_l2_rt != NULL);
        EXPECT_TRUE(remote_evpn_rt != NULL);
        EXPECT_TRUE(remote_vm_rt->GetActivePath()->peer_sequence_number() ==
                    sequence_number);
        EXPECT_TRUE(remote_vm_l2_rt->GetActivePath()->peer_sequence_number() ==
                    0);
        EXPECT_TRUE(remote_evpn_rt->GetActivePath()->peer_sequence_number() ==
                    sequence_number);
    }

    void Ready(BgpPeer *bgp_peer, bool remote_route_delete) {
        AgentXmppChannel *channel = bgp_peer->GetAgentXmppChannel();
        AgentIfMapXmppChannel *ifmap_channel =
            agent_->ifmap_xmpp_channel(agent_->ifmap_active_xmpp_server_index());
        uint64_t sequence_number = channel->sequence_number();
        uint64_t current_time = UTCTimestampUsec();

        end_of_rib_rx_seen_ = false;
        bgp_peer->set_delete_stale_walker_cb
            (boost::bind(&LlgrTest::EndOfRibRx, this));

        //Check for sequence number, should not change.
        AgentXmppChannel::XmppClientChannelEvent(bgp_peer_->GetAgentXmppChannel(),
                                                 xmps::READY);
        WaitForChannelStateChangeRequestProcessed();
        //Sequence number bumped
        WAIT_FOR(1000, 1000, (channel->sequence_number() > sequence_number));

        //Fire end_of_config
        ifmap_channel->end_of_config_timer()->Fire();
        //fire may dampen the end of config enqueue because of inactivity time,
        //so artificially push it.
        ifmap_channel->EnqueueEndOfConfig();
        EXPECT_TRUE(ifmap_channel->end_of_config_timer()->running() == false);
        WAIT_FOR(1000, 1000,
         (ifmap_channel->end_of_config_timer()->end_of_config_processed_time_ >=
              current_time));

        //end of rib timer
        //fire the timer
        channel->end_of_rib_tx_timer()->Fire();
        //Force end of rib tx, in case timer decides otherwise because of
        //inactivity time
        channel->StartEndOfRibTxWalker();
        client->WaitForIdle();

        //Add the remote vm route, to update sequence number.
        AddRemoteVmRoute(remote_vm_mac_, remote_vm_ip4_, server1_ip_,
                         200, (TunnelType::AllType()));
        client->WaitForIdle();

        //End of rib rx timer fire
        channel->end_of_rib_rx_timer()->Fire();
        WAIT_FOR(1000, 1000, (end_of_rib_rx_seen_ == true));

        //Verify local vm routes, no bgp peer, so NOOP
        InetUnicastRouteEntry* local_vm_rt = RouteGet("vrf1", local_vm_ip4_,
                                                       32);
        BridgeRouteEntry* local_vm_l2_rt = L2RouteGet("vrf1", local_vm_mac_);
        EvpnRouteEntry *local_evpn_rt = EvpnRouteGet("vrf1", local_vm_mac_,
                                                     local_vm_ip4_, 0);
        EXPECT_TRUE(local_vm_rt != NULL);
        EXPECT_TRUE(local_vm_l2_rt != NULL);
        EXPECT_TRUE(local_evpn_rt != NULL);
        EXPECT_TRUE(local_vm_rt->GetActivePath()->peer_sequence_number() == 0);
        EXPECT_TRUE(local_vm_l2_rt->GetActivePath()->peer_sequence_number() == 0);
        EXPECT_TRUE(local_evpn_rt->GetActivePath()->peer_sequence_number() == 0);

        //Remote VM routes with bgp peer.
        InetUnicastRouteEntry *remote_vm_rt = RouteGet("vrf1", remote_vm_ip4_, 32);
        BridgeRouteEntry *remote_vm_l2_rt = L2RouteGet("vrf1", remote_vm_mac_);
        EvpnRouteEntry *remote_evpn_rt = EvpnRouteGet("vrf1", remote_vm_mac_,
                                                      remote_vm_ip4_, 0);
        EXPECT_TRUE(remote_vm_rt != NULL);
        EXPECT_TRUE(remote_vm_l2_rt != NULL);
        EXPECT_TRUE(remote_evpn_rt != NULL);
        WAIT_FOR(1000, 1000,
                 (remote_vm_rt->GetActivePath()->peer_sequence_number() ==
                  channel->sequence_number()));
        EXPECT_TRUE(remote_vm_l2_rt->GetActivePath()->peer_sequence_number() ==
                    0);
        WAIT_FOR(1000, 1000,
                 (remote_evpn_rt->GetActivePath()->peer_sequence_number() ==
                  channel->sequence_number()));

        //Check the state of bgp_peer on route
        VrfEntry *vrf1 = VrfGet("vrf1");
        VrfExport::State *vs = static_cast<VrfExport::State *>
            (bgp_peer->GetVrfExportState(vrf1->get_table_partition(), vrf1));
        WAIT_FOR(1000, 1000,
                 (vs->last_sequence_number_ == channel->sequence_number()));
        WAIT_FOR(1000, 1000,
                 (channel->end_of_rib_tx_timer()->running() == false));

        //All timers are fired, forcefully
        EXPECT_TRUE(channel->end_of_rib_rx_timer()->running() == false);
    }

    void TimedOut(BgpPeer *bgp_peer) {
        //Check for sequence number, should not change.
        AgentXmppChannel::XmppClientChannelEvent(bgp_peer_->GetAgentXmppChannel(),
                                                 xmps::TIMEDOUT);
        WaitForChannelStateChangeRequestProcessed();
        NotReady(bgp_peer);
        //TODO Verify channel is present in timed out list
    }

    void DeleteChannel(BgpPeer *bgp_peer) {
    }

    std::string vrf_name_;
    std::string eth_name_;
    Ip4Address  default_dest_ip_;

    char local_vm_mac_str_[100];
    MacAddress  local_vm_mac_;
    char local_vm_ip4_str_[100];
    Ip4Address  local_vm_ip4_;
    char local_vm_ip6_str_[100];
    IpAddress  local_vm_ip6_;

    char remote_vm_mac_str_[100];
    MacAddress  remote_vm_mac_;
    char remote_vm_ip4_str_[100];
    Ip4Address  remote_vm_ip4_;
    char remote_vm_ip6_str_[100];
    IpAddress  remote_vm_ip6_;

    Ip4Address  vhost_ip_;
    Ip4Address  server1_ip_;
    Ip4Address  server2_ip_;

    BgpPeer *bgp_peer_;
    bool end_of_rib_rx_seen_;
    Agent *agent_;
};

// One CN tests
TEST_F(LlgrTest, basic) {
    SetupSingleVmEnvironment();
    client->WaitForIdle();
    DeleteSingleVmEnvironment();
    client->WaitForIdle();
}

TEST_F(LlgrTest, ready_on_ready_channel) {
    SetupSingleVmEnvironment();
    client->WaitForIdle();
    //Ready
    Ready(bgp_peer_, false);
    client->WaitForIdle();
    //Cleanup
    DeleteSingleVmEnvironment();
    client->WaitForIdle();
}

TEST_F(LlgrTest, static_vrf_deleted_peer) {
    SetupSingleVmEnvironment();
    client->WaitForIdle();
    //Ready
    Ready(bgp_peer_, false);
    agent_->vrf_table()->CreateVrfReq("vrf10");
    client->WaitForIdle();
    DeleteBgpPeer(bgp_peer_);
    client->WaitForIdle();
    bgp_peer_ = NULL;
    //Cleanup
    agent_->vrf_table()->DeleteVrfReq("vrf10");
    DeleteSingleVmEnvironment();
    client->WaitForIdle();
}

TEST_F(LlgrTest, flap_control_node) {
    SetupSingleVmEnvironment();
    client->WaitForIdle();
    //NOT_READY
    NotReady(bgp_peer_);
    //Ready
    Ready(bgp_peer_, false);
    //Cleanup
    DeleteSingleVmEnvironment();
    client->WaitForIdle();
}

TEST_F(LlgrTest, timeout_control_node) {
    SetupSingleVmEnvironment();
    client->WaitForIdle();
    //Timeout
    TimedOut(bgp_peer_);
    //Cleanup
    DeleteSingleVmEnvironment();
    client->WaitForIdle();
}

int main(int argc, char *argv[]) {
    ::testing::InitGoogleTest(&argc, argv);
    GETUSERARGS();
    client = TestInit(init_file, ksync_init, true, false);
    eth_itf = Agent::GetInstance()->fabric_interface_name();

    int ret = 0;
    ret += RUN_ALL_TESTS();
    client->WaitForIdle();
    TestShutdown();
    delete client;
    return ret;
}
