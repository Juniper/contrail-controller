/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/uuid/uuid_io.hpp>

#include <cmn/agent_cmn.h>
#include <route/route.h>

#include <oper/route_common.h>
#include <oper/vrf.h>
#include <oper/tunnel_nh.h>
#include <oper/mpls.h>
#include <oper/mirror_table.h>
#include <controller/controller_export.h>
#include <controller/controller_peer.h>
#include <oper/agent_sandesh.h>

using namespace std;
using namespace boost::asio;

/////////////////////////////////////////////////////////////////////////////
// Utility functions
/////////////////////////////////////////////////////////////////////////////
static void Layer2TableEnqueue(Agent *agent, DBRequest *req) {
    AgentRouteTable *table = agent->fabric_l2_unicast_table();
    if (table) {
        table->Enqueue(req);
    }
}

static void Layer2TableProcess(Agent *agent, const string &vrf_name,
                               DBRequest &req) {
    AgentRouteTable *table =
        agent->vrf_table()->GetLayer2RouteTable(vrf_name);
    if (table) {
        table->Process(req);
    }
}

/////////////////////////////////////////////////////////////////////////////
// Layer2RouteKey methods
/////////////////////////////////////////////////////////////////////////////
string Layer2RouteKey::ToString() const {
    return dmac_.ToString();
}

Layer2RouteKey *Layer2RouteKey::Clone() const {
    return new Layer2RouteKey(peer_, vrf_name_, dmac_);
}

AgentRoute *
Layer2RouteKey::AllocRouteEntry(VrfEntry *vrf, bool is_multicast) const
{
    Layer2RouteEntry *entry = new Layer2RouteEntry(vrf, dmac_,
                                                   peer()->GetType(),
                                                   is_multicast);
    return static_cast<AgentRoute *>(entry);
}

/////////////////////////////////////////////////////////////////////////////
// Layer2AgentRouteTable methods
/////////////////////////////////////////////////////////////////////////////
DBTableBase *Layer2AgentRouteTable::CreateTable(DB *db,
                                                const std::string &name) {
    AgentRouteTable *table = new Layer2AgentRouteTable(db, name);
    table->Init();
    return table;
}

Layer2RouteEntry *Layer2AgentRouteTable::FindRoute(const Agent *agent,
                                                   const string &vrf_name,
                                                   const MacAddress &mac) {
    VrfEntry *vrf = agent->vrf_table()->FindVrfFromName(vrf_name);
    if (vrf == NULL)
        return NULL;

    Layer2RouteKey key(agent->local_vm_peer(), vrf_name, mac, 0);
    Layer2AgentRouteTable *table = static_cast<Layer2AgentRouteTable *>
        (vrf->GetLayer2RouteTable());
    Layer2RouteEntry *route =
        static_cast<Layer2RouteEntry *>(table->FindActiveEntry(&key));
    return route;
}

/////////////////////////////////////////////////////////////////////////////
// Layer2AgentRouteTable utility methods to add/delete routes
/////////////////////////////////////////////////////////////////////////////
void Layer2AgentRouteTable::AddLayer2ReceiveRoute(const Peer *peer,
                                                  const string &vrf_name,
                                                  const MacAddress &mac,
                                                  const string &vn_name,
                                                  const string &interface,
                                                  bool policy) {
    Agent *agent = Agent::GetInstance();
    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    req.key.reset(new Layer2RouteKey(peer, vrf_name, mac, 0));

    PacketInterfaceKey intf_key(nil_uuid(), agent->pkt_interface_name());
    req.data.reset(new HostRoute(intf_key, vn_name));

    Layer2TableEnqueue(agent, &req);
}

void Layer2AgentRouteTable::AddLayer2ReceiveRouteReq(const Peer *peer,
                                                     const string &vrf_name,
                                                     uint32_t vxlan_id,
                                                     const MacAddress &mac,
                                                     const string &vn_name) {
    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    req.key.reset(new Layer2RouteKey(peer, vrf_name, mac, vxlan_id));
    req.data.reset(new L2ReceiveRoute(vn_name, vxlan_id));
    Enqueue(&req);
}

