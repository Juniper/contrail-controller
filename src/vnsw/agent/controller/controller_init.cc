/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <algorithm>
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
#include "oper/global_qos_config.h"
#include "oper/vrf.h"
#include "oper/peer.h"
#include "oper/mirror_table.h"
#include "oper/multicast.h"
#include "controller/controller_types.h"
#include "controller/controller_init.h"
#include "controller/controller_timer.h"
#include "controller/controller_peer.h"
#include "controller/controller_ifmap.h"
#include "controller/controller_dns.h"
#include "controller/controller_export.h"

using namespace boost::asio;

SandeshTraceBufferPtr ControllerConnectionsTraceBuf(SandeshTraceBufferCreate(
    "ControllerConnections", 5000));
SandeshTraceBufferPtr ControllerInfoTraceBuf(SandeshTraceBufferCreate(
    "ControllerInfo", 5000));
SandeshTraceBufferPtr ControllerTxConfigTraceBuf1(SandeshTraceBufferCreate(
    "ControllerTxConfig_1", 5000));
SandeshTraceBufferPtr ControllerTxConfigTraceBuf2(SandeshTraceBufferCreate(
    "ControllerTxConfig_2", 5000));
SandeshTraceBufferPtr ControllerRouteWalkerTraceBuf(SandeshTraceBufferCreate(
    "ControllerRouteWalker", 5000));
SandeshTraceBufferPtr ControllerTraceBuf(SandeshTraceBufferCreate(
    "Controller", 5000));
SandeshTraceBufferPtr ControllerRxRouteMessageTraceBuf1(SandeshTraceBufferCreate(
    "ControllerRxRouteXmppMessage1", 5000));
SandeshTraceBufferPtr ControllerRxConfigMessageTraceBuf1(SandeshTraceBufferCreate(
    "ControllerRxConfigXmppMessage1", 5000));
SandeshTraceBufferPtr ControllerRxRouteMessageTraceBuf2(SandeshTraceBufferCreate(
    "ControllerRxRouteXmppMessage2", 5000));
SandeshTraceBufferPtr ControllerRxConfigMessageTraceBuf2(SandeshTraceBufferCreate(
    "ControllerRxConfigXmppMessage2", 5000));
SandeshTraceBufferPtr ControllerTxMessageTraceBuf1(SandeshTraceBufferCreate(
    "ControllerTxXmppMessage_1", 5000));
SandeshTraceBufferPtr ControllerTxMessageTraceBuf2(SandeshTraceBufferCreate(
    "ControllerTxXmppMessage_2", 5000));

ControllerReConfigData::ControllerReConfigData(std::string service_name,
                                               std::vector<string>server_list) :
    ControllerWorkQueueData(), service_name_(service_name),
    server_list_(server_list) {
}

ControllerDelPeerData::ControllerDelPeerData(AgentXmppChannel *channel) :
    ControllerWorkQueueData(), channel_(channel) {
}

