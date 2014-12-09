/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#include <assert.h>
#include <cstddef>
#include <string.h>
#include <stdlib.h>

extern "C" {
#include <ovsdb_wrapper.h>
};
#include <oper/agent_sandesh.h>
#include <ovsdb_types.h>
#include <ovsdb_client_idl.h>
#include <ovsdb_client_session.h>
#include <ovsdb_route_peer.h>
#include <ovsdb_entry.h>
#include <physical_switch_ovsdb.h>
#include <logical_switch_ovsdb.h>
#include <physical_port_ovsdb.h>
#include <physical_locator_ovsdb.h>
#include <vlan_port_binding_ovsdb.h>
#include <unicast_mac_local_ovsdb.h>
#include <unicast_mac_remote_ovsdb.h>
#include <vm_interface_ksync.h>

SandeshTraceBufferPtr OvsdbTraceBuf(SandeshTraceBufferCreate("Ovsdb", 5000));
SandeshTraceBufferPtr OvsdbPktTraceBuf(SandeshTraceBufferCreate("Ovsdb Pkt", 5000));

class PhysicalDeviceTable;
class InterfaceTable;
class PhysicalDeviceVnTable;

using OVSDB::OvsdbClientIdl;
using OVSDB::OvsdbClientSession;
using OVSDB::OvsdbEntryBase;
using OVSDB::VMInterfaceKSyncObject;
using OVSDB::PhysicalSwitchTable;
using OVSDB::LogicalSwitchTable;
using OVSDB::PhysicalPortTable;
using OVSDB::PhysicalLocatorTable;
using OVSDB::VlanPortBindingTable;
using OVSDB::UnicastMacLocalOvsdb;
using OVSDB::VrfOvsdbObject;

namespace OVSDB {
void ovsdb_wrapper_idl_callback(void *idl_base, int op,
        struct ovsdb_idl_row *row) {
    OvsdbClientIdl *client_idl = (OvsdbClientIdl *) idl_base;
    int i = ovsdb_wrapper_row_type(row);
    if (i >= OvsdbClientIdl::OVSDB_TYPE_COUNT)
        return;
    if (client_idl->callback_[i] != NULL)
        client_idl->callback_[i]((OvsdbClientIdl::Op)op, row);
}

void ovsdb_wrapper_idl_txn_ack(void *idl_base, struct ovsdb_idl_txn *txn) {
    OvsdbClientIdl *client_idl = (OvsdbClientIdl *) idl_base;
    OvsdbEntryBase *entry = client_idl->pending_txn_[txn];
    bool success = ovsdb_wrapper_is_txn_success(txn);
    if (!success) {
        OVSDB_TRACE(Error, "Transaction failed: " +
                std::string(ovsdb_wrapper_txn_get_error(txn)));
        // we don't handle the case where txn fails, when entry is not present
        // case of unicast_mac_remote entry.
        assert(entry != NULL);
    }
    client_idl->DeleteTxn(txn);
    if (entry)
        entry->Ack(success);
}
};

OvsdbClientIdl::OvsdbClientIdl(OvsdbClientSession *session, Agent *agent,
        OvsPeerManager *manager) : idl_(ovsdb_wrapper_idl_create()),
    session_(session), agent_(agent), pending_txn_() {
    vtep_global_= ovsdb_wrapper_vteprec_global_first(idl_);
    ovsdb_wrapper_idl_set_callback(idl_, (void *)this,
            ovsdb_wrapper_idl_callback, ovsdb_wrapper_idl_txn_ack);
    parser_ = NULL;
    for (int i = 0; i < OVSDB_TYPE_COUNT; i++) {
        callback_[i] = NULL;
    }
    route_peer_.reset(manager->Allocate(IpAddress()));
    vm_interface_table_.reset(new VMInterfaceKSyncObject(this,
                (DBTable *)agent->interface_table()));
    physical_switch_table_.reset(new PhysicalSwitchTable(this));
    logical_switch_table_.reset(new LogicalSwitchTable(this,
               (DBTable *)agent->physical_device_vn_table()));
    physical_port_table_.reset(new PhysicalPortTable(this));
    physical_locator_table_.reset(new PhysicalLocatorTable(this));
    vlan_port_table_.reset(new VlanPortBindingTable(this,
                (DBTable *)agent->interface_table()));
    unicast_mac_local_ovsdb_.reset(new UnicastMacLocalOvsdb(this,
                route_peer()));
    vrf_ovsdb_.reset(new VrfOvsdbObject(this, (DBTable *)agent->vrf_table()));
}

