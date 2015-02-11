/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/uuid/uuid_io.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/foreach.hpp>
#include <cmn/agent_cmn.h>
#include <route/route.h>

#include <vnc_cfg_types.h> 
#include <agent_types.h>

#include <filter/acl.h>

#include <oper/peer.h>
#include <oper/vrf.h>
#include <oper/interface_common.h>
#include <oper/nexthop.h>
#include <oper/tunnel_nh.h>
#include <oper/vn.h>
#include <oper/mirror_table.h>
#include <oper/vxlan.h>
#include <oper/mpls.h>
#include <oper/route_common.h>
#include <oper/agent_sandesh.h>
using namespace std;
using namespace boost::asio;

AgentPath::AgentPath(const Peer *peer, AgentRoute *rt):
    Path(), peer_(peer), nh_(NULL), label_(MplsTable::kInvalidLabel),
    vxlan_id_(VxLanTable::kInvalidvxlan_id), dest_vn_name_(""),
    sync_(false), force_policy_(false), sg_list_(),
    server_ip_(0), tunnel_bmap_(TunnelType::AllType()),
    tunnel_type_(TunnelType::ComputeType(TunnelType::AllType())),
    vrf_name_(""), gw_ip_(0), unresolved_(true), is_stale_(false),
    is_subnet_discard_(false), dependant_rt_(rt), path_preference_(),
    local_ecmp_mpls_label_(rt), composite_nh_key_(NULL), subnet_gw_ip_(),
    flood_dhcp_(false) {
}

AgentPath::~AgentPath() {
    clear_sg_list();
}

uint32_t AgentPath::GetTunnelBmap() const {
    TunnelType::Type type = TunnelType::ComputeType(tunnel_bmap_);
    if ((type == (1 << TunnelType::VXLAN)) && (vxlan_id_ != 0)) {
        return (1 << TunnelType::VXLAN);
    } else {
        return tunnel_bmap_;
    }
}

uint32_t AgentPath::GetActiveLabel() const {
    if (tunnel_type_ == TunnelType::VXLAN) {
        return vxlan_id_;
    } else {
        return label_;
    }
}

NextHop* AgentPath::nexthop() const {
    return nh_.get();
}

const NextHop* AgentPath::ComputeNextHop(Agent *agent) const {
    if (nh_) {
        return nh_.get();
    }

    if (unresolved_ == true) {
        DiscardNH key;
        return static_cast<NextHop *>
            (agent->nexthop_table()->FindActiveEntry(&key));
    }

    //Indirect route's path, get direct route's NH
    const NextHop *nh = dependant_rt_.get()->GetActiveNextHop();
    if (nh == NULL) {
        assert(0);
    }
    return nh;
}

bool AgentPath::ChangeNH(Agent *agent, NextHop *nh) {
    // If NH is not found, point route to discard NH
    bool ret = false;
    if (nh == NULL) {
        nh = agent->nexthop_table()->discard_nh();
    }

    if (nh_ != nh) {
        nh_ = nh;
        ret = true;
    }

    if (peer_ && (peer_->GetType() == Peer::MULTICAST_PEER) &&
        (label_ != MplsTable::kInvalidLabel)) {
        MplsLabelKey key(MplsLabel::MCAST_NH, label_);
        MplsLabel *mpls = static_cast<MplsLabel *>(agent->mpls_table()->
                                                   FindActiveEntry(&key));
        ret = agent->mpls_table()->ChangeNH(mpls, nh);
        if (mpls) {
            //Send notify of change
            mpls->get_table_partition()->Notify(mpls);
        }
    }

    return ret;
}

bool AgentPath::RebakeAllTunnelNHinCompositeNH(const AgentRoute *sync_route) {
    if (nh_->GetType() != NextHop::COMPOSITE){
        return false;
    }

    Agent *agent =
        static_cast<AgentRouteTable *>(sync_route->get_table())->agent();
    CompositeNH *cnh = static_cast<CompositeNH *>(nh_.get());

    //Compute new tunnel type
    TunnelType::Type new_tunnel_type;
    //Only MPLS types are supported for multicast
    if ((sync_route->is_multicast()) && (peer_->GetType() ==
                                         Peer::MULTICAST_FABRIC_TREE_BUILDER)) {
        new_tunnel_type = TunnelType::ComputeType(TunnelType::MplsType());
        if (new_tunnel_type == TunnelType::VXLAN) {
            new_tunnel_type = TunnelType::MPLS_GRE;
        }
    } else {
        new_tunnel_type = TunnelType::ComputeType(tunnel_bmap_);
    }

    CompositeNH *new_composite_nh = NULL;
    new_composite_nh = cnh->ChangeTunnelType(agent, new_tunnel_type);
    if (ChangeNH(agent, new_composite_nh)) {
        //Update composite NH key list to reflect new type
        if (composite_nh_key_)
            composite_nh_key_->ChangeTunnelType(new_tunnel_type);
        return true;
    }
    return false;
}

