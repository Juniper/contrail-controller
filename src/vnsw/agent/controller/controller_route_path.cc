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

ControllerPeerPath::ControllerPeerPath(const Peer *peer) :
    AgentRouteData(false), peer_(peer) {
    //TODO Test cases send NULL peer, fix them, for non bgp peer ignore.
    if ((peer == NULL) || (peer->GetType() != Peer::BGP_PEER) ) {
        channel_ = NULL;
        sequence_number_ = VNController::kInvalidPeerIdentifier;
    } else { 
        const BgpPeer *bgp_peer = static_cast<const BgpPeer *>(peer);
        channel_ = bgp_peer->GetBgpXmppPeerConst();

        assert(channel_);
        sequence_number_ = bgp_peer->GetBgpXmppPeerConst()->
            unicast_sequence_number();
    }
}

bool ControllerPeerPath::CheckPeerValidity() const {
    //TODO reaching here as test case route add called with NULL peer
    if (sequence_number_ == VNController::kInvalidPeerIdentifier)
        return true;

    assert(channel_);
    if (channel_->bgp_peer_id() == NULL)
       return false;

    if (sequence_number_ == channel_->unicast_sequence_number())
        return true;

    return false;
}

bool ControllerEcmpRoute::IsPeerValid() const {
    return CheckPeerValidity();
}

bool ControllerEcmpRoute::AddChangePath(Agent *agent, AgentPath *path) {
    CompositeNHKey *comp_key = static_cast<CompositeNHKey *>(nh_req_.key.get());
    path->SetCompositeNH(agent, comp_key);
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
    return CheckPeerValidity();
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

    return ret;
}
