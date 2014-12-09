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
    client_idl_(this, agent, manager), agent_(agent) {
}

OvsdbClientSession::~OvsdbClientSession() {
}

void OvsdbClientSession::MessageProcess(const u_int8_t *buf, std::size_t len) {
    client_idl_.MessageProcess(buf, len);
}

void OvsdbClientSession::OnEstablish() {
    OVSDB_TRACE(Trace, "Connection to client established");
    client_idl_.SendMointorReq();
}

void OvsdbClientSession::OnClose() {
    OVSDB_TRACE(Trace, "Connection to client Closed");
    assert(0);
}

OvsdbClientIdl *OvsdbClientSession::client_idl() {
    return &client_idl_;
}

