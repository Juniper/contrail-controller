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
#include <oper/physical_device.h>
#include <controller/controller_export.h>
#include <controller/controller_peer.h>
#include <controller/controller_route_path.h>
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
    return new BridgeRouteKey(peer(), vrf_name_, dmac_);
}

AgentRoute *
BridgeRouteKey::AllocRouteEntry(VrfEntry *vrf, bool is_multicast) const
{
    BridgeRouteEntry *entry = new BridgeRouteEntry(vrf, dmac_,
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

BridgeRouteEntry *BridgeAgentRouteTable::FindRoute(const MacAddress &mac) {
    BridgeRouteEntry entry(vrf_entry(), mac, Peer::LOCAL_PEER, false);
    return static_cast<BridgeRouteEntry *>(FindActiveEntry(&entry));
}

BridgeRouteEntry *BridgeAgentRouteTable::FindRouteNoLock(const MacAddress &mac){
    BridgeRouteEntry entry(vrf_entry(), mac, Peer::LOCAL_PEER, false);
    return static_cast<BridgeRouteEntry *>(FindActiveEntryNoLock(&entry));
}

BridgeRouteEntry *BridgeAgentRouteTable::FindRoute(const MacAddress &mac,
                                                   Peer::Type peer) {
    BridgeRouteEntry entry(vrf_entry(), mac, peer, false);
    return static_cast<BridgeRouteEntry *>(FindActiveEntry(&entry));
}

/////////////////////////////////////////////////////////////////////////////
// BridgeAgentRouteTable utility methods to add/delete routes
/////////////////////////////////////////////////////////////////////////////
void BridgeAgentRouteTable::AddBridgeReceiveRoute(const Peer *peer,
                                                  const string &vrf_name,
                                                  const MacAddress &mac,
                                                  const string &vn_name,
                                                  const string &interface,
                                                  bool policy) {
    Agent *agent = Agent::GetInstance();
    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    req.key.reset(new BridgeRouteKey(peer, vrf_name, mac, 0));

    PacketInterfaceKey intf_key(nil_uuid(), agent->pkt_interface_name());
    req.data.reset(new HostRoute(intf_key, vn_name));

    BridgeTableEnqueue(agent, &req);
}

void BridgeAgentRouteTable::AddBridgeReceiveRouteReq(const Peer *peer,
                                                     const string &vrf_name,
                                                     uint32_t vxlan_id,
                                                     const MacAddress &mac,
                                                     const string &vn_name) {
    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    req.key.reset(new BridgeRouteKey(peer, vrf_name, mac, vxlan_id));
    req.data.reset(new L2ReceiveRoute(vn_name, vxlan_id, 0, PathPreference(),
                                      peer->sequence_number()));
    agent()->fabric_l2_unicast_table()->Enqueue(&req);
}

void BridgeAgentRouteTable::AddBridgeReceiveRoute(const Peer *peer,
                                                  const string &vrf_name,
                                                  uint32_t vxlan_id,
                                                  const MacAddress &mac,
                                                  const string &vn_name) {
    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    req.key.reset(new BridgeRouteKey(peer, vrf_name, mac, vxlan_id));
    req.data.reset(new L2ReceiveRoute(vn_name, vxlan_id, 0, PathPreference(),
                                      peer->sequence_number()));
    Process(req);
}

void BridgeAgentRouteTable::AddBridgeRoute(const AgentRoute *rt) {
    const EvpnRouteEntry *evpn_rt =
        static_cast<const EvpnRouteEntry *>(rt);
    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    req.key.reset(new BridgeRouteKey(agent()->evpn_peer(),
                                     evpn_rt->vrf()->GetName(),
                                     evpn_rt->mac(), 0));
    req.data.reset(new EvpnDerivedPathData(evpn_rt));
    BridgeTableProcess(agent(), vrf_name(), req);
}


void BridgeAgentRouteTable::AddMacVmBindingRoute(const Peer *peer,
                                                 const std::string &vrf_name,
                                                 const MacAddress &mac,
                                                 const VmInterface *vm_intf) {
    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    req.key.reset(new BridgeRouteKey(peer, vrf_name, mac, 0));
    req.data.reset(new MacVmBindingPathData(vm_intf));
    BridgeTableProcess(agent(), vrf_name, req);
}

void BridgeAgentRouteTable::DeleteMacVmBindingRoute(const Peer *peer,
                                                    const std::string &vrf_name,
                                                    const MacAddress &mac,
                                                    const VmInterface *vm_intf) {
    DBRequest req(DBRequest::DB_ENTRY_DELETE);
    req.key.reset(new BridgeRouteKey(peer, vrf_name, mac, 0));
    req.data.reset(new MacVmBindingPathData(vm_intf));
    BridgeTableProcess(agent(), vrf_name, req);
}

void BridgeAgentRouteTable::DeleteBridgeRoute(const AgentRoute *rt) {
    const EvpnRouteEntry *evpn_rt =
        static_cast<const EvpnRouteEntry *>(rt);
    DBRequest req(DBRequest::DB_ENTRY_DELETE);
    req.key.reset(new BridgeRouteKey(agent()->evpn_peer(),
                                     evpn_rt->vrf()->GetName(),
                                     evpn_rt->mac(), 0));
    req.data.reset(new EvpnDerivedPathData(evpn_rt));
    BridgeTableProcess(Agent::GetInstance(), evpn_rt->vrf()->GetName(), req);
}

void BridgeAgentRouteTable::DeleteReq(const Peer *peer, const string &vrf_name,
                                      const MacAddress &mac,
                                      uint32_t ethernet_tag,
                                      AgentRouteData *data) {
    DBRequest req(DBRequest::DB_ENTRY_DELETE);
    req.key.reset(new BridgeRouteKey(peer, vrf_name, mac, ethernet_tag));
    req.data.reset(data);
    BridgeTableEnqueue(Agent::GetInstance(), &req);
}

void BridgeAgentRouteTable::Delete(const Peer *peer, const string &vrf_name,
                                   const MacAddress &mac,
                                   uint32_t ethernet_tag) {
    DBRequest req(DBRequest::DB_ENTRY_DELETE);
    req.key.reset(new BridgeRouteKey(peer, vrf_name, mac, ethernet_tag));
    req.data.reset(NULL);
    BridgeTableProcess(Agent::GetInstance(), vrf_name, req);
}

AgentRouteData *BridgeAgentRouteTable::BuildNonBgpPeerData(const string &vrf_name,
                                                           const std::string &vn_name,
                                                           uint32_t label,
                                                           int vxlan_id,
                                                           uint32_t tunnel_type,
                                                           Composite::Type type,
                                                           ComponentNHKeyList
                                                           &component_nh_key_list,
                                                           bool pbb_nh,
                                                           bool learning_enabled) {
    DBRequest nh_req(DBRequest::DB_ENTRY_ADD_CHANGE);
    nh_req.key.reset(new CompositeNHKey(type, false, component_nh_key_list,
                                        vrf_name));
    nh_req.data.reset(new CompositeNHData(pbb_nh, learning_enabled, false));
    return (new MulticastRoute(vn_name, label,
                               vxlan_id, tunnel_type,
                               nh_req, type, 0));
}

AgentRouteData *BridgeAgentRouteTable::BuildBgpPeerData(const Peer *peer,
                                                        const string &vrf_name,
                                                        const std::string &vn_name,
                                                        uint32_t label,
                                                        int vxlan_id,
                                                        uint32_t ethernet_tag,
                                                        uint32_t tunnel_type,
                                                        Composite::Type type,
                                                        ComponentNHKeyList
                                                        &component_nh_key_list,
                                                        bool pbb_nh,
                                                        bool learning_enabled) {
    const BgpPeer *bgp_peer = dynamic_cast<const BgpPeer *>(peer);
    assert(bgp_peer != NULL);
    DBRequest nh_req(DBRequest::DB_ENTRY_ADD_CHANGE);
    nh_req.key.reset(new CompositeNHKey(type, false, component_nh_key_list,
                                        vrf_name));
    nh_req.data.reset(new CompositeNHData(pbb_nh, learning_enabled, false));
    return (new MulticastRoute(vn_name, label, ethernet_tag, tunnel_type,
                               nh_req, type, bgp_peer->sequence_number()));
}

void BridgeAgentRouteTable::AddBridgeBroadcastRoute(const Peer *peer,
                                                    const string &vrf_name,
                                                    uint32_t ethernet_tag,
                                                    AgentRouteData *data) {

    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    req.key.reset(new BridgeRouteKey(peer, vrf_name,
                                     MacAddress::BroadcastMac(), ethernet_tag));
    req.data.reset(data);
    BridgeTableEnqueue(Agent::GetInstance(), &req);
}

void BridgeAgentRouteTable::DeleteBroadcastReq(const Peer *peer,
                                               const string &vrf_name,
                                               uint32_t ethernet_tag,
                                               COMPOSITETYPE type) {
    DBRequest req(DBRequest::DB_ENTRY_DELETE);
    req.key.reset(new BridgeRouteKey(peer, vrf_name,
                                     MacAddress::BroadcastMac(), ethernet_tag));
    DBRequest nh_req;
    //For same BGP peer type comp type helps in identifying if its a delete
    //for TOR or EVPN path.
    //Only ethernet tag is required, rest are dummy.
    const BgpPeer *bgp_peer = dynamic_cast<const BgpPeer *>(peer);
    if (bgp_peer) {
        req.data.reset(new MulticastRoute("", 0,
                                     ethernet_tag, TunnelType::AllType(),
                                     nh_req, type,
                                     bgp_peer->sequence_number()));
    } else {
        req.data.reset(new MulticastRoute("", 0, ethernet_tag,
                                          TunnelType::AllType(),
                                          nh_req, type, 0));
    }

    BridgeTableEnqueue(Agent::GetInstance(), &req);
}

const VmInterface *BridgeAgentRouteTable::FindVmFromDhcpBinding
(const MacAddress &mac) {
    const BridgeRouteEntry *l2_rt = FindRoute(mac);
    if (l2_rt == NULL)
        return NULL;

    const MacVmBindingPath *dhcp_path = l2_rt->FindMacVmBindingPath();
    if (dhcp_path == NULL)
        return NULL;
    return dhcp_path->vm_interface();
}

/////////////////////////////////////////////////////////////////////////////
// BridgeRouteEntry methods
/////////////////////////////////////////////////////////////////////////////
const std::string BridgeRouteEntry::GetAddressString() const {
    //For multicast use the same tree as of 255.255.255.255
    if (is_multicast()) {
        return "255.255.255.255";
    }
    return ToString();
}

string BridgeRouteEntry::ToString() const {
    return mac_.ToString();
}

int BridgeRouteEntry::CompareTo(const Route &rhs) const {
    const BridgeRouteEntry &a = static_cast<const BridgeRouteEntry &>(rhs);

    return mac_.CompareTo(a.mac_);
}

DBEntryBase::KeyPtr BridgeRouteEntry::GetDBRequestKey() const {
    BridgeRouteKey *key =
        new BridgeRouteKey(Agent::GetInstance()->local_vm_peer(),
                           vrf()->GetName(), mac_);
    return DBEntryBase::KeyPtr(key);
}

void BridgeRouteEntry::SetKey(const DBRequestKey *key) {
    const BridgeRouteKey *k = static_cast<const BridgeRouteKey *>(key);
    SetVrf(Agent::GetInstance()->vrf_table()->FindVrfFromName(k->vrf_name()));
    mac_ = k->GetMac();
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

AgentPath *BridgeRouteEntry::FindPathUsingKeyData
(const AgentRouteKey *key, const AgentRouteData *data) const {
    const Peer *peer = key->peer();
    if (is_multicast())
        return FindMulticastPathUsingKeyData(key, data);
    const EvpnPeer *evpn_peer = dynamic_cast<const EvpnPeer*>(peer);
    if (evpn_peer != NULL)
        return FindEvpnPathUsingKeyData(key, data);

    return FindPath(peer);
}

AgentPath *BridgeRouteEntry::FindMulticastPathUsingKeyData
(const AgentRouteKey *key, const AgentRouteData *data) const {
    assert(is_multicast());

    Route::PathList::const_iterator it;
    for (it = GetPathList().begin(); it != GetPathList().end();
         it++) {
        const AgentPath *path = static_cast<const AgentPath *>(it.operator->());
        if (path->peer() != key->peer())
            continue;

        //Handle multicast peer matching,
        //In case of BGP peer also match VXLAN id.
        if (path->peer()->GetType() != Peer::BGP_PEER)
            return const_cast<AgentPath *>(path);

        const MulticastRoute *multicast_data =
            dynamic_cast<const MulticastRoute *>(data);
        assert(multicast_data != NULL);
        if (multicast_data->vxlan_id() != path->vxlan_id())
            continue;

        //In multicast from same peer, TOR and EVPN comp can
        //come. These should not overlap and be installed as
        //different path.
        const CompositeNH *cnh = dynamic_cast<CompositeNH *>(path->nexthop());
        if ((cnh != NULL) &&
            (multicast_data->comp_nh_type() != cnh->composite_nh_type()))
            continue;

        return const_cast<AgentPath *>(path);
    }
    return NULL;
}

AgentPath *BridgeRouteEntry::FindEvpnPathUsingKeyData
(const AgentRouteKey *key, const AgentRouteData *data) const {
    const Peer *peer = key->peer();
    const EvpnPeer *evpn_peer = dynamic_cast<const EvpnPeer*>(peer);
    assert(evpn_peer != NULL);

    Route::PathList::const_iterator it;
    for (it = GetPathList().begin(); it != GetPathList().end(); it++) {
        const AgentPath *path = static_cast<const AgentPath *>(it.operator->());
        if (path->peer() != key->peer())
            continue;

        //Handle mac route added via evpn route.
        const EvpnDerivedPath *evpn_path =
            dynamic_cast<const EvpnDerivedPath *>(path);
        const EvpnDerivedPathData *evpn_data =
            dynamic_cast<const EvpnDerivedPathData *>(data);
        assert(evpn_path != NULL);
        assert(evpn_data != NULL);
        if (evpn_path->ethernet_tag() != evpn_data->ethernet_tag())
            continue;
        if (evpn_path->ip_addr() != evpn_data->ip_addr())
            continue;
        return const_cast<AgentPath *>(path);
    }
    return NULL;
}

void BridgeRouteEntry::DeletePathUsingKeyData(const AgentRouteKey *key,
                                              const AgentRouteData *data,
                                              bool force_delete) {
    Agent *agent = (static_cast<AgentRouteTable *> (get_table()))->agent();
    std::list<AgentPath *> to_be_deleted_path_list;
    Route::PathList::iterator it;
    //If peer in key is deleted, set force_delete to true.
    if (key->peer()->IsDeleted() || key->peer()->SkipAddChangeRequest()) {
        force_delete = true;
    }

    for (it = GetPathList().begin(); it != GetPathList().end(); ) {
        AgentPath *path = static_cast<AgentPath *>(it.operator->());
        bool delete_path = false;

        // Current path can be deleted below. Incremnt the iterator and dont
        // use it again below
        it++;

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
                    //BGP peer path uses channel peer unicast sequence number.
                    //If it is stale, then delete same.
                    if (data->CanDeletePath(agent, path, this) == false) {
                        delete_path = false;
                        continue;
                    }
                    //Not a stale so check for multicast data
                    const MulticastRoute *multicast_data =
                        dynamic_cast<const MulticastRoute *>(data);
                    if (multicast_data &&
                        (multicast_data->vxlan_id() != path->vxlan_id())) {
                        continue;
                    }
                    delete_path = true;
                } else if (path->peer()->GetType() == Peer::EVPN_PEER) {
                    const EvpnDerivedPath *evpn_path =
                        dynamic_cast<const EvpnDerivedPath *>(path);
                    assert(evpn_path != NULL);
                    const EvpnDerivedPathData *evpn_data =
                        dynamic_cast<const EvpnDerivedPathData *>(data);
                    //Operate on this path only if data is EvpnDerivedPathData.
                    //Rest data type need to be ignored.
                    if (evpn_data == NULL)
                        continue;
                    if (evpn_path->ethernet_tag() != evpn_data->ethernet_tag()) {
                        continue;
                    }
                    if (evpn_path->ip_addr() != evpn_data->ip_addr()) {
                        continue;
                    }
                    delete_path = true;
                }
            }

            // In case of multicast routes, BGP can give multiple paths.
            // So, continue looking for other paths for this peer
            if (delete_path) {
                to_be_deleted_path_list.push_back(path);
            }
        }
    }

    std::list<AgentPath *>::iterator to_be_deleted_path_list_it =
        to_be_deleted_path_list.begin();
    while (to_be_deleted_path_list_it != to_be_deleted_path_list.end()) {
        AgentPath *path = static_cast<AgentPath *>(*to_be_deleted_path_list_it);
        DeletePathInternal(path);
        to_be_deleted_path_list_it++;
    }
}

