/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_ksync_linux_fdb_h
#define vnsw_agent_ksync_linux_fdb_h

class KSyncLinuxVrfObject;
class KSyncLinuxFdbObject;

class KSyncLinuxFdbEntry : public KSyncVxlanFdbEntry {
public:
    KSyncLinuxFdbEntry(KSyncLinuxFdbObject *obj, const Layer2RouteEntry *route);
    KSyncLinuxFdbEntry(KSyncLinuxFdbObject *obj,
                       const KSyncLinuxFdbEntry *entry);
    virtual ~KSyncLinuxFdbEntry();

    bool Add();
    bool Change();
    bool Delete();
private:
    DISALLOW_COPY_AND_ASSIGN(KSyncLinuxFdbEntry);
};

class KSyncLinuxFdbObject : public KSyncVxlanRouteObject {
public:
    KSyncLinuxFdbObject(KSyncLinuxVrfObject *vrf, AgentRouteTable *rt_table);
    virtual ~KSyncLinuxFdbObject();

    KSyncEntry *Alloc(const KSyncEntry *entry, uint32_t index);
    KSyncEntry *DBToKSyncEntry(const DBEntry *e);
private:
    DISALLOW_COPY_AND_ASSIGN(KSyncLinuxFdbObject);
};

class KSyncLinuxVrfObject : public KSyncVxlanVrfObject {
public:
    KSyncLinuxVrfObject(KSyncLinuxVxlan *ksync);
    virtual ~KSyncLinuxVrfObject();

    KSyncVxlanRouteObject *AllocLayer2RouteTable(const VrfEntry *entry);
private:
    DISALLOW_COPY_AND_ASSIGN(KSyncLinuxVrfObject);
};

#endif // vnsw_agent_ksync_linux_fdb_h
