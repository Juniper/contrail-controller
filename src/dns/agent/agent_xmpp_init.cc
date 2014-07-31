/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/logging.h"
#include "base/contrail_ports.h"
#include "xmpp/xmpp_init.h"
#include "pugixml/pugixml.hpp"
#include "cmn/dns.h"
#include "agent/agent_xmpp_init.h"
#include "agent/agent_xmpp_channel.h"

using namespace boost::asio;

void DnsAgentXmppManager::Init() {
    uint32_t port = Dns::GetXmppServerPort();
    if (!port)
        port = ContrailPorts::DnsXmpp();

    XmppInit *init = new XmppInit();
    XmppServer *server = new XmppServer(Dns::GetEventManager());
    XmppChannelConfig xmpp_cfg(false);
    xmpp_cfg.FromAddr = XmppInit::kDnsNodeJID;
    xmpp_cfg.endpoint.port(port);
    init->AddXmppChannelConfig(&xmpp_cfg);
    init->InitServer(server, port, false);
    Dns::SetXmppServer(server);

    DnsAgentXmppChannelManager *agent_xmpp_mgr = 
                        new DnsAgentXmppChannelManager(server);
    Dns::SetAgentXmppChannelManager(agent_xmpp_mgr);
}

void DnsAgentXmppManager::Shutdown() {
    XmppServer *server;
    if ((server = Dns::GetXmppServer()) != NULL) {
        Dns::SetXmppServer(NULL);
        server->Shutdown();
        delete server;
    }
}
