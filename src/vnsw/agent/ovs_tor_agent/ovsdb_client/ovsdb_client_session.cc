/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#include <assert.h>

extern "C" {
#include <ovsdb_wrapper.h>
};

#include <cmn/agent.h>
#include <ovsdb_types.h>
#include <ovsdb_client_connection_state.h>
#include <ovsdb_client_idl.h>
#include <ovsdb_client_session.h>
#include <ovsdb_route_peer.h>

#include <cstddef>

using OVSDB::OvsdbClientIdl;
using OVSDB::OvsdbClientSession;
using OVSDB::ConnectionStateTable;

int OvsdbClientSession::ovsdb_io_task_id_ = -1;

OvsdbClientSession::OvsdbClientSession(Agent *agent, OvsPeerManager *manager) :
    client_idl_(NULL), agent_(agent), manager_(manager), parser_(NULL),
    connection_time_("-") {

    idl_inited_ = false;
    // initialize ovsdb_io task id on first constructor.
    if (ovsdb_io_task_id_ == -1) {
        ovsdb_io_task_id_ = agent->task_scheduler()->GetTaskId("OVSDB::IO");
    }
}

OvsdbClientSession::~OvsdbClientSession() {
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
            struct jsonrpc_msg *msg = NULL;
            char *error = ovsdb_wrapper_jsonrpc_msg_from_json(json, &msg);

            if (error) {
                assert(msg == NULL);

                OVSDB_SESSION_TRACE(Trace, this,
                                    "Error parsing incoming message: " +
                                    std::string(error));
                free(error);
                // trigger close due to message parse failure.
                TriggerClose();

                // bail out, skip processing further.
                return;
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
        } else {
            // enqueue a NULL message to idl (to track activity), so that
            // if we keep on reading partial data for a very long time
            // particularly during initial response for monitor request
            // with scaled config/routes in OVSDB-server
            if (idl_inited_ == true && !client_idl_->deleted()) {
                client_idl_->MessageProcess(NULL);
            }

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
    connection_time_ = UTCUsecToString(UTCTimestampUsec());
    OVSDB_SESSION_TRACE(Trace, this, "Connection to client established");
    client_idl_ = new OvsdbClientIdl(this, agent_, manager_);
    idl_inited_ = true;
    client_idl_->OnEstablish();
}

void OvsdbClientSession::OnClose() {
    if (ovsdb_close_reason().value() == 0) {
        OVSDB_SESSION_TRACE(Trace, this, "Connection to client Closed");
    } else {
        OVSDB_SESSION_TRACE(Trace, this, "Connection to client Closed due to "
                            + ovsdb_close_reason().message());
    }
    if (!idl_inited_) {
        return;
    }
    client_idl_->TriggerDeletion();
}

OvsdbClientIdl *OvsdbClientSession::client_idl() {
    return client_idl_.get();
}

void OvsdbClientSession::AddSessionInfo(SandeshOvsdbClientSession &session) {
    session.set_status(status());
    session.set_remote_ip(remote_ip().to_string());
    session.set_remote_port(remote_port());
    SandeshOvsdbTxnStats sandesh_stats;
    if (client_idl_.get() != NULL) {
        const OvsdbClientIdl::TxnStats &stats = client_idl_->stats();
        sandesh_stats.set_txn_initiated(stats.txn_initiated);
        sandesh_stats.set_txn_succeeded(stats.txn_succeeded);
        sandesh_stats.set_txn_failed(stats.txn_failed);
        sandesh_stats.set_txn_pending(client_idl_->pending_txn_count());
        sandesh_stats.set_pending_send_msg(
                client_idl_->pending_send_msg_count());
    } else {
        sandesh_stats.set_txn_initiated(0);
        sandesh_stats.set_txn_succeeded(0);
        sandesh_stats.set_txn_failed(0);
        sandesh_stats.set_txn_pending(0);
        sandesh_stats.set_pending_send_msg(0);
    }
    session.set_connection_time(connection_time_);
    session.set_txn_stats(sandesh_stats);
}

