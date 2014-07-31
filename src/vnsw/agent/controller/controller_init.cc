/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/logging.h"
#include "base/timer.h"
#include "base/contrail_ports.h"
#include "base/connection_info.h"
#include <sandesh/sandesh.h>
#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh_trace.h>
#include "cmn/agent_cmn.h"
#include "xmpp/xmpp_init.h"
#include "pugixml/pugixml.hpp"
#include "oper/vrf.h"
#include "oper/peer.h"
#include "oper/mirror_table.h"
#include "oper/multicast.h"
#include "controller/controller_types.h"
#include "controller/controller_init.h"
#include "controller/controller_cleanup_timer.h"
#include "controller/controller_peer.h"
#include "controller/controller_ifmap.h"
#include "controller/controller_dns.h"
#include "controller/controller_export.h"
#include "bind/bind_resolver.h"

using namespace boost::asio;

SandeshTraceBufferPtr ControllerTraceBuf(SandeshTraceBufferCreate(
    "Controller", 1000));

VNController::VNController(Agent *agent) 
    : agent_(agent), multicast_sequence_number_(0),
    unicast_cleanup_timer_(agent), multicast_cleanup_timer_(agent), 
    config_cleanup_timer_(agent) {
        decommissioned_peer_list_.clear();
}

VNController::~VNController() {
}

void VNController::XmppServerConnect() {

    uint8_t count = 0;

    while (count < MAX_XMPP_SERVERS) {
        if (!agent_->controller_ifmap_xmpp_server(count).empty()) {

            AgentXmppChannel *ch = agent_->controller_xmpp_channel(count);
            if (ch) {
                // Channel is created, do not disturb
                CONTROLLER_TRACE(DiscoveryConnection, "XMPP Server",
                                 ch->controller_ifmap_xmpp_server(), 
                                 "is already present, ignore discovery response");
                count++;
                continue; 
            }

            XmppInit *xmpp = new XmppInit();
            XmppClient *client = new XmppClient(agent_->event_manager());
            agent_->SetAgentMcastLabelRange(count);
            // create bgp peer
            AgentXmppChannel *bgp_peer = new AgentXmppChannel(agent_,
                              agent_->controller_ifmap_xmpp_server(count),
                              agent_->multicast_label_range(count),
                              count);
            client->RegisterConnectionEvent(xmps::BGP,
               boost::bind(&AgentXmppChannel::HandleAgentXmppClientChannelEvent,
                           bgp_peer, _2));

            XmppChannelConfig *xmpp_cfg = new XmppChannelConfig(true);
            xmpp_cfg->ToAddr = XmppInit::kControlNodeJID;
            boost::system::error_code ec;
            xmpp_cfg->FromAddr = agent_->host_name();
            xmpp_cfg->NodeAddr = XmppInit::kPubSubNS; 
            xmpp_cfg->endpoint.address(
                ip::address::from_string(agent_->controller_ifmap_xmpp_server(count), ec));
            assert(ec.value() == 0);
            uint32_t port = agent_->controller_ifmap_xmpp_port(count);
            if (!port) {
                port = XMPP_SERVER_PORT;
            }
            xmpp_cfg->endpoint.port(port);
            xmpp->AddXmppChannelConfig(xmpp_cfg);
            xmpp->InitClient(client);

            XmppChannel *channel = client->
                FindChannel(XmppInit::kControlNodeJID);
            assert(channel);
            bgp_peer->RegisterXmppChannel(channel);

            bgp_peer->UpdateConnectionInfo(channel->GetPeerState());

            // create ifmap peer
            AgentIfMapXmppChannel *ifmap_peer = 
                new AgentIfMapXmppChannel(agent_, channel, count);

            agent_->set_controller_xmpp_channel(bgp_peer, count);
            agent_->set_ifmap_xmpp_channel(ifmap_peer, count);
            agent_->set_controller_ifmap_xmpp_client(client, count);
            agent_->set_controller_ifmap_xmpp_init(xmpp, count);
        }
        count++;
    }
}

