/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_VNSW_AGENT_OVS_TOR_AGENT_OVSDB_CLIENT_OVSDB_MC_ROUTE_H_
#define SRC_VNSW_AGENT_OVS_TOR_AGENT_OVSDB_CLIENT_OVSDB_MC_ROUTE_H_

#include <ovsdb_entry.h>
#include <ovsdb_object.h>
class OvsPeer;

namespace OVSDB {
class MulticastMacLocalEntry;
class MulticastMacLocalOvsdb : public OvsdbObject {
public:
    typedef std::pair<VrfEntry *, MulticastMacLocalEntry *> VrfDepEntry;
    typedef std::set<VrfDepEntry> VrfDepList;

    MulticastMacLocalOvsdb(OvsdbClientIdl *idl, OvsPeer *peer);
    ~MulticastMacLocalOvsdb();

    OvsPeer *peer();
    void Notify(OvsdbClientIdl::Op op, struct ovsdb_idl_row *row);
    KSyncEntry *Alloc(const KSyncEntry *key, uint32_t index);

    void VrfReEvalEnqueue(VrfEntry *vrf);
    bool VrfReEval(VrfEntryRef vrf);

private:
    friend class MulticastMacLocalEntry;
    OvsPeer *peer_;
    VrfDepList vrf_dep_list_;
    WorkQueue<VrfEntryRef> *vrf_reeval_queue_;
    DISALLOW_COPY_AND_ASSIGN(MulticastMacLocalOvsdb);
};

class MulticastMacLocalEntry : public OvsdbEntry {
public:
    MulticastMacLocalEntry(MulticastMacLocalOvsdb *table,
            const MulticastMacLocalEntry *key);
    MulticastMacLocalEntry(MulticastMacLocalOvsdb *table,
            struct ovsdb_idl_row *row);
    ~MulticastMacLocalEntry();

    bool Add();
    bool Change();
    bool Delete();
    bool IsLess(const KSyncEntry&) const;
    KSyncEntry* UnresolvedReference();
    std::string ToString() const {return "Multicast Mac Local";}

    const std::string &mac() const;
    const std::string &logical_switch_name() const;
    const std::string &dip_str() const {return dip_str_;}

private:
    friend class MulticastMacLocalOvsdb;
    std::string mac_;
    std::string logical_switch_name_;
    // take reference to the vrf while exporting route, to assure sanity
    // of vrf pointer even if Add route request fails, due to any reason
    VrfEntryRef vrf_;
    uint32_t vxlan_id_;
    std::string dip_str_;
    DISALLOW_COPY_AND_ASSIGN(MulticastMacLocalEntry);
};

};

#endif //SRC_VNSW_AGENT_OVS_TOR_AGENT_OVSDB_CLIENT_OVSDB_MC_ROUTE_H_