bool AgentPath::UpdateNHPolicy(Agent *agent) {
    bool ret = false;
    if (nh_.get() == NULL || nh_->GetType() != NextHop::INTERFACE) {
        return ret;
    }

    const InterfaceNH *intf_nh = static_cast<const InterfaceNH *>(nh_.get());
    if (intf_nh->GetInterface()->type() != Interface::VM_INTERFACE) {
        return ret;
    }

    const VmInterface *vm_port =
        static_cast<const VmInterface *>(intf_nh->GetInterface());

    bool policy = vm_port->policy_enabled();
    if (force_policy_) {
        policy = true;
    }

    NextHop *nh = NULL;
    if (intf_nh->PolicyEnabled() != policy) {
        //Make path point to policy enabled interface
        InterfaceNHKey key(new VmInterfaceKey(AgentKey::ADD_DEL_CHANGE,
                                              vm_port->GetUuid(), ""),
                           policy, intf_nh->GetFlags());
        nh = static_cast<NextHop *>
            (agent->nexthop_table()->FindActiveEntry(&key));
        // If NH is not found, point route to discard NH
        if (nh == NULL) {
            LOG(DEBUG, "Interface NH for <" 
                << boost::lexical_cast<std::string>(vm_port->GetUuid())
                << " : policy = " << policy);
            nh = agent->nexthop_table()->discard_nh();
        }
        if (ChangeNH(agent, nh) == true) {
            ret = true;
        }
    }

    return ret;
}

bool AgentPath::UpdateTunnelType(Agent *agent, const AgentRoute *sync_route) {
    //Return if there is no change in tunnel type for non Composite NH.
    //For composite NH component needs to be traversed.
    if ((tunnel_type_ == TunnelType::ComputeType(tunnel_bmap_)) &&
        (nh_.get() && nh_.get()->GetType() != NextHop::COMPOSITE)) {
        return false;
    }

    tunnel_type_ = TunnelType::ComputeType(tunnel_bmap_);
    if (tunnel_type_ == TunnelType::VXLAN &&
        vxlan_id_ == VxLanTable::kInvalidvxlan_id) {
        tunnel_type_ = TunnelType::ComputeType(TunnelType::MplsType());
    }
    if (nh_.get() && nh_->GetType() == NextHop::TUNNEL) {
        DBRequest nh_req(DBRequest::DB_ENTRY_ADD_CHANGE);
        TunnelNHKey *tnh_key =
            new TunnelNHKey(agent->fabric_vrf_name(), agent->router_id(),
                            server_ip_, false, tunnel_type_);
        nh_req.key.reset(tnh_key);
        nh_req.data.reset(new TunnelNHData());
        agent->nexthop_table()->Process(nh_req);

        TunnelNHKey nh_key(agent->fabric_vrf_name(), agent->router_id(),
                           server_ip_, false, tunnel_type_);
        NextHop *nh = static_cast<NextHop *>
            (agent->nexthop_table()->FindActiveEntry(&nh_key));
        ChangeNH(agent, nh);
    }

    if (nh_.get() && nh_->GetType() == NextHop::COMPOSITE) {
        RebakeAllTunnelNHinCompositeNH(sync_route);
    }
    return true;
}

bool AgentPath::Sync(AgentRoute *sync_route) {
    bool ret = false;
    bool unresolved = false;

    Agent *agent = static_cast<AgentRouteTable *>
        (sync_route->get_table())->agent();

    // Check if there is change in policy on the interface
    // If yes update the path to point to policy enabled NH
    if (UpdateNHPolicy(agent)) {
        ret = true;
    }

    //Handle tunnel type change
    if (UpdateTunnelType(agent, sync_route)) {
        ret = true;
    }

    //Check if there was a change in local ecmp composite nexthop
    if (nh_ && nh_->GetType() == NextHop::COMPOSITE &&
        composite_nh_key_.get() != NULL &&
        local_ecmp_mpls_label_.get() != NULL) {
        boost::scoped_ptr<CompositeNHKey> composite_nh_key(composite_nh_key_->Clone());
        if (ReorderCompositeNH(agent, composite_nh_key.get())) {
            if (ChangeCompositeNH(agent, composite_nh_key.get())) {
                ret = true;
            }
        }
    }

    if (vrf_name_ == Agent::NullString()) {
        return ret;
    }

    InetUnicastAgentRouteTable *table = NULL;
    InetUnicastRouteEntry *rt = NULL;
    table = agent->vrf_table()->GetInet4UnicastRouteTable(vrf_name_);
    if (table)
        rt = table->FindRoute(gw_ip_);

    if (rt == sync_route) {
        rt = NULL;
    }

    if (rt == NULL || rt->plen() == 0) {
        unresolved = true;
    } else if (rt->GetActiveNextHop()->GetType() == NextHop::RESOLVE) {
        const ResolveNH *nh =
            static_cast<const ResolveNH *>(rt->GetActiveNextHop());
        table->AddArpReq(vrf_name_, gw_ip_, nh->interface()->vrf()->GetName(),
                         nh->interface(), nh->PolicyEnabled(), dest_vn_name_,
                         sg_list_);
        unresolved = true;
    } else {
        unresolved = false;
    }

    if (unresolved_ != unresolved) {
        unresolved_ = unresolved;
        ret = true;
    }

    // Reset to new gateway route, no nexthop for indirect route
    if (dependant_rt_.get() != rt) {
        dependant_rt_.reset(rt);
        ret = true;
    }

    return ret;
}

