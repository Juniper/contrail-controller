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
        if (!Agent::GetInstance()->GetXmppServer(count).empty()) {

            AgentXmppChannel *ch = Agent::GetInstance()->GetAgentXmppChannel(count);
            if (ch) {
                // Channel is created, do not disturb
                CONTROLLER_TRACE(DiscoveryConnection, "XMPP Server",
                                 ch->GetXmppServer(), 
                                 "is already present, ignore discovery response");
                count++;
                continue; 
            }

            XmppInit *xmpp = new XmppInit();
            XmppClient *client = new XmppClient(Agent::GetInstance()->GetEventManager());
            XmppChannelConfig *xmpp_cfg = new XmppChannelConfig(true);
            xmpp_cfg->ToAddr = XmppInit::kControlNodeJID;
            boost::system::error_code ec;
            xmpp_cfg->FromAddr = Agent::GetInstance()->GetHostName();
            xmpp_cfg->NodeAddr = XmppInit::kPubSubNS; 
            xmpp_cfg->endpoint.address(
                ip::address::from_string(Agent::GetInstance()->GetXmppServer(count), ec));
            assert(ec.value() == 0);
            uint32_t port = Agent::GetInstance()->GetXmppPort(count);
            if (!port) {
                port = XMPP_SERVER_PORT;
            }
            xmpp_cfg->endpoint.port(port);
            xmpp->AddXmppChannelConfig(xmpp_cfg);
            xmpp->InitClient(client);

            XmppChannel *channel = client->FindChannel(XmppInit::kControlNodeJID);
            assert(channel);

            Agent::GetInstance()->SetAgentMcastLabelRange(count);
            // create bgp peer
            AgentXmppChannel *bgp_peer = new AgentXmppChannel(channel, 
                                             Agent::GetInstance()->GetXmppServer(count), 
                                             Agent::GetInstance()->GetAgentMcastLabelRange(count),
                                             count);
            client->RegisterConnectionEvent(xmps::BGP,
                boost::bind(&AgentXmppChannel::HandleXmppClientChannelEvent, 
                            bgp_peer, _2));

            // create ifmap peer
            AgentIfMapXmppChannel *ifmap_peer = new AgentIfMapXmppChannel(channel, count);

            Agent::GetInstance()->SetAgentXmppChannel(bgp_peer, count);
            Agent::GetInstance()->SetAgentIfMapXmppChannel(ifmap_peer, count);
            Agent::GetInstance()->SetAgentXmppClient(client, count);
            Agent::GetInstance()->SetAgentXmppInit(xmpp, count);
        }
        count++;
    }
}

