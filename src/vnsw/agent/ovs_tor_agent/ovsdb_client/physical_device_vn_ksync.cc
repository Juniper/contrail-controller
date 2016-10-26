/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

extern "C" {
#include <ovsdb_wrapper.h>
};
#include <ovs_tor_agent/tor_agent_init.h>
#include <ovsdb_client.h>
#include <ovsdb_client_idl.h>
#include <ovsdb_client_session.h>
#include <physical_switch_ovsdb.h>
#include <physical_device_vn_ksync.h>
#include <logical_switch_ovsdb.h>

#include <oper/vn.h>
#include <oper/vrf.h>
#include <oper/physical_device_vn.h>
#include <ovsdb_sandesh.h>
#include <ovsdb_types.h>

using namespace OVSDB;

PhysicalDeviceVnKSyncEntry::PhysicalDeviceVnKSyncEntry(OvsdbDBObject *table,
        const PhysicalDeviceVn *entry) : OvsdbDBEntry(table),
    name_(UuidToString(entry->vn()->GetUuid())),
    device_name_(entry->device_display_name()), vxlan_id_(entry->vxlan_id()),
    logical_switch_ref_(NULL), ls_create_ref_(NULL) {
}

PhysicalDeviceVnKSyncEntry::PhysicalDeviceVnKSyncEntry(OvsdbDBObject *table,
        const PhysicalDeviceVnKSyncEntry *entry) : OvsdbDBEntry(table),
    name_(entry->name_), device_name_( entry->device_name_),
    vxlan_id_(entry->vxlan_id_), logical_switch_ref_(NULL),
    ls_create_ref_(NULL) {
}

PhysicalDeviceVnKSyncEntry::~PhysicalDeviceVnKSyncEntry() {
}

void PhysicalDeviceVnKSyncEntry::AddMsg(struct ovsdb_idl_txn *txn) {
    // on successful ADD after resolving the physical switch
    // take the reference to logical switch entry and creator reference
    // to the create request for the same
    LogicalSwitchTable *l_table = table_->client_idl()->logical_switch_table();
    LogicalSwitchEntry key(l_table, name_.c_str());
    logical_switch_ref_ = l_table->GetReference(&key);
    LogicalSwitchEntry *logical_switch =
        static_cast<LogicalSwitchEntry *>(logical_switch_ref_.get());
    ls_create_ref_ = logical_switch->create_request();
    // set to correct vn_entry
    PhysicalDeviceVn *entry =
        static_cast<PhysicalDeviceVn *>(GetDBEntry());
    logical_switch->set_vn_ref(entry->vn());
    SendTrace(PhysicalDeviceVnKSyncEntry::ADD_REQ);
}

void PhysicalDeviceVnKSyncEntry::ChangeMsg(struct ovsdb_idl_txn *txn) {
    AddMsg(txn);
}

void PhysicalDeviceVnKSyncEntry::DeleteMsg(struct ovsdb_idl_txn *txn) {
    // release creator reference to logical switch
    logical_switch_ref_ = NULL;
    ls_create_ref_ = NULL;
    SendTrace(PhysicalDeviceVnKSyncEntry::DEL_REQ);
}

const std::string &PhysicalDeviceVnKSyncEntry::name() const {
    return name_;
}

const std::string &PhysicalDeviceVnKSyncEntry::device_name() const {
    return device_name_;
}

int64_t PhysicalDeviceVnKSyncEntry::vxlan_id() const {
    return vxlan_id_;
}

bool PhysicalDeviceVnKSyncEntry::Sync(DBEntry *db_entry) {
    PhysicalDeviceVn *entry =
        static_cast<PhysicalDeviceVn *>(db_entry);
    bool change = false;
    if (vxlan_id_ != entry->vxlan_id()) {
        vxlan_id_ = entry->vxlan_id();
        change = true;
    }
    if (device_name_ != entry->device_display_name()) {
        device_name_ = entry->device_display_name();
        change = true;
    }
    PhysicalSwitchTable *p_table = table_->client_idl()->physical_switch_table();
    PhysicalSwitchEntry key(p_table, device_name_.c_str());
    PhysicalSwitchEntry *p_switch =
        static_cast<PhysicalSwitchEntry *>(p_table->Find(&key));
    if (NULL == p_switch || !p_switch->IsResolved()) {
        change = true;
    }
    return change;
}

