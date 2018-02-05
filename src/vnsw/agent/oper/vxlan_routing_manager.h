/*
 * Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __AGENT_OPER_VXLAN_ROUTING_H
#define __AGENT_OPER_VXLAN_ROUTING_H

#include <cmn/agent_cmn.h>
#include <cmn/agent.h>
#include <oper/oper_db.h>

struct VxlanRoutingState : public DBState {
    VxlanRoutingState(VxlanRoutingManager *mgr,
                      VrfEntry *vrf);
    virtual ~VxlanRoutingState();

    DBTable::ListenerId inet4_id_;
    DBTable::ListenerId inet6_id_;
    DBTable::ListenerId evpn_id_;
    AgentRouteTable *inet4_table_;
    AgentRouteTable *inet6_table_;
    AgentRouteTable *evpn_table_;
};

class VxlanRoutingRouteWalker : public AgentRouteWalker {
public:
    VxlanRoutingRouteWalker(const std::string &name,
                            VxlanRoutingManager *mgr,
                            Agent *agent);
    virtual ~VxlanRoutingRouteWalker();

    virtual bool RouteWalkNotify(DBTablePartBase *partition, DBEntryBase *e);

private:
    VxlanRoutingManager *mgr_;
    DISALLOW_COPY_AND_ASSIGN(VxlanRoutingRouteWalker);
};

class VxlanRoutingManager {
public:
    VxlanRoutingManager(Agent *agent);
    virtual ~VxlanRoutingManager();

    //Oper handler
    void Register();
    void Shutdown();

    //Listener to vxlan config
    void Config();

    void Enabled();
    void Disabled();
    void VnNotify(DBTablePartBase *partition, DBEntryBase *e);
    void VrfNotify(DBTablePartBase *partition, DBEntryBase *e);
    bool RouteNotify(DBTablePartBase *partition, DBEntryBase *e);
    void UpdateDefaultRoute(VnEntryRef vn);
    void DeleteDefaultRoute(VnEntryRef vn);
    VnEntryConstRef vxlan_routing_vn();

private:
    friend class VxlanRoutingRouteWalker;
    void DeleteInetRoute(DBTablePartBase *partition, DBEntryBase *e);
    void UpdateInetRoute(DBTablePartBase *partition, DBEntryBase *e);
    void UpdateEvpnType5Route(Agent *agent,
                              const AgentRoute *rt,
                              const AgentPath *path);
    bool InetRouteNotify(DBTablePartBase *partition, DBEntryBase *e);
    bool EvpnRouteNotify(DBTablePartBase *partition, DBEntryBase *e);
    bool EvpnType5RouteNotify(DBTablePartBase *partition, DBEntryBase *e);
    bool EvpnType2RouteNotify(DBTablePartBase *partition, DBEntryBase *e);

    bool vxlan_routing_enabled_;
    VnEntryConstRef vxlan_routing_vn_;
    DBTable::ListenerId vn_listener_id_;
    AgentRouteWalkerPtr walker_;
    DBTable::ListenerId vrf_listener_id_;
    Agent *agent_;
    DISALLOW_COPY_AND_ASSIGN(VxlanRoutingManager);
};

#endif
