/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_VNSW_AGENT_OVS_TOR_AGENT_OVSDB_CLIENT_PHYSICAL_PORT_OVSDB_H_
#define SRC_VNSW_AGENT_OVS_TOR_AGENT_OVSDB_CLIENT_PHYSICAL_PORT_OVSDB_H_

#include <ovsdb_entry.h>
#include <ovsdb_object.h>

namespace OVSDB {
class PhysicalPortEntry;

class PhysicalPortTable : public OvsdbObject {
public:
    typedef std::map<struct ovsdb_idl_row *, PhysicalPortEntry *> IdlEntryMap;
    PhysicalPortTable(OvsdbClientIdl *idl);
    virtual ~PhysicalPortTable();

    void Notify(OvsdbClientIdl::Op, struct ovsdb_idl_row *);
    KSyncEntry *Alloc(const KSyncEntry *key, uint32_t index);

    void CreatePortEntry(struct ovsdb_idl_row *row,
                         const std::string &physical_device);
    PhysicalPortEntry *FindPortEntry(struct ovsdb_idl_row *row);
    void DeletePortEntry(struct ovsdb_idl_row *row);

    void set_stale_create_done();

private:
    void EntryOvsdbUpdate(PhysicalPortEntry *entry);

    IdlEntryMap idl_entry_map_;
    bool stale_create_done_;
    DISALLOW_COPY_AND_ASSIGN(PhysicalPortTable);
};

class PhysicalPortEntry : public OvsdbEntry {
public:
    typedef std::map<uint32_t, LogicalSwitchEntry *> VlanLSTable;
    typedef std::map<uint32_t, struct ovsdb_idl_row *> VlanStatsTable;
    PhysicalPortEntry(PhysicalPortTable *table, const std::string &dev_name,
                      const std::string &name);
    ~PhysicalPortEntry();

    virtual bool Add();
    virtual bool Change();
    virtual bool Delete();

    bool IsLess(const KSyncEntry&) const;
    std::string ToString() const {return "Physical Port";}
    KSyncEntry* UnresolvedReference();
    void TriggerUpdate();
    void AddBinding(int16_t vlan, LogicalSwitchEntry *ls);
    void DeleteBinding(int16_t vlan, LogicalSwitchEntry *ls);

    const std::string &name() const;
    const std::string &dev_name() const;
    const VlanLSTable &binding_table() const;
    const VlanLSTable &ovs_binding_table() const;
    const VlanStatsTable &stats_table() const;

private:
    friend class PhysicalPortTable;
    void Encode(struct ovsdb_idl_txn *);
    bool OverrideOvs();
    std::string name_;
    std::string dev_name_;
    VlanLSTable binding_table_;
    VlanLSTable ovs_binding_table_;
    VlanStatsTable stats_table_;
    DISALLOW_COPY_AND_ASSIGN(PhysicalPortEntry);
};
};

#endif //SRC_VNSW_AGENT_OVS_TOR_AGENT_OVSDB_CLIENT_PHYSICAL_PORT_OVSDB_H_

