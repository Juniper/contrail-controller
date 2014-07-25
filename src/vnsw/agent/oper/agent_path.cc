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
    sync_(false), proxy_arp_(false), force_policy_(false), sg_list_(),
    server_ip_(0), tunnel_bmap_(TunnelType::AllType()),
    tunnel_type_(TunnelType::ComputeType(TunnelType::AllType())),
    vrf_name_(""), gw_ip_(0), unresolved_(true), is_stale_(false),
    is_subnet_discard_(false), dependant_rt_(rt), path_preference_(),
    local_ecmp_mpls_label_(rt), composite_nh_key_(NULL) {
}

AgentPath::~AgentPath() {
    clear_sg_list();
}

uint32_t AgentPath::GetTunnelBmap() const {
    TunnelType::Type type = TunnelType::ComputeType(TunnelType::AllType());
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

const NextHop* AgentPath::nexthop(Agent *agent) const {
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
    if (nh == NULL) {
        nh = agent->nexthop_table()->discard_nh();
    }

    if (nh_ != nh) {
        nh_ = nh;
        return true;
    }
    return false;
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
    if (sync_route->is_multicast()) {
        new_tunnel_type = TunnelType::ComputeType(TunnelType::MplsType());
        if (new_tunnel_type == TunnelType::VXLAN) {
            new_tunnel_type = TunnelType::MPLS_GRE;
        }
    } else {
        new_tunnel_type = TunnelType::ComputeType(tunnel_bmap_);
    }

    CompositeNH *new_composite_nh = NULL;
    new_composite_nh = cnh->ChangeTunnelType(agent, new_tunnel_type);
    return ChangeNH(agent, new_composite_nh);
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
    if (tunnel_type_ == TunnelType::ComputeType(tunnel_bmap_)) {
        return false;
    }

    tunnel_type_ = TunnelType::ComputeType(tunnel_bmap_);
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
        composite_nh_key_.get() != NULL) {
        if (SetCompositeNH(agent, composite_nh_key_.get(), true)) {
            ret = true;
        }
    }

    if (vrf_name_ == Agent::NullString()) {
        return ret;
    }

    Inet4UnicastAgentRouteTable *table = NULL;
    Inet4UnicastRouteEntry *rt = NULL;
    table = static_cast<Inet4UnicastAgentRouteTable *>
        (agent->vrf_table()->GetInet4UnicastRouteTable(vrf_name_));
    if (table)
        rt = table->FindRoute(gw_ip_);

    if (rt == sync_route) {
        rt = NULL;
    }

    if (rt == NULL || rt->plen() == 0) {
        unresolved = true;
    } else if (rt->GetActiveNextHop()->GetType() == NextHop::RESOLVE) {
        table->AddArpReq(vrf_name_, gw_ip_);
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

bool HostRoute::AddChangePath(Agent *agent, AgentPath *path) {
    bool ret = false;
    NextHop *nh = NULL;
    InterfaceNHKey key(intf_.Clone(), false, InterfaceNHFlags::INET4);
    nh = static_cast<NextHop *>(agent->nexthop_table()->FindActiveEntry(&key));
    if (path->dest_vn_name() != dest_vn_name_) {
        path->set_dest_vn_name(dest_vn_name_);
        ret = true;
    }
    if (path->proxy_arp() != proxy_arp_) {
        path->set_proxy_arp(proxy_arp_);
        ret = true;
    }

    path->set_unresolved(false);
    if (path->ChangeNH(agent, nh) == true)
        ret = true;

    return ret;
} 

bool InetInterfaceRoute::AddChangePath(Agent *agent, AgentPath *path) {
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

bool DropRoute::AddChangePath(Agent *agent, AgentPath *path) {
    bool ret = false;

    if (path->is_subnet_discard() != is_subnet_discard_) {
        path->set_is_subnet_discard(is_subnet_discard_);
        ret = true;
    }

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

bool LocalVmRoute::AddChangePath(Agent *agent, AgentPath *path) {
    bool ret = false;
    NextHop *nh = NULL;
    SecurityGroupList path_sg_list;

    //TODO Based on key table type pick up interface
    VmInterfaceKey intf_key(AgentKey::ADD_DEL_CHANGE, intf_.uuid_, "");
    VmInterface *vm_port = static_cast<VmInterface *>
        (agent->interface_table()->FindActiveEntry(&intf_key));

    bool policy = false;
    // Use policy based NH if policy enabled on interface
    if (vm_port && vm_port->policy_enabled()) {
        policy = true;
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

    if (path->proxy_arp() != proxy_arp_) {
        path->set_proxy_arp(proxy_arp_);
        ret = true;
    }

    path_sg_list = path->sg_list();
    if (path_sg_list != sg_list_) {
        path->set_sg_list(sg_list_);
        ret = true;
    }

    //If there is a transition in path from active-active to
    //ative-backup or vice-versa copy over entire path preference structure
    if (path->path_preference().ecmp() != path_preference_.ecmp()) {
        path->set_path_preference(path_preference_);
        ret = true;
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
    if (path->nexthop(agent) && path->nexthop(agent)->PolicyEnabled())
        old_policy = true;
    if (nh && nh->PolicyEnabled())
        new_policy = true;
    if (old_policy != new_policy) {
        sync_route_ = true;
    }

    path->set_unresolved(false);
    path->SyncRoute(sync_route_);
    if (path->ChangeNH(agent, nh) == true)
        ret = true;

    return ret;
}

bool VlanNhRoute::AddChangePath(Agent *agent, AgentPath *path) { 
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

    path->set_unresolved(false);
    if (path->ChangeNH(agent, nh) == true) {
        ret = true;
    }

    return ret;
}

bool ResolveRoute::AddChangePath(Agent *agent, AgentPath *path) {
    bool ret = false;
    NextHop *nh = NULL;
    ResolveNHKey key;

    nh = static_cast<NextHop *>(agent->nexthop_table()->FindActiveEntry(&key));
    path->set_unresolved(false);
    if (path->dest_vn_name() != agent->fabric_vn_name()) {
        path->set_dest_vn_name(agent->fabric_vn_name());
        ret = true;
    }
    if (path->ChangeNH(agent, nh) == true)
        ret = true;

    return ret;
}

bool ReceiveRoute::AddChangePath(Agent *agent, AgentPath *path) {
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

    if (path->proxy_arp() != proxy_arp_) {
        path->set_proxy_arp(proxy_arp_);
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

bool MulticastRoute::AddChangePath(Agent *agent, AgentPath *path) {
    bool ret = false;
    NextHop *nh = NULL;

    nh = static_cast<NextHop *>(agent->nexthop_table()->
            FindActiveEntry(composite_nh_req_.key.get()));
    if (nh == NULL) {
        nh = static_cast<NextHop *>(agent->nexthop_table()->
                            FindActiveEntry(composite_nh_req_.key.get()));
    }
    assert(nh);
    path->set_dest_vn_name(vn_name_);
    path->set_unresolved(false);
    path->set_vxlan_id(vxlan_id_);
    ret = true;

    if (path->ChangeNH(agent, nh) == true)
        ret = true;

    return ret;
}

bool PathPreferenceData::AddChangePath(Agent *agent, AgentPath *path) {
    bool ret = false;
    if (path &&
        path->path_preference() != path_preference_) {
        path->set_path_preference(path_preference_);
        ret = true;
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
        const NextHop *nh = path->nexthop(agent);
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
  
        default:
            assert(0);
            break;
        }
       break;
    }
    }
}

void AgentPath::SetSandeshData(PathSandeshData &pdata) const {
    if (nh_.get() != NULL) {
        nh_->SetNHSandeshData(pdata.nh);
    }
    pdata.set_peer(const_cast<Peer *>(peer())->GetName());
    pdata.set_dest_vn(dest_vn_name());
    pdata.set_unresolved(unresolved() ? "true" : "false");

    if (!gw_ip().is_unspecified()) {
        pdata.set_gw_ip(gw_ip().to_string());
        pdata.set_vrf(vrf_name());
    }
    if (proxy_arp()) {
        pdata.set_proxy_arp("ProxyArp");
    }

    pdata.set_sg_list(sg_list());
    if ((tunnel_type() == TunnelType::VXLAN)) {
        pdata.set_vxlan_id(vxlan_id());
    } else {
        pdata.set_label(label());
    }
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
}

void AgentPath::set_local_ecmp_mpls_label(MplsLabel *mpls) {
    local_ecmp_mpls_label_.reset(mpls);
}

const MplsLabel* AgentPath::local_ecmp_mpls_label() const {
    return local_ecmp_mpls_label_.get();
}

bool AgentPath::SetCompositeNH(Agent *agent,
                               CompositeNHKey *composite_nh_key, bool create) {
    bool ret = false;
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
    composite_nh_key->Reorder(agent, label_, nexthop(agent));
    //Create the nexthop
    if (create) {
        DBRequest nh_req(DBRequest::DB_ENTRY_ADD_CHANGE);
        nh_req.key.reset(composite_nh_key->Clone());
        nh_req.data.reset(new CompositeNHData());
        agent->nexthop_table()->Process(nh_req);

        NextHop *nh = static_cast<NextHop *>(agent->nexthop_table()->
                FindActiveEntry(composite_nh_key));
        assert(nh);

        if (ChangeNH(agent, nh) == true) {
            ret = true;
        }
    }
    //Copy the unchanged component NH list to path data
    set_composite_nh_key(comp_key);
    return ret;
}
