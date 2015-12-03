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

bool DnsAgentXmppManager::Init(bool xmpp_auth_enabled,
                               const std::string &xmpp_server_cert,
                               const std::string &xmpp_server_key,
                               const std::string &xmpp_ca_cert) {
    uint32_t port = Dns::GetXmppServerPort();
    if (!port)
        port = ContrailPorts::DnsXmpp();

    // XmppChannel Configuration
    XmppChannelConfig xmpp_cfg(false);
    xmpp_cfg.FromAddr = XmppInit::kDnsNodeJID;
    xmpp_cfg.endpoint.port(port);
    xmpp_cfg.auth_enabled = xmpp_auth_enabled;
    if (xmpp_cfg.auth_enabled) {
        xmpp_cfg.path_to_server_cert = xmpp_server_cert;
        xmpp_cfg.path_to_server_priv_key = xmpp_server_key;
        xmpp_cfg.path_to_ca_cert = xmpp_ca_cert;
    }

    // Create XmppServer
    XmppServer *server = new XmppServer(Dns::GetEventManager(),
                                        Dns::GetHostName(), &xmpp_cfg);
    if (!server->Initialize(port, false)) {
        return false;
    }
    Dns::SetXmppServer(server);

    DnsAgentXmppChannelManager *agent_xmpp_mgr = 
                        new DnsAgentXmppChannelManager(server);
    Dns::SetAgentXmppChannelManager(agent_xmpp_mgr);
    return true;
}

void DnsAgentXmppManager::Shutdown() {
    XmppServer *server;
    if ((server = Dns::GetXmppServer()) != NULL) {
        Dns::SetXmppServer(NULL);
        server->Shutdown();
        delete server;
    }
}
