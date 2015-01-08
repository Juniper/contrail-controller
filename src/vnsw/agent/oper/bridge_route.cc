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
static void BridgeTableEnqueue(Agent *agent, DBRequest *req) {
    AgentRouteTable *table = agent->fabric_l2_unicast_table();
    if (table) {
        table->Enqueue(req);
    }
}

static void BridgeTableProcess(Agent *agent, const string &vrf_name,
                               DBRequest &req) {
    AgentRouteTable *table =
        agent->vrf_table()->GetBridgeRouteTable(vrf_name);
    if (table) {
        table->Process(req);
    }
}

/////////////////////////////////////////////////////////////////////////////
// BridgeRouteKey methods
/////////////////////////////////////////////////////////////////////////////
string BridgeRouteKey::ToString() const {
    return dmac_.ToString();
}

BridgeRouteKey *BridgeRouteKey::Clone() const {
    return new BridgeRouteKey(peer_, vrf_name_, dmac_, ip_addr_,
                              ethernet_tag_);
}

AgentRoute *
BridgeRouteKey::AllocRouteEntry(VrfEntry *vrf, bool is_multicast) const
{
    BridgeRouteEntry *entry = new BridgeRouteEntry(vrf, dmac_, ip_addr_,
                                                   peer()->GetType(),
                                                   is_multicast);
    return static_cast<AgentRoute *>(entry);
}

/////////////////////////////////////////////////////////////////////////////
// BridgeAgentRouteTable methods
/////////////////////////////////////////////////////////////////////////////
DBTableBase *BridgeAgentRouteTable::CreateTable(DB *db,
                                                const std::string &name) {
    AgentRouteTable *table = new BridgeAgentRouteTable(db, name);
    table->Init();
    return table;
}

BridgeRouteEntry *BridgeAgentRouteTable::FindRoute(const Agent *agent,
                                                   const string &vrf_name,
                                                   const MacAddress &mac,
                                                   const IpAddress &ip_addr) {
    VrfEntry *vrf = agent->vrf_table()->FindVrfFromName(vrf_name);
    if (vrf == NULL)
        return NULL;

    BridgeRouteKey key(agent->local_vm_peer(), vrf_name, mac, ip_addr, 0);
    BridgeAgentRouteTable *table = static_cast<BridgeAgentRouteTable *>
        (vrf->GetBridgeRouteTable());
    BridgeRouteEntry *route =
        static_cast<BridgeRouteEntry *>(table->FindActiveEntry(&key));
    return route;
}

/////////////////////////////////////////////////////////////////////////////
// BridgeAgentRouteTable utility methods to add/delete routes
/////////////////////////////////////////////////////////////////////////////
void BridgeAgentRouteTable::AddLocalVmRouteReq(const Peer *peer,
                                               const string &vrf_name,
                                               const MacAddress &mac,
                                               const IpAddress &ip_addr,
                                               uint32_t ethernet_tag,
                                               LocalVmRoute *data) {
    assert(peer);
    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);

    req.key.reset(new BridgeRouteKey(peer, vrf_name, mac, ip_addr,
                                     ethernet_tag));
    data->set_tunnel_bmap(TunnelType::AllType());
    req.data.reset(data);
    BridgeTableEnqueue(Agent::GetInstance(), &req);
}

static LocalVmRoute *MakeData(const VmInterface *intf) {
    SecurityGroupList sg_list;
    PathPreference path_preference;
    VmInterfaceKey intf_key(AgentKey::ADD_DEL_CHANGE, intf->GetUuid(), "");
    LocalVmRoute *data = new LocalVmRoute(intf_key, intf->l2_label(),
                                          intf->vxlan_id(), false,
                                          intf->vn()->GetName(),
                                          InterfaceNHFlags::BRIDGE,
                                          sg_list, path_preference,
                                          IpAddress());
    data->set_tunnel_bmap(TunnelType::AllType());
    return data;
}

void BridgeAgentRouteTable::AddLocalVmRoute(const Peer *peer,
                                            const VmInterface *intf) {
    Agent *agent = static_cast<AgentDBTable *>(intf->get_table())->agent();
    assert(peer);
    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);

    // Add route for IPv4 address
    if (intf->ip_addr().is_unspecified() == false) {
        req.key.reset(new BridgeRouteKey(peer, intf->vrf()->GetName(),
                                         MacAddress::FromString(intf->vm_mac()),
                                         IpAddress(intf->ip_addr()), 0));
        req.data.reset(MakeData(intf));
        BridgeTableProcess(agent, intf->vrf()->GetName(), req);
    }

    // Add route for IPv6 address
    if (intf->ip6_addr().is_unspecified() == false) {
        req.key.reset(new BridgeRouteKey(peer, intf->vrf()->GetName(),
                                         MacAddress::FromString(intf->vm_mac()),
                                         IpAddress(intf->ip6_addr()), 0));
        req.data.reset(MakeData(intf));
        BridgeTableProcess(agent, intf->vrf()->GetName(), req);
    }
}