VNController::VNController(Agent *agent) 
    : agent_(agent), multicast_sequence_number_(0),
    work_queue_(agent->task_scheduler()->GetTaskId("Agent::ControllerXmpp"), 0,
                boost::bind(&VNController::ControllerWorkQueueProcess, this,
                            _1)),
    fabric_multicast_label_range_(), xmpp_channel_down_cb_(),
    disconnect_(false) {
    work_queue_.set_name("Controller Queue");
    for (uint8_t count = 0; count < MAX_XMPP_SERVERS; count++) {
        timed_out_channels_[count].clear();
    }
    delpeer_walks_.clear();
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
    if (agent_->vrouter_max_labels() <=  MIN_UNICAST_LABEL_RANGE) {
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

void VNController::SetDscpConfig(XmppChannelConfig *xmpp_cfg) const {
    GlobalQosConfig* qos = NULL;
    if (agent_->oper_db()) {
        qos = agent_->oper_db()->global_qos_config();
    }
    if (qos && qos->control_dscp() != GlobalQosConfig::kInvalidDscp) {
        xmpp_cfg->dscp_value = qos->control_dscp();
    } else {
        xmpp_cfg->dscp_value = 0;
    }
}

void VNController::XmppServerConnect() {

    uint8_t count = 0;

    while (count < MAX_XMPP_SERVERS) {
        SetAgentMcastLabelRange(count);
        if (!agent_->controller_ifmap_xmpp_server(count).empty()) {

            AgentXmppChannel *ch = agent_->controller_xmpp_channel(count);
            if (ch) {
                // Channel is created, do not disturb
                CONTROLLER_CONNECTIONS_TRACE(DiscoveryConnection, 
                    "XMPP Server is already present, ignore reconfig response",
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
            SetDscpConfig(xmpp_cfg);

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
            channel->RegisterRxMessageTraceCallback(
                             boost::bind(&VNController::RxXmppMessageTrace,
                                         this, bgp_peer->GetXmppServerIdx(),
                                         _1, _2, _3, _4, _5));
            channel->RegisterTxMessageTraceCallback(
                             boost::bind(&VNController::TxXmppMessageTrace,
                                         this, bgp_peer->GetXmppServerIdx(),
                                         _1, _2, _3, _4, _5));
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
                CONTROLLER_CONNECTIONS_TRACE(DiscoveryConnection,
                    "DNS Server is already present, ignore reconfig response",
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
            if (agent_->xmpp_dns_test_mode()) {
                xmpp_cfg_dns->endpoint.port(agent_->dns_server_port(count));
            } else {
                xmpp_cfg_dns->endpoint.port(ContrailPorts::DnsXmpp());
            }
            xmpp_cfg_dns->auth_enabled = agent_->dns_auth_enabled();
            if (xmpp_cfg_dns->auth_enabled) {
                xmpp_cfg_dns->path_to_server_cert = agent_->xmpp_server_cert();
                xmpp_cfg_dns->path_to_server_priv_key =  agent_->xmpp_server_key();
                xmpp_cfg_dns->path_to_ca_cert =  agent_->xmpp_ca_cert();
            }
            SetDscpConfig(xmpp_cfg_dns);

            // Create Xmpp Client
            XmppClient *client_dns = new XmppClient(agent_->event_manager(),
                                                    xmpp_cfg_dns);

            XmppInit *xmpp_dns = new XmppInit();
            // create dns peer
            AgentDnsXmppChannel *dns_peer = new AgentDnsXmppChannel(agent_,
                                                agent_->dns_server(count),
                                                count);
            client_dns->RegisterConnectionEvent(xmps::DNS,
                boost::bind(&AgentDnsXmppChannel::XmppClientChannelEvent,
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
        }
        count++;
    }
}

void VNController::Connect() {
    /* Connect to Control-Node Xmpp Server */
    controller_list_chksum_ = Agent::GetInstance()->GetControllerlistChksum();
    XmppServerConnect();

    /* Connect to DNS Xmpp Server */
    dns_list_chksum_ = Agent::GetInstance()->GetDnslistChksum();
    DnsXmppServerConnect();

    /* Inits */
    agent_->controller()->increment_multicast_sequence_number();
    agent_->set_cn_mcast_builder(NULL);
    agent_ifmap_vm_export_.reset(new AgentIfMapVmExport(agent_));
}

// Disconnect on agent shutdown.
void VNController::XmppServerDisConnect() {
    XmppClient *cl;
    uint8_t count = 0;
    disconnect_ = true;
    while (count < MAX_XMPP_SERVERS) {
        if ((cl = agent_->controller_ifmap_xmpp_client(count)) != NULL) {
            cl->Shutdown();
        }
        // Delpeer walk for channel slot = count
        StartDelPeerWalk(agent_->controller_xmpp_channel_ref(count));
        // Delpeer walk done for timedout channels which used to represent this
        // slot.
        FlushTimedOutChannels(count);
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

void VNController::StartDelPeerWalk(AgentXmppChannelPtr ptr) {
    if (!ptr.get())
        return;

    ptr.get()->bgp_peer_id()->DelPeerRoutes(
        boost::bind(&VNController::DelPeerWalkDone, this, ptr.get()),
        ptr.get()->sequence_number());
    delpeer_walks_.push_back(ptr);
}

//During delete of xmpp channel, check if BGP peer is deleted.
//If not agent never got a channel down state and is being removed
//as it is not part of discovery list.
//Artificially inject NOT_READY in agent xmpp channel.
void VNController::DeleteAgentXmppChannel(uint8_t idx) {
    AgentXmppChannel *channel = agent_->controller_xmpp_channel(idx);
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
    //Every delete of channel should delete flow of bgp-as-a-service,
    //which is using this CN.
    if (xmpp_channel_down_cb_.empty() == false) {
        xmpp_channel_down_cb_(idx);
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

void VNController::DelPeerWalkDone(AgentXmppChannel* ptr) {
    ControllerDelPeerDataType data(new ControllerDelPeerData(ptr));
    ControllerWorkQueueDataType base_data =
        boost::static_pointer_cast<ControllerWorkQueueData>(data);
    work_queue_.Enqueue(base_data);
}

void VNController::DelPeerWalkDoneProcess(AgentXmppChannel *channel) {
    DynamicPeer::ProcessDelete(channel->bgp_peer_id());
    for (AgentXmppChannelListIter it = delpeer_walks_.begin();
         it != delpeer_walks_.end(); it++) {
        if ((*it).get() == channel) {
            delpeer_walks_.erase(it);
            break;
        }
    }
    //delete channel;
    if (disconnect_ & delpeer_walks_.empty())
        Cleanup();
}

void VNController::FlushTimedOutChannels(uint8_t idx) {
    for (AgentXmppChannelListIter it = timed_out_channels_[idx].begin();
         it != timed_out_channels_[idx].end(); it++) {
        StartDelPeerWalk((*it));
    }
    timed_out_channels_[idx].clear();
}

void VNController::DisConnectControllerIfmapServer(uint8_t idx) {

    // Managed Delete of XmppClient object, which deletes the
    // dependent XmppClientConnection object and
    // scoped XmppChannel object
    XmppClient *xc = agent_->controller_ifmap_xmpp_client(idx);
    //In case of UT xc can be NULL
    if (!agent_->test_mode() || xc) {
        xc->UnRegisterConnectionEvent(xmps::BGP);
        xc->Shutdown(); // ManagedDelete
    }
    agent_->set_controller_ifmap_xmpp_client(NULL, idx);

    //cleanup AgentXmppChannel
    DeleteAgentXmppChannel(idx);
    //Trigger removal from service inuse list for discovery
    //cleanup AgentIfmapXmppChannel
    timed_out_channels_[idx].push_back(agent_->controller_xmpp_channel_ref(idx));
    agent_->controller_xmpp_channel(idx)->UpdateConnectionInfo(xmps::TIMEDOUT);
    agent_->reset_controller_xmpp_channel(idx);

    delete agent_->ifmap_xmpp_channel(idx);
    agent_->set_ifmap_xmpp_channel(NULL, idx);

    if (!agent_->test_mode() || xc) {
        agent_->controller_ifmap_xmpp_init(idx)->Reset();
        delete agent_->controller_ifmap_xmpp_init(idx);
        agent_->set_controller_ifmap_xmpp_init(NULL, idx);
        DeleteConnectionInfo(agent_->controller_ifmap_xmpp_server(idx), false);
    }

    agent_->reset_controller_ifmap_xmpp_server(idx);
}

bool VNController::AgentReConfigXmppServerConnectedExists(
                                 const std::string &server_ip,
                                 std::vector<std::string> resp) {

    std::vector<std::string>::iterator iter;
    int8_t count = -1;
    int8_t min_iter = std::min(static_cast<int>(resp.size()), MAX_XMPP_SERVERS);
    for (iter = resp.begin(); ++count < min_iter; iter++) {
        std::vector<string> servers;
        boost::split(servers, *iter, boost::is_any_of(":"));
        if (servers[0].compare(server_ip) == 0) {
            return true;
        }
    }
    return false;
}


void VNController::ReConnectXmppServer() {

    std::vector<string> controller_list =
        Agent::GetInstance()->GetControllerlist();

    ControllerReConfigDataType data(new ControllerReConfigData(
        g_vns_constants.XMPP_SERVER_DISCOVERY_SERVICE_NAME,
        controller_list));
    ControllerWorkQueueDataType base_data =
        boost::static_pointer_cast<ControllerWorkQueueData>(data);
    work_queue_.Enqueue(base_data);
}

void VNController::ReConnectDnsServer() {

    std::vector<string> dns_list =
        Agent::GetInstance()->GetDnslist();

    ControllerReConfigDataType data(new ControllerReConfigData(
        g_vns_constants.DNS_SERVER_DISCOVERY_SERVICE_NAME,
        dns_list));
    ControllerWorkQueueDataType base_data =
        boost::static_pointer_cast<ControllerWorkQueueData>(data);
    work_queue_.Enqueue(base_data);
}

void VNController::ReConnect() {

    if (controller_list_chksum_ !=
        Agent::GetInstance()->GetControllerlistChksum()) {

        controller_list_chksum_ =
            Agent::GetInstance()->GetControllerlistChksum();

        ReConnectXmppServer();
    }

    if (dns_list_chksum_ !=
        Agent::GetInstance()->GetDnslistChksum()) {

        dns_list_chksum_ =
            Agent::GetInstance()->GetDnslistChksum();

        ReConnectDnsServer();
    }
}

bool VNController::ApplyControllerReConfigInternal(std::vector<string> resp) {
    std::vector<string>::iterator iter;
    int8_t count = -1;

    /* Apply only MAX_XMPP_SERVERS from list as the list is ordered */
    int8_t min_iter = std::min(static_cast<int>(resp.size()), MAX_XMPP_SERVERS);
    for (iter = resp.begin(); ++count < min_iter; iter++) {
        std::vector<string> srv;
        boost::split(srv, *iter, boost::is_any_of(":"));
        std::string server_ip = srv[0];
        std::string server_port = srv[1];
        uint32_t port;
        port = strtoul(srv[1].c_str(), NULL, 0);

        CONTROLLER_CONNECTIONS_TRACE(DiscoveryConnection, "XMPP ReConfig Apply Server Ip",
            count, server_ip, server_port);

        AgentXmppChannel *chnl = FindAgentXmppChannel(server_ip);
        if (chnl) {
            if (chnl->GetXmppChannel() &&
                chnl->GetXmppChannel()->GetPeerState() == xmps::READY) {
                CONTROLLER_CONNECTIONS_TRACE(DiscoveryConnection,
                    " XMPP ReConfig Server is READY and running, ignore", count,
                    chnl->GetXmppServer(), "");
                continue;
            } else {
                CONTROLLER_CONNECTIONS_TRACE(DiscoveryConnection,
                    " XMPP ReConfig Server is NOT_READY, ignore", count,
                    chnl->GetXmppServer(), "");
                continue;
            }

        } else {

            for (uint8_t xs_idx = 0; xs_idx < MAX_XMPP_SERVERS; xs_idx++) {

                if (agent_->controller_ifmap_xmpp_server(xs_idx).empty()) {

                    CONTROLLER_CONNECTIONS_TRACE(DiscoveryConnection,
                        "Set Xmpp ReConfig Channel",
                        xs_idx, server_ip, server_port);

                    agent_->set_controller_ifmap_xmpp_server(
                        server_ip, xs_idx);

                    agent_->set_controller_ifmap_xmpp_port(port, xs_idx);
                    break;

                } else if (agent_->controller_xmpp_channel(xs_idx)) {

                    if (AgentReConfigXmppServerConnectedExists(
                        agent_->controller_ifmap_xmpp_server(xs_idx), resp)) {

                        CONTROLLER_CONNECTIONS_TRACE(DiscoveryConnection,
                            "Retain Xmpp ReConfig Channel ", xs_idx,
                             agent_->controller_ifmap_xmpp_server(xs_idx), "");
                        continue;
                    }

                    CONTROLLER_CONNECTIONS_TRACE(DiscoveryConnection,
                        "ReSet Xmpp ReConfig Channel ", xs_idx,
                        agent_->controller_ifmap_xmpp_server(xs_idx),
                        server_ip);

                    DisConnectControllerIfmapServer(xs_idx);
                    agent_->set_controller_ifmap_xmpp_server(
                         server_ip, xs_idx);
                    agent_->set_controller_ifmap_xmpp_port(
                        port, xs_idx);
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

bool VNController::ApplyDnsReConfigInternal(std::vector<string> resp) {
    std::vector<string>::iterator iter;
    int8_t count = -1;

    /* Apply only MAX_XMPP_SERVERS from list as the list is ordered */
    int8_t min_iter = std::min(static_cast<int>(resp.size()), MAX_XMPP_SERVERS);
    for (iter = resp.begin(); ++count < min_iter; iter++) {
        std::vector<string> srv;
        boost::split(srv, *iter, boost::is_any_of(":"));
        std::string server_ip = srv[0];
        std::string server_port = srv[1];
        uint32_t port;
        port = strtoul(srv[1].c_str(), NULL, 0);

        CONTROLLER_CONNECTIONS_TRACE(DiscoveryConnection,
            "DNS Server ReConfig Apply Server Ip",
            count, server_ip, server_port);

        AgentDnsXmppChannel *chnl = FindAgentDnsXmppChannel(server_ip);
        if (chnl) {
            if (chnl->GetXmppChannel() &&
                chnl->GetXmppChannel()->GetPeerState() == xmps::READY) {
                CONTROLLER_CONNECTIONS_TRACE(DiscoveryConnection,
                    "DNS Server is READY and running, ignore", count,
                    chnl->GetXmppServer(), "");
                continue;
            } else {
                CONTROLLER_CONNECTIONS_TRACE(DiscoveryConnection,
                    "DNS Server is NOT_READY, ignore", count,
                    chnl->GetXmppServer(), "");
                continue;
            }
        } else {

            for (uint8_t xs_idx = 0; xs_idx < MAX_XMPP_SERVERS; xs_idx++) {

                if (agent_->dns_server(xs_idx).empty()) {

                    CONTROLLER_CONNECTIONS_TRACE(DiscoveryConnection,
                        "Set Dns Xmpp Channel ",
                        xs_idx, server_ip, server_port);

                    agent_->set_dns_server(server_ip, xs_idx);
                    agent_->set_dns_server_port(port, xs_idx);
                    break;

                } else if (agent_->dns_xmpp_channel(xs_idx)) {

                    if (AgentReConfigXmppServerConnectedExists(
                        agent_->dns_server(xs_idx), resp)) {

                        CONTROLLER_CONNECTIONS_TRACE(DiscoveryConnection,
                            "Retain Dns Xmpp Channel ", xs_idx,
                            agent_->dns_server(xs_idx), "");
                        continue;
                    }

                    CONTROLLER_CONNECTIONS_TRACE(DiscoveryConnection,
                        "ReSet Dns ReConfigChannel ", xs_idx,
                        agent_->dns_server(xs_idx), server_ip);

                    DisConnectDnsServer(xs_idx);
                    agent_->set_dns_server(server_ip, xs_idx);
                    agent_->set_dns_server_port(port, xs_idx);
                    break;
               }
           }
        }
    }

    DnsXmppServerConnect();
    return true;
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

void VNController::StartEndOfRibTxTimer() {
    uint8_t count = 0;
    while (count < MAX_XMPP_SERVERS) {
        if (agent_->controller_xmpp_channel(count))
            agent_->controller_xmpp_channel(count)->end_of_rib_tx_timer()->
                Start(agent_->controller_xmpp_channel(count));
        count++;
    }
}

void VNController::StopEndOfRibTx() {
    uint8_t count = 0;
    while (count < MAX_XMPP_SERVERS) {
        if (agent_->controller_xmpp_channel(count)) {
            agent_->controller_xmpp_channel(count)->
                end_of_rib_tx_timer()->Cancel();
            agent_->controller_xmpp_channel(count)->StopEndOfRibTxWalker();
        }
        count++;
    }
}

bool VNController::ControllerWorkQueueProcess(ControllerWorkQueueDataType data) {

    //DOM processing
    ControllerXmppDataType derived_xmpp_data =
        boost::dynamic_pointer_cast<ControllerXmppData>(data);
    if (derived_xmpp_data) {
        return XmppMessageProcess(derived_xmpp_data);
    }

    // VM Subscription message
    ControllerVmiSubscribeData *subscribe_data =
        boost::dynamic_pointer_cast<ControllerVmiSubscribeData>(data.get());
    if (subscribe_data && agent_ifmap_vm_export_.get()) {
        agent_ifmap_vm_export_->VmiEvent(subscribe_data);
    }

    //ReConfig
    ControllerReConfigDataType reconfig_data =
        boost::dynamic_pointer_cast<ControllerReConfigData>(data);
    if (reconfig_data) {
        if (reconfig_data->service_name_.compare(
            g_vns_constants.XMPP_SERVER_DISCOVERY_SERVICE_NAME) == 0) {
            return ApplyControllerReConfigInternal(reconfig_data->server_list_);
        } else if (reconfig_data->service_name_.compare(
            g_vns_constants.DNS_SERVER_DISCOVERY_SERVICE_NAME) == 0) {
            return ApplyDnsReConfigInternal(reconfig_data->server_list_);
        } else {
            LOG(ERROR, "Unknown Service Name %s" << reconfig_data->service_name_);
        }
    }

    ControllerDelPeerDataType del_peer_data =
        boost::dynamic_pointer_cast<ControllerDelPeerData>(data);
    if (del_peer_data) {
        DelPeerWalkDoneProcess(del_peer_data.get()->channel());
    }

    AgentIfMapXmppChannel::EndOfConfigDataPtr end_of_config_data =
        boost::dynamic_pointer_cast<EndOfConfigData>(data);
    if (end_of_config_data &&
        (agent_->ifmap_xmpp_channel(agent_->ifmap_active_xmpp_server_index()) ==
         end_of_config_data->channel())) {
        end_of_config_data->channel()->ProcessEndOfConfig();
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
        if (data->config()) {
            if (peer) {
                peer->ReceiveDnsMessage(data->dom());
            }
        } else {
            if (peer) {
                AgentDnsXmppChannel::HandleXmppClientChannelEvent(peer,
                                                                  data->peer_state());
            }
        }
    }

    return true;
}

void VNController::Enqueue(ControllerWorkQueueDataType data) {
    work_queue_.Enqueue(data);
}

bool VNController::RxXmppMessageTrace(uint8_t peer_index,
                                      const std::string &to_address,
                                      int port, int size,
                                      const std::string &msg,
                                      const XmppStanza::XmppMessage *xmppmsg) {
    const std::string &to = xmppmsg->to;
    if (to.find(XmppInit::kBgpPeer) != string::npos) {
        CONTROLLER_RX_ROUTE_MESSAGE_TRACE(Message, peer_index, to_address,
                                           port, size, msg);
        return true;
    } else if (to.find(XmppInit::kConfigPeer) != string::npos) {
        CONTROLLER_RX_CONFIG_MESSAGE_TRACE(Message, peer_index, to_address,
                                           port, size, msg);
        return true;
    }
    return false;
}

bool VNController::TxXmppMessageTrace(uint8_t peer_index,
                                      const std::string &to_address,
                                      int port, int size,
                                      const std::string &msg,
                                      const XmppStanza::XmppMessage *xmppmsg) {
    CONTROLLER_TX_MESSAGE_TRACE(Message, peer_index, to_address,
                                port, size, msg);
    return true;
}

bool VNController::IsWorkQueueEmpty() const {
    return (work_queue_.IsQueueEmpty() == 0);
}
