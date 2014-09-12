#include <test/test_basic_scale.h>

// Bring channel down and delete VRF, check vrf state for peer
TEST_F(AgentBasicScaleTest, Del_peer_deleted_vrf_1) {
    client->Reset();
    client->WaitForIdle();

    XmppConnectionSetUp();
    BuildVmPortEnvironment();
    
    //Get the vrf export id
    BgpPeer *peer = static_cast<BgpPeer *>(bgp_peer[0].get()->bgp_peer_id());
    uint32_t listener_id = peer->GetVrfExportListenerId();
    EXPECT_TRUE(listener_id != 0);
    VrfEntry *vrf = VrfGet("vrf1");
    DBTablePartBase *part = Agent::GetInstance()->vrf_table()->GetTablePartition(vrf);
    EXPECT_TRUE(vrf->GetState(part->parent(), listener_id) != NULL);

    bgp_peer[0].get()->HandleXmppChannelEvent(xmps::NOT_READY);
    client->WaitForIdle();
    DelVrf("vrf1");
    client->WaitForIdle();

    EXPECT_TRUE(vrf->GetState(part->parent(), listener_id) == NULL);

    //Delete vm-port and route entry in vrf1
    DeleteVmPortEnvironment();
}

// Delete VRF and bring channel down, check vrf state for peer
TEST_F(AgentBasicScaleTest, Del_peer_deleted_vrf_2) {
    client->Reset();
    client->WaitForIdle();

    XmppConnectionSetUp();
    BuildVmPortEnvironment();
    
    //Get the vrf export id
    BgpPeer *peer = static_cast<BgpPeer *>(bgp_peer[0].get()->bgp_peer_id());
    uint32_t listener_id = peer->GetVrfExportListenerId();
    EXPECT_TRUE(listener_id != 0);
    VrfEntry *vrf = VrfGet("vrf1");
    DBTablePartBase *part = Agent::GetInstance()->vrf_table()->GetTablePartition(vrf);
    EXPECT_TRUE(vrf->GetState(part->parent(), listener_id) != NULL);

    DelVrf("vrf1");
    client->WaitForIdle();

    EXPECT_TRUE(vrf->GetState(part->parent(), listener_id) == NULL);

    bgp_peer[0].get()->HandleXmppChannelEvent(xmps::NOT_READY);
    client->WaitForIdle();
    //Delete vm-port and route entry in vrf1
    DeleteVmPortEnvironment();
}

TEST_F(AgentBasicScaleTest, Basic) {
    client->Reset();
    client->WaitForIdle();

    XmppConnectionSetUp();
    BuildVmPortEnvironment();
	IpamInfo ipam_info[] = {
	    {"1.1.1.0", 24, "1.1.1.200", true}
	};
    AddIPAM("vn1", ipam_info, 1);
    WAIT_FOR(1000, 10000, RouteFind("vrf1", Ip4Address::from_string("1.1.1.255"), 32));

    Ip4Address mc_addr = Ip4Address::from_string("255.255.255.255");
    WAIT_FOR(1000, 10000, MCRouteFind("vrf1", mc_addr));
    Ip4Address uc_addr = Ip4Address::from_string("1.1.1.1");
    WAIT_FOR(1000, 1000, RouteFind("vrf1", uc_addr, 32));
    const struct ether_addr *flood_mac = ether_aton("ff:ff:ff:ff:ff:ff");
    EXPECT_TRUE(L2RouteFind("vrf1", *flood_mac));
    const struct ether_addr *mac = ether_aton("00:00:00:00:01:01");
    EXPECT_TRUE(L2RouteFind("vrf1", *mac));

    VerifyVmPortActive(true);
    VerifyRoutes(false);
    mc_addr = Ip4Address::from_string("1.1.1.255");
    EXPECT_TRUE(RouteFind("vrf1", mc_addr, 32));
    
    //Delete vm-port and route entry in vrf1
    DelIPAM("vn1");
    WAIT_FOR(1000, 10000, !RouteFind("vrf1", Ip4Address::from_string("1.1.1.255"), 32));
    DeleteVmPortEnvironment();
}

