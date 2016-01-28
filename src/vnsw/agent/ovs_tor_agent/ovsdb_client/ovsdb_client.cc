/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#include <ovsdb_object.h>
#include <oper/agent_sandesh.h>
#include <ovsdb_types.h>
#include <ovsdb_client.h>
#include <ovsdb_client_ssl.h>
#include <ovsdb_client_tcp.h>
#include <ovsdb_client_connection_state.h>
#include <ovs_tor_agent/tor_agent_init.h>
#include <ovs_tor_agent/tor_agent_param.h>

using OVSDB::OvsdbClient;
using OVSDB::ConnectionStateTable;

OvsdbClient::OvsdbClient(OvsPeerManager *manager, int keepalive_interval,
                         int ha_stale_route_interval) :
    peer_manager_(manager), ksync_obj_manager_(KSyncObjectManager::Init()),
    keepalive_interval_(keepalive_interval),
    ha_stale_route_interval_(ha_stale_route_interval) {
}

OvsdbClient::~OvsdbClient() {
}

void OvsdbClient::set_connect_complete_cb(SessionEventCb cb) {
    connect_complete_cb_ = cb;
}

void OvsdbClient::set_pre_connect_complete_cb(SessionEventCb cb) {
    pre_connect_complete_cb_ = cb;
}

void OvsdbClient::RegisterConnectionTable(Agent *agent) {
    connection_table_.reset(new ConnectionStateTable(agent, peer_manager_));
}

ConnectionStateTable *OvsdbClient::connection_table() {
    return connection_table_.get();
}

KSyncObjectManager *OvsdbClient::ksync_obj_manager() {
    return ksync_obj_manager_;
}

int OvsdbClient::keepalive_interval() const {
    if (keepalive_interval_ < 0) {
        // unconfigured return default
        return OVSDBKeepAliveTimer;
    }

    if (keepalive_interval_ != 0 &&
        keepalive_interval_ < OVSDBMinKeepAliveTimer) {
        return OVSDBMinKeepAliveTimer;
    }

    return keepalive_interval_;
}

int OvsdbClient::ha_stale_route_interval() const {
    if (ha_stale_route_interval_ < 0) {
        // unconfigured return default
        return OVSDBHaStaleRouteTimer;
    }

    if (ha_stale_route_interval_ < OVSDBMinHaStaleRouteTimer) {
        return OVSDBMinHaStaleRouteTimer;
    }

    return ha_stale_route_interval_;
}

void OvsdbClient::Init() {
}

OvsdbClient *OvsdbClient::Allocate(Agent *agent, TorAgentParam *params,
        OvsPeerManager *manager) {
    KSyncObjectManager::Init();
    if (params->tor_protocol() == "tcp") {
        return (new OvsdbClientTcp(agent, IpAddress(params->tor_ip()),
                                   params->tor_port(), params->tsn_ip(),
                                   params->keepalive_interval(),
                                   params->ha_stale_route_interval(), manager));
    } else if (params->tor_protocol() == "pssl") {
        return (new OvsdbClientSsl(agent, IpAddress(params->tor_ip()),
                                   params->tor_port(), params->tsn_ip(),
                                   params->keepalive_interval(),
                                   params->ha_stale_route_interval(),
                                   params->ssl_cert(), params->ssl_privkey(),
                                   params->ssl_cacert(), manager));
    }
    return NULL;
}

/////////////////////////////////////////////////////////////////////////////
// Sandesh routines
/////////////////////////////////////////////////////////////////////////////
class OvsdbClientSandesTask : public Task {
public:
    OvsdbClientSandesTask(std::string resp_ctx) :
        Task((TaskScheduler::GetInstance()->GetTaskId("Agent::KSync")), 0),
        resp_(new OvsdbClientResp()), resp_data_(resp_ctx) {
    }

    virtual ~OvsdbClientSandesTask() {}

    virtual bool Run() {
        SandeshOvsdbClient client_data;
        OvsdbClient *client = Agent::GetInstance()->ovsdb_client();
        client_data.set_protocol(client->protocol());
        client_data.set_server(client->server());
        client_data.set_port(client->port());
        client_data.set_tor_service_node(client->tsn_ip().to_string());
        client_data.set_keepalive_interval(client->keepalive_interval());
        client_data.set_ha_stale_route_interval(client->ha_stale_route_interval());
        client->AddSessionInfo(client_data);
        resp_->set_client(client_data);
        SendResponse();
        return true;
    }
    std::string Description() const { return "OvsdbClientSandesTask"; }

private:
    void SendResponse() {
        resp_->set_context(resp_data_);
        resp_->set_more(false);
        resp_->Response();
    }

    OvsdbClientResp *resp_;
    std::string resp_data_;
    DISALLOW_COPY_AND_ASSIGN(OvsdbClientSandesTask);
};

void OvsdbClientReq::HandleRequest() const {
    OvsdbClientSandesTask *task = new OvsdbClientSandesTask(context());
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    scheduler->Enqueue(task);
}

