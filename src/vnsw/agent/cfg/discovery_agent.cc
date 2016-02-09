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
    uint32_t port = agent_cfg_->agent()->discovery_server_port();
    if (!port) {
        port = DISCOVERY_SERVER_PORT;
    }

    ip::tcp::endpoint dss_ep;
    DiscoveryServiceClient *ds_client = NULL;
    if (DiscoveryServiceClient::ParseDiscoveryServerConfig(
        agent_cfg_->agent()->discovery_server(), port, &dss_ep)) {

        std::string subscriber_name =
            agent_cfg_->agent()->discovery_client_name();
        ds_client =
            (new DiscoveryServiceClient(agent_cfg_->agent()->event_manager(), 
             dss_ep, subscriber_name));
        ds_client->Init();
    } else {
        LOG(ERROR, "Invalid Discovery Server hostname or ip " <<
                   agent_cfg_->agent()->discovery_server());
    }
    agent_cfg_->agent()->set_discovery_service_client(ds_client);
}

void DiscoveryAgentClient::DiscoverController() {
    
    DiscoveryServiceClient *ds_client = 
        agent_cfg_->agent()->discovery_service_client();
    if (ds_client) {

        int xs_instances = 0;
        ds_client->Subscribe(
            g_vns_constants.XMPP_SERVER_DISCOVERY_SERVICE_NAME, xs_instances,
            boost::bind(&DiscoveryAgentClient::DiscoverySubscribeXmppHandler,
                         this, _1), MAX_XMPP_SERVERS);
    }    
}

void DiscoveryAgentClient::ReDiscoverController() {

    DiscoveryServiceClient *ds_client =
        agent_cfg_->agent()->discovery_service_client();
    if (ds_client) {
        ds_client->Subscribe(g_vns_constants.XMPP_SERVER_DISCOVERY_SERVICE_NAME);
    }
}


void DiscoveryAgentClient::DiscoverDNS() {
    
    DiscoveryServiceClient *ds_client = 
        agent_cfg_->agent()->discovery_service_client();
    if (ds_client) {

        int dns_instances = agent_cfg_->agent()->discovery_xmpp_server_instances();
        if (dns_instances > MAX_XMPP_SERVERS || dns_instances <= 0) {
            dns_instances = MAX_XMPP_SERVERS;
        }
        ds_client->Subscribe(
            g_vns_constants.DNS_SERVER_DISCOVERY_SERVICE_NAME, dns_instances,
            boost::bind(&DiscoveryAgentClient::DiscoverySubscribeDNSHandler, 
                        this, _1));
    }    
}

void DiscoveryAgentClient::ReDiscoverDNS() {
    
    DiscoveryServiceClient *ds_client = 
        agent_cfg_->agent()->discovery_service_client();
    if (ds_client) {
        ds_client->Subscribe(g_vns_constants.DNS_SERVER_DISCOVERY_SERVICE_NAME);
    }    
}



void DiscoveryAgentClient::DiscoverySubscribeDNSHandler(std::vector<DSResponse> resp) {
    agent_cfg_->agent()->controller()->ApplyDiscoveryDnsXmppServices(resp);
}


void DiscoveryAgentClient::DiscoverySubscribeXmppHandler(std::vector<DSResponse> resp) {
    agent_cfg_->agent()->controller()->ApplyDiscoveryXmppServices(resp);
}

void DiscoveryAgentClient::DiscoverServices() {
    Agent *agent = agent_cfg_->agent();
    if (!agent->discovery_server().empty()) {
        DiscoveryServiceClient *ds_client = 
            agent->discovery_service_client();
        if (ds_client) {

            //subscribe to collector service
            if (param_->collector_server_list().size() == 0) {
                Module::type module = static_cast<Module::type>
                    (agent->module_type());
                NodeType::type node_type = 
                    g_vns_constants.Module2NodeType.find(module)->second;
                std::string node_type_name = 
                    g_vns_constants.NodeTypeNames.find(node_type)->second;

                Sandesh::CollectorSubFn csf = 0;
                csf = boost::bind(&DiscoveryServiceClient::Subscribe, 
                                  ds_client, _1, _2, _3);
                std::vector<std::string> list;
                list.clear();
                Sandesh::set_send_rate_limit(agent->sandesh_send_rate_limit());
                Sandesh::InitGenerator(agent->module_name(),
                                       agent->host_name(),
                                       node_type_name,
                                       agent->instance_id(),
                                       agent->event_manager(),
                                       agent->introspect_port(),
                                       csf,
                                       list,
                                       NULL);
            }

            //subscribe to Xmpp Server on controller
            if (agent->controller_ifmap_xmpp_server(0).empty()) {
                DiscoveryAgentClient::DiscoverController(); 
            } 

            //subscribe to DNServer 
            if (agent->dns_server(0).empty()) {
                DiscoveryAgentClient::DiscoverDNS(); 
            } 
        }
    }
}

void DiscoveryAgentClient::Shutdown() {
    DiscoveryServiceClient *ds_client = 
        agent_cfg_->agent()->discovery_service_client(); 
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
        Agent::GetInstance()->discovery_service_client();
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