TEST_F(AgentBasicScaleTest, multicast_one_channel_down_up) {
    client->Reset();
    client->WaitForIdle();

    XmppConnectionSetUp();
    BuildVmPortEnvironment();
	IpamInfo ipam_info[] = {
	    {"1.1.1.0", 24, "1.1.1.200", true}
	};
    AddIPAM("vn1", ipam_info, 1);
    WAIT_FOR(1000, 10000, RouteFind("vrf1", Ip4Address::from_string("1.1.1.255"), 32));

    //expect subscribe message+route at the mock server
    Ip4Address mc_addr = Ip4Address::from_string("255.255.255.255");
    WAIT_FOR(1000, 10000, MCRouteFind("vrf1", mc_addr));
    MulticastGroupObject *flood_mcobj = MulticastHandler::GetInstance()->
        FindGroupObject("vrf1", mc_addr);

    VerifyVmPortActive(true);
    VerifyRoutes(false);

    mc_addr = Ip4Address::from_string("1.1.1.255");
    EXPECT_TRUE(RouteFind("vrf1", mc_addr, 32));
    
    //Store the src mpls label to verify it does not change after channel down
    MulticastGroupObject *mcobj = MulticastHandler::GetInstance()->
        FindGroupObject("vrf1", mc_addr);
    EXPECT_TRUE(mcobj != NULL);

    uint32_t old_multicast_identifier = 
        Agent::GetInstance()->controller()->multicast_sequence_number();
    //WAIT_FOR(1000, 10000, (mcobj->GetSourceMPLSLabel() != 0));
    //uint32_t subnet_src_label = mcobj->GetSourceMPLSLabel();

    //WAIT_FOR(1000, 10000, (flood_mcobj->GetSourceMPLSLabel() != 0));
    //WAIT_FOR(1000, 10000, (flood_mcobj->GetEvpnOlist().size() == 2));

    //Bring down the channel
    AgentXmppChannel *ch = static_cast<AgentXmppChannel *>(bgp_peer[0].get());
    bgp_peer[0].get()->HandleXmppChannelEvent(xmps::NOT_READY);
    client->WaitForIdle();
    mc_addr = Ip4Address::from_string("1.1.1.255");
    EXPECT_TRUE(RouteFind("vrf1", mc_addr, 32));
    mcobj = MulticastHandler::GetInstance()->FindGroupObject("vrf1", mc_addr);
    EXPECT_TRUE(mcobj != NULL);
    //EXPECT_TRUE(mcobj->GetSourceMPLSLabel() == subnet_src_label);
    WAIT_FOR(1000, 10000, ((mcobj->peer_identifier() + 1) == 
                Agent::GetInstance()->controller()->multicast_sequence_number()));

    mc_addr = Ip4Address::from_string("255.255.255.255");
    mcobj = MulticastHandler::GetInstance()->FindGroupObject("vrf1", mc_addr);
    //uint32_t source_flood_label = mcobj->GetSourceMPLSLabel();
    EXPECT_TRUE(MCRouteFind("vrf1", mc_addr));
    //EXPECT_TRUE(mcobj->GetSourceMPLSLabel() != 0);
    WAIT_FOR(1000, 10000, ((mcobj->peer_identifier() + 1) == 
                Agent::GetInstance()->controller()->multicast_sequence_number()));

    //Bring up the channel
    bgp_peer[0].get()->HandleXmppChannelEvent(xmps::READY);
    VerifyConnections(0, 17);

    mc_addr = Ip4Address::from_string("1.1.1.255");
    EXPECT_TRUE(RouteFind("vrf1", mc_addr, 32));
    mcobj = MulticastHandler::GetInstance()->FindGroupObject("vrf1", mc_addr);
    EXPECT_TRUE(mcobj != NULL);
    //EXPECT_TRUE(mcobj->GetSourceMPLSLabel() != 0);
    //WAIT_FOR(1000, 1000, (mcobj->GetSourceMPLSLabel() != subnet_src_label));
    EXPECT_TRUE(mcobj->peer_identifier() == 
                Agent::GetInstance()->controller()->multicast_sequence_number());
    mc_addr = Ip4Address::from_string("255.255.255.255");
    mcobj = MulticastHandler::GetInstance()->FindGroupObject("vrf1", mc_addr);
    EXPECT_TRUE(MCRouteFind("vrf1", mc_addr));
    //EXPECT_TRUE(mcobj->GetSourceMPLSLabel() != 0);
    //WAIT_FOR(1000, 1000, (mcobj->GetSourceMPLSLabel() != source_flood_label));
    EXPECT_TRUE(mcobj->peer_identifier() == 
                Agent::GetInstance()->controller()->multicast_sequence_number());
    EXPECT_TRUE(old_multicast_identifier != 
                Agent::GetInstance()->controller()->multicast_sequence_number());

    //Delete vm-port and route entry in vrf1
    DelIPAM("vn1");
    mc_addr = Ip4Address::from_string("1.1.1.255");
    WAIT_FOR(1000, 10000, !RouteFind("vrf1", Ip4Address::from_string("1.1.1.255"), 32));
    DeleteVmPortEnvironment();
}

