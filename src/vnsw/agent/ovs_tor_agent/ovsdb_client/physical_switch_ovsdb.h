/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_VNSW_AGENT_OVS_TOR_AGENT_OVSDB_CLIENT_PHYSICAL_SWITCH_OVSDB_H_
#define SRC_VNSW_AGENT_OVS_TOR_AGENT_OVSDB_CLIENT_PHYSICAL_SWITCH_OVSDB_H_

#include <ovsdb_entry.h>
#include <ovsdb_object.h>

namespace OVSDB {
class PhysicalSwitchEntry;

class PhysicalSwitchTable : public OvsdbObject {
public:
    PhysicalSwitchTable(OvsdbClientIdl *idl);
    virtual ~PhysicalSwitchTable();

    void Notify(OvsdbClientIdl::Op, struct ovsdb_idl_row *);
    KSyncEntry *Alloc(const KSyncEntry *key, uint32_t index);

    void StartUpdatePorts();

private:
    void UpdatePorts(PhysicalSwitchEntry *entry);

    // By default update ports is set to false to avoid updation
    // of physical port entries in ovsdb client context, which
    // helps to ignore partial updates on physical ports from
    // ovsdb library to avoid useless processing on half cooked
    // information. once the initial monitor request processing
    // is complete OvsdbClientIdl triggers StartUpdatePorts to
    // update the pending ports and allow further ports updation
    bool update_ports_;
    DISALLOW_COPY_AND_ASSIGN(PhysicalSwitchTable);
};

class PhysicalSwitchEntry : public OvsdbEntry {
public:
    typedef std::set<struct ovsdb_idl_row *> InterfaceList;

    enum Trace {
        ADD,
        DEL,
    };

    PhysicalSwitchEntry(PhysicalSwitchTable *table, const std::string &name);
    ~PhysicalSwitchEntry();

    bool Add();
    bool Delete();
    Ip4Address &tunnel_ip();
    const std::string &name();
    void set_tunnel_ip(std::string ip);
    bool IsLess(const KSyncEntry&) const;
    std::string ToString() const {return "Physical Switch";}
    KSyncEntry* UnresolvedReference();

private:
    friend class PhysicalSwitchTable;
    void SendTrace(Trace event) const;

    std::string name_;
    Ip4Address tunnel_ip_;
    InterfaceList intf_list_;
    DISALLOW_COPY_AND_ASSIGN(PhysicalSwitchEntry);
};
};

#endif //SRC_VNSW_AGENT_OVS_TOR_AGENT_OVSDB_CLIENT_PHYSICAL_SWITCH_OVSDB_H_