void VNController::DnsXmppServerConnect() {

    if (agent_->GetDnsProto() == NULL) {
        return;
    }

    uint8_t count = 0;
    while (count < MAX_XMPP_SERVERS) {
        if (!agent_->dns_server(count).empty()) {

            AgentDnsXmppChannel *ch = agent_->dns_xmpp_channel(count);
            if (ch) {
                // Channel is up and running, do not disturb
                CONTROLLER_TRACE(DiscoveryConnection, "DNS Server",
                                 ch->controller_ifmap_xmpp_server(), 
                                 "is already present, ignore discovery response");
                count++;
                continue; 
            }

            // create Xmpp channel with DNS server
            XmppInit *xmpp_dns = new XmppInit();
            XmppClient *client_dns = new XmppClient(agent_->event_manager());
            // create dns peer
            AgentDnsXmppChannel *dns_peer = new AgentDnsXmppChannel(agent_,
                                                agent_->dns_server(count),
                                                count);
            client_dns->RegisterConnectionEvent(xmps::DNS,
                boost::bind(&AgentDnsXmppChannel::HandleXmppClientChannelEvent,
                            dns_peer, _2));

            XmppChannelConfig *xmpp_cfg_dns = new XmppChannelConfig(true);
            //XmppChannelConfig xmpp_cfg_dns(true);
            xmpp_cfg_dns->ToAddr = XmppInit::kDnsNodeJID;
            boost::system::error_code ec;
            xmpp_cfg_dns->FromAddr = agent_->host_name() + "/dns";
            xmpp_cfg_dns->NodeAddr = "";
            xmpp_cfg_dns->endpoint.address(
                     ip::address::from_string(agent_->dns_server(count), ec));
            assert(ec.value() == 0);
            xmpp_cfg_dns->endpoint.port(ContrailPorts::DnsXmpp());
            xmpp_dns->AddXmppChannelConfig(xmpp_cfg_dns);
            xmpp_dns->InitClient(client_dns);

            XmppChannel *channel_dns = client_dns->FindChannel(
                                                   XmppInit::kDnsNodeJID);
            assert(channel_dns);
            dns_peer->RegisterXmppChannel(channel_dns);


            dns_peer->UpdateConnectionInfo(channel_dns->GetPeerState());
            agent_->set_dns_xmpp_client(client_dns, count);
            agent_->set_dns_xmpp_channel(dns_peer, count);
            agent_->set_dns_xmpp_init(xmpp_dns, count);
            BindResolver::Resolver()->SetupResolver(
                BindResolver::DnsServer(agent_->dns_server(count),
                                        agent_->dns_server_port(count)),
                count);
        }
        count++;
    }
}

void VNController::Connect() {
    /* Connect to Control-Node Xmpp Server */
    XmppServerConnect();

    /* Connect to DNS Xmpp Server */
    DnsXmppServerConnect();

    /* Inits */
    agent_->controller()->increment_multicast_sequence_number();
    agent_->set_cn_mcast_builder(NULL);
    agent_ifmap_vm_export_.reset(new AgentIfMapVmExport(agent_));
}

void VNController::XmppServerDisConnect() {
    XmppClient *cl;
    uint8_t count = 0;
    while (count < MAX_XMPP_SERVERS) {
        if ((cl = agent_->controller_ifmap_xmpp_client(count)) != NULL) {
            BgpPeer *peer = agent_->controller_xmpp_channel(count)->bgp_peer_id();
            // Sets the context of walk to decide on callback when walks are
            // done, setting to true results in callback of cleanup for
            // VNController once all walks are done for deleting peer info.
            if (peer)
                peer->set_is_disconnect_walk(true);
            //shutdown triggers cleanup of routes learnt from
            //the control-node. 
            cl->Shutdown();
        }
        count ++;
    }
}