void Layer2AgentRouteTable::AddLayer2ReceiveRoute(const Peer *peer,
                                                  const string &vrf_name,
                                                  uint32_t vxlan_id,
                                                  const MacAddress &mac,
                                                  const string &vn_name) {
    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    req.key.reset(new Layer2RouteKey(peer, vrf_name, mac, vxlan_id));
    req.data.reset(new L2ReceiveRoute(vn_name, vxlan_id));
    Process(req);
}

void Layer2AgentRouteTable::AddLayer2Route(AgentRoute *rt) {
    EvpnRouteEntry *evpn_rt =
        static_cast<EvpnRouteEntry *>(rt);
    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    req.key.reset(new Layer2RouteKey(agent()->evpn_peer(),
                                     evpn_rt->vrf()->GetName(),
                                     evpn_rt->mac(), 0));
    req.data.reset(new EvpnPathData(evpn_rt));
    Layer2TableProcess(agent(), vrf_name(), req);
}

void Layer2AgentRouteTable::DeleteLayer2Route(AgentRoute *rt) {
    EvpnRouteEntry *evpn_rt =
        static_cast<EvpnRouteEntry *>(rt);
    DBRequest req(DBRequest::DB_ENTRY_DELETE);
    req.key.reset(new Layer2RouteKey(agent()->evpn_peer(),
                                     evpn_rt->vrf()->GetName(),
                                     evpn_rt->mac(), 0));
    req.data.reset(new EvpnPathData(evpn_rt));
    Layer2TableProcess(Agent::GetInstance(), evpn_rt->vrf()->GetName(), req);
}

void Layer2AgentRouteTable::DeleteReq(const Peer *peer, const string &vrf_name,
                                      const MacAddress &mac,
                                      uint32_t ethernet_tag) {
    DBRequest req(DBRequest::DB_ENTRY_DELETE);
    req.key.reset(new Layer2RouteKey(peer, vrf_name, mac, ethernet_tag));
    req.data.reset(NULL);
    Layer2TableEnqueue(Agent::GetInstance(), &req);
}

void Layer2AgentRouteTable::Delete(const Peer *peer, const string &vrf_name,
                                   const MacAddress &mac,
                                   uint32_t ethernet_tag) {
    DBRequest req(DBRequest::DB_ENTRY_DELETE);
    req.key.reset(new Layer2RouteKey(peer, vrf_name, mac, ethernet_tag));
    req.data.reset(NULL);
    Layer2TableProcess(Agent::GetInstance(), vrf_name, req);
}

void Layer2AgentRouteTable::AddLayer2BroadcastRoute(const Peer *peer,
                                                    const string &vrf_name,
                                                    const string &vn_name,
                                                    uint32_t label,
                                                    int vxlan_id,
                                                    uint32_t ethernet_tag,
                                                    uint32_t tunnel_type,
                                                    COMPOSITETYPE type,
                                                    ComponentNHKeyList
                                                    &component_nh_key_list) {
    DBRequest nh_req(DBRequest::DB_ENTRY_ADD_CHANGE);
    nh_req.key.reset(new CompositeNHKey(type, false, component_nh_key_list,
                                        vrf_name));
    nh_req.data.reset(new CompositeNHData());

    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    req.key.reset(new Layer2RouteKey(peer, vrf_name,
                                     MacAddress::BroadcastMac(), ethernet_tag));
    req.data.reset(new MulticastRoute(vn_name, label,
                                      ((peer->GetType() == Peer::BGP_PEER) ?
                                      ethernet_tag : vxlan_id), tunnel_type,
                                      nh_req));
    Layer2TableEnqueue(Agent::GetInstance(), &req);
}

