/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

extern "C" {
#include <ovsdb_wrapper.h>
};

#include <base/task.h>

#include <ovs_tor_agent/tor_agent_init.h>
#include <ovsdb_client.h>
#include <ovsdb_client_idl.h>
#include <ovsdb_client_session.h>

#include <logical_switch_ovsdb.h>
#include <physical_port_ovsdb.h>
#include <vlan_port_binding_ovsdb.h>
#include <ovsdb_sandesh.h>
#include <ovsdb_types.h>

using namespace OVSDB;

PhysicalPortEntry::PhysicalPortEntry(PhysicalPortTable *table,
        const std::string &dev_name, const std::string &name) :
    OvsdbEntry(table), name_(name), dev_name_(dev_name), binding_table_(),
    ovs_binding_table_() {
}

PhysicalPortEntry::~PhysicalPortEntry() {
}

bool PhysicalPortEntry::Add() {
    if (ovs_entry_ == NULL) {
        return true;
    }

    return OverrideOvs();
}

bool PhysicalPortEntry::Change() {
    return Add();
}

bool PhysicalPortEntry::Delete() {
    // Nothing to do on delete, return true and be done
    return true;
}

bool PhysicalPortEntry::IsLess(const KSyncEntry &entry) const {
    const PhysicalPortEntry &ps_entry =
        static_cast<const PhysicalPortEntry&>(entry);
    if (dev_name_ != ps_entry.dev_name_) {
        return (dev_name_.compare(ps_entry.dev_name_) < 0);
    }

    return (name_.compare(ps_entry.name_) < 0);
}

KSyncEntry *PhysicalPortEntry::UnresolvedReference() {
    return NULL;
}

void PhysicalPortEntry::TriggerUpdate() {
    if (!IsResolved()) {
        /*
         * we can only modify the vlan bindings in physical port
         * table as we don't own the table, we are not suppose to create
         * a new port entry in the table, so return from here if entry is
         * not resolved
         */
        return;
    }
    table_->Change(this);
}

void PhysicalPortEntry::Encode(struct ovsdb_idl_txn *txn) {
    struct ovsdb_wrapper_port_vlan_binding binding[binding_table_.size()];
    VlanLSTable::iterator it = binding_table_.begin();
    std::size_t i = 0;
    for ( ; it != binding_table_.end(); it++) {
        struct ovsdb_idl_row *ls = it->second->ovs_entry();
        if (ls != NULL) {
            binding[i].ls = ls;
            binding[i].vlan = it->first;
            i++;
        }
    }
    ovsdb_wrapper_update_physical_port(txn, ovs_entry_, binding, i);
}

void PhysicalPortEntry::AddBinding(int16_t vlan, LogicalSwitchEntry *ls) {
    binding_table_[vlan] = ls;
}

void PhysicalPortEntry::DeleteBinding(int16_t vlan, LogicalSwitchEntry *ls) {
    binding_table_.erase(vlan);
}

const std::string &PhysicalPortEntry::name() const {
    return name_;
}

const std::string &PhysicalPortEntry::dev_name() const {
    return dev_name_;
}

const PhysicalPortEntry::VlanLSTable &
PhysicalPortEntry::binding_table() const {
    return binding_table_;
}

const PhysicalPortEntry::VlanLSTable &
PhysicalPortEntry::ovs_binding_table() const {
    return ovs_binding_table_;
}

const PhysicalPortEntry::VlanStatsTable &
PhysicalPortEntry::stats_table() const {
    return stats_table_;
}

bool PhysicalPortEntry::OverrideOvs() {
    struct ovsdb_idl_txn *txn =
        table_->client_idl()->CreateTxn(this, KSyncEntry::ADD_ACK);
    if (txn == NULL) {
        // failed to create transaction because of idl marked for
        // deletion return from here.
        return true;
    }
    Encode(txn);
    struct jsonrpc_msg *msg = ovsdb_wrapper_idl_txn_encode(txn);
    if (msg == NULL) {
        table_->client_idl()->DeleteTxn(txn);
        return true;
    }
    OVSDB_TRACE(Trace, "Sending Vlan Port Binding update for Physical route " +
                       dev_name_ + " Physical Port " + name_);
    table_->client_idl()->TxnScheduleJsonRpc(msg);
    return false;
}

PhysicalPortTable::PhysicalPortTable(OvsdbClientIdl *idl) :
    OvsdbObject(idl), stale_create_done_(false) {
    idl->Register(OvsdbClientIdl::OVSDB_PHYSICAL_PORT,
                  boost::bind(&PhysicalPortTable::Notify, this, _1, _2));
}

PhysicalPortTable::~PhysicalPortTable() {
}

void PhysicalPortTable::Notify(OvsdbClientIdl::Op op,
        struct ovsdb_idl_row *row) {
    if (op == OvsdbClientIdl::OVSDB_DEL) {
        DeletePortEntry(row);
    } else if (op == OvsdbClientIdl::OVSDB_ADD) {
        PhysicalPortEntry *entry = FindPortEntry(row);
        if (entry) {
            EntryOvsdbUpdate(entry);
        }
    }
}

