/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_VNSW_AGENT_OVS_TOR_AGENT_OVSDB_CLIENT_VN_OVSDB_H_
#define SRC_VNSW_AGENT_OVS_TOR_AGENT_OVSDB_CLIENT_VN_OVSDB_H_

#include <ovsdb_entry.h>
#include <ovsdb_object.h>

class VnEntry;

namespace OVSDB {
class VnOvsdbObject : public OvsdbDBObject {
public:
    VnOvsdbObject(OvsdbClientIdl *idl, DBTable *table);
    virtual ~VnOvsdbObject();

    KSyncEntry *Alloc(const KSyncEntry *key, uint32_t index);
    KSyncEntry *DBToKSyncEntry(const DBEntry*);
    DBFilterResp OvsdbDBEntryFilter(const DBEntry *entry,
                                    const OvsdbDBEntry *ovsdb_entry);

private:
    DISALLOW_COPY_AND_ASSIGN(VnOvsdbObject);
};

class VnOvsdbEntry : public OvsdbDBEntry {
public:
    VnOvsdbEntry(VnOvsdbObject *table, const boost::uuids::uuid &uuid);

    // ovs_entry ref is not valid for VN, override IsDataResolved
    // to return always true
    bool IsDataResolved() {return true;}
    void AddMsg(struct ovsdb_idl_txn *);
    void ChangeMsg(struct ovsdb_idl_txn *);
    void DeleteMsg(struct ovsdb_idl_txn *);
    bool Sync(DBEntry*);
    bool IsLess(const KSyncEntry&) const;
    std::string ToString() const {return "Virtual Network Ksync";}
    KSyncEntry* UnresolvedReference();

    VrfEntry *vrf();
    uint32_t vxlan_id() { return vxlan_id_; }
    const std::string &name() { return name_; }

private:
    friend class VnOvsdbObject;
    boost::uuids::uuid uuid_;
    VrfEntryRef vrf_;
    uint32_t vxlan_id_;
    std::string name_;
    DISALLOW_COPY_AND_ASSIGN(VnOvsdbEntry);
};
};

#endif //SRC_VNSW_AGENT_OVS_TOR_AGENT_OVSDB_CLIENT_VN_OVSDB_H_

