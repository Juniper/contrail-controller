/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/uuid/uuid_io.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/foreach.hpp>
#include <cmn/agent_cmn.h>
#include <route/route.h>
#include <init/agent_param.h>

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
#include <oper/ecmp_load_balance.h>
#include <oper/agent_sandesh.h>
#include <oper/tsn_elector.h>

using namespace std;
using namespace boost::asio;

//Helpers
bool RebakeLabel(MplsTable *table, uint32_t label, NextHop *nh) {
    bool ret = false;
    if (label != MplsTable::kInvalidLabel) {
        MplsLabelKey key(label);
        MplsLabel *mpls = static_cast<MplsLabel *>(table->
                                                   FindActiveEntry(&key));
        if (mpls && mpls->ChangeNH(nh)) {
            ret = true;
            //Send notify of change
            mpls->get_table_partition()->Notify(mpls);
        }
    }
    return ret;
}

//AgentPath
AgentPath::AgentPath(const Peer *peer, AgentRoute *rt):
    Path(), peer_(peer), nh_(NULL), label_(MplsTable::kInvalidLabel),
    vxlan_id_(VxLanTable::kInvalidvxlan_id), dest_vn_list_(),
    origin_vn_(""), sync_(false), force_policy_(false), sg_list_(),
    tunnel_dest_(0), tunnel_bmap_(TunnelType::AllType()),
    tunnel_type_(TunnelType::ComputeType(TunnelType::AllType())),
    vrf_name_(""), gw_ip_(), unresolved_(true),
    is_subnet_discard_(false), dependant_rt_(rt), path_preference_(),
    local_ecmp_mpls_label_(rt), composite_nh_key_(NULL), subnet_service_ip_(),
    arp_mac_(), arp_interface_(NULL), arp_valid_(false),
    ecmp_suppressed_(false), is_local_(false), is_health_check_service_(false),
    peer_sequence_number_(0), etree_leaf_(false), layer2_control_word_(false),
    inactive_(false), copy_local_path_(false), parent_rt_(rt), dependent_table_(NULL) {
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

    // Send back discard NH if dependant_rt_ is not set
    if (dependant_rt_.get() == NULL) {
        DiscardNH key;
        return static_cast<NextHop *>
            (agent->nexthop_table()->FindActiveEntry(&key));
    }

    //Indirect route's path, get direct route's NH
    const NextHop *nh = dependant_rt_.get()->GetActiveNextHop();
    if (nh == NULL) {
        DiscardNH key;
        return static_cast<NextHop *>
            (agent->nexthop_table()->FindActiveEntry(&key));
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
        if (nh && nh->GetType() == NextHop::TUNNEL) {
            TunnelNH *tunnel_nh = static_cast<TunnelNH *>(nh);
            tunnel_dest_ = *tunnel_nh->GetDip();
        }
        ret = true;
    }

    if (peer_ && (peer_->GetType() == Peer::ECMP_PEER) &&
        (label_ != MplsTable::kInvalidLabel)) {
        if (RebakeLabel(agent->mpls_table(), label_, nh))
            ret = true;
    }

    if (PostChangeNH(agent, nh)) {
        ret = true;
    }
    return ret;
}

bool AgentPath::PostChangeNH(Agent *agent, NextHop *nh) {
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
                                              vm_port->GetUuid(),
                                              vm_port->name()),
                           policy, intf_nh->GetFlags(),
                           intf_nh->GetDMac());
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
    // if tunnel type is MPLS over MPLS, transport tunnel
    // type might be changed ( mpls over gre or mpls over udp)
    // so check nh transport tunnel type and trigger update if there
    // is any change
    if ((tunnel_type_ != TunnelType::MPLS_OVER_MPLS) &&
            (tunnel_type_ == TunnelType::ComputeType(tunnel_bmap_)) &&
        (nh_.get() && nh_.get()->GetType() != NextHop::COMPOSITE)) {
        return false;
    }

    tunnel_type_ = TunnelType::ComputeType(tunnel_bmap_);
    if (tunnel_type_ == TunnelType::VXLAN &&
        vxlan_id_ == VxLanTable::kInvalidvxlan_id) {
        tunnel_type_ = TunnelType::ComputeType(TunnelType::MplsType());
    }
    DBRequest nh_req(DBRequest::DB_ENTRY_ADD_CHANGE);
    const TunnelNH *tunnel_nh = static_cast<const TunnelNH*>(nh_.get());
    if (nh_.get() && nh_->GetType() == NextHop::TUNNEL) {
        if (tunnel_nh->GetTunnelType().GetType() == TunnelType::MPLS_OVER_MPLS) {
            const LabelledTunnelNH *label_tunnel_nh =
                    static_cast<const LabelledTunnelNH*>(nh_.get());
            // check if transport tunnel type is changed
            if (label_tunnel_nh->GetTransportTunnelType() ==
                    TunnelType::ComputeType(TunnelType::MplsType())) {
                return false;
            }
            LabelledTunnelNHKey *tnh_key =
                new LabelledTunnelNHKey(agent->fabric_vrf_name(),
                        *(label_tunnel_nh->GetSip()),
                        *(label_tunnel_nh->GetDip()),
                        false, tunnel_type_,
                        label_tunnel_nh->rewrite_dmac(),
                        label_tunnel_nh->GetTransportLabel());
            nh_req.key.reset(tnh_key);
            nh_req.data.reset(new TunnelNHData());
            agent->nexthop_table()->Process(nh_req);

            LabelledTunnelNHKey nh_key(agent->fabric_vrf_name(),
                        *(label_tunnel_nh->GetSip()),
                        tunnel_dest_, false, tunnel_type_,
                        label_tunnel_nh->rewrite_dmac(),
                        label_tunnel_nh->GetTransportLabel());
            NextHop *nh = static_cast<NextHop *>
                (agent->nexthop_table()->FindActiveEntry(&nh_key));
            ChangeNH(agent, nh);
        } else {

            TunnelNHKey *tnh_key =
                new TunnelNHKey(agent->fabric_vrf_name(), *(tunnel_nh->GetSip()),
                                tunnel_dest_, false, tunnel_type_);
            nh_req.key.reset(tnh_key);
            nh_req.data.reset(new TunnelNHData());
            agent->nexthop_table()->Process(nh_req);

            TunnelNHKey nh_key(agent->fabric_vrf_name(), *(tunnel_nh->GetSip()),
                            tunnel_dest_, false, tunnel_type_);
            NextHop *nh = static_cast<NextHop *>
                (agent->nexthop_table()->FindActiveEntry(&nh_key));
            ChangeNH(agent, nh);
        }
    }

    if (nh_.get() && nh_->GetType() == NextHop::COMPOSITE) {
        RebakeAllTunnelNHinCompositeNH(sync_route);
    }
    return true;
}

