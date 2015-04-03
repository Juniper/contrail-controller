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
#include <multicast_mac_local_ovsdb.h>
#include <vm_interface_ksync.h>
#include <vn_ovsdb.h>
#include <vrf_ovsdb.h>

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
using OVSDB::MulticastMacLocalOvsdb;
using OVSDB::VrfOvsdbObject;
using OVSDB::VnOvsdbObject;

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

void intrusive_ptr_add_ref(OvsdbClientIdl *p) {
    assert(p->deleted_ == false);
    p->refcount_++;
}

void intrusive_ptr_release(OvsdbClientIdl *p) {
    int count = --p->refcount_;
    switch (count) {
    case 1:
        // intrusive pointer for IDL is always taken first by session while
        // creating new object, and the last reference remaining is always
        // with the session object which on cleanup release idl object.
        OVSDB_TRACE(Trace, "Triggered Session Cleanup on Close");

        // intrusive pointer reference to idl is removed only when ksync
        // object is empty, with this assumption trigger delete for KsyncDb
        // Objects in KSync Context.
        KSyncObjectManager::Unregister(p->vm_interface_table_.release());
        KSyncObjectManager::Unregister(p->logical_switch_table_.release());
        KSyncObjectManager::Unregister(p->vlan_port_table_.release());
        KSyncObjectManager::Unregister(p->vn_ovsdb_.release());
        p->session_->OnCleanup();
        break;
    case 0:
        OVSDB_TRACE(Trace, "Deleted IDL associated to Closed Session");
        delete p;
        break;
    default:
        break;
    }
}

};

OvsdbClientIdl::OvsdbClientIdl(OvsdbClientSession *session, Agent *agent,
        OvsPeerManager *manager) : idl_(ovsdb_wrapper_idl_create()),
    session_(session), agent_(agent), pending_txn_(), deleted_(false),
    manager_(manager), connection_state_(OvsdbSessionRcvWait),
    keepalive_timer_(TimerManager::CreateTimer(
                *(agent->event_manager())->io_service(),
                "OVSDB Client Keep Alive Timer",
                TaskScheduler::GetInstance()->GetTaskId("Agent::KSync"), 0)) {
    refcount_ = 0;
    vtep_global_= ovsdb_wrapper_vteprec_global_first(idl_);
    ovsdb_wrapper_idl_set_callback(idl_, (void *)this,
            ovsdb_wrapper_idl_callback, ovsdb_wrapper_idl_txn_ack);
    parser_ = NULL;
    receive_queue_ = new WorkQueue<OvsdbMsg *>(
            TaskScheduler::GetInstance()->GetTaskId("Agent::KSync"), 0,
            boost::bind(&OvsdbClientIdl::ProcessMessage, this, _1));
    for (int i = 0; i < OVSDB_TYPE_COUNT; i++) {
        callback_[i] = NULL;
    }
    route_peer_.reset(manager->Allocate(session_->remote_ip()));
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
    multicast_mac_local_ovsdb_.reset(new MulticastMacLocalOvsdb(this,
                                                                route_peer()));
    vrf_ovsdb_.reset(new VrfOvsdbObject(this, (DBTable *)agent->vrf_table()));
    vn_ovsdb_.reset(new VnOvsdbObject(this, (DBTable *)agent->vn_table()));
}

OvsdbClientIdl::~OvsdbClientIdl() {
    TimerManager::DeleteTimer(keepalive_timer_);
    receive_queue_->Shutdown();
    delete receive_queue_;
    manager_->Free(route_peer_.release());
    ovsdb_wrapper_idl_destroy(idl_);
}

OvsdbClientIdl::OvsdbMsg::OvsdbMsg(struct jsonrpc_msg *m) : msg(m) {
}

OvsdbClientIdl::OvsdbMsg::~OvsdbMsg() {
    if (this->msg != NULL) {
        ovsdb_wrapper_jsonrpc_msg_destroy(this->msg);
        this->msg = NULL;
    }
}

void OvsdbClientIdl::OnEstablish() {
    if (deleted_) {
        OVSDB_TRACE(Trace, "IDL deleted skipping Monitor Request");
        return;
    }
    OVSDB_TRACE(Trace, "Sending Monitor Request");
    SendJsonRpc(ovsdb_wrapper_idl_encode_monitor_request(idl_));

    int keepalive_intv = session_->keepalive_interval();
    if (keepalive_intv < 0) {
        // timer not configured, use default timer
        keepalive_intv = OVSDBKeepAliveTimer;
    } else if (keepalive_intv == 0) {
        // timer configured not to run, return from here.
        return;
    }

    if (keepalive_intv < OVSDBMinKeepAliveTimer) {
        // keepalive interval is not supposed to be less than min value.
        keepalive_intv = OVSDBMinKeepAliveTimer;
    }

    // Start the Keep Alives
    keepalive_timer_->Start(keepalive_intv,
            boost::bind(&OvsdbClientIdl::KeepAliveTimerCb, this));
}

void OvsdbClientIdl::SendJsonRpc(struct jsonrpc_msg *msg) {
    struct json *json_msg = ovsdb_wrapper_jsonrpc_msg_to_json(msg);
    char *s = ovsdb_wrapper_json_to_string(json_msg, 0);
    ovsdb_wrapper_json_destroy(json_msg);

    session_->SendMsg((u_int8_t *)s, strlen(s));
    // release the memory allocated by ovsdb_wrapper_json_to_string
    free(s);
}

