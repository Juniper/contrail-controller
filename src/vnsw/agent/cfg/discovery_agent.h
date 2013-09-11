/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __VNSW_DISCOVERY_AGENT_HPP__
#define __VNSW_DISCOVERY_AGENT_HPP__

#include <discovery_client.h>

class DiscoveryAgentClient {
    public:
        static void Init();
        static void Shutdown();
        static void DiscoverServices();
        static void DiscoverController(); //subscribe to XMPP server on controller
        static void DiscoverDNS();        //subscribe to DN server

        // subscribe callbacks
        static void DiscoverySubscribeXmppHandler(std::vector<DSResponse> resp);
        static void DiscoverySubscribeDNSHandler(std::vector<DSResponse> resp);
};

#endif
