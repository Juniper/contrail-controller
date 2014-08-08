/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_ksync_linux_fdb_h
#define vnsw_agent_ksync_linux_fdb_h

class KSyncLinuxVrfObject;
class KSyncLinuxFdbObject;

/**************************************************************************
 * Implements KSyncVxlanVrfObject for Linux
 **************************************************************************/
class KSyncLinuxVrfObject : public KSyncVxlanVrfObject {
public:
    KSyncLinuxVrfObject(KSyncLinuxVxlan *ksync);
    virtual ~KSyncLinuxVrfObject();

    // Allocates a KSyncVxlanFdbObject for every VRF
    KSyncVxlanRouteObject *AllocLayer2RouteTable(const VrfEntry *entry);
private:
    DISALLOW_COPY_AND_ASSIGN(KSyncLinuxVrfObject);
};

/**************************************************************************
 * Implements KSyncVxlanRouteObject for Linux
 **************************************************************************/
class KSyncLinuxFdbObject : public KSyncVxlanRouteObject {
public:
    KSyncLinuxFdbObject(KSyncLinuxVrfObject *vrf, AgentRouteTable *rt_table);
    virtual ~KSyncLinuxFdbObject();

    // Allocates an entry of type KSyncLinuxFdbEntry from DBEntry of type
    // Layer2RouteEntry
    KSyncEntry *DBToKSyncEntry(const DBEntry *e);
    // Allocate and initialize an entry of type KSyncLinuxFdbEntry
    KSyncEntry *Alloc(const KSyncEntry *entry, uint32_t index);
private:
    DISALLOW_COPY_AND_ASSIGN(KSyncLinuxFdbObject);
};

/**************************************************************************
 * Implements KSyncVxlanFdbEntry for Linux
 **************************************************************************/
class KSyncLinuxFdbEntry : public KSyncVxlanFdbEntry {
public:
    KSyncLinuxFdbEntry(KSyncLinuxFdbObject *obj, const Layer2RouteEntry *route);
    KSyncLinuxFdbEntry(KSyncLinuxFdbObject *obj,
                       const KSyncLinuxFdbEntry *entry);
    virtual ~KSyncLinuxFdbEntry();

    // Creates a FDB entry in Vxlan Bridge
    // Handles FDB entries on local port as well as those reachable on tunnel
    bool Add();
    // Modify a FDB entry in Vxlan Bridge
    bool Change();
    // Modify a FDB entry from Vxlan Bridge
    bool Delete();
private:
    DISALLOW_COPY_AND_ASSIGN(KSyncLinuxFdbEntry);
};
#endif // vnsw_agent_ksync_linux_fdb_h