bool AgentPath::IsLess(const AgentPath &r_path) const {
    if (peer()->GetType() == Peer::LOCAL_VM_PORT_PEER && 
        peer()->GetType() == r_path.peer()->GetType()) {
        if (path_preference() != r_path.path_preference()) {
            //If right path has lesser preference, then
            //it should be after the current entry
            //Hence the reverse check
            return (r_path.path_preference() < path_preference());
        }
    }

    return peer()->IsLess(r_path.peer());
}

void AgentPath::set_nexthop(NextHop *nh) {
    nh_ = nh;
}

EvpnDerivedPath::EvpnDerivedPath(const EvpnPeer *evpn_peer,
                   const IpAddress &ip_addr,
                   uint32_t ethernet_tag,
                   const std::string &parent) :
    AgentPath(evpn_peer, NULL),
    ip_addr_(ip_addr), ethernet_tag_(ethernet_tag),
    parent_(parent){
}

bool EvpnDerivedPath::IsLess(const AgentPath &r_path) const {
    const EvpnDerivedPath *r_evpn_path =
        dynamic_cast<const EvpnDerivedPath *>(&r_path);
    if (r_evpn_path != NULL) {
        if (r_evpn_path->ip_addr() != ip_addr_) {
            return (ip_addr_ < r_evpn_path->ip_addr());
        }
    }

    return peer()->IsLess(r_path.peer());
}

const NextHop *EvpnDerivedPath::ComputeNextHop(Agent *agent) const {
    return nexthop();
}

EvpnDerivedPathData::EvpnDerivedPathData(const EvpnRouteEntry *evpn_rt) :
    AgentRouteData(false), ethernet_tag_(evpn_rt->ethernet_tag()),
    ip_addr_(evpn_rt->ip_addr()), reference_path_(evpn_rt->GetActivePath()) {
    // For debuging add peer of active path in parent as well
    std::stringstream s;
    s << evpn_rt->ToString();
    s << " ";
    if (reference_path_ && reference_path_->peer())
        s << reference_path_->peer()->GetName();
    parent_ = s.str();
}

AgentPath *EvpnDerivedPathData::CreateAgentPath(const Peer *peer,
                                         AgentRoute *rt) const {
    const EvpnPeer *evpn_peer = dynamic_cast<const EvpnPeer *>(peer);
    assert(evpn_peer != NULL);
    return (new EvpnDerivedPath(evpn_peer, ip_addr_, ethernet_tag_,
                                parent_));
}

bool EvpnDerivedPathData::AddChangePath(Agent *agent, AgentPath *path,
                                 const AgentRoute *rt) {
    bool ret = false;
    EvpnDerivedPath *evpn_path = dynamic_cast<EvpnDerivedPath *>(path);
    assert(evpn_path != NULL);

    uint32_t label = reference_path_->label();
    if (evpn_path->label() != label) {
        evpn_path->set_label(label);
        ret = true;
    }

    uint32_t vxlan_id = reference_path_->vxlan_id();
    if (evpn_path->vxlan_id() != vxlan_id) {
        evpn_path->set_vxlan_id(vxlan_id);
        ret = true;
    }

    uint32_t tunnel_bmap = reference_path_->tunnel_bmap();
    if (evpn_path->tunnel_bmap() != tunnel_bmap) {
        evpn_path->set_tunnel_bmap(tunnel_bmap);
        ret = true;
    }

    TunnelType::Type tunnel_type = reference_path_->tunnel_type();
    if (evpn_path->tunnel_type() != tunnel_type) {
        evpn_path->set_tunnel_type(tunnel_type);
        ret = true;
    }

    if (evpn_path->nexthop() !=
        reference_path_->nexthop()) {
        evpn_path->set_nexthop(reference_path_->nexthop());
        ret = true;
    }

    const SecurityGroupList &sg_list = reference_path_->sg_list();
    if (evpn_path->sg_list() != sg_list) {
        evpn_path->set_sg_list(sg_list);
        ret = true;
    }

    const std::string &dest_vn = reference_path_->dest_vn_name();
    if (evpn_path->dest_vn_name() != dest_vn) {
        evpn_path->set_dest_vn_name(dest_vn);
        ret = true;
    }

    bool flood_dhcp = reference_path_->flood_dhcp();
    if (evpn_path->flood_dhcp() != flood_dhcp) {
        evpn_path->set_flood_dhcp(flood_dhcp);
        ret = true;
    }

    return ret;
}