TEST_F(AgentBasicScaleTest, multicast_one_channel_down_up_skip_route_from_peer) {
    //TODO modify the value for two peers
    if (num_ctrl_peers != 1)
        return;

    client->Reset();
    client->WaitForIdle();

    XmppConnectionSetUp();
    BuildVmPortEnvironment();
	IpamInfo ipam_info[] = {
	    {"1.1.1.0", 24, "1.1.1.200", true}
	};
    AddIPAM("vn1", ipam_info, 1);
    WAIT_FOR(1000, 10000, RouteFind("vrf1", Ip4Address::from_string("1.1.1.255"), 32));

    //expect subscribe message+route at the mock server
    Ip4Address mc_addr = Ip4Address::from_string("255.255.255.255");
    WAIT_FOR(1000, 10000, MCRouteFind("vrf1", mc_addr));

    VerifyVmPortActive(true);
    VerifyRoutes(false);
    mc_addr = Ip4Address::from_string("1.1.1.255");
    EXPECT_TRUE(RouteFind("vrf1", mc_addr, 32));
    
    //Store the src mpls label to verify it does not change after channel down
    MulticastGroupObject *mcobj = MulticastHandler::GetInstance()->
        FindGroupObject("vrf1", mc_addr);
    EXPECT_TRUE(mcobj != NULL);

    uint32_t old_multicast_identifier = 
        Agent::GetInstance()->controller()->multicast_sequence_number();
    //WAIT_FOR(1000, 1000, (mcobj->GetSourceMPLSLabel() != 0));
    //uint32_t subnet_src_label = mcobj->GetSourceMPLSLabel();

    //Bring down the channel
    AgentXmppChannel *ch = static_cast<AgentXmppChannel *>(bgp_peer[0].get());
    bgp_peer[0].get()->HandleXmppChannelEvent(xmps::NOT_READY);
    client->WaitForIdle();

    mc_addr = Ip4Address::from_string("1.1.1.255");
    EXPECT_TRUE(RouteFind("vrf1", mc_addr, 32));
    mcobj = MulticastHandler::GetInstance()->FindGroupObject("vrf1", mc_addr);
    EXPECT_TRUE(mcobj != NULL);
    //EXPECT_TRUE(mcobj->GetSourceMPLSLabel() == subnet_src_label);
    EXPECT_TRUE((mcobj->peer_identifier() + 1) == 
               Agent::GetInstance()->controller()->multicast_sequence_number());

    //uint32_t source_flood_label = mcobj->GetSourceMPLSLabel();
    mc_addr = Ip4Address::from_string("255.255.255.255");
    EXPECT_TRUE(MCRouteFind("vrf1", mc_addr));
    //EXPECT_TRUE(mcobj->GetSourceMPLSLabel() != 0);
    EXPECT_TRUE((mcobj->peer_identifier() + 1) == 
           Agent::GetInstance()->controller()->multicast_sequence_number());

    //Bring up the channel
    mock_peer[0].get()->SkipRoute("1.1.1.255");
    bgp_peer[0].get()->HandleXmppChannelEvent(xmps::READY);
    VerifyConnections(0, 17);

    mc_addr = Ip4Address::from_string("1.1.1.255");
    EXPECT_TRUE(RouteFind("vrf1", mc_addr, 32));
    mcobj = MulticastHandler::GetInstance()->FindGroupObject("vrf1", mc_addr);
    EXPECT_TRUE(mcobj != NULL);
    //EXPECT_TRUE(mcobj->GetSourceMPLSLabel() != 0);
    //EXPECT_TRUE(mcobj->GetSourceMPLSLabel() == subnet_src_label);
    EXPECT_TRUE(mcobj->peer_identifier() == old_multicast_identifier); 
    mc_addr = Ip4Address::from_string("255.255.255.255");
    mcobj = MulticastHandler::GetInstance()->FindGroupObject("vrf1", mc_addr);
    EXPECT_TRUE(MCRouteFind("vrf1", mc_addr));
    //EXPECT_TRUE(mcobj->GetSourceMPLSLabel() != 0);
    WAIT_FOR(1000, 1000, (mcobj->peer_identifier() == 
                Agent::GetInstance()->controller()->multicast_sequence_number()));
    //EXPECT_TRUE(mcobj->GetSourceMPLSLabel() != source_flood_label);
    EXPECT_TRUE(old_multicast_identifier != 
                Agent::GetInstance()->controller()->multicast_sequence_number());
    EXPECT_TRUE(Agent::GetInstance()->controller()->multicast_cleanup_timer().cleanup_timer_->running());

    //Fire the timer
    client->WaitForIdle();
    TaskScheduler::GetInstance()->Stop();
    Agent::GetInstance()->controller()->multicast_cleanup_timer().cleanup_timer_->Fire();
    TaskScheduler::GetInstance()->Start();
    client->WaitForIdle();
    mc_addr = Ip4Address::from_string("1.1.1.255");
    mcobj = MulticastHandler::GetInstance()->FindGroupObject("vrf1", mc_addr);
    EXPECT_TRUE(mcobj != NULL);
    //WAIT_FOR(1000, 1000, (mcobj->GetSourceMPLSLabel() == 0));

    //Delete vm-port and route entry in vrf1
    DelIPAM("vn1");
    WAIT_FOR(1000, 10000, !RouteFind("vrf1", Ip4Address::from_string("1.1.1.255"), 32));
    DeleteVmPortEnvironment();
}