void Layer2AgentRouteTable::DeleteBroadcastReq(const Peer *peer,
                                               const string &vrf_name,
                                               uint32_t ethernet_tag) {
    DBRequest req(DBRequest::DB_ENTRY_DELETE);
    req.key.reset(new Layer2RouteKey(peer, vrf_name,
                                     MacAddress::BroadcastMac(), ethernet_tag));
    DBRequest nh_req;
    //Only ethernet tag is required, rest are dummy.
    req.data.reset(new MulticastRoute("", 0, ethernet_tag,
                                      TunnelType::AllType(),
                                      nh_req));
    Layer2TableEnqueue(Agent::GetInstance(), &req);
}

/////////////////////////////////////////////////////////////////////////////
// Layer2RouteEntry methods
/////////////////////////////////////////////////////////////////////////////
string Layer2RouteEntry::ToString() const {
    return mac_.ToString();
}

int Layer2RouteEntry::CompareTo(const Route &rhs) const {
    const Layer2RouteEntry &a = static_cast<const Layer2RouteEntry &>(rhs);

    int cmp = mac_.CompareTo(a.mac_);
    if (cmp != 0)
        return cmp;

    return 0;
}

DBEntryBase::KeyPtr Layer2RouteEntry::GetDBRequestKey() const {
    Layer2RouteKey *key =
        new Layer2RouteKey(Agent::GetInstance()->local_vm_peer(),
                           vrf()->GetName(), mac_);
    return DBEntryBase::KeyPtr(key);
}

void Layer2RouteEntry::SetKey(const DBRequestKey *key) {
    const Layer2RouteKey *k = static_cast<const Layer2RouteKey *>(key);
    SetVrf(Agent::GetInstance()->vrf_table()->FindVrfFromName(k->vrf_name()));
    mac_ = k->GetMac();
}

uint32_t Layer2RouteEntry::GetActiveLabel() const {
    uint32_t label = 0;

    if (is_multicast()) {
        if (TunnelType::ComputeType(TunnelType::AllType()) ==
            (1 << TunnelType::VXLAN)) {
            label = GetActivePath()->vxlan_id();
        } else {
            label = GetActivePath()->label();
        }
    } else {
        label = GetActivePath()->GetActiveLabel();
    }
    return label;
}

AgentPath *Layer2RouteEntry::FindPathUsingKeyData
(const AgentRouteKey *key, const AgentRouteData *data) const {
    const Peer *peer = key->peer();
    const EvpnPeer * evpn_peer = dynamic_cast<const EvpnPeer*>(peer);

    //For non multicast route not programmed via EVPN route table,
    //use Findpath.
    if ((is_multicast() == false) && (evpn_peer == NULL))
        return FindPath(key->peer());

    Route::PathList::const_iterator it;
    for (it = GetPathList().begin(); it != GetPathList().end();
         it++) {
        const AgentPath *path = static_cast<const AgentPath *>(it.operator->());
        if (path->peer() != key->peer())
            continue;

        if (is_multicast()) {
            //Handle multicast peer matching
            if (path->peer()->GetType() != Peer::BGP_PEER)
                return const_cast<AgentPath *>(path);

            //Not the BGP peer we are looking for.
            if (path->tunnel_type() != TunnelType::VXLAN)
                continue;

            const MulticastRoute *multicast_data =
                dynamic_cast<const MulticastRoute *>(data);
            assert(multicast_data != NULL);
            if (multicast_data->vxlan_id() != path->vxlan_id())
                continue;
        } else {
            //Handle mac route added via evpn route.
            const EvpnPath *evpn_path =
                dynamic_cast<const EvpnPath *>(path);
            const EvpnPathData *evpn_data =
                dynamic_cast<const EvpnPathData *>(data);
            assert(evpn_path != NULL);
            assert(evpn_data != NULL);
            if (evpn_path->ethernet_tag() != evpn_data->ethernet_tag())
                continue;
            if (evpn_path->ip_addr() != evpn_data->ip_addr())
                continue;
        }
        return const_cast<AgentPath *>(path);
    }
    return NULL;
}

