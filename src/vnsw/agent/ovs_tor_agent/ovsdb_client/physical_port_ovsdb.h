/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_VNSW_AGENT_OVS_TOR_AGENT_OVSDB_CLIENT_PHYSICAL_PORT_OVSDB_H_
#define SRC_VNSW_AGENT_OVS_TOR_AGENT_OVSDB_CLIENT_PHYSICAL_PORT_OVSDB_H_

#include <ovsdb_entry.h>
#include <ovsdb_object.h>

namespace OVSDB {
class PhysicalPortTable : public OvsdbObject {
public:
    PhysicalPortTable(OvsdbClientIdl *idl);
    virtual ~PhysicalPortTable();

    void Notify(OvsdbClientIdl::Op, struct ovsdb_idl_row *);
    KSyncEntry *Alloc(const KSyncEntry *key, uint32_t index);
private:
    DISALLOW_COPY_AND_ASSIGN(PhysicalPortTable);
};

class PhysicalPortEntry : public OvsdbEntry {
public:
    typedef std::map<int, LogicalSwitchEntry *> VlanLSTable;
    typedef std::map<int, struct ovsdb_idl_row *> VlanStatsTable;
    PhysicalPortEntry(PhysicalPortTable *table, const char *name);
    PhysicalPortEntry(PhysicalPortTable *table, const std::string &name);
    ~PhysicalPortEntry();

    bool IsLess(const KSyncEntry&) const;
    std::string ToString() const {return "Physical Port";}
    KSyncEntry* UnresolvedReference();
    void Encode(struct ovsdb_idl_txn *);
    void AddBinding(int16_t vlan, LogicalSwitchEntry *ls);
    void DeleteBinding(int16_t vlan, LogicalSwitchEntry *ls);

    const std::string &name() const;
    const VlanLSTable &ovs_binding_table() const;
    const VlanStatsTable &stats_table() const;

private:
    friend class PhysicalPortTable;
    void OverrideOvs();
    std::string name_;
    struct ovsdb_idl_row *ovs_entry_;
    VlanLSTable binding_table_;
    VlanLSTable ovs_binding_table_;
    VlanStatsTable stats_table_;
    DISALLOW_COPY_AND_ASSIGN(PhysicalPortEntry);
};
};

#endif //SRC_VNSW_AGENT_OVS_TOR_AGENT_OVSDB_CLIENT_PHYSICAL_PORT_OVSDB_H_

