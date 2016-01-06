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

SandeshTraceBufferPtr ControllerDiscoveryTraceBuf(SandeshTraceBufferCreate(
    "ControllerDiscovery", 5000));
SandeshTraceBufferPtr ControllerInfoTraceBuf(SandeshTraceBufferCreate(
    "ControllerInfo", 5000));
SandeshTraceBufferPtr ControllerRouteWalkerTraceBuf(SandeshTraceBufferCreate(
    "ControllerRouteWalker", 5000));
SandeshTraceBufferPtr ControllerTraceBuf(SandeshTraceBufferCreate(
    "Controller", 5000));

ControllerDiscoveryData::ControllerDiscoveryData(std::vector<DSResponse> resp) :
    ControllerWorkQueueData(), discovery_response_(resp) {
}

VNController::VNController(Agent *agent) 
    : agent_(agent), multicast_sequence_number_(0),
    unicast_cleanup_timer_(agent), multicast_cleanup_timer_(agent), 
    config_cleanup_timer_(agent),
    work_queue_(TaskScheduler::GetInstance()->GetTaskId("Agent::ControllerXmpp"), 0,
        boost::bind(&VNController::ControllerWorkQueueProcess, this, _1)),
    fabric_multicast_label_range_() {
    decommissioned_peer_list_.clear();
}

VNController::~VNController() {
    work_queue_.Shutdown();
}

void VNController::FillMcastLabelRange(uint32_t *start_idx,
                                       uint32_t *end_idx,
                                       uint8_t idx) const {
    uint32_t max_mc_labels = 2 * (agent_->vrouter_max_vrfs());
    uint32_t mc_label_count = 0;
    uint32_t vrouter_max_labels = agent_->vrouter_max_labels();

    if (max_mc_labels + MIN_UNICAST_LABEL_RANGE < vrouter_max_labels) {
        mc_label_count = agent_->vrouter_max_vrfs();
    } else {
        mc_label_count = (vrouter_max_labels - MIN_UNICAST_LABEL_RANGE)/2;
    }

    *start_idx = vrouter_max_labels - ((idx + 1) * mc_label_count);
    *end_idx = (vrouter_max_labels - ((idx) * mc_label_count) - 1);
}

void VNController::SetAgentMcastLabelRange(uint8_t idx) {
    uint32_t start = 0;
    uint32_t end = 0;
    std::stringstream str;

    //Logic for multicast label allocation
    //  1> Reserve minimum 4k label for unicast
    //  2> In the remaining label space
    //       * Try allocating labels equal to no. of VN
    //         for each control node
    //       * If label space is not huge enough
    //         split remaining unicast label for both control
    //         node
    //  Remaining label would be used for unicast mpls label
    if (agent_->vrouter_max_labels() == 0) {
        str << 0 << "-" << 0;
        fabric_multicast_label_range_[idx].start = 0;
        fabric_multicast_label_range_[idx].end = 0;
        fabric_multicast_label_range_[idx].fabric_multicast_label_range_str =
            str.str();
        return;
    }

    FillMcastLabelRange(&start, &end, idx);
    str << start << "-" << end;

    agent_->mpls_table()->ReserveMulticastLabel(start, end + 1, idx);
    fabric_multicast_label_range_[idx].start = start;
    fabric_multicast_label_range_[idx].end = (end + 1);
    fabric_multicast_label_range_[idx].fabric_multicast_label_range_str =
        str.str();
}

