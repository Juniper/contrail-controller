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

DBTableBase *Layer2AgentRouteTable::CreateTable(DB *db,
                                                const std::string &name) {
    AgentRouteTable *table = new Layer2AgentRouteTable(db, name);
    table->Init();
    return table;
}

Layer2RouteKey *Layer2RouteKey::Clone() const {
    return new Layer2RouteKey(peer_, vrf_name_, dmac_, vm_ip_, plen_,
                              ethernet_tag_);
}

AgentRoute *
Layer2RouteKey::AllocRouteEntry(VrfEntry *vrf, bool is_multicast) const
{
    Layer2RouteEntry * entry = new Layer2RouteEntry(vrf, dmac_, vm_ip_, plen_,
                                                    peer()->GetType(), is_multicast);
    return static_cast<AgentRoute *>(entry);
}

Layer2RouteEntry *Layer2AgentRouteTable::FindRoute(const Agent *agent,
                                                   const string &vrf_name,
                                                   const MacAddress &mac)
{
    VrfEntry *vrf = agent->vrf_table()->FindVrfFromName(vrf_name);
    if (vrf == NULL)
        return NULL;

    Layer2RouteKey key(agent->local_vm_peer(), vrf_name, mac, 0);
    Layer2RouteEntry *route =
        static_cast<Layer2RouteEntry *>
        (static_cast<Layer2AgentRouteTable *>(vrf->
                                              GetLayer2RouteTable())->FindActiveEntry(&key));
    return route;
}

void Layer2AgentRouteTable::AddLocalVmRouteReq(const Peer *peer,
                                               const string &vrf_name,
                                               const MacAddress &mac,
                                               const Ip4Address &vm_ip,
                                               uint32_t ethernet_tag,
                                               uint32_t plen,
                                               LocalVmRoute *data) {
    assert(peer);
    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;

    Layer2RouteKey *key = new Layer2RouteKey(peer, vrf_name, mac, vm_ip, 32,
                                             ethernet_tag);
    req.key.reset(key);
    data->set_tunnel_bmap(TunnelType::AllType());
    req.data.reset(data);
    Layer2TableEnqueue(Agent::GetInstance(), &req);
}

void Layer2AgentRouteTable::AddLocalVmRouteReq(const Peer *peer,
                                               const uuid &intf_uuid,
                                               const string &vn_name,
                                               const string &vrf_name,
                                               uint32_t mpls_label,
                                               uint32_t vxlan_id,
                                               const MacAddress &mac,
                                               const Ip4Address &vm_ip,
                                               uint32_t ethernet_tag,
                                               uint32_t plen) {
    assert(peer);
    VmInterfaceKey intf_key(AgentKey::ADD_DEL_CHANGE, intf_uuid, "");
    SecurityGroupList sg_list;
    PathPreference path_preference;
    LocalVmRoute *data = new LocalVmRoute(intf_key, mpls_label, vxlan_id,
                                          false, vn_name,
                                          InterfaceNHFlags::LAYER2,
                                          sg_list, path_preference,
                                          Ip4Address(0));
    AddLocalVmRouteReq(peer, vrf_name, mac, vm_ip, ethernet_tag, plen, data);
}

void Layer2AgentRouteTable::AddLocalVmRoute(const Peer *peer,
                                            const uuid &intf_uuid,
                                            const string &vn_name,
                                            const string &vrf_name,
                                            uint32_t mpls_label,
                                            uint32_t vxlan_id,
                                            const MacAddress &mac,
                                            const Ip4Address &vm_ip,
                                            uint32_t ethernet_tag,
                                            uint32_t plen) {
    assert(peer);
    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;

    Layer2RouteKey *key = new Layer2RouteKey(peer, vrf_name, mac, vm_ip, 32,
                                             ethernet_tag);
    req.key.reset(key);

    VmInterfaceKey intf_key(AgentKey::ADD_DEL_CHANGE, intf_uuid, "");
    SecurityGroupList sg_list;
    PathPreference path_preference;
    LocalVmRoute *data = new LocalVmRoute(intf_key, mpls_label, vxlan_id,
                                          false, vn_name,
                                          InterfaceNHFlags::LAYER2,
                                          sg_list, path_preference,
                                          Ip4Address(0));
    data->set_tunnel_bmap(TunnelType::AllType());
    req.data.reset(data);
    Layer2TableProcess(Agent::GetInstance(), vrf_name, req);
}

