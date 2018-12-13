/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

extern "C" {
#include <ovsdb_wrapper.h>
};
#include <ovs_tor_agent/tor_agent_init.h>
#include <ovsdb_client.h>
#include <ovsdb_client_idl.h>
#include <ovsdb_client_session.h>
#include <physical_switch_ovsdb.h>
#include <logical_switch_ovsdb.h>
#include <physical_port_ovsdb.h>
#include <vlan_port_binding_ovsdb.h>
#include <vm_interface_ksync.h>

#include <oper/vn.h>
#include <oper/interface_common.h>
#include <oper/physical_device_vn.h>
#include <ovsdb_sandesh.h>
#include <ovsdb_types.h>

using namespace OVSDB;

VlanPortBindingEntry::VlanPortBindingEntry(VlanPortBindingTable *table,
        const std::string &physical_device, const std::string &physical_port,
        uint16_t vlan_tag, const std::string &logical_switch) :
    OvsdbDBEntry(table), logical_switch_name_(logical_switch),
    physical_port_name_(physical_port), physical_device_name_(physical_device),
    vlan_(vlan_tag), vmi_uuid_(boost::uuids::nil_uuid()),
    old_logical_switch_name_(), logical_switch_(NULL, this) {
}

VlanPortBindingEntry::VlanPortBindingEntry(VlanPortBindingTable *table,
        const VlanLogicalInterface *entry) : OvsdbDBEntry(table),
    logical_switch_name_(), physical_port_name_(entry->phy_intf_display_name()),
    physical_device_name_(entry->phy_dev_display_name()), vlan_(entry->vlan()),
    vmi_uuid_(boost::uuids::nil_uuid()), old_logical_switch_name_(),
    logical_switch_(NULL, this) {
}

VlanPortBindingEntry::VlanPortBindingEntry(VlanPortBindingTable *table,
        const VlanPortBindingEntry *key) : OvsdbDBEntry(table),
    logical_switch_name_(key->logical_switch_name_),
    physical_port_name_(key->physical_port_name_),
    physical_device_name_(key->physical_device_name_), vlan_(key->vlan_),
    vmi_uuid_(boost::uuids::nil_uuid()), old_logical_switch_name_(),
    logical_switch_(NULL, this) {
}

void VlanPortBindingEntry::PreAddChange() {
    if (!logical_switch_name_.empty()) {
        LogicalSwitchTable *l_table =
            table_->client_idl()->logical_switch_table();
        LogicalSwitchEntry ls_key(l_table, logical_switch_name_.c_str());
        logical_switch_ =
            static_cast<LogicalSwitchEntry *>(l_table->GetReference(&ls_key));
    } else {
        logical_switch_ = NULL;
    }
}

void VlanPortBindingEntry::PostDelete() {
    logical_switch_ = NULL;
}

void VlanPortBindingEntry::AddMsg(struct ovsdb_idl_txn *txn) {
    PhysicalPortTable *p_table = table_->client_idl()->physical_port_table();
    PhysicalPortEntry key(p_table, physical_device_name_, physical_port_name_);
    physical_port_ = p_table->GetReference(&key);
    PhysicalPortEntry *port =
        static_cast<PhysicalPortEntry *>(physical_port_.get());

    // update logical switch name propagated to OVSDB server, this will
    // be used in change Operation to reduce the unncessary computation
    // when there is change which VlanPortBindingEntry is not
    // interested into
    old_logical_switch_name_ = logical_switch_name_;
    if (!logical_switch_name_.empty()) {
        port->AddBinding(vlan_,
                static_cast<LogicalSwitchEntry *>(logical_switch_.get()));
        OVSDB_TRACE(Trace, "Adding port vlan binding port " +
                physical_port_name_ + " vlan " + integerToString(vlan_) +
                " to Logical Switch " + logical_switch_name_);
    } else {
        OVSDB_TRACE(Trace, "Deleting port vlan binding port " +
                physical_port_name_ + " vlan " + integerToString(vlan_));
        port->DeleteBinding(vlan_, NULL);
    }

    // Don't trigger update for stale entries
    if (!stale()) {
        port->TriggerUpdate();
    }
}