OvsdbClientIdl::~OvsdbClientIdl() {
    ovsdb_wrapper_idl_destroy(idl_);
}

void OvsdbClientIdl::SendMointorReq() {
    OVSDB_TRACE(Trace, "Sending Monitor Request");
    SendJsonRpc(ovsdb_wrapper_idl_encode_monitor_request(idl_));
}

void OvsdbClientIdl::SendJsonRpc(struct jsonrpc_msg *msg) {
    struct json *json_msg = ovsdb_wrapper_jsonrpc_msg_to_json(msg);
    char *s = ovsdb_wrapper_json_to_string(json_msg, 0);
    ovsdb_wrapper_json_destroy(json_msg);

    session_->SendMsg((u_int8_t *)s, strlen(s));
}

void OvsdbClientIdl::MessageProcess(const u_int8_t *buf, std::size_t len) {
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
        OVSDB_PKT_TRACE(Trace, "Processed: " + std::string((const char *)pkt, read));
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
                //continue;
            }

            if (ovsdb_wrapper_msg_echo_req(msg)) {
                /* Echo request.  Send reply. */
                struct jsonrpc_msg *reply;
                reply = ovsdb_wrapper_jsonrpc_create_reply(msg);
                SendJsonRpc(reply);
                //jsonrpc_session_send(s, reply);
            } else if (ovsdb_wrapper_msg_echo_reply(msg)) {
                /* It's a reply to our echo request.  Suppress it. */
            } else {
                ovsdb_wrapper_idl_msg_process(idl_, msg);
                continue;
            }
            ovsdb_wrapper_jsonrpc_msg_destroy(msg);
        }
    }
}

struct ovsdb_idl_txn *OvsdbClientIdl::CreateTxn(OvsdbEntryBase *entry) {
    struct ovsdb_idl_txn *txn =  ovsdb_wrapper_idl_txn_create(idl_);
    pending_txn_[txn] = entry;
    return txn;
}

void OvsdbClientIdl::DeleteTxn(struct ovsdb_idl_txn *txn) {
    pending_txn_.erase(txn);
    ovsdb_wrapper_idl_txn_destroy(txn);
}

Ip4Address OvsdbClientIdl::tsn_ip() {
    return session_->tsn_ip();
}

KSyncObjectManager *OvsdbClientIdl::ksync_obj_manager() {
    return session_->ksync_obj_manager();
}

OvsPeer *OvsdbClientIdl::route_peer() {
    return route_peer_.get();
}

VMInterfaceKSyncObject *OvsdbClientIdl::vm_interface_table() {
    return vm_interface_table_.get();
}

PhysicalSwitchTable *OvsdbClientIdl::physical_switch_table() {
    return physical_switch_table_.get();
}

LogicalSwitchTable *OvsdbClientIdl::logical_switch_table() {
    return logical_switch_table_.get();
}

PhysicalPortTable *OvsdbClientIdl::physical_port_table() {
    return physical_port_table_.get();
}

PhysicalLocatorTable *OvsdbClientIdl::physical_locator_table() {
    return physical_locator_table_.get();
}

VlanPortBindingTable *OvsdbClientIdl::vlan_port_table() {
    return vlan_port_table_.get();
}

UnicastMacLocalOvsdb *OvsdbClientIdl::unicast_mac_local_ovsdb() {
    return unicast_mac_local_ovsdb_.get();
}

VrfOvsdbObject *OvsdbClientIdl::vrf_ovsdb() {
    return vrf_ovsdb_.get();
}

