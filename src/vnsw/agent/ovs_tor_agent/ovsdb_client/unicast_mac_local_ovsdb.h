/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_VNSW_AGENT_OVS_TOR_AGENT_OVSDB_CLIENT_OVSDB_ROUTE_H_
#define SRC_VNSW_AGENT_OVS_TOR_AGENT_OVSDB_CLIENT_OVSDB_ROUTE_H_

#include <ovsdb_entry.h>
#include <ovsdb_object.h>
class OvsPeer;

namespace OVSDB {
class UnicastMacLocalEntry;
class UnicastMacLocalOvsdb : public OvsdbObject {
public:
    typedef std::pair<VrfEntry *, UnicastMacLocalEntry *> VrfDepEntry;
    typedef std::set<VrfDepEntry> VrfDepList;

    UnicastMacLocalOvsdb(OvsdbClientIdl *idl, OvsPeer *peer);
    ~UnicastMacLocalOvsdb();

    OvsPeer *peer();
    void Notify(OvsdbClientIdl::Op op, struct ovsdb_idl_row *row);
    KSyncEntry *Alloc(const KSyncEntry *key, uint32_t index);

    void VrfReEvalEnqueue(VrfEntry *vrf);
    bool VrfReEval(VrfEntryRef vrf);

    bool IsVrfReEvalQueueActive() const;

private:
    friend class UnicastMacLocalEntry;

    // delete table triggered callback
    void DeleteTableDone(void);

    OvsPeer *peer_;
    VrfDepList vrf_dep_list_;
    WorkQueue<VrfEntryRef> *vrf_reeval_queue_;
    DISALLOW_COPY_AND_ASSIGN(UnicastMacLocalOvsdb);
};

class UnicastMacLocalEntry : public OvsdbEntry {
public:
    UnicastMacLocalEntry(UnicastMacLocalOvsdb *table,
            const std::string &logical_switch, const std::string &mac);
    UnicastMacLocalEntry(UnicastMacLocalOvsdb *table,
            const UnicastMacLocalEntry *key);
    UnicastMacLocalEntry(UnicastMacLocalOvsdb *table,
            struct ovsdb_idl_row *row);
    ~UnicastMacLocalEntry();

    bool Add();
    bool Delete();
    bool IsLess(const KSyncEntry&) const;
    KSyncEntry* UnresolvedReference();
    std::string ToString() const {return "Unicast Mac Local";}

    const std::string &mac() const;
    const std::string &logical_switch_name() const;
    const std::string &dest_ip() const;

private:
    friend class UnicastMacLocalOvsdb;
    std::string mac_;
    std::string logical_switch_name_;
    std::string dest_ip_;
    // take reference to the vrf while exporting route, to assure sanity
    // of vrf pointer even if Add route request fails, due to any reason
    VrfEntryRef vrf_;
    uint32_t vxlan_id_;
    DISALLOW_COPY_AND_ASSIGN(UnicastMacLocalEntry);
};

};

#endif //SRC_VNSW_AGENT_OVS_TOR_AGENT_OVSDB_CLIENT_OVSDB_ROUTE_H_

