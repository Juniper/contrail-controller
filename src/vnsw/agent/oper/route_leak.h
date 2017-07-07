/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */
#ifndef SRC_VNSW_AGENT_OPER_ROUTE_LEAK_H__
#define SRC_VNSW_AGENT_OPER_ROUTE_LEAK_H__

struct RouteLeakState :  public DBState {
    RouteLeakState(Agent *agent, VrfEntry *vrf):
        agent_(agent), dest_vrf_(vrf) {}

    void AddRoute(const AgentRoute *route);
    void DeleteRoute(const AgentRoute *route);

    void set_dest_vrf(VrfEntry *vrf) {
        dest_vrf_ = vrf;
    }

    VrfEntry* dest_vrf() const {
        return dest_vrf_.get();
    }

private:
    void AddIndirectRoute(const AgentRoute *route);
    void AddInterfaceRoute(const AgentRoute *route);
    Agent *agent_;
    VrfEntryRef dest_vrf_;
};

class RouteLeakVrfState : public DBState {
public:
    RouteLeakVrfState(VrfEntry *source_vrf, VrfEntry *dest_vrf);
    ~RouteLeakVrfState();
    //Route Notify handler
    void Notify(DBTablePartBase *partition, DBEntryBase *e);
    void Delete();
    void SetDestVrf(VrfEntry *dest_vrf);

    VrfEntry* dest_vrf() {
        return dest_vrf_.get();
    }

private:
    void WalkDoneInternal(DBTableBase *part);
    bool WalkCallBack(DBTablePartBase *partition, DBEntryBase *entry);
    //VRF from which routes have to be sourced
    VrfEntryRef source_vrf_;
    //VRF to which routes have to be mirrored
    VrfEntryRef dest_vrf_;
    DBTableBase::ListenerId route_listener_id_;
    DBTable::DBTableWalkRef walk_ref_;
    bool deleted_;
};

//Base class registering to VRF table.
//If route have to be leaked from one VRF to another takes case of it.
class RouteLeakManager {
public:
    RouteLeakManager(Agent *agent);
    ~RouteLeakManager();

    Agent *agent() {
        return agent_;
    }

    //VRF notify handler
    void Notify(DBTablePartBase *partition, DBEntryBase *e);

private:
    Agent *agent_;
    DBTableBase::ListenerId vrf_listener_id_;
};
#endif
