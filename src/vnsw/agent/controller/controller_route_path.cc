/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
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
#include <oper/peer.h>
#include <oper/agent_route.h>
#include <controller/controller_route_path.h>
#include <controller/controller_peer.h>
#include <controller/controller_init.h>
#include <controller/controller_export.h>
#include <oper/agent_sandesh.h>

using namespace std;
using namespace boost::asio;

/*
 * Common routine for all controller route data types
 * to verify peer validity.
 */
bool CheckPeerValidity(const AgentXmppChannel *channel,
                       uint64_t sequence_number) {
    //TODO reaching here as test case route add called with NULL peer
    if (sequence_number == ControllerPeerPath::kInvalidPeerIdentifier)
        return true;

    assert(channel);
    if (channel->bgp_peer_id() == NULL)
       return false;

    if (sequence_number == channel->unicast_sequence_number())
        return true;

    return false;
}

ControllerPeerPath::ControllerPeerPath(const Peer *peer) :
    AgentRouteData(false), peer_(peer) {
    if ((peer != NULL) && (peer->GetType() == Peer::BGP_PEER) ) {
        const BgpPeer *bgp_peer = static_cast<const BgpPeer *>(peer);
        channel_ = bgp_peer->GetBgpXmppPeerConst();
        sequence_number_ = channel_->unicast_sequence_number();
    } else {
        channel_ = NULL;
        sequence_number_ = kInvalidPeerIdentifier;
    }
}

bool ControllerEcmpRoute::IsPeerValid() const {
    return CheckPeerValidity(channel(), sequence_number());
}

bool ControllerEcmpRoute::AddChangePath(Agent *agent, AgentPath *path) {
    CompositeNHKey *comp_key = static_cast<CompositeNHKey *>(nh_req_.key.get());
    //Reorder the component NH list, and add a reference to local composite mpls
    //label if any
    path->SetCompositeNH(agent, comp_key, false);
    return Inet4UnicastRouteEntry::ModifyEcmpPath(dest_addr_, plen_, vn_name_,
                                                  label_, local_ecmp_nh_,
                                                  vrf_name_, sg_list_,
                                                  path_preference_,
                                                  nh_req_, agent, path);
}

ControllerVmRoute *ControllerVmRoute::MakeControllerVmRoute(const Peer *peer,
                                         const string &default_vrf,
                                         const Ip4Address &router_id,
                                         const string &vrf_name,
                                         const Ip4Address &server_ip, 
                                         TunnelType::TypeBmap bmap,
                                         uint32_t label,
                                         const string &dest_vn_name,
                                         const SecurityGroupList &sg_list,
                                         const PathPreference &path_preference) {
    // Make Tunnel-NH request
    DBRequest nh_req(DBRequest::DB_ENTRY_ADD_CHANGE);
    nh_req.key.reset(new TunnelNHKey(default_vrf, router_id, server_ip, false,
                                     TunnelType::ComputeType(bmap)));
    nh_req.data.reset(new TunnelNHData());

    // Make route request pointing to Tunnel-NH created above
    ControllerVmRoute *data =
        new ControllerVmRoute(peer, default_vrf, server_ip, label, dest_vn_name,
                              bmap, sg_list, path_preference, nh_req);
    return data;
}

bool ControllerVmRoute::IsPeerValid() const {
    return CheckPeerValidity(channel(), sequence_number());
}

bool ControllerVmRoute::AddChangePath(Agent *agent, AgentPath *path) {
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
                                          agent->router_id(), server_ip_,
                                          false, new_tunnel_type));
    }
    agent->nexthop_table()->Process(nh_req_);
    TunnelNHKey key(agent->fabric_vrf_name(), agent->router_id(), server_ip_,
                    false, new_tunnel_type);
    nh = static_cast<NextHop *>(agent->nexthop_table()->FindActiveEntry(&key));
    path->set_server_ip(server_ip_);

    if (path->tunnel_type() != new_tunnel_type) {
        path->set_tunnel_type(new_tunnel_type);
        ret = true;
    }

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

    if (path->dest_vn_name() != dest_vn_name_) {
        path->set_dest_vn_name(dest_vn_name_);
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

    return ret;
}

ControllerLocalVmRoute::ControllerLocalVmRoute(const VmInterfaceKey &intf,
                                               uint32_t mpls_label,
                                               uint32_t vxlan_id,
                                               bool force_policy,
                                               const string &vn_name,
                                               uint8_t flags,
                                               const SecurityGroupList &sg_list,
                                               const PathPreference &path_preference,
                                               uint64_t sequence_number,
                                               const AgentXmppChannel *channel) :
    LocalVmRoute(intf, mpls_label, vxlan_id, force_policy, vn_name, flags, sg_list,
                 path_preference), sequence_number_(sequence_number), channel_(channel) { }

bool ControllerLocalVmRoute::IsPeerValid() const {
    return CheckPeerValidity(channel_, sequence_number_);
}

ControllerVlanNhRoute::ControllerVlanNhRoute(const VmInterfaceKey &intf,
                                             uint32_t tag,
                                             uint32_t label,
                                             const string &dest_vn_name,
                                             const SecurityGroupList &sg_list,
                                             const PathPreference &path_preference,
                                             uint64_t sequence_number,
                                             const AgentXmppChannel *channel) :
    VlanNhRoute(intf, tag, label, dest_vn_name, sg_list, path_preference),
    sequence_number_(sequence_number), channel_(channel) { }

bool ControllerVlanNhRoute::IsPeerValid() const {
    return CheckPeerValidity(channel_, sequence_number_);
}

ControllerInetInterfaceRoute::ControllerInetInterfaceRoute(const InetInterfaceKey &intf,
                                                           uint32_t label,
                                                           int tunnel_bmap,
                                                           const string &dest_vn_name,
                                                           uint64_t sequence_number,
                                                           const AgentXmppChannel *channel) :
    InetInterfaceRoute(intf, label, tunnel_bmap, dest_vn_name),
    sequence_number_(sequence_number), channel_(channel) { }

bool ControllerInetInterfaceRoute::IsPeerValid() const {
    return CheckPeerValidity(channel_, sequence_number_);
}