void AgentPath::ImportPrevActiveNH(Agent *agent, NextHop *nh)
{
    if (nh_ == nh) {
        return;
    }
    // change NH ,
    // assumptions here is that composite nh grid order would be different
    // but elements would be same , so importing previous active nh
    // would make sure the holes are already set in proper order
    // ex: previous nh order A,B, C, new path nh order would be BCA
    // ex2: previous nh order _,B,C, new path nh order is just B,C
    ChangeNH(agent, nh);
    // will there be a race condition while removing the active path,
    // adding new component nh? 
    // this can be handled by calling reorder composite nh method
    if (composite_nh_key_ == NULL) { 
        return;
    }
    boost::scoped_ptr<CompositeNHKey> composite_nh_key(
                                    composite_nh_key_->Clone());
    bool comp_nh_policy = false;
    //TODO: optimize here , compare compositenh_key from path and nh?
    ReorderCompositeNH(agent, composite_nh_key.get(), comp_nh_policy, NULL);
    composite_nh_key->SetPolicy(comp_nh_policy);
    ChangeCompositeNH(agent, composite_nh_key.get());

}

bool AgentPath::ResolveGwNextHops(Agent *agent, const AgentRoute *sync_route) {

    if (tunnel_type_ != TunnelType::MPLS_OVER_MPLS) {
        return false;
    }
    if (ecmp_member_list_.size() == 0) {
        return false;
    }
    NextHop *nh = NULL;
    InetUnicastAgentRouteTable *table = NULL;
    table = static_cast<InetUnicastAgentRouteTable *>
        (agent->fabric_inet4_mpls_table());
    assert(table != NULL);
    //if ecmp member list size is one then it is non ecmp route sync
    if (ecmp_member_list_.size() == 1) {
        AgentPathEcmpComponentPtr member = ecmp_member_list_[0];
        InetUnicastRouteEntry *uc_rt = table->FindRoute(member->GetGwIpAddr());
        if (uc_rt == NULL || uc_rt->plen() == 0) {
            set_unresolved(true);
            member->UpdateDependentRoute(NULL);
            member->SetUnresolved(true);
        } else {
            set_unresolved(false);
            table->RemoveUnresolvedRoute(GetParentRoute());
            DBEntryBase::KeyPtr key =
                uc_rt->GetActiveNextHop()->GetDBRequestKey();
            const NextHopKey *nh_key =
                static_cast<const NextHopKey*>(key.get());
            nh = static_cast<NextHop *>(agent->nexthop_table()->
                                FindActiveEntry(nh_key));
            assert(nh !=NULL);
            //Reset to new gateway route, no nexthop for indirect route
            member->UpdateDependentRoute(uc_rt);
            member->SetUnresolved(false);
            set_nexthop(NULL);
        }
        if (nh != NULL) {
            ChangeNH(agent, nh);
        }
    } else {
        // this flag is set if atleast one member is unresolved
        bool path_unresolved = false;
        // if this flag is false , then
        // composite NH is created with one compoenent NH as discard
        // and avoid reording teh existing composite NH
        // this flag is set to true if atleast one member is resolved
        bool is_ecmp_member_resolved = false;
        ComponentNHKeyList comp_nh_list;
        bool comp_nh_policy = false;
        AgentPathEcmpComponentPtrList::const_iterator ecmp_member_it =
                    ecmp_member_list_.begin();
        while (ecmp_member_it != ecmp_member_list_.end()) {
            InetUnicastRouteEntry *uc_rt =
                table->FindRoute((*ecmp_member_it)->GetGwIpAddr());
            if (uc_rt == NULL || uc_rt->plen() == 0) {
                (*ecmp_member_it)->UpdateDependentRoute(NULL);
                (*ecmp_member_it)->SetUnresolved(true);
                if (!path_unresolved) {
                    path_unresolved  = true;
                    DBEntryBase::KeyPtr key =
                        agent->nexthop_table()->discard_nh()->GetDBRequestKey();
                    NextHopKey *nh_key =
                        static_cast<NextHopKey *>(key.release());
                    std::auto_ptr<const NextHopKey> nh_key_ptr(nh_key);
                    ComponentNHKeyPtr component_nh_key(new ComponentNHKey(
                                    (*ecmp_member_it)->GetLabel(),
                                    nh_key_ptr));
                    comp_nh_list.push_back(component_nh_key);
                }
            } else {
                is_ecmp_member_resolved = true;
                DBEntryBase::KeyPtr key =
                    uc_rt->GetActiveNextHop()->GetDBRequestKey();
                NextHopKey *nh_key = static_cast<NextHopKey *>(key.release());
                if (nh_key->GetType() != NextHop::COMPOSITE) {
                    //By default all component members of composite NH
                    //will be policy disabled, except for component NH
                    //of type composite
                    nh_key->SetPolicy(false);
                }
                std::auto_ptr<const NextHopKey> nh_key_ptr(nh_key);
                ComponentNHKeyPtr component_nh_key(new
                        ComponentNHKey((*ecmp_member_it)->GetLabel(),
                                                   nh_key_ptr));
                comp_nh_list.push_back(component_nh_key);
                //Reset to new gateway route, no nexthop for indirect route
                (*ecmp_member_it)->UpdateDependentRoute(uc_rt);
                (*ecmp_member_it)->SetUnresolved(false);
            }
            ecmp_member_it++;
        }
        DBRequest nh_req(DBRequest::DB_ENTRY_ADD_CHANGE);
        nh_req.key.reset(new CompositeNHKey(Composite::ECMP, comp_nh_policy,
                                    comp_nh_list, vrf_name_));
        nh_req.data.reset(new CompositeNHData());
        CompositeNHKey *comp_key =
            static_cast<CompositeNHKey *>(nh_req.key.get());
        bool new_comp_nh_policy = false;
        if ((is_ecmp_member_resolved == false) ||
                ReorderCompositeNH(agent, comp_key, new_comp_nh_policy,
                               sync_route->FindLocalVmPortPath())) {
            comp_key->SetPolicy(new_comp_nh_policy);
            ChangeCompositeNH(agent, comp_key);
        }

        set_unresolved(path_unresolved);
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
    if (ResolveGwNextHops(agent, sync_route)) {
        return true;
    }
    //Handle tunnel type change
    if (UpdateTunnelType(agent, sync_route)) {
        ret = true;
    }

    //Check if there was a change in local ecmp composite nexthop
    if (nh_ && nh_->GetType() == NextHop::COMPOSITE &&
        composite_nh_key_.get() != NULL && (copy_local_path_ ||
        local_ecmp_mpls_label_.get() != NULL)) {
        boost::scoped_ptr<CompositeNHKey> composite_nh_key(composite_nh_key_->Clone());
        bool comp_nh_policy = false;
        if (ReorderCompositeNH(agent, composite_nh_key.get(), comp_nh_policy,
                               sync_route->FindLocalVmPortPath())) {
            composite_nh_key->SetPolicy(comp_nh_policy);
            if (ChangeCompositeNH(agent, composite_nh_key.get())) {
                ret = true;
            }
        }
    }

    if (nh_ && nh_->GetType() == NextHop::ARP) {
        if (CopyArpData()) {
            ret = true;
        }
    }

    if (vrf_name_ == Agent::NullString()) {
        return ret;
    }

    // check dependant route change
    // this is needed only for inet routes
    InetUnicastAgentRouteTable *table = NULL;
    table = dynamic_cast<InetUnicastAgentRouteTable *>
                                (sync_route->get_table());
    if (table == NULL) {
        return ret;
    }

    InetUnicastRouteEntry *rt = NULL;

    rt = table->FindRoute(gw_ip_);
    if (rt == sync_route) {
        rt = NULL;
    }

    if (rt == NULL || rt->plen() == 0) {
       if (agent->params()->subnet_hosts_resolvable() == false &&
            agent->fabric_vrf_name() == vrf_name_) {
            unresolved = false;
            assert(gw_ip_.is_v4());
            table->AddArpReq(vrf_name_, gw_ip_.to_v4(), vrf_name_,
                             agent->vhost_interface(), false,
                             dest_vn_list_, sg_list_, tag_list_);
        } else {
            unresolved = true;
        }
    } else if (rt->GetActiveNextHop()->GetType() == NextHop::RESOLVE) {
        const ResolveNH *nh =
            static_cast<const ResolveNH *>(rt->GetActiveNextHop());
        std::string nexthop_vrf = nh->get_interface()->vrf()->GetName();
        if (nh->get_interface()->vrf()->forwarding_vrf()) {
            nexthop_vrf = nh->get_interface()->vrf()->forwarding_vrf()->GetName();
        }

        assert(gw_ip_.is_v4());
        table->AddArpReq(vrf_name_, gw_ip_.to_v4(), nexthop_vrf,
                         nh->get_interface(), nh->PolicyEnabled(), dest_vn_list_,
                         sg_list_, tag_list_);
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
        if (rt) {
            TunnelType::TypeBmap dep_bmap =
                dependant_rt_->GetActivePath()->tunnel_bmap();
            if (tunnel_bmap_ & 1 << TunnelType::NATIVE) {
                dep_bmap |= (1 << TunnelType::NATIVE);
            }
            set_tunnel_bmap(dep_bmap);
        }
    }
    return ret;
}

bool AgentPath::IsLess(const AgentPath &r_path) const {
    if (peer()->GetType() == r_path.peer()->GetType()) {
        if (path_preference() != r_path.path_preference()) {
            //If right path has lesser preference, then
            //it should be after the current entry
            //Hence the reverse check
            return (r_path.path_preference() < path_preference());
        }
    }

    return peer()->IsLess(r_path.peer());
}

const AgentPath *AgentPath::UsablePath() const {
    return this;
}

void AgentPath::set_nexthop(NextHop *nh) {
    nh_ = nh;
}

bool AgentPath::CopyArpData() {
    bool ret = false;
    if (nh_ && nh_->GetType() == NextHop::ARP) {
        const ArpNH *arp_nh = static_cast<const ArpNH *>(nh_.get());
        if (arp_mac() != arp_nh->GetMac()) {
            set_arp_mac(arp_nh->GetMac());
            ret = true;
        }

        if (arp_interface() != arp_nh->GetInterface()) {
            set_arp_interface(arp_nh->GetInterface());
            ret = true;
        }

        if (arp_valid() != arp_nh->IsValid()) {
            set_arp_valid(arp_nh->IsValid());
            ret = true;
        }
    }
    return ret;
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
    AgentRouteData(AgentRouteData::ADD_DEL_CHANGE,
                   evpn_rt->is_multicast(), 0),
    ethernet_tag_(evpn_rt->ethernet_tag()), ip_addr_(evpn_rt->ip_addr()),
    reference_path_(evpn_rt->GetActivePath()), ecmp_suppressed_(false) {
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

bool EvpnDerivedPathData::AddChangePathExtended(Agent *agent, AgentPath *path,
                                                const AgentRoute *rt) {
    bool ret = false;
    EvpnDerivedPath *evpn_path = dynamic_cast<EvpnDerivedPath *>(path);
    assert(evpn_path != NULL);

    evpn_path->set_tunnel_dest(reference_path_->tunnel_dest());
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

    PathPreference pref = reference_path_->path_preference();
    if (evpn_path->path_preference() != pref) {
        // Take path preference from parent path
        evpn_path->set_path_preference(pref);
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

    const TagList &tag_list = reference_path_->tag_list();
    if (evpn_path->tag_list() != tag_list) {
        evpn_path->set_tag_list(tag_list);
        ret = true;
    }

    const VnListType &dest_vn_list = reference_path_->dest_vn_list();
    if (evpn_path->dest_vn_list() != dest_vn_list) {
        evpn_path->set_dest_vn_list(dest_vn_list);
        ret = true;
    }

    if (evpn_path->ecmp_suppressed() != ecmp_suppressed_) {
        evpn_path->set_ecmp_suppressed(ecmp_suppressed_);
        ret = true;
    }

    if (evpn_path->etree_leaf() != reference_path_->etree_leaf()) {
        evpn_path->set_etree_leaf(reference_path_->etree_leaf());
        ret = true;
    }

    if (evpn_path->ResyncControlWord(rt)) {
        ret = true;
    }

    path->set_unresolved(false);

    return ret;
}

bool EvpnDerivedPathData::CanDeletePath(Agent *agent, AgentPath *path,
                                        const AgentRoute *rt) const {
    const EvpnDerivedPath *evpn_path =
        dynamic_cast<const EvpnDerivedPath *>(path);
    assert(evpn_path != NULL);

    if (evpn_path->ethernet_tag() != ethernet_tag())
        return false;

    return (evpn_path->ip_addr() == ip_addr());
}

bool HostRoute::AddChangePathExtended(Agent *agent, AgentPath *path,
                                      const AgentRoute *rt) {
    bool ret = false;
    NextHop *nh = NULL;

    InterfaceNHKey key(intf_.Clone(), policy_, InterfaceNHFlags::INET4,
                       agent->pkt_interface_mac());
    nh = static_cast<NextHop *>(agent->nexthop_table()->FindActiveEntry(&key));
    VnListType dest_vn_list;
    dest_vn_list.insert(dest_vn_name_);
    if (path->dest_vn_list() != dest_vn_list) {
        path->set_dest_vn_list(dest_vn_list);
        ret = true;
    }

    path->set_unresolved(false);
    if (path->ChangeNH(agent, nh) == true)
        ret = true;

    return ret;
}

bool HostRoute::UpdateRoute(AgentRoute *rt) {
    InetUnicastRouteEntry *uc_rt =
        static_cast<InetUnicastRouteEntry *>(rt);
    AgentRouteTable *table = static_cast<AgentRouteTable *>(rt->get_table());
    if ((table->GetTableType() != Agent::INET4_UNICAST) &&
        (table->GetTableType() != Agent::INET6_UNICAST))
        return false;

    return uc_rt->UpdateRouteFlags(false, false, true);
}

bool L2ReceiveRoute::AddChangePathExtended(Agent *agent, AgentPath *path,
                                           const AgentRoute *rt) {
    bool ret = false;

    path->set_unresolved(false);

    VnListType dest_vn_list;
    dest_vn_list.insert(dest_vn_name_);
    if (path->dest_vn_list() != dest_vn_list) {
        path->set_dest_vn_list(dest_vn_list);
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

    if (path->ChangeNH(agent, agent->nexthop_table()->l2_receive_nh()) == true)
        ret = true;

    return ret;
}

bool InetInterfaceRoute::UpdateRoute(AgentRoute *rt) {
    AgentRouteTable *table = static_cast<AgentRouteTable *>(rt->get_table());
    if ((table->GetTableType() != Agent::INET4_UNICAST) &&
        (table->GetTableType() != Agent::INET6_UNICAST))
        return false;

    InetUnicastRouteEntry *uc_rt = static_cast<InetUnicastRouteEntry *>(rt);

    return uc_rt->UpdateRouteFlags(false, false, true);
}

bool InetInterfaceRoute::AddChangePathExtended(Agent *agent, AgentPath *path,
                                               const AgentRoute *rt) {
    bool ret = false;
    NextHop *nh = NULL;
    InterfaceNHKey key(intf_.Clone(), false, InterfaceNHFlags::INET4,
                       agent->pkt_interface_mac());
    nh = static_cast<NextHop *>(agent->nexthop_table()->FindActiveEntry(&key));
    if (path->dest_vn_list() != dest_vn_list_) {
        path->set_dest_vn_list(dest_vn_list_);
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

bool DropRoute::AddChangePathExtended(Agent *agent, AgentPath *path,
                                      const AgentRoute *rt) {
    bool ret = false;

    VnListType dest_vn_list;
    dest_vn_list.insert(vn_);
    if (path->dest_vn_list() != dest_vn_list) {
        path->set_dest_vn_list(dest_vn_list);
        ret = true;
    }

    NextHop *nh = agent->nexthop_table()->discard_nh();
    path->set_unresolved(false);
    if (path->ChangeNH(agent, nh) == true) {
        ret = true;
    }

    return ret;
}

bool LocalVmRoute::AddChangePathExtended(Agent *agent, AgentPath *path,
                                         const AgentRoute *rt) {
    bool ret = false;
    NextHop *nh = NULL;
    SecurityGroupList path_sg_list;
    TagList path_tag_list;
    CommunityList path_communities;

    //TODO Based on key table type pick up interface
    VmInterfaceKey intf_key(AgentKey::ADD_DEL_CHANGE, intf_.uuid_, intf_.name_);
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

    if (native_encap_) {
        tunnel_bmap_ |= (1 << TunnelType::NATIVE);
    }

    if (tunnel_bmap_ != path->tunnel_bmap()) {
        path->set_tunnel_bmap(tunnel_bmap_);
        ret = true;
    }

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

    MacAddress mac = MacAddress::ZeroMac();
    if (vm_port) {
        mac = vm_port->vm_mac();
        const InetUnicastRouteEntry *ip_rt =
            dynamic_cast<const InetUnicastRouteEntry *>(rt);
        if (ip_rt) {
            mac = vm_port->GetIpMac(ip_rt->addr(), ip_rt->plen());
        }
    }

    InterfaceNHKey key(intf_.Clone(), policy, flags_, mac);
    nh = static_cast<NextHop *>(agent->nexthop_table()->FindActiveEntry(&key));

    if (path->label() != mpls_label_) {
        path->set_label(mpls_label_);
        ret = true;
    }

    if (path->vxlan_id() != vxlan_id_) {
        path->set_vxlan_id(vxlan_id_);
        ret = true;
    }

    if (path->dest_vn_list() != dest_vn_list_) {
        path->set_dest_vn_list(dest_vn_list_);
        ret = true;
    }

    path_sg_list = path->sg_list();
    if (path_sg_list != sg_list_) {
        path->set_sg_list(sg_list_);
        ret = true;
    }

    path_tag_list = path->tag_list();
    if (path_tag_list != tag_list_) {
        path->set_tag_list(tag_list_);
        ret = true;
    }

    path_communities = path->communities();
    if (path_communities != communities_) {
        path->set_communities(communities_);
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
    if (path->path_preference().ConfigChanged(path_preference_) ||
        path->peer() == agent->fabric_rt_export_peer()) {
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

    if (path->subnet_service_ip() != subnet_service_ip_) {
        path->set_subnet_service_ip(subnet_service_ip_);
        ret = true;
    }

    path->set_unresolved(false);
    path->SyncRoute(sync_route_);

    if (ecmp_load_balance_ != path->ecmp_load_balance()) {
        path->set_ecmp_load_balance(ecmp_load_balance_);
        ret = true;
    }

    if (path->ChangeNH(agent, nh) == true)
        ret = true;

    if (is_local_ != path->is_local()) {
        path->set_is_local(is_local_);
        ret = true;
    }

    if (is_health_check_service_ != path->is_health_check_service()) {
        path->set_is_health_check_service(is_health_check_service_);
        ret = true;
    }

    if (etree_leaf_ != path->etree_leaf()) {
        path->set_etree_leaf(etree_leaf_);
        ret = true;
    }

    if (native_vrf_id_ != path->native_vrf_id()) {
        path->set_native_vrf_id(native_vrf_id_);
        ret = true;
    }

    return ret;
}

bool LocalVmRoute::UpdateRoute(AgentRoute *rt) {
    AgentRouteTable *table = static_cast<AgentRouteTable *>(rt->get_table());
    if ((table->GetTableType() != Agent::INET4_UNICAST) &&
        (table->GetTableType() != Agent::INET6_UNICAST)) {
        return false;
    }

    /* Route Reflect will not have the field populated and we do not
     * want to override the route type for reflected routes */
    if (intf_route_type().empty()) {
       return false;
    }

    if (intf_route_type().compare(rt->intf_route_type()) != 0) {
        rt->set_intf_route_type(intf_route_type());
        return true;
    }

    return false;
}


bool PBBRoute::AddChangePathExtended(Agent *agent, AgentPath *path,
                                     const AgentRoute *rt) {
    bool ret = false;
    NextHop *nh = NULL;
    SecurityGroupList path_sg_list;
    TagList path_tag_list;
    CommunityList path_communities;

    VrfEntry *vrf = static_cast<VrfEntry *>
        (agent->vrf_table()->FindActiveEntry(&vrf_key_));

    if (vrf != NULL) {
        //Create PBB NH
        DBRequest nh_req(DBRequest::DB_ENTRY_ADD_CHANGE);
        nh_req.key.reset(new PBBNHKey(vrf_key_.name_, dmac_, isid_));
        nh_req.data.reset(new PBBNHData());
        agent->nexthop_table()->Process(nh_req);

        PBBNHKey pbb_nh_key(vrf_key_.name_, dmac_, isid_);
        nh = static_cast<NextHop *>(agent->nexthop_table()->
                                    FindActiveEntry(&pbb_nh_key));
    }

    if (path->dest_vn_list() != dest_vn_list_) {
        path->set_dest_vn_list(dest_vn_list_);
        ret = true;
    }

    path_sg_list = path->sg_list();
    if (path_sg_list != sg_list_) {
        path->set_sg_list(sg_list_);
        ret = true;
    }

    path_tag_list = path->tag_list();
    if (path_tag_list != tag_list_) {
        path->set_tag_list(tag_list_);
        ret = true;
    }

    //Copy over entire path preference structure, whenever there is a

    if (path->ChangeNH(agent, nh) == true) {
        ret = true;
    }

    path->set_unresolved(false);
    return ret;
}

bool VlanNhRoute::AddChangePathExtended(Agent *agent, AgentPath *path,
                                        const AgentRoute *rt) {
    bool ret = false;
    NextHop *nh = NULL;
    SecurityGroupList path_sg_list;
    TagList path_tag_list;

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

    if (path->dest_vn_list() != dest_vn_list_) {
        path->set_dest_vn_list(dest_vn_list_);
        ret = true;
    }

    path_sg_list = path->sg_list();
    if (path_sg_list != sg_list_) {
        path->set_sg_list(sg_list_);
        ret = true;
    }

    path_tag_list = path->tag_list();
    if (path_tag_list != tag_list_) {
        path->set_tag_list(tag_list_);
        ret = true;
    }

    //Copy over entire path preference structure, whenever there is a

    //Copy over entire path preference structure, whenever there is a
    //transition from active-active to active-backup struture
    if (path->path_preference().ConfigChanged(path_preference_)) {
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

bool ResolveRoute::AddChangePathExtended(Agent *agent, AgentPath *path,
                                         const AgentRoute *rt) {
    bool ret = false;
    NextHop *nh = NULL;
    ResolveNHKey key(intf_key_.get(), policy_);

    nh = static_cast<NextHop *>(agent->nexthop_table()->FindActiveEntry(&key));
    path->set_unresolved(false);

    VnListType dest_vn_list;
    dest_vn_list.insert(dest_vn_name_);
    if (path->dest_vn_list() != dest_vn_list) {
        path->set_dest_vn_list(dest_vn_list);
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

bool ReceiveRoute::AddChangePathExtended(Agent *agent, AgentPath *path,
                                         const AgentRoute *rt) {
    bool ret = false;
    NextHop *nh = NULL;

    //TODO check if it needs to know table type
    ReceiveNHKey key(intf_->Clone(), policy_);
    nh = static_cast<NextHop *>(agent->nexthop_table()->FindActiveEntry(&key));
    path->set_unresolved(false);

    VnListType dest_vn_list;
    dest_vn_list.insert(vn_);
    if (path->dest_vn_list() != dest_vn_list) {
        path->set_dest_vn_list(dest_vn_list);
        ret = true;
    }

    if (path->label() != label_) {
        path->set_label(label_);
        ret = true;
    }

    path->set_tunnel_bmap(tunnel_bmap_);

    if (path->ChangeNH(agent, nh) == true)
        ret = true;

    return ret;
}

bool ReceiveRoute::UpdateRoute(AgentRoute *rt) {
    AgentRouteTable *table = static_cast<AgentRouteTable *>(rt->get_table());
    if ((table->GetTableType() != Agent::INET4_UNICAST) &&
        (table->GetTableType() != Agent::INET6_UNICAST))
        return false;

    InetUnicastRouteEntry *uc_rt =
        static_cast<InetUnicastRouteEntry *>(rt);

    // Receive route can be a /32 route of multicast-subnet route
    // proxy_arp_ is set only for host route. So, set ipam_host_route_ if
    // proxy_arp_ enabled
    return uc_rt->UpdateRouteFlags(false, ipam_host_route_, proxy_arp_);
}

MulticastRoutePath::MulticastRoutePath(const Peer *peer) :
    AgentPath(peer, NULL), original_nh_(),
    evpn_label_(MplsTable::kInvalidLabel),
    fabric_label_(MplsTable::kInvalidLabel) {
}

bool MulticastRoutePath::PostChangeNH(Agent *agent, NextHop *nh) {
    bool ret = false;
    if (!peer())
        return false;

    if (peer()->GetType() != Peer::MULTICAST_PEER)
        return false;

    //Bake for label in path
    if (RebakeLabel(agent->mpls_table(), label(), nh))
        ret = true;

    //If path label is not same as evpn label, then rebake evpn label
    if ((label() != evpn_label_) &&
        RebakeLabel(agent->mpls_table(), evpn_label_, nh))
        ret = true;

    //If path label is not same as fmg label, then rebake fmg label
    if ((label() != fabric_label_) &&
        RebakeLabel(agent->mpls_table(), fabric_label_, nh))
        ret = true;

    return ret;
}

NextHop *MulticastRoutePath::UpdateNH(Agent *agent,
                                      CompositeNH *cnh,
                                      const TsnElector *te) {
    ComponentNHKeyList new_component_nh_key_list;
    for (ComponentNHKeyList::const_iterator it =
         cnh->component_nh_key_list().begin();
         it != cnh->component_nh_key_list().end(); it++) {
        if ((*it) == NULL)
            continue;
        const TunnelNHKey *tunnel_nh_key =
            static_cast<const TunnelNHKey *>((*it)->nh_key());
        if (std::find(te->ManagedPhysicalDevices().begin(),
                      te->ManagedPhysicalDevices().end(),
                      tunnel_nh_key->dip().to_string()) !=
            te->ManagedPhysicalDevices().end()) {
            new_component_nh_key_list.push_back(*it);
        }
    }
    set_original_nh(cnh);
    DBRequest nh_req(DBRequest::DB_ENTRY_ADD_CHANGE);
    nh_req.key.reset(new CompositeNHKey(Composite::EVPN, false,
                                        new_component_nh_key_list,
                                        cnh->vrf()->GetName()));
    nh_req.data.reset(new CompositeNHData(cnh->pbb_nh(),
                                          cnh->learning_enabled(), false));
    agent->nexthop_table()->Process(nh_req);
    NextHop *new_nh = static_cast<NextHop *>(agent->nexthop_table()->
                      FindActiveEntry(nh_req.key.get()));
    assert(new_nh);
    return new_nh;
}

AgentPath *MulticastRoute::CreateAgentPath(const Peer *peer,
                                           AgentRoute *rt) const {
    return (new MulticastRoutePath(peer));
}

bool MulticastRoute::AddChangePathExtended(Agent *agent, AgentPath *path,
                                           const AgentRoute *rt) {
    bool ret = false;
    NextHop *nh = NULL;

    agent->nexthop_table()->Process(composite_nh_req_);
    nh = static_cast<NextHop *>(agent->nexthop_table()->
            FindActiveEntry(composite_nh_req_.key.get()));
    assert(nh);

    if (agent->params()->agent_mode() == AgentParam::TSN_NO_FORWARDING_AGENT) {
        MulticastRoutePath *multicast_path =
            dynamic_cast<MulticastRoutePath *>(path);
        multicast_path->set_original_nh(nh);
        const TsnElector *te = agent->oper_db()->tsn_elector();
        CompositeNH *cnh = dynamic_cast<CompositeNH *>(nh);
        if (cnh && (cnh->composite_nh_type() == Composite::EVPN)) {
            if (te->IsMaster() == false)
                path->set_inactive(true);
            nh = multicast_path->UpdateNH(agent, cnh, te);
        }
    }
    ret = MulticastRoute::CopyPathParameters(agent,
                   path,
                   vn_name_,
                   false,
                   vxlan_id_,
                   ((label_ == MplsTable::kInvalidLabel) ? path->label() : label_),
                   tunnel_type_,
                   nh, rt, ha_stale_);
    return ret;
}

bool MulticastRoute::CopyPathParameters(Agent *agent,
                                        AgentPath *path,
                                        const std::string &vn_name,
                                        bool unresolved,
                                        uint32_t vxlan_id,
                                        uint32_t label,
                                        uint32_t tunnel_type,
                                        NextHop *nh,
                                        const AgentRoute *rt,
                                        bool ha_stale) {
    VnListType dest_vn_list;
    dest_vn_list.insert(vn_name);
    path->set_dest_vn_list(dest_vn_list);
    path->set_unresolved(unresolved);
    path->set_vxlan_id(vxlan_id);
    if ((path->peer() != agent->local_vm_peer()) &&
        (path->peer() != agent->local_peer()))
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

    uint32_t old_pref = path->path_preference().preference();
    if (ha_stale) {
        if (old_pref != PathPreference::HA_STALE) {
            path->path_preference_non_const().set_preference(PathPreference::HA_STALE);
        }
    } else {
        if (old_pref != PathPreference::LOW) {
            path->path_preference_non_const().set_preference(PathPreference::LOW);
        }
    }

    path->ChangeNH(agent, nh);

    path->ResyncControlWord(rt);

    return true;
}

bool MulticastRoute::CanDeletePath(Agent *agent, AgentPath *path,
                                   const AgentRoute *rt) const {
    return (vxlan_id() == path->vxlan_id());
}

bool PathPreferenceData::AddChangePathExtended(Agent *agent, AgentPath *path,
                                       const AgentRoute *rt) {
    bool ret = false;
    //ECMP flag will not be changed by path preference module,
    //hence retain value in path
    if (!path) {
        return ret;
    }

    path_preference_.set_ecmp(path->path_preference().ecmp());
    path_preference_.set_dependent_ip(path->path_preference().dependent_ip());
    path_preference_.set_vrf(path->path_preference().vrf());
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
    AgentRouteData(AgentRouteData::ADD_DEL_CHANGE, false, 0),
    dest_vn_name_(dest_vn_name) {
    nh_req_.Swap(&nh_req);
}

bool IpamSubnetRoute::AddChangePathExtended(Agent *agent, AgentPath *path,
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

    VnListType dest_vn_list;
    dest_vn_list.insert(dest_vn_name_);
    if (path->dest_vn_list() != dest_vn_list) {
        path->set_dest_vn_list(dest_vn_list);
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
    InetUnicastRouteEntry *uc_rt = static_cast<InetUnicastRouteEntry *>(rt);

    return uc_rt->UpdateRouteFlags(true, false, false);
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
                           const AgentPath *path) const {
    Agent *agent = static_cast<AgentRouteTable *>(get_table())->agent();
    rt_info.set_ip(ToString());
    rt_info.set_vrf(vrf()->GetName());

    switch(event) {
    case ADD:{
        rt_info.set_op("ADD");
        break;
    }

    case DEL: {
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

        case NextHop::PBB: {
            const PBBNH *pbb_nh = static_cast<const PBBNH *>(nh);
            rt_info.set_nh_type("PBB");
            rt_info.set_mac(pbb_nh->dest_bmac().ToString());
            if (pbb_nh->vrf()) {
                rt_info.set_dest_server_vrf(pbb_nh->vrf()->GetName());
            }
            break;
        }

        case NextHop::VRF: {
            const VrfNH *vrf_nh = static_cast<const VrfNH *>(nh);
            rt_info.set_nh_type("VRF");
            rt_info.set_vrf_nh_name(vrf_nh->GetVrf()->GetName());
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

void AgentPath::GetDestinationVnList(std::vector<std::string> *vn_list) const {
    for (VnListType::const_iterator vnit = dest_vn_list().begin();
         vnit != dest_vn_list().end(); ++vnit) {
        vn_list->push_back(*vnit);
    }
}

void AgentPath::SetSandeshData(PathSandeshData &pdata) const {
    const NextHop *nh = nexthop();
    if (nh != NULL) {
        nh->SetNHSandeshData(pdata.nh);
    }
    pdata.set_peer(const_cast<Peer *>(peer())->GetName());
    std::vector<std::string> vn_list;
    GetDestinationVnList(&vn_list);
    pdata.set_dest_vn_list(vn_list);
    pdata.set_origin_vn(origin_vn());
    pdata.set_unresolved(unresolved() ? "true" : "false");

    if (!gw_ip().is_unspecified()) {
        pdata.set_gw_ip(gw_ip().to_string());
        pdata.set_vrf(vrf_name());
    }

    if (ecmp_suppressed()) {
        pdata.set_ecmp_suppressed(true);
    }

    pdata.set_sg_list(sg_list());
    pdata.set_communities(communities());
    pdata.set_tag_list(tag_list());
    pdata.set_vxlan_id(vxlan_id());
    pdata.set_label(label());
    if (nh != NULL && nh->GetType() == NextHop::PBB) {
        const PBBNH *pbb_nh = static_cast<const PBBNH *>(nh);
        if (pbb_nh->child_nh() != NULL) {
            const TunnelNH *tun_nh = dynamic_cast<const TunnelNH *>(pbb_nh->child_nh());
            if (tun_nh != NULL) {
                pdata.set_active_tunnel_type((tun_nh->GetTunnelType()).ToString());
            }
        }
    } else {
        pdata.set_active_tunnel_type(
            TunnelType(tunnel_type()).ToString());
    }
    pdata.set_supported_tunnel_type(
            TunnelType::GetString(tunnel_bmap()));
    PathPreferenceSandeshData path_preference_data;
    path_preference_data.set_sequence(path_preference_.sequence());
    path_preference_data.set_preference(path_preference_.preference());
    path_preference_data.set_ecmp(path_preference_.is_ecmp());
    if ((peer()->GetType() != Peer::BGP_PEER) && (peer()->GetType() != Peer::ECMP_PEER )) {
        path_preference_data.set_wait_for_traffic(
             path_preference_.wait_for_traffic());
    }
    if (path_preference_.dependent_ip().is_unspecified() == false) {
        std::ostringstream str;
        str << path_preference_.vrf() << " : " <<path_preference_.dependent_ip().to_string();
        path_preference_data.set_dependent_ip(str.str());
    }
    pdata.set_path_preference_data(path_preference_data);
    pdata.set_active_label(GetActiveLabel());
    if (peer()->GetType() == Peer::MAC_VM_BINDING_PEER) {
        const MacVmBindingPath *dhcp_path =
            static_cast<const MacVmBindingPath *>(this);
        pdata.set_flood_dhcp(dhcp_path->flood_dhcp() ? "true" : "false");
        pdata.set_vm_name(dhcp_path->vm_interface()->ToString());
    }
    std::vector<std::string> string_vector;
    ecmp_load_balance_.GetStringVector(string_vector);
    std::vector<std::string>::iterator string_vector_iter =
        string_vector.begin();
    std::stringstream ss;
    while (string_vector_iter != string_vector.end()) {
        ss << (*string_vector_iter);
        ss << ",";
        string_vector_iter++;
    }
    pdata.set_ecmp_hashing_fields(ss.str());
    pdata.set_peer_sequence_number(peer_sequence_number());
    const BgpPeer *bgp_peer = dynamic_cast<const BgpPeer *>(peer());
    bool is_stale = false;
    if (bgp_peer) {
        if (peer_sequence_number() < bgp_peer->ChannelSequenceNumber())
            is_stale = true;
    }
    pdata.set_stale(is_stale);
    pdata.set_etree_leaf(etree_leaf());
    pdata.set_layer2_control_word(layer2_control_word());
    pdata.set_inactive(inactive());
}

void AgentPath::set_local_ecmp_mpls_label(MplsLabel *mpls) {
    local_ecmp_mpls_label_.reset(mpls);
}

bool AgentPath::dest_vn_match(const std::string &vn) const {
    if (dest_vn_list_.find(vn) != dest_vn_list_.end())
        return true;
    return false;
}

const MplsLabel* AgentPath::local_ecmp_mpls_label() const {
    return local_ecmp_mpls_label_.get();
}

//In case of composite NH in fabric VRF, if BGP path has router
//id of compute node then copy the ECMP peer or local VM peer path
//to composite NH.
//In case of overlay mode MPLS label was used to frame local nexthop
//list which is not feasible in this fabric VRF
void AgentPath::CopyLocalPath(CompositeNHKey *composite_nh_key,
                              const AgentPath *local_path) {

    if (local_path == NULL) {
        return;
    }

    DBEntryBase::KeyPtr key_nh = local_path->nexthop()->GetDBRequestKey();
    NextHopKey *nh_key = static_cast<NextHopKey *>(key_nh.get());
    nh_key->SetPolicy(false);
    std::auto_ptr<const NextHopKey> nh_key_p(nh_key->Clone());

    ComponentNHKeyList comp_nh_list;
    if (nh_key_p->GetType() == NextHop::COMPOSITE) {
        //Append the list of component NH list from local ecmp peer
        const CompositeNHKey *comp_nh_key =
            static_cast<const CompositeNHKey *>(nh_key);
        comp_nh_list = comp_nh_key->component_nh_key_list();
    } else {
        //Append the interface NH to composite NH list
        ComponentNHKeyPtr new_comp_nh(new ComponentNHKey(0, nh_key_p));
        comp_nh_list.push_back(new_comp_nh);
    }

    composite_nh_key->ReplaceLocalNexthop(comp_nh_list);
}

bool AgentPath::ReorderCompositeNH(Agent *agent,
                                   CompositeNHKey *composite_nh_key,
                                   bool &comp_nh_policy,
                                   const AgentPath *path) {
    if (copy_local_path_) {
        CopyLocalPath(composite_nh_key, path);
    }

    //Find local composite mpls label, if present
    //This has to be done, before expanding component NH
    BOOST_FOREACH(ComponentNHKeyPtr component_nh_key,
                  composite_nh_key->component_nh_key_list()) {
         if (component_nh_key.get() == NULL ||
                 component_nh_key->nh_key()->GetType() != NextHop::COMPOSITE) {
             continue;
         }
         const CompositeNHKey *composite_key =
             static_cast<const CompositeNHKey *>(component_nh_key->nh_key());
         // composite NH, check composite nh type
         // if it is not local ecmp , skip processing
         if (composite_key->composite_nh_type() !=
                        Composite::LOCAL_ECMP) {
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

         if (mpls == local_ecmp_mpls_label_.get()) {
             break;
         }
         local_ecmp_mpls_label_.reset(mpls);
         //Check if MPLS is pointing to same NH as mentioned in key list.
         //It may so happen that by the time this request is serviced(say in
         //case of remote route add from CN), mpls label ahs been re-used for
         //some other purpose. If it is so then ignore the request and wait for
         //another update.
         const NextHopKey *nh_key_1 = component_nh_key->nh_key();
         DBEntryBase::KeyPtr key = mpls->nexthop()->GetDBRequestKey();
         const NextHopKey *nh_key_2 = static_cast<const NextHopKey*>(key.get());
         if (nh_key_1->IsEqual(*nh_key_2) == false) {
             return false;
         }
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
    comp_nh_policy = composite_nh_key->Reorder(agent, label_,
                                               ComputeNextHop(agent));
    //Copy the unchanged component NH list to path data
    set_composite_nh_key(comp_key);
    return true;
}

bool AgentPath::ChangeCompositeNH(Agent *agent,
                                  CompositeNHKey *composite_nh_key) {
    DBRequest nh_req(DBRequest::DB_ENTRY_ADD_CHANGE);
    nh_req.key.reset(composite_nh_key->Clone());
    nh_req.data.reset(new CompositeNHData());

    NextHop *nh = static_cast<NextHop *>(agent->nexthop_table()->
            FindActiveEntry(composite_nh_key));
    if (!nh) {
        agent->nexthop_table()->Process(nh_req);
        nh = static_cast<NextHop *>(agent->nexthop_table()->
            FindActiveEntry(composite_nh_key));
    }

    // NH can be NULL in the following scenario
    // when VMI is deleted and it is the only member of VRF
    // and VMI is part of AAP, then local ecmp route path
    // is deleted from route and vrf delete would trigger route deletions
    // in the VRF. while deleting the active path, import Nh from prev active
    // path function copies the previous active path's NH to current active path
    // and recalucalate composite NH based on composite nh key of current
    // active path. if component nh key element of composite nh key is
    // interface nh key which is already deleted then processing nh req would
    // result NULL, in that case return false.
    //assert(nh);
    if (nh == NULL) {
        return false;
    }

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

MacVmBindingPath::MacVmBindingPath(const Peer *peer) :
    AgentPath(peer, NULL), vm_interface_(NULL), flood_dhcp_(false) {
}

bool MacVmBindingPath::IsLess(const AgentPath &r_path) const {
    return peer()->IsLess(r_path.peer());
}

const NextHop *MacVmBindingPath::ComputeNextHop(Agent *agent) const {
    return nexthop();
}

AgentPath *MacVmBindingPathData::CreateAgentPath(const Peer *peer,
                                         AgentRoute *rt) const {
    const Peer *mac_vm_binding_peer =
        dynamic_cast<const Peer *>(peer);
    assert(mac_vm_binding_peer != NULL);
    return (new MacVmBindingPath(mac_vm_binding_peer));
}

bool MacVmBindingPathData::AddChangePathExtended(Agent *agent, AgentPath *path,
                                         const AgentRoute *rt) {
    bool ret = false;
    MacVmBindingPath *dhcp_path =
        dynamic_cast<MacVmBindingPath *>(path);

    NextHop *nh = agent->nexthop_table()->discard_nh();
    if (path->ChangeNH(agent, nh) == true)
        ret = true;

    if (dhcp_path->flood_dhcp() != flood_dhcp_) {
        dhcp_path->set_flood_dhcp(flood_dhcp_);
        ret = true;
    }

    if (dhcp_path->vm_interface() != vm_intf_) {
        dhcp_path->set_vm_interface(vm_intf_);
        ret = true;
    }

    return ret;
}

void AgentPath::UpdateEcmpHashFields(const Agent *agent,
                                     const EcmpLoadBalance &ecmp_load_balance,
                                     DBRequest &nh_req) {

    NextHop *nh = NULL;
    nh = static_cast<NextHop *>(agent->nexthop_table()->
                                FindActiveEntry(nh_req.key.get()));
    CompositeNH *cnh = dynamic_cast< CompositeNH *>(nh);
    if (cnh) {
        ecmp_hash_fields_.CalculateChangeInEcmpFields(ecmp_load_balance,
                                                     cnh->CompEcmpHashFields());
    } else {
        agent->nexthop_table()->Process(nh_req);
        nh = static_cast<NextHop *>(agent->nexthop_table()->
                                    FindActiveEntry(nh_req.key.get()));
        CompositeNH *cnh = static_cast< CompositeNH *>(nh);
        if (cnh) {
            ecmp_hash_fields_.CalculateChangeInEcmpFields(ecmp_load_balance,
                                                          cnh->CompEcmpHashFields());
        }
    }
}

void AgentPath::ResetEcmpHashFields() {
    ecmp_hash_fields_.Reset();
}

bool AgentPath::ResyncControlWord(const AgentRoute *rt) {
    const BridgeRouteEntry *bridge_rt =
        dynamic_cast<const BridgeRouteEntry *>(rt);
    if (!bridge_rt || rt->vrf() == NULL) {
        return false;
    }

    if (layer2_control_word() != bridge_rt->vrf()->layer2_control_word()) {
        set_layer2_control_word(bridge_rt->vrf()->layer2_control_word());
        return true;
    }

    return false;
}

EvpnRoutingPath::EvpnRoutingPath(const Peer *peer,
                                 AgentRoute *rt,
                                 VrfEntryConstRef routing_vrf) :
    AgentPath(peer, rt), routing_vrf_(routing_vrf) {
        l3_vrf_vxlan_id_ = routing_vrf.get()->vxlan_id();
}

EvpnRoutingPath::~EvpnRoutingPath() {
}

const VrfEntry *EvpnRoutingPath::routing_vrf() const {
    return routing_vrf_.get();
}

void EvpnRoutingPath::set_routing_vrf(const VrfEntry *vrf) {
    routing_vrf_.reset(vrf);
}

void EvpnRoutingPath::DeleteEvpnType5Route(Agent *agent,
                                           const AgentRoute *rt) const {
    const VrfEntry *l3_vrf = routing_vrf_.get();
    if (!l3_vrf)
        return;

    const InetUnicastRouteEntry *inet_rt =
        dynamic_cast<const InetUnicastRouteEntry *>(rt);
    if (!inet_rt) {
        return;
    }
    const EvpnAgentRouteTable *evpn_table =
        static_cast<EvpnAgentRouteTable *>(l3_vrf->GetEvpnRouteTable());
    evpn_table->Delete(agent->local_vm_export_peer(), l3_vrf->GetName(),
                       MacAddress(), inet_rt->addr(),
                       l3_vrf_vxlan_id_);
}

EvpnRoutingData::EvpnRoutingData(DBRequest &nh_req,
                                 const SecurityGroupList &sg_list,
                                 const CommunityList &communities,
                                 const PathPreference &path_preference,
                                 const EcmpLoadBalance &ecmp_load_balance,
                                 const TagList &tag_list,
                                 VrfEntryConstRef vrf_entry,
                                 uint32_t vxlan_id,
                                 const VnListType& vn_list,
                                 const std::string& origin_vn):
    AgentRouteData(AgentRouteData::ADD_DEL_CHANGE, false, 0),
    sg_list_(sg_list),
    communities_(communities),
    path_preference_(path_preference),
    ecmp_load_balance_(ecmp_load_balance),
    tag_list_(tag_list),
    routing_vrf_(vrf_entry), vxlan_id_(vxlan_id),
    dest_vn_list_(vn_list),
    origin_vn_(origin_vn) {
        nh_req_.Swap(&nh_req);
}

AgentPath *EvpnRoutingData::CreateAgentPath(const Peer *peer,
                                            AgentRoute *rt) const {
    return (new EvpnRoutingPath(peer, rt, routing_vrf_));
}

bool EvpnRoutingData::AddChangePathExtended(Agent *agent,
                                            AgentPath *path,
                                            const AgentRoute *rt) {
    bool ret = false;
    EvpnRoutingPath *evpn_path = static_cast<EvpnRoutingPath *>(path);

    //Process nexthop
    agent->nexthop_table()->Process(nh_req_);
    NextHop *nh = static_cast<NextHop *>(agent->nexthop_table()->
                                         FindActiveEntry(nh_req_.key.get()));

    InterfaceNH *intf_nh = dynamic_cast<InterfaceNH *>(nh);
    if (intf_nh) {
        intf_nh->set_delete_on_zero_refcount(true);
    }
    if (routing_vrf_.get() != evpn_path->routing_vrf()) {
        //Remove from old vrf
        evpn_path->DeleteEvpnType5Route(agent, rt);
        evpn_path->set_routing_vrf(routing_vrf_.get());
        ret = true;
    }

    if (path->ChangeNH(agent, nh) == true) {
        ret = true;
    }

    if (sg_list_ != path->sg_list()) {
        path->set_sg_list(sg_list_);
        ret = true;
    }

    /* Setting the origin_vn in the route path */
    if (path->origin_vn() != origin_vn_) {
        path->set_origin_vn(origin_vn_);
        ret = true;
    }

    if (communities_ != path->communities()) {
        path->set_communities(communities_);
        ret =true;
    }

    if (path_preference_ != path->path_preference()) {
        path->set_path_preference(path_preference_);
        ret = true;
    }

    if (ecmp_load_balance_ != path->ecmp_load_balance()) {
        path->set_ecmp_load_balance(ecmp_load_balance_);
        ret =true;
    }

    if (tag_list_ != path->tag_list()) {
        path->set_tag_list(tag_list_);
        ret =true;
    }

    path->set_tunnel_type(TunnelType::VXLAN);
    path->set_tunnel_bmap(TunnelType::VxlanType());
    path->set_vxlan_id(vxlan_id_);

    return ret;
}

bool EvpnRoutingData::CanDeletePath(Agent *agent, AgentPath *path,
                                    const AgentRoute *rt) const {
    EvpnRoutingPath *evpn_path = dynamic_cast<EvpnRoutingPath *>(path);
    if (!evpn_path) {
        return true;
    }
    if (rt->vrf() != evpn_path->routing_vrf()) {
        //Remove from old vrf
        evpn_path->DeleteEvpnType5Route(agent, rt);
    }
    return true;
}
bool EvpnRoutingData::UpdateRoute(AgentRoute *rt) {
    InetUnicastRouteEntry *uc_rt =
        dynamic_cast<InetUnicastRouteEntry *>(rt);
    if (!uc_rt) {
        return false;
    }
    return uc_rt->UpdateRouteFlags(false, false, true);
}
AgentPathEcmpComponent::AgentPathEcmpComponent(IpAddress addr, uint32_t label,
                            AgentRoute *rt):
           addr_(addr), label_(label), dependent_rt_(rt) {}