bool PhysicalDeviceVnKSyncEntry::IsLess(const KSyncEntry &entry) const {
    const PhysicalDeviceVnKSyncEntry &ps_entry =
        static_cast<const PhysicalDeviceVnKSyncEntry&>(entry);
    if (device_name_ != ps_entry.device_name_)
        return device_name_ < ps_entry.device_name_;
    return name_ < ps_entry.name_;
}

KSyncEntry *PhysicalDeviceVnKSyncEntry::UnresolvedReference() {
    PhysicalSwitchTable *p_table = table_->client_idl()->physical_switch_table();
    PhysicalSwitchEntry key(p_table, device_name_.c_str());
    PhysicalSwitchEntry *p_switch =
        static_cast<PhysicalSwitchEntry *>(p_table->GetReference(&key));
    if (!p_switch->IsResolved()) {
        // release creator reference to logical switch
        // to delete Logical switch if created before by this
        // physical device vn entry
        logical_switch_ref_ = NULL;
        ls_create_ref_ = NULL;
        return p_switch;
    }

    return NULL;
}

LogicalSwitchEntry *PhysicalDeviceVnKSyncEntry::logical_switch() {
    return static_cast<LogicalSwitchEntry *>(logical_switch_ref_.get());
}

void PhysicalDeviceVnKSyncEntry::SendTrace(Trace event) const {
    SandeshPhysicalDeviceVnKSyncInfo info;
    switch (event) {
    case ADD_REQ:
        info.set_op("Add Requested");
        break;
    case DEL_REQ:
        info.set_op("Delete Requested");
        break;
    default:
        info.set_op("unknown");
    }
    info.set_name(name_);
    info.set_device_name(device_name_);
    info.set_vxlan(vxlan_id_);
    OVSDB_TRACE(PhysicalDeviceVnKSync, info);
}

PhysicalDeviceVnKSyncTable::PhysicalDeviceVnKSyncTable(OvsdbClientIdl *idl) :
    OvsdbDBObject(idl, false) {
}

PhysicalDeviceVnKSyncTable::~PhysicalDeviceVnKSyncTable() {
}

KSyncEntry *PhysicalDeviceVnKSyncTable::Alloc(const KSyncEntry *key,
                                              uint32_t index) {
    const PhysicalDeviceVnKSyncEntry *k_entry =
        static_cast<const PhysicalDeviceVnKSyncEntry *>(key);
    PhysicalDeviceVnKSyncEntry *entry =
        new PhysicalDeviceVnKSyncEntry(this, k_entry);
    return entry;
}

KSyncEntry *PhysicalDeviceVnKSyncTable::DBToKSyncEntry(const DBEntry* db_entry) {
    const PhysicalDeviceVn *entry =
        static_cast<const PhysicalDeviceVn *>(db_entry);
    PhysicalDeviceVnKSyncEntry *key =
        new PhysicalDeviceVnKSyncEntry(this, entry);
    return static_cast<KSyncEntry *>(key);
}

OvsdbDBEntry *PhysicalDeviceVnKSyncTable::AllocOvsEntry(struct ovsdb_idl_row *row) {
    return NULL;
}

KSyncDBObject::DBFilterResp PhysicalDeviceVnKSyncTable::OvsdbDBEntryFilter(
        const DBEntry *db_entry, const OvsdbDBEntry *ovsdb_entry) {
    const PhysicalDeviceVn *entry =
        static_cast<const PhysicalDeviceVn *>(db_entry);

    // Delete the entry which has invalid VxLAN id associated.
    if (entry->vxlan_id() == 0) {
        return DBFilterDelete;
    }
    return DBFilterAccept;
}

