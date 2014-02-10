/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/uuid/uuid_io.hpp>
#include <boost/lexical_cast.hpp>

#include <cmn/agent_cmn.h>
#include <route/route.h>

#include <cmn/agent_cmn.h>
#include <oper/route_common.h>
#include <oper/vrf.h>
#include <oper/tunnel_nh.h>
#include <oper/mpls.h>
#include <oper/mirror_table.h>
#include <controller/controller_export.h>
#include <oper/agent_sandesh.h>

using namespace std;
using namespace boost::asio;

uint32_t AgentPath::GetTunnelBmap() const {
    if ((TunnelType::ComputeType(TunnelType::AllType()) == 
         (1 << TunnelType::VXLAN)) &&
        (vxlan_id_ != 0)) {
        return (1 << TunnelType::VXLAN);
    } else {
        return (TunnelType::MplsType());
    }
}

uint32_t AgentPath::GetActiveLabel() const {
    if (tunnel_type_ == TunnelType::VXLAN) {
        return vxlan_id_;
    } else {
        return label_;
    }
}

const NextHop* AgentPath::GetNextHop(void) const {
    if (nh_) {
        return nh_.get();
    }

    if (unresolved_ == true) {
        DiscardNH key;
        return static_cast<NextHop *>
            (Agent::GetInstance()->GetNextHopTable()->FindActiveEntry(&key));
    }

    //Indirect route's path, get direct route's NH
    const NextHop *nh = dependant_rt_.get()->GetActiveNextHop();
    if (nh == NULL) {
        assert(0);
    }
    return nh;
}

bool AgentPath::ChangeNH(NextHop *nh) {
    // If NH is not found, point route to discard NH
    if (nh == NULL) {
        //TODO convert to oper_trace
        //LOG(DEBUG, "NH not found for route <" << path->vrf_name_ << 
        //    ":"  << rt_key->addr_.to_string() << "/" << rt_key->plen_
        //    << ">. Setting NH to Discard NH ");
        DiscardNHKey key;
        nh = static_cast<NextHop *>(Agent::GetInstance()->
                                    GetNextHopTable()->FindActiveEntry(&key));
    }

    if (nh_ != nh) {
        nh_ = nh;
        return true;
    }
    return false;
}

bool AgentPath::RebakeAllTunnelNHinCompositeNH(const AgentRoute *sync_route, 
                                               const NextHop *nh) {
    bool ret = false;
    const CompositeNH *cnh = static_cast<const CompositeNH *>(nh);
    const CompositeNH::ComponentNHList *comp_nh_list = 
        cnh->GetComponentNHList();

    TunnelType::Type new_tunnel_type;
    //Only MPLS types are supported for multicast
    if (sync_route->IsMulticast()) {
        new_tunnel_type = TunnelType::ComputeType(TunnelType::MplsType());
        if (new_tunnel_type == TunnelType::VXLAN)
            new_tunnel_type = TunnelType::MPLS_GRE;
    } else {
        new_tunnel_type = TunnelType::ComputeType(tunnel_bmap_);              
    }        
    for (CompositeNH::ComponentNHList::const_iterator it =
         comp_nh_list->begin(); it != comp_nh_list->end(); it++) {
        if ((*it) == NULL) {
            continue;
        }

        const NextHop *nh = (*it)->GetNH();
        switch (nh->GetType()) {
        case NextHop::TUNNEL: {
            const TunnelNH *tnh = static_cast<const TunnelNH *>(nh);
            if (new_tunnel_type != tnh->GetTunnelType().GetType()) {
                NextHopKey *tnh_key = 
                    new TunnelNHKey(tnh->GetVrf()->GetName(), *(tnh->GetSip()), 
                                    *(tnh->GetDip()), tnh->PolicyEnabled(), 
                                    new_tunnel_type);
                DBRequest tnh_req;
                tnh_req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
                TunnelNHData *tnh_data = new TunnelNHData();
                tnh_req.key.reset(tnh_key);
                tnh_req.data.reset(tnh_data);
                Agent::GetInstance()->GetNextHopTable()->Process(tnh_req);
                tnh_key = 
                    new TunnelNHKey(tnh->GetVrf()->GetName(), *(tnh->GetSip()), 
                                    *(tnh->GetDip()), tnh->PolicyEnabled(), 
                                    new_tunnel_type);
                NextHop *new_tnh = static_cast<NextHop *>(Agent::GetInstance()->
                                               GetNextHopTable()->
                                               FindActiveEntry(tnh_key)); 
                (*it)->SetNH(new_tnh);
                ret = true;
            }
            break;
        }
        case NextHop::COMPOSITE: {
            ret = RebakeAllTunnelNHinCompositeNH(sync_route, nh);
            break;
        }      
        default:
            continue;
            break;
        }
    }

    if (ret) {
        //Resync the parent composite NH
        DBRequest cnh_req;
        
        NextHopKey *cnh_key = NULL;
        if (sync_route->IsMulticast()) {
            cnh_key = new CompositeNHKey(cnh->GetVrfName(), 
                                         cnh->GetGrpAddr(), 
                                         cnh->GetSrcAddr(),
                                         cnh->IsLocal(),
                                         cnh->CompositeType());
        } else {
            cnh_key = new CompositeNHKey(cnh->GetVrfName(), 
                                         cnh->GetGrpAddr(), 
                                         cnh->prefix_len(), 
                                         cnh->IsLocal());
        }
        cnh_key->sub_op_ = AgentKey::RESYNC;
        cnh_req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
        cnh_req.key.reset(cnh_key);
        CompositeNHData *cnh_data = 
            new CompositeNHData(CompositeNHData::REBAKE);
        cnh_req.data.reset(cnh_data);
        Agent::GetInstance()->GetNextHopTable()->Process(cnh_req);
    }
    return ret;
}

