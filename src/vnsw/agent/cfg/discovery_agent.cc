/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/program_options.hpp>

#include <sandesh/sandesh.h>
#include "base/logging.h"
#include "cmn/agent_cmn.h"
#include "init/agent_param.h"
#include "discovery_agent.h"
#include "controller/controller_init.h"
#include "cmn/agent_cmn.h"
#include "cfg/cfg_init.h"

#include <discovery/client/discovery_client_stats_types.h>

using namespace boost::asio;

void DiscoveryAgentClient::Init(AgentParam *param) {
    param_ = param;
    if (!agent_cfg_->agent()->GetDiscoveryServer().empty()) {
        boost::system::error_code ec;
        ip::tcp::endpoint dss_ep;
        dss_ep.address(
            ip::address::from_string(agent_cfg_->agent()->GetDiscoveryServer(),
            ec));
        uint32_t port = agent_cfg_->agent()->GetDiscoveryServerPort();
        if (!port) {
            port = DISCOVERY_SERVER_PORT;
        }
        dss_ep.port(port);
 
        std::string subscriber_name = 
            g_vns_constants.ModuleNames.find(Module::VROUTER_AGENT)->second;
        DiscoveryServiceClient *ds_client = 
            (new DiscoveryServiceClient(agent_cfg_->agent()->GetEventManager(), 
             dss_ep, subscriber_name));
        ds_client->Init();

        agent_cfg_->agent()->SetDiscoveryServiceClient(ds_client);
    }
}

void DiscoveryAgentClient::DiscoverController() {
    
    DiscoveryServiceClient *ds_client = 
        agent_cfg_->agent()->GetDiscoveryServiceClient();
    if (ds_client) {

        int xs_instances = 
            agent_cfg_->agent()->GetDiscoveryXmppServerInstances();
        if ((xs_instances < 0) || (xs_instances > 2)) {
            xs_instances = 2;
        }

        ds_client->Subscribe(DiscoveryServiceClient::XmppService, xs_instances,
            boost::bind(&DiscoveryAgentClient::DiscoverySubscribeXmppHandler,
                         this, _1)); 
    }    
}

void DiscoveryAgentClient::DiscoverDNS() {
    
    DiscoveryServiceClient *ds_client = 
        agent_cfg_->agent()->GetDiscoveryServiceClient();
    if (ds_client) {

        int dns_instances = 
            agent_cfg_->agent()->GetDiscoveryXmppServerInstances();
        if ((dns_instances < 0) || (dns_instances > 2)) {
            dns_instances = 2;
        }

        ds_client->Subscribe(DiscoveryServiceClient::DNSService, dns_instances,
            boost::bind(&DiscoveryAgentClient::DiscoverySubscribeDNSHandler, 
                        this, _1)); 
    }    
}


void DiscoveryAgentClient::DiscoverySubscribeDNSHandler(std::vector<DSResponse> resp) {
    agent_cfg_->agent()->controller()->ApplyDiscoveryDnsXmppServices(resp);
}


void DiscoveryAgentClient::DiscoverySubscribeXmppHandler(std::vector<DSResponse> resp) {
    agent_cfg_->agent()->controller()->ApplyDiscoveryXmppServices(resp);
}

void DiscoveryAgentClient::DiscoverServices() {
    if (!agent_cfg_->agent()->GetDiscoveryServer().empty()) {
        DiscoveryServiceClient *ds_client = 
            agent_cfg_->agent()->GetDiscoveryServiceClient();
        if (ds_client) {

            //subscribe to collector service
            if (param_->collector().to_ulong() == 0) {
                Module::type module = Module::VROUTER_AGENT;
                NodeType::type node_type = 
                    g_vns_constants.Module2NodeType.find(module)->second;
                std::string subscriber_name = 
                    g_vns_constants.ModuleNames.find(module)->second;
                std::string node_type_name = 
                    g_vns_constants.NodeTypeNames.find(node_type)->second;

                Sandesh::CollectorSubFn csf = 0;
                csf = boost::bind(&DiscoveryServiceClient::Subscribe, 
                                  ds_client, _1, _2, _3);
                std::vector<std::string> list;
                list.clear();
                Sandesh::InitGenerator(subscriber_name,
                                       Agent::GetInstance()->GetHostName(),
                                       node_type_name,
                                       g_vns_constants.INSTANCE_ID_DEFAULT, 
                                       Agent::GetInstance()->GetEventManager(),
                                       Agent::GetInstance()->GetSandeshPort(),
                                       csf,
                                       list,
                                       NULL);
            }

            //subscribe to Xmpp Server on controller
            if (agent_cfg_->agent()->GetXmppServer(0).empty()) {
                DiscoveryAgentClient::DiscoverController(); 
            } 

            //subscribe to DNServer 
            if (agent_cfg_->agent()->GetDnsXmppServer(0).empty()) {
                DiscoveryAgentClient::DiscoverDNS(); 
            } 
        }
    }
}

void DiscoveryAgentClient::Shutdown() {
    DiscoveryServiceClient *ds_client = 
        agent_cfg_->agent()->GetDiscoveryServiceClient(); 
    if (ds_client) {
        //unsubscribe to services 
        ds_client->Shutdown();
    }
}

// sandesh discovery subscriber stats
void DiscoveryClientSubscriberStatsReq::HandleRequest() const {

    DiscoveryClientSubscriberStatsResponse *resp =
        new DiscoveryClientSubscriberStatsResponse();
    resp->set_context(context());

    std::vector<DiscoveryClientSubscriberStats> stats_list;
    DiscoveryServiceClient *ds = 
        Agent::GetInstance()->GetDiscoveryServiceClient();
        //DiscoveryAgentClient::GetAgentDiscoveryServiceClient();
    if (ds) {
        ds->FillDiscoveryServiceSubscriberStats(stats_list);
    }

    resp->set_subscriber(stats_list);
    resp->set_more(false);
    resp->Response();
}

// sandesh discovery publisher stats
void DiscoveryClientPublisherStatsReq::HandleRequest() const {

    DiscoveryClientPublisherStatsResponse *resp =
        new DiscoveryClientPublisherStatsResponse();
    resp->set_context(context());

    resp->set_more(false);
    resp->Response();
}
