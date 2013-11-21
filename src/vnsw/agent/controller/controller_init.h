/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __VNSW_CONTROLLER_INIT_HPP__
#define __VNSW_CONTROLLER_INIT_HPP__

#include <sandesh/sandesh_trace.h>
#include <discovery_client.h>

class AgentXmppChannel;
class AgentDnsXmppChannel;

class VNController {
    public:
        static void Connect();
        static void DisConnect();

        static void Cleanup();

        static void XmppServerConnect();
        static void DnsXmppServerConnect();

        static void XmppServerDisConnect();
        static void DnsXmppServerDisConnect();

        static AgentXmppChannel *FindAgentXmppChannel(std::string server_ip);
        static void ApplyDiscoveryXmppServices(std::vector<DSResponse> resp); 

        static AgentDnsXmppChannel *FindAgentDnsXmppChannel(std::string server_ip);
        static void ApplyDiscoveryDnsXmppServices(std::vector<DSResponse> resp); 
};

extern SandeshTraceBufferPtr ControllerTraceBuf;

#define CONTROLLER_TRACE(obj, ...)\
do {\
    AgentXmpp##obj::TraceMsg(ControllerTraceBuf, __FILE__, __LINE__, __VA_ARGS__);\
} while(0);\

#endif