void Layer2AgentRouteTable::AddLayer2BroadcastRoute(const Peer *peer,
                                                    const string &vrf_name,
                                                    const string &vn_name,
                                                    uint32_t label,
                                                    int vxlan_id,
                                                    uint32_t ethernet_tag,
                                                    COMPOSITETYPE type,
                                                    ComponentNHKeyList
                                                    &component_nh_key_list) {
    DBRequest nh_req;
    NextHopKey *nh_key;
    CompositeNHData *nh_data;

    nh_key = new CompositeNHKey(type, false, component_nh_key_list,
                                vrf_name);
    nh_req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    nh_req.key.reset(nh_key);
    nh_data = new CompositeNHData();
    nh_req.data.reset(nh_data);

    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;

    req.key.reset(new Layer2RouteKey(peer, vrf_name, ethernet_tag));
    req.data.reset(new MulticastRoute(vn_name, label,
                                      ((peer->GetType() == Peer::BGP_PEER) ?
                                      ethernet_tag : vxlan_id), nh_req));
    Layer2TableEnqueue(Agent::GetInstance(), &req);
}


void Layer2AgentRouteTable::AddRemoteVmRouteReq(const Peer *peer,
                                                const string &vrf_name,
                                                const MacAddress &mac,
                                                const Ip4Address &vm_ip,
                                                uint32_t ethernet_tag,
                                                uint8_t plen,
                                                AgentRouteData *data) {
    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    Layer2RouteKey *key = new Layer2RouteKey(peer, vrf_name, mac, vm_ip, plen,
                                             ethernet_tag);
    req.key.reset(key);
    req.data.reset(data);

    Layer2TableEnqueue(Agent::GetInstance(), &req);
}

void Layer2AgentRouteTable::DeleteReq(const Peer *peer, const string &vrf_name,
                                      const MacAddress &mac,
                                      uint32_t ethernet_tag,
                                      AgentRouteData *data) {
    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_DELETE;

    Layer2RouteKey *key = new Layer2RouteKey(peer, vrf_name, mac, ethernet_tag);
    req.key.reset(key);
    req.data.reset(data);
    Layer2TableEnqueue(Agent::GetInstance(), &req);
}

void Layer2AgentRouteTable::Delete(const Peer *peer, const string &vrf_name,
                                   uint32_t ethernet_tag,
                                   const MacAddress &mac) {
    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_DELETE;

    Layer2RouteKey *key = new Layer2RouteKey(peer, vrf_name, mac, ethernet_tag);
    req.key.reset(key);
    req.data.reset(NULL);
    Layer2TableProcess(Agent::GetInstance(), vrf_name, req);
}

void Layer2AgentRouteTable::DeleteBroadcastReq(const Peer *peer,
                                               const string &vrf_name,
                                               uint32_t ethernet_tag) {
    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_DELETE;

    Layer2RouteKey *key = new Layer2RouteKey(peer, vrf_name, ethernet_tag);
    req.key.reset(key);
    req.data.reset(NULL);
    Layer2TableEnqueue(Agent::GetInstance(), &req);
}

string Layer2RouteKey::ToString() const {
    return dmac_.ToString();
}

AgentPath *Layer2RouteEntry::FindPathUsingKey(const AgentRouteKey *key) {
    const Peer *peer = key->peer();
    const Layer2RouteKey *l2_rt_key =
        static_cast<const Layer2RouteKey *>(key);
    if (peer->GetType() != Peer::BGP_PEER)
        return FindPath(peer);

    Route::PathList::iterator it;
    for (it = GetPathList().begin(); it != GetPathList().end();
         it++) {
        AgentPath *path = static_cast<AgentPath *>(it.operator->());
        if (path->peer() != key->peer())
            continue;

        if (path->peer()->GetType() != Peer::BGP_PEER)
            return path;

        //Not the BGP peer we are looking for.
        if (path->tunnel_type() != TunnelType::VXLAN)
            return path;

        if (path->vxlan_id() == l2_rt_key->ethernet_tag())
            return path;
    }
    return NULL;
}

