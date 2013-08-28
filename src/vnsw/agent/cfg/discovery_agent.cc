/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/logging.h"
#include "cmn/agent_cmn.h"
#include "discovery_agent.h"
#include "controller/controller_init.h"
#include "cmn/agent_cmn.h"
#include <sandesh/sandesh.h>

using namespace boost::asio;

void DiscoveryAgentClient::Init() {

    if (!Agent::GetDiscoveryServer().empty()) {
        boost::system::error_code ec;
        ip::tcp::endpoint dss_ep;
        dss_ep.address(ip::address::from_string(Agent::GetDiscoveryServer(), ec));
        uint32_t port = Agent::GetDiscoveryServerPort();
        if (!port) {
            port = DISCOVERY_SERVER_PORT;
        }
        dss_ep.port(port);
 
        DiscoveryServiceClient *ds_client = 
            (new DiscoveryServiceClient(Agent::GetEventManager(), dss_ep));
        ds_client->Init();

        Agent::SetDiscoveryServiceClient(ds_client);

    }
}

void DiscoveryAgentClient::DiscoverController() {
    
    DiscoveryServiceClient *ds_client = Agent::GetDiscoveryServiceClient();
    if (ds_client) {

        int xs_instances = Agent::GetDiscoveryXmppServerInstances();
        if (xs_instances < 0) {
            xs_instances = 2;
        }

        std::string subscriber_name = g_vns_constants.ModuleNames.find(Module::VROUTER_AGENT)->second;
        ds_client->Subscribe(subscriber_name, DiscoveryServiceClient::XmppService, xs_instances,
            boost::bind(&DiscoveryAgentClient::DiscoverySubscribeXmppHandler, _1)); 
    }    
}

void DiscoveryAgentClient::DiscoverCollector() {
    
    DiscoveryServiceClient *ds_client = Agent::GetDiscoveryServiceClient();
    if (ds_client) {

        int collector_instances = 1;
        std::string service_name = g_vns_constants.ModuleNames.find(Module::COLLECTOR)->second;
        std::string subscriber_name = g_vns_constants.ModuleNames.find(Module::VROUTER_AGENT)->second;
        ds_client->Subscribe(subscriber_name, service_name, collector_instances,
            boost::bind(&DiscoveryAgentClient::DiscoverySubscribeCollectorHandler, _1)); 
    }    
}

void DiscoveryAgentClient::DiscoverDNS() {
    
    DiscoveryServiceClient *ds_client = Agent::GetDiscoveryServiceClient();
    if (ds_client) {

        int dns_instances = 2;
        std::string subscriber_name = g_vns_constants.ModuleNames.find(Module::VROUTER_AGENT)->second;
        ds_client->Subscribe(subscriber_name, DiscoveryServiceClient::DNSService, dns_instances,
            boost::bind(&DiscoveryAgentClient::DiscoverySubscribeDNSHandler, _1)); 
    }    
}


void DiscoveryAgentClient::DiscoverySubscribeDNSHandler(std::vector<DSResponse> resp) {
    VNController::ApplyDiscoveryDnsXmppServices(resp);
}


void DiscoveryAgentClient::DiscoverySubscribeXmppHandler(std::vector<DSResponse> resp) {
    VNController::ApplyDiscoveryXmppServices(resp);
}

void DiscoveryAgentClient::DiscoverySubscribeCollectorHandler(std::vector<DSResponse> resp) {

    static bool call_connect_once = false;
    if (call_connect_once) {
        return;
    }

    std::vector<DSResponse>::iterator iter;
    for (iter = resp.begin(); iter != resp.end(); iter++) {
        DSResponse dr = *iter;
        // TODO expect only one instance of collector service

        Agent::SetCollector(dr.ep.address().to_string());
        Agent::SetCollectorPort(dr.ep.port());
        Sandesh::ConnectToCollector(dr.ep.address().to_string(), dr.ep.port());
        call_connect_once = true;
    } 
}

void DiscoveryAgentClient::DiscoverServices() {
    if (!Agent::GetDiscoveryServer().empty()) {
        DiscoveryServiceClient *ds_client = Agent::GetDiscoveryServiceClient();
        if (ds_client) {

            //subscribe to collector service
            AgentInit *instance = AgentInit::GetInstance();    
            if (instance) {
                if (instance->GetCollectorServer().empty()) { 
                    DiscoveryAgentClient::DiscoverCollector(); 
                }
            }

            //subscribe to Xmpp Server on controller
            if (Agent::GetXmppServer(0).empty()) {
                DiscoveryAgentClient::DiscoverController(); 
            } 
            
            //subscribe to DNServer 
            if (Agent::GetDnsXmppServer(0).empty()) {
                DiscoveryAgentClient::DiscoverDNS(); 
            } 
        }
    }
}

void DiscoveryAgentClient::Shutdown() {
    DiscoveryServiceClient *ds_client = Agent::GetDiscoveryServiceClient(); 
    if (ds_client) {
        //unsubscribe to services 
        ds_client->Unsubscribe(DiscoveryServiceClient::XmppService);
        ds_client->Unsubscribe(g_vns_constants.ModuleNames.find(Module::COLLECTOR)->second);
        ds_client->Unsubscribe(DiscoveryServiceClient::DNSService);

        delete ds_client;
        Agent::SetDiscoveryServiceClient(NULL);
    }
}
