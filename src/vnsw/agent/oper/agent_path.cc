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

string AgentRouteTableAPIS::GetSuffix(TableType type) {
    switch (type) {
      case AgentRouteTableAPIS::INET4_UNICAST:
          return ".uc.route.0";
      case AgentRouteTableAPIS::INET4_MULTICAST:
          return ".mc.route.0";
      case AgentRouteTableAPIS::LAYER2:
          return ".l2.route.0";
      default:
          return "";
    }
}

void AgentRouteTableAPIS::CreateRouteTablesInVrf(DB *db, const string &name,
                                          AgentRouteTable *table_list[]) {
    for (int rt_table_cnt = 0; rt_table_cnt < AgentRouteTableAPIS::MAX;
         rt_table_cnt++) {
        table_list[rt_table_cnt] = static_cast<AgentRouteTable *>
            (db->CreateTable(name + AgentRouteTableAPIS::GetSuffix(
               static_cast<AgentRouteTableAPIS::TableType>(rt_table_cnt))));
    }
}

DBTableBase *AgentRouteTableAPIS::CreateRouteTable(DB *db, const std::string &name,
                                                   TableType type) {
    AgentRouteTable *table;
    size_t index;

    switch (type) {
      case AgentRouteTableAPIS::INET4_UNICAST:
          table = static_cast<AgentRouteTable *>(new Inet4UnicastAgentRouteTable(db, name));
          index = name.rfind(GetSuffix(AgentRouteTableAPIS::INET4_UNICAST));
          break;
      case AgentRouteTableAPIS::INET4_MULTICAST:
          table = static_cast<AgentRouteTable *>(new Inet4MulticastAgentRouteTable(db, name));
          index = name.rfind(GetSuffix(AgentRouteTableAPIS::INET4_MULTICAST));
          break;
      case AgentRouteTableAPIS::LAYER2:
          table = static_cast<AgentRouteTable *>(new Layer2AgentRouteTable(db, name));
          index = name.rfind(GetSuffix(AgentRouteTableAPIS::LAYER2));
          break;
      default:
          return NULL;
    }
    table->Init();
    assert(index != string::npos);
    string vrf = name.substr(0, index);
    VrfEntry *vrf_entry = 
        static_cast<VrfEntry *>(Agent::GetInstance()->
                                GetVrfTable()->FindVrfFromName(vrf));
    assert(vrf_entry);
    table->SetVrfEntry(vrf_entry);
    table->SetVrfDeleteRef(vrf_entry->deleter());

    if (RouteTableTree[type] == NULL)
        RouteTableTree[type] = table;
    return table;
};

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

    if (path->GetTunnelBmap() != tunnel_bmap_) {
        path->SetTunnelBmap(tunnel_bmap_);
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
    // If policy force-enabled in request, enable policy
    path->SetForcePolicy(force_policy_);
    if (force_policy_) {
        policy = true;
    }
    InterfaceNHKey key(intf_.Clone(), policy, flags_);
    nh = static_cast<NextHop *>(Agent::GetInstance()->
                                GetNextHopTable()->FindActiveEntry(&key));

    if (path->GetLabel() != label_) {
        path->SetLabel(label_);
        ret = true;
    }

    if (path->GetTunnelBmap() != tunnel_bmap_) {
        path->SetTunnelBmap(tunnel_bmap_);
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

    TunnelNHKey key(server_vrf_, Agent::GetInstance()->GetRouterId(),
                    server_ip_, false, TunnelType::ComputeType(tunnel_bmap_)); 
    nh = static_cast<NextHop *>(Agent::GetInstance()->
                                GetNextHopTable()->FindActiveEntry(&key));
    if (!nh) {
        Agent::GetInstance()->GetNextHopTable()->Process(nh_req_);
        nh = static_cast<NextHop *>(Agent::GetInstance()->
                                    GetNextHopTable()->FindActiveEntry(&key));
    }

    if (path->GetLabel() != label_) {
        path->SetLabel(label_);
        ret = true;
    }

    if (path->GetTunnelBmap() != tunnel_bmap_) {
        path->SetTunnelBmap(tunnel_bmap_);
        ret = true;
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

    if (path->GetTunnelBmap() != tunnel_bmap_) {
        path->SetTunnelBmap(tunnel_bmap_);
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
    path->SetLabel(vxlan_id_);
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
        (vrf->GetRouteTable(AgentRouteTableAPIS::INET4_UNICAST));
    AgentRouteTable::const_nh_iterator it;
    NhListResp *resp = new NhListResp();

    //TODO - Convert inet4ucroutetable to agentroutetable
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

