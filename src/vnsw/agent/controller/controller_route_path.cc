/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/uuid/uuid_io.hpp>
#include <boost/lexical_cast.hpp>

#include <cmn/agent_cmn.h>
#include <route/route.h>

#include <cmn/agent_cmn.h>
#include <oper/ecmp_load_balance.h>
#include <oper/ecmp.h>
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
#include <controller/controller_types.h>
#include <oper/agent_sandesh.h>
#include <xmpp/xmpp_channel.h>
#include <xmpp_enet_types.h>
#include <xmpp_unicast_types.h>

using namespace std;
using namespace boost::asio;

ControllerPeerPath::ControllerPeerPath(const BgpPeer *peer) :
    AgentRouteData(AgentRouteData::ADD_DEL_CHANGE, false, 0), peer_(peer) {
    sequence_number_ = 0;
    if (peer)
        sequence_number_ = peer->GetAgentXmppChannel()->
            sequence_number();
}

bool ControllerEcmpRoute::CopyToPath(AgentPath *path) {
    bool ret = false;

    path->set_peer_sequence_number(sequence_number());
    if (path->ecmp_load_balance() != ecmp_load_balance_) {
        path->UpdateEcmpHashFields(agent_, ecmp_load_balance_,
                                          nh_req_);
        ret = true;
    }

    return ret;
}

bool ControllerEcmpRoute::AddChangePathExtended(Agent *agent, AgentPath *path,
                                                const AgentRoute *rt) {
    bool path_unresolved = false;
    path->set_copy_local_path(copy_local_path_);

    // check if tunnel type is mpls and vrf is non default
    if ((tunnel_bmap_ == (1 << TunnelType::MPLS_OVER_MPLS)) &&
            (vrf_name_ != agent->fabric_vrf_name())) {

        // this path depends on inet.3 route table,
        // update dependent_table if it is not set already.
        if (!path->GetDependentTable()) {
            InetUnicastAgentRouteTable *table = NULL;
            table = static_cast<InetUnicastAgentRouteTable *>
                (agent->fabric_inet4_mpls_table());
            assert(table != NULL);
            path->SetDependentTable(table);
        }
        InetUnicastAgentRouteTable *table =
            (InetUnicastAgentRouteTable *)path->GetDependentTable();
        assert(table != NULL);
        AgentPathEcmpComponentPtrList new_list;
        ComponentNHKeyList comp_nh_list;
        bool comp_nh_policy = false;
        for (uint32_t i = 0; i < tunnel_dest_list_.size(); i++) {
            // step1: update ecmpcomponent list
            IpAddress addr = tunnel_dest_list_[i];
            uint32_t label = label_list_[i];
            AgentPathEcmpComponentPtr member(new AgentPathEcmpComponent(
                                    addr, label, path->GetParentRoute()));
            InetUnicastRouteEntry *uc_rt = table->FindRoute(addr);
            if (uc_rt == NULL || uc_rt->plen() == 0) {
                member->SetUnresolved(true);
                if (!path_unresolved) {
                    path_unresolved  = true;
                    DBEntryBase::KeyPtr key =
                     agent->nexthop_table()->discard_nh()->GetDBRequestKey();
                    NextHopKey *nh_key =
                        static_cast<NextHopKey *>(key.release());
                    std::auto_ptr<const NextHopKey> nh_key_ptr(nh_key);
                    ComponentNHKeyPtr component_nh_key(new ComponentNHKey(label,
                                                   nh_key_ptr));
                    comp_nh_list.push_back(component_nh_key);
                }
            } else {
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
                ComponentNHKeyPtr component_nh_key(new ComponentNHKey(label,
                                                   nh_key_ptr));
                comp_nh_list.push_back(component_nh_key);
                //Reset to new gateway route, no nexthop for indirect route
                member->UpdateDependentRoute(uc_rt);
                member->SetUnresolved(false);
            }
            new_list.push_back(member);
        }
        DBRequest nh_req(DBRequest::DB_ENTRY_ADD_CHANGE);
        nh_req.key.reset(new CompositeNHKey(Composite::ECMP, comp_nh_policy,
                                    comp_nh_list, vrf_name_));
        nh_req.data.reset(new CompositeNHData());
        nh_req_.Swap(&nh_req);
        path->ResetEcmpMemberList(new_list);
    }
    path->set_vrf_name(vrf_name_);

    CompositeNHKey *comp_key = static_cast<CompositeNHKey *>(nh_req_.key.get());
    bool ret = false;

    //Reorder the component NH list, and add a reference to local composite mpls
    //label if any
    bool comp_nh_policy = comp_key->GetPolicy();
    bool new_comp_nh_policy = false;
    if (path->ReorderCompositeNH(agent, comp_key, new_comp_nh_policy,
                                 rt->FindLocalVmPortPath()) == false)
        return false;
    if (!comp_nh_policy) {
        comp_key->SetPolicy(new_comp_nh_policy);
    }
    ret |= CopyToPath(path);

    EcmpData ecmp_data(agent, rt->vrf()->GetName(), rt->ToString(),
                       path, false);
    ret |= ecmp_data.UpdateWithParams(sg_list_, tag_list_, CommunityList(),
                                path_preference_, tunnel_bmap_,
                                ecmp_load_balance_, vn_list_, nh_req_);
    path->set_unresolved(path_unresolved);

    return ret;
}

