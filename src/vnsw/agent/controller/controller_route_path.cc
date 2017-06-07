/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/uuid/uuid_io.hpp>
#include <boost/lexical_cast.hpp>

#include <cmn/agent_cmn.h>
#include <route/route.h>

#include <cmn/agent_cmn.h>
#include <oper/ecmp_load_balance.h>
#include <oper/route_common.h>
#include <oper/vrf.h>
#include <oper/tunnel_nh.h>
#include <oper/mpls.h>
#include <oper/mirror_table.h>
#include <oper/peer.h>
#include <oper/agent_route.h>
#include <controller/controller_route_path.h>
#include <controller/controller_peer.h>
#include <controller/controller_init.h>
#include <controller/controller_export.h>
#include <oper/agent_sandesh.h>

using namespace std;
using namespace boost::asio;

ControllerPeerPath::ControllerPeerPath(const Peer *peer) :
    AgentRouteData(false), peer_(peer) {
}

bool ControllerEcmpRoute::AddChangePath(Agent *agent, AgentPath *path,
                                        const AgentRoute *rt) {
    CompositeNHKey *comp_key = static_cast<CompositeNHKey *>(nh_req_.key.get());
    bool ret = false;
    //Reorder the component NH list, and add a reference to local composite mpls
    //label if any
    bool comp_nh_policy = comp_key->GetPolicy();
    bool new_comp_nh_policy = false;
    if (path->ReorderCompositeNH(agent, comp_key, new_comp_nh_policy) == false)
        return false;
    if (!comp_nh_policy) {
        comp_key->SetPolicy(new_comp_nh_policy);
    }

    if (path->ecmp_load_balance() != ecmp_load_balance_) {
        path->UpdateEcmpHashFields(agent, ecmp_load_balance_,
                                          nh_req_);
        ret = true;
    }

    ret |= InetUnicastRouteEntry::ModifyEcmpPath(dest_addr_, plen_, vn_list_,
                                                 label_, local_ecmp_nh_,
                                                 vrf_name_, sg_list_,
                                                 CommunityList(),
                                                 path_preference_,
                                                 tunnel_bmap_,
                                                 ecmp_load_balance_,
                                                 nh_req_, agent, path);
    return ret;
}

ControllerVmRoute *ControllerVmRoute::MakeControllerVmRoute(const Peer *peer,
                                         const string &default_vrf,
                                         const Ip4Address &router_id,
                                         const string &vrf_name,
                                         const Ip4Address &tunnel_dest,
                                         TunnelType::TypeBmap bmap,
                                         uint32_t label,
                                         const VnListType &dest_vn_list,
                                         const SecurityGroupList &sg_list,
                                         const PathPreference &path_preference,
                                         bool ecmp_suppressed,
                                         const EcmpLoadBalance &ecmp_load_balance) {
    // Make Tunnel-NH request
    DBRequest nh_req(DBRequest::DB_ENTRY_ADD_CHANGE);
    nh_req.key.reset(new TunnelNHKey(default_vrf, router_id, tunnel_dest, false,
                                     TunnelType::ComputeType(bmap)));
    nh_req.data.reset(new TunnelNHData());

    // Make route request pointing to Tunnel-NH created above
    ControllerVmRoute *data =
        new ControllerVmRoute(peer, default_vrf, tunnel_dest, label,
                              dest_vn_list, bmap, sg_list, path_preference,
                              nh_req, ecmp_suppressed,
                              ecmp_load_balance);
    return data;
}