KSyncEntry *PhysicalPortTable::Alloc(const KSyncEntry *key, uint32_t index) {
    const PhysicalPortEntry *k_entry =
        static_cast<const PhysicalPortEntry *>(key);
    PhysicalPortEntry *entry = new PhysicalPortEntry(this, k_entry->dev_name_,
                                                     k_entry->name_);
    return entry;
}

void PhysicalPortTable::CreatePortEntry(struct ovsdb_idl_row *row,
                                        const std::string &physical_device) {
    assert(FindPortEntry(row) == NULL);
    // Create a port entry and add to the idl entry map
    PhysicalPortEntry key(this, physical_device,
                          ovsdb_wrapper_physical_port_name(row));
    PhysicalPortEntry *entry = static_cast<PhysicalPortEntry *>(Find(&key));
    if (entry == NULL) {
        OVSDB_TRACE(Trace, "Add/Change of Physical Port " +
                std::string(ovsdb_wrapper_physical_port_name(row)));
        entry = static_cast<PhysicalPortEntry *>(Create(&key));
        entry->ovs_entry_ = row;
    } else if (!entry->IsActive()) {
        // entry is present but it is a temp entry.
        OVSDB_TRACE(Trace, "Add/Change of Physical Port " +
                std::string(ovsdb_wrapper_physical_port_name(row)));
        Change(entry);
        // Set row pointer after triggering change to activate entry
        // so that message is not encoded.
        entry->ovs_entry_ = row;
    }
    EntryOvsdbUpdate(entry);
    idl_entry_map_[row] = entry;
}

PhysicalPortEntry *PhysicalPortTable::FindPortEntry(struct ovsdb_idl_row *row) {
    IdlEntryMap::iterator it = idl_entry_map_.find(row);
    if (it != idl_entry_map_.end()) {
        return it->second;
    }

    return NULL;
}

void PhysicalPortTable::DeletePortEntry(struct ovsdb_idl_row *row) {
    PhysicalPortEntry *entry = FindPortEntry(row);
    if (entry == NULL) {
        return;
    }

    OVSDB_TRACE(Trace, "Delete of Physical Port " +
            std::string(ovsdb_wrapper_physical_port_name(row)));
    idl_entry_map_.erase(row);
    entry->ovs_entry_ = NULL;
    Delete(entry);
}

void PhysicalPortTable::set_stale_create_done() {
    stale_create_done_ = true;
}

void PhysicalPortTable::EntryOvsdbUpdate(PhysicalPortEntry *entry) {
    std::size_t count =
        ovsdb_wrapper_physical_port_vlan_binding_count(entry->ovs_entry());
    struct ovsdb_wrapper_port_vlan_binding new_bind[count];
    ovsdb_wrapper_physical_port_vlan_binding(entry->ovs_entry(), new_bind);

    // clear the old ovs_binding_table and fill new entries.
    entry->ovs_binding_table_.clear();
    for (std::size_t i = 0; i < count; i++) {
        LogicalSwitchEntry key(client_idl_->logical_switch_table(),
                ovsdb_wrapper_logical_switch_name(new_bind[i].ls));
        LogicalSwitchEntry *ls_entry =
            static_cast<LogicalSwitchEntry *>(
                    client_idl_->logical_switch_table()->Find(&key));
        if (ls_entry != NULL) {
            entry->ovs_binding_table_[new_bind[i].vlan] = ls_entry;
        }
    }

    // Compare difference between tor agent and ovsdb server.
    // on mis-match override and re-program Physical port
    PhysicalPortEntry::VlanLSTable::iterator it =
        entry->binding_table_.begin();
    PhysicalPortEntry::VlanLSTable::iterator ovs_it =
        entry->ovs_binding_table_.begin();
    VlanPortBindingTable *vp_binding_table = client_idl_->vlan_port_table();
    bool ret_override = false;
    while (ovs_it != entry->ovs_binding_table_.end()) {
        if (it != entry->binding_table_.end() && it->first == ovs_it->first) {
            if (it->second != ovs_it->second) {
                // mis-match of logical switch for the vlan
                //break;
                ret_override = true;
            }
            it++;
            ovs_it++;
        } else {
            ret_override = true;
            // mis-match of vlans in binding
            if (stale_create_done_) {
                // done creating stale entries, bail out and override
                break;
            }
            if (it == entry->binding_table_.end() || it->first > ovs_it->first) {
                // Create stale vlan port binding entry
                VlanPortBindingEntry key(vp_binding_table, entry->dev_name(),
                                         entry->name(), ovs_it->first,
                                         ovs_it->second->name());
                vp_binding_table->CreateStale(&key);
                ovs_it++;
            } else {
                it++;
            }
        }
    }

    if (entry->binding_table_.size() != entry->ovs_binding_table_.size()) {
        // we need to anyway override OVSDB if the table size are different
        ret_override = true;
    }

    count = ovsdb_wrapper_physical_port_vlan_stats_count(entry->ovs_entry());
    struct ovsdb_wrapper_port_vlan_stats stats[count];
    ovsdb_wrapper_physical_port_vlan_stats(entry->ovs_entry(), stats);
    entry->stats_table_.clear();
    for (std::size_t i = 0; i < count; i++) {
        entry->stats_table_[stats[i].vlan] = stats[i].stats;
    }

    if (ret_override) {
        // change entry to update vlan port bindings
        Change(entry);
    }
}