ControllerEcmpRoute::ControllerEcmpRoute(const BgpPeer *peer,
                        const VnListType &vn_list,
                        const EcmpLoadBalance &ecmp_load_balance,
                        const TagList &tag_list,
                        const SecurityGroupList &sg_list,
                        const PathPreference &path_pref,
                        TunnelType::TypeBmap tunnel_bmap,
                        DBRequest &nh_req,
                        const std::string &prefix_str) :
    ControllerPeerPath(peer), vn_list_(vn_list), sg_list_(sg_list),
    ecmp_load_balance_(ecmp_load_balance), tag_list_(tag_list),
    path_preference_(path_pref), tunnel_bmap_(tunnel_bmap),
    copy_local_path_(false) {
        nh_req_.Swap(&nh_req);
    agent_ = peer->agent();
    vrf_name_ = agent_->fabric_vrf_name();
}

ControllerEcmpRoute::ControllerEcmpRoute(const BgpPeer *peer,
                        const VnListType &vn_list,
                        const EcmpLoadBalance &ecmp_load_balance,
                        const TagList &tag_list,
                        const SecurityGroupList &sg_list,
                        const PathPreference &path_pref,
                        TunnelType::TypeBmap tunnel_bmap,
                        std::vector<IpAddress> &tunnel_dest_list,
                        std::vector<uint32_t> &label_list,
                        const std::string &prefix_str,
                        const std::string &vrf_name):
    ControllerPeerPath(peer), vn_list_(vn_list), sg_list_(sg_list),
    ecmp_load_balance_(ecmp_load_balance), tag_list_(tag_list),
    path_preference_(path_pref), tunnel_bmap_(tunnel_bmap),
    copy_local_path_(false), tunnel_dest_list_(tunnel_dest_list),
   label_list_(label_list), vrf_name_(vrf_name) {
    agent_ = peer->agent();
   }