bool ControllerVmRoute::UpdateRoute(AgentRoute *rt) {
    bool ret = false;
    //For IP subnet routes with Tunnel NH, check if arp flood
    //needs to be enabled for the path.
    if ((rt->GetTableType() == Agent::INET4_UNICAST) ||
        (rt->GetTableType() == Agent::INET6_UNICAST)) {
        InetUnicastRouteEntry *inet_rt =
            static_cast<InetUnicastRouteEntry *>(rt);
        //If its the IPAM route then no change required neither
        //super net needs to be searched.
        if (inet_rt->ipam_subnet_route())
            return ret;

        bool ipam_subnet_route = inet_rt->IpamSubnetRouteAvailable();
        if (inet_rt->ipam_subnet_route() != ipam_subnet_route) {
            inet_rt->set_ipam_subnet_route(ipam_subnet_route);
            ret = true;
        }

        if (ipam_subnet_route) { 
            if (inet_rt->proxy_arp() == true) {
                inet_rt->set_proxy_arp(false);
                ret = true;
            }
        } else {
            if (inet_rt->proxy_arp() == false) {
                inet_rt->set_proxy_arp(true);
                ret = true;
            }
        }

    }
    return ret;
}

bool ControllerVmRoute::AddChangePath(Agent *agent, AgentPath *path,
                                      const AgentRoute *rt) {
    bool ret = false;
    NextHop *nh = NULL;
    SecurityGroupList path_sg_list;

    if (path->tunnel_bmap() != tunnel_bmap_) {
        path->set_tunnel_bmap(tunnel_bmap_);
        ret = true;
    }

    TunnelType::Type new_tunnel_type = TunnelType::ComputeType(tunnel_bmap_);
    if ((tunnel_bmap_ == (1 << TunnelType::VXLAN) && 
         (new_tunnel_type != TunnelType::VXLAN)) ||
        (tunnel_bmap_ != (1 << TunnelType::VXLAN) &&
         (new_tunnel_type == TunnelType::VXLAN))) {
        new_tunnel_type = TunnelType::INVALID;
        nh_req_.key.reset(new TunnelNHKey(agent->fabric_vrf_name(),
                                          agent->router_id(), tunnel_dest_,
                                          false, new_tunnel_type));
    }
    agent->nexthop_table()->Process(nh_req_);
    TunnelNHKey key(agent->fabric_vrf_name(), agent->router_id(), tunnel_dest_,
                    false, new_tunnel_type);
    nh = static_cast<NextHop *>(agent->nexthop_table()->FindActiveEntry(&key));
    path->set_tunnel_dest(tunnel_dest_);

    if (path->tunnel_type() != new_tunnel_type) {
        path->set_tunnel_type(new_tunnel_type);
        ret = true;
    }

    //Interpret label sent by control node
    if (tunnel_bmap_ == TunnelType::VxlanType()) {
        //Only VXLAN encap is sent, so label is VXLAN
        path->set_vxlan_id(label_);
        if (path->label() != MplsTable::kInvalidLabel) {
            path->set_label(MplsTable::kInvalidLabel);
            ret = true;
        }
    } else if (tunnel_bmap_ == TunnelType::MplsType()) {
        //MPLS (GRE/UDP) is the only encap sent,
        //so label is MPLS.
        if (path->label() != label_) {
            path->set_label(label_);
            ret = true;
        }
        path->set_vxlan_id(VxLanTable::kInvalidvxlan_id);
    } else {
        //Got a mix of Vxlan and Mpls, so interpret label
        //as per the computed tunnel type.
        if (new_tunnel_type == TunnelType::VXLAN) {
            if (path->vxlan_id() != label_) {
                path->set_vxlan_id(label_);
                path->set_label(MplsTable::kInvalidLabel);
                ret = true;
            }
        } else {
            if (path->label() != label_) {
                path->set_label(label_);
                path->set_vxlan_id(VxLanTable::kInvalidvxlan_id);
                ret = true;
            }
        }
    }

    if (path->dest_vn_list() != dest_vn_list_) {
        path->set_dest_vn_list(dest_vn_list_);
        ret = true;
    }

    path->set_unresolved(false);
    if (path->ChangeNH(agent, nh) == true)
        ret = true;

    path_sg_list = path->sg_list();
    if (path_sg_list != sg_list_) {
        path->set_sg_list(sg_list_);
        ret = true;
    }

    if (path->path_preference() != path_preference_) {
        path->set_path_preference(path_preference_);
        ret = true;
    }

    //If a transition of path happens from ECMP to non ECMP
    //reset local mpls label reference and composite nh key
    if (path->composite_nh_key()) {
        path->set_composite_nh_key(NULL);
        path->set_local_ecmp_mpls_label(NULL);
    }

    if (path->ecmp_suppressed() != ecmp_suppressed_) {
        path->set_ecmp_suppressed(ecmp_suppressed_);
        ret = true;
    }

    if (ecmp_load_balance_ != path->ecmp_load_balance()) {
        path->set_ecmp_load_balance(ecmp_load_balance_);
        ret = true;
    }

    return ret;
}