bool HostRoute::AddChangePath(Agent *agent, AgentPath *path,
                              const AgentRoute *rt) {
    bool ret = false;
    NextHop *nh = NULL;
    InterfaceNHKey key(intf_.Clone(), false, InterfaceNHFlags::INET4);
    nh = static_cast<NextHop *>(agent->nexthop_table()->FindActiveEntry(&key));
    if (path->dest_vn_name() != dest_vn_name_) {
        path->set_dest_vn_name(dest_vn_name_);
        ret = true;
    }

    path->set_unresolved(false);
    if (path->ChangeNH(agent, nh) == true)
        ret = true;

    return ret;
} 

bool HostRoute::UpdateRoute(AgentRoute *rt) {
    bool ret = false;
    InetUnicastRouteEntry *uc_rt =
        static_cast<InetUnicastRouteEntry *>(rt);
    AgentRouteTable *table = static_cast<AgentRouteTable *>(rt->get_table());
    if ((table->GetTableType() != Agent::INET4_UNICAST) && 
        (table->GetTableType() != Agent::INET6_UNICAST))
        return ret;

    if (uc_rt->proxy_arp() != true) {
        uc_rt->set_proxy_arp(true);
        ret = true;
    }
    return ret;
}

bool L2ReceiveRoute::AddChangePath(Agent *agent, AgentPath *path,
                                   const AgentRoute *rt) {
    bool ret = false;

    path->set_unresolved(false);

    if (path->dest_vn_name() != dest_vn_name_) {
        path->set_dest_vn_name(dest_vn_name_);
        ret = true;
    }

    if (path->label() != mpls_label_) {
        path->set_label(mpls_label_);
        ret = true;
    }

    if (path->vxlan_id() != vxlan_id_) {
        path->set_vxlan_id(vxlan_id_);
        ret = true;
    }

    if (path->ChangeNH(agent, agent->nexthop_table()->l2_receive_nh()) == true)
        ret = true;

    return ret;
} 

bool InetInterfaceRoute::UpdateRoute(AgentRoute *rt) {
    bool ret = false;
    AgentRouteTable *table = static_cast<AgentRouteTable *>(rt->get_table());
    if ((table->GetTableType() != Agent::INET4_UNICAST) && 
        (table->GetTableType() != Agent::INET6_UNICAST))
        return ret;

    InetUnicastRouteEntry *uc_rt =
        static_cast<InetUnicastRouteEntry *>(rt);
    if (uc_rt->proxy_arp() != true) {
        uc_rt->set_proxy_arp(true);
        ret = true;
    }

    if (uc_rt->ipam_subnet_route() == true) {
        uc_rt->set_ipam_subnet_route(false);
        ret = true;
    }

    return ret;
}

bool InetInterfaceRoute::AddChangePath(Agent *agent, AgentPath *path,
                                       const AgentRoute *rt) {
    bool ret = false;
    NextHop *nh = NULL;
    InterfaceNHKey key(intf_.Clone(), false, InterfaceNHFlags::INET4);
    nh = static_cast<NextHop *>(agent->nexthop_table()->FindActiveEntry(&key));
    if (path->dest_vn_name() != dest_vn_name_) {
        path->set_dest_vn_name(dest_vn_name_);
        ret = true;
    }

    if (path->label() != label_) {
        path->set_label(label_);
        ret = true;
    }

    path->set_tunnel_bmap(tunnel_bmap_);
    TunnelType::Type tunnel_type = TunnelType::ComputeType(tunnel_bmap_);
    if (tunnel_type != path->tunnel_type()) {
        path->set_tunnel_type(tunnel_type);
        ret = true;
    }

    path->set_unresolved(false);
    if (path->ChangeNH(agent, nh) == true)
        ret = true;

    return ret;
}

bool DropRoute::AddChangePath(Agent *agent, AgentPath *path,
                              const AgentRoute *rt) {
    bool ret = false;

    if (path->dest_vn_name() != vn_) {
        path->set_dest_vn_name(vn_);
        ret = true;
    }

    NextHop *nh = agent->nexthop_table()->discard_nh();
    path->set_unresolved(false);
    if (path->ChangeNH(agent, nh) == true) {
        ret = true;
    }

    return ret;
}