void Layer2RouteEntry::DeletePathUsingKeyData(const AgentRouteKey *key,
                                              const AgentRouteData *data,
                                              bool force_delete) {
    Route::PathList::iterator it;
    for (it = GetPathList().begin(); it != GetPathList().end();
         it++) {
        AgentPath *path = static_cast<AgentPath *>(it.operator->());
        bool delete_path = false;
        if (key->peer() == path->peer()) {
            if ((path->peer()->GetType() != Peer::BGP_PEER) &&
                (path->peer()->GetType() != Peer::EVPN_PEER)) {
                delete_path = true;
            } else {
                //There are two ways to receive delete of BGP peer path in
                //l2 route.
                //First is via withdraw meesage from control node in which
                //force_delete will be false and vxlan_id will be matched to
                //decide. 
                //Second can be via route walkers where on peer going down or
                //vrf delete, paths from BGP peer should be deleted irrespective
                //of vxlan_id. 
                if (force_delete && (path->peer()->GetType() ==
                                     Peer::BGP_PEER)) {
                    delete_path = true;
                } else if (is_multicast()) {
                    assert(path->peer()->GetType() == Peer::BGP_PEER); 
                    const MulticastRoute *multicast_data =
                        dynamic_cast<const MulticastRoute *>(data);
                    assert(multicast_data != NULL);
                    if (multicast_data->vxlan_id() != path->vxlan_id())
                        continue;
                    delete_path = true;
                } else if (path->peer()->GetType() == Peer::EVPN_PEER) {
                    const EvpnPath *evpn_path =
                        dynamic_cast<const EvpnPath *>(path);
                    const EvpnPathData *evpn_data =
                        dynamic_cast<const EvpnPathData *>(data);
                    assert(evpn_path != NULL);
                    assert(evpn_data != NULL);
                    if (evpn_path->ethernet_tag() != evpn_data->ethernet_tag())
                        continue;
                    if (evpn_path->ip_addr() != evpn_data->ip_addr())
                        continue;
                    delete_path = true;
                }
            }

            if (delete_path) {
                DeletePathInternal(path);
                return;
            }
        }
    }
}

bool Layer2RouteEntry::ReComputePathAdd(AgentPath *path) {
    if (is_multicast()) {
        //evaluate add of path
        return ReComputeMulticastPaths(path, false);
    }
    return false;
}

bool Layer2RouteEntry::ReComputePathDeletion(AgentPath *path) {
    if (is_multicast()) {
        //evaluate delete of path
        return ReComputeMulticastPaths(path, true);
    }
    return false;
}