/////////////////////////////////////////////////////////////////////////////
// Sandesh routines
/////////////////////////////////////////////////////////////////////////////
PhysicalDeviceVnKSyncTask::PhysicalDeviceVnKSyncTask(
        std::string resp_ctx, AgentSandeshArguments &args) :
    OvsdbSandeshTask(resp_ctx, args), name_(""), device_name_("") {
    if (false == args.Get("name", &name_)) {
        name_ = "";
    }
    if (false == args.Get("device_name", &name_)) {
        device_name_ = "";
    }
    int vxlan_id = 0;
    if (false == args.Get("vxlan_id", &vxlan_id)) {
        vxlan_id = 0;
    }
    vxlan_id_ = vxlan_id;
}

PhysicalDeviceVnKSyncTask::PhysicalDeviceVnKSyncTask(std::string resp_ctx,
                                                     const std::string &ip,
                                                     uint32_t port,
                                                     const std::string &name,
                                                     const std::string &device_name,
                                                     uint32_t vxlan_id) :
    OvsdbSandeshTask(resp_ctx, ip, port), name_(name),
    device_name_(device_name), vxlan_id_(vxlan_id) {
}

PhysicalDeviceVnKSyncTask::~PhysicalDeviceVnKSyncTask() {
}

void PhysicalDeviceVnKSyncTask::EncodeArgs(AgentSandeshArguments &args) {
    if (!name_.empty()) {
        args.Add("name", name_);
    }
    if (!device_name_.empty()) {
        args.Add("device_name", device_name_);
    }
    if (vxlan_id_ != 0) {
        args.Add("vxlan_id", vxlan_id_);
    }
}

OvsdbSandeshTask::FilterResp
PhysicalDeviceVnKSyncTask::Filter(KSyncEntry *kentry) {
    PhysicalDeviceVnKSyncEntry *entry =
        static_cast<PhysicalDeviceVnKSyncEntry *>(kentry);
    if (!name_.empty()) {
        if (entry->name().find(name_) == std::string::npos) {
            return FilterDeny;
        }
    }
    if (!device_name_.empty()) {
        if (entry->device_name().find(device_name_) == std::string::npos) {
            return FilterDeny;
        }
    }
    LogicalSwitchEntry *lentry = entry->logical_switch();
    if (vxlan_id_ != 0 && lentry != NULL) {
        const OvsdbResourceVxLanId &res = lentry->res_vxlan_id();
        if (lentry->vxlan_id() != vxlan_id_ &&
            res.active_vxlan_id() != vxlan_id_) {
            return FilterDeny;
        }
    }
    return FilterAllow;
}

void PhysicalDeviceVnKSyncTask::UpdateResp(KSyncEntry *kentry,
                                           SandeshResponse *resp) {
    PhysicalDeviceVnKSyncEntry *pentry =
        static_cast<PhysicalDeviceVnKSyncEntry *>(kentry);
    OvsdbPhysicaDeviceVnEntry entry;
    entry.set_state(pentry->StateString());
    entry.set_physical_switch(pentry->device_name());
    entry.set_name(pentry->name());
    entry.set_vxlan_id(pentry->vxlan_id());
    if (pentry->logical_switch() != NULL) {
        LogicalSwitchSandeshTask task("", ip_, port_, pentry->name(), 0);
        entry.set_logical_switch(task.EncodeFirstPage());
    }
    OvsdbPhysicaDeviceVnResp *dev_vn_resp =
        static_cast<OvsdbPhysicaDeviceVnResp *>(resp);
    std::vector<OvsdbPhysicaDeviceVnEntry> &dev_vn =
        const_cast<std::vector<OvsdbPhysicaDeviceVnEntry>&>(
                dev_vn_resp->get_dev_vn());
    dev_vn.push_back(entry);
}

SandeshResponse *PhysicalDeviceVnKSyncTask::Alloc() {
    return static_cast<SandeshResponse *>(new OvsdbPhysicaDeviceVnResp());
}

KSyncObject *PhysicalDeviceVnKSyncTask::GetObject(OvsdbClientSession *session) {
    return static_cast<KSyncObject *>(
            session->client_idl()->physical_device_vn_table());
}

void OvsdbPhysicaDeviceVnReq::HandleRequest() const {
    PhysicalDeviceVnKSyncTask *task =
        new PhysicalDeviceVnKSyncTask(context(), get_session_remote_ip(),
                                      get_session_remote_port(),
                                      get_name(), get_device_name(), get_vxlan_id());
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    scheduler->Enqueue(task);
}
