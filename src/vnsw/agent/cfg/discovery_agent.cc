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

    if (!Agent::GetInstance()->GetDiscoveryServer().empty()) {
        boost::system::error_code ec;
        ip::tcp::endpoint dss_ep;
        dss_ep.address(ip::address::from_string(Agent::GetInstance()->GetDiscoveryServer(), ec));
        uint32_t port = Agent::GetInstance()->GetDiscoveryServerPort();
        if (!port) {
            port = DISCOVERY_SERVER_PORT;
        }
        dss_ep.port(port);
 
        DiscoveryServiceClient *ds_client = 
            (new DiscoveryServiceClient(Agent::GetInstance()->GetEventManager(), dss_ep));
        ds_client->Init();

        Agent::GetInstance()->SetDiscoveryServiceClient(ds_client);

    }
}

void DiscoveryAgentClient::DiscoverController() {
    
    DiscoveryServiceClient *ds_client = Agent::GetInstance()->GetDiscoveryServiceClient();
    if (ds_client) {

        int xs_instances = Agent::GetInstance()->GetDiscoveryXmppServerInstances();
        if ((xs_instances < 0) || (xs_instances > 2)) {
            xs_instances = 2;
        }

        std::string subscriber_name = g_vns_constants.ModuleNames.find(Module::VROUTER_AGENT)->second;
        ds_client->Subscribe(subscriber_name, DiscoveryServiceClient::XmppService, xs_instances,
            boost::bind(&DiscoveryAgentClient::DiscoverySubscribeXmppHandler, _1)); 
    }    
}

void DiscoveryAgentClient::DiscoverDNS() {
    
    DiscoveryServiceClient *ds_client = Agent::GetInstance()->GetDiscoveryServiceClient();
    if (ds_client) {

        int dns_instances = Agent::GetInstance()->GetDiscoveryXmppServerInstances();
        if ((dns_instances < 0) || (dns_instances > 2)) {
            dns_instances = 2;
        }

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

void DiscoveryAgentClient::DiscoverServices() {
    if (!Agent::GetInstance()->GetDiscoveryServer().empty()) {
        DiscoveryServiceClient *ds_client = Agent::GetInstance()->GetDiscoveryServiceClient();
        if (ds_client) {

            //subscribe to collector service
            AgentInit *instance = AgentInit::GetInstance();    
            if (instance) {
                if (instance->GetCollectorServer().empty()) { 

                    std::string subscriber_name = g_vns_constants.ModuleNames.find(Module::VROUTER_AGENT)->second;

                    Sandesh::CollectorSubFn csf = 0;
                    csf = boost::bind(&DiscoveryServiceClient::Subscribe, 
                                      ds_client, subscriber_name, _1, _2, _3);
                    std::vector<std::string> list;
                    list.clear();
                    Sandesh::InitGenerator(subscriber_name,
                                    Agent::GetInstance()->GetHostName(), 
                                    Agent::GetInstance()->GetEventManager(),
                                    Agent::GetInstance()->GetSandeshPort(),
                                    csf,
                                    list,
                                    NULL);
                }
            }

            //subscribe to Xmpp Server on controller
            if (Agent::GetInstance()->GetXmppServer(0).empty()) {
                DiscoveryAgentClient::DiscoverController(); 
            } 
            
            //subscribe to DNServer 
            if (Agent::GetInstance()->GetDnsXmppServer(0).empty()) {
                DiscoveryAgentClient::DiscoverDNS(); 
            } 
        }
    }
}

void DiscoveryAgentClient::Shutdown() {
    DiscoveryServiceClient *ds_client = Agent::GetInstance()->GetDiscoveryServiceClient(); 
    if (ds_client) {
        //unsubscribe to services 
        ds_client->Shutdown();
        delete ds_client;
        Agent::GetInstance()->SetDiscoveryServiceClient(NULL);
    }
}
