/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_VNSW_AGENT_OVS_TOR_AGENT_OVSDB_CLIENT_PHYSICAL_LOCATOR_OVSDB_H_
#define SRC_VNSW_AGENT_OVS_TOR_AGENT_OVSDB_CLIENT_PHYSICAL_LOCATOR_OVSDB_H_

#include <ovsdb_entry.h>
#include <ovsdb_object.h>

namespace OVSDB {
class PhysicalLocatorTable : public OvsdbObject {
public:
    PhysicalLocatorTable(OvsdbClientIdl *idl);
    virtual ~PhysicalLocatorTable();

    void OvsdbNotify(OvsdbClientIdl::Op, struct ovsdb_idl_row *);
    KSyncEntry *Alloc(const KSyncEntry *key, uint32_t index);

private:
    DISALLOW_COPY_AND_ASSIGN(PhysicalLocatorTable);
};

class PhysicalLocatorEntry : public OvsdbEntry {
public:
    PhysicalLocatorEntry(PhysicalLocatorTable *table,
            const std::string &dip_str);
    virtual ~PhysicalLocatorEntry();

    bool IsLess(const KSyncEntry&) const;
    std::string ToString() const {return "Physical Locator";}
    KSyncEntry* UnresolvedReference();
    bool AcquireCreateRequest(KSyncEntry *creator);
    void ReleaseCreateRequest(KSyncEntry *creator);
private:

    friend class PhysicalLocatorTable;
    std::string dip_;
    // KSync Entry trying to create physical locator
    KSyncEntry *create_req_owner_;
    DISALLOW_COPY_AND_ASSIGN(PhysicalLocatorEntry);
};
};  // namespace OVSDB

#endif  // SRC_VNSW_AGENT_OVS_TOR_AGENT_OVSDB_CLIENT_PHYSICAL_LOCATOR_OVSDB_H_

