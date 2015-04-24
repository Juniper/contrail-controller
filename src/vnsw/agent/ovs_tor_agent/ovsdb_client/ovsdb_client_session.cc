/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#include <assert.h>

extern "C" {
#include <ovsdb_wrapper.h>
};

#include <cmn/agent.h>
#include <ovsdb_types.h>
#include <ovsdb_client_idl.h>
#include <ovsdb_client_session.h>
#include <ovsdb_route_peer.h>

#include <cstddef>

using OVSDB::OvsdbClientIdl;
using OVSDB::OvsdbClientSession;

OvsdbClientSession::OvsdbClientSession(Agent *agent, OvsPeerManager *manager) :
    client_idl_(NULL), agent_(agent), manager_(manager), parser_(NULL),
    monitor_req_timer_(TimerManager::CreateTimer(
                *(agent->event_manager())->io_service(),
                "OVSDB Client Send Monitor Request Wait",
                TaskScheduler::GetInstance()->GetTaskId("Agent::KSync"), 0)) {
    idl_inited_ = false;
}

OvsdbClientSession::~OvsdbClientSession() {
    TimerManager::DeleteTimer(monitor_req_timer_);
}

// This is invoked from OVSDB::IO task context. Handle the keepalive messages
// in The OVSDB::IO task context itself. OVSDB::IO should not have exclusion
// with any of the tasks
void OvsdbClientSession::MessageProcess(const u_int8_t *buf, std::size_t len) {
    std::size_t used = 0;
    // Multiple json message may be clubbed together, need to keep reading
    // the buffer till whole message is consumed.
    while (used != len) {
        if (parser_ == NULL) {
            parser_ = ovsdb_wrapper_json_parser_create(0);
        }
        const u_int8_t *pkt = buf + used;
        std::size_t pkt_len = len - used;
        std::size_t read;
        read = ovsdb_wrapper_json_parser_feed(parser_, (const char *)pkt,
                                              pkt_len);
        used +=read;

        /* If we have complete JSON, attempt to parse it as JSON-RPC. */
        if (ovsdb_wrapper_json_parser_is_done(parser_)) {
            struct json *json = ovsdb_wrapper_json_parser_finish(parser_);
            parser_ = NULL;
            struct jsonrpc_msg *msg;
            char *error = ovsdb_wrapper_jsonrpc_msg_from_json(json, &msg);
            if (error) {
                assert(0);
                free(error);
            }

            if (ovsdb_wrapper_msg_echo_req(msg)) {
                // Echo request from ovsdb-server, reply inline so that
                // ovsdb-server knows that connection is still active
                struct jsonrpc_msg *reply;
                reply = ovsdb_wrapper_jsonrpc_create_reply(msg);
                SendJsonRpc(reply);
            }

            // If idl is inited and active, handover msg to IDL for processing
            // we even enqueue processed echo req message to workqueue, to
            // track session activity in IDL.
            if (idl_inited_ == true && !client_idl_->deleted()) {
                client_idl_->MessageProcess(msg);
                continue;
            }

            ovsdb_wrapper_jsonrpc_msg_destroy(msg);
        }
    }
}

void OvsdbClientSession::SendJsonRpc(struct jsonrpc_msg *msg) {
    struct json *json_msg = ovsdb_wrapper_jsonrpc_msg_to_json(msg);
    char *s = ovsdb_wrapper_json_to_string(json_msg, 0);
    ovsdb_wrapper_json_destroy(json_msg);

    SendMsg((u_int8_t *)s, strlen(s));
    // release the memory allocated by ovsdb_wrapper_json_to_string
    free(s);
}

void OvsdbClientSession::OnEstablish() {
    OVSDB_TRACE(Trace, "Connection to client established");
    client_idl_ = new OvsdbClientIdl(this, agent_, manager_);
    idl_inited_ = true;
    monitor_req_timer_->Start(SendMonitorReqWait,
                boost::bind(&OvsdbClientSession::SendMonitorReqTimerCb, this));
}

void OvsdbClientSession::OnClose() {
    OVSDB_TRACE(Trace, "Connection to client Closed");
    client_idl_->trigger_deletion();
}

OvsdbClientIdl *OvsdbClientSession::client_idl() {
    return client_idl_.get();
}

bool OvsdbClientSession::SendMonitorReqTimerCb() {
    client_idl_->OnEstablish();
    return false;
}