void VlanPortBindingEntry::ChangeMsg(struct ovsdb_idl_txn *txn) {
    if (logical_switch_name_ == old_logical_switch_name_) {
        // no change return from here.
        return;
    }
    PhysicalPortEntry *port =
        static_cast<PhysicalPortEntry *>(physical_port_.get());
    OVSDB_TRACE(Trace, "Changing port vlan binding port " +
            physical_port_name_ + " vlan " + integerToString(vlan_));
    port->DeleteBinding(vlan_, NULL);

    AddMsg(txn);
}

void VlanPortBindingEntry::DeleteMsg(struct ovsdb_idl_txn *txn) {
    if (!physical_port_) {
        return;
    }
    PhysicalPortEntry *port =
        static_cast<PhysicalPortEntry *>(physical_port_.get());
    OVSDB_TRACE(Trace, "Deleting port vlan binding port " +
            physical_port_name_ + " vlan " + integerToString(vlan_));
    port->DeleteBinding(vlan_,
            static_cast<LogicalSwitchEntry *>(logical_switch_.get()));
    port->TriggerUpdate();
}

bool VlanPortBindingEntry::Sync(DBEntry *db_entry) {
    VlanLogicalInterface *entry =
        static_cast<VlanLogicalInterface *>(db_entry);
    std::string ls_name =
        (dynamic_cast<const VlanPortBindingTable *>(table_))->
        GetLogicalSwitchName(entry);
    boost::uuids::uuid vmi_uuid(boost::uuids::nil_uuid());
    bool change = false;

    if (entry->vm_interface()) {
        vmi_uuid = entry->vm_interface()->GetUuid();
    }

    if (vmi_uuid_ != vmi_uuid) {
        vmi_uuid_ = vmi_uuid;
        change = true;
    }

    if (ls_name != logical_switch_name_) {
        logical_switch_name_ = ls_name;
        change = true;
    }
    return change;
}

bool VlanPortBindingEntry::IsLess(const KSyncEntry &entry) const {
    const VlanPortBindingEntry &vps_entry =
        static_cast<const VlanPortBindingEntry&>(entry);
    if (vlan_ != vps_entry.vlan_)
        return vlan_ < vps_entry.vlan_;
    if (physical_device_name_ != vps_entry.physical_device_name_)
        return physical_device_name_ < vps_entry.physical_device_name_;
    return physical_port_name_ < vps_entry.physical_port_name_;
}

KSyncEntry *VlanPortBindingEntry::UnresolvedReference() {
    PhysicalSwitchTable *ps_table =
        table_->client_idl()->physical_switch_table();
    PhysicalSwitchEntry ps_key(ps_table, physical_device_name_.c_str());
    PhysicalSwitchEntry *p_switch =
        static_cast<PhysicalSwitchEntry *>(ps_table->GetReference(&ps_key));
    if (!p_switch->IsResolved()) {
        OVSDB_TRACE(Trace, "Physical Switch unavailable for Port Vlan Binding "+
                physical_port_name_ + " vlan " + integerToString(vlan_) +
                " to Logical Switch " + logical_switch_name_);
        return p_switch;
    }

    PhysicalPortTable *p_table = table_->client_idl()->physical_port_table();
    PhysicalPortEntry key(p_table, physical_device_name_, physical_port_name_);
    PhysicalPortEntry *p_port =
        static_cast<PhysicalPortEntry *>(p_table->GetReference(&key));
    if (!p_port->IsResolved()) {
        OVSDB_TRACE(Trace, "Physical Port unavailable for Port Vlan Binding " +
                physical_port_name_ + " vlan " + integerToString(vlan_) +
                " to Logical Switch " + logical_switch_name_);
        return p_port;
    }

    // check for VMI only if entry is not stale marked.
    if (!stale()) {
        VMInterfaceKSyncObject *vm_intf_table =
            table_->client_idl()->vm_interface_table();
        VMInterfaceKSyncEntry vm_intf_key(vm_intf_table, vmi_uuid_);
        VMInterfaceKSyncEntry *vm_intf = static_cast<VMInterfaceKSyncEntry *>(
                vm_intf_table->GetReference(&vm_intf_key));
        if (!vm_intf->IsResolved()) {
            OVSDB_TRACE(Trace, "VM Interface unavailable for Port Vlan Binding " +
                    physical_port_name_ + " vlan " + integerToString(vlan_) +
                    " to Logical Switch " + logical_switch_name_);
            return vm_intf;
        } else if (logical_switch_name_.empty()) {
            // update latest name after resolution.
            logical_switch_name_ = vm_intf->vn_name();
        }
    }

    if (!logical_switch_name_.empty()) {
        // Check only if logical switch name is present.
        LogicalSwitchTable *l_table =
            table_->client_idl()->logical_switch_table();
        LogicalSwitchEntry ls_key(l_table, logical_switch_name_.c_str());
        LogicalSwitchEntry *ls_entry =
            static_cast<LogicalSwitchEntry *>(l_table->GetReference(&ls_key));
        if (!ls_entry->IsResolved()) {
            OVSDB_TRACE(Trace, "Logical Switch  unavailable for Port Vlan "
                    "Binding " + physical_port_name_ + " vlan " +
                    integerToString(vlan_) + " to Logical Switch " +
                    logical_switch_name_);
            return ls_entry;
        }
    }

    return NULL;
}

