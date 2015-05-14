/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_VNSW_AGENT_OVS_TOR_AGENT_OVSDB_CLIENT_MULTICAST_MAC_LOCAL_OVSDB_H_ 
#define SRC_VNSW_AGENT_OVS_TOR_AGENT_OVSDB_CLIENT_MULTICAST_MAC_LOCAL_OVSDB_H_ 

#include <ovsdb_entry.h>
#include <ovsdb_object.h>
class OvsPeer;

namespace OVSDB {
class VnOvsdbEntry;

class LogicalSwitchEntry;
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
                           const LogicalSwitchEntry *logical_switch);

    bool Add();
    bool Change();
    bool Delete();
    bool IsLess(const KSyncEntry&) const;
    KSyncEntry* UnresolvedReference();
    std::string ToString() const {return "Multicast Mac Local";}

    const std::string &mac() const;
    const std::string &logical_switch_name() const;
    const LogicalSwitchEntry *logical_switch() {return logical_switch_;}
    const uint32_t vxlan_id() const {return vxlan_id_;}
    OVSDB::VnOvsdbEntry *GetVnEntry() const;

private:
    void OnVrfDelete();
    void EvaluateVrfDependency(VrfEntry *vrf);

    friend class MulticastMacLocalOvsdb;
    std::string logical_switch_name_;
    // take reference to the vrf while exporting route, to assure sanity
    // of vrf pointer even if Add route request fails, due to any reason
    VrfEntryRef vrf_;
    uint32_t vxlan_id_;
    const LogicalSwitchEntry *logical_switch_;
    DISALLOW_COPY_AND_ASSIGN(MulticastMacLocalEntry);
};

};

#endif //SRC_VNSW_AGENT_OVS_TOR_AGENT_OVSDB_CLIENT_MULTICAST_MAC_LOCAL_OVSDB_H_

