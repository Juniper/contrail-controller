/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_VNSW_AGENT_OVS_TOR_AGENT_OVSDB_CLIENT_OVSDB_RESOURCE_VXLAN_ID_H_
#define SRC_VNSW_AGENT_OVS_TOR_AGENT_OVSDB_CLIENT_OVSDB_RESOURCE_VXLAN_ID_H_

#include <cmn/index_vector.h>

namespace OVSDB {
class OvsdbResourceVxLanIdCmp;
class OvsdbResourceVxLanIdTable;

class OvsdbResourceVxLanId {
public:
    OvsdbResourceVxLanId(OvsdbResourceVxLanIdTable *table,
                         KSyncEntry *entry);
    virtual ~OvsdbResourceVxLanId();

    bool AcquireVxLanId(uint32_t vxlan_id);
    void ReleaseVxLanId(bool active);
    void set_active_vxlan_id(uint32_t vxlan_id);

    uint32_t VxLanId() const;
    uint32_t active_vxlan_id() const;

private:
    friend class OvsdbResourceVxLanIdCmp;
    friend class OvsdbResourceVxLanIdTable;
    OvsdbResourceVxLanIdTable *table_;
    KSyncEntry *entry_;
    uint32_t resource_id_;
    uint32_t vxlan_id_;
    uint32_t active_vxlan_id_;
    DISALLOW_COPY_AND_ASSIGN(OvsdbResourceVxLanId);
};  // class OvsdbResourceVxLanId

class OvsdbResourceVxLanIdCmp {
public:
    bool operator() (OvsdbResourceVxLanId* const lhs,
                     OvsdbResourceVxLanId* const rhs) const {
        if (lhs->resource_id_ != rhs->resource_id_)
            return lhs->resource_id_ < rhs->resource_id_;
        return lhs->entry_ < rhs->entry_;
    }
};

class OvsdbResourceVxLanIdTable {
public:
    OvsdbResourceVxLanIdTable();
    virtual ~OvsdbResourceVxLanIdTable();

private:
    friend class OvsdbResourceVxLanId;
    typedef std::set<OvsdbResourceVxLanId*,
                     OvsdbResourceVxLanIdCmp> ResourcePendingList;

    bool AcquireVxLanId(OvsdbResourceVxLanId *entry, uint32_t vxlan_id);

    void ReleaseVxLanId(OvsdbResourceVxLanId *entry, uint32_t vxlan_id,
                        uint32_t resource_id);

    struct ResourceEntry {
        ResourceEntry() : active_entry(NULL), resource_id_count_(0) {
        }
        OvsdbResourceVxLanId *active_entry;
        ResourcePendingList pending_list;
        uint32_t resource_id_count_;
    };

    std::map<uint32_t, ResourceEntry*> vxlan_table_;
    DISALLOW_COPY_AND_ASSIGN(OvsdbResourceVxLanIdTable);
};  // class OvsdbResourceVxLanIdTable
};  // namespace OVSDB

#endif //SRC_VNSW_AGENT_OVS_TOR_AGENT_OVSDB_CLIENT_OVSDB_RESOURCE_VXLAN_ID_H_

