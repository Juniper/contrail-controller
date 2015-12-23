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
#include <ovsdb_client_connection_state.h>
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
#include <ovsdb_resource_vxlan_id.h>

SandeshTraceBufferPtr OvsdbTraceBuf(SandeshTraceBufferCreate("Ovsdb", 5000));
SandeshTraceBufferPtr OvsdbSMTraceBuf(SandeshTraceBufferCreate("Ovsdb SM", 5000));
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
using OVSDB::ConnectionStateTable;
using OVSDB::OvsdbResourceVxLanIdTable;

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
    OvsdbEntryList &entry_list = client_idl->pending_txn_[txn];
    bool success = ovsdb_wrapper_is_txn_success(txn);
    if (!success) {
        // increment stats.
        client_idl->stats_.txn_failed++;
        OVSDB_TRACE(Error, "Transaction failed: " +
                std::string(ovsdb_wrapper_txn_get_error(txn)));
        // we don't handle the case where txn fails, when entry is not present
        // case of unicast_mac_remote entry.
        assert(!entry_list.empty());
    } else {
        // increment stats.
        client_idl->stats_.txn_succeeded++;
    }

    // trigger ack for all the entries encode in this txn
    OvsdbEntryList::iterator it;
    for (it = entry_list.begin(); it != entry_list.end(); ++it) {
        OvsdbEntryBase *entry = *it;
        entry->Ack(success);
    }

    // Donot Access entry_list ref after transaction delete
    client_idl->DeleteTxn(txn);

    // if there are pending txn messages to be scheduled, pick one and schedule
    if (!client_idl->pending_send_msgs_.empty()) {
        client_idl->session_->SendJsonRpc(client_idl->pending_send_msgs_.front());
        client_idl->pending_send_msgs_.pop();
    }
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
        OVSDB_SESSION_TRACE(Trace, p->session_,
                            "Triggered Session Cleanup on Close");

        // intrusive pointer reference to idl is removed only when ksync
        // object is empty, with this assumption trigger delete for KsyncDb
        // Objects in KSync Context.
        KSyncObjectManager::Unregister(p->vm_interface_table_.release());
        KSyncObjectManager::Unregister(p->physical_switch_table_.release());
        KSyncObjectManager::Unregister(p->logical_switch_table_.release());
        KSyncObjectManager::Unregister(p->physical_port_table_.release());
        KSyncObjectManager::Unregister(p->physical_locator_table_.release());
        KSyncObjectManager::Unregister(p->vlan_port_table_.release());
        KSyncObjectManager::Unregister(p->unicast_mac_local_ovsdb_.release());
        KSyncObjectManager::Unregister(p->multicast_mac_local_ovsdb_.release());
        KSyncObjectManager::Unregister(p->vrf_ovsdb_.release());
        KSyncObjectManager::Unregister(p->vn_ovsdb_.release());
        p->session_->OnCleanup();
        break;
    case 0:
        OVSDB_SM_TRACE(Trace, "Deleted IDL associated to Closed Session");
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
                agent->task_scheduler()->GetTaskId("Agent::KSync"), 0)),
    monitor_request_id_(NULL), bulk_txn_(NULL), stats_() {
    refcount_ = 0;
    vtep_global_= ovsdb_wrapper_vteprec_global_first(idl_);
    ovsdb_wrapper_idl_set_callback(idl_, (void *)this,
            ovsdb_wrapper_idl_callback, ovsdb_wrapper_idl_txn_ack);
    receive_queue_ = new WorkQueue<OvsdbMsg *>(
            agent->task_scheduler()->GetTaskId("Agent::KSync"), 0,
            boost::bind(&OvsdbClientIdl::ProcessMessage, this, _1));
    receive_queue_->set_name("OVSDB receive queue");
    for (int i = 0; i < OVSDB_TYPE_COUNT; i++) {
        callback_[i] = NULL;
    }
    route_peer_.reset(manager->Allocate(session_->remote_ip()));
    vxlan_table_.reset(new OvsdbResourceVxLanIdTable());
    vm_interface_table_.reset(new VMInterfaceKSyncObject(this,
            (DBTable *)agent->interface_table()));
    physical_switch_table_.reset(new PhysicalSwitchTable(this));
    logical_switch_table_.reset(new LogicalSwitchTable(this));
    physical_port_table_.reset(new PhysicalPortTable(this));
    physical_locator_table_.reset(new PhysicalLocatorTable(this));
    vlan_port_table_.reset(new VlanPortBindingTable(this));
    unicast_mac_local_ovsdb_.reset(new UnicastMacLocalOvsdb(this,
                route_peer()));
    multicast_mac_local_ovsdb_.reset(new MulticastMacLocalOvsdb(this,
                                                                route_peer()));
    vrf_ovsdb_.reset(new VrfOvsdbObject(this));
    vn_ovsdb_.reset(new VnOvsdbObject(this, (DBTable *)agent->vn_table()));
}