void VNController::XmppServerConnect() {

    uint8_t count = 0;

    while (count < MAX_XMPP_SERVERS) {
        SetAgentMcastLabelRange(count);
        if (!agent_->controller_ifmap_xmpp_server(count).empty()) {

            AgentXmppChannel *ch = agent_->controller_xmpp_channel(count);
            if (ch) {
                // Channel is created, do not disturb
                CONTROLLER_DISCOVERY_TRACE(DiscoveryConnection, 
                    "XMPP Server is already present, ignore discovery response",
                    count, ch->GetXmppServer(), "");
                count++;
                continue; 
            }

            boost::system::error_code ec;
            XmppChannelConfig *xmpp_cfg = new XmppChannelConfig(true);
            xmpp_cfg->ToAddr = XmppInit::kControlNodeJID;
            xmpp_cfg->FromAddr = agent_->agent_name();
            xmpp_cfg->NodeAddr = XmppInit::kPubSubNS;
            xmpp_cfg->endpoint.address(
                ip::address::from_string(agent_->controller_ifmap_xmpp_server(count), ec));
            assert(ec.value() == 0);
            xmpp_cfg->auth_enabled = agent_->xmpp_auth_enabled();
            if (xmpp_cfg->auth_enabled) {
                xmpp_cfg->path_to_server_cert =  agent_->xmpp_server_cert();
                xmpp_cfg->path_to_server_priv_key =  agent_->xmpp_server_key();
                xmpp_cfg->path_to_ca_cert =  agent_->xmpp_ca_cert();
            }
            uint32_t port = agent_->controller_ifmap_xmpp_port(count);
            if (!port) {
                port = XMPP_SERVER_PORT;
            }
            xmpp_cfg->endpoint.port(port);

            // Create Xmpp Client
            XmppClient *client = new XmppClient(agent_->event_manager(), xmpp_cfg);

            XmppInit *xmpp = new XmppInit();
            xmpp->AddXmppChannelConfig(xmpp_cfg);
            // create bgp peer
            AgentXmppChannel *bgp_peer = new AgentXmppChannel(agent_,
                              agent_->controller_ifmap_xmpp_server(count),
                              fabric_multicast_label_range(count).
                                               fabric_multicast_label_range_str,
                              count);
            client->RegisterConnectionEvent(xmps::BGP,
               boost::bind(&AgentXmppChannel::XmppClientChannelEvent,
                           bgp_peer, _2));
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
                CONTROLLER_DISCOVERY_TRACE(DiscoveryConnection,
                    "DNS Server is already present, ignore discovery response",
                    count, ch->GetXmppServer(), "");
                count++;
                continue; 
            }

            // XmppChannel Configuration
            boost::system::error_code ec;
            XmppChannelConfig *xmpp_cfg_dns = new XmppChannelConfig(true);
            xmpp_cfg_dns->ToAddr = XmppInit::kDnsNodeJID;
            xmpp_cfg_dns->FromAddr = agent_->agent_name() + "/dns";
            xmpp_cfg_dns->NodeAddr = "";
            xmpp_cfg_dns->endpoint.address(
                     ip::address::from_string(agent_->dns_server(count), ec));
            assert(ec.value() == 0);
            xmpp_cfg_dns->endpoint.port(ContrailPorts::DnsXmpp());
            xmpp_cfg_dns->auth_enabled = agent_->dns_auth_enabled();
            if (xmpp_cfg_dns->auth_enabled) {
                xmpp_cfg_dns->path_to_server_cert = agent_->xmpp_server_cert();
                xmpp_cfg_dns->path_to_server_priv_key =  agent_->xmpp_server_key();
                xmpp_cfg_dns->path_to_ca_cert =  agent_->xmpp_ca_cert();
            }

            // Create Xmpp Client
            XmppClient *client_dns = new XmppClient(agent_->event_manager(),
                                                    xmpp_cfg_dns);

            XmppInit *xmpp_dns = new XmppInit();
            // create dns peer
            AgentDnsXmppChannel *dns_peer = new AgentDnsXmppChannel(agent_,
                                                agent_->dns_server(count),
                                                count);
            client_dns->RegisterConnectionEvent(xmps::DNS,
                boost::bind(&AgentDnsXmppChannel::HandleXmppClientChannelEvent,
                            dns_peer, _2));

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

//During delete of xmpp channel, check if BGP peer is deleted.
//If not agent never got a channel down state and is being removed
//as it is not part of discovery list.
//Artificially inject NOT_READY in agent xmpp channel.
void VNController::DeleteAgentXmppChannel(AgentXmppChannel *channel) {
    if (!channel)
        return;

    BgpPeer *bgp_peer = channel->bgp_peer_id();
    if (bgp_peer != NULL) {
        //Defer delete of channel till delete walk of bgp peer is over.
        //Till walk is over, unregister as table listener is not done and there
        //may be notifications(which will be ignored though, but valid pointer is
        //needed). BgpPeer destructor will handle deletion of channel.
        AgentXmppChannel::HandleAgentXmppClientChannelEvent(channel,
                                                            xmps::NOT_READY);
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
        if (ch && (ch->GetXmppServer().compare(server_ip) == 0)) {
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

    // Managed Delete of XmppClient object, which deletes the
    // dependent XmppClientConnection object and
    // scoped XmppChannel object
    XmppClient *xc = agent_->controller_ifmap_xmpp_client(idx);
    xc->UnRegisterConnectionEvent(xmps::BGP);
    xc->Shutdown(); // ManagedDelete
    agent_->set_controller_ifmap_xmpp_client(NULL, idx);

    //cleanup AgentXmppChannel
    DeleteAgentXmppChannel(agent_->controller_xmpp_channel(idx));
    agent_->reset_controller_xmpp_channel(idx);

    //cleanup AgentIfmapXmppChannel
    delete agent_->ifmap_xmpp_channel(idx);
    agent_->set_ifmap_xmpp_channel(NULL, idx);

    agent_->controller_ifmap_xmpp_init(idx)->Reset();
    delete agent_->controller_ifmap_xmpp_init(idx);
    agent_->set_controller_ifmap_xmpp_init(NULL, idx);

    DeleteConnectionInfo(agent_->controller_ifmap_xmpp_server(idx), false);
    agent_->reset_controller_ifmap_xmpp_server(idx);
}

bool VNController::AgentXmppServerExists(const std::string &server_ip,
                                         std::vector<DSResponse> resp) {

    std::vector<DSResponse>::iterator iter;
    for (iter = resp.begin(); iter != resp.end(); iter++) {
        DSResponse dr = *iter;
        if (dr.ep.address().to_string().compare(server_ip) == 0) {
            return true;
        }
    }
    return false;
}

void VNController::ApplyDiscoveryXmppServices(std::vector<DSResponse> resp) {
    ControllerDiscoveryDataType data(new ControllerDiscoveryData(resp));
    ControllerWorkQueueDataType base_data =
        boost::static_pointer_cast<ControllerWorkQueueData>(data);
    work_queue_.Enqueue(base_data);
}

bool VNController::ApplyDiscoveryXmppServicesInternal(std::vector<DSResponse> resp) {
    std::vector<DSResponse>::iterator iter;
    int8_t count = -1;
    agent_->UpdateDiscoveryServerResponseList(resp);
    for (iter = resp.begin(); iter != resp.end(); iter++) {
        DSResponse dr = *iter;
        count ++;

        CONTROLLER_DISCOVERY_TRACE(DiscoveryConnection, "XMPP Discovery Server Response",
            count, dr.ep.address().to_string(), integerToString(dr.ep.port()));

        AgentXmppChannel *chnl = FindAgentXmppChannel(dr.ep.address().to_string());
        if (chnl) { 
            if (chnl->GetXmppChannel() &&
                chnl->GetXmppChannel()->GetPeerState() == xmps::READY) {
                CONTROLLER_DISCOVERY_TRACE(DiscoveryConnection, 
                    "XMPP Server is READY and running, ignore", count,
                    chnl->GetXmppServer(), "");
                continue;
            } else { 
                CONTROLLER_DISCOVERY_TRACE(DiscoveryConnection, 
                    "XMPP Server is NOT_READY, ignore", count,
                    chnl->GetXmppServer(), "");
                continue;
            }

        } else { 

            for (uint8_t xs_idx = 0; xs_idx < MAX_XMPP_SERVERS; xs_idx++) {

                if (agent_->controller_ifmap_xmpp_server(xs_idx).empty()) {

                    CONTROLLER_DISCOVERY_TRACE(DiscoveryConnection,
                                               "Set Xmpp Channel",
                        xs_idx, dr.ep.address().to_string(), 
                        integerToString(dr.ep.port())); 

                    agent_->set_controller_ifmap_xmpp_server(
                        dr.ep.address().to_string(), xs_idx);
                    agent_->set_controller_ifmap_xmpp_port(dr.ep.port(), xs_idx);
                    break; 

                } else if (agent_->controller_xmpp_channel(xs_idx)) {

                    if (AgentXmppServerExists(
                        agent_->controller_ifmap_xmpp_server(xs_idx), resp)) {

                        CONTROLLER_DISCOVERY_TRACE(DiscoveryConnection,
                            "Retain Xmpp Channel ", xs_idx,
                             agent_->controller_ifmap_xmpp_server(xs_idx), "");
                        continue;
                    }

                    CONTROLLER_DISCOVERY_TRACE(DiscoveryConnection, 
                        "ReSet Xmpp Channel ", xs_idx, 
                        agent_->controller_ifmap_xmpp_server(xs_idx),
                        dr.ep.address().to_string());

                    DisConnectControllerIfmapServer(xs_idx);
                    agent_->set_controller_ifmap_xmpp_server(
                         dr.ep.address().to_string(),xs_idx);
                    agent_->set_controller_ifmap_xmpp_port(dr.ep.port(), xs_idx);
                    break;
                }
            }
        }
    }

    XmppServerConnect();
    return true;
}

AgentDnsXmppChannel *VNController::FindAgentDnsXmppChannel(
                                   const std::string &server_ip) {

    uint8_t count = 0;
    while (count < MAX_XMPP_SERVERS) {
        AgentDnsXmppChannel *ch = agent_->dns_xmpp_channel(count);
        if (ch && (ch->GetXmppServer().compare(server_ip) == 0)) {
            return ch; 
        }
        count++;
    }

    return NULL;
}

void VNController::DisConnectDnsServer(uint8_t idx) {

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

    DeleteConnectionInfo(agent_->dns_server(idx), true);
    agent_->reset_dns_server(idx);
}


void VNController::ApplyDiscoveryDnsXmppServices(std::vector<DSResponse> resp) {

    std::vector<DSResponse>::iterator iter;
    int8_t count = -1;
    agent_->UpdateDiscoveryDnsServerResponseList(resp);
    for (iter = resp.begin(); iter != resp.end(); iter++) {
        DSResponse dr = *iter;
        count++;

        CONTROLLER_DISCOVERY_TRACE(DiscoveryConnection,
                                   "DNS Discovery Server Response", count,
            dr.ep.address().to_string(), integerToString(dr.ep.port()));

        AgentDnsXmppChannel *chnl = FindAgentDnsXmppChannel(dr.ep.address().to_string());
        if (chnl) { 
            if (chnl->GetXmppChannel() &&
                chnl->GetXmppChannel()->GetPeerState() == xmps::READY) {
                CONTROLLER_DISCOVERY_TRACE(DiscoveryConnection, 
                    "DNS Server is READY and running, ignore", count,
                    chnl->GetXmppServer(), "");
                continue;
            } else { 
                CONTROLLER_DISCOVERY_TRACE(DiscoveryConnection, 
                    "DNS Server is NOT_READY, ignore", count,
                    chnl->GetXmppServer(), "");
                continue;
            } 

        } else { 

            for (uint8_t xs_idx = 0; xs_idx < MAX_XMPP_SERVERS; xs_idx++) {

                if (agent_->dns_server(xs_idx).empty()) {

                    CONTROLLER_DISCOVERY_TRACE(DiscoveryConnection, 
                        "Set Dns Xmpp Channel ", xs_idx,
                        dr.ep.address().to_string(), integerToString(dr.ep.port()));

                    agent_->set_dns_server(dr.ep.address().to_string(), xs_idx);
                    agent_->set_dns_server_port(dr.ep.port(), xs_idx);
                    break;
            
                } else if (agent_->dns_xmpp_channel(xs_idx)) {

                    if (AgentXmppServerExists(
                        agent_->dns_server(xs_idx), resp)) {

                        CONTROLLER_DISCOVERY_TRACE(DiscoveryConnection,
                            "Retain Dns Xmpp Channel ", xs_idx,
                            agent_->dns_server(xs_idx), "");
                        continue;
                    }

                    CONTROLLER_DISCOVERY_TRACE(DiscoveryConnection,   
                        "ReSet Dns Xmpp Channel ", xs_idx,
                        agent_->dns_server(xs_idx),
                        dr.ep.address().to_string());

                    DisConnectDnsServer(xs_idx);
                    agent_->set_dns_server(dr.ep.address().to_string(), xs_idx);
                    agent_->set_dns_server_port(dr.ep.port(), xs_idx);
                    break;
               }
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

void VNController::ControllerPeerHeadlessAgentDelDoneEnqueue(BgpPeer *bgp_peer) {
    ControllerDeletePeerDataType data(new ControllerDeletePeerData(bgp_peer));
    ControllerWorkQueueDataType base_data =
        boost::static_pointer_cast<ControllerWorkQueueData>(data);
    work_queue_.Enqueue(base_data);
}

/*
 * Callback function executed on expiration of unicast stale timer.
 * Goes through decommisoned peer list and removes the peer.
 * This results in zero referencing(shared_ptr) of BgpPeer object and 
 * destruction of same.
 */
bool VNController::ControllerPeerHeadlessAgentDelDone(BgpPeer *bgp_peer) {
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
    return true;
}

/*
 * Callback for unicast timer expiration.
 * Iterates through all decommisioned peers and issues 
 * delete peer walk for each one with peer as self
 */
void VNController::UnicastCleanupTimerExpired() {
    for (BgpPeerIterator it  = decommissioned_peer_list_.begin();
         it != decommissioned_peer_list_.end(); ++it) {
        BgpPeer *bgp_peer = static_cast<BgpPeer *>((*it).get());
        bgp_peer->DelPeerRoutes(
            boost::bind(&VNController::ControllerPeerHeadlessAgentDelDoneEnqueue,
                        this, bgp_peer));
    }
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
void VNController::MulticastCleanupTimerExpired(uint64_t peer_sequence) {
    MulticastHandler::GetInstance()->FlushPeerInfo(peer_sequence);
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

bool VNController::ControllerWorkQueueProcess(ControllerWorkQueueDataType data) {

    //DOM processing
    ControllerXmppDataType derived_xmpp_data =
        boost::dynamic_pointer_cast<ControllerXmppData>(data);
    if (derived_xmpp_data) {
        return XmppMessageProcess(derived_xmpp_data);
    }
    //Walk done processing
    ControllerDeletePeerDataType derived_walk_done_data =
        boost::dynamic_pointer_cast<ControllerDeletePeerData>(data);
    if (derived_walk_done_data) {
        return ControllerPeerHeadlessAgentDelDone(derived_walk_done_data->
                                                  bgp_peer());
    }
    //Discovery response for servers
    ControllerDiscoveryDataType discovery_data =
        boost::dynamic_pointer_cast<ControllerDiscoveryData>(data);
    if (discovery_data) {
        return ApplyDiscoveryXmppServicesInternal(discovery_data->
                                                  discovery_response_);
    }
    return true;
}

bool VNController::XmppMessageProcess(ControllerXmppDataType data) {
    if (data->peer_id() == xmps::BGP) {
        if (data->config()) {
            AgentXmppChannel *peer =
                agent_->controller_xmpp_channel(data->channel_id());
            if (peer) {
                peer->ReceiveBgpMessage(data->dom());
            }
        } else {
            AgentXmppChannel *peer =
                agent_->controller_xmpp_channel(data->channel_id());
            if (peer) {
                AgentXmppChannel::HandleAgentXmppClientChannelEvent(peer,
                                                                    data->peer_state());
            }
        }
    } else if (data->peer_id() == xmps::CONFIG) {
        AgentIfMapXmppChannel *peer =
            agent_->ifmap_xmpp_channel(data->channel_id());
        if (peer) {
            peer->ReceiveConfigMessage(data->dom());
        }
    } else if (data->peer_id() == xmps::DNS) {
        AgentDnsXmppChannel *peer =
            agent_->dns_xmpp_channel(data->channel_id());
        if (peer) {
            AgentDnsXmppChannel::HandleXmppClientChannelEvent(peer,
                                                              data->peer_state());
        }
    }

    return true;
}

void VNController::Enqueue(ControllerWorkQueueDataType data) {
    work_queue_.Enqueue(data);
}
