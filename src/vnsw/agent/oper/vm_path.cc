/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/uuid/uuid_io.hpp>

#include <cmn/agent_cmn.h>
#include <cfg/init_config.h>
#include <route/route.h>
#include <oper/nexthop.h>
#include <oper/inet4_ucroute.h>
#include <oper/inet4_mcroute.h>
#include <oper/vm_path.h>
#include <oper/interface.h>
#include <oper/mirror_table.h>

// Handle change in route data. Update NH and label if there is any change
bool AgentPath::RouteChange(AgentDBTable *nh_table, Inet4UcRouteKey *rt_key,
                            Inet4RouteData *d, bool &sync) {
    bool ret = false;
    NextHop *nh = NULL;
    sync = false;

    switch (d->type_) {
    case Inet4RouteData::HOST: {
        Inet4UcHostRoute *host = static_cast<Inet4UcHostRoute *>(d);
        InterfaceNHKey key(host->intf_.Clone(), false);
        nh = static_cast<NextHop *>(nh_table->FindActiveEntry(&key));
        if (dest_vn_name_ != host->dest_vn_name_) {
            dest_vn_name_ = host->dest_vn_name_;
            ret = true;
        }
        if (proxy_arp_ != host->proxy_arp_) {
            proxy_arp_ = host->proxy_arp_;
            ret = true;
        }

        unresolved_ = false;
        break;
    }

    case Inet4RouteData::DROP_ROUTE: {
        DiscardNHKey key;
        nh = static_cast<NextHop *>(nh_table->FindActiveEntry(&key));
        unresolved_ = false;
        break;
    }

    case Inet4RouteData::LOCAL_VM: {
        Inet4UcLocalVmRoute *local = static_cast<Inet4UcLocalVmRoute *>(d);
        VmPortInterfaceKey intf_key(local->intf_.uuid_, "");
        VmPortInterface *vm_port = static_cast<VmPortInterface *>
            (Agent::GetInstance()->GetInterfaceTable()->FindActiveEntry(&intf_key));
        
        bool policy = false;
        // Use policy based NH if policy enabled on interface
        if (vm_port && vm_port->IsPolicyEnabled()) {
            policy = true;
        }
        // If policy force-enabled in request, enable policy
        force_policy_ = local->force_policy_;
        if (local->force_policy_) {
            policy = true;
        }

        InterfaceNHKey key(local->intf_.Clone(), policy);
        nh = static_cast<NextHop *>(nh_table->FindActiveEntry(&key));

        if (label_ != local->label_) {
            label_ = local->label_;
            ret = true;
        }

        if (dest_vn_name_ != local->dest_vn_name_) {
            dest_vn_name_ = local->dest_vn_name_;
            ret = true;
        }

        if (proxy_arp_ != local->proxy_arp_) {
            proxy_arp_ = local->proxy_arp_;
            ret = true;
        }

        if (sg_list_ != local->sg_list_) {
            sg_list_ = local->sg_list_;
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
        if (nh_.get() && nh_->PolicyEnabled())
            old_policy = true;
        if (nh && nh->PolicyEnabled())
            new_policy = true;
        if (old_policy != new_policy) {
            sync = true;
        }

        unresolved_ = false;
        break;
    }

    case Inet4RouteData::VLAN_NH: {
        Inet4UcVlanNhRoute *vlan = static_cast<Inet4UcVlanNhRoute *>(d);
        assert(vlan->intf_.type_ == Interface::VMPORT);
        VlanNHKey key(vlan->intf_.uuid_, vlan->tag_);
        nh = static_cast<NextHop *>(nh_table->FindActiveEntry(&key));
        if (nh) {
            assert(nh->GetType() == NextHop::VLAN);
        }

        if (label_ != vlan->label_) {
            label_ = vlan->label_;
            ret = true;
        }

        if (dest_vn_name_ != vlan->dest_vn_name_) {
            dest_vn_name_ = vlan->dest_vn_name_;
            ret = true;
        }

        if (sg_list_ != vlan->sg_list_) {
            sg_list_ = vlan->sg_list_;
            ret = true;
        }

        unresolved_ = false;
        break;
    }

    case Inet4RouteData::REMOTE_VM: {
        Inet4UcRemoteVmRoute *remote = static_cast<Inet4UcRemoteVmRoute *>(d);
        tunnel_bmap_ = remote->tunnel_bmap_;
        TunnelNHKey key(remote->server_vrf_, Agent::GetInstance()->GetRouterId(),
                        remote->server_ip_, false,
                        TunnelType::ComputeType(tunnel_bmap_));
        nh = static_cast<NextHop *>(nh_table->FindActiveEntry(&key));
        if (label_ != remote->label_) {
            label_ = remote->label_;
            ret = true;
        }

        if (dest_vn_name_ != remote->dest_vn_name_) {
            dest_vn_name_ = remote->dest_vn_name_;
            ret = true;
        }

        if (sg_list_ != remote->sg_list_) {
            sg_list_ = remote->sg_list_;
            ret = true;
        }

        unresolved_ = false;

        break;
    }

    case Inet4RouteData::ARP_ROUTE: {
        ArpNHKey key(rt_key->vrf_name_, rt_key->addr_);
        nh = static_cast<NextHop *>(nh_table->FindActiveEntry(&key));
        unresolved_ = false;
        dest_vn_name_ = Agent::GetInstance()->GetFabricVnName();
        if (dest_vn_name_ != Agent::GetInstance()->GetFabricVnName()) {
            ret = true;
        }
        ret = true;
        break;
    }

    case Inet4RouteData::RESOLVE_ROUTE: {
        ResolveNHKey key;
        nh = static_cast<NextHop *>(nh_table->FindActiveEntry(&key));
        unresolved_ = false;
        if (dest_vn_name_ != Agent::GetInstance()->GetFabricVnName()) {
            dest_vn_name_ = Agent::GetInstance()->GetFabricVnName();
            ret = true;
        }
        break;
    }

    case Inet4RouteData::RECEIVE_ROUTE: {
        Inet4UcReceiveRoute *vhost = static_cast<Inet4UcReceiveRoute *>(d);
        ReceiveNHKey key(vhost->intf_.Clone(), vhost->policy_);
        nh = static_cast<NextHop *>(nh_table->FindActiveEntry(&key));
        unresolved_ = false;
        if (dest_vn_name_ != vhost->vn_) {
            dest_vn_name_ = vhost->vn_;
            ret = true;
        }

        if (proxy_arp_ != vhost->proxy_arp_) {
            proxy_arp_ = vhost->proxy_arp_;
            ret = true;
        }

        if (label_ != vhost->label_) {
            label_ = vhost->label_;
            ret = true;
        }

        if (tunnel_bmap_ != vhost->tunnel_bmap_) {
            tunnel_bmap_ = vhost->tunnel_bmap_;
            ret = true;
        }

        break;
    }

    case Inet4RouteData::GATEWAY_ROUTE: {
        Inet4UcGatewayRoute *ind_rt = static_cast<Inet4UcGatewayRoute *>(d);
        // Find gateway route, add a reference to it
        vrf_name_ = rt_key->vrf_name_;
        Inet4UcRoute *rt = Inet4UcRouteTable::FindRoute(rt_key->vrf_name_, 
                                                    ind_rt->gw_ip_);
        if (rt == NULL || rt->GetPlen() == 0) {
            unresolved_ = true;
        } else if (rt->GetActiveNextHop()->GetType() == NextHop::RESOLVE) {
            unresolved_ = true;
            Inet4UcRouteTable::AddArpReq(rt_key->vrf_name_, ind_rt->gw_ip_);
        } else {
            unresolved_ = false;
        }

        //Reset to new gateway route, no nexthop for indirect route
        gw_ip_ = ind_rt->gw_ip_;
        gw_rt_.reset(rt);
        if (dest_vn_name_ != Agent::GetInstance()->GetFabricVnName()) {
            dest_vn_name_ = Agent::GetInstance()->GetFabricVnName();
            ret = true;
        }
        return true;
    }

    case Inet4RouteData::SBCAST_ROUTE: {
        Inet4UcSbcastRoute *sbcast_rt = static_cast<Inet4UcSbcastRoute*>(d);
        CompositeNHKey key(rt_key->vrf_name_, sbcast_rt->grp_addr_,
                           sbcast_rt->src_addr_, false);
        nh = static_cast<NextHop *>(nh_table->FindActiveEntry(&key));
        dest_vn_name_ = sbcast_rt->vn_name_;
        ret = true;
        unresolved_ = false;
        break;
    }

    case Inet4RouteData::ECMP_ROUTE: {
        Inet4UcEcmpRoute *rt_data = static_cast<Inet4UcEcmpRoute*>(d);

        if (label_ != rt_data->label_) {
            label_ = rt_data->label_;                                                                                                                             
            ret = true;                                                                                                                                         
        }                                                                                                                                                       
        CompositeNHKey key(rt_key->vrf_name_, rt_data->dest_addr_, 
                           rt_data->local_ecmp_nh_);
        nh = static_cast<NextHop *>(nh_table->FindActiveEntry(&key));
        dest_vn_name_ = rt_data->vn_name_;
        ret = true;
        unresolved_ = false;
        break;
    }

    default:
        assert(0);
    }

    // If NH is not found, point route to discard NH
    if (nh == NULL) {
        LOG(DEBUG, "NH not found for route <" << rt_key->vrf_name_ << 
            ":"  << rt_key->addr_.to_string() << "/" << rt_key->plen_
            << ">. Setting NH to Discard NH ");
        DiscardNHKey key;
        nh = static_cast<NextHop *>(nh_table->FindActiveEntry(&key));
    }

    if (nh_ != nh) {
        nh_ = nh;
        ret = true;
    }

    return ret;
}

bool AgentPath::Sync(Inet4UcRoute *sync_route) {
    bool ret = false;
    bool unresolved = false;

    //Check if there is change in policy on the interface
    //If yes update the path to point to policy enabled NH
    if (nh_.get() && nh_->GetType() == NextHop::INTERFACE) {
        const InterfaceNH *intf_nh = static_cast<const InterfaceNH *>(nh_.get());
        const VmPortInterface *vm_port = 
            static_cast<const VmPortInterface *>(intf_nh->GetInterface());

        bool policy = vm_port->IsPolicyEnabled();
        if (force_policy_) {
            policy = true;
        }

        if (intf_nh->PolicyEnabled() != policy) {
            //Make path point to policy enabled interface
            InterfaceNHKey key(new VmPortInterfaceKey(vm_port->GetUuid(), ""),
                               policy);
            nh_ = static_cast<NextHop *>
                (Agent::GetInstance()->GetNextHopTable()->FindActiveEntry(&key));
            // If NH is not found, point route to discard NH
            if (nh_ == NULL) {
                LOG(DEBUG, "Interface NH for <" 
                    << UuidToString(vm_port->GetUuid())
                    << " : policy = " << policy);
                DiscardNHKey key;
                nh_ = static_cast<NextHop *>
                    (Agent::GetInstance()->GetNextHopTable()->FindActiveEntry(&key));
            }
        }
    }

    if (vrf_name_ == Agent::GetInstance()->NullString()) {
        return ret;
    }
 
    Inet4UcRoute *rt = Inet4UcRouteTable::FindRoute(vrf_name_, gw_ip_);
    if (rt == sync_route) {
        rt = NULL;
    }

    if (rt == NULL) {
        unresolved = true;
    } else if (rt->GetActiveNextHop()->GetType() == NextHop::RESOLVE) {
        Inet4UcRouteTable::AddArpReq(vrf_name_, gw_ip_);
        unresolved = true;
    } else {
        unresolved = false;
    }

    if (unresolved_ != unresolved) {
        unresolved_ = unresolved;
        ret = true;
    }
    //Reset to new gateway route, no nexthop for indirect route
    if (gw_rt_.get() != rt) {
        gw_rt_.reset(rt);
        ret = true;
    }
    return ret;
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
    const NextHop *nh = gw_rt_.get()->GetActiveNextHop();
    if (nh == NULL) {
        assert(0);
    }
    return nh;
}