bool LocalVmRoute::AddChangePath(Agent *agent, AgentPath *path,
                                 const AgentRoute *rt) {
    bool ret = false;
    NextHop *nh = NULL;
    SecurityGroupList path_sg_list;

    //TODO Based on key table type pick up interface
    VmInterfaceKey intf_key(AgentKey::ADD_DEL_CHANGE, intf_.uuid_, "");
    VmInterface *vm_port = static_cast<VmInterface *>
        (agent->interface_table()->FindActiveEntry(&intf_key));

    bool policy = false;
    if (vm_port) {
        // Use policy based NH if policy enabled on interface
        if (vm_port->policy_enabled()) {
            policy = true;
            ret = true;
        }
    }

    path->set_tunnel_bmap(tunnel_bmap_);
    TunnelType::Type new_tunnel_type = TunnelType::ComputeType(tunnel_bmap_);
    if (new_tunnel_type == TunnelType::VXLAN &&
        vxlan_id_ == VxLanTable::kInvalidvxlan_id) {
        new_tunnel_type = TunnelType::ComputeType(TunnelType::MplsType());
    }

    if (path->tunnel_type() != new_tunnel_type) {
        path->set_tunnel_type(new_tunnel_type);
        ret = true;
    }

    // If policy force-enabled in request, enable policy
    path->set_force_policy(force_policy_);
    if (force_policy_) {
        policy = true;
    }
    InterfaceNHKey key(intf_.Clone(), policy, flags_);
    nh = static_cast<NextHop *>(agent->nexthop_table()->FindActiveEntry(&key));

    if (path->label() != mpls_label_) {
        path->set_label(mpls_label_);
        ret = true;
    }

    if (path->vxlan_id() != vxlan_id_) {
        path->set_vxlan_id(vxlan_id_);
        ret = true;
    }

    if (path->dest_vn_name() != dest_vn_name_) {
        path->set_dest_vn_name(dest_vn_name_);
        ret = true;
    }

    path_sg_list = path->sg_list();
    if (path_sg_list != sg_list_) {
        path->set_sg_list(sg_list_);
        ret = true;
    }

    //Priority and sequence no of path are updated from path
    //preference state machine
    //Path preference value enqueued here would be copied
    //only if
    //1> ecmp field is set to true, meaning path would be
    //   active-active
    //2> static preference is set, meaning external entity
    //   would specify the preference of this path(ex LBaaS)
    //3> Change in priority when static preference is set
    if (path->path_preference().ConfigChanged(path_preference_)) {
        path->set_path_preference(path_preference_);
        ret = true;
    }

    if (path->peer() && path->peer()->GetType() == Peer::BGP_PEER) {
        //Copy entire path preference for BGP peer path,
        //since allowed-address pair config doesn't modify
        //preference on BGP path
        if (path->path_preference() != path_preference_) {
            path->set_path_preference(path_preference_);
            ret = true;
        }
    }

    // When BGP path was added, the policy flag in BGP path was based on
    // interface config at that instance. If the policy flag changes in
    // path for "Local Peer", we should change policy flag on BGP peer
    // also. Check if policy has changed and enable SYNC of all path in
    // this case
    // Ideally his is needed only for LocalPath. But, having code for all
    // paths does not have any problem
    bool old_policy = false;
    bool new_policy = false;
    if (path->ComputeNextHop(agent) && path->ComputeNextHop(agent)->PolicyEnabled())
        old_policy = true;
    if (nh && nh->PolicyEnabled())
        new_policy = true;
    if (old_policy != new_policy) {
        sync_route_ = true;
    }

    if (path->subnet_gw_ip() != subnet_gw_ip_) {
        path->set_subnet_gw_ip(subnet_gw_ip_);
        ret = true;
    }

    path->set_unresolved(false);
    path->SyncRoute(sync_route_);
    if (path->ChangeNH(agent, nh) == true)
        ret = true;

    return ret;
}

bool VlanNhRoute::AddChangePath(Agent *agent, AgentPath *path,
                                const AgentRoute *rt) {
    bool ret = false;
    NextHop *nh = NULL;
    SecurityGroupList path_sg_list;

    assert(intf_.type_ == Interface::VM_INTERFACE);
    VlanNHKey key(intf_.uuid_, tag_);

    nh = static_cast<NextHop *>(agent->nexthop_table()->FindActiveEntry(&key));
    if (nh) {
        assert(nh->GetType() == NextHop::VLAN);
    }

    if (path->label() != label_) {
        path->set_label(label_);
        ret = true;
    }

    if (path->dest_vn_name() != dest_vn_name_) {
        path->set_dest_vn_name(dest_vn_name_);
        ret = true;
    }

    path_sg_list = path->sg_list();
    if (path_sg_list != sg_list_) {
        path->set_sg_list(sg_list_);
        ret = true;
    }

    //Copy over entire path preference structure, whenever there is a
    //transition from active-active to active-backup struture
    if (path->path_preference().ecmp() != path_preference_.ecmp()) {
        path->set_path_preference(path_preference_);
        ret = true;
    }

    path->set_tunnel_bmap(tunnel_bmap_);
    TunnelType::Type tunnel_type = TunnelType::ComputeType(tunnel_bmap_);
    if (tunnel_type != path->tunnel_type()) {
        path->set_tunnel_type(tunnel_type);
        ret = true;
    }

    path->set_unresolved(false);
    if (path->ChangeNH(agent, nh) == true) {
        ret = true;
    }

    return ret;
}

