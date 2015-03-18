/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#include <ovsdb_object.h>
#include <oper/agent_sandesh.h>
#include <ovsdb_types.h>
#include <ovsdb_client.h>
#include <ovsdb_client_ssl.h>
#include <ovsdb_client_tcp.h>
#include <ovs_tor_agent/tor_agent_init.h>
#include <ovs_tor_agent/tor_agent_param.h>

using OVSDB::OvsdbClient;

OvsdbClient::OvsdbClient(OvsPeerManager *manager, int keepalive_interval) :
    peer_manager_(manager), ksync_obj_manager_(KSyncObjectManager::Init()),
    keepalive_interval_(keepalive_interval) {
}

OvsdbClient::~OvsdbClient() {
}

KSyncObjectManager *OvsdbClient::ksync_obj_manager() {
    return ksync_obj_manager_;
}

int OvsdbClient::keepalive_interval() const {
    return keepalive_interval_;
}

void OvsdbClient::Init() {
}

OvsdbClient *OvsdbClient::Allocate(Agent *agent, TorAgentParam *params,
        OvsPeerManager *manager) {
    KSyncObjectManager::Init();
    if (params->tor_protocol() == "tcp") {
        return (new OvsdbClientTcp(agent, IpAddress(params->tor_ip()),
                                   params->tor_port(), params->tsn_ip(),
                                   params->keepalive_interval(), manager));
    } else if (params->tor_protocol() == "pssl") {
        return (new OvsdbClientSsl(agent, params, manager));
    }
    return NULL;
}

/////////////////////////////////////////////////////////////////////////////
// Sandesh routines
/////////////////////////////////////////////////////////////////////////////
void OvsdbClientReq::HandleRequest() const {
    OvsdbClientResp *resp = new OvsdbClientResp();
    SandeshOvsdbClient client_data;
    OvsdbClient *client = Agent::GetInstance()->ovsdb_client();
    client_data.set_protocol(client->protocol());
    client_data.set_server(client->server());
    client_data.set_port(client->port());
    client_data.set_tor_service_node(client->tsn_ip().to_string());
    client->AddSessionInfo(client_data);
    resp->set_client(client_data);
    resp->set_context(context());
    resp->set_more(false);
    resp->Response();
}