template <typename TYPE>
ControllerEcmpRoute::ControllerEcmpRoute(const BgpPeer *peer,
                                         const VnListType &vn_list,
                                         const EcmpLoadBalance &ecmp_load_balance,
                                         const TagList &tag_list,
                                         const TYPE *item,
                                         const AgentRouteTable *rt_table,
                                         const std::string &prefix_str) :
        ControllerPeerPath(peer), vn_list_(vn_list),
        ecmp_load_balance_(ecmp_load_balance), tag_list_(tag_list),
        copy_local_path_(false) {
    const AgentXmppChannel *channel = peer->GetAgentXmppChannel();
    std::string bgp_peer_name = channel->GetBgpPeerName();
    std::string vrf_name = rt_table->vrf_name();
    agent_ = rt_table->agent();
    vrf_name_  = vrf_name;
    Composite::Type composite_nh_type = Composite::ECMP;

    // use LOW PathPreference if local preference attribute is not set
    uint32_t preference = PathPreference::LOW;
    TunnelType::TypeBmap encap = TunnelType::AllType(); //default
    if (item->entry.local_preference != 0) {
        preference = item->entry.local_preference;
    }

    //Override encap type for fabric VRF routes
    //only type supported is underlay
    if (vrf_name == agent_->fabric_vrf_name()) {
        encap = TunnelType::NativeType();
    }

    PathPreference rp(item->entry.sequence_number, preference, false, false);
    path_preference_ = rp;

    sg_list_ = item->entry.security_group_list.security_group;

    ComponentNHKeyList comp_nh_list;
    bool comp_nh_policy = false;
    for (uint32_t i = 0; i < item->entry.next_hops.next_hop.size(); i++) {
        std::string nexthop_addr =
            item->entry.next_hops.next_hop[i].address;
        boost::system::error_code ec;
        IpAddress addr = IpAddress::from_string(nexthop_addr, ec);
        if (ec.value() != 0) {
            CONTROLLER_TRACE(Trace, bgp_peer_name, vrf_name,
                             "Error parsing nexthop ip address");
            continue;
        }
        if (!addr.is_v4()) {
            CONTROLLER_TRACE(Trace, bgp_peer_name, vrf_name,
                             "Non IPv4 address not supported as nexthop");
            continue;
        }

        if (comp_nh_list.size() >= maximum_ecmp_paths) {
            std::stringstream msg;
            msg << "Nexthop paths for prefix "
                << prefix_str
                << " (" << item->entry.next_hops.next_hop.size()
                << ") exceed the maximum supported, ignoring them";
            CONTROLLER_TRACE(Trace, bgp_peer_name, vrf_name, msg.str());
            break;
        }

        uint32_t label = item->entry.next_hops.next_hop[i].label;
        if (agent_->router_id() == addr.to_v4()) {
            //Get local list of interface and append to the list
            MplsLabel *mpls =
                agent_->mpls_table()->FindMplsLabel(label);
            if (mpls != NULL) {
                if (mpls->nexthop()->GetType() == NextHop::VRF) {
                    ClonedLocalPath *data =
                        new ClonedLocalPath(label, vn_list,
                                item->entry.security_group_list.security_group,
                                tag_list, sequence_number());
                    cloned_data_list_.push_back(data);
                    continue;
                }

                const NextHop *mpls_nh = mpls->nexthop();
                DBEntryBase::KeyPtr key = mpls_nh->GetDBRequestKey();
                NextHopKey *nh_key = static_cast<NextHopKey *>(key.release());
                if (nh_key->GetType() != NextHop::COMPOSITE) {
                    //By default all component members of composite NH
                    //will be policy disabled, except for component NH
                    //of type composite
                    nh_key->SetPolicy(false);
                }
                std::auto_ptr<const NextHopKey> nh_key_ptr(nh_key);
                ComponentNHKeyPtr component_nh_key(new ComponentNHKey(label,
                                                   nh_key_ptr));
                comp_nh_list.push_back(component_nh_key);
                if (!comp_nh_policy) {
                    comp_nh_policy = mpls_nh->NexthopToInterfacePolicy();
                }
                tunnel_dest_list_.push_back(addr);
                label_list_.push_back(label);
            } else if (label == 0) {
                copy_local_path_ = true;
            }
        } else {
            encap = agent_->controller()->GetTypeBitmap
                (item->entry.next_hops.next_hop[i].tunnel_encapsulation_list);
            if (vrf_name == agent_->fabric_vrf_name()) {
                if (label != MplsTable::kInvalidLabel &&
                        label != MplsTable::kInvalidExportLabel) {
                    encap = TunnelType::MplsoMplsType();
                } else {
                    encap = TunnelType::NativeType();
                }
            }
            MacAddress mac = agent_->controller()->GetTunnelMac
                (item->entry.next_hops.next_hop[i]);
            if (encap == TunnelType::MplsoMplsType() &&
                    vrf_name == agent_->fabric_vrf_name()) {
                composite_nh_type = Composite::LU_ECMP;
                LabelledTunnelNHKey *nh_key = new LabelledTunnelNHKey(
                                            agent_->fabric_vrf_name(),
                                            agent_->router_id(),
                                            addr.to_v4(),
                                            false,
                                            TunnelType::ComputeType(encap),
                                            mac,
                                            label);
                std::auto_ptr<const NextHopKey> nh_key_ptr(nh_key);
                ComponentNHKeyPtr component_nh_key(new ComponentNHKey(label,
                                                                    nh_key_ptr));
                comp_nh_list.push_back(component_nh_key);
            } else if (encap == TunnelType::MplsoMplsType()) {
                // copy ecmp nexthop list
                tunnel_dest_list_.push_back(addr);
                label_list_.push_back(label);
            } else {

                TunnelNHKey *nh_key = new TunnelNHKey(agent_->fabric_vrf_name(),
                                                    agent_->router_id(),
                                                    addr.to_v4(),
                                                    false,
                                                    TunnelType::ComputeType(encap),
                                                    mac);
                std::auto_ptr<const NextHopKey> nh_key_ptr(nh_key);
                ComponentNHKeyPtr component_nh_key(new ComponentNHKey(label,
                                                                    nh_key_ptr));
                comp_nh_list.push_back(component_nh_key);
            }
        }
    }
    
    tunnel_bmap_ = encap;
    if ((encap != TunnelType::MplsoMplsType()) ||
            ((encap == TunnelType::MplsoMplsType()) &&
             (vrf_name_ == agent_->fabric_vrf_name())))  {
        // Build the NH request and then create route data to be passed
        DBRequest nh_req(DBRequest::DB_ENTRY_ADD_CHANGE);
        nh_req.key.reset(new CompositeNHKey(composite_nh_type, comp_nh_policy,
                                            comp_nh_list, vrf_name));
        nh_req.data.reset(new CompositeNHData());
        nh_req_.Swap(&nh_req);
    }
 }