bool ResolveRoute::AddChangePath(Agent *agent, AgentPath *path,
                                 const AgentRoute *rt) {
    bool ret = false;
    NextHop *nh = NULL;
    ResolveNHKey key(intf_key_.get(), policy_);

    nh = static_cast<NextHop *>(agent->nexthop_table()->FindActiveEntry(&key));
    path->set_unresolved(false);

    if (path->dest_vn_name() != dest_vn_name_) {
        path->set_dest_vn_name(dest_vn_name_);
        ret = true;
    }

    if (path->label() != label_) {
        path->set_label(label_);
        ret = true;
    }

    if (path->sg_list() != path_sg_list_) {
        path->set_sg_list(path_sg_list_);
        ret = true;
    }

    //By default resolve route on gateway interface
    //is supported with MPLSoGRE or MplsoUdp port
    path->set_tunnel_bmap(TunnelType::MplsType());
    TunnelType::Type new_tunnel_type =
        TunnelType::ComputeType(TunnelType::MplsType());
    if (path->tunnel_type() != new_tunnel_type) {
        path->set_tunnel_type(new_tunnel_type);
    }

    if (path->ChangeNH(agent, nh) == true)
        ret = true;

    return ret;
}

bool ReceiveRoute::AddChangePath(Agent *agent, AgentPath *path,
                                 const AgentRoute *rt) {
    bool ret = false;
    NextHop *nh = NULL;

    //TODO check if it needs to know table type
    ReceiveNHKey key(intf_.Clone(), policy_);
    nh = static_cast<NextHop *>(agent->nexthop_table()->FindActiveEntry(&key));
    path->set_unresolved(false);

    if (path->dest_vn_name() != vn_) {
        path->set_dest_vn_name(vn_);
        ret = true;
    }

    if (path->label() != label_) {
        path->set_label(label_);
        ret = true;
    }

    if (path->ChangeNH(agent, nh) == true)
        ret = true;

    return ret;
}

bool ReceiveRoute::UpdateRoute(AgentRoute *rt) {
    bool ret = false;
    AgentRouteTable *table = static_cast<AgentRouteTable *>(rt->get_table());
    if ((table->GetTableType() != Agent::INET4_UNICAST) && 
        (table->GetTableType() != Agent::INET6_UNICAST))
        return ret;

    InetUnicastRouteEntry *uc_rt =
        static_cast<InetUnicastRouteEntry *>(rt);
    if (uc_rt->proxy_arp() != proxy_arp_) {
        uc_rt->set_proxy_arp(proxy_arp_);
        ret = true;
    }
    return ret;
}

bool MulticastRoute::AddChangePath(Agent *agent, AgentPath *path,
                                   const AgentRoute *rt) {
    bool ret = false;
    NextHop *nh = NULL;

    agent->nexthop_table()->Process(composite_nh_req_);
    nh = static_cast<NextHop *>(agent->nexthop_table()->
            FindActiveEntry(composite_nh_req_.key.get()));
    assert(nh);
    ret = MulticastRoute::CopyPathParameters(agent,
                                             path,
                                             vn_name_,
                                             false,
                                             vxlan_id_,
                                             label_,
                                             tunnel_type_,
                                             nh);
    return ret;
}

bool MulticastRoute::CopyPathParameters(Agent *agent,
                                        AgentPath *path,
                                        const std::string &vn_name,
                                        bool unresolved,
                                        uint32_t vxlan_id,
                                        uint32_t label,
                                        uint32_t tunnel_type,
                                        NextHop *nh) {
    path->set_dest_vn_name(vn_name);
    path->set_unresolved(unresolved);
    path->set_vxlan_id(vxlan_id);
    path->set_label(label);

    //Setting of tunnel is only for simulated TOR.
    path->set_tunnel_bmap(tunnel_type);
    TunnelType::Type new_tunnel_type =
        TunnelType::ComputeType(tunnel_type);
    if (new_tunnel_type == TunnelType::VXLAN &&
        vxlan_id == VxLanTable::kInvalidvxlan_id) {
        new_tunnel_type = TunnelType::ComputeType(TunnelType::MplsType());
    }

    if (path->tunnel_type() != new_tunnel_type) {
        path->set_tunnel_type(new_tunnel_type);
    }

    path->ChangeNH(agent, nh);

    return true;
}

bool PathPreferenceData::AddChangePath(Agent *agent, AgentPath *path,
                                       const AgentRoute *rt) {
    bool ret = false;
    //ECMP flag will not be changed by path preference module,
    //hence retain value in path
    if (!path) {
        return ret;
    }

    path_preference_.set_ecmp(path->path_preference().ecmp());
    if (path &&
        path->path_preference() != path_preference_) {
        path->set_path_preference(path_preference_);
        ret = true;
    }
    return ret;
}

// Subnet Route route data
IpamSubnetRoute::IpamSubnetRoute(DBRequest &nh_req,
                                 const std::string &dest_vn_name) :
    AgentRouteData(false), dest_vn_name_(dest_vn_name) {
    nh_req_.Swap(&nh_req);
}