void Layer2RouteEntry::DeletePath(const AgentRouteKey *key) {
    const Layer2RouteKey *l2_rt_key =
        static_cast<const Layer2RouteKey *>(key);
    Route::PathList::iterator it;
    for (it = GetPathList().begin(); it != GetPathList().end();
         it++) {
        AgentPath *path = static_cast<AgentPath *>(it.operator->());
        if (key->peer() == path->peer() &&
            ((path->peer()->GetType() != Peer::BGP_PEER) ||
            (path->vxlan_id() == l2_rt_key->ethernet_tag()))) {
            DeletePathInternal(path);
            return;
        }
    }
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

bool Layer2RouteEntry::ReComputeMulticastPaths(AgentPath *path, bool del) {
    if (path->peer() == NULL) {
        return false;
    }

    Agent *agent =
        (static_cast<InetUnicastAgentRouteTable *> (get_table()))->agent();
    std::vector<AgentPath *> delete_paths;
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

    //Delete path label
    if (del) {
        MplsLabel::DeleteReq(path->label());
    }

    for (Route::PathList::iterator it = GetPathList().begin();
        it != GetPathList().end(); it++) {
        AgentPath *it_path =
            static_cast<AgentPath *>(it.operator->());
        //Handle deletions
        if (del && (path->peer() == it_path->peer())) {
            continue;
        }

        //Handle Add/Changes
        if (it_path->peer() == agent->local_vm_peer()) {
            local_peer_path = it_path;
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
        (evpn_peer_path == NULL) &&
        (fabric_peer_path == NULL) &&
        (multicast_peer_path != NULL)) {
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

    bool ret = MulticastRoute::CopyPathParameters(agent,
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
                                      false,
                                      nh);

    //Bake all MPLS label
    if (fabric_peer_path) {
        //Add new label
        MplsLabel::CreateMcastLabelReq(fabric_peer_path->label(),
                                       Composite::L2COMP,
                                       component_nh_list,
                                       vrf()->GetName());
        //Delete Old label, in case label has changed for same peer.
        if (old_fabric_mpls_label != fabric_peer_path->label()) {
            MplsLabel::DeleteReq(old_fabric_mpls_label);
        }
    }
    if (evpn_peer_path) {
        MplsLabel::CreateMcastLabelReq(evpn_peer_path->label(),
                                       Composite::L2COMP,
                                       component_nh_list,
                                       vrf()->GetName());
    }
    return ret;
}

string Layer2RouteEntry::ToString() const {
    return mac_.ToString();
}

int Layer2RouteEntry::CompareTo(const Route &rhs) const {
    const Layer2RouteEntry &a = static_cast<const Layer2RouteEntry &>(rhs);

    return mac_.CompareTo(a.mac_);
};

DBEntryBase::KeyPtr Layer2RouteEntry::GetDBRequestKey() const {
    Layer2RouteKey *key =
        new Layer2RouteKey(Agent::GetInstance()->local_vm_peer(),
                           vrf()->GetName(), mac_, 0);
    return DBEntryBase::KeyPtr(key);
}

void Layer2RouteEntry::SetKey(const DBRequestKey *key) {
    const Layer2RouteKey *k = static_cast<const Layer2RouteKey *>(key);
    SetVrf(Agent::GetInstance()->vrf_table()->FindVrfFromName(k->vrf_name()));
    mac_ = k->GetMac();
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
            data.path_list.push_back(pdata);
        }
    }
    std::vector<RouteL2SandeshData> &list = 
        const_cast<std::vector<RouteL2SandeshData>&>(resp->get_route_list());
    list.push_back(data);
    return true;
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

void Layer2RouteReq::HandleRequest() const {
    VrfEntry *vrf = 
        Agent::GetInstance()->vrf_table()->FindVrfFromId(get_vrf_index());
    if (!vrf) {
        ErrorResp *resp = new ErrorResp();
        resp->set_context(context());
        resp->Response();
        return;
    }

    AgentLayer2RtSandesh *sand = new AgentLayer2RtSandesh(vrf, 
                                                          context(), "", get_stale());
    sand->DoSandesh();
}
