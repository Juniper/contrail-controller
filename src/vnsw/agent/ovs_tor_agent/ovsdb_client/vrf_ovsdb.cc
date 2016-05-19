/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

extern "C" {
#include <ovsdb_wrapper.h>
};
#include <ovs_tor_agent/tor_agent_init.h>
#include <ovsdb_client.h>
#include <ovsdb_client_idl.h>
#include <ovsdb_client_session.h>
#include <logical_switch_ovsdb.h>
#include <vrf_ovsdb.h>
#include <unicast_mac_remote_ovsdb.h>
#include <physical_locator_ovsdb.h>

#include <oper/vn.h>
#include <oper/vrf.h>
#include <oper/nexthop.h>
#include <oper/tunnel_nh.h>
#include <oper/agent_path.h>
#include <oper/bridge_route.h>
#include <ovsdb_sandesh.h>
#include <ovsdb_types.h>

using namespace OVSDB;

VrfOvsdbEntry::VrfOvsdbEntry(OvsdbDBObject *table,
        const std::string &logical_switch) : OvsdbDBEntry(table),
    logical_switch_name_(logical_switch), route_table_(NULL),
    oper_route_table_(NULL) {
}

VrfOvsdbEntry::~VrfOvsdbEntry() {
    assert(route_table_ == NULL);
}

bool VrfOvsdbEntry::Add() {
    // if table is scheduled for delete, return from here
    // and wait for delete callback
    if (table_->delete_scheduled()) {
        return true;
    }

    // create route table and register
    if (route_table_ == NULL) {
        route_table_ = new UnicastMacRemoteTable(table_->client_idl(),
                                                 logical_switch_name_, this);
    }

    if (!stale() && route_table_->GetDBTable() == NULL &&
        oper_route_table_ != NULL) {
        route_table_->OvsdbRegisterDBTable(oper_route_table_);
    }
    return true;
}

bool VrfOvsdbEntry::Change() {
    return Add();
}

bool VrfOvsdbEntry::Delete() {
    // delete route table.
    if (route_table_ != NULL) {
        route_table_->DeleteTable();
        // while triggering a delete for UnicastMacRemoteTable, wait
        // for table cleanup to complete to proceed with the KSync
        // state machine, so that we maintain consistency for remote
        // MAC table by not allowing two transient route table, one
        // in process of deletion and other in process of addition
        // which can lead to inconsistent state in OVSDB-server
        return false;
    }

    return true;
}

bool VrfOvsdbEntry::Sync(DBEntry *db_entry) {
    // check if route table is available.
    const VrfEntry *vrf = static_cast<const VrfEntry *>(db_entry);
    assert(logical_switch_name_ == UuidToString(vrf->vn()->GetUuid()));
    if (oper_route_table_ != vrf->GetBridgeRouteTable()) {
        oper_route_table_ = vrf->GetBridgeRouteTable();
        return true;
    }
    return false;
}

bool VrfOvsdbEntry::IsLess(const KSyncEntry &entry) const {
    const VrfOvsdbEntry &vrf_entry =
        static_cast<const VrfOvsdbEntry &>(entry);
    return (logical_switch_name_.compare(vrf_entry.logical_switch_name_) < 0);
}

KSyncEntry* VrfOvsdbEntry::UnresolvedReference() {
    return NULL;
}

void VrfOvsdbEntry::TriggerAck(UnicastMacRemoteTable *table) {
    OvsdbDBObject *object = static_cast<OvsdbDBObject*>(GetObject());
    assert(route_table_ == table);
    route_table_ = NULL;
    object->SafeNotifyEvent(this, KSyncEntry::DEL_ACK);
}

VrfOvsdbObject::VrfOvsdbObject(OvsdbClientIdl *idl) : OvsdbDBObject(idl, true) {
    client_idl_->Register(OvsdbClientIdl::OVSDB_UCAST_MAC_REMOTE,
            boost::bind(&VrfOvsdbObject::OvsdbNotify, this, _1, _2));
}

VrfOvsdbObject::~VrfOvsdbObject() {
}

void VrfOvsdbObject::OvsdbNotify(OvsdbClientIdl::Op op,
        struct ovsdb_idl_row *row) {
    const char *mac = ovsdb_wrapper_ucast_mac_remote_mac(row);
    const char *logical_switch =
        ovsdb_wrapper_ucast_mac_remote_logical_switch(row);
    /* if logical switch is not available ignore notification */
    if (logical_switch == NULL)
        return;
    VrfOvsdbEntry vrf_key(this, logical_switch);
    VrfOvsdbEntry *vrf_ovsdb = static_cast<VrfOvsdbEntry *>(Find(&vrf_key));

    if (vrf_ovsdb == NULL) {
        if (op == OvsdbClientIdl::OVSDB_DEL) {
            // nothing to do return from here.
            return;
        }
        // if vrf is not available create a stale entry
        // to accomodate stale unicast remote route
        vrf_ovsdb = static_cast<VrfOvsdbEntry *>(CreateStale(&vrf_key));
    }

    const char *dest_ip = ovsdb_wrapper_ucast_mac_remote_dst_ip(row);
    UnicastMacRemoteTable *table= vrf_ovsdb->route_table_;
    UnicastMacRemoteEntry key(table, mac);
    if (op == OvsdbClientIdl::OVSDB_DEL) {
        table->NotifyDeleteOvsdb((OvsdbDBEntry*)&key, row);
        if (dest_ip)
            key.dest_ip_ = std::string(dest_ip);
        key.SendTrace(UnicastMacRemoteEntry::DEL_ACK);
    } else if (op == OvsdbClientIdl::OVSDB_ADD) {
        table->NotifyAddOvsdb((OvsdbDBEntry*)&key, row);
        if (dest_ip)
            key.dest_ip_ = std::string(dest_ip);
        key.SendTrace(UnicastMacRemoteEntry::ADD_ACK);
    }
}

