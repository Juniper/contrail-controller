/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_ksync_vxlan_h
#define vnsw_agent_ksync_vxlan_h

class Agent;

class KSyncVxlanBridgeObject;
class KSyncVxlanBridgeEntry;
class KSyncVxlanPortObject;
class KSyncVxlanPortEntry;
class KSyncVxlanVrfObject;
class KSyncVxlanRouteEntry;
class KSyncVxlanRouteTable;

/**************************************************************************
 * Top level KSync Vxlan object
 **************************************************************************/
class KSyncVxlan {
public:
    KSyncVxlan(Agent *agent);
    virtual ~KSyncVxlan();

    void RegisterDBClients(DB *db);
    void Init();
    void Shutdown();

    KSyncVxlanBridgeObject *bridge_obj() const;
    KSyncVxlanPortObject *port_obj() const;
    KSyncVxlanVrfObject *vrf_obj() const;
    Agent *agent() const { return agent_; }
    static KSyncEntry *defer_entry();
    static void set_defer_entry(KSyncEntry *entry);
protected:
    void set_bridge_obj(KSyncVxlanBridgeObject *obj);
    void set_port_obj(KSyncVxlanPortObject *obj);
    void set_vrf_obj(KSyncVxlanVrfObject *obj);

private:
    Agent *agent_;
    std::auto_ptr<KSyncVxlanBridgeObject> bridge_obj_;
    std::auto_ptr<KSyncVxlanPortObject> port_obj_;
    std::auto_ptr<KSyncVxlanVrfObject> vrf_obj_;
    static KSyncEntry *defer_entry_;
    DISALLOW_COPY_AND_ASSIGN(KSyncVxlan);
};

#endif // vnsw_agent_ksync_vxlan_h