void VNController::DnsXmppServerDisConnect() {
    XmppClient *cl;
    uint8_t count = 0;
    while (count < MAX_XMPP_SERVERS) {
        if ((cl = agent_->dns_xmpp_client(count)) != NULL) {
            cl->Shutdown();
        }
        count ++;
    }


}

//Trigger shutdown and cleanup of routes for the client
void VNController::DisConnect() {
    XmppServerDisConnect();
    DnsXmppServerDisConnect();
}

void VNController::Cleanup() {
    uint8_t count = 0;
    XmppClient *cl;
    while (count < MAX_XMPP_SERVERS) {
        if ((cl = agent_->controller_ifmap_xmpp_client(count)) != NULL) {
            DisConnectControllerIfmapServer(count);
        }
        if ((cl = agent_->dns_xmpp_client(count)) != NULL) {
            DisConnectDnsServer(count);
        }
        count++;
    }

    agent_->controller()->increment_multicast_sequence_number();
    agent_->set_cn_mcast_builder(NULL);
    decommissioned_peer_list_.clear();
    agent_ifmap_vm_export_.reset();
}


AgentXmppChannel *VNController::FindAgentXmppChannel(
                                const std::string &server_ip) {

    uint8_t count = 0;
    while (count < MAX_XMPP_SERVERS) {
        AgentXmppChannel *ch = agent_->controller_xmpp_channel(count);
        if (ch && (ch->controller_ifmap_xmpp_server().compare(server_ip) == 0)) {
            return ch; 
        }
        count++;
    }

    return NULL;
}

const string VNController::MakeConnectionPrefix(bool is_dns) const {
    string name_prefix;
    if (is_dns) {
        name_prefix = agent_->xmpp_dns_server_prefix();
    } else {
        name_prefix = agent_->xmpp_control_node_prefix();
    }
    return name_prefix;
}

void VNController::DeleteConnectionInfo(const std::string &addr, bool is_dns)
                                        const {
    const string &name_prefix = MakeConnectionPrefix(is_dns);
    agent_->connection_state()->Delete(process::ConnectionType::XMPP,
                                           name_prefix + addr);
}

void VNController::DisConnectControllerIfmapServer(uint8_t idx) {

	DeleteConnectionInfo(agent_->controller_ifmap_xmpp_server(idx),
			     false);

	// Managed Delete of XmppClient object, which deletes the 
	// dependent XmppClientConnection object and
        // scoped XmppChannel object
	XmppClient *xc = agent_->controller_ifmap_xmpp_client(idx);
	xc->UnRegisterConnectionEvent(xmps::BGP);
	xc->Shutdown(); // ManagedDelete
	agent_->set_controller_ifmap_xmpp_client(NULL, idx);

	//cleanup AgentXmppChannel
	agent_->ResetAgentMcastLabelRange(idx);
	delete agent_->controller_xmpp_channel(idx);
	agent_->set_controller_xmpp_channel(NULL, idx);

	//cleanup AgentIfmapXmppChannel
	delete agent_->ifmap_xmpp_channel(idx);
	agent_->set_ifmap_xmpp_channel(NULL, idx);

	agent_->controller_ifmap_xmpp_init(idx)->Reset();
	delete agent_->controller_ifmap_xmpp_init(idx);
	agent_->set_controller_ifmap_xmpp_init(NULL, idx);

        agent_->reset_controller_ifmap_xmpp_server(idx);
}

