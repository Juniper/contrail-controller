/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#include <assert.h>

#include <cmn/agent.h>
#include <ovsdb_types.h>
#include <ovsdb_client_idl.h>
#include <ovsdb_client_session.h>
#include <ovsdb_route_peer.h>

#include <cstddef>

using OVSDB::OvsdbClientIdl;
using OVSDB::OvsdbClientSession;

OvsdbClientSession::OvsdbClientSession(Agent *agent, OvsPeerManager *manager) :
    client_idl_(NULL), agent_(agent), manager_(manager),
    monitor_req_timer_(TimerManager::CreateTimer(
                *(agent->event_manager())->io_service(),
                "OVSDB Client Send Monitor Request Wait",
                TaskScheduler::GetInstance()->GetTaskId("Agent::KSync"), 0)) {
}

OvsdbClientSession::~OvsdbClientSession() {
    TimerManager::DeleteTimer(monitor_req_timer_);
}

void OvsdbClientSession::MessageProcess(const u_int8_t *buf, std::size_t len) {
    client_idl_->MessageProcess(buf, len);
}

void OvsdbClientSession::OnEstablish() {
    OVSDB_TRACE(Trace, "Connection to client established");
    client_idl_ = new OvsdbClientIdl(this, agent_, manager_);
    monitor_req_timer_->Start(SendMonitorReqWait,
                boost::bind(&OvsdbClientSession::SendMonitorReqTimerCb, this));
}

void OvsdbClientSession::OnClose() {
    OVSDB_TRACE(Trace, "Connection to client Closed");
    client_idl_->TriggerDeletion();
}

OvsdbClientIdl *OvsdbClientSession::client_idl() {
    return client_idl_.get();
}

bool OvsdbClientSession::SendMonitorReqTimerCb() {
    client_idl_->OnEstablish();
    return false;
}

