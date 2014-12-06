/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_VNSW_AGENT_OVS_TOR_AGENT_OVSDB_CLIENT_PHYSICAL_SWITCH_OVSDB_H_
#define SRC_VNSW_AGENT_OVS_TOR_AGENT_OVSDB_CLIENT_PHYSICAL_SWITCH_OVSDB_H_

#include <ovsdb_entry.h>
#include <ovsdb_object.h>

namespace OVSDB {
class PhysicalSwitchTable : public OvsdbObject {
public:
    PhysicalSwitchTable(OvsdbClientIdl *idl);
    virtual ~PhysicalSwitchTable();

    void Notify(OvsdbClientIdl::Op, struct ovsdb_idl_row *);
    KSyncEntry *Alloc(const KSyncEntry *key, uint32_t index);
private:
    DISALLOW_COPY_AND_ASSIGN(PhysicalSwitchTable);
};

class PhysicalSwitchEntry : public OvsdbEntry {
public:
    enum Trace {
        ADD,
        DEL,
    };
    PhysicalSwitchEntry(PhysicalSwitchTable *table, const std::string &name);
    ~PhysicalSwitchEntry();

    Ip4Address &tunnel_ip();
    const std::string &name();
    void set_tunnel_ip(std::string ip);
    bool IsLess(const KSyncEntry&) const;
    std::string ToString() const {return "Physical Switch";}
    KSyncEntry* UnresolvedReference();
private:
    void SendTrace(Trace event) const;

    friend class PhysicalSwitchTable;
    std::string name_;
    Ip4Address tunnel_ip_;
    DISALLOW_COPY_AND_ASSIGN(PhysicalSwitchEntry);
};
};

#endif //SRC_VNSW_AGENT_OVS_TOR_AGENT_OVSDB_CLIENT_PHYSICAL_SWITCH_OVSDB_H_