/////////////////////////////////////////////////////////////////////////////
// Sandesh routines
/////////////////////////////////////////////////////////////////////////////
PhysicalPortSandeshTask::PhysicalPortSandeshTask(std::string resp_ctx,
                                                 AgentSandeshArguments &args) :
    OvsdbSandeshTask(resp_ctx, args), name_("") {
    if (false == args.Get("name", &name_)) {
        name_ = "";
    }
}

PhysicalPortSandeshTask::PhysicalPortSandeshTask(std::string resp_ctx,
                                                 const std::string &ip,
                                                 uint32_t port,
                                                 const std::string &name) :
    OvsdbSandeshTask(resp_ctx, ip, port), name_(name) {
}

PhysicalPortSandeshTask::~PhysicalPortSandeshTask() {
}

void PhysicalPortSandeshTask::EncodeArgs(AgentSandeshArguments &args) {
    if (!name_.empty()) {
        args.Add("name", name_);
    }
}

OvsdbSandeshTask::FilterResp
PhysicalPortSandeshTask::Filter(KSyncEntry *kentry) {
    if (!name_.empty()) {
        PhysicalPortEntry *entry = static_cast<PhysicalPortEntry *>(kentry);
        if (entry->name().find(name_) != std::string::npos) {
            return FilterAllow;
        }
        return FilterDeny;
    }
    return FilterAllow;
}

void PhysicalPortSandeshTask::UpdateResp(KSyncEntry *kentry,
                                         SandeshResponse *resp) {
    PhysicalPortEntry *entry = static_cast<PhysicalPortEntry *>(kentry);
    OvsdbPhysicalPortEntry pentry;
    pentry.set_state(entry->StateString());
    pentry.set_switch_name(entry->dev_name());
    pentry.set_name(entry->name());
    const PhysicalPortEntry::VlanLSTable &bindings =
        entry->binding_table();
    const PhysicalPortEntry::VlanStatsTable &stats_table =
        entry->stats_table();
    PhysicalPortEntry::VlanLSTable::const_iterator it =
        bindings.begin();
    std::vector<OvsdbPhysicalPortVlanInfo> vlan_list;
    for (; it != bindings.end(); it++) {
        OvsdbPhysicalPortVlanInfo vlan;
        vlan.set_vlan(it->first);
        vlan.set_logical_switch(it->second->name());
        PhysicalPortEntry::VlanStatsTable::const_iterator stats_it =
            stats_table.find(it->first);
        if (stats_it != stats_table.end()) {
            int64_t in_pkts, in_bytes, out_pkts, out_bytes;
            ovsdb_wrapper_get_logical_binding_stats(stats_it->second,
                    &in_pkts, &in_bytes, &out_pkts, &out_bytes);
            vlan.set_in_pkts(in_pkts);
            vlan.set_in_bytes(in_bytes);
            vlan.set_out_pkts(out_pkts);
            vlan.set_out_bytes(out_bytes);
        } else {
            vlan.set_in_pkts(0);
            vlan.set_in_bytes(0);
            vlan.set_out_pkts(0);
            vlan.set_out_bytes(0);
        }
        vlan_list.push_back(vlan);
    }
    pentry.set_vlans(vlan_list);

    OvsdbPhysicalPortResp *port_resp =
        static_cast<OvsdbPhysicalPortResp *>(resp);
    std::vector<OvsdbPhysicalPortEntry> &port_list =
        const_cast<std::vector<OvsdbPhysicalPortEntry>&>(
                port_resp->get_port());
    port_list.push_back(pentry);
}

SandeshResponse *PhysicalPortSandeshTask::Alloc() {
    return static_cast<SandeshResponse *>(new OvsdbPhysicalPortResp());
}

KSyncObject *PhysicalPortSandeshTask::GetObject(OvsdbClientSession *session) {
    return static_cast<KSyncObject *>(
            session->client_idl()->physical_port_table());
}

void OvsdbPhysicalPortReq::HandleRequest() const {
    PhysicalPortSandeshTask *task =
        new PhysicalPortSandeshTask(context(), get_session_remote_ip(),
                                    get_session_remote_port(),
                                    get_name());
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    scheduler->Enqueue(task);
}