bool Layer2RouteEntry::ReComputeMulticastPaths(AgentPath *path, bool del) {
    if (path->peer() == NULL) {
        return false;
    }

    //HACK: subnet route uses multicast NH. During IPAM delete
    //subnet discard is deleted. Consider this as delete of all
    //paths. Though this can be handled via multicast module
    //which can also issue delete of all peers, however
    //this is a temporary code as subnet route will not use
    //multicast NH.
    bool delete_all = false;
    if (path->is_subnet_discard() && del) {
        delete_all = true;
    }

    Agent *agent = (static_cast<AgentRouteTable *> (get_table()))->agent();
    if (del && (path->peer() == agent->multicast_peer()))
        return false;

    //Possible paths:
    //EVPN path - can be from multiple peers.
    //Fabric path - from multicast builder
    //Multicast peer
    AgentPath *multicast_peer_path = NULL;
    AgentPath *local_peer_path = NULL;
    AgentPath *evpn_peer_path = NULL;
    AgentPath *fabric_peer_path = NULL;
    AgentPath *tor_peer_path = NULL;

    //Delete path label
    if (del) {
        MplsLabel::DeleteReq(agent, path->label());
    }

    for (Route::PathList::iterator it = GetPathList().begin();
        it != GetPathList().end(); it++) {
        AgentPath *it_path =
            static_cast<AgentPath *>(it.operator->());

        if (delete_all && (it_path->peer() != agent->multicast_peer()))
            continue;

        //Handle deletions
        if (del && (path->peer() == it_path->peer())) {
            continue;
        }

        //Handle Add/Changes
        if (it_path->peer() == agent->local_vm_peer()) {
            local_peer_path = it_path;
        } else if (it_path->peer()->GetType() == Peer::MULTICAST_TOR_PEER) {
            tor_peer_path = it_path;
        } else if (it_path->peer()->GetType() == Peer::BGP_PEER) {
            //Pick up the first peer.
            if (evpn_peer_path == NULL)
                evpn_peer_path = it_path;
        } else if (it_path->peer()->GetType() ==
                   Peer::MULTICAST_FABRIC_TREE_BUILDER) {
            fabric_peer_path = it_path;
        } else if (it_path->peer() == agent->multicast_peer()) {
            multicast_peer_path = it_path;
        }
    }

    //all paths are gone so delete multicast_peer path as well
    if ((local_peer_path == NULL) &&
        (tor_peer_path == NULL) &&
        (evpn_peer_path == NULL) &&
        (fabric_peer_path == NULL)) {
        if (multicast_peer_path != NULL)
            RemovePath(multicast_peer_path);
        return true;
    }

    uint32_t old_fabric_mpls_label = 0;
    if (multicast_peer_path == NULL) {
        multicast_peer_path = new AgentPath(agent->multicast_peer(), NULL);
        InsertPath(multicast_peer_path);
    } else {
        old_fabric_mpls_label = multicast_peer_path->label();
    }

    ComponentNHKeyList component_nh_list;

    if (tor_peer_path) {
        NextHopKey *tor_peer_key =
            static_cast<NextHopKey *>((tor_peer_path->
                        nexthop(agent)->GetDBRequestKey()).release());
        std::auto_ptr<const NextHopKey> key4(tor_peer_key);
        ComponentNHKeyPtr component_nh_data4(new ComponentNHKey(0, key4));
        component_nh_list.push_back(component_nh_data4);
    }
    
    if (evpn_peer_path) {
        NextHopKey *evpn_peer_key =
            static_cast<NextHopKey *>((evpn_peer_path->
                        nexthop(agent)->GetDBRequestKey()).release());
        std::auto_ptr<const NextHopKey> key2(evpn_peer_key);
        ComponentNHKeyPtr component_nh_data2(new ComponentNHKey(0, key2));
        component_nh_list.push_back(component_nh_data2);
    }

    if (fabric_peer_path) {
        NextHopKey *fabric_peer_key =
            static_cast<NextHopKey *>((fabric_peer_path->
                        nexthop(agent)->GetDBRequestKey()).release());
        std::auto_ptr<const NextHopKey> key3(fabric_peer_key);
        ComponentNHKeyPtr component_nh_data3(new ComponentNHKey(0, key3));
        component_nh_list.push_back(component_nh_data3);
    }

    if (local_peer_path) {
        NextHopKey *local_peer_key =
            static_cast<NextHopKey *>((local_peer_path->
                        nexthop(agent)->GetDBRequestKey()).release());
        std::auto_ptr<const NextHopKey> key1(local_peer_key);
        ComponentNHKeyPtr component_nh_data1(new ComponentNHKey(0, key1));
        component_nh_list.push_back(component_nh_data1);
    }

    DBRequest nh_req(DBRequest::DB_ENTRY_ADD_CHANGE);
    nh_req.key.reset(new CompositeNHKey(Composite::L2COMP,
                                        false,
                                        component_nh_list,
                                        vrf()->GetName()));
    nh_req.data.reset(new CompositeNHData());
    agent->nexthop_table()->Process(nh_req);
    NextHop *nh = static_cast<NextHop *>(agent->nexthop_table()->
                                 FindActiveEntry(nh_req.key.get()));
    //NH may not get added if VRF is marked for delete. Route may be in
    //transition of getting deleted, skip NH modification.
    if (!nh) {
        return false;
    }

    bool ret = false;
    if (tor_peer_path) {
        ret = MulticastRoute::CopyPathParameters(agent,
                                                 multicast_peer_path,
                                                 (tor_peer_path ? tor_peer_path->
                                                  dest_vn_name() : ""),
                                                 (tor_peer_path ? tor_peer_path->
                                                  unresolved() : false),
                                                 (tor_peer_path ? tor_peer_path->
                                                  vxlan_id() : 0),
                                                 (fabric_peer_path ? fabric_peer_path->
                                                  label() : 0),
                                                 TunnelType::VxlanType(),
                                                 nh);
    }
    if (local_peer_path && local_peer_path->nexthop(agent)->GetType() !=
        NextHop::DISCARD) {
        ret = MulticastRoute::CopyPathParameters(agent,
                                                 multicast_peer_path,
                                                 (local_peer_path ? local_peer_path->
                                                  dest_vn_name() : ""),
                                                 (local_peer_path ? local_peer_path->
                                                  unresolved() : false),
                                                 (local_peer_path ? local_peer_path->
                                                  vxlan_id() : 0),
                                                 (fabric_peer_path ? fabric_peer_path->
                                                  label() : 0),
                                                 TunnelType::AllType(),
                                                 nh);
    }

    //Bake all MPLS label
    if (fabric_peer_path) {
        if (!ret) {
            ret = MulticastRoute::CopyPathParameters(agent,
                                                     multicast_peer_path,
                                                     (fabric_peer_path ? fabric_peer_path->
                                                      dest_vn_name() : ""),
                                                     (fabric_peer_path ? fabric_peer_path->
                                                      unresolved() : false),
                                                     (fabric_peer_path ? fabric_peer_path->
                                                      vxlan_id() : 0),
                                                     (fabric_peer_path ? fabric_peer_path->
                                                      label() : 0),
                                                     TunnelType::MplsType(),
                                                     nh);
        }
        //Add new label
        MplsLabel::CreateMcastLabelReq(agent,
                                       fabric_peer_path->label(),
                                       Composite::L2COMP,
                                       component_nh_list,
                                       vrf()->GetName());
        //Delete Old label, in case label has changed for same peer.
        if (old_fabric_mpls_label != fabric_peer_path->label()) {
            MplsLabel::DeleteReq(agent, old_fabric_mpls_label);
        }
    }
    if (evpn_peer_path) {
        MplsLabel::CreateMcastLabelReq(agent,
                                       evpn_peer_path->label(),
                                       Composite::L2COMP,
                                       component_nh_list,
                                       vrf()->GetName());
    }
    return ret;
}