const MacVmBindingPath *BridgeRouteEntry::FindMacVmBindingPath() const {
    Agent *agent = (static_cast<AgentRouteTable *> (get_table()))->agent();
    return dynamic_cast<MacVmBindingPath*>(FindPath(agent->mac_vm_binding_peer()));
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

void BridgeRouteEntry::HandleMulticastLabel(const Agent *agent,
                                            AgentPath *path,
                                            const AgentPath *local_peer_path,
                                            const AgentPath *local_vm_peer_path,
                                            bool del, uint32_t *evpn_label) {
    *evpn_label = MplsTable::kInvalidLabel;

    //EVPN label is present in two paths:
    // local_vm_peer(courtesy: vmi) or local_peer(courtesy: vn)
    // Irrespective of delete/add operation if one of them is present and is not
    // the affected path, then extract the label from same.
    // By default pick it from available path (local or local_vm).
    switch (path->peer()->GetType()) {
    case Peer::LOCAL_VM_PEER:
        //Use local_peer path for label
        if (local_peer_path) {
            *evpn_label = local_peer_path->label();
            assert(*evpn_label != MplsTable::kInvalidLabel);
        }
        break;
    case Peer::LOCAL_PEER:
        //Use local_peer path for label
        if (local_vm_peer_path) {
            *evpn_label = local_vm_peer_path->label();
            assert(*evpn_label != MplsTable::kInvalidLabel);
        }
        break;
    default:
        if (local_vm_peer_path) {
            *evpn_label = local_vm_peer_path->label();
            assert(*evpn_label != MplsTable::kInvalidLabel);
        } else if (local_peer_path) {
            *evpn_label = local_peer_path->label();
            assert(*evpn_label != MplsTable::kInvalidLabel);
        }
        break;
    }

    //Delete path evpn label if path is local_peer or local_vm_peer.
    //Delete fabric label if path is multicast_fabric_tree
    if (del) {
        bool delete_label = false;
        // On deletion of fabric path delete fabric label.
        // Other type of label is evpn mcast label.
        // EVPN label is deleted when both local peer and local_vm_peer path are
        // gone.
        if (path->peer()->GetType() == Peer::MULTICAST_FABRIC_TREE_BUILDER)
            delete_label = true;
        else if ((path->peer() == agent->local_vm_peer()) ||
                 (path->peer() == agent->local_peer())) {
            if (local_peer_path == NULL &&
                local_vm_peer_path == NULL)
                delete_label = true;
        }
        if (delete_label) {
            //Reset evpn label to invalid as it is freed
            if (*evpn_label == path->label()) {
                *evpn_label = MplsTable::kInvalidLabel;
            }
            agent->mpls_table()->FreeLabel(path->label());
            //Reset path label to invalid as it is freed
            path->set_label(MplsTable::kInvalidLabel);
        }
        return;
    }

    // Currently other than evpn label no other multicast path requires dynamic
    // allocation so return.
    if ((path != local_peer_path) && (path != local_vm_peer_path))
        return;

    // Path already has label, return.
    if (path->label() != MplsTable::kInvalidLabel) {
        if (*evpn_label ==  MplsTable::kInvalidLabel) {
            *evpn_label = path->label();
        }
        return;
    }

    // If this is the first time i.e. local_peer has come with no local_vm_peer
    // and vice versa then allocate label.
    // If its not then we should have valid evpn label calculated above.
    if (*evpn_label == MplsTable::kInvalidLabel) {
        // XOR use - we shud never reach here when both are NULL or set.
        // Only one should be present.
        assert((local_vm_peer_path != NULL) ^ (local_peer_path != NULL));
        // Allocate route label with discard nh, nh in label gets updated
        // after composite-nh is created.
        DiscardNHKey key;
        *evpn_label = agent->mpls_table()->CreateRouteLabel(*evpn_label, &key,
                                                            vrf()->GetName(),
                                                            ToString());
    }
    assert(*evpn_label != MplsTable::kInvalidLabel);
    path->set_label(*evpn_label);
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
    AgentPath *local_vm_peer_path = NULL;
    AgentPath *evpn_peer_path = NULL;
    AgentPath *fabric_peer_path = NULL;
    AgentPath *tor_peer_path = NULL;
    AgentPath *local_peer_path = NULL;
    bool tor_path = false;
    uint32_t old_fabric_mpls_label = 0;

    const CompositeNH *cnh =
         static_cast<const CompositeNH *>(path->nexthop());
    if (cnh && (cnh->composite_nh_type() == Composite::TOR)) {
        tor_path = true;
    }

    for (Route::PathList::iterator it = GetPathList().begin();
        it != GetPathList().end(); it++) {
        AgentPath *it_path =
            static_cast<AgentPath *>(it.operator->());

        if (delete_all && (it_path->peer() != agent->multicast_peer()))
            continue;

        //Handle deletions
        if (del && (path->peer() == it_path->peer())) {
            if (path->peer()->GetType() != Peer::BGP_PEER)
                continue;

            //Dive into comp NH type for BGP peer
            const CompositeNH *it_path_comp_nh =
                static_cast<const CompositeNH *>(it_path->nexthop());
            const CompositeNH *comp_nh =
                static_cast<const CompositeNH *>(path->nexthop());
            if (it_path_comp_nh->composite_nh_type() ==
                comp_nh->composite_nh_type())
                continue;
        }

        //Handle Add/Changes
        if (it_path->peer() == agent->local_vm_peer()) {
            local_vm_peer_path = it_path;
        } else if (it_path->peer()->GetType() == Peer::BGP_PEER) {
            const CompositeNH *bgp_comp_nh =
                static_cast<const CompositeNH *>(it_path->nexthop());    
            //Its a TOR NH
            if (bgp_comp_nh && (bgp_comp_nh->composite_nh_type() ==
                                Composite::TOR)) {
                if (tor_peer_path == NULL)
                    tor_peer_path = it_path;
            }
            //Pick up the first peer.
            if (bgp_comp_nh && (bgp_comp_nh->composite_nh_type() ==
                                Composite::EVPN)) {
                if (evpn_peer_path == NULL)
                    evpn_peer_path = it_path;
            }
        } else if (it_path->peer()->GetType() ==
                   Peer::MULTICAST_FABRIC_TREE_BUILDER) {
            fabric_peer_path = it_path;
            old_fabric_mpls_label = fabric_peer_path->label();
        } else if (it_path->peer() == agent->multicast_peer()) {
            multicast_peer_path = it_path;
        } else if (it_path->peer() == agent->local_peer()) {
            local_peer_path = it_path;
        }
    }

    if (tor_path) {
        if ((del && (tor_peer_path == NULL)) || !del) {
            HandleDeviceMastershipUpdate(path, del);
        }
    }

    uint32_t evpn_label = MplsTable::kInvalidLabel;
    HandleMulticastLabel(agent, path, local_peer_path, local_vm_peer_path, del,
                         &evpn_label);

    //all paths are gone so delete multicast_peer path as well
    if ((local_vm_peer_path == NULL) &&
        (tor_peer_path == NULL) &&
        (evpn_peer_path == NULL) &&
        (fabric_peer_path == NULL)) {
        if (multicast_peer_path != NULL) {
            if ((evpn_label != MplsTable::kInvalidLabel) && (local_peer_path)) {
                // Make evpn label point to discard-nh as composite-nh gets
                // deleted.
                DiscardNHKey key;
                agent->mpls_table()->CreateRouteLabel(evpn_label, &key,
                                                      vrf()->GetName(),
                                                      ToString());
            }
            RemovePath(multicast_peer_path);
        }
        return true;
    }

    bool learning_enabled = false;
    bool pbb_nh = false;
    if (multicast_peer_path == NULL) {
        multicast_peer_path = new AgentPath(agent->multicast_peer(), NULL);
        InsertPath(multicast_peer_path);
    }

    ComponentNHKeyList component_nh_list;

    if (tor_peer_path) {
        NextHopKey *tor_peer_key =
            static_cast<NextHopKey *>((tor_peer_path->
                        ComputeNextHop(agent)->GetDBRequestKey()).release());
        std::auto_ptr<const NextHopKey> key4(tor_peer_key);
        ComponentNHKeyPtr component_nh_data4(new ComponentNHKey(0, key4));
        component_nh_list.push_back(component_nh_data4);
    }
    
    if (evpn_peer_path) {
        NextHopKey *evpn_peer_key =
            static_cast<NextHopKey *>((evpn_peer_path->
                        ComputeNextHop(agent)->GetDBRequestKey()).release());
        std::auto_ptr<const NextHopKey> key2(evpn_peer_key);
        ComponentNHKeyPtr component_nh_data2(new ComponentNHKey(0, key2));
        component_nh_list.push_back(component_nh_data2);
    }

    if (fabric_peer_path) {
        NextHopKey *fabric_peer_key =
            static_cast<NextHopKey *>((fabric_peer_path->
                        ComputeNextHop(agent)->GetDBRequestKey()).release());
        std::auto_ptr<const NextHopKey> key3(fabric_peer_key);
        ComponentNHKeyPtr component_nh_data3(new ComponentNHKey(0, key3));
        component_nh_list.push_back(component_nh_data3);
    }

    if (local_vm_peer_path) {
        NextHopKey *local_vm_peer_key =
            static_cast<NextHopKey *>((local_vm_peer_path->
                                       ComputeNextHop(agent)->GetDBRequestKey()).release());
        std::auto_ptr<const NextHopKey> key4(local_vm_peer_key);
        ComponentNHKeyPtr component_nh_data4(new ComponentNHKey(0, key4));
        component_nh_list.push_back(component_nh_data4);

        const CompositeNH *cnh = dynamic_cast<const CompositeNH *>(
                local_vm_peer_path->ComputeNextHop(agent));
        if (cnh && cnh->learning_enabled() == true) {
            learning_enabled = true;
        }
        if (cnh && cnh->pbb_nh() == true) {
            pbb_nh = true;
        }
    }

    DBRequest nh_req(DBRequest::DB_ENTRY_ADD_CHANGE);
    nh_req.key.reset(new CompositeNHKey(Composite::L2COMP,
                                        false,
                                        component_nh_list,
                                        vrf()->GetName()));
    nh_req.data.reset(new CompositeNHData(pbb_nh, learning_enabled,
                                          vrf()->layer2_control_word()));
    agent->nexthop_table()->Process(nh_req);
    NextHop *nh = static_cast<NextHop *>(agent->nexthop_table()->
                                 FindActiveEntry(nh_req.key.get()));
    //NH may not get added if VRF is marked for delete. Route may be in
    //transition of getting deleted, skip NH modification.
    if (!nh) {
        return false;
    }

    NextHopKey *key = static_cast<NextHopKey *>(nh_req.key.get());
    //Bake all MPLS label
    if (fabric_peer_path) {
        //Add new label
        agent->mpls_table()->CreateRouteLabel(fabric_peer_path->label(), key,
                                              vrf()->GetName(), ToString());
        //Delete Old label, in case label has changed for same peer.
        if (old_fabric_mpls_label != fabric_peer_path->label()) {
            agent->mpls_table()->FreeLabel(old_fabric_mpls_label);
        }
    }

    // Rebake label with whatever comp NH has been calculated.
    if (evpn_label != MplsTable::kInvalidLabel) {
        evpn_label = agent->mpls_table()->CreateRouteLabel(evpn_label, key,
                                              vrf()->GetName(), ToString());
    }

    bool ret = false;
    //Identify parameters to be passed to populate multicast_peer path and
    //based on peer priorites for each attribute.
    std::string dest_vn_name = "";
    bool unresolved = false;
    uint32_t vxlan_id = 0;
    uint32_t label = 0;
    uint32_t tunnel_bmap = TunnelType::AllType();

    //Select based on priority of path peer.
    if (local_vm_peer_path) {
        dest_vn_name = local_vm_peer_path->dest_vn_name();
        unresolved = local_vm_peer_path->unresolved();
        vxlan_id = local_vm_peer_path->vxlan_id();
        tunnel_bmap = TunnelType::AllType();
        label = local_vm_peer_path->label();
    } else if (tor_peer_path) {
        dest_vn_name = tor_peer_path->dest_vn_name();
        unresolved = tor_peer_path->unresolved();
        vxlan_id = tor_peer_path->vxlan_id();
        tunnel_bmap = TunnelType::VxlanType();
        label = tor_peer_path->label();
    } else if (fabric_peer_path) {
        dest_vn_name = fabric_peer_path->dest_vn_name();
        unresolved = fabric_peer_path->unresolved();
        vxlan_id = fabric_peer_path->vxlan_id();
        tunnel_bmap = TunnelType::MplsType();
        label = fabric_peer_path->label();
    } else if (evpn_peer_path) {
        dest_vn_name = evpn_peer_path->dest_vn_name();
        unresolved = evpn_peer_path->unresolved();
        vxlan_id = evpn_peer_path->vxlan_id();
        tunnel_bmap = TunnelType::VxlanType();
        label = evpn_peer_path->label();
    }

    //Mpls label selection needs to be overridden by fabric label
    //if fabric peer is present
    if (fabric_peer_path) {
        label = fabric_peer_path->label();
    }

    ret = MulticastRoute::CopyPathParameters(agent,
                                             multicast_peer_path,
                                             dest_vn_name,
                                             unresolved,
                                             vxlan_id,
                                             label,
                                             tunnel_bmap,
                                             nh, this);

    return ret;
}

void BridgeRouteEntry::HandleDeviceMastershipUpdate(AgentPath *path, bool del) {
    Agent *agent = Agent::GetInstance();
    PhysicalDeviceTable *table = agent->physical_device_table();
    CompositeNH *nh = static_cast<CompositeNH *>(path->nexthop());
    ComponentNHList clist = nh->component_nh_list();
    table->UpdateDeviceMastership(vrf()->GetName(), clist, del);
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

    AgentSandeshPtr sand(new AgentBridgeRtSandesh(vrf, context(), "",
                                                  get_stale()));
    sand->DoSandesh(sand);
}

AgentSandeshPtr BridgeAgentRouteTable::GetAgentSandesh
(const AgentSandeshArguments *args, const std::string &context) {
    return AgentSandeshPtr(new AgentBridgeRtSandesh(vrf_entry(), context, "",
                                                    false));
}

bool BridgeRouteEntry::DBEntrySandesh(Sandesh *sresp, bool stale) const {
    BridgeRouteResp *resp = static_cast<BridgeRouteResp *>(sresp);
    RouteL2SandeshData data;
    data.set_mac(ToString());

    for (Route::PathList::const_iterator it = GetPathList().begin();
         it != GetPathList().end(); it++) {
        const AgentPath *path = static_cast<const AgentPath *>(it.operator->());
        if (path) {
            PathSandeshData pdata;
            path->SetSandeshData(pdata);
            if (is_multicast()) {
                pdata.set_vxlan_id(path->vxlan_id());
            }
            const EvpnDerivedPath *evpn_path = dynamic_cast<const EvpnDerivedPath *>(path);
            if (evpn_path) {
                pdata.set_info(evpn_path->parent());
            }
            data.path_list.push_back(pdata);
        }
    }
    std::vector<RouteL2SandeshData> &list =
        const_cast<std::vector<RouteL2SandeshData>&>(resp->get_route_list());
    list.push_back(data);
    return true;
}

//Supporting deprecated layer2 requests
void Layer2RouteReq::HandleRequest() const {
    VrfEntry *vrf =
        Agent::GetInstance()->vrf_table()->FindVrfFromId(get_vrf_index());
    if (!vrf) {
        ErrorResp *resp = new ErrorResp();
        resp->set_context(context());
        resp->Response();
        return;
    }

    AgentSandeshPtr sand(new AgentLayer2RtSandesh(vrf, context(), "",
                                                  get_stale()));
    sand->DoSandesh(sand);
}
