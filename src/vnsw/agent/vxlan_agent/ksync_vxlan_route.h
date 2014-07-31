/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_ksync_vxlan_route_h
#define vnsw_agent_ksync_vxlan_route_h

class KSyncVxlanRouteObject;
class KSyncVxlanVrfObject;

/****************************************************************************
 * File contains KSync objects to manage FDB Route table for Vxlan bridge.
 ****************************************************************************/
  
/****************************************************************************
 * In Contrail, each Virtual-Network defines a broadcast domain. Each
 * Virtual-Network has a VRF associated with it. The FDB Table for a
 * Virtual-Network is got from the VRF for it.
 *
 * KSyncVxlanVrfObject listens to VRF entries being created and in-turn 
 * create a KSyncVxlanRouteObject for Layer2 Route Table in the VRF
 ****************************************************************************/
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

    // Virtual Method to allocate KSync Object KSyncVxlanRouteObject for every
    // Layer2 Route Table.
    // This API is invoked when Vxlan Agent sees Layer2 Route Table in a new VRF
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

/****************************************************************************
 * KSync Object to manage Vxlan FDB Table. A KSyncVxlanRouteObject is created
 * for every Layer2 Route Table. 
 *
 * Registers as client to Layer2 Route DBTable
 ****************************************************************************/
class KSyncVxlanRouteObject : public KSyncDBObject {
public:
    KSyncVxlanRouteObject(KSyncVxlanVrfObject *vrf, AgentRouteTable *rt_table);
    virtual ~KSyncVxlanRouteObject();

    // Method to allocate the inherited KSyncVxlanFdbEntry
    // Must populate the KSyncEntry from DBEntry passed
    // DBEntry will be of type Layer2RouteEntry
    virtual KSyncEntry *DBToKSyncEntry(const DBEntry *e) = 0;

    // Method to allocate the inherited KSyncVxlanFdbEntry
    // The fields in "key" must be copied into newly allocated object
    virtual KSyncEntry *Alloc(const KSyncEntry *key, uint32_t index) = 0;

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

/****************************************************************************
 * An abstract class for all Route Entries.
 * Can potentially be used later to support Inet Unicast/Multicast route tables
 ****************************************************************************/
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

/****************************************************************************
 * KSync representation of every FDB entry in a Vxlan Bridge
 *
 * A KSyncVxlanFdbEntry is created for every entry in Layer2 Route Table
 ****************************************************************************/
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

    // Virtual method invoked when a FDB entry is to be created
    virtual bool Add() = 0;
    // Virtual method invoked when a FDB entry is to be modified
    virtual bool Change() = 0;
    // Virtual method invoked when a FDB entry is to be Deleted
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

#endif // vnsw_agent_ksync_vxlan_route_h
