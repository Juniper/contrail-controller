/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/logging.h"
#include "base/contrail_ports.h"
#include <sandesh/sandesh.h>
#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh_trace.h>
#include "cmn/agent_cmn.h"
#include "xmpp/xmpp_init.h"
#include "pugixml/pugixml.hpp"
#include "controller/controller_types.h"
#include "controller/controller_init.h"
#include "controller/controller_peer.h"
#include "controller/controller_ifmap.h"
#include "controller/controller_dns.h"
#include "bind/bind_resolver.h"

using namespace boost::asio;

SandeshTraceBufferPtr ControllerTraceBuf(SandeshTraceBufferCreate(
    "Controller", 1000));

void VNController::XmppServerConnect() {

    uint8_t count = 0;
    while (count < MAX_XMPP_SERVERS) {
        if (!Agent::GetXmppServer(count).empty()) {

            AgentXmppChannel *ch = Agent::GetAgentXmppChannel(count);
            if (ch) {
                // Channel is created, do not disturb
                CONTROLLER_TRACE(DiscoveryConnection, "XMPP Server",
                                 ch->GetXmppServer(), 
                                 "is already present, ignore discovery response");
                count++;
                continue; 
            }

            XmppInit *xmpp = new XmppInit();
            XmppClient *client = new XmppClient(Agent::GetEventManager());
            XmppChannelConfig *xmpp_cfg = new XmppChannelConfig(true);
            xmpp_cfg->ToAddr = XmppInit::kControlNodeJID;
            boost::system::error_code ec;
            xmpp_cfg->FromAddr = Agent::GetHostName();
            xmpp_cfg->NodeAddr = XmppInit::kPubSubNS; 
            xmpp_cfg->endpoint.address(
                ip::address::from_string(Agent::GetXmppServer(count), ec));
            assert(ec.value() == 0);
            uint32_t port = Agent::GetXmppPort(count);
            if (!port) {
                port = XMPP_SERVER_PORT;
            }
            xmpp_cfg->endpoint.port(port);
            xmpp->AddXmppChannelConfig(xmpp_cfg);
            xmpp->InitClient(client);

            XmppChannel *channel = client->FindChannel(XmppInit::kControlNodeJID);
            assert(channel);

            Agent::SetAgentMcastLabelRange(count);
            // create bgp peer
            AgentXmppChannel *bgp_peer = new AgentXmppChannel(channel, 
                                             Agent::GetXmppServer(count), 
                                             Agent::GetAgentMcastLabelRange(count),
                                             count);
            client->RegisterConnectionEvent(xmps::BGP,
                boost::bind(&AgentXmppChannel::HandleXmppClientChannelEvent, 
                            bgp_peer, _2));

            // create ifmap peer
            AgentIfMapXmppChannel *ifmap_peer = new AgentIfMapXmppChannel(channel, count);

            Agent::SetAgentXmppChannel(bgp_peer, count);
            Agent::SetAgentIfMapXmppChannel(ifmap_peer, count);
            Agent::SetAgentXmppClient(client, count);
            Agent::SetAgentXmppInit(xmpp, count);
        }
        count++;
    }
}

void VNController::DnsXmppServerConnect() {

    uint8_t count = 0;
    while (count < MAX_XMPP_SERVERS) {
        if (!Agent::GetDnsXmppServer(count).empty()) {

            AgentDnsXmppChannel *ch = Agent::GetAgentDnsXmppChannel(count);
            if (ch) {
                // Channel is up and running, do not disturb
                CONTROLLER_TRACE(DiscoveryConnection, "DNS Server",
                                 ch->GetXmppServer(), 
                                 "is already present, ignore discovery response");
                count++;
                continue; 
            }

            // create Xmpp channel with DNS server
            XmppInit *xmpp_dns = new XmppInit();
            XmppClient *client_dns = new XmppClient(Agent::GetEventManager());
            XmppChannelConfig xmpp_cfg_dns(true);
            xmpp_cfg_dns.ToAddr = XmppInit::kDnsNodeJID;
            boost::system::error_code ec;
            xmpp_cfg_dns.FromAddr = Agent::GetHostName() + "/dns";
            xmpp_cfg_dns.NodeAddr = "";
            xmpp_cfg_dns.endpoint.address(
                     ip::address::from_string(Agent::GetDnsXmppServer(count), ec));
            assert(ec.value() == 0);
            xmpp_cfg_dns.endpoint.port(ContrailPorts::DnsXmpp);
            xmpp_dns->AddXmppChannelConfig(&xmpp_cfg_dns);
            xmpp_dns->InitClient(client_dns);

            XmppChannel *channel_dns = client_dns->FindChannel(
                                                   XmppInit::kDnsNodeJID);
            assert(channel_dns);

            // create dns peer
            AgentDnsXmppChannel *dns_peer = new AgentDnsXmppChannel(channel_dns, 
                                                Agent::GetDnsXmppServer(count), count);
            client_dns->RegisterConnectionEvent(xmps::DNS,
                boost::bind(&AgentDnsXmppChannel::HandleXmppClientChannelEvent, 
                            dns_peer, _2));
            Agent::SetAgentDnsXmppClient(client_dns, count);
            Agent::SetAgentDnsXmppChannel(dns_peer, count);
            Agent::SetAgentDnsXmppInit(xmpp_dns, count);
            BindResolver::Resolver()->SetupResolver(
                Agent::GetDnsXmppServer(count), count);
        }
        count++;
    }
}