const std::string &VlanPortBindingEntry::logical_switch_name() const {
    return logical_switch_name_;
}

const std::string &VlanPortBindingEntry::physical_port_name() const {
    return physical_port_name_;
}

const std::string &VlanPortBindingEntry::physical_device_name() const {
    return physical_device_name_;
}

uint16_t VlanPortBindingEntry::vlan() const {
    return vlan_;
}

VlanPortBindingTable::VlanPortBindingTable(OvsdbClientIdl *idl) :
    OvsdbDBObject(idl, true) {
}

VlanPortBindingTable::~VlanPortBindingTable() {
}

KSyncEntry *VlanPortBindingTable::Alloc(const KSyncEntry *key, uint32_t index) {
    const VlanPortBindingEntry *k_entry =
        static_cast<const VlanPortBindingEntry *>(key);
    VlanPortBindingEntry *entry = new VlanPortBindingEntry(this, k_entry);
    return entry;
}

KSyncEntry *VlanPortBindingTable::DBToKSyncEntry(const DBEntry* db_entry) {
    const VlanLogicalInterface *entry =
        static_cast<const VlanLogicalInterface *>(db_entry);
    VlanPortBindingEntry *key = new VlanPortBindingEntry(this, entry);
    return static_cast<KSyncEntry *>(key);
}

std::string
VlanPortBindingTable::GetLogicalSwitchName(const DBEntry *e) const {
    const VlanLogicalInterface *entry =
        dynamic_cast<const VlanLogicalInterface *>(e);
    std::string ls_name;
    if (entry->vm_interface() && entry->vm_interface()->vn()) {
        ls_name = UuidToString(entry->vm_interface()->vn()->GetUuid());
    }

    return ls_name;
}