TEST_F(AgentBasicScaleTest, v4_unicast_one_channel_down_up) {
    //TODO modify the value for two peers
    if (num_ctrl_peers != 1)
        return;

    client->Reset();
    client->WaitForIdle();

    XmppConnectionSetUp();
    BuildVmPortEnvironment();

    //expect subscribe message+route at the mock server
    Ip4Address uc_addr = Ip4Address::from_string("1.1.1.1");
    WAIT_FOR(1000, 10000, RouteFind("vrf1", uc_addr, 32));
    Inet4UnicastRouteEntry *rt = RouteGet("vrf1", uc_addr, 32);
    if (num_ctrl_peers == 2) {
        WAIT_FOR(1000, 10000, (rt->GetPathList().size() == 3));
    } else {
        WAIT_FOR(1000, 10000, (rt->GetPathList().size() == 2));
    }

    //Get the peer
    Peer *peer = Agent::GetInstance()->controller_xmpp_channel(0)->bgp_peer_id();
    AgentPath *path = static_cast<AgentPath *>(rt->FindPath(peer));
    EXPECT_TRUE(path->is_stale() == false);

    VerifyVmPortActive(true);
    VerifyRoutes(false);
    
    //Bring down the channel
    AgentXmppChannel *ch = static_cast<AgentXmppChannel *>(bgp_peer[0].get());
    bgp_peer[0].get()->HandleXmppChannelEvent(xmps::NOT_READY);
    path = static_cast<AgentPath *>(rt->FindPath(peer));
    WAIT_FOR(1000, 1000, (path->is_stale()));
    EXPECT_TRUE(RouteFind("vrf1", uc_addr, 32));
    client->WaitForIdle();

    //Bring up the channel
    bgp_peer[0].get()->HandleXmppChannelEvent(xmps::READY);
    if (num_ctrl_peers == 2) {
        WAIT_FOR(1000, 10000, (rt->GetPathList().size() == 4));
    } else {
        WAIT_FOR(1000, 10000, (rt->GetPathList().size() == 3));
    }
    EXPECT_TRUE(RouteFind("vrf1", uc_addr, 32));
    path = static_cast<AgentPath *>(rt->FindPath(peer));
    EXPECT_TRUE(path->is_stale());
    Peer *new_peer = Agent::GetInstance()->controller_xmpp_channel(0)->bgp_peer_id();
    AgentPath *new_path = static_cast<AgentPath *>(rt->FindPath(new_peer));
    EXPECT_TRUE(new_path != path);
    EXPECT_TRUE(!new_path->is_stale());
    EXPECT_TRUE(path->is_stale());

    //Fire timer and verify stale path is gone
    TaskScheduler::GetInstance()->Stop();
    Agent::GetInstance()->controller()->unicast_cleanup_timer().cleanup_timer_->Fire();
    TaskScheduler::GetInstance()->Start();
    WAIT_FOR(1000, 1000, (rt->FindPath(peer) == NULL));
    if (num_ctrl_peers == 2) {
        EXPECT_TRUE(rt->GetPathList().size() == 3);
    } else {
        EXPECT_TRUE(rt->GetPathList().size() == 2);
    }
    new_path = static_cast<AgentPath *>(rt->FindPath(new_peer));
    EXPECT_TRUE(!new_path->is_stale());

    //Delete vm-port and route entry in vrf1
    DeleteVmPortEnvironment();
}

TEST_F(AgentBasicScaleTest, walk_on_vrf_marked_for_delete) {
    //TODO modify the value for two peers
    if (num_ctrl_peers != 1)
        return;

    client->Reset();
    client->WaitForIdle();

    XmppConnectionSetUp();
    BuildVmPortEnvironment();

    //expect subscribe message+route at the mock server
    Ip4Address uc_addr = Ip4Address::from_string("1.1.1.1");
    WAIT_FOR(1000, 10000, RouteFind("vrf1", uc_addr, 32));
    Inet4UnicastRouteEntry *rt = RouteGet("vrf1", uc_addr, 32);
    if (num_ctrl_peers == 2) {
        WAIT_FOR(1000, 10000, (rt->GetPathList().size() == 3));
    } else {
        WAIT_FOR(1000, 10000, (rt->GetPathList().size() == 2));
    }

    //Get the peer
    Peer *peer = Agent::GetInstance()->controller_xmpp_channel(0)->bgp_peer_id();
    AgentPath *path = static_cast<AgentPath *>(rt->FindPath(peer));
    EXPECT_TRUE(path->is_stale() == false);

    VerifyVmPortActive(true);
    VerifyRoutes(false);

    //Bring down the channel
    bgp_peer[0].get()->HandleXmppChannelEvent(xmps::NOT_READY);
    client->WaitForIdle();
    
    //VRF will not get deleted as interface is yet present. So bringing up
    //channel should invoke walk on this VRF and skip it.
    DelVrf("vrf1");
    WAIT_FOR(1000, 1000, (VrfGet("vrf1") == NULL));

    //Bring up the channel
    bgp_peer[0].get()->HandleXmppChannelEvent(xmps::READY);
    VerifyConnections(0, 7);
    WAIT_FOR(1000, 10000, (RouteFind("vrf1", uc_addr, 32) == false));

    //Delete vm-port and route entry in vrf1
    DeleteVmPortEnvironment();
}