void VNController::Connect() {

    /* Connect to Control-Node Xmpp Server */
    VNController::XmppServerConnect();

    /* Connect to DNS Xmpp Server */
    VNController::DnsXmppServerConnect();

    /* Inits */
    Agent::SetControlNodeMulticastBuilder(NULL);
    AgentIfMapVmExport::Init();
}

void VNController::DisConnect() {
    uint8_t count = 0;
    XmppClient *cl;
    while (count < MAX_XMPP_SERVERS) {
        if ((cl = Agent::GetAgentXmppClient(count)) != NULL) {

            //shutdown triggers cleanup of routes learnt from
            //the control-node. 
            cl->Shutdown();

            Agent::ResetAgentMcastLabelRange(count);
            delete Agent::GetAgentXmppChannel(count);
            Agent::SetAgentXmppChannel(NULL, count);

            delete Agent::GetAgentIfMapXmppChannel(count);
            Agent::SetAgentIfMapXmppChannel(NULL, count);

            Agent::SetAgentXmppClient(NULL, count);

            delete Agent::GetAgentXmppInit(count);
            Agent::SetAgentXmppInit(NULL, count);
        }
        if ((cl = Agent::GetAgentDnsXmppClient(count)) != NULL) {
            cl->Shutdown();
            delete Agent::GetAgentDnsXmppChannel(count);
            delete Agent::GetAgentDnsXmppInit(count);
            Agent::SetAgentDnsXmppChannel(NULL, count);
            Agent::SetAgentDnsXmppClient(NULL, count);
            Agent::SetAgentDnsXmppInit(NULL, count);
            Agent::SetDnsXmppPort(0, count); 
            Agent::SetXmppDnsCfgServer(-1);
        }
        count++;
    }

    Agent::SetControlNodeMulticastBuilder(NULL);
    AgentIfMapVmExport::Shutdown();
}


AgentXmppChannel *VNController::FindAgentXmppChannel(std::string server_ip) {

    uint8_t count = 0;
    while (count < MAX_XMPP_SERVERS) {
        AgentXmppChannel *ch = Agent::GetAgentXmppChannel(count);
        if (ch && (ch->GetXmppServer().compare(server_ip) == 0)) {
            return ch; 
        }
        count++;
    }

    return NULL;
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
                                 chnl->GetXmppServer(), "is UP and runnning, ignore");
                continue;
            } else { 
                CONTROLLER_TRACE(DiscoveryConnection, "XMPP Server",
                                 chnl->GetXmppServer(), "is NOT_READY, ignore");
                continue;
            } 

        } else { 
            if (Agent::GetXmppServer(0).empty()) {
                Agent::SetXmppServer(dr.ep.address().to_string(), 0);
                Agent::SetXmppPort(dr.ep.port(), 0);
                CONTROLLER_TRACE(DiscoveryConnection, "Set Xmpp Channel[0] = ", 
                                 dr.ep.address().to_string(), ""); 
            } else if (Agent::GetXmppServer(1).empty()) {
                Agent::SetXmppServer(dr.ep.address().to_string(), 1);
                Agent::SetXmppPort(dr.ep.port(), 1);
                CONTROLLER_TRACE(DiscoveryConnection, "Set Xmpp Channel[1] = ", 
                                 dr.ep.address().to_string(), ""); 
            } else if (Agent::GetAgentXmppChannel(0)->GetXmppChannel()->GetPeerState() 
                       == xmps::NOT_READY) {

                //cleanup older xmpp channel
                Agent::ResetAgentMcastLabelRange(0);
                delete Agent::GetAgentXmppChannel(0);
                Agent::SetAgentXmppChannel(NULL, 0);
                delete Agent::GetAgentIfMapXmppChannel(0);
                Agent::SetAgentIfMapXmppChannel(NULL, 0);
                Agent::SetAgentXmppClient(NULL, 0);
                delete Agent::GetAgentXmppInit(0);
                Agent::SetAgentXmppInit(NULL, 0);

                CONTROLLER_TRACE(DiscoveryConnection, 
                                "Refresh Xmpp Channel[0] = ", dr.ep.address().to_string(), ""); 
                Agent::Agent::SetXmppServer(dr.ep.address().to_string(),0);
                Agent::SetXmppPort(dr.ep.port(), 0);

            } else if (Agent::GetAgentXmppChannel(1)->GetXmppChannel()->GetPeerState() 
                       == xmps::NOT_READY) {

                //cleanup older xmpp channel
                Agent::ResetAgentMcastLabelRange(1);
                delete Agent::GetAgentXmppChannel(1);
                Agent::SetAgentXmppChannel(NULL, 1);
                delete Agent::GetAgentIfMapXmppChannel(1);
                Agent::SetAgentIfMapXmppChannel(NULL, 1);
                Agent::SetAgentXmppClient(NULL, 1);
                delete Agent::GetAgentXmppInit(1);
                Agent::SetAgentXmppInit(NULL, 1);

                CONTROLLER_TRACE(DiscoveryConnection, 
                                 "Refresh Xmpp Channel[1] = ", dr.ep.address().to_string(), ""); 
                Agent::SetXmppServer(dr.ep.address().to_string(), 1);
                Agent::SetXmppPort(dr.ep.port(), 1);
           }
        }
    }

    VNController::XmppServerConnect();
}

