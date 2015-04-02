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

int OvsdbClientSession::ovsdb_io_task_id_ = -1;

OvsdbClientSession::OvsdbClientSession(Agent *agent, OvsPeerManager *manager) :
    client_idl_(NULL), agent_(agent), manager_(manager),
    monitor_req_timer_(TimerManager::CreateTimer(
                *(agent->event_manager())->io_service(),
                "OVSDB Client Send Monitor Request Wait",
                TaskScheduler::GetInstance()->GetTaskId("Agent::KSync"), 0)) {

    // initialize ovsdb_io task id on first constructor.
    if (ovsdb_io_task_id_ == -1) {
        ovsdb_io_task_id_ =
            TaskScheduler::GetInstance()->GetTaskId("OVSDB::IO");
    }
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
    client_idl_->OnEstablish();
}

void OvsdbClientSession::OnClose() {
    OVSDB_TRACE(Trace, "Connection to client Closed");
    client_idl_->TriggerDeletion();
}

OvsdbClientIdl *OvsdbClientSession::client_idl() {
    return client_idl_.get();
}