static uint8_t CountStalePath(AgentRoute *rt) {
    uint8_t stale_path_count = 0;
    //Check only one stale path is thr
    for(Route::PathList::const_iterator it = rt->GetPathList().begin(); 
        it != rt->GetPathList().end(); it++) {
        const AgentPath *path = static_cast<const AgentPath *>(it.operator->());
        if (path->is_stale()) {
            EXPECT_TRUE(path->peer()->GetType() == Peer::BGP_PEER);
            stale_path_count++;
        } 
    }
    return stale_path_count;
}

TEST_F(AgentBasicScaleTest, flap_xmpp_channel_check_stale_path_count) {
    //TODO modify the value for two peers
    if (num_ctrl_peers != 1)
        return;

    client->Reset();
    client->WaitForIdle();

    XmppConnectionSetUp();
    BuildVmPortEnvironment();

    //expect subscribe message+route at the mock server
    Ip4Address uc_addr = Ip4Address::from_string("1.1.1.1");
    WAIT_FOR(1000, 10000, RouteFind("vrf1", uc_addr, 32));
    Inet4UnicastRouteEntry *rt = RouteGet("vrf1", uc_addr, 32);
    WAIT_FOR(1000, 10000, (rt->GetPathList().size() == 2));

    //Get the peer
    Peer *peer = Agent::GetInstance()->controller_xmpp_channel(0)->bgp_peer_id();
    AgentPath *path = static_cast<AgentPath *>(rt->FindPath(peer));
    EXPECT_TRUE(path->is_stale() == false);

    VerifyVmPortActive(true);
    VerifyRoutes(false);
    
    AgentXmppChannel *ch = static_cast<AgentXmppChannel *>(bgp_peer[0].get());
    //Bring down the channel
    bgp_peer[0].get()->HandleXmppChannelEvent(xmps::NOT_READY);
    client->WaitForIdle();
    EXPECT_TRUE(CountStalePath(rt) == 1);
    //Bring up the channel
    bgp_peer[0].get()->HandleXmppChannelEvent(xmps::READY);
    client->WaitForIdle();
    EXPECT_TRUE(CountStalePath(rt) == 1);
    //Bring down the channel
    bgp_peer[0].get()->HandleXmppChannelEvent(xmps::NOT_READY);
    client->WaitForIdle();
    EXPECT_TRUE(CountStalePath(rt) == 1);
    //Bring up the channel
    bgp_peer[0].get()->HandleXmppChannelEvent(xmps::READY);
    client->WaitForIdle();
    EXPECT_TRUE(CountStalePath(rt) == 1);

    //Delete vm-port and route entry in vrf1
    DeleteVmPortEnvironment();
}