ControllerVmRoute *ControllerVmRoute::MakeControllerVmRoute(
                                         const BgpPeer *bgp_peer,
                                         const string &default_vrf,
                                         const Ip4Address &router_id,
                                         const string &vrf_name,
                                         const Ip4Address &tunnel_dest,
                                         TunnelType::TypeBmap bmap,
                                         uint32_t label,
                                         const MacAddress rewrite_dmac,
                                         const VnListType &dest_vn_list,
                                         const SecurityGroupList &sg_list,
                                         const TagList &tag_list,
                                         const PathPreference &path_preference,
                                         bool ecmp_suppressed,
                                         const EcmpLoadBalance &ecmp_load_balance,
                                         bool etree_leaf) {

    DBRequest nh_req(DBRequest::DB_ENTRY_ADD_CHANGE);
    // Make Tunnel-NH request
    nh_req.key.reset(new TunnelNHKey(default_vrf, router_id, tunnel_dest, false,
                                    TunnelType::ComputeType(bmap),
                                    rewrite_dmac));
    nh_req.data.reset(new TunnelNHData());

    // Make route request pointing to Tunnel-NH created above
    ControllerVmRoute *data =
        new ControllerVmRoute(bgp_peer, default_vrf, tunnel_dest, label,
                              dest_vn_list, bmap, sg_list, tag_list, path_preference,
                              nh_req, ecmp_suppressed,
                              ecmp_load_balance, etree_leaf, rewrite_dmac);
    return data;
}

ControllerMplsRoute *ControllerMplsRoute::MakeControllerMplsRoute(
                                         const BgpPeer *bgp_peer,
                                         const string &default_vrf,
                                         const Ip4Address &router_id,
                                         const string &vrf_name,
                                         const Ip4Address &tunnel_dest,
                                         TunnelType::TypeBmap bmap,
                                         uint32_t label,
                                         const MacAddress rewrite_dmac,
                                         const VnListType &dest_vn_list,
                                         const SecurityGroupList &sg_list,
                                         const TagList &tag_list,
                                         const PathPreference &path_preference,
                                         bool ecmp_suppressed,
                                         const EcmpLoadBalance &ecmp_load_balance,
                                         bool etree_leaf) {
 
    DBRequest nh_req(DBRequest::DB_ENTRY_ADD_CHANGE);
    // Make Labelled Tunnel-NH request
    nh_req.key.reset(new LabelledTunnelNHKey(default_vrf, router_id, tunnel_dest, false,
                                    TunnelType::MPLS_OVER_MPLS,
                                    rewrite_dmac, label));
    nh_req.data.reset(new LabelledTunnelNHData());

    // Make route request pointing to Labelled Tunnel-NH created above
    ControllerMplsRoute *data =
        new ControllerMplsRoute(bgp_peer, default_vrf, tunnel_dest, label,
                              dest_vn_list, bmap, sg_list, tag_list, path_preference,
                              nh_req, ecmp_suppressed,
                              ecmp_load_balance, etree_leaf, rewrite_dmac);
    return data;
}
bool ControllerVmRoute::UpdateRoute(AgentRoute *rt) {
    bool ret = false;
    // For IP subnet routes with Tunnel NH, update arp_flood_ and
    // local_host_flag_ flags
    if ((rt->GetTableType() == Agent::INET4_UNICAST) ||
        (rt->GetTableType() == Agent::INET6_UNICAST)) {
        InetUnicastRouteEntry *inet_rt =
            static_cast<InetUnicastRouteEntry *>(rt);
        //If its the IPAM route then no change required neither
        //super net needs to be searched.
        if (inet_rt->ipam_subnet_route())
            return ret;

        bool ipam_subnet_route = inet_rt->IpamSubnetRouteAvailable();
        ret = inet_rt->UpdateIpamHostFlags(ipam_subnet_route);
    }
    return ret;
}