AgentDnsXmppChannel *VNController::FindAgentDnsXmppChannel(std::string server_ip) {

    uint8_t count = 0;
    while (count < MAX_XMPP_SERVERS) {
        AgentDnsXmppChannel *ch = Agent::GetAgentDnsXmppChannel(count);
        if (ch && (ch->GetXmppServer().compare(server_ip) == 0)) {
            return ch; 
        }
        count++;
    }

    return NULL;
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
                                 chnl->GetXmppServer(), "is UP and runnning, ignore");
                continue;
            } else { 
                CONTROLLER_TRACE(DiscoveryConnection, "DNS Server",
                                 chnl->GetXmppServer(), "is NOT_READY, ignore");
                continue;
            } 

        } else { 
            if (Agent::GetDnsXmppServer(0).empty()) {
                Agent::SetDnsXmppServer(dr.ep.address().to_string(), 0);
                Agent::SetDnsXmppPort(dr.ep.port(), 0);
                CONTROLLER_TRACE(DiscoveryConnection, "Set Dns Xmpp Channel[0] = ", 
                                 dr.ep.address().to_string(), ""); 
            } else if (Agent::GetDnsXmppServer(1).empty()) {
                Agent::SetDnsXmppServer(dr.ep.address().to_string(), 1);
                Agent::SetDnsXmppPort(dr.ep.port(), 1);
                CONTROLLER_TRACE(DiscoveryConnection, "Set Dns Xmpp Channel[1] = ", 
                                 dr.ep.address().to_string(), ""); 
            } else if (Agent::GetAgentDnsXmppChannel(0)->GetXmppChannel()->GetPeerState() 
                       == xmps::NOT_READY) {

                //cleanup older xmpp channel
                delete Agent::GetAgentDnsXmppChannel(0);
                delete Agent::GetAgentDnsXmppInit(0);
                Agent::SetAgentDnsXmppChannel(NULL, 0);
                Agent::SetAgentDnsXmppClient(NULL, 0);
                Agent::SetAgentDnsXmppInit(NULL, 0);

                CONTROLLER_TRACE(DiscoveryConnection,   
                                "Refresh Dns Xmpp Channel[0] = ", 
                                 dr.ep.address().to_string(), ""); 
                Agent::SetDnsXmppServer(dr.ep.address().to_string(), 0);
                Agent::SetDnsXmppPort(dr.ep.port(), 0);

            } else if (Agent::GetAgentDnsXmppChannel(1)->GetXmppChannel()->GetPeerState() 
                       == xmps::NOT_READY) {

                //cleanup older xmpp channel
                delete Agent::GetAgentDnsXmppChannel(1);
                delete Agent::GetAgentDnsXmppInit(1);
                Agent::SetAgentDnsXmppChannel(NULL, 1);
                Agent::SetAgentDnsXmppClient(NULL, 1);
                Agent::SetAgentDnsXmppInit(NULL, 1);

                CONTROLLER_TRACE(DiscoveryConnection, 
                                 "Refresh Dns Xmpp Channel[1] = ", 
                                 dr.ep.address().to_string(), ""); 
                Agent::SetDnsXmppServer(dr.ep.address().to_string(), 1);
                Agent::SetDnsXmppPort(dr.ep.port(), 1);
           }
        }
    } 

    VNController::DnsXmppServerConnect();
}