TEST_F(AgentBasicScaleTest, unicast_one_channel_down_up_skip_route_from_peer) {
    //TODO modify the value for two peers
    if (num_ctrl_peers != 1)
        return;

    client->Reset();
    client->WaitForIdle();

    XmppConnectionSetUp();
    BuildVmPortEnvironment();
	IpamInfo ipam_info[] = {
	    {"1.1.1.0", 24, "1.1.1.200", true}
	};
    AddIPAM("vn1", ipam_info, 1);
    WAIT_FOR(1000, 10000, RouteFind("vrf1", Ip4Address::from_string("1.1.1.255"), 32));

    //expect subscribe message+route at the mock server
    Ip4Address mc_addr = Ip4Address::from_string("255.255.255.255");
    WAIT_FOR(1000, 10000, MCRouteFind("vrf1", mc_addr));
    //WAIT_FOR(100, 10000, (mock_peer[0].get()->Count() == 
    //                      ((6 * num_vns * num_vms_per_vn) + num_vns)));

    VerifyVmPortActive(true);
    VerifyRoutes(false);
    mc_addr = Ip4Address::from_string("1.1.1.255");
    EXPECT_TRUE(RouteFind("vrf1", mc_addr, 32));
    
    //Store the src mpls label to verify it does not change after channel down
    MulticastGroupObject *mcobj = MulticastHandler::GetInstance()->
        FindGroupObject("vrf1", mc_addr);
    EXPECT_TRUE(mcobj != NULL);

    uint32_t old_multicast_identifier = 
        Agent::GetInstance()->controller()->multicast_sequence_number();
    //WAIT_FOR(1000, 1000, (mcobj->GetSourceMPLSLabel() != 0));
    //uint32_t subnet_src_label = mcobj->GetSourceMPLSLabel();
    //EXPECT_TRUE(Agent::GetInstance()->mpls_table()->FindMplsLabel(subnet_src_label));

    //Bring down the channel
    AgentXmppChannel *ch = static_cast<AgentXmppChannel *>(bgp_peer[0].get());
    bgp_peer[0].get()->HandleXmppChannelEvent(xmps::NOT_READY);
    mc_addr = Ip4Address::from_string("1.1.1.255");
    EXPECT_TRUE(RouteFind("vrf1", mc_addr, 32));
    mcobj = MulticastHandler::GetInstance()->FindGroupObject("vrf1", mc_addr);
    EXPECT_TRUE(mcobj != NULL);
    //EXPECT_TRUE(mcobj->GetSourceMPLSLabel() == subnet_src_label);
    EXPECT_TRUE(mcobj->peer_identifier() == 
                Agent::GetInstance()->controller()->multicast_sequence_number());

    //uint32_t source_flood_label = mcobj->GetSourceMPLSLabel();
    mc_addr = Ip4Address::from_string("255.255.255.255");
    EXPECT_TRUE(MCRouteFind("vrf1", mc_addr));
    //EXPECT_TRUE(mcobj->GetSourceMPLSLabel() != 0);
    EXPECT_TRUE(mcobj->peer_identifier() == 
                Agent::GetInstance()->controller()->multicast_sequence_number());

    //Bring up the channel
    mock_peer[0].get()->SkipRoute("1.1.1.255");
    bgp_peer[0].get()->HandleXmppChannelEvent(xmps::READY);
    client->WaitForIdle();

    mc_addr = Ip4Address::from_string("1.1.1.255");
    EXPECT_TRUE(RouteFind("vrf1", mc_addr, 32));
    mcobj = MulticastHandler::GetInstance()->FindGroupObject("vrf1", mc_addr);
    EXPECT_TRUE(mcobj != NULL);
    //EXPECT_TRUE(mcobj->GetSourceMPLSLabel() != 0);
    //EXPECT_TRUE(mcobj->GetSourceMPLSLabel() == subnet_src_label);
    EXPECT_TRUE(mcobj->peer_identifier() == old_multicast_identifier); 
    mc_addr = Ip4Address::from_string("255.255.255.255");
    mcobj = MulticastHandler::GetInstance()->FindGroupObject("vrf1", mc_addr);
    EXPECT_TRUE(MCRouteFind("vrf1", mc_addr));
    //EXPECT_TRUE(mcobj->GetSourceMPLSLabel() != 0);
    WAIT_FOR(1000, 1000, (mcobj->peer_identifier() == 
                Agent::GetInstance()->controller()->multicast_sequence_number()));
    //EXPECT_TRUE(mcobj->GetSourceMPLSLabel() != source_flood_label);
    EXPECT_TRUE(old_multicast_identifier != 
                Agent::GetInstance()->controller()->multicast_sequence_number());
    EXPECT_TRUE(Agent::GetInstance()->controller()->multicast_cleanup_timer().cleanup_timer_->running());

    //Fire the timer
    TaskScheduler::GetInstance()->Stop();
    Agent::GetInstance()->controller()->multicast_cleanup_timer().cleanup_timer_->Fire();
    TaskScheduler::GetInstance()->Start();
    client->WaitForIdle();
    mc_addr = Ip4Address::from_string("1.1.1.255");
    mcobj = MulticastHandler::GetInstance()->FindGroupObject("vrf1", mc_addr);
    EXPECT_TRUE(mcobj != NULL);
    //WAIT_FOR(1000, 1000, (mcobj->GetSourceMPLSLabel() == 0));

    //Delete vm-port and route entry in vrf1
    DelIPAM("vn1");
    WAIT_FOR(1000, 10000, !RouteFind("vrf1", Ip4Address::from_string("1.1.1.255"), 32));
    DeleteVmPortEnvironment();
}