// This is invoked from OVSDB::IO task context. Handle the keepalive messages
// in The OVSDB::IO task context itself. OVSDB::IO should not have exclusion
// with any of the tasks
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

            // Process all received messages in a KSync workqueue task context,
            // to assure only one thread is writting data to OVSDB client.
            // we even enqueue processed echo req message to workqueue, to
            // track session activity in KSync task context.
            if (!deleted_) {
                OvsdbMsg *ovs_msg = new OvsdbMsg(msg);
                receive_queue_->Enqueue(ovs_msg);
                continue;
            }

            ovsdb_wrapper_jsonrpc_msg_destroy(msg);
        }
    }
}

bool OvsdbClientIdl::ProcessMessage(OvsdbMsg *msg) {
    if (!deleted_) {
        // echo req and reply messages are just enqueued to identify
        // session activity, since they need no further processing
        // skip and delete the message
        if (!ovsdb_wrapper_msg_echo_req(msg->msg) &&
            !ovsdb_wrapper_msg_echo_reply(msg->msg)) {
            ovsdb_wrapper_idl_msg_process(idl_, msg->msg);
            // msg->msg is freed by process method above
            msg->msg = NULL;
        }
        connection_state_ = OvsdbSessionActive;
    }
    delete msg;
    return true;
}

struct ovsdb_idl_txn *OvsdbClientIdl::CreateTxn(OvsdbEntryBase *entry,
                                            KSyncEntry::KSyncEvent ack_event) {
    if (deleted_) {
        // Don't create new transactions for deleted idl.
        return NULL;
    }
    struct ovsdb_idl_txn *txn =  ovsdb_wrapper_idl_txn_create(idl_);
    pending_txn_[txn] = entry;
    if (entry != NULL) {
        // if entry is available store the ack_event in entry
        entry->ack_event_ = ack_event;
    }
    return txn;
}

void OvsdbClientIdl::DeleteTxn(struct ovsdb_idl_txn *txn) {
    pending_txn_.erase(txn);
    ovsdb_wrapper_idl_txn_destroy(txn);
}

// API to trigger ovs row del followed by add
// used by OvsdbEntry on catastrophic change event, which
// results in emulating a delete followed by add
void OvsdbClientIdl::NotifyDelAdd(struct ovsdb_idl_row *row) {
    int i = ovsdb_wrapper_row_type(row);
    if (i >= OvsdbClientIdl::OVSDB_TYPE_COUNT)
        return;
    if (callback_[i] != NULL) {
        callback_[i](OvsdbClientIdl::OVSDB_DEL, row);
        callback_[i](OvsdbClientIdl::OVSDB_ADD, row);
    }
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

MulticastMacLocalOvsdb *OvsdbClientIdl::multicast_mac_local_ovsdb() {
    return multicast_mac_local_ovsdb_.get();
}

VrfOvsdbObject *OvsdbClientIdl::vrf_ovsdb() {
    return vrf_ovsdb_.get();
}

VnOvsdbObject *OvsdbClientIdl::vn_ovsdb() {
    return vn_ovsdb_.get();
}

bool OvsdbClientIdl::KeepAliveTimerCb() {
    switch (connection_state_) {
    case OvsdbSessionActive:
        // session is active, move to Receive wait state to
        // identify session activity.
        connection_state_ = OvsdbSessionRcvWait;
        return true;
    case OvsdbSessionRcvWait:
        {
        // send echo request and restart the timer to wait for reply
        struct jsonrpc_msg *req = ovsdb_wrapper_jsonrpc_create_echo_request();
        connection_state_ = OvsdbSessionEchoWait;
        SendJsonRpc(req);
        }
        return true;
    case OvsdbSessionEchoWait:
        // echo reply not recevied ovsdb-server is not responding,
        // close the session
        OVSDB_TRACE(Error, "KeepAlive failed, Closing Session");
        session_->TriggerClose();
        // Connection is closed, timer doesn't need restart
        return false;
    }
    return true;
}

void OvsdbClientIdl::TriggerDeletion() {
    // idl should not be already marked as deleted
    assert(!deleted_);
    // mark idl being set for deletion, so we don't create further txn
    deleted_ = true;

    // trigger txn failure for pending transcations
    PendingTxnMap::iterator it = pending_txn_.begin();
    while (it != pending_txn_.end()) {
        OvsdbEntryBase *entry = it->second;
        DeleteTxn(it->first);
        // Ack failure, if entry is available.
        if (entry)
            entry->Ack(false);
        it = pending_txn_.begin();
    }

    // trigger KSync Object delete for all objects.
    vm_interface_table_->DeleteTable();
    physical_switch_table_->DeleteTable();
    logical_switch_table_->DeleteTable();
    physical_port_table_->DeleteTable();
    physical_locator_table_->DeleteTable();
    vlan_port_table_->DeleteTable();
    unicast_mac_local_ovsdb_->DeleteTable();
    multicast_mac_local_ovsdb_->DeleteTable();
    vn_ovsdb_->DeleteTable();

    // trigger delete table for vrf table, which internally handles
    // deletion of route table.
    vrf_ovsdb_->DeleteTable();
}