bool IpamSubnetRoute::AddChangePath(Agent *agent, AgentPath *path,
                                const AgentRoute *rt) {
    agent->nexthop_table()->Process(nh_req_);
    NextHop *nh = static_cast<NextHop *>(agent->nexthop_table()->
                                    FindActiveEntry(nh_req_.key.get()));
    assert(nh);
    
    bool ret = false;

    if (path->ChangeNH(agent, nh) == true) {
        ret = true;
    }
    path->set_is_subnet_discard(true);

    if (path->dest_vn_name() != dest_vn_name_) {
        path->set_dest_vn_name(dest_vn_name_);
        ret = true;
    }

    //Resync of subnet route is needed for identifying if arp flood flag
    //needs to be enabled for all the smaller subnets present w.r.t. this subnet
    //route. 
    AgentRouteTable *table = static_cast<AgentRouteTable *>(rt->get_table());
    assert((table->GetTableType() == Agent::INET4_UNICAST) ||
           (table->GetTableType() == Agent::INET6_UNICAST));

    InetUnicastAgentRouteTable *uc_rt_table =
        static_cast<InetUnicastAgentRouteTable *>(table);
    const InetUnicastRouteEntry *uc_rt =
        static_cast<const InetUnicastRouteEntry *>(rt);
    uc_rt_table->ResyncSubnetRoutes(uc_rt, true);
    return ret;
}

bool IpamSubnetRoute::UpdateRoute(AgentRoute *rt) {
    bool ret = false;
    InetUnicastRouteEntry *uc_rt =
        static_cast<InetUnicastRouteEntry *>(rt);
    if (uc_rt->ipam_subnet_route() != true) {
        uc_rt->set_ipam_subnet_route(true);
        ret = true;
    }

    if (uc_rt->proxy_arp() == true) {
        uc_rt->set_proxy_arp(false);
        ret =true;
    }

    return ret;
}

///////////////////////////////////////////////
// Sandesh routines below (route_sandesh.cc) 
//////////////////////////////////////////////
//TODO make it generic 
void UnresolvedNH::HandleRequest() const {

    VrfEntry *vrf = Agent::GetInstance()->vrf_table()->FindVrfFromId(0);
    if (!vrf) {
        ErrorResp *resp = new ErrorResp();
        resp->set_context(context());
        resp->Response();
        return;
    }
   
    int count = 0;
    std::string empty(""); 
    AgentRouteTable *rt_table = static_cast<AgentRouteTable *>
        (vrf->GetInet4UnicastRouteTable());
    NhListResp *resp = new NhListResp();

    //TODO - Convert inet4ucroutetable to agentroutetable
    AgentRouteTable::UnresolvedNHTree::const_iterator it;
    it = rt_table->unresolved_nh_begin();
    for (;it != rt_table->unresolved_nh_end(); it++) {
        count++;
        const NextHop *nh = *it;
        nh->DBEntrySandesh(resp, empty);
        if (count == 1) {
            resp->set_context(context()+"$");
            resp->Response();
            count = 0;
            resp = new NhListResp();
        }
    }

    resp->set_context(context());
    resp->Response();
    return;
}

//TODO IMplement filltrace in path class
void AgentRoute::FillTrace(RouteInfo &rt_info, Trace event, 
                           const AgentPath *path) {
    Agent *agent = static_cast<AgentRouteTable *>(get_table())->agent();
    rt_info.set_ip(ToString());
    rt_info.set_vrf(vrf()->GetName());

    switch(event) {
    case ADD:{
        rt_info.set_op("ADD");
        break;
    }

    case DELETE: {
        rt_info.set_op("DELETE");
        break;
    }

    case ADD_PATH:
    case DELETE_PATH:
    case STALE_PATH:
    case CHANGE_PATH: {
        if (event == ADD_PATH) {
            rt_info.set_op("PATH ADD");
        } else if (event == CHANGE_PATH) {
            rt_info.set_op("PATH CHANGE");
        } else if (event == DELETE_PATH) {
            rt_info.set_op("PATH DELETE");
        } else if (event == STALE_PATH) {
            rt_info.set_op("PATH STALE");
        }

        if (path == NULL) {
            rt_info.set_nh_type("<NULL>");
            break;
        }

        if (path->peer()) {
            rt_info.set_peer(path->peer()->GetName());
        }
        rt_info.set_ecmp(path->path_preference().ecmp());
        const NextHop *nh = path->ComputeNextHop(agent);
        if (nh == NULL) {
            rt_info.set_nh_type("<NULL>");
            break;
        }

        switch (nh->GetType()) {
        case NextHop::TUNNEL: {
            const TunnelNH *tun = static_cast<const TunnelNH *>(nh);
            rt_info.set_nh_type("TUNNEL");
            rt_info.set_dest_server(tun->GetDip()->to_string());
            rt_info.set_dest_server_vrf(tun->GetVrf()->GetName());
            break;
        }

        case NextHop::ARP:{
            rt_info.set_nh_type("DIRECT");
            break;
        }

        case NextHop::INTERFACE: {
            const InterfaceNH *intf_nh = static_cast<const InterfaceNH *>(nh);
            rt_info.set_nh_type("INTERFACE");
            rt_info.set_intf(intf_nh->GetInterface()->name());
            break;
        }

        case NextHop::RECEIVE: {
            const ReceiveNH *rcv_nh = static_cast<const ReceiveNH *>(nh);
            rt_info.set_nh_type("RECEIVE");
            rt_info.set_intf(rcv_nh->GetInterface()->name());
            break;
        }

        case NextHop::DISCARD: {
            rt_info.set_nh_type("DISCARD");
            break;
        }

        case NextHop::VLAN: {
            rt_info.set_nh_type("VLAN");
            break;
        }

        case NextHop::RESOLVE: {
            rt_info.set_nh_type("RESOLVE");
            break;
        }

        case NextHop::COMPOSITE: {
            rt_info.set_nh_type("COMPOSITE");
            break;
        }
 
        case NextHop::L2_RECEIVE: {
            rt_info.set_nh_type("L2_RECEIVE");
            break;
        }
  
        default:
            assert(0);
            break;
        }
       break;
    }
    }
}