bool AgentPath::Sync(AgentRoute *sync_route) {
    bool ret = false;
    bool unresolved = false;

    //Check if there is change in policy on the interface
    //If yes update the path to point to policy enabled NH
    if (nh_.get() && nh_->GetType() == NextHop::INTERFACE) {
        const InterfaceNH *intf_nh = static_cast<const InterfaceNH *>(nh_.get());
        const VmInterface *vm_port = 
            static_cast<const VmInterface *>(intf_nh->GetInterface());

        bool policy = vm_port->policy_enabled();
        if (force_policy_) {
            policy = true;
        }

        if (intf_nh->PolicyEnabled() != policy) {
            //Make path point to policy enabled interface
            InterfaceNHKey key(new VmInterfaceKey(AgentKey::ADD_DEL_CHANGE,
                                                  vm_port->GetUuid(), ""),
                                policy, intf_nh->GetFlags());
            nh_ = static_cast<NextHop *>
                (Agent::GetInstance()->
                 GetNextHopTable()->FindActiveEntry(&key));
            // If NH is not found, point route to discard NH
            if (nh_ == NULL) {
                LOG(DEBUG, "Interface NH for <" 
                    << boost::lexical_cast<std::string>(vm_port->GetUuid())
                    << " : policy = " << policy);
                DiscardNHKey key;
                nh_ = static_cast<NextHop *>
                    (Agent::GetInstance()->
                     GetNextHopTable()->FindActiveEntry(&key));
            }
        }

        if (tunnel_type_ != TunnelType::ComputeType(tunnel_bmap_)) {
            tunnel_type_ = TunnelType::ComputeType(tunnel_bmap_);
            ret = true;
        }
    }

    bool remote_vm_nh_reevaluate = false;
    //Remote vm nh add was skipped because of encap mismatch
    if (nh_.get() && nh_->GetType() == NextHop::TUNNEL) {
        remote_vm_nh_reevaluate = true;
    }

    if (remote_vm_nh_reevaluate) {
        if (tunnel_type_ != TunnelType::ComputeType(tunnel_bmap_)) {
            tunnel_type_ = TunnelType::ComputeType(tunnel_bmap_);
            ret = true;

            TunnelNHKey *tnh_key = 
                new TunnelNHKey(Agent::GetInstance()->GetDefaultVrf(), 
                                Agent::GetInstance()->GetRouterId(),
                                server_ip_, false, tunnel_type_);
            DBRequest nh_req;
            nh_req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
            nh_req.key.reset(tnh_key);
            TunnelNHData *nh_data = new TunnelNHData();
            nh_req.data.reset(nh_data);
            Agent::GetInstance()->GetNextHopTable()->Process(nh_req);
            tnh_key = new TunnelNHKey(Agent::GetInstance()->GetDefaultVrf(), 
                                      Agent::GetInstance()->GetRouterId(),
                                      server_ip_, false, tunnel_type_);
            nh_ = static_cast<NextHop *>
                (Agent::GetInstance()->
                 GetNextHopTable()->FindActiveEntry(tnh_key));
        }
    }

    if (nh_.get() && nh_->GetType() == NextHop::COMPOSITE) {
        ret = RebakeAllTunnelNHinCompositeNH(sync_route, nh_.get());
    }

    if (vrf_name_ == Agent::GetInstance()->NullString()) {
        return ret;
    }
 
    Inet4UnicastRouteEntry *rt = 
        Inet4UnicastAgentRouteTable::FindRoute(vrf_name_, gw_ip_);
    if (rt == sync_route) {
        rt = NULL;
    }

    if (rt == NULL || rt->GetPlen() == 0) {
        unresolved = true;
    } else if (rt->GetActiveNextHop()->GetType() == NextHop::RESOLVE) {
        Inet4UnicastAgentRouteTable::AddArpReq(vrf_name_, gw_ip_);
        unresolved = true;
    } else {
        unresolved = false;
    }

    if (unresolved_ != unresolved) {
        unresolved_ = unresolved;
        ret = true;
    }
    //Reset to new gateway route, no nexthop for indirect route
    if (dependant_rt_.get() != rt) {
        dependant_rt_.reset(rt);
        ret = true;
    }
    return ret;
}