void BridgeAgentRouteTable::DelLocalVmRoute(const Peer *peer,
                                            const string &vrf_name,
                                            const VmInterface *intf,
                                            const Ip4Address &old_v4_addr,
                                            const Ip6Address &old_v6_addr) {
    Agent *agent = static_cast<AgentDBTable *>(intf->get_table())->agent();
    assert(peer);
    DBRequest req(DBRequest::DB_ENTRY_DELETE);

    // Delete route for IPv4 address
    if (old_v4_addr.is_unspecified() == false) {
        req.key.reset(new BridgeRouteKey(peer, vrf_name,
                                         MacAddress::FromString(intf->vm_mac()),
                                         IpAddress(old_v4_addr), 0));
        req.data.reset(NULL);
        BridgeTableProcess(agent, vrf_name, req);
    }

    // Delete route for IPv6 address
    if (old_v6_addr.is_unspecified() == false) {
        req.key.reset(new BridgeRouteKey(peer, vrf_name,
                                         MacAddress::FromString(intf->vm_mac()),
                                         IpAddress(old_v6_addr), 0));
        req.data.reset(NULL);
        BridgeTableProcess(agent, vrf_name, req);
    }
}

void BridgeAgentRouteTable::AddBridgeReceiveRoute(const Peer *peer,
                                                  const string &vrf_name,
                                                  const MacAddress &mac,
                                                  const IpAddress &ip_addr,
                                                  const string &vn_name,
                                                  const string &interface,
                                                  bool policy) {
    Agent *agent = Agent::GetInstance();
    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    req.key.reset(new BridgeRouteKey(peer, vrf_name, mac, ip_addr, 0));

    PacketInterfaceKey intf_key(nil_uuid(), agent->pkt_interface_name());
    req.data.reset(new HostRoute(intf_key, vn_name));

    BridgeTableEnqueue(agent, &req);
}

void BridgeAgentRouteTable::AddRemoteVmRouteReq(const Peer *peer,
                                                const string &vrf_name,
                                                const MacAddress &mac,
                                                const IpAddress &ip_addr,
                                                uint32_t ethernet_tag,
                                                AgentRouteData *data) {
    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    req.key.reset(new BridgeRouteKey(peer, vrf_name, mac, ip_addr,
                                     ethernet_tag));
    req.data.reset(data);

    BridgeTableEnqueue(Agent::GetInstance(), &req);
}

void BridgeAgentRouteTable::DeleteReq(const Peer *peer, const string &vrf_name,
                                      const MacAddress &mac,
                                      const IpAddress &ip_addr,
                                      uint32_t ethernet_tag) {
    DBRequest req(DBRequest::DB_ENTRY_DELETE);
    req.key.reset(new BridgeRouteKey(peer, vrf_name, mac, ip_addr,
                                     ethernet_tag));
    req.data.reset(NULL);
    BridgeTableEnqueue(Agent::GetInstance(), &req);
}

void BridgeAgentRouteTable::Delete(const Peer *peer, const string &vrf_name,
                                   const MacAddress &mac,
                                   const IpAddress &ip_addr,
                                   uint32_t ethernet_tag) {
    DBRequest req(DBRequest::DB_ENTRY_DELETE);
    req.key.reset(new BridgeRouteKey(peer, vrf_name, mac, ip_addr,
                                     ethernet_tag));
    req.data.reset(NULL);
    BridgeTableProcess(Agent::GetInstance(), vrf_name, req);
}

void BridgeAgentRouteTable::AddBridgeBroadcastRoute(const Peer *peer,
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
    req.key.reset(new BridgeRouteKey(peer, vrf_name, MacAddress::BroadcastMac(),
                                     IpAddress(), ethernet_tag));
    req.data.reset(new MulticastRoute(vn_name, label,
                                      ((peer->GetType() == Peer::BGP_PEER) ?
                                      ethernet_tag : vxlan_id), tunnel_type,
                                      nh_req));
    BridgeTableEnqueue(Agent::GetInstance(), &req);
}