void AgentPath::SetSandeshData(PathSandeshData &pdata) const {
    const NextHop *nh = nexthop();
    if (nh != NULL) {
        nh->SetNHSandeshData(pdata.nh);
    }
    pdata.set_peer(const_cast<Peer *>(peer())->GetName());
    pdata.set_dest_vn(dest_vn_name());
    pdata.set_unresolved(unresolved() ? "true" : "false");

    if (!gw_ip().is_unspecified()) {
        pdata.set_gw_ip(gw_ip().to_string());
        pdata.set_vrf(vrf_name());
    }

    pdata.set_sg_list(sg_list());
    pdata.set_vxlan_id(vxlan_id());
    pdata.set_label(label());
    pdata.set_active_tunnel_type(
            TunnelType(tunnel_type()).ToString());
    pdata.set_supported_tunnel_type(
            TunnelType::GetString(tunnel_bmap()));
    pdata.set_stale(is_stale());
    PathPreferenceSandeshData path_preference_data;
    path_preference_data.set_sequence(path_preference_.sequence());
    path_preference_data.set_preference(path_preference_.preference());
    path_preference_data.set_ecmp(path_preference_.ecmp());
    path_preference_data.set_wait_for_traffic(
         path_preference_.wait_for_traffic());
    pdata.set_path_preference_data(path_preference_data);
    pdata.set_active_label(GetActiveLabel());
    pdata.set_flood_dhcp(flood_dhcp() ? "true" : "false");
}

void AgentPath::set_local_ecmp_mpls_label(MplsLabel *mpls) {
    local_ecmp_mpls_label_.reset(mpls);
}

const MplsLabel* AgentPath::local_ecmp_mpls_label() const {
    return local_ecmp_mpls_label_.get();
}

bool AgentPath::ReorderCompositeNH(Agent *agent,
                                   CompositeNHKey *composite_nh_key) {
    //Find local composite mpls label, if present
    //This has to be done, before expanding component NH
    BOOST_FOREACH(ComponentNHKeyPtr component_nh_key,
                  composite_nh_key->component_nh_key_list()) {
         if (component_nh_key.get() == NULL ||
                 component_nh_key->nh_key()->GetType() != NextHop::COMPOSITE) {
             continue;
         }
         //Get mpls label allocated for this composite NH
         MplsLabel *mpls = agent->mpls_table()->
             FindMplsLabel(component_nh_key->label());
         if (!mpls) {
             //If a mpls label is deleted,
             //wait for bgp to update latest list
             local_ecmp_mpls_label_.reset(mpls);
             return false;
         }
         local_ecmp_mpls_label_.reset(mpls);
         break;
     }

    //Make a copy of composite NH, so that aggregarate mpls
    //label allocated for local composite ecmp is maintained
    //as data in path
    CompositeNHKey *comp_key = composite_nh_key->Clone();
    //Reorder the keys so that, existing component NH maintain
    //there previous position
    //For example take a composite NH with members A, B, C
    //in that exact order,If B gets deleted,
    //the new composite NH created should be A <NULL> C in that order,
    //irrespective of the order user passed it in
    composite_nh_key->Reorder(agent, label_, ComputeNextHop(agent));
    //Copy the unchanged component NH list to path data
    set_composite_nh_key(comp_key);
    return true;
}

bool AgentPath::ChangeCompositeNH(Agent *agent,
                                  CompositeNHKey *composite_nh_key) {
    DBRequest nh_req(DBRequest::DB_ENTRY_ADD_CHANGE);
    nh_req.key.reset(composite_nh_key->Clone());
    nh_req.data.reset(new CompositeNHData());
    agent->nexthop_table()->Process(nh_req);

    NextHop *nh = static_cast<NextHop *>(agent->nexthop_table()->
            FindActiveEntry(composite_nh_key));
    assert(nh);

    if (ChangeNH(agent, nh) == true) {
        return true;
    }
    return false;
}

const Ip4Address *AgentPath::NexthopIp(Agent *agent) const {
    if (peer_ == NULL) {
        return agent->router_ip_ptr();
    }

    return peer_->NexthopIp(agent, this);
}