bool HostRoute::AddChangePath(AgentPath *path) {
    bool ret = false;
    NextHop *nh = NULL;
    InterfaceNHKey key(intf_.Clone(), false, InterfaceNHFlags::INET4);
    nh = static_cast<NextHop *>(Agent::GetInstance()->
                                GetNextHopTable()->FindActiveEntry(&key));
    if (path->GetDestVnName() != dest_vn_name_) {
        path->SetDestVnName(dest_vn_name_);
        ret = true;
    }
    if (path->GetProxyArp() != proxy_arp_) {
        path->SetProxyArp(proxy_arp_);
        ret = true;
    }

    path->SetUnresolved(false);
    if (path->ChangeNH(nh) == true)
        ret = true;

    return ret;
} 

bool InetInterfaceRoute::AddChangePath(AgentPath *path) {
    bool ret = false;
    NextHop *nh = NULL;
    InterfaceNHKey key(intf_.Clone(), false, InterfaceNHFlags::INET4);
    nh = static_cast<NextHop *>(Agent::GetInstance()->
                                GetNextHopTable()->FindActiveEntry(&key));
    if (path->GetDestVnName() != dest_vn_name_) {
        path->SetDestVnName(dest_vn_name_);
        ret = true;
    }

    if (path->GetLabel() != label_) {
        path->SetLabel(label_);
        ret = true;
    }

    path->SetUnresolved(false);
    if (path->ChangeNH(nh) == true)
        ret = true;

    return ret;
}

bool DropRoute::AddChangePath(AgentPath *path) {
    NextHop *nh = NULL;
    DiscardNHKey key;
    nh = static_cast<NextHop *>(Agent::GetInstance()->
                                GetNextHopTable()->FindActiveEntry(&key));
    path->SetUnresolved(false);
    if (path->ChangeNH(nh) == true)
        return true;

    return false;
}