KSyncDBObject::DBFilterResp VlanPortBindingTable::OvsdbDBEntryFilter(
        const DBEntry *entry, const OvsdbDBEntry *ovsdb_entry) {
    const VlanLogicalInterface *l_port =
        dynamic_cast<const VlanLogicalInterface *>(entry);
    if (l_port == NULL) {
        // Ignore entries other than VLanLogicalInterface.
        return DBFilterIgnore;
    }

    // Logical interface without vm interface is incomplete entry
    // for ovsdb, trigger delete.
    if (l_port->vm_interface() == NULL) {
        if (ovsdb_entry != NULL) {
            OVSDB_TRACE(Trace, "VM Interface Unavialable, Deleting Logical "
                        "Port " + l_port->name());
        } else {
            OVSDB_TRACE(Trace, "VM Interface Unavialable, Ignoring Logical "
                        "Port " + l_port->name());
        }
        return DBFilterDelete;
    }

    // Since we need physical port name and device name as key, ignore entry
    // if physical port or device is not yet present.
    if (l_port->phy_intf_display_name().empty()) {
        OVSDB_TRACE(Trace, "Ignoring Port Vlan Binding due to physical port "
                "name unavailablity Logical port = " + l_port->name());
        return DBFilterIgnore;
    }

    if (l_port->phy_dev_display_name().empty()) {
        OVSDB_TRACE(Trace, "Ignoring Port Vlan Binding due to device name "
                "unavailablity Logical port = " + l_port->name());
        return DBFilterIgnore;
    }

    // On LS change old vlan-port binding to old LS has to be removed from tor
    // and new LS need to be added for the same vlan-port
    if (ovsdb_entry &&
        (dynamic_cast<const VlanPortBindingEntry*>(ovsdb_entry))->
         logical_switch_name() != GetLogicalSwitchName(entry)) {
        return DBFilterDelAdd;
    }

    return DBFilterAccept;
}

/////////////////////////////////////////////////////////////////////////////
// Sandesh routines
/////////////////////////////////////////////////////////////////////////////
VlanPortBindingSandeshTask::VlanPortBindingSandeshTask(
        std::string resp_ctx, AgentSandeshArguments &args) :
    OvsdbSandeshTask(resp_ctx, args), physical_port_("") {
    if (false == args.Get("physical_port", &physical_port_)) {
        physical_port_ = "";
    }
}

VlanPortBindingSandeshTask::VlanPortBindingSandeshTask(
        std::string resp_ctx, const std::string &ip, uint32_t port,
        const std::string &physical_port) :
    OvsdbSandeshTask(resp_ctx, ip, port), physical_port_(physical_port) {
}

VlanPortBindingSandeshTask::~VlanPortBindingSandeshTask() {
}

void VlanPortBindingSandeshTask::EncodeArgs(AgentSandeshArguments &args) {
    if (!physical_port_.empty()) {
        args.Add("physical_port", physical_port_);
    }
}

OvsdbSandeshTask::FilterResp
VlanPortBindingSandeshTask::Filter(KSyncEntry *kentry) {
    if (!physical_port_.empty()) {
        VlanPortBindingEntry *entry =
            static_cast<VlanPortBindingEntry *>(kentry);
        if (entry->physical_port_name().find(
                    physical_port_) != std::string::npos) {
            return FilterAllow;
        }
        return FilterDeny;
    }
    return FilterAllow;
}

void VlanPortBindingSandeshTask::UpdateResp(KSyncEntry *kentry,
                                            SandeshResponse *resp) {
    VlanPortBindingEntry *entry = static_cast<VlanPortBindingEntry *>(kentry);
    OvsdbVlanPortBindingEntry oentry;
    oentry.set_state(entry->StateString());
    oentry.set_physical_port(entry->physical_port_name());
    oentry.set_physical_device(entry->physical_device_name());
    oentry.set_logical_switch(entry->logical_switch_name());
    oentry.set_vlan(entry->vlan());
    OvsdbVlanPortBindingResp *v_resp =
        static_cast<OvsdbVlanPortBindingResp *>(resp);
    std::vector<OvsdbVlanPortBindingEntry> &bindings =
        const_cast<std::vector<OvsdbVlanPortBindingEntry>&>(
                v_resp->get_bindings());
    bindings.push_back(oentry);
}

SandeshResponse *VlanPortBindingSandeshTask::Alloc() {
    return static_cast<SandeshResponse *>(new OvsdbVlanPortBindingResp());
}

KSyncObject *
VlanPortBindingSandeshTask::GetObject(OvsdbClientSession *session) {
    return static_cast<KSyncObject *>(
            session->client_idl()->vlan_port_table());
}

void OvsdbVlanPortBindingReq::HandleRequest() const {
    VlanPortBindingSandeshTask *task =
        new VlanPortBindingSandeshTask(context(), get_session_remote_ip(),
                                       get_session_remote_port(),
                                       get_physical_port());
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    scheduler->Enqueue(task);
}