void BridgeAgentRouteTable::DeleteBroadcastReq(const Peer *peer,
                                               const string &vrf_name,
                                               uint32_t ethernet_tag) {
    DBRequest req(DBRequest::DB_ENTRY_DELETE);
    req.key.reset(new BridgeRouteKey(peer, vrf_name, MacAddress::BroadcastMac(),
                                     IpAddress(), ethernet_tag));
    req.data.reset(NULL);
    BridgeTableEnqueue(Agent::GetInstance(), &req);
}

/////////////////////////////////////////////////////////////////////////////
// BridgeRouteEntry methods
/////////////////////////////////////////////////////////////////////////////
string BridgeRouteEntry::ToString() const {
    return mac_.ToString();
}

int BridgeRouteEntry::CompareTo(const Route &rhs) const {
    const BridgeRouteEntry &a = static_cast<const BridgeRouteEntry &>(rhs);

    int cmp = mac_.CompareTo(a.mac_);
    if (cmp != 0)
        return cmp;

    if (ip_addr_ < a.ip_addr_)
        return -1;

    if (ip_addr_ > a.ip_addr_)
        return 1;

    return 0;
}

DBEntryBase::KeyPtr BridgeRouteEntry::GetDBRequestKey() const {
    BridgeRouteKey *key =
        new BridgeRouteKey(Agent::GetInstance()->local_vm_peer(),
                           vrf()->GetName(), mac_, ip_addr_, 0);
    return DBEntryBase::KeyPtr(key);
}

void BridgeRouteEntry::SetKey(const DBRequestKey *key) {
    const BridgeRouteKey *k = static_cast<const BridgeRouteKey *>(key);
    SetVrf(Agent::GetInstance()->vrf_table()->FindVrfFromName(k->vrf_name()));
    mac_ = k->GetMac();
    ip_addr_ = k->ip_addr();
}

const uint32_t BridgeRouteEntry::GetVmIpPlen() const {
    if (ip_addr_.is_v4())
        return 32;
    if (ip_addr_.is_v6())
        return 128;
    assert(0);
}

uint32_t BridgeRouteEntry::GetActiveLabel() const {
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

AgentPath *BridgeRouteEntry::FindPathUsingKey(const AgentRouteKey *key) {
    const Peer *peer = key->peer();
    const BridgeRouteKey *l2_rt_key =
        static_cast<const BridgeRouteKey *>(key);
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

void BridgeRouteEntry::DeletePath(const AgentRouteKey *key, bool force_delete) {
    const BridgeRouteKey *l2_rt_key =
        static_cast<const BridgeRouteKey *>(key);
    Route::PathList::iterator it;
    for (it = GetPathList().begin(); it != GetPathList().end();
         it++) {
        AgentPath *path = static_cast<AgentPath *>(it.operator->());
        bool delete_path = false;
        if (key->peer() == path->peer()) {
            if (path->peer()->GetType() != Peer::BGP_PEER) {
                delete_path = true;
            } else if (force_delete || (path->vxlan_id() ==
                                      l2_rt_key->ethernet_tag())) {
                //There are two ways to receive delete of BGP peer path in
                //l2 route.
                //First is via withdraw meesage from control node in which
                //force_delete will be false and vxlan_id will be matched to
                //decide. 
                //Second can be via route walkers where on peer going down or
                //vrf delete, paths from BGP peer should be deleted irrespective
                //of vxlan_id. 
                delete_path = true;
            }

            if (delete_path) {
                DeletePathInternal(path);
                return;
            }
        }
    }
}

bool BridgeRouteEntry::ReComputePathAdd(AgentPath *path) {
    if (is_multicast()) {
        //evaluate add of path
        return ReComputeMulticastPaths(path, false);
    }
    return false;
}

bool BridgeRouteEntry::ReComputePathDeletion(AgentPath *path) {
    if (is_multicast()) {
        //evaluate delete of path
        return ReComputeMulticastPaths(path, true);
    }
    return false;
}

bool BridgeRouteEntry::ReComputeMulticastPaths(AgentPath *path, bool del) {
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
void BridgeRouteReq::HandleRequest() const {
    VrfEntry *vrf = 
        Agent::GetInstance()->vrf_table()->FindVrfFromId(get_vrf_index());
    if (!vrf) {
        ErrorResp *resp = new ErrorResp();
        resp->set_context(context());
        resp->Response();
        return;
    }

    AgentBridgeRtSandesh *sand =
        new AgentBridgeRtSandesh(vrf, context(), "", get_stale());
    sand->DoSandesh();
}

bool BridgeRouteEntry::DBEntrySandesh(Sandesh *sresp, bool stale) const {
    BridgeRouteResp *resp = static_cast<BridgeRouteResp *>(sresp);
    RouteL2SandeshData data;
    data.set_mac(ToString());
    data.set_ip_addr(ip_addr_.to_string());

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
