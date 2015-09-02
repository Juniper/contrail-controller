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
 * KSync VxLan defines datamodel for port of agent to platforms supporting
 * Linux Kernel like VxLan Bridges. Note, the code in this directory does not
 * program the datapath and only creates tables according to the datamodel
 * in Vxlan Linux Kernel.
 *
 * KSyncVxlan is the top level object. It drives creation of following
 * objects,
 *
 * - KSyncVxlanBridgeObject : KSync Object for VxLan Bridges
 * - KSyncVxlanPortObject : KSync Object for ports in VxLan Bridges
 * - KSyncVxlanVrfObject : KSync Object for every VRF in agent oper-db. VRF
 *   entries in turn will contain FDB tables that must be programmed to
 *   Vxlan Bridges
 *
 * The classes defined in this directoy must be inherited according to the
 * platform. Programming of dataplane should be done in the inherited class
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