bool ClonedLocalPath::AddChangePath(Agent *agent, AgentPath *path,
                                    const AgentRoute *rt) {
    bool ret = false;

    MplsLabel *mpls = agent->mpls_table()->FindMplsLabel(mpls_label_);
    if (!mpls) {
        return ret;
    }

    //Do a route lookup in native VRF
    assert(mpls->nexthop()->GetType() == NextHop::VRF);
    const VrfNH *vrf_nh = static_cast<const VrfNH *>(mpls->nexthop());
    const InetUnicastRouteEntry *uc_rt =
        static_cast<const InetUnicastRouteEntry *>(rt);
    const AgentRoute *mpls_vrf_uc_rt =
        vrf_nh->GetVrf()->GetUcRoute(uc_rt->addr());
    if (mpls_vrf_uc_rt == NULL) {
        return ret;
    }
    AgentPath *local_path = mpls_vrf_uc_rt->FindLocalVmPortPath();
    if (!local_path) {
        return ret;
    }

    if (path->dest_vn_list() != vn_list_) {
        path->set_dest_vn_list(vn_list_);
        ret = true;
    }
    path->set_unresolved(false);

    if (path->sg_list() != sg_list_) {
        path->set_sg_list(sg_list_);
        ret = true;
    }

    path->set_tunnel_bmap(local_path->tunnel_bmap());
    TunnelType::Type new_tunnel_type =
        TunnelType::ComputeType(path->tunnel_bmap());
    if (new_tunnel_type == TunnelType::VXLAN &&
        local_path->vxlan_id() == VxLanTable::kInvalidvxlan_id) {
        new_tunnel_type = TunnelType::ComputeType(TunnelType::MplsType());
    }

    if (path->tunnel_type() != new_tunnel_type) {
        path->set_tunnel_type(new_tunnel_type);
        ret = true;
    }

    // If policy force-enabled in request, enable policy
    path->set_force_policy(local_path->force_policy());

    if (path->label() != local_path->label()) {
        path->set_label(local_path->label());
        ret = true;
    }

    if (path->vxlan_id() != local_path->vxlan_id()) {
        path->set_vxlan_id(local_path->vxlan_id());
        ret = true;
    }

    NextHop *nh = const_cast<NextHop *>(local_path->ComputeNextHop(agent));
    if (path->ChangeNH(agent, nh) == true) {
        ret = true;
    }
    return ret;
}

bool StalePathData::AddChangePath(Agent *agent, AgentPath *path,
                                  const AgentRoute *route) {
    if (path->is_stale() || route->IsDeleted())
        return false;
    AgentPath *old_stale_path = route->FindStalePath();
    path->set_is_stale(true);
    //Delete old stale path
    DBRequest req(DBRequest::DB_ENTRY_DELETE);
    AgentRouteKey *key = (static_cast<AgentRouteKey *>(route->
                                      GetDBRequestKey().get()))->Clone();
    key->set_peer(old_stale_path->peer());
    req.key.reset(key);
    req.data.reset();
    AgentRouteTable *table = static_cast<AgentRouteTable *>(route->get_table());
    table->Process(req);
    return true;
}