void VNController::DnsXmppServerConnect() {

    uint8_t count = 0;
    while (count < MAX_XMPP_SERVERS) {
        if (!Agent::GetInstance()->GetDnsXmppServer(count).empty()) {

            AgentDnsXmppChannel *ch = Agent::GetInstance()->GetAgentDnsXmppChannel(count);
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
            XmppClient *client_dns = new XmppClient(Agent::GetInstance()->GetEventManager());
            XmppChannelConfig xmpp_cfg_dns(true);
            xmpp_cfg_dns.ToAddr = XmppInit::kDnsNodeJID;
            boost::system::error_code ec;
            xmpp_cfg_dns.FromAddr = Agent::GetInstance()->GetHostName() + "/dns";
            xmpp_cfg_dns.NodeAddr = "";
            xmpp_cfg_dns.endpoint.address(
                     ip::address::from_string(Agent::GetInstance()->GetDnsXmppServer(count), ec));
            assert(ec.value() == 0);
            xmpp_cfg_dns.endpoint.port(ContrailPorts::DnsXmpp);
            xmpp_dns->AddXmppChannelConfig(&xmpp_cfg_dns);
            xmpp_dns->InitClient(client_dns);

            XmppChannel *channel_dns = client_dns->FindChannel(
                                                   XmppInit::kDnsNodeJID);
            assert(channel_dns);

            // create dns peer
            AgentDnsXmppChannel *dns_peer = new AgentDnsXmppChannel(channel_dns, 
                                                Agent::GetInstance()->GetDnsXmppServer(count), count);
            client_dns->RegisterConnectionEvent(xmps::DNS,
                boost::bind(&AgentDnsXmppChannel::HandleXmppClientChannelEvent, 
                            dns_peer, _2));
            Agent::GetInstance()->SetAgentDnsXmppClient(client_dns, count);
            Agent::GetInstance()->SetAgentDnsXmppChannel(dns_peer, count);
            Agent::GetInstance()->SetAgentDnsXmppInit(xmpp_dns, count);
            BindResolver::Resolver()->SetupResolver(
                Agent::GetInstance()->GetDnsXmppServer(count), count);
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
    Agent::GetInstance()->SetControlNodeMulticastBuilder(NULL);
    AgentIfMapVmExport::Init();
}

void VNController::DisConnect() {
    uint8_t count = 0;
    XmppClient *cl;
    while (count < MAX_XMPP_SERVERS) {
        if ((cl = Agent::GetInstance()->GetAgentXmppClient(count)) != NULL) {

            //shutdown triggers cleanup of routes learnt from
            //the control-node. 
            cl->Shutdown();

            Agent::GetInstance()->ResetAgentMcastLabelRange(count);
            delete Agent::GetInstance()->GetAgentXmppChannel(count);
            Agent::GetInstance()->SetAgentXmppChannel(NULL, count);

            delete Agent::GetInstance()->GetAgentIfMapXmppChannel(count);
            Agent::GetInstance()->SetAgentIfMapXmppChannel(NULL, count);

            Agent::GetInstance()->SetAgentXmppClient(NULL, count);

            delete Agent::GetInstance()->GetAgentXmppInit(count);
            Agent::GetInstance()->SetAgentXmppInit(NULL, count);
        }
        if ((cl = Agent::GetInstance()->GetAgentDnsXmppClient(count)) != NULL) {
            cl->Shutdown();
            delete Agent::GetInstance()->GetAgentDnsXmppChannel(count);
            delete Agent::GetInstance()->GetAgentDnsXmppInit(count);
            Agent::GetInstance()->SetAgentDnsXmppChannel(NULL, count);
            Agent::GetInstance()->SetAgentDnsXmppClient(NULL, count);
            Agent::GetInstance()->SetAgentDnsXmppInit(NULL, count);
            Agent::GetInstance()->SetDnsXmppPort(0, count); 
            Agent::GetInstance()->SetXmppDnsCfgServer(-1);
        }
        count++;
    }

    Agent::GetInstance()->SetControlNodeMulticastBuilder(NULL);
    AgentIfMapVmExport::Shutdown();
}


AgentXmppChannel *VNController::FindAgentXmppChannel(std::string server_ip) {

    uint8_t count = 0;
    while (count < MAX_XMPP_SERVERS) {
        AgentXmppChannel *ch = Agent::GetInstance()->GetAgentXmppChannel(count);
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
            if (Agent::GetInstance()->GetXmppServer(0).empty()) {
                Agent::GetInstance()->SetXmppServer(dr.ep.address().to_string(), 0);
                Agent::GetInstance()->SetXmppPort(dr.ep.port(), 0);
                CONTROLLER_TRACE(DiscoveryConnection, "Set Xmpp Channel[0] = ", 
                                 dr.ep.address().to_string(), ""); 
            } else if (Agent::GetInstance()->GetXmppServer(1).empty()) {
                Agent::GetInstance()->SetXmppServer(dr.ep.address().to_string(), 1);
                Agent::GetInstance()->SetXmppPort(dr.ep.port(), 1);
                CONTROLLER_TRACE(DiscoveryConnection, "Set Xmpp Channel[1] = ", 
                                 dr.ep.address().to_string(), ""); 
            } else if (Agent::GetInstance()->GetAgentXmppChannel(0)->GetXmppChannel()->GetPeerState() 
                       == xmps::NOT_READY) {

                //cleanup older xmpp channel
                Agent::GetInstance()->ResetAgentMcastLabelRange(0);
                delete Agent::GetInstance()->GetAgentXmppChannel(0);
                Agent::GetInstance()->SetAgentXmppChannel(NULL, 0);
                delete Agent::GetInstance()->GetAgentIfMapXmppChannel(0);
                Agent::GetInstance()->SetAgentIfMapXmppChannel(NULL, 0);
                Agent::GetInstance()->SetAgentXmppClient(NULL, 0);
                delete Agent::GetInstance()->GetAgentXmppInit(0);
                Agent::GetInstance()->SetAgentXmppInit(NULL, 0);

                CONTROLLER_TRACE(DiscoveryConnection, 
                                "Refresh Xmpp Channel[0] = ", dr.ep.address().to_string(), ""); 
                Agent::GetInstance()->Agent::GetInstance()->SetXmppServer(dr.ep.address().to_string(),0);
                Agent::GetInstance()->SetXmppPort(dr.ep.port(), 0);

            } else if (Agent::GetInstance()->GetAgentXmppChannel(1)->GetXmppChannel()->GetPeerState() 
                       == xmps::NOT_READY) {

                //cleanup older xmpp channel
                Agent::GetInstance()->ResetAgentMcastLabelRange(1);
                delete Agent::GetInstance()->GetAgentXmppChannel(1);
                Agent::GetInstance()->SetAgentXmppChannel(NULL, 1);
                delete Agent::GetInstance()->GetAgentIfMapXmppChannel(1);
                Agent::GetInstance()->SetAgentIfMapXmppChannel(NULL, 1);
                Agent::GetInstance()->SetAgentXmppClient(NULL, 1);
                delete Agent::GetInstance()->GetAgentXmppInit(1);
                Agent::GetInstance()->SetAgentXmppInit(NULL, 1);

                CONTROLLER_TRACE(DiscoveryConnection, 
                                 "Refresh Xmpp Channel[1] = ", dr.ep.address().to_string(), ""); 
                Agent::GetInstance()->SetXmppServer(dr.ep.address().to_string(), 1);
                Agent::GetInstance()->SetXmppPort(dr.ep.port(), 1);
           }
        }
    }

    VNController::XmppServerConnect();
}

AgentDnsXmppChannel *VNController::FindAgentDnsXmppChannel(std::string server_ip) {

    uint8_t count = 0;
    while (count < MAX_XMPP_SERVERS) {
        AgentDnsXmppChannel *ch = Agent::GetInstance()->GetAgentDnsXmppChannel(count);
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
            if (Agent::GetInstance()->GetDnsXmppServer(0).empty()) {
                Agent::GetInstance()->SetDnsXmppServer(dr.ep.address().to_string(), 0);
                Agent::GetInstance()->SetDnsXmppPort(dr.ep.port(), 0);
                CONTROLLER_TRACE(DiscoveryConnection, "Set Dns Xmpp Channel[0] = ", 
                                 dr.ep.address().to_string(), ""); 
            } else if (Agent::GetInstance()->GetDnsXmppServer(1).empty()) {
                Agent::GetInstance()->SetDnsXmppServer(dr.ep.address().to_string(), 1);
                Agent::GetInstance()->SetDnsXmppPort(dr.ep.port(), 1);
                CONTROLLER_TRACE(DiscoveryConnection, "Set Dns Xmpp Channel[1] = ", 
                                 dr.ep.address().to_string(), ""); 
            } else if (Agent::GetInstance()->GetAgentDnsXmppChannel(0)->GetXmppChannel()->GetPeerState() 
                       == xmps::NOT_READY) {

                //cleanup older xmpp channel
                delete Agent::GetInstance()->GetAgentDnsXmppChannel(0);
                delete Agent::GetInstance()->GetAgentDnsXmppInit(0);
                Agent::GetInstance()->SetAgentDnsXmppChannel(NULL, 0);
                Agent::GetInstance()->SetAgentDnsXmppClient(NULL, 0);
                Agent::GetInstance()->SetAgentDnsXmppInit(NULL, 0);

                CONTROLLER_TRACE(DiscoveryConnection,   
                                "Refresh Dns Xmpp Channel[0] = ", 
                                 dr.ep.address().to_string(), ""); 
                Agent::GetInstance()->SetDnsXmppServer(dr.ep.address().to_string(), 0);
                Agent::GetInstance()->SetDnsXmppPort(dr.ep.port(), 0);

            } else if (Agent::GetInstance()->GetAgentDnsXmppChannel(1)->GetXmppChannel()->GetPeerState() 
                       == xmps::NOT_READY) {

                //cleanup older xmpp channel
                delete Agent::GetInstance()->GetAgentDnsXmppChannel(1);
                delete Agent::GetInstance()->GetAgentDnsXmppInit(1);
                Agent::GetInstance()->SetAgentDnsXmppChannel(NULL, 1);
                Agent::GetInstance()->SetAgentDnsXmppClient(NULL, 1);
                Agent::GetInstance()->SetAgentDnsXmppInit(NULL, 1);

                CONTROLLER_TRACE(DiscoveryConnection, 
                                 "Refresh Dns Xmpp Channel[1] = ", 
                                 dr.ep.address().to_string(), ""); 
                Agent::GetInstance()->SetDnsXmppServer(dr.ep.address().to_string(), 1);
                Agent::GetInstance()->SetDnsXmppPort(dr.ep.port(), 1);
           }
        }
    } 

    VNController::DnsXmppServerConnect();
}