KSyncEntry *VrfOvsdbObject::Alloc(const KSyncEntry *key, uint32_t index) {
    const VrfOvsdbEntry *k_entry =
        static_cast<const VrfOvsdbEntry *>(key);
    VrfOvsdbEntry *entry =
        new VrfOvsdbEntry(this, k_entry->logical_switch_name_);
    return entry;
}

KSyncEntry *VrfOvsdbObject::DBToKSyncEntry(const DBEntry* db_entry) {
    const VrfEntry *entry = static_cast<const VrfEntry *>(db_entry);
    VrfOvsdbEntry *key =
        new VrfOvsdbEntry(this, UuidToString(entry->vn()->GetUuid()));
    return static_cast<KSyncEntry *>(key);
}

OvsdbDBEntry *VrfOvsdbObject::AllocOvsEntry(struct ovsdb_idl_row *row) {
    return NULL;
}

KSyncDBObject::DBFilterResp VrfOvsdbObject::OvsdbDBEntryFilter(
        const DBEntry *entry, const OvsdbDBEntry *ovsdb_entry) {
    const VrfEntry *vrf = static_cast<const VrfEntry *>(entry);
    // Delete Vrf if vn goes NULL
    if (vrf->vn() == NULL) {
        return DBFilterDelete;
    }

    if (ovsdb_entry != NULL) {
        const VrfOvsdbEntry *o_vrf =
            static_cast<const VrfOvsdbEntry *>(ovsdb_entry);
        if (o_vrf->logical_switch_name_ != UuidToString(vrf->vn()->GetUuid())) {
            return DBFilterDelAdd;
        }
    }

    return DBFilterAccept;
}

/////////////////////////////////////////////////////////////////////////////
// Sandesh routines
/////////////////////////////////////////////////////////////////////////////
OvsdbVrfSandeshTask::OvsdbVrfSandeshTask(std::string resp_ctx,
                                         AgentSandeshArguments &args) :
    OvsdbSandeshTask(resp_ctx, args) {
    if (false == args.Get("logical_switch", &logical_switch_)) {
        logical_switch_ = "";
    }
    if (false == args.Get("mac", &mac_)) {
        mac_ = "";
    }
}

OvsdbVrfSandeshTask::OvsdbVrfSandeshTask(
        std::string resp_ctx, const std::string &ip, uint32_t port,
        const std::string &logical_switch, const std::string &mac) :
    OvsdbSandeshTask(resp_ctx, ip, port), logical_switch_(logical_switch),
    mac_(mac) {
}

OvsdbVrfSandeshTask::~OvsdbVrfSandeshTask() {
}

void OvsdbVrfSandeshTask::EncodeArgs(AgentSandeshArguments &args) {
    if (!logical_switch_.empty()) {
        args.Add("logical_switch", logical_switch_);
    }
    if (!mac_.empty()) {
        args.Add("mac", mac_);
    }
}

OvsdbSandeshTask::FilterResp OvsdbVrfSandeshTask::Filter(KSyncEntry *kentry) {
    if (!logical_switch_.empty()) {
        VrfOvsdbEntry *entry = static_cast<VrfOvsdbEntry *>(kentry);
        if (entry->logical_switch_name().find(
                    logical_switch_) != std::string::npos) {
            return FilterAllow;
        }
        return FilterDeny;
    }
    return FilterAllow;
}

void OvsdbVrfSandeshTask::UpdateResp(KSyncEntry *kentry,
                                     SandeshResponse *resp) {
    VrfOvsdbEntry *entry = static_cast<VrfOvsdbEntry *>(kentry);
    OvsdbVrfEntry ventry;
    ventry.set_state(entry->StateString());
    ventry.set_logical_switch(entry->logical_switch_name());
    UnicastMacRemoteSandeshTask task("", ip_, port_,
                                     entry->logical_switch_name(), mac_);
    ventry.set_unicast_remote_table(task.EncodeFirstPage());
    OvsdbVrfResp *vrf_resp = static_cast<OvsdbVrfResp *>(resp);

    std::vector<OvsdbVrfEntry> &vrf_list =
        const_cast<std::vector<OvsdbVrfEntry>&>(vrf_resp->get_vrfs());
    vrf_list.push_back(ventry);
}

SandeshResponse *OvsdbVrfSandeshTask::Alloc() {
    return static_cast<SandeshResponse *>(new OvsdbVrfResp());
}

KSyncObject *OvsdbVrfSandeshTask::GetObject(OvsdbClientSession *session) {
    return static_cast<KSyncObject *>(session->client_idl()->vrf_ovsdb());
}

void OvsdbVrfReq::HandleRequest() const {
    OvsdbVrfSandeshTask *task =
        new OvsdbVrfSandeshTask(context(), get_session_remote_ip(),
                                get_session_remote_port(),
                                get_logical_switch(), get_mac());
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    scheduler->Enqueue(task);
}