void VNController::ApplyDiscoveryXmppServices(std::vector<DSResponse> resp) {

    std::vector<DSResponse>::iterator iter;
    for (iter = resp.begin(); iter != resp.end(); iter++) {
        DSResponse dr = *iter;
        
        CONTROLLER_TRACE(DiscoveryConnection, "XMPP Server", dr.ep.address().to_string(),
                         "Discovery Server Response"); 
        AgentXmppChannel *chnl = FindAgentXmppChannel(dr.ep.address().to_string());
        if (chnl) { 
            if (chnl->GetXmppChannel() &&
                chnl->GetXmppChannel()->GetPeerState() == xmps::READY) {
                CONTROLLER_TRACE(DiscoveryConnection, "XMPP Server",
                                 chnl->controller_ifmap_xmpp_server(), "is UP and running, ignore");
                continue;
            } else { 
                CONTROLLER_TRACE(DiscoveryConnection, "XMPP Server",
                                 chnl->controller_ifmap_xmpp_server(), "is NOT_READY, ignore");
                continue;
            } 

        } else { 
            if (agent_->controller_ifmap_xmpp_server(0).empty()) {
                agent_->set_controller_ifmap_xmpp_server(dr.ep.address().to_string(), 0);
                agent_->set_controller_ifmap_xmpp_port(dr.ep.port(), 0);
                CONTROLLER_TRACE(DiscoveryConnection, "Set Xmpp Channel[0] = ", 
                                 dr.ep.address().to_string(), ""); 
            } else if (agent_->controller_ifmap_xmpp_server(1).empty()) {
                agent_->set_controller_ifmap_xmpp_server(dr.ep.address().to_string(), 1);
                agent_->set_controller_ifmap_xmpp_port(dr.ep.port(), 1);
                CONTROLLER_TRACE(DiscoveryConnection, "Set Xmpp Channel[1] = ", 
                                 dr.ep.address().to_string(), ""); 
            } else if (agent_->controller_xmpp_channel(0)->GetXmppChannel()->
                       GetPeerState() == xmps::NOT_READY) {

                DisConnectControllerIfmapServer(0);

                CONTROLLER_TRACE(DiscoveryConnection, 
                                "Refresh Xmpp Channel[0] = ", dr.ep.address().to_string(), ""); 
                agent_->set_controller_ifmap_xmpp_server(dr.ep.address().to_string(),0);
                agent_->set_controller_ifmap_xmpp_port(dr.ep.port(), 0);

            } else if (agent_->controller_xmpp_channel(1)->GetXmppChannel()->
                       GetPeerState() == xmps::NOT_READY) {

                DisConnectControllerIfmapServer(1);

                CONTROLLER_TRACE(DiscoveryConnection, 
                                 "Refresh Xmpp Channel[1] = ", dr.ep.address().to_string(), ""); 
                agent_->set_controller_ifmap_xmpp_server(dr.ep.address().to_string(), 1);
                agent_->set_controller_ifmap_xmpp_port(dr.ep.port(), 1);
           }
        }
    }

    XmppServerConnect();
}

AgentDnsXmppChannel *VNController::FindAgentDnsXmppChannel(
                                   const std::string &server_ip) {

    uint8_t count = 0;
    while (count < MAX_XMPP_SERVERS) {
        AgentDnsXmppChannel *ch = agent_->dns_xmpp_channel(count);
        if (ch && (ch->controller_ifmap_xmpp_server().compare(server_ip) == 0)) {
            return ch; 
        }
        count++;
    }

    return NULL;
}

void VNController::DisConnectDnsServer(uint8_t idx) {

    DeleteConnectionInfo(agent_->dns_server(0), true);

    // Managed Delete of XmppClient object, which deletes the 
    // dependent XmppClientConnection object and
    // scoped_ptr XmppChannel
    XmppClient *xc = agent_->dns_xmpp_client(idx);
    xc->UnRegisterConnectionEvent(xmps::DNS);
    xc->Shutdown();
    agent_->set_dns_xmpp_client(NULL, idx);

    //cleanup AgentDnsXmppChannel
    delete agent_->dns_xmpp_channel(idx);
    agent_->set_dns_xmpp_channel(NULL, idx);

    agent_->dns_xmpp_init(idx)->Reset();
    delete agent_->dns_xmpp_init(idx);
    agent_->set_dns_xmpp_init(NULL, idx);

    agent_->reset_dns_server(idx);
    agent_->set_dns_xmpp_server_index(-1);
}