//Bring all peer down and then bring up one.
//Later bring up second and keep flapping second
//Expectation is that timer should not reschedule because of second peer
//flapping.
TEST_F(AgentBasicScaleTest, unicast_cleanup_timer_1) {
    if (num_ctrl_peers != 2)
        return;

    client->Reset();
    client->WaitForIdle();

    XmppConnectionSetUp();
    BuildVmPortEnvironment();

    //expect subscribe message+route at the mock server
    Ip4Address uc_addr = Ip4Address::from_string("1.1.1.1");
    WAIT_FOR(1000, 10000, RouteFind("vrf1", uc_addr, 32));
    Inet4UnicastRouteEntry *rt = RouteGet("vrf1", uc_addr, 32);
    WAIT_FOR(1000, 10000, (rt->GetPathList().size() == 3));

    //Get the peer
    Peer *peer_1 = Agent::GetInstance()->controller_xmpp_channel(0)->bgp_peer_id();
    Peer *peer_2 = Agent::GetInstance()->controller_xmpp_channel(1)->bgp_peer_id();
    AgentPath *path1 = static_cast<AgentPath *>(rt->FindPath(peer_1));
    EXPECT_TRUE(path1->is_stale() == false);
    AgentPath *path2 = static_cast<AgentPath *>(rt->FindPath(peer_2));
    EXPECT_TRUE(path2->is_stale() == false);

    VerifyVmPortActive(true);
    VerifyRoutes(false);
    
    AgentXmppChannel *ch1 = static_cast<AgentXmppChannel *>(bgp_peer[0].get());
    AgentXmppChannel *ch2 = static_cast<AgentXmppChannel *>(bgp_peer[1].get());
    //Bring down the channel
    bgp_peer[0].get()->HandleXmppChannelEvent(xmps::NOT_READY);
    client->WaitForIdle();
    EXPECT_TRUE(CountStalePath(rt) == 0);

    bgp_peer[1].get()->HandleXmppChannelEvent(xmps::NOT_READY);
    client->WaitForIdle();
    EXPECT_TRUE(CountStalePath(rt) == 1);
    uint32_t stale_timeout_interval = Agent::GetInstance()->controller()->
        unicast_cleanup_timer().stale_timer_interval();
    EXPECT_TRUE(stale_timeout_interval != 0);
    EXPECT_TRUE(Agent::GetInstance()->controller()->
                unicast_cleanup_timer().cleanup_timer_->running() == false);
    AgentXmppChannel *ch = Agent::GetInstance()->controller()->
       unicast_cleanup_timer().agent_xmpp_channel_; 
    EXPECT_TRUE(ch == NULL);

    //Bring up the channel
    bgp_peer[0].get()->HandleXmppChannelEvent(xmps::READY);
    client->WaitForIdle();
    EXPECT_TRUE(CountStalePath(rt) == 1);
    stale_timeout_interval = Agent::GetInstance()->controller()->
        unicast_cleanup_timer().stale_timer_interval();
    EXPECT_TRUE(stale_timeout_interval != 0);
    EXPECT_TRUE(Agent::GetInstance()->controller()->
                unicast_cleanup_timer().cleanup_timer_->running() == true);
    ch = Agent::GetInstance()->controller()->
       unicast_cleanup_timer().agent_xmpp_channel_; 
    EXPECT_TRUE(ch != NULL);

    bgp_peer[1].get()->HandleXmppChannelEvent(xmps::READY);
    client->WaitForIdle();
    EXPECT_TRUE(CountStalePath(rt) == 1);
    stale_timeout_interval = Agent::GetInstance()->controller()->
        unicast_cleanup_timer().stale_timer_interval();
    EXPECT_TRUE(stale_timeout_interval != 0);
    EXPECT_TRUE(Agent::GetInstance()->controller()->
                unicast_cleanup_timer().cleanup_timer_->running() == true);
    AgentXmppChannel *ch_2 = Agent::GetInstance()->controller()->
       unicast_cleanup_timer().agent_xmpp_channel_; 
    EXPECT_TRUE(ch == ch_2);

    //Flap the second channel
    bgp_peer[1].get()->HandleXmppChannelEvent(xmps::NOT_READY);
    client->WaitForIdle();
    EXPECT_TRUE(CountStalePath(rt) == 1);

    bgp_peer[1].get()->HandleXmppChannelEvent(xmps::READY);
    client->WaitForIdle();
    EXPECT_TRUE(CountStalePath(rt) == 1);
    ch_2 = Agent::GetInstance()->controller()->
       unicast_cleanup_timer().agent_xmpp_channel_; 
    EXPECT_TRUE(Agent::GetInstance()->controller()->
                unicast_cleanup_timer().cleanup_timer_->running() == true);
    EXPECT_TRUE(ch == ch_2);

    //Delete vm-port and route entry in vrf1
    DeleteVmPortEnvironment();
}