bool ControllerVmRoute::AddChangePathExtended(Agent *agent, AgentPath *path,
                                              const AgentRoute *rt) {
    bool ret = false;
    NextHop *nh = NULL;
    SecurityGroupList path_sg_list;

    path->set_peer_sequence_number(sequence_number());
    if (path->tunnel_bmap() != tunnel_bmap_) {
        path->set_tunnel_bmap(tunnel_bmap_);
        ret = true;
    }

    TunnelType::Type new_tunnel_type = TunnelType::ComputeType(tunnel_bmap_);
    if (new_tunnel_type == TunnelType::MPLS_OVER_MPLS) {
        // this path depends on inet.3 route table,
        // update dependent_table if it is not set already.
        if (!path->GetDependentTable()) {
            InetUnicastAgentRouteTable *table = NULL;
            table = static_cast<InetUnicastAgentRouteTable *>
                (agent->fabric_inet4_mpls_table());
            assert(table != NULL);
            path->SetDependentTable(table);
        }
        InetUnicastAgentRouteTable *table =
            (InetUnicastAgentRouteTable *)path->GetDependentTable();

        AgentPathEcmpComponentPtrList new_list; 
        AgentPathEcmpComponentPtr member( new AgentPathEcmpComponent(
                                        tunnel_dest_, label_,
                                        path->GetParentRoute()));
        InetUnicastRouteEntry *uc_rt = table->FindRoute(tunnel_dest_);
        if (uc_rt == NULL || uc_rt->plen() == 0) {
            path->set_unresolved(true);
            member->SetUnresolved(false);
        } else {
            path->set_unresolved(false);
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
            path->set_gw_ip(tunnel_dest_);
            path->set_nexthop(NULL);
        }
        new_list.push_back(member);
        path->ResetEcmpMemberList(new_list);

    } else {

        if ((tunnel_bmap_ == (1 << TunnelType::VXLAN) &&
            (new_tunnel_type != TunnelType::VXLAN)) ||
            (tunnel_bmap_ != (1 << TunnelType::VXLAN) &&
            (new_tunnel_type == TunnelType::VXLAN))) {
            new_tunnel_type = TunnelType::INVALID;
            nh_req_.key.reset(new TunnelNHKey(agent->fabric_vrf_name(),
                                            agent->router_id(), tunnel_dest_,
                                            false, new_tunnel_type,
                                            rewrite_dmac_));
        }
        agent->nexthop_table()->Process(nh_req_);
        TunnelNHKey key(agent->fabric_vrf_name(), agent->router_id(), tunnel_dest_,
                        false, new_tunnel_type, rewrite_dmac_);
        nh = static_cast<NextHop *>(agent->nexthop_table()->FindActiveEntry(&key));

        path->set_unresolved(false);
    }
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
    } else if ((tunnel_bmap_ == TunnelType::MplsType())||
            (tunnel_bmap_ == TunnelType::MplsoMplsType())) {
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

    if (nh != NULL) {
        if (path->ChangeNH(agent, nh) == true)
        ret = true;
    }

    path_sg_list = path->sg_list();
    if (path_sg_list != sg_list_) {
        path->set_sg_list(sg_list_);
        ret = true;
    }

    if (tag_list_ != path->tag_list()) {
        path->set_tag_list(tag_list_);
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

    if (path->etree_leaf() != etree_leaf_) {
        path->set_etree_leaf(etree_leaf_);
        ret = true;
    }

    return ret;
}
bool ControllerMplsRoute::AddChangePathExtended(Agent *agent, AgentPath *path,
                                              const AgentRoute *rt) {
    bool ret = false;
    NextHop *nh = NULL;
    SecurityGroupList path_sg_list;

    path->set_peer_sequence_number(sequence_number());
    if (path->tunnel_bmap() != tunnel_bmap_) {
        path->set_tunnel_bmap(tunnel_bmap_);
        ret = true;
    }
 
    agent->nexthop_table()->Process(nh_req_);
    LabelledTunnelNHKey key(agent->fabric_vrf_name(), agent->router_id(), tunnel_dest_,
                    false, TunnelType::MPLS_OVER_MPLS, rewrite_dmac_, label_);
    nh = static_cast<NextHop *>(agent->nexthop_table()->FindActiveEntry(&key));
    path->set_tunnel_dest(tunnel_dest_);

    if (path->label() != label_) {
        path->set_label(label_);
        ret = true;
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

    if (tag_list_ != path->tag_list()) {
        path->set_tag_list(tag_list_);
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

    if (path->etree_leaf() != etree_leaf_) {
        path->set_etree_leaf(etree_leaf_);
        ret = true;
    }

    return ret;
}

bool ClonedLocalPath::AddChangePathExtended(Agent *agent, AgentPath *path,
                                            const AgentRoute *rt) {
    bool ret = false;

    AgentPath *local_path = NULL;
    if (mpls_label_ == MplsTable::kInvalidExportLabel) {
        local_path = rt->FindPath(agent->fabric_rt_export_peer());
        if (local_path == NULL) {
            local_path = rt->FindLocalVmPortPath();
        }
    } else {
        MplsLabel *mpls = agent->mpls_table()->FindMplsLabel(mpls_label_);
        if (!mpls) {
            return ret;
        }

        assert(mpls->nexthop()->GetType() == NextHop::VRF);
        const VrfNH *vrf_nh = static_cast<const VrfNH *>(mpls->nexthop());
        const InetUnicastRouteEntry *uc_rt =
            static_cast<const InetUnicastRouteEntry *>(rt);
        const AgentRoute *mpls_vrf_uc_rt =
            vrf_nh->GetVrf()->GetUcRoute(uc_rt->addr());
        if (mpls_vrf_uc_rt == NULL) {
            return ret;
        }
        local_path = mpls_vrf_uc_rt->FindLocalVmPortPath();
    }

    path->set_peer_sequence_number(sequence_number_);

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

    if (path->path_preference() != local_path->path_preference()) {
        path->set_path_preference(local_path->path_preference());
        ret = true;
    }

    NextHop *nh = const_cast<NextHop *>(local_path->ComputeNextHop(agent));
    if (path->ChangeNH(agent, nh) == true) {
        ret = true;
    }
    return ret;
}

bool StalePathData::AddChangePathExtended(Agent *agent, AgentPath *path,
                                          const AgentRoute *route) {
    if (path->peer_sequence_number() >= sequence_number_)
        return false;

    if (route->IsDeleted())
        return false;

    //Delete old stale path
    DBRequest req(DBRequest::DB_ENTRY_DELETE);
    AgentRouteKey *key = (static_cast<AgentRouteKey *>(route->
                                      GetDBRequestKey().get()))->Clone();
    key->set_peer(path->peer());
    req.key.reset(key);
    req.data.reset();
    AgentRouteTable *table = static_cast<AgentRouteTable *>(route->get_table());
    table->Process(req);
    return true;
}

bool StalePathData::CanDeletePath(Agent *agent, AgentPath *path,
                                  const AgentRoute *route) const {
    if (path->peer_sequence_number() >= sequence_number_)
       return false;
   return true;
}

//Force instantiation
template ControllerEcmpRoute::ControllerEcmpRoute(const BgpPeer *peer,
 const VnListType &vn_list,
 const EcmpLoadBalance &ecmp_load_balance,
 const TagList &tag_list, const autogen::ItemType *item,
 const AgentRouteTable *rt_table, const std::string &prefix_str);
template ControllerEcmpRoute::ControllerEcmpRoute(const BgpPeer *peer,
 const VnListType &vn_list,
 const EcmpLoadBalance &ecmp_load_balance,
 const TagList &tag_list, const autogen::EnetItemType *item,
 const AgentRouteTable *rt_table, const std::string &prefix_str);