OvsdbClientIdl::~OvsdbClientIdl() {
    if (monitor_request_id_ != NULL) {
        ovsdb_wrapper_json_destroy(monitor_request_id_);
        monitor_request_id_ = NULL;
    }

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

OvsdbClientIdl::TxnStats::TxnStats() : txn_initiated(0), txn_succeeded(0),
    txn_failed(0) {
}

void OvsdbClientIdl::OnEstablish() {
    if (deleted_) {
        OVSDB_SESSION_TRACE(Trace, session_,
                            "IDL deleted skipping Monitor Request");
        return;
    }

    struct jsonrpc_msg *monitor_request =
        ovsdb_wrapper_idl_encode_monitor_request(idl_);

    assert(monitor_request_id_ == NULL);
    // clone and save json for monitor request
    monitor_request_id_ = ovsdb_wrapper_jsonrpc_clone_id(monitor_request);

    OVSDB_SESSION_TRACE(Trace, session_, "Sending Monitor Request");
    session_->SendJsonRpc(monitor_request);

    int keepalive_intv = session_->keepalive_interval();
    if (keepalive_intv == 0) {
        // timer configured not to run, return from here.
        return;
    }

    // Start the Keep Alives
    keepalive_timer_->Start(keepalive_intv,
            boost::bind(&OvsdbClientIdl::KeepAliveTimerCb, this));
}

void OvsdbClientIdl::TxnScheduleJsonRpc(struct jsonrpc_msg *msg) {
    // increment stats.
    stats_.txn_initiated++;

    if (!session_->ThrottleInFlightTxnMessages() ||
        OVSDBMaxInFlightPendingTxn >= pending_txn_.size()) {
        session_->SendJsonRpc(msg);
    } else {
        // throttle txn messages, push the message to pending send
        // msg queue to be scheduled later.
        pending_send_msgs_.push(msg);
    }
}

bool OvsdbClientIdl::ProcessMessage(OvsdbMsg *msg) {
    if (!deleted_) {
        // NULL message, echo req and reply messages are just enqueued to
        // identify session activity, since they need no further processing
        // skip and delete the message
        if (msg->msg != NULL &&
            !ovsdb_wrapper_msg_echo_req(msg->msg) &&
            !ovsdb_wrapper_msg_echo_reply(msg->msg)) {
            bool connect_oper_db = false;
            if (ovsdb_wrapper_idl_msg_is_monitor_response(monitor_request_id_,
                                                          msg->msg)) {
                // destroy saved monitor request json message
                ovsdb_wrapper_json_destroy(monitor_request_id_);
                monitor_request_id_ = NULL;
                connect_oper_db = true;
            }
            ovsdb_wrapper_idl_msg_process(idl_, msg->msg);
            // msg->msg is freed by process method above
            msg->msg = NULL;

            // after processing the response to monitor request
            // connect to oper db.
            if (connect_oper_db) {
                // enable physical port updation, before connect to
                // Oper DB, to allow creation of stale entries for
                // vlan port bindings.
                physical_switch_table_->StartUpdatePorts();

                physical_port_table_->set_stale_create_done();

                ConnectOperDB();
            }
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

    // while encode a non bulk entry send the previous bulk entry to ensure
    // sanity of txns
    if (bulk_txn_ != NULL) {
        // reset bulk_txn_ and bulk_entries_ before triggering EncodeSendTxn
        // to let the transaction send go through
        pending_txn_[bulk_txn_] = bulk_entries_;
        bulk_entries_.clear();
        struct ovsdb_idl_txn *bulk_txn = bulk_txn_;
        bulk_txn_ = NULL;
        EncodeSendTxn(bulk_txn, NULL);
    }

    struct ovsdb_idl_txn *txn = ovsdb_wrapper_idl_txn_create(idl_);
    OvsdbEntryList entry_list;
    if (entry != NULL) {
        entry_list.insert(entry);
        // if entry is available store the ack_event in entry
        entry->ack_event_ = ack_event;
    }
    pending_txn_[txn] = entry_list;
    return txn;
}

struct ovsdb_idl_txn *OvsdbClientIdl::CreateBulkTxn(OvsdbEntryBase *entry,
                                            KSyncEntry::KSyncEvent ack_event) {
    if (deleted_) {
        // Don't create new transactions for deleted idl.
        return NULL;
    }

    if (bulk_txn_ == NULL) {
        // if bulk txn is not available create one
        bulk_txn_ = ovsdb_wrapper_idl_txn_create(idl_);
    }

    struct ovsdb_idl_txn *bulk_txn = bulk_txn_;

    // bulk txn can be done only for entries
    assert(entry != NULL);
    bulk_entries_.insert(entry);
    entry->ack_event_ = ack_event;

    // try creating bulk transaction only if pending txn are there
    if (pending_txn_.empty() || bulk_entries_.size() == OVSDBEntriesInBulkTxn) {
        // once done bunch entries add the txn to pending txn list and
        // reset bulk_txn_ to let EncodeSendTxn proceed with bulk txn
        pending_txn_[bulk_txn_] = bulk_entries_;
        bulk_txn_ = NULL;
        bulk_entries_.clear();
    }
    return bulk_txn;
}

bool OvsdbClientIdl::EncodeSendTxn(struct ovsdb_idl_txn *txn,
                                   OvsdbEntryBase *skip_entry) {
    // return false to wait for bulk txn to complete
    if (txn == bulk_txn_) {
        return false;
    }

    struct jsonrpc_msg *msg = ovsdb_wrapper_idl_txn_encode(txn);
    if (msg == NULL) {
        // if it was a bulk transaction trigger Ack for previously
        // held entries, that are waiting for Ack
        OvsdbEntryList &entry_list = pending_txn_[txn];
        OvsdbEntryList::iterator it;
        for (it = entry_list.begin(); it != entry_list.end(); ++it) {
            OvsdbEntryBase *entry = *it;
            if (entry != skip_entry) {
                entry->Ack(true);
            } else {
                entry->TxnDoneNoMessage();
            }
        }
        DeleteTxn(txn);
        return true;
    }
    TxnScheduleJsonRpc(msg);
    return false;
}

void OvsdbClientIdl::DeleteTxn(struct ovsdb_idl_txn *txn) {
    pending_txn_.erase(txn);
    // third party code and handle only one txn at a time,
    // if there is a pending bulk entry encode and send before
    // destroying the current txn
    if (bulk_txn_ != NULL) {
        pending_txn_[bulk_txn_] = bulk_entries_;
        bulk_entries_.clear();
        struct ovsdb_idl_txn *bulk_txn = bulk_txn_;
        bulk_txn_ = NULL;
        EncodeSendTxn(bulk_txn, NULL);
    }
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

void OvsdbClientIdl::MessageProcess(struct jsonrpc_msg *msg) {
    // Enqueue all received messages in receive queue running KSync task
    // context, to assure only one thread is writting data to OVSDB client.
    OvsdbMsg *ovs_msg = new OvsdbMsg(msg);
    receive_queue_->Enqueue(ovs_msg);
}

Ip4Address OvsdbClientIdl::remote_ip() const {
    return session_->remote_ip();
}

uint16_t OvsdbClientIdl::remote_port() const {
    return session_->remote_port();
}

ConnectionStateTable *OvsdbClientIdl::connection_table() {
    return session_->connection_table();
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

OvsdbResourceVxLanIdTable *OvsdbClientIdl::vxlan_table() {
    return vxlan_table_.get();
}

bool OvsdbClientIdl::IsKeepAliveTimerActive() {
    return !keepalive_timer_->cancelled();
}

bool OvsdbClientIdl::IsMonitorInProcess() {
    return (monitor_request_id_ != NULL);
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
        session_->SendJsonRpc(req);
        }
        return true;
    case OvsdbSessionEchoWait:
        // echo reply not recevied ovsdb-server is not responding,
        // close the session
        OVSDB_SESSION_TRACE(Error, session_,
                            "KeepAlive failed, Closing Session");
        session_->TriggerClose();
        // Connection is closed, timer doesn't need restart
        return false;
    }
    return true;
}

void OvsdbClientIdl::TriggerDeletion() {
    // if idl is already marked for delete, return from here
    if (deleted_) {
        return;
    }

    // mark idl being set for deletion, so we don't create further txn
    deleted_ = true;

    // Since IDL is scheduled for deletion cancel keepalive timer
    keepalive_timer_->Cancel();

    // trigger txn failure for pending transcations
    PendingTxnMap::iterator it = pending_txn_.begin();
    while (it != pending_txn_.end()) {
        OvsdbEntryList &entry_list = it->second;
        // Ack failure, if any entry is available.
        OvsdbEntryList::iterator entry_it;
        for (entry_it = entry_list.begin(); entry_it != entry_list.end();
             ++entry_it) {
            OvsdbEntryBase *entry = *entry_it;
            entry->Ack(false);
        }
        DeleteTxn(it->first);
        it = pending_txn_.begin();
    }

    while (!pending_send_msgs_.empty()) {
        // flush and destroy all the pending send messages
        ovsdb_wrapper_jsonrpc_msg_destroy(pending_send_msgs_.front());
        pending_send_msgs_.pop();
    }

    // trigger KSync Object delete for all objects.
    vm_interface_table_->DeleteTable();
    physical_switch_table_->DeleteTable();

    // trigger Process Delete, which will do internal processing to
    // clear self reference from logical switch before triggering
    // delete table
    logical_switch_table_->ProcessDeleteTableReq();

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

const OvsdbClientIdl::TxnStats &OvsdbClientIdl::stats() const {
    return stats_;
}

uint64_t OvsdbClientIdl::pending_txn_count() const {
    return pending_txn_.size();
}

uint64_t OvsdbClientIdl::pending_send_msg_count() const {
    return pending_send_msgs_.size();
}

void OvsdbClientIdl::ConnectOperDB() {
    OVSDB_SESSION_TRACE(Trace, session_,
                        "Received Monitor Response connecting to OperDb");
    logical_switch_table_->OvsdbRegisterDBTable(
            (DBTable *)agent_->physical_device_vn_table());
    vlan_port_table_->OvsdbRegisterDBTable(
            (DBTable *)agent_->interface_table());
    vrf_ovsdb_->OvsdbRegisterDBTable(
            (DBTable *)agent_->vrf_table());
}