TEST_F(AgentBasicScaleTest, unicast_cleanup_timer_2) {
    if (num_ctrl_peers != 2)
        return;

    client->Reset();
    client->WaitForIdle();

    XmppConnectionSetUp();
    BuildVmPortEnvironment();

    //expect subscribe message+route at the mock server
    Ip4Address uc_addr = Ip4Address::from_string("1.1.1.1");
    WAIT_FOR(1000, 10000, RouteFind("vrf1", uc_addr, 32));
    Inet4UnicastRouteEntry *rt = RouteGet("vrf1", uc_addr, 32);
    WAIT_FOR(1000, 10000, (rt->GetPathList().size() == 3));

    //Get the peer
    Peer *peer_1 = Agent::GetInstance()->controller_xmpp_channel(0)->bgp_peer_id();
    Peer *peer_2 = Agent::GetInstance()->controller_xmpp_channel(1)->bgp_peer_id();
    AgentPath *path1 = static_cast<AgentPath *>(rt->FindPath(peer_1));
    EXPECT_TRUE(path1->is_stale() == false);
    AgentPath *path2 = static_cast<AgentPath *>(rt->FindPath(peer_2));
    EXPECT_TRUE(path2->is_stale() == false);

    VerifyVmPortActive(true);
    VerifyRoutes(false);
    
    AgentXmppChannel *ch1 = static_cast<AgentXmppChannel *>(bgp_peer[0].get());
    AgentXmppChannel *ch2 = static_cast<AgentXmppChannel *>(bgp_peer[1].get());
    //Bring down the channel
    bgp_peer[0].get()->HandleXmppChannelEvent(xmps::NOT_READY);
    client->WaitForIdle();
    EXPECT_TRUE(CountStalePath(rt) == 0);

    bgp_peer[1].get()->HandleXmppChannelEvent(xmps::NOT_READY);
    client->WaitForIdle();
    EXPECT_TRUE(CountStalePath(rt) == 1);
    uint32_t stale_timeout_interval = Agent::GetInstance()->controller()->
        unicast_cleanup_timer().stale_timer_interval();
    EXPECT_TRUE(stale_timeout_interval != 0);
    AgentXmppChannel *ch = Agent::GetInstance()->controller()->
       unicast_cleanup_timer().agent_xmpp_channel_; 
    EXPECT_TRUE(ch == NULL);

    //Bring up the channel
    bgp_peer[0].get()->HandleXmppChannelEvent(xmps::READY);
    client->WaitForIdle();
    EXPECT_TRUE(CountStalePath(rt) == 1);
    stale_timeout_interval = Agent::GetInstance()->controller()->
        unicast_cleanup_timer().stale_timer_interval();
    EXPECT_TRUE(stale_timeout_interval != 0);
    ch = Agent::GetInstance()->controller()->
       unicast_cleanup_timer().agent_xmpp_channel_; 
    EXPECT_TRUE(ch != NULL);
    EXPECT_TRUE(Agent::GetInstance()->controller()->
                unicast_cleanup_timer().cleanup_timer_->running() == true);

    bgp_peer[1].get()->HandleXmppChannelEvent(xmps::READY);
    client->WaitForIdle();
    EXPECT_TRUE(CountStalePath(rt) == 1);
    stale_timeout_interval = Agent::GetInstance()->controller()->
        unicast_cleanup_timer().stale_timer_interval();
    EXPECT_TRUE(stale_timeout_interval != 0);
    AgentXmppChannel *ch_2 = Agent::GetInstance()->controller()->
       unicast_cleanup_timer().agent_xmpp_channel_; 
    EXPECT_TRUE(ch == ch_2);
    EXPECT_TRUE(Agent::GetInstance()->controller()->
                unicast_cleanup_timer().cleanup_timer_->running() == true);

    //Bring down the channel 0 (since that started the timer)
    bgp_peer[0].get()->HandleXmppChannelEvent(xmps::NOT_READY);
    client->WaitForIdle();
    EXPECT_TRUE(CountStalePath(rt) == 1);
    ch_2 = Agent::GetInstance()->controller()->
       unicast_cleanup_timer().agent_xmpp_channel_; 
    EXPECT_TRUE(ch != ch_2);
    EXPECT_TRUE(Agent::GetInstance()->controller()->
                unicast_cleanup_timer().cleanup_timer_->running() == true);

    //Delete vm-port and route entry in vrf1
    DeleteVmPortEnvironment();
}

int main(int argc, char **argv) {
    GETSCALEARGS();
    if ((num_vns * num_vms_per_vn) > MAX_INTERFACES) {
        LOG(DEBUG, "Max interfaces is 200");
        return false;
    }
    if (num_ctrl_peers == 0 || num_ctrl_peers > MAX_CONTROL_PEER) {
        LOG(DEBUG, "Supported values - 1, 2");
        return false;
    }

    client = TestInit(init_file, ksync_init);
    Agent::GetInstance()->set_headless_agent_mode(true);

    num_ctrl_peers = 1;
    InitXmppServers();
    int ret = RUN_ALL_TESTS();

    num_ctrl_peers = 2;
    InitXmppServers();
    ret |= RUN_ALL_TESTS();

    Agent::GetInstance()->event_manager()->Shutdown();
    AsioStop();
    TaskScheduler::GetInstance()->Terminate();
    return ret;
}
