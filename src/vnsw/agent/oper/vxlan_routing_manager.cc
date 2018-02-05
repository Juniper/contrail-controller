/*
 * Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/uuid/uuid_io.hpp>
#include <cmn/agent_cmn.h>

#include <base/logging.h>
#include <oper/operdb_init.h>
#include <oper/route_common.h>
#include <oper/vrf.h>
#include <oper/bridge_route.h>
#include <oper/inet_unicast_route.h>
#include <oper/evpn_route.h>
#include <oper/agent_route.h>
#include <oper/agent_route_walker.h>
#include <oper/project_config.h>
#include <oper/vn.h>
#include <oper/vrf.h>
#include <oper/vxlan_routing_manager.h>

using namespace std;

VxlanRoutingState::VxlanRoutingState(VxlanRoutingManager *mgr,
                                     VrfEntry *vrf) {
    inet4_table_ = vrf->GetInet4UnicastRouteTable();
    inet6_table_ = vrf->GetInet6UnicastRouteTable();
    evpn_table_ = vrf->GetEvpnRouteTable();
    inet4_id_ = inet4_table_->
        Register(boost::bind(&VxlanRoutingManager::RouteNotify,
                             mgr, _1, _2));
    inet6_id_ = inet6_table_->
        Register(boost::bind(&VxlanRoutingManager::RouteNotify,
                             mgr, _1, _2));
    evpn_id_ = evpn_table_->
        Register(boost::bind(&VxlanRoutingManager::RouteNotify,
                             mgr, _1, _2));
}

VxlanRoutingState::~VxlanRoutingState() {
    inet4_table_->Unregister(inet4_id_);
    inet6_table_->Unregister(inet6_id_);
    evpn_table_->Unregister(evpn_id_);
}

VxlanRoutingRouteWalker::VxlanRoutingRouteWalker(const std::string &name,
                                                 VxlanRoutingManager *mgr,
                                                 Agent *agent) :
    AgentRouteWalker(name, agent), mgr_(mgr) {
}

VxlanRoutingRouteWalker::~VxlanRoutingRouteWalker() {
}

//Only take notification of evpn type 2 routes.
//Change in them will trigger change in rest.
bool VxlanRoutingRouteWalker::RouteWalkNotify(DBTablePartBase *partition,
                                              DBEntryBase *e) {
    const EvpnRouteEntry *evpn_rt =
        dynamic_cast<const EvpnRouteEntry *>(e);
    if (evpn_rt->mac().IsZero())
        return true;

    return mgr_->EvpnType2RouteNotify(partition, e);
}

VxlanRoutingManager::VxlanRoutingManager(Agent *agent) :
    vxlan_routing_enabled_(false), vxlan_routing_vn_(), walker_(),
    vrf_listener_id_(), agent_(agent) {
    vn_listener_id_ = agent->vn_table()->
        Register(boost::bind(&VxlanRoutingManager::VnNotify,
                             this, _1, _2));
}

VxlanRoutingManager::~VxlanRoutingManager() {
}

void VxlanRoutingManager::Register() {
    agent_->oper_db()->project_config()->Register(
          boost::bind(&VxlanRoutingManager::Config, this));
    vrf_listener_id_ = agent_->vrf_table()->Register(
        boost::bind(&VxlanRoutingManager::VrfNotify, this, _1, _2));
    walker_.reset(new VxlanRoutingRouteWalker("VxlanRoutingManager", this,
                                              agent_));
    agent_->oper_db()->agent_route_walk_manager()->
        RegisterWalker(static_cast<AgentRouteWalker *>(walker_.get()));
}

void VxlanRoutingManager::Shutdown() {
    agent_->vrf_table()->Unregister(vrf_listener_id_);
    agent_->oper_db()->agent_route_walk_manager()->
        ReleaseWalker(walker_.get());
    walker_.reset(NULL);
}

void VxlanRoutingManager::Config() {
    if (agent_->oper_db()->project_config()->vxlan_routing() ==
        vxlan_routing_enabled_) {
        return;
    }

    vxlan_routing_enabled_ =
        agent_->oper_db()->project_config()->vxlan_routing();

    if (vxlan_routing_enabled_) {
        walker_->StartVrfWalk();
    }
}

void VxlanRoutingManager::VnNotify(DBTablePartBase *partition, DBEntryBase *e) {
    VnEntry *vn = static_cast<VnEntry *>(e);
    VnEntryConstRef old_vxlan_routing_vn = vxlan_routing_vn_;

    if (vn->IsDeleted() || (vn->GetVrf() == NULL)) {
        if (vxlan_routing_vn_.get() == vn) {
            walker_->StartVrfWalk();
        }
        //DeleteDefaultRoute(vxlan_routing_vn_.get());
        vxlan_routing_vn_.reset();
        return;
    }

    if (vn->vxlan_routing_vn()) {
        if (vxlan_routing_vn_.get() == vn) {
            return;
        }
        vxlan_routing_vn_ = vn;
    }
    //UpdateDefaultRoute(vxlan_routing_vn_.get());
    walker_->StartVrfWalk();
}

void VxlanRoutingManager::VrfNotify(DBTablePartBase *partition,
                                    DBEntryBase *e) {
    VrfEntry *vrf = static_cast<VrfEntry *>(e);
    if (vrf->GetName().compare(agent_->fabric_policy_vrf_name()) != 0)
        return;

    VxlanRoutingState *state = dynamic_cast<VxlanRoutingState *>(vrf->
                             GetState(partition->parent(), vrf_listener_id_));

    if (vrf->IsDeleted()) {
        if (state) {
            vrf->ClearState(partition->parent(), vrf_listener_id_);
            delete state;
        }
        return;
    }

    if (!state) {
        state = new VxlanRoutingState(this, vrf);
        vrf->SetState(partition->parent(), vrf_listener_id_, state);
    }
}

void VxlanRoutingManager::UpdateEvpnType5Route(Agent *agent,
                                            const AgentRoute *route,
                                            const AgentPath *path) {
    VrfEntry *l3_vrf = vxlan_routing_vn_.get()->GetVrf();
    const InetUnicastRouteEntry *inet_rt =
        static_cast<const InetUnicastRouteEntry *>(route);
    EvpnAgentRouteTable *evpn_table =
        static_cast<EvpnAgentRouteTable *>(l3_vrf->GetEvpnRouteTable());

    //Add route in evpn table
    DBRequest nh_req;
    nh_req.key.reset(path->nexthop()->GetDBRequestKey().get());
    nh_req.data.reset(NULL);
    evpn_table->AddType5Route(agent->local_vm_peer(),
                              l3_vrf->GetName(),
                              inet_rt->addr(),
                              l3_vrf->vxlan_id(),
                              new EvpnRoutingData(nh_req,
                                                  l3_vrf));
}

//Handles change in NH of local vm port path
bool VxlanRoutingManager::InetRouteNotify(DBTablePartBase *partition,
                                          DBEntryBase *e) {
    const InetUnicastRouteEntry *inet_rt =
        dynamic_cast<const InetUnicastRouteEntry*>(e);
    const AgentPath *local_vm_port_path = inet_rt->FindLocalVmPortPath();

    if (inet_rt->vrf() == vxlan_routing_vn_.get()->GetVrf())
        return true;

    const EvpnRoutingPath *evpn_routing_path =
        static_cast<const EvpnRoutingPath *>(inet_rt->
                                             FindPath(agent_->evpn_routing_peer()));
    if (!evpn_routing_path) {
        return true;
    }

    if (inet_rt->IsDeleted() || (local_vm_port_path == NULL)) {
        evpn_routing_path->DeleteEvpnType5Route(agent_, inet_rt,
                                     vxlan_routing_vn_.get()->GetVrf());
        return true;
    }

    UpdateEvpnType5Route(agent_, inet_rt, local_vm_port_path);
    return true;
}

bool VxlanRoutingManager::EvpnType5RouteNotify(DBTablePartBase *partition,
                                               DBEntryBase *e) {
    EvpnRouteEntry *evpn_rt = dynamic_cast<EvpnRouteEntry *>(e);
    VrfEntry *vrf = evpn_rt->vrf();

    if (evpn_rt->mac().IsZero() == false)
        return true;

    if (evpn_rt->IsDeleted()) {
        InetUnicastAgentRouteTable *inet_table =
            vrf->GetInetUnicastRouteTable(evpn_rt->ip_addr());
        inet_table->Delete(agent_->evpn_routing_peer(),
                           vrf->GetName(),
                           evpn_rt->ip_addr(),
                           evpn_rt->GetVmIpPlen());
        return true;
    }

    InetUnicastAgentRouteTable *inet_table =
        evpn_rt->vrf()->GetInetUnicastRouteTable(evpn_rt->ip_addr());

    DBRequest nh_req(DBRequest::DB_ENTRY_ADD_CHANGE);
    nh_req.key.reset(evpn_rt->GetActiveNextHop()->GetDBRequestKey().get());
    inet_table->AddEvpnRoutingRoute(evpn_rt->ip_addr(),
                                    evpn_rt->GetVmIpPlen(),
                                    vrf,
                                    agent_->evpn_routing_peer(),
                                    nh_req);
    return true;
}

void VxlanRoutingManager::DeleteInetRoute(DBTablePartBase *partition,
                                          DBEntryBase *e) {
    EvpnRouteEntry *evpn_route = dynamic_cast<EvpnRouteEntry *>(e);
    if (evpn_route->GetActiveNextHop()->GetType() == NextHop::TUNNEL)
        return;

    VrfEntry *l2_vrf = evpn_route->vrf();
    //Add Inet route to point to table NH in l2 vrf inet
    const IpAddress &ip_addr = evpn_route->ip_addr();
    if (ip_addr.is_unspecified())
        return;

    InetUnicastAgentRouteTable *inet_table =
        l2_vrf->GetInetUnicastRouteTable(ip_addr);
    inet_table->Delete(agent_->evpn_routing_peer(),
                       l2_vrf->GetName(), ip_addr,
                       evpn_route->GetVmIpPlen());
}

void VxlanRoutingManager::UpdateInetRoute(DBTablePartBase *partition,
                                          DBEntryBase *e) {
    EvpnRouteEntry *evpn_route = dynamic_cast<EvpnRouteEntry *>(e);
    if (evpn_route->GetActiveNextHop()->GetType() == NextHop::TUNNEL)
        return;

    VrfEntry *l2_vrf = evpn_route->vrf();
    //Add Inet route to point to table NH in l2 vrf inet
    const IpAddress &ip_addr = evpn_route->ip_addr();
    if (ip_addr.is_unspecified())
        return;
    InetUnicastAgentRouteTable *inet_table =
        l2_vrf->GetInetUnicastRouteTable(ip_addr);
    DBRequest nh_req(DBRequest::DB_ENTRY_ADD_CHANGE);
    nh_req.key.reset(new VrfNHKey(vxlan_routing_vn_.get()->GetVrf()->GetName(),
                                  false, false));
    nh_req.data.reset(new VrfNHData(false, false, false));
    inet_table->AddEvpnRoutingRoute(ip_addr, evpn_route->GetVmIpPlen(),
                                    vxlan_routing_vn_.get()->GetVrf(),
                                    agent_->evpn_routing_peer(),
                                    nh_req);
}

bool VxlanRoutingManager::EvpnType2RouteNotify(DBTablePartBase *partition,
                                               DBEntryBase *e) {
    EvpnRouteEntry *evpn_route = dynamic_cast<EvpnRouteEntry *>(e);
    AgentPath *path = evpn_route->FindLocalVmPortPath();

    if (evpn_route->mac().IsZero() || (path == NULL))
        return true;

    bool withdraw = (agent_->oper_db()->vxlan_routing_manager()->
                     vxlan_routing_vn() == NULL);
    if (evpn_route->IsDeleted()) {
        withdraw = true;
    }

    if (withdraw) {
        DeleteInetRoute(partition, e);
    } else {
        UpdateInetRoute(partition, e);
    }
    return true;
}

bool VxlanRoutingManager::EvpnRouteNotify(DBTablePartBase *partition,
                                          DBEntryBase *e) {
    const EvpnRouteEntry *evpn_rt =
        dynamic_cast<const EvpnRouteEntry *>(e);

    if (!evpn_rt || !evpn_rt->is_multicast())
        return true;

    if (evpn_rt->vrf() != vxlan_routing_vn_.get()->GetVrf()) {
        return EvpnType2RouteNotify(partition, e);
    } else {
        return EvpnType5RouteNotify(partition, e);
    }

    return true;
}

bool VxlanRoutingManager::RouteNotify(DBTablePartBase *partition,
                                      DBEntryBase *e) {
    const InetUnicastRouteEntry *inet_rt =
        dynamic_cast<const InetUnicastRouteEntry*>(e);
    if (inet_rt) {
        return InetRouteNotify(partition, e);
    }

    const EvpnRouteEntry *evpn_rt =
        dynamic_cast<const EvpnRouteEntry *>(e);
    if (evpn_rt) {
        return EvpnRouteNotify(partition, e);
    }
    return true;
}

void VxlanRoutingManager::DeleteDefaultRoute(VnEntryRef vn) {
    VrfEntry *l3_vrf = vn.get()->GetVrf();
    InetUnicastAgentRouteTable *inet4_table =
        l3_vrf->GetInet4UnicastRouteTable();
    InetUnicastAgentRouteTable *inet6_table =
        l3_vrf->GetInet6UnicastRouteTable();

    inet4_table->Delete(agent_->evpn_routing_peer(), l3_vrf->GetName(),
                        Ip4Address(), 0);
    inet6_table->Delete(agent_->evpn_routing_peer(), l3_vrf->GetName(),
                        Ip6Address(), 0);
}

void VxlanRoutingManager::UpdateDefaultRoute(VnEntryRef vn) {
    VrfEntry *l3_vrf = vn.get()->GetVrf();
    InetUnicastAgentRouteTable *inet4_table =
        l3_vrf->GetInet4UnicastRouteTable();
    InetUnicastAgentRouteTable *inet6_table =
        l3_vrf->GetInet6UnicastRouteTable();

    DBRequest nh_req(DBRequest::DB_ENTRY_ADD_CHANGE);
    nh_req.key.reset(new VrfNHKey(vxlan_routing_vn_.get()->GetVrf()->GetName(),
                                  false, false));
    nh_req.data.reset(new VrfNHData(false, false, false));
    inet4_table->AddEvpnRoutingRoute(Ip4Address(), 0,
                                    vxlan_routing_vn_.get()->GetVrf(),
                                    agent_->evpn_routing_peer(),
                                    nh_req);
    inet6_table->AddEvpnRoutingRoute(Ip6Address(), 0,
                                    vxlan_routing_vn_.get()->GetVrf(),
                                    agent_->evpn_routing_peer(),
                                    nh_req);
}

VnEntryConstRef VxlanRoutingManager::vxlan_routing_vn() {
    return vxlan_routing_vn_;
}

