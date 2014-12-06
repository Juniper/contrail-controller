/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_VNSW_AGENT_OVS_TOR_AGENT_OVSDB_CLIENT_UNICAST_MAC_REMOTE_OVSDB_H_
#define SRC_VNSW_AGENT_OVS_TOR_AGENT_OVSDB_CLIENT_UNICAST_MAC_REMOTE_OVSDB_H_

#include <base/lifetime.h>
#include <ovsdb_entry.h>
#include <ovsdb_object.h>
#include <ovsdb_client_idl.h>

class Layer2RouteEntry;

namespace OVSDB {
class VrfOvsdbObject;

class UnicastMacRemoteTable : public OvsdbDBObject {
public:
    UnicastMacRemoteTable(OvsdbClientIdl *idl, AgentRouteTable *table);
    virtual ~UnicastMacRemoteTable();

    void OvsdbNotify(OvsdbClientIdl::Op, struct ovsdb_idl_row *);

    KSyncEntry *Alloc(const KSyncEntry *key, uint32_t index);
    KSyncEntry *DBToKSyncEntry(const DBEntry*);
    OvsdbDBEntry *AllocOvsEntry(struct ovsdb_idl_row *row);
    void ManagedDelete();
    void Unregister();
    virtual void EmptyTable();

    void set_deleted(bool deleted);
    bool deleted();

private:
    bool deleted_;
    LifetimeRef<UnicastMacRemoteTable> table_delete_ref_;
    DISALLOW_COPY_AND_ASSIGN(UnicastMacRemoteTable);
};

class UnicastMacRemoteEntry : public OvsdbDBEntry {
public:
    enum Trace {
        ADD_REQ,
        DEL_REQ,
        ADD_ACK,
        DEL_ACK,
    };
    UnicastMacRemoteEntry(OvsdbDBObject *table, const std::string mac,
            const std::string logical_switch);
    UnicastMacRemoteEntry(OvsdbDBObject *table, const Layer2RouteEntry *entry);
    UnicastMacRemoteEntry(OvsdbDBObject *table, const UnicastMacRemoteEntry *key);
    UnicastMacRemoteEntry(OvsdbDBObject *table,
            struct ovsdb_idl_row *entry);

    void PreAddChange();
    void PostDelete();
    void AddMsg(struct ovsdb_idl_txn *);
    void ChangeMsg(struct ovsdb_idl_txn *);
    void DeleteMsg(struct ovsdb_idl_txn *);

    void OvsdbChange();

    bool Sync(DBEntry*);
    bool IsLess(const KSyncEntry&) const;
    std::string ToString() const {return "Unicast Mac Remote";}
    KSyncEntry* UnresolvedReference();

    const std::string &mac() const;
    const std::string &logical_switch_name() const;
    const std::string &dest_ip() const;
    bool self_exported_route() const;

private:
    friend class UnicastMacRemoteTable;
    friend class VrfOvsdbObject;
    void SendTrace(Trace event) const;
    std::string mac_;
    std::string logical_switch_name_;
    std::string dest_ip_;
    bool self_exported_route_;
    KSyncEntryPtr logical_switch_;
    DISALLOW_COPY_AND_ASSIGN(UnicastMacRemoteEntry);
};

class VrfOvsdbObject {
public:
    struct VrfState : DBState {
        std::string logical_switch_name_;
        UnicastMacRemoteTable *l2_table;
    };
    typedef std::map<std::string, VrfState *> LogicalSwitchMap;

    VrfOvsdbObject(OvsdbClientIdl *idl, DBTable *table);
    virtual ~VrfOvsdbObject();

    void OvsdbRouteNotify(OvsdbClientIdl::Op, struct ovsdb_idl_row *);

    void VrfNotify(DBTablePartBase *partition, DBEntryBase *e);
    const LogicalSwitchMap &logical_switch_map() const;

private:
    OvsdbClientIdl *client_idl_;
    DBTable *table_;
    LogicalSwitchMap logical_switch_map_;
    DBTableBase::ListenerId vrf_listener_id_;
    DISALLOW_COPY_AND_ASSIGN(VrfOvsdbObject);
};

};

#endif //SRC_VNSW_AGENT_OVS_TOR_AGENT_OVSDB_CLIENT_UNICAST_MAC_REMOTE_OVSDB_H_

