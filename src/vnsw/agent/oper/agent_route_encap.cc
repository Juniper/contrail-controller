/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */
#include <cmn/agent_cmn.h>
#include <route/route.h>

#include <vnc_cfg_types.h> 
#include <agent_types.h>

#include <cmn/agent_db.h>

#include <oper/agent_route_walker.h>
#include <oper/agent_route_encap.h>
#include <oper/vrf.h>
#include <oper/agent_route.h>
#include <oper/agent_path.h>

AgentRouteEncap::AgentRouteEncap(Agent *agent) : 
    AgentRouteWalker(agent, AgentRouteWalker::ALL) {
}

bool AgentRouteEncap::RouteWalkNotify(DBTablePartBase *partition,
                                      DBEntryBase *e) {
    AgentRoute *route = static_cast<AgentRoute *>(e);
    //route->EnqueueRouteResync();
    if (route->GetTableType() == Agent::EVPN) {
        const EvpnRouteEntry *evpn_rt =
            static_cast<const EvpnRouteEntry *>(route);
        //Find VM interface and update l2 route via same.
        //Encap change for EVPN should result in withdraw of old route
        //and addition of new route with appropriate label.
        //So simple encap change of Nexthop will not help of evpn routes.
        //Check only for local path as controller driven path will get settled
        //by virtue of all local path being rebaked.
        const AgentPath *path = route->FindLocalVmPortPath();
        if (path == NULL)
            return true;
        const NextHop *nh = path->nexthop(agent());
        if (nh == NULL)
            return true;
        assert(nh->GetType() == NextHop::INTERFACE);
        const InterfaceNH *intf_nh = static_cast<const InterfaceNH *>(nh);
        const Interface *intf = intf_nh->GetInterface();
        assert(intf->type() == Interface::VM_INTERFACE);
        const VmInterface *vm_intf = static_cast<const VmInterface *>(intf);
        vm_intf->UpdateL2InterfaceRoute(vm_intf->l2_active(), true,
                                        vm_intf->vrf(),
                                        vm_intf->ip_addr(),
                                        vm_intf->ip6_addr(),
                                        evpn_rt->ethernet_tag());
    } else {
        route->EnqueueRouteResync();
    }
    return true;
}

void AgentRouteEncap::Update() {
    StartVrfWalk(); 
}