/////////////////////////////////////////////////////////////////////////////
// Sandesh related methods
/////////////////////////////////////////////////////////////////////////////
void Layer2RouteReq::HandleRequest() const {
    VrfEntry *vrf = 
        Agent::GetInstance()->vrf_table()->FindVrfFromId(get_vrf_index());
    if (!vrf) {
        ErrorResp *resp = new ErrorResp();
        resp->set_context(context());
        resp->Response();
        return;
    }

    AgentLayer2RtSandesh *sand =
        new AgentLayer2RtSandesh(vrf, context(), "", get_stale());
    sand->DoSandesh();
}

bool Layer2RouteEntry::DBEntrySandesh(Sandesh *sresp, bool stale) const {
    Layer2RouteResp *resp = static_cast<Layer2RouteResp *>(sresp);
    RouteL2SandeshData data;
    data.set_mac(ToString());

    for (Route::PathList::const_iterator it = GetPathList().begin();
         it != GetPathList().end(); it++) {
        const AgentPath *path = static_cast<const AgentPath *>(it.operator->());
        if (path) {
            if (stale && !path->is_stale())
                continue;
            PathSandeshData pdata;
            path->SetSandeshData(pdata);
            if (is_multicast()) {
                pdata.set_vxlan_id(path->vxlan_id());
            }
            const EvpnPath *evpn_path = dynamic_cast<const EvpnPath *>(path);
            if (evpn_path) {
                pdata.set_info(evpn_path->parent());
                pdata.set_ip_address(evpn_path->ip_addr().to_string());
            }
            data.path_list.push_back(pdata);
        }
    }
    std::vector<RouteL2SandeshData> &list = 
        const_cast<std::vector<RouteL2SandeshData>&>(resp->get_route_list());
    list.push_back(data);
    return true;
}