void VNController::ApplyDiscoveryDnsXmppServices(std::vector<DSResponse> resp) {

    std::vector<DSResponse>::iterator iter;
    for (iter = resp.begin(); iter != resp.end(); iter++) {
        DSResponse dr = *iter;
        
        CONTROLLER_TRACE(DiscoveryConnection, "DNS Server", dr.ep.address().to_string(),
                         "Discovery Server Response"); 
        AgentDnsXmppChannel *chnl = FindAgentDnsXmppChannel(dr.ep.address().to_string());
        if (chnl) { 
            if (chnl->GetXmppChannel() &&
                chnl->GetXmppChannel()->GetPeerState() == xmps::READY) {
                CONTROLLER_TRACE(DiscoveryConnection, "DNS Server",
                                 chnl->controller_ifmap_xmpp_server(), "is UP and running, ignore");
                continue;
            } else { 
                CONTROLLER_TRACE(DiscoveryConnection, "DNS Server",
                                 chnl->controller_ifmap_xmpp_server(), "is NOT_READY, ignore");
                continue;
            } 

        } else { 
            if (agent_->dns_server(0).empty()) {
                agent_->set_dns_server(dr.ep.address().to_string(), 0);
                agent_->set_dns_server_port(dr.ep.port(), 0);
                CONTROLLER_TRACE(DiscoveryConnection, "Set Dns Xmpp Channel[0] = ", 
                                 dr.ep.address().to_string(), integerToString(dr.ep.port())); 
            } else if (agent_->dns_server(1).empty()) {
                agent_->set_dns_server(dr.ep.address().to_string(), 1);
                agent_->set_dns_server_port(dr.ep.port(), 1);
                CONTROLLER_TRACE(DiscoveryConnection, "Set Dns Xmpp Channel[1] = ", 
                                 dr.ep.address().to_string(), integerToString(dr.ep.port())); 
            } else if (agent_->dns_xmpp_channel(0)->GetXmppChannel()->GetPeerState() 
                       == xmps::NOT_READY) {

                DisConnectDnsServer(0);

                CONTROLLER_TRACE(DiscoveryConnection,   
                                "Refresh Dns Xmpp Channel[0] = ", 
                                 dr.ep.address().to_string(), integerToString(dr.ep.port())); 
                agent_->set_dns_server(dr.ep.address().to_string(), 0);
                agent_->set_dns_server_port(dr.ep.port(), 0);

            } else if (agent_->dns_xmpp_channel(1)->GetXmppChannel()->GetPeerState() 
                       == xmps::NOT_READY) {

                DisConnectDnsServer(1);

                CONTROLLER_TRACE(DiscoveryConnection, 
                                 "Refresh Dns Xmpp Channel[1] = ", 
                                 dr.ep.address().to_string(), integerToString(dr.ep.port())); 
                agent_->set_dns_server(dr.ep.address().to_string(), 1);
                agent_->set_dns_server_port(dr.ep.port(), 1);
           }
        }
    } 

    DnsXmppServerConnect();
}

/*
 * Returns the number of active agentxmppchannel.
 * AgentXmppChannel is identified as active if it has a BGP peer
 * attached to it.
 */
uint8_t VNController::ActiveXmppConnectionCount() {
    uint8_t active_xmpps = 0;
    for (uint8_t count = 0; count < MAX_XMPP_SERVERS; count++) {
        AgentXmppChannel *xc = agent_->controller_xmpp_channel(count);
       if (xc) {
           // Check if AgentXmppChannel has BGP peer
           if (xc->bgp_peer_id() != NULL)
               active_xmpps++;
       }
    }

    return active_xmpps;
}

AgentXmppChannel *VNController::GetActiveXmppChannel() {
    for (uint8_t count = 0; count < MAX_XMPP_SERVERS; count++) {
        AgentXmppChannel *xc = agent_->controller_xmpp_channel(count);
       if (xc) {
           // Check if AgentXmppChannel has BGP peer
           if (xc->bgp_peer_id() != NULL)
               return xc;
       }
    }

    return NULL;
}

void VNController::AddToDecommissionedPeerList(BgpPeerPtr peer) {
    decommissioned_peer_list_.push_back(peer);
}

