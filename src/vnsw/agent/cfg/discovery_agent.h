/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __VNSW_DISCOVERY_AGENT_HPP__
#define __VNSW_DISCOVERY_AGENT_HPP__

#include <discovery/client/discovery_client.h>

class AgentParam;
class AgentConfig;

class DiscoveryAgentClient {
public:
    DiscoveryAgentClient(AgentConfig *cfg) : agent_cfg_(cfg) { }
    ~DiscoveryAgentClient() { }

    void Init(AgentParam *param);
    void Shutdown();
    void DiscoverServices();
    void DiscoverController(); //subscribe to XMPP server on controller
    void DiscoverDNS();        //subscribe to DN server

    // subscribe callbacks
    void DiscoverySubscribeXmppHandler(std::vector<DSResponse> resp);
    void DiscoverySubscribeDNSHandler(std::vector<DSResponse> resp);
private:
    AgentConfig *agent_cfg_;
    AgentParam *param_;
    DISALLOW_COPY_AND_ASSIGN(DiscoveryAgentClient);
};

#endif
