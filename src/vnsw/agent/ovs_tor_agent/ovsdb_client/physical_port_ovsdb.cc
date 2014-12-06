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
#include <ovsdb_types.h>

using OVSDB::OvsdbClient;
using OVSDB::OvsdbClientSession;
using OVSDB::PhysicalPortEntry;
using OVSDB::PhysicalPortTable;

PhysicalPortEntry::PhysicalPortEntry(PhysicalPortTable *table,
        const char *name) : OvsdbEntry(table), name_(name), binding_table_(),
    ovs_binding_table_() {
}

PhysicalPortEntry::PhysicalPortEntry(PhysicalPortTable *table,
        const std::string &name) : OvsdbEntry(table), name_(name),
    binding_table_(), ovs_binding_table_() {
}

PhysicalPortEntry::~PhysicalPortEntry() {
}

bool PhysicalPortEntry::IsLess(const KSyncEntry &entry) const {
    const PhysicalPortEntry &ps_entry =
        static_cast<const PhysicalPortEntry&>(entry);
    return (name_.compare(ps_entry.name_) < 0);
}

KSyncEntry *PhysicalPortEntry::UnresolvedReference() {
    return NULL;
}

void PhysicalPortEntry::Encode(struct ovsdb_idl_txn *txn) {
    if (GetState() == KSyncEntry::TEMP || IsDeleted()) {
        /*
         * we can only modify the vlan bindings in physical port
         * table as we don't own the table, we are not suppose to create
         * a new port entry in the table, so return from here if entry is
         * marked temporary or deleted.
         */
        return;
    }
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

const PhysicalPortEntry::VlanLSTable &
PhysicalPortEntry::ovs_binding_table() const {
    return ovs_binding_table_;
}

const PhysicalPortEntry::VlanStatsTable &
PhysicalPortEntry::stats_table() const {
    return stats_table_;
}

void PhysicalPortEntry::OverrideOvs() {
    struct ovsdb_idl_txn *txn = table_->client_idl()->CreateTxn(this);
    Encode(txn);
    struct jsonrpc_msg *msg = ovsdb_wrapper_idl_txn_encode(txn);
    if (msg == NULL) {
        table_->client_idl()->DeleteTxn(txn);
        return;
    }
    table_->client_idl()->SendJsonRpc(msg);
}

PhysicalPortTable::PhysicalPortTable(OvsdbClientIdl *idl) :
    OvsdbObject(idl) {
    idl->Register(OvsdbClientIdl::OVSDB_PHYSICAL_PORT,
                  boost::bind(&PhysicalPortTable::Notify, this, _1, _2));
}

PhysicalPortTable::~PhysicalPortTable() {
    client_idl_->UnRegister(OvsdbClientIdl::OVSDB_PHYSICAL_PORT);
}

void PhysicalPortTable::Notify(OvsdbClientIdl::Op op,
        struct ovsdb_idl_row *row) {
    bool override_ovs = false;
    PhysicalPortEntry key(this, ovsdb_wrapper_physical_port_name(row));
    PhysicalPortEntry *entry = static_cast<PhysicalPortEntry *>(FindActiveEntry(&key));
    if (op == OvsdbClientIdl::OVSDB_DEL) {
        OVSDB_TRACE(Trace, "Delete of Physical Port " +
                std::string(ovsdb_wrapper_physical_port_name(row)));
        if (entry != NULL) {
            Delete(entry);
        }
    } else if (op == OvsdbClientIdl::OVSDB_ADD) {
        if (entry == NULL) {
            OVSDB_TRACE(Trace, "Add/Change of Physical Port " +
                    std::string(ovsdb_wrapper_physical_port_name(row)));
            entry = static_cast<PhysicalPortEntry *>(Create(&key));
            entry->ovs_entry_ = row;
        }
        PhysicalPortEntry::VlanLSTable old(entry->ovs_binding_table_);
        std::size_t count = ovsdb_wrapper_physical_port_vlan_binding_count(row);
        struct ovsdb_wrapper_port_vlan_binding new_bind[count];
        ovsdb_wrapper_physical_port_vlan_binding(row, new_bind);
        for (std::size_t i = 0; i < count; i++) {
            if (entry->binding_table_.find(new_bind[i].vlan) ==
                    entry->binding_table_.end()) {
                /*
                 * Entries present which are not owned by us,
                 * write port transaction to override the value.
                 */
                 override_ovs = true;
                continue;
            }
            LogicalSwitchEntry key(client_idl_->logical_switch_table(),
                    ovsdb_wrapper_logical_switch_name(new_bind[i].ls));
            LogicalSwitchEntry *ls_entry =
                static_cast<LogicalSwitchEntry *>(
                        client_idl_->logical_switch_table()->Find(&key));
            entry->ovs_binding_table_[new_bind[i].vlan] = ls_entry;
            old.erase(new_bind[i].vlan);
        }
        count = ovsdb_wrapper_physical_port_vlan_stats_count(row);
        struct ovsdb_wrapper_port_vlan_stats stats[count];
        ovsdb_wrapper_physical_port_vlan_stats(row, stats);
        entry->stats_table_.clear();
        for (std::size_t i = 0; i < count; i++) {
            entry->stats_table_[stats[i].vlan] = stats[i].stats;
        }
        PhysicalPortEntry::VlanLSTable::iterator it = old.begin();
        for ( ; it != old.end(); it++) {
            if (entry->binding_table_.find(it->first) !=
                    entry->binding_table_.end()) {
                /*
                 * Entry owned by us is somehow deleted,
                 * write port transaction to override the value.
                 */
                 override_ovs = true;
            }
            entry->ovs_binding_table_.erase(it->first);
        }
    } else {
        assert(0);
    }
    if (override_ovs)
        entry->OverrideOvs();
}

KSyncEntry *PhysicalPortTable::Alloc(const KSyncEntry *key, uint32_t index) {
    const PhysicalPortEntry *k_entry =
        static_cast<const PhysicalPortEntry *>(key);
    PhysicalPortEntry *entry = new PhysicalPortEntry(this, k_entry->name_);
    return entry;
}

/////////////////////////////////////////////////////////////////////////////
// Sandesh routines
/////////////////////////////////////////////////////////////////////////////
class PhysicalPortSandeshTask : public Task {
public:
    PhysicalPortSandeshTask(std::string resp_ctx) :
        Task((TaskScheduler::GetInstance()->GetTaskId("Agent::KSync")), -1),
        resp_(new OvsdbPhysicalPortResp()), resp_data_(resp_ctx) {
    }
    virtual ~PhysicalPortSandeshTask() {}
    virtual bool Run() {
        std::vector<OvsdbPhysicalPortEntry> port_list;
        TorAgentInit *init =
            static_cast<TorAgentInit *>(Agent::GetInstance()->agent_init());
        OvsdbClientSession *session = init->ovsdb_client()->next_session(NULL);
        PhysicalPortTable *table =
            session->client_idl()->physical_port_table();
        PhysicalPortEntry *entry =
            static_cast<PhysicalPortEntry *>(table->Next(NULL));
        while (entry != NULL) {
            OvsdbPhysicalPortEntry pentry;
            pentry.set_state(entry->StateString());
            pentry.set_name(entry->name());
            const PhysicalPortEntry::VlanLSTable &bindings =
                entry->ovs_binding_table();
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
            port_list.push_back(pentry);
            entry = static_cast<PhysicalPortEntry *>(table->Next(entry));
        }
        resp_->set_port(port_list);
        SendResponse();
        return true;
    }
private:
    void SendResponse() {
        resp_->set_context(resp_data_);
        resp_->set_more(false);
        resp_->Response();
    }

    OvsdbPhysicalPortResp *resp_;
    std::string resp_data_;
    DISALLOW_COPY_AND_ASSIGN(PhysicalPortSandeshTask);
};

void OvsdbPhysicalPortReq::HandleRequest() const {
    PhysicalPortSandeshTask *task = new PhysicalPortSandeshTask(context());
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    scheduler->Enqueue(task);
}

