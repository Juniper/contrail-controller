/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_ksync_vxlan_route_h
#define vnsw_agent_ksync_vxlan_route_h

class KSyncVxlanRouteObject;
class KSyncVxlanVrfObject;

class KSyncVxlanRouteEntry : public KSyncDBEntry {
public:
    KSyncVxlanRouteEntry(KSyncVxlanRouteObject *obj,
                         const KSyncVxlanRouteEntry *entry);
    KSyncVxlanRouteEntry(KSyncVxlanRouteObject *obj, const AgentRoute *route);
    virtual ~KSyncVxlanRouteEntry();

    virtual bool IsLess(const KSyncEntry &rhs) const;
    virtual bool CompareRoute(const KSyncVxlanRouteEntry &rhs) const = 0;

    KSyncDBObject *GetObject();
    uint32_t vrf_id() const { return vrf_id_; }
    KSyncVxlanRouteObject *ksync_object() const { return ksync_obj_; }
private:
    uint32_t vrf_id_;
    KSyncVxlanRouteObject *ksync_obj_;
    DISALLOW_COPY_AND_ASSIGN(KSyncVxlanRouteEntry);
};

class KSyncVxlanFdbEntry : public KSyncVxlanRouteEntry {
public:
    KSyncVxlanFdbEntry(KSyncVxlanRouteObject *obj,
                       const KSyncVxlanFdbEntry *entry);
    KSyncVxlanFdbEntry(KSyncVxlanRouteObject *obj,
                       const Layer2RouteEntry *route);
    virtual ~KSyncVxlanFdbEntry();

    virtual bool CompareRoute(const KSyncVxlanRouteEntry &rhs) const;
    virtual std::string ToString() const;
    virtual bool Sync(DBEntry *e);
    virtual KSyncEntry *UnresolvedReference();

    virtual bool Add() = 0;
    virtual bool Change() = 0;
    virtual bool Delete() = 0;

    const struct ether_addr &mac() const { return mac_; }
    const KSyncVxlanBridgeEntry *bridge() const { return bridge_; }
    const KSyncVxlanPortEntry *port() const { return port_; }
    const Ip4Address &tunnel_dest() const { return tunnel_dest_; }
private:
    KSyncVxlanBridgeEntry *bridge_;
    struct ether_addr mac_;
    KSyncVxlanPortEntry *port_;
    Ip4Address tunnel_dest_;
    DISALLOW_COPY_AND_ASSIGN(KSyncVxlanFdbEntry);
};

class KSyncVxlanRouteObject : public KSyncDBObject {
public:
    KSyncVxlanRouteObject(KSyncVxlanVrfObject *vrf, AgentRouteTable *rt_table);
    virtual ~KSyncVxlanRouteObject();

    virtual KSyncEntry *Alloc(const KSyncEntry *entry, uint32_t index) = 0;
    virtual KSyncEntry *DBToKSyncEntry(const DBEntry *e) = 0;

    void ManagedDelete();
    void Unregister();
    virtual void EmptyTable();

    KSyncVxlan *ksync() const { return ksync_; }
    const AgentRouteTable *route_table() const { return rt_table_; }
private:
    KSyncVxlan *ksync_;
    bool marked_delete_;
    AgentRouteTable *rt_table_;
    LifetimeRef<KSyncVxlanRouteObject> table_delete_ref_;
    DISALLOW_COPY_AND_ASSIGN(KSyncVxlanRouteObject);
};


class KSyncVxlanVrfObject {
public:
    typedef std::map<uint32_t, KSyncVxlanRouteObject *> VrfRouteObjectMap;
    struct VrfState : DBState {
        VrfState() : DBState(), seen_(false) {};
        bool seen_;
    };

    KSyncVxlanVrfObject(KSyncVxlan *ksync);
    virtual ~KSyncVxlanVrfObject();

    void Init();
    void Shutdown();
    void RegisterDBClients();

    virtual KSyncVxlanRouteObject *AllocLayer2RouteTable(const VrfEntry *entry)
        = 0;
    KSyncVxlan *ksync() const { return ksync_; }

    void VrfNotify(DBTablePartBase *partition, DBEntryBase *e);
    void AddToVrfMap(uint32_t vrf_id, KSyncVxlanRouteObject *);
    void DelFromVrfMap(KSyncVxlanRouteObject *);
    KSyncVxlanRouteObject *GetRouteKSyncObject(uint32_t vrf_id) const;
private:
    KSyncVxlan *ksync_;
    DBTableBase::ListenerId vrf_listener_id_;
    VrfRouteObjectMap vrf_fdb_object_map_;
    DISALLOW_COPY_AND_ASSIGN(KSyncVxlanVrfObject);
};

#endif // vnsw_agent_ksync_vxlan_route_h