bool LocalVmRoute::AddChangePath(AgentPath *path) {
    bool ret = false;
    NextHop *nh = NULL;
    SecurityGroupList path_sg_list;

    //TODO Based on key table type pick up interface
    VmInterfaceKey intf_key(AgentKey::ADD_DEL_CHANGE, intf_.uuid_, "");
    VmInterface *vm_port = static_cast<VmInterface *>
        (Agent::GetInstance()->GetInterfaceTable()->FindActiveEntry(&intf_key));

    bool policy = false;
    // Use policy based NH if policy enabled on interface
    if (vm_port && vm_port->policy_enabled()) {
        policy = true;
    }

    path->SetTunnelBmap(tunnel_bmap_);
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
    path->SetForcePolicy(force_policy_);
    if (force_policy_) {
        policy = true;
    }
    InterfaceNHKey key(intf_.Clone(), policy, flags_);
    nh = static_cast<NextHop *>(Agent::GetInstance()->
                                GetNextHopTable()->FindActiveEntry(&key));

    if (path->GetLabel() != mpls_label_) {
        path->SetLabel(mpls_label_);
        ret = true;
    }

    if (path->vxlan_id() != vxlan_id_) {
        path->set_vxlan_id(vxlan_id_);
        ret = true;
    }

    if (path->GetDestVnName() != dest_vn_name_) {
        path->SetDestVnName(dest_vn_name_);
        ret = true;
    }

    if (path->GetProxyArp() != proxy_arp_) {
        path->SetProxyArp(proxy_arp_);
        ret = true;
    }

    path_sg_list = path->GetSecurityGroupList();
    if (path_sg_list != sg_list_) {
        path->SetSecurityGroupList(sg_list_);
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
    if (path->GetNextHop() && path->GetNextHop()->PolicyEnabled())
        old_policy = true;
    if (nh && nh->PolicyEnabled())
        new_policy = true;
    if (old_policy != new_policy) {
        sync_route_ = true;
    }

    path->SetUnresolved(false);
    path->SyncRoute(sync_route_);
    if (path->ChangeNH(nh) == true)
        ret = true;

    return ret;
}

bool VlanNhRoute::AddChangePath(AgentPath *path) { 
    bool ret = false;
    NextHop *nh = NULL;
    SecurityGroupList path_sg_list;

    assert(intf_.type_ == Interface::VM_INTERFACE);
    VlanNHKey key(intf_.uuid_, tag_);

    nh = static_cast<NextHop *>(Agent::GetInstance()->
                                GetNextHopTable()->FindActiveEntry(&key));
    if (nh) {
        assert(nh->GetType() == NextHop::VLAN);
    }

    if (path->GetLabel() != label_) {
        path->SetLabel(label_);
        ret = true;
    }

    if (path->GetDestVnName() != dest_vn_name_) {
        path->SetDestVnName(dest_vn_name_);
        ret = true;
    }

    path_sg_list = path->GetSecurityGroupList();
    if (path_sg_list != sg_list_) {
        path->SetSecurityGroupList(sg_list_);
        ret = true;
    }

    path->SetUnresolved(false);
    if (path->ChangeNH(nh) == true)
        ret = true;

    return ret;
}

bool RemoteVmRoute::AddChangePath(AgentPath *path) {
    bool ret = false;
    NextHop *nh = NULL;
    SecurityGroupList path_sg_list;

    if (path->tunnel_bmap() != tunnel_bmap_) {
        path->SetTunnelBmap(tunnel_bmap_);
        ret = true;
    }

    TunnelType::Type new_tunnel_type = TunnelType::ComputeType(tunnel_bmap_);
    if ((tunnel_bmap_ == (1 << TunnelType::VXLAN) && 
         (new_tunnel_type != TunnelType::VXLAN)) ||
        (tunnel_bmap_ != (1 << TunnelType::VXLAN) &&
         (new_tunnel_type == TunnelType::VXLAN))) {
        new_tunnel_type = TunnelType::INVALID;
        NextHopKey *invalid_tunnel_nh_key = 
            new TunnelNHKey(Agent::GetInstance()->GetDefaultVrf(),
                            Agent::GetInstance()->GetRouterId(),
                            server_ip_, false, new_tunnel_type); 
        nh_req_.key.reset(invalid_tunnel_nh_key);
    }
    Agent::GetInstance()->GetNextHopTable()->Process(nh_req_);
    TunnelNHKey key(Agent::GetInstance()->GetDefaultVrf(), 
                    Agent::GetInstance()->GetRouterId(),
                    server_ip_, false, new_tunnel_type); 
    nh = static_cast<NextHop *>(Agent::GetInstance()->
                                GetNextHopTable()->FindActiveEntry(&key));
    path->set_server_ip(server_ip_);

    if (path->tunnel_type() != new_tunnel_type) {
        path->set_tunnel_type(new_tunnel_type);
        ret = true;
    }

    if (new_tunnel_type == TunnelType::VXLAN) {
        if (path->vxlan_id() != label_) {
            path->set_vxlan_id(label_);
            path->SetLabel(MplsTable::kInvalidLabel);
            ret = true;
        }
    } else {
        if (path->GetLabel() != label_) {
            path->SetLabel(label_);
            path->set_vxlan_id(VxLanTable::kInvalidvxlan_id);
            ret = true;
        }
    }

    if (path->GetDestVnName() != dest_vn_name_) {
        path->SetDestVnName(dest_vn_name_);
        ret = true;
    }

    path->SetUnresolved(false);
    if (path->ChangeNH(nh) == true)
        ret = true;

    path_sg_list = path->GetSecurityGroupList();
    if (path_sg_list != sg_list_) {
        path->SetSecurityGroupList(sg_list_);
        ret = true;
    }

    return ret;
}

bool ResolveRoute::AddChangePath(AgentPath *path) {
    bool ret = false;
    NextHop *nh = NULL;
    ResolveNHKey key;

    nh = static_cast<NextHop *>(Agent::GetInstance()->
                                GetNextHopTable()->FindActiveEntry(&key));
    path->SetUnresolved(false);
    if (path->GetDestVnName() != Agent::GetInstance()->GetFabricVnName()) {
        path->SetDestVnName(Agent::GetInstance()->GetFabricVnName());
        ret = true;
    }
    if (path->ChangeNH(nh) == true)
        ret = true;

    return ret;
}

bool ReceiveRoute::AddChangePath(AgentPath *path) {
    bool ret = false;
    NextHop *nh = NULL;

    //TODO check if it needs to know table type
    ReceiveNHKey key(intf_.Clone(), policy_);
    nh = static_cast<NextHop *>(Agent::GetInstance()->
                                GetNextHopTable()->FindActiveEntry(&key));
    path->SetUnresolved(false);

    if (path->GetDestVnName() != vn_) {
        path->SetDestVnName(vn_);
        ret = true;
    }

    if (path->GetProxyArp() != proxy_arp_) {
        path->SetProxyArp(proxy_arp_);
        ret = true;
    }

    if (path->GetLabel() != label_) {
        path->SetLabel(label_);
        ret = true;
    }

    if (path->ChangeNH(nh) == true)
        ret = true;

    return ret;
}

bool MulticastRoute::AddChangePath(AgentPath *path) {
    bool ret = false;
    NextHop *nh = NULL;

    CompositeNHKey key(vrf_name_, grp_addr_,
                       src_addr_, false, comp_type_);
    nh = static_cast<NextHop *>(Agent::GetInstance()->
                                GetNextHopTable()->FindActiveEntry(&key));
    path->SetDestVnName(vn_name_);
    path->SetUnresolved(false);
    path->set_vxlan_id(vxlan_id_);
    ret = true;

    if (path->ChangeNH(nh) == true)
        ret = true;

    return ret;
}

///////////////////////////////////////////////
// Sandesh routines below (route_sandesh.cc) 
//////////////////////////////////////////////
//TODO make it generic 
void UnresolvedNH::HandleRequest() const {

    VrfEntry *vrf = Agent::GetInstance()->GetVrfTable()->FindVrfFromId(0);
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
    rt_info.set_ip(ToString());
    rt_info.set_vrf(GetVrfEntry()->GetName());

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
    case CHANGE_PATH: {
        if (event == ADD_PATH) {
            rt_info.set_op("PATH ADD");
        } else if (event == CHANGE_PATH) {
            rt_info.set_op("PATH CHANGE");
        } else if (event == DELETE_PATH) {
            rt_info.set_op("PATH DELETE");
        }
        if (path->GetPeer()) {
            rt_info.set_peer(path->GetPeer()->GetName());
        }
        const NextHop *nh = path->GetNextHop();
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