/*
 * Callback function executed on expiration of unicast stale timer.
 * Goes through decommisoned peer list and removes the peer.
 * This results in zero referencing(shared_ptr) of BgpPeer object and 
 * destruction of same.
 */
void VNController::ControllerPeerHeadlessAgentDelDone(BgpPeer *bgp_peer) {
    // Retain the disconnect state for peer as bgp_peer will be freed
    // below.
    bool is_disconnect_walk = bgp_peer->is_disconnect_walk();
    for (BgpPeerIterator it  = decommissioned_peer_list_.begin(); 
         it != decommissioned_peer_list_.end(); ++it) {
        BgpPeer *peer = static_cast<BgpPeer *>((*it).get());
        if (peer == bgp_peer) {
            //Release BGP peer, ideally this should be the last reference being
            //released for peer.
            decommissioned_peer_list_.remove(*it);
            break;
        }
    }

    // Delete walk for peer was issued via shutdown of agentxmppchannel
    // If all bgp peers are gone(i.e. walk for delpeer for all decommissioned
    // peer is over), go ahead with cleanup.
    if (decommissioned_peer_list_.empty() && is_disconnect_walk) {
        agent()->controller()->Cleanup();
    }
}

/*
 * Callback for unicast timer expiration.
 * Iterates through all decommisioned peers and issues 
 * delete peer walk for each one with peer as self
 */
bool VNController::UnicastCleanupTimerExpired() {
    for (BgpPeerIterator it  = decommissioned_peer_list_.begin(); 
         it != decommissioned_peer_list_.end(); ++it) {
        BgpPeer *bgp_peer = static_cast<BgpPeer *>((*it).get());
        bgp_peer->DelPeerRoutes(
            boost::bind(&VNController::ControllerPeerHeadlessAgentDelDone, 
                        this, bgp_peer));
    }

    return false;
}

void VNController::StartUnicastCleanupTimer(
                               AgentXmppChannel *agent_xmpp_channel) {
    // In non-headless mode trigger cleanup 
    if (!(agent_->headless_agent_mode())) {
        UnicastCleanupTimerExpired();
        return;
    }

    unicast_cleanup_timer_.Start(agent_xmpp_channel);
}

// Multicast info is maintained using sequence number and not peer,
// so on expiration of timer send the sequence number specified at start of
// timer. 
bool VNController::MulticastCleanupTimerExpired(uint64_t peer_sequence) {
    MulticastHandler::GetInstance()->FlushPeerInfo(peer_sequence);
    return false;
}

void VNController::StartMulticastCleanupTimer(
                                 AgentXmppChannel *agent_xmpp_channel) {
    // In non-headless mode trigger cleanup 
    if (!(agent_->headless_agent_mode())) {
        MulticastCleanupTimerExpired(multicast_sequence_number_);
        return;
    }

    // Pass the current peer sequence. In the timer expiration interval 
    // if new peer sends new info sequence number wud have incremented in
    // multicast.
    multicast_cleanup_timer_.peer_sequence_ = agent_->controller()->
        multicast_sequence_number();
    multicast_cleanup_timer_.Start(agent_xmpp_channel);
}

void VNController::StartConfigCleanupTimer(
                              AgentXmppChannel *agent_xmpp_channel) {
        config_cleanup_timer_.Start(agent_xmpp_channel);
}

// Helper to iterate thru all decommisioned peer and delete the vrf state for
// specified vrf entry. Called on per VRF basis.
void VNController::DeleteVrfStateOfDecommisionedPeers(
                                                DBTablePartBase *partition,
                                                DBEntryBase *e) {
    for (BgpPeerIterator it  = decommissioned_peer_list_.begin(); 
         it != decommissioned_peer_list_.end(); 
         ++it) {
        BgpPeer *bgp_peer = static_cast<BgpPeer *>((*it).get());
        bgp_peer->DeleteVrfState(partition, e);
    }
}
