/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/bgp_evpn.h"

#include <boost/foreach.hpp>

#include <algorithm>
#include <string>

#include "base/set_util.h"
#include "base/task_annotations.h"
#include "bgp/bgp_log.h"
#include "bgp/bgp_multicast.h"
#include "bgp/bgp_peer_types.h"
#include "bgp/bgp_server.h"
#include "bgp/bgp_update.h"
#include "bgp/ermvpn/ermvpn_route.h"
#include "bgp/ermvpn/ermvpn_table.h"
#include "bgp/evpn/evpn_table.h"
#include "bgp/extended-community/multicast_flags.h"
#include "bgp/routing-instance/routing_instance.h"
#include "bgp/routing-instance/routing_instance_analytics_types.h"
#include "bgp/routing-instance/routing_instance_log.h"

using std::pair;
using std::set;
using std::sort;
using std::string;
using std::vector;

// A global MVPN state for a given <S.G> within a EvpnProjectManager.
EvpnState::EvpnState(const SG &sg, StatesMap *states, EvpnManager *manager) :
        sg_(sg), global_ermvpn_tree_rt_(NULL), states_(states),
        manager_(manager) {
    refcount_ = 0;
}

EvpnState::~EvpnState() {
    assert(!global_ermvpn_tree_rt_);
    EVPN_TRACE(EvpnStateCreate, sg_.source.to_string(), sg_.group.to_string());
}

const ErmVpnTable *EvpnState::table() const {
    return manager_ ? manager_->ermvpn_table() : NULL;
}

class EvpnManager::DeleteActor : public LifetimeActor {
public:
    explicit DeleteActor(EvpnManager *evpn_manager)
        : LifetimeActor(evpn_manager->server()->lifetime_manager()),
          evpn_manager_(evpn_manager) {
    }
    virtual ~DeleteActor() {
    }

    virtual bool MayDelete() const {
        CHECK_CONCURRENCY("bgp::Config");
        return evpn_manager_->MayDelete();
    }

    virtual void Shutdown() {
        evpn_manager_->Shutdown();
    }

    virtual void Destroy() {
        evpn_manager_->table_->DestroyEvpnManager();
    }

private:
    EvpnManager *evpn_manager_;
};

//
// Constructor for EvpnMcastNode. The type indicates whether this is a local
// or remote EvpnMcastNode.
//
EvpnMcastNode::EvpnMcastNode(EvpnManagerPartition *partition,
    EvpnRoute *route, uint8_t type)
    : partition_(partition),
      state_(NULL),
      route_(route),
      type_(type),
      label_(0),
      edge_replication_not_supported_(false),
      assisted_replication_supported_(false),
      assisted_replication_leaf_(false) {
    UpdateAttributes(route);
}

EvpnMcastNode::EvpnMcastNode(EvpnManagerPartition *partition,
    EvpnRoute *route, uint8_t type, EvpnStatePtr state)
    : partition_(partition),
      state_(state),
      route_(route),
      type_(type),
      label_(0),
      edge_replication_not_supported_(false),
      assisted_replication_supported_(false),
      assisted_replication_leaf_(false) {
    if (route)
        UpdateAttributes(route);
}

//
// Destructor for EvpnMcastNode.
//
EvpnMcastNode::~EvpnMcastNode() {
    set_state(NULL);
}

//
// Update the label and attributes for a EvpnMcastNode.
// Return true if either of them changed.
//
bool EvpnMcastNode::UpdateAttributes(EvpnRoute *route) {
    bool changed = false;
    const BgpPath *path = route->BestPath();
    if (path->GetLabel() != label_) {
        label_ = path->GetLabel();
        changed = true;
    }
    if (path->GetAttr() != attr_) {
        attr_ = path->GetAttr();
        changed = true;
    }

    const PmsiTunnel *pmsi_tunnel = attr_->pmsi_tunnel();
    uint8_t ar_type = 0;
    bool edge_replication_not_supported = false;
    if (pmsi_tunnel) {
        ar_type = pmsi_tunnel->tunnel_flags() &
                  PmsiTunnelSpec::AssistedReplicationType;
        if ((pmsi_tunnel->tunnel_flags() &
             PmsiTunnelSpec::EdgeReplicationSupported) == 0) {
            edge_replication_not_supported = true;
        }
        if (replicator_address_ != pmsi_tunnel->identifier()) {
            replicator_address_ = pmsi_tunnel->identifier();
            changed = true;
        }
    }

    if (edge_replication_not_supported != edge_replication_not_supported_) {
        edge_replication_not_supported_ = edge_replication_not_supported;
        changed = true;
    }

    bool assisted_replication_supported = false;
    if (ar_type == PmsiTunnelSpec::ARReplicator)
        assisted_replication_supported = true;
    if (assisted_replication_supported != assisted_replication_supported_) {
        assisted_replication_supported_ = assisted_replication_supported;
        changed = true;
    }

    bool assisted_replication_leaf = false;
    if (ar_type == PmsiTunnelSpec::ARLeaf)
        assisted_replication_leaf = true;
    if (assisted_replication_leaf != assisted_replication_leaf_) {
        assisted_replication_leaf_ = assisted_replication_leaf;
        changed = true;
    }

    if (address_ != path->GetAttr()->nexthop().to_v4()) {
        address_ = path->GetAttr()->nexthop().to_v4();
        changed = true;
    }

    return changed;
}

//
// Constructor for EvpnLocalMcastNode.
//
// Add an Inclusive Multicast route corresponding to Broadcast MAC route.
//
// Need to Notify the Broadcast MAC route so that the table Export method
// can run and build the OList. OList is not built till EvpnLocalMcastNode
// has been created.
//
EvpnLocalMcastNode::EvpnLocalMcastNode(EvpnManagerPartition *partition,
    EvpnRoute *route)
    : EvpnMcastNode(partition, route, EvpnMcastNode::LocalNode),
      inclusive_mcast_route_(NULL) {
    AddInclusiveMulticastRoute();
    DBTablePartition *tbl_partition = partition_->GetTablePartition();
    tbl_partition->Notify(route_);
}

EvpnLocalMcastNode::EvpnLocalMcastNode(EvpnManagerPartition *partition,
    EvpnRoute *route, EvpnStatePtr state)
    : EvpnMcastNode(partition, route, EvpnMcastNode::LocalNode, state),
      inclusive_mcast_route_(NULL) {
    if (!route)
        return;
    AddInclusiveMulticastRoute();
    DBTablePartition *tbl_partition = partition_->GetTablePartition();
    tbl_partition->Notify(route_);
}

//
// Destructor for EvpnLocalMcastNode.
//
EvpnLocalMcastNode::~EvpnLocalMcastNode() {
    DeleteInclusiveMulticastRoute();
}

//
// Add Inclusive Multicast route for this EvpnLocalMcastNode.
// The attributes are based on the Broadcast MAC route.
//
void EvpnLocalMcastNode::AddInclusiveMulticastRoute() {
    // No need to create IMET route if group is specified
    if (!route_->GetPrefix().group().is_unspecified())
        return;
    assert(!inclusive_mcast_route_);
    if (label_ == 0)
        return;

    // Construct the prefix and route key.
    // Build the RD using the TOR IP address, not the TOR agent IP address.
    // This ensures that the MAC broadcast route from the primary and backup
    // TOR Agents results in the same Inclusive Multicast prefix.
    const EvpnPrefix &mac_prefix = route_->GetPrefix();
    RouteDistinguisher rd(
        address_.to_ulong(), mac_prefix.route_distinguisher().GetVrfId());
    EvpnPrefix prefix(rd, mac_prefix.tag(), address_);
    EvpnRoute rt_key(prefix);

    // Find or create the route.
    DBTablePartition *tbl_partition = partition_->GetTablePartition();
    EvpnRoute *route = static_cast<EvpnRoute *>(tbl_partition->Find(&rt_key));
    if (!route) {
        route = new EvpnRoute(prefix);
        tbl_partition->Add(route);
    } else {
        route->ClearDelete();
    }
    // Add MulticastFlags community to specify that it supports SMET route
    ExtCommunity::ExtCommunityList mcastFlags;
    mcastFlags.push_back(MulticastFlags().GetExtCommunity());
    ExtCommunityPtr ext_community = partition_->server()->extcomm_db()->
            ReplaceMFlagsAndLocate(attr_->ext_community(), mcastFlags);
    attr_ = partition_->server()->attr_db()->ReplaceExtCommunityAndLocate(
        attr_.get(), ext_community);

    // Add a path with source BgpPath::Local and the peer address as path_id.
    uint32_t path_id = mac_prefix.route_distinguisher().GetAddress();
    BgpPath *path = new BgpPath(path_id, BgpPath::Local, attr_, 0, label_);
    route->InsertPath(path);
    inclusive_mcast_route_ = route;
    tbl_partition->Notify(inclusive_mcast_route_);
    BGP_LOG_ROUTE(partition_->table(), static_cast<IPeer *>(NULL),
        route, "Insert new Local path");
}

//
// Delete Inclusive Multicast route for this EvpnLocalMcastNode.
//
void EvpnLocalMcastNode::DeleteInclusiveMulticastRoute() {
    if (!inclusive_mcast_route_)
        return;

    const EvpnPrefix &mac_prefix = route_->GetPrefix();
    uint32_t path_id = mac_prefix.route_distinguisher().GetAddress();
    DBTablePartition *tbl_partition = partition_->GetTablePartition();
    inclusive_mcast_route_->RemovePath(BgpPath::Local, path_id);
    BGP_LOG_ROUTE(partition_->table(), static_cast<IPeer *>(NULL),
        inclusive_mcast_route_, "Delete Local path");

    if (!inclusive_mcast_route_->HasPaths()) {
        tbl_partition->Delete(inclusive_mcast_route_);
    } else {
        tbl_partition->Notify(inclusive_mcast_route_);
    }
    inclusive_mcast_route_ = NULL;
}

//
// Handle update of EvpnLocalMcastNode.
//
// We delete and add the Inclusive Multicast route to ensure that all the
// attributes are updated. An in-place update is not always possible since
// the vRouter address is part of the key for the Inclusive Multicast route.
//
void EvpnLocalMcastNode::TriggerUpdate() {
    DeleteInclusiveMulticastRoute();
    AddInclusiveMulticastRoute();
}

//
// Construct an UpdateInfo with the RibOutAttr that needs to be advertised to
// the IPeer for the EvpnRoute associated with this EvpnLocalMcastNode. This
// is used the Export method of the EvpnTable. It is expected that the caller
// fills in the target RibPeerSet in the UpdateInfo.
//
// The main functionality here is to build a per-IPeer BgpOList from the list
// of EvpnRemoteMcastNodes.
//
UpdateInfo *EvpnLocalMcastNode::GetUpdateInfo(EvpnRoute *route) {
    CHECK_CONCURRENCY("db::DBTable");

    // Nothing to send for a leaf as it already knows the replicator-address.
    if (assisted_replication_leaf_)
        return NULL;

    const RoutingInstance *rti = partition_->table()->routing_instance();
    bool pbb_evpn_enable = rti->virtual_network_pbb_evpn_enable();
    uint32_t local_ethernet_tag = route_->GetPrefix().tag();

    EvpnState::SG sg = EvpnState::SG(route->GetPrefix().source(),
                                     route->GetPrefix().group());
    // Go through list of EvpnRemoteMcastNodes and build the BgpOList.
    BgpOListSpec olist_spec(BgpAttribute::OList);
    if (partition_->remote_mcast_node_list()->count(sg)) {
        set<EvpnMcastNode*> nodes = (*partition_->remote_mcast_node_list())[sg];
        BOOST_FOREACH(EvpnMcastNode *node, nodes) {
            if (node->address() == address_)
                continue;
            if (node->assisted_replication_leaf())
                continue;
            if (!edge_replication_not_supported_ &&
                !node->edge_replication_not_supported())
                continue;
            uint32_t remote_ethernet_tag = node->route()->GetPrefix().tag();

            if (pbb_evpn_enable && remote_ethernet_tag &&
                (local_ethernet_tag != remote_ethernet_tag))
                continue;

            const ExtCommunity *extcomm = node->attr()->ext_community();
            BgpOListElem elem(node->address(), node->label(),
                    extcomm ? extcomm->GetTunnelEncap() : vector<string>());
            olist_spec.elements.push_back(elem);
        }
    }

    // Go through list of leaf EvpnMcastNodes and build the leaf BgpOList.
    BgpOListSpec leaf_olist_spec(BgpAttribute::LeafOList);
    if (assisted_replication_supported_) {
        if (partition_->leaf_node_list()->count(sg)) {
            set<EvpnMcastNode*> nodes = (*partition_->leaf_node_list())[sg];
            BOOST_FOREACH(EvpnMcastNode *node, nodes) {
                if (node->replicator_address() != address_)
                    continue;

                const ExtCommunity *extcomm = node->attr()->ext_community();
                BgpOListElem elem(node->address(), node->label(),
                        extcomm ? extcomm->GetTunnelEncap() : vector<string>());
                leaf_olist_spec.elements.push_back(elem);
            }
        }
    }

    // Bail if both BgpOLists are empty.
    if (olist_spec.elements.empty() && leaf_olist_spec.elements.empty())
        return NULL;

    // Add BgpOList and leaf BgpOList to RibOutAttr for broadcast MAC route.
    BgpAttrDB *attr_db = partition_->server()->attr_db();
    BgpAttrPtr attr = attr_db->ReplaceOListAndLocate(attr_.get(), &olist_spec);
    attr = attr_db->ReplaceLeafOListAndLocate(attr.get(), &leaf_olist_spec);

    UpdateInfo *uinfo = new UpdateInfo;
    uinfo->roattr =
        RibOutAttr(partition_->table(), route_, attr.get(), 0, false, true);
    return uinfo;
}

//
// Constructor for EvpnRemoteMcastNode.
//
EvpnRemoteMcastNode::EvpnRemoteMcastNode(EvpnManagerPartition *partition,
    EvpnRoute *route)
    : EvpnMcastNode(partition, route, EvpnMcastNode::RemoteNode) {
}

EvpnRemoteMcastNode::EvpnRemoteMcastNode(EvpnManagerPartition *partition,
    EvpnRoute *route, EvpnStatePtr state)
    : EvpnMcastNode(partition, route, EvpnMcastNode::RemoteNode, state) {
}

//
// Destructor for EvpnRemoteMcastNode.
//
EvpnRemoteMcastNode::~EvpnRemoteMcastNode() {
}

//
// Handle update of EvpnRemoteMcastNode.
//
void EvpnRemoteMcastNode::TriggerUpdate() {
}

//
// Constructor for EvpnSegment.
//
EvpnSegment::EvpnSegment(EvpnManager *manager, const EthernetSegmentId &esi)
  : evpn_manager_(manager),
    esi_(esi),
    esi_ad_route_(NULL),
    single_active_(true),
    route_lists_(DB::PartitionCount()) {
}

//
// Destructor for EvpnSegment.
//
EvpnSegment::~EvpnSegment() {
    assert(!esi_ad_route_);
    assert(pe_list_.empty());
}

//
// Add the given MAC route as a dependent of this EvpnSegment.
//
void EvpnSegment::AddMacRoute(size_t part_id, EvpnRoute *route) {
    pair<RouteList::iterator, bool> ret = route_lists_[part_id].insert(route);
    assert(ret.second);
}

//
// Delete the given MAC route as a dependent of this EvpnSegment.
// Trigger deletion of the EvpnSegment if there are no dependent
// routes in the partition.
//
void EvpnSegment::DeleteMacRoute(size_t part_id, EvpnRoute *route) {
    size_t count = route_lists_[part_id].erase(route);
    assert(count == 1);
    if (route_lists_[part_id].empty())
        evpn_manager_->TriggerSegmentDelete(this);
}

//
// Trigger an update of all dependent MAC routes for this EvpnSegment.
// Note that the bgp::EvpnSegment task is mutually exclusive with the
// db::DBTable task.
//
void EvpnSegment::TriggerMacRouteUpdate() {
    CHECK_CONCURRENCY("bgp::EvpnSegment");

    for (size_t part_id = 0; part_id < route_lists_.size(); ++part_id) {
        EvpnManagerPartition *partition = evpn_manager_->GetPartition(part_id);
        BOOST_FOREACH(EvpnRoute *route, route_lists_[part_id]) {
            partition->TriggerMacRouteUpdate(route);
        }
    }
}

//
// Update the PE list for this EvpnSegment. This should be called when
// the AutoDisocvery route is updated.
// Return true if there's a change in the PE list i.e. if an entry is
// added, deleted or updated.
//
bool EvpnSegment::UpdatePeList() {
    CHECK_CONCURRENCY("bgp::EvpnSegment");

    // Mark all entries as invalid.
    for (RemotePeList::iterator it = pe_list_.begin();
        it != pe_list_.end(); ++it) {
        it->esi_valid = false;
    }

    // Go through all paths for the route and refresh/update existing entries
    // or add new ones as necessary. Remember that there was a change in the
    // list if any element changed or is added/deleted.
    bool changed = false;
    for (Route::PathList::const_iterator path_it =
         esi_ad_route_->GetPathList().begin();
         path_it != esi_ad_route_->GetPathList().end(); ++path_it) {
        const BgpPath *path =
            static_cast<const BgpPath *>(path_it.operator->());
        const BgpAttr *attr = path->GetAttr();

        // Skip non-VXLAN paths for now.
        const ExtCommunity *extcomm = attr->ext_community();
        if (!extcomm || !extcomm->ContainsTunnelEncapVxlan())
            continue;

        // Go through existing pe list and try to find the RemotePe.
        bool found = false;
        RemotePe remote_pe(path);
        for (RemotePeList::iterator it = pe_list_.begin();
            it != pe_list_.end(); ++it) {
            // Skip if the nexthop doesn't match.
            if (it->attr->nexthop() != remote_pe.attr->nexthop())
                continue;

            // We're done if there are multiple paths for the same remote PE
            // e.g. when a pair of route reflectors is being used.
            if (it->esi_valid) {
                found = true;
                break;
            }

            // Check if we have a match.
            if (*it == remote_pe) {
                it->esi_valid = true;
                found = true;
                break;
            }
        }

        // Add a new entry to the pe list if we didn't find the RemotePe.
        if (!found) {
            pe_list_.push_back(remote_pe);
            changed = true;
        }
    }

    // Erase invalid entries from the list.
    for (RemotePeList::iterator it = pe_list_.begin(), next = it;
        it != pe_list_.end(); it = next) {
        ++next;
        if (!it->esi_valid) {
            pe_list_.erase(it);
            changed = true;
        }
    }

    // Update the single active status of the EvpnSegment itself.
    single_active_ = false;
    for (RemotePeList::iterator it = pe_list_.begin();
        it != pe_list_.end(); ++it) {
        if (it->single_active) {
            single_active_ = true;
            break;
        }
    }

    return changed;
}

//
// Return true if it's safe to delete this EvpnSegment.
//
bool EvpnSegment::MayDelete() const {
    CHECK_CONCURRENCY("bgp::EvpnSegment");

    // Bail if we have state set on the per-ESI AD route.
    if (esi_ad_route_)
        return false;

    // Bail if the dependent route list for any partition is not empty.
    for (size_t part_id = 0; part_id < route_lists_.size(); ++part_id) {
        if (!route_lists_[part_id].empty())
            return false;
    }

    return true;
}

//
// Constructor for EvpnSegment::RemotePe.
//
EvpnSegment::RemotePe::RemotePe(const BgpPath *path)
  : esi_valid(true),
    single_active(path->GetAttr()->evpn_single_active()),
    peer(path->GetPeer()),
    attr(path->GetAttr()),
    flags(path->GetFlags()),
    src(path->GetSource()) {
}

//
// Equality operator for EvpnSegment::RemotePe.
// Do not compare esi_valid and single_active fields since they are
// derived.
//
bool EvpnSegment::RemotePe::operator==(const RemotePe &rhs) const {
    if (peer != rhs.peer)
        return false;
    if (attr != rhs.attr)
        return false;
    if (flags != rhs.flags)
        return false;
    if (src != rhs.src)
        return false;
    return true;
}

//
// Constructor for EvpnMacState.
//
EvpnMacState::EvpnMacState(EvpnManager *evpn_manager, EvpnRoute *route)
  : evpn_manager_(evpn_manager), route_(route), segment_(NULL) {
}

//
// Destructor for EvpnMacState.
//
EvpnMacState::~EvpnMacState() {
    assert(segment_ == NULL);
    assert(aliased_path_list_.empty());
}

//
// Add the BgpPath specified by the iterator to the aliased path list.
// Also inserts the BgpPath to the BgpRoute.
//
void EvpnMacState::AddAliasedPath(AliasedPathList::const_iterator it) {
    BgpPath *path = *it;
    const IPeer *peer = path->GetPeer();
    aliased_path_list_.insert(path);
    route_->InsertPath(path);
    BGP_LOG_STR(BgpMessage, SandeshLevel::SYS_DEBUG, BGP_LOG_FLAG_TRACE,
        "Added aliased path " << route_->ToString() <<
        " peer " << (peer ? peer->ToString() : "None") <<
        " nexthop " << path->GetAttr()->nexthop().to_string() <<
        " label " << path->GetLabel() <<
        " in table " << evpn_manager_->table()->name());
}

//
// Delete the BgpPath specified by the iterator from the aliased path list.
// Also deletes the BgpPath from the BgpRoute.
//
void EvpnMacState::DeleteAliasedPath(AliasedPathList::const_iterator it) {
    BgpPath *path = *it;
    const IPeer *peer = path->GetPeer();
    BGP_LOG_STR(BgpMessage, SandeshLevel::SYS_DEBUG, BGP_LOG_FLAG_TRACE,
        "Deleted aliased path " << route_->ToString() <<
        " peer " << (peer ? peer->ToString() : "None") <<
        " nexthop " << path->GetAttr()->nexthop().to_string() <<
        " label " << path->GetLabel() <<
        " in table " << evpn_manager_->table()->name());
    route_->DeletePath(path);
    aliased_path_list_.erase(it);
}

//
// Find or create the matching aliased BgpPath.
//
BgpPath *EvpnMacState::LocateAliasedPath(
    const EvpnSegment::RemotePe *remote_pe, uint32_t label) {
    const IPeer *peer = remote_pe->peer;
    const BgpAttr *attr = remote_pe->attr.get();
    uint32_t flags = remote_pe->flags | BgpPath::AliasedPath;
    BgpPath::PathSource src = remote_pe->src;
    for (AliasedPathList::iterator it = aliased_path_list_.begin();
        it != aliased_path_list_.end(); ++it) {
        BgpPath *path = *it;
        if (path->GetPeer() == peer &&
            path->GetAttr() == attr &&
            path->GetFlags() == flags &&
            path->GetLabel() == label) {
            return path;
        }
    }

    return (new BgpPath(peer, src, attr, flags, label));
}

//
// Update aliased BgpPaths for the EvpnRoute based on the remote PEs
// for the EvpnSegment.
// Return true if the list of aliased paths is modified.
//
bool EvpnMacState::ProcessMacRouteAliasing() {
    CHECK_CONCURRENCY("db::DBTable");

    const BgpPath *path = route_->BestPath();

    // Find the remote PE entry that corresponds to the primary path.
    bool found_primary_pe = false;
    EvpnSegment::const_iterator it;
    if (path && segment_ && !segment_->single_active())
        it = segment_->begin();
    for (; path && segment_ && !segment_->single_active() &&
         it != segment_->end(); ++it) {
        if (path->GetAttr()->nexthop() != it->attr->nexthop())
            continue;
        found_primary_pe = true;
        break;
    }

    // Go through the remote PE list for the EvpnSegment and build the
    // list of future aliased paths.
    AliasedPathList future_aliased_path_list;
    if (found_primary_pe && path && segment_ && !segment_->single_active())
        it = segment_->begin();
    for (; found_primary_pe && path && segment_ &&
         !segment_->single_active() && it != segment_->end(); ++it) {
        // Skip if there's a BGP_XMPP path with the remote PE as nexthop.
        if (route_->FindPath(it->attr->nexthop()))
            continue;

        // Locate the aliased path using attributes for the remote PE and
        // label from the primary path.
        BgpPath *aliased_path =
            LocateAliasedPath(it.operator->(), path->GetLabel());
        future_aliased_path_list.insert(aliased_path);
    }

    // Reconcile the current and future aliased paths and notify/delete the
    // route as appropriate.
    bool modified = set_synchronize(&aliased_path_list_,
        &future_aliased_path_list,
        boost::bind(&EvpnMacState::AddAliasedPath, this, _1),
        boost::bind(&EvpnMacState::DeleteAliasedPath, this, _1));
    return modified;
}

//
// Constructor for EvpnManagerPartition.
//
EvpnManagerPartition::EvpnManagerPartition(EvpnManager *evpn_manager,
    size_t part_id)
    : evpn_manager_(evpn_manager),
      part_id_(part_id),
      mac_update_trigger_(new TaskTrigger(
          boost::bind(&EvpnManagerPartition::ProcessMacUpdateList, this),
          TaskScheduler::GetInstance()->GetTaskId("db::DBTable"),
          part_id)) {
    table_partition_ = evpn_manager_->GetTablePartition(part_id);
}

//
// Destructor for EvpnManagerPartition.
//
EvpnManagerPartition::~EvpnManagerPartition() {
    assert(local_mcast_node_list_.empty());
    assert(remote_mcast_node_list_.empty());
    assert(mac_update_list_.empty());
}

//
// Get the DBTablePartition for the EvpnTable for our partition id.
//
DBTablePartition *EvpnManagerPartition::GetTablePartition() {
    return evpn_manager_->GetTablePartition(part_id_);
}

//
// Notify the Broadcast MAC route for the given EvpnMcastNode.
//
void EvpnManagerPartition::NotifyNodeRoute(EvpnMcastNode *node) {
    DBTablePartition *tbl_partition = GetTablePartition();
    tbl_partition->Notify(node->route());
}

//
// Go through all replicator EvpnMcastNodes and notify associated Broadcast
// MAC route.
//
void EvpnManagerPartition::NotifyReplicatorNodeRoutes() {
    DBTablePartition *tbl_partition = GetTablePartition();
    EvpnMcastNodeList::const_iterator it = replicator_node_list_.begin();
    for (; it != replicator_node_list_.end(); it++) {
        BOOST_FOREACH(EvpnMcastNode *node, it->second)
            tbl_partition->Notify(node->route());
    }
}

//
// Go through all ingress replication client EvpnMcastNodes and notify the
// associated Broadcast MAC route.
//
void EvpnManagerPartition::NotifyIrClientNodeRoutes(
    bool exclude_edge_replication_supported) {
    DBTablePartition *tbl_partition = GetTablePartition();
    EvpnMcastNodeList::const_iterator it = ir_client_node_list_.begin();
    for (; it != ir_client_node_list_.end(); it++) {
        BOOST_FOREACH(EvpnMcastNode *node, it->second) {
            if (exclude_edge_replication_supported &&
                !node->edge_replication_not_supported()) {
                continue;
            }
            tbl_partition->Notify(node->route());
        }
    }
}

//
// Add an EvpnMcastNode to the EvpnManagerPartition.
//
void EvpnManagerPartition::AddMcastNode(EvpnMcastNode *node, EvpnRoute *rt) {
    EvpnState::SG sg = EvpnState::SG(rt->GetPrefix().source(),
                                     rt->GetPrefix().group());
    if (node->type() == EvpnMcastNode::LocalNode) {
        local_mcast_node_list_[sg].insert(node);
        if (node->assisted_replication_supported())
            replicator_node_list_[sg].insert(node);
        if (!node->assisted_replication_leaf())
            ir_client_node_list_[sg].insert(node);
        NotifyNodeRoute(node);
    } else {
        remote_mcast_node_list_[sg].insert(node);
        if (node->assisted_replication_leaf()) {
            leaf_node_list_[sg].insert(node);
            NotifyReplicatorNodeRoutes();
        } else if (node->edge_replication_not_supported()) {
            regular_node_list_[sg].insert(node);
            NotifyIrClientNodeRoutes(false);
        } else if (!node->assisted_replication_leaf()) {
            NotifyIrClientNodeRoutes(true);
        }
    }
}

//
// Delete an EvpnMcastNode from the EvpnManagerPartition.
//
bool EvpnManagerPartition::RemoveMcastNodeFromList(EvpnState::SG &sg,
                                                   EvpnMcastNode *node,
                                                   EvpnMcastNodeList *list) {
    size_t deleted = 0;
    if (list->count(sg)) {
        deleted = (*list)[sg].erase(node);
        if ((*list)[sg].size() == 0)
            list->erase(sg);
    }
    return (deleted > 0);
}

//
// Delete an EvpnMcastNode from the EvpnManagerPartition.
//
void EvpnManagerPartition::DeleteMcastNode(EvpnMcastNode *node,
                                           EvpnRoute * rt) {
    EvpnState::SG sg = EvpnState::SG(rt->GetPrefix().source(),
                                     rt->GetPrefix().group());
    if (node->type() == EvpnMcastNode::LocalNode) {
        RemoveMcastNodeFromList(sg, node, &local_mcast_node_list_);
        RemoveMcastNodeFromList(sg, node, &replicator_node_list_);
        RemoveMcastNodeFromList(sg, node, &ir_client_node_list_);
    } else {
        RemoveMcastNodeFromList(sg, node, &remote_mcast_node_list_);
        if (RemoveMcastNodeFromList(sg, node, &leaf_node_list_)) {
            NotifyReplicatorNodeRoutes();
        } else {
            NotifyIrClientNodeRoutes(true);
        }
        if (RemoveMcastNodeFromList(sg, node, &regular_node_list_)) {
            NotifyIrClientNodeRoutes(false);
        }
    }
    if (empty())
        evpn_manager_->RetryDelete();
}

//
// Update an EvpnMcastNode in the EvpnManagerPartition.
// Need to remove/add EvpnMcastNode from the replicator, leaf and ir client
// lists as appropriate.
//
void EvpnManagerPartition::UpdateMcastNode(EvpnMcastNode *node, EvpnRoute *rt) {
    node->TriggerUpdate();
    EvpnState::SG sg = EvpnState::SG(rt->GetPrefix().source(),
                                     rt->GetPrefix().group());
    if (node->type() == EvpnMcastNode::LocalNode) {
        RemoveMcastNodeFromList(sg, node, &replicator_node_list_);
        if (node->assisted_replication_supported())
            replicator_node_list_[sg].insert(node);
        RemoveMcastNodeFromList(sg, node, &ir_client_node_list_);
        if (!node->assisted_replication_leaf())
            ir_client_node_list_[sg].insert(node);
        NotifyNodeRoute(node);
    } else {
        bool was_leaf = RemoveMcastNodeFromList(sg, node, &leaf_node_list_);
        if (node->assisted_replication_leaf())
            leaf_node_list_[sg].insert(node);
        if (was_leaf || node->assisted_replication_leaf())
            NotifyReplicatorNodeRoutes();
        if (!was_leaf || !node->assisted_replication_leaf())
            NotifyIrClientNodeRoutes(true);
        bool was_regular = RemoveMcastNodeFromList(
                                   sg, node, &regular_node_list_);
        if (node->edge_replication_not_supported())
            regular_node_list_[sg].insert(node);
        if (was_regular || node->edge_replication_not_supported())
            NotifyIrClientNodeRoutes(false);
    }
}

//
// Add the given MAC route to the update list.
// This method gets called either when the MAC route itself changes or when
// the remote PE list for the EvpnSegment of the MAC route gets updated.
//
void EvpnManagerPartition::TriggerMacRouteUpdate(EvpnRoute *route) {
    CHECK_CONCURRENCY("db::DBTable", "bgp::EvpnSegment");

    mac_update_list_.insert(route);
    mac_update_trigger_->Set();
}

//
// Process the MAC route update list for this EvpnManagerPartition.
//
bool EvpnManagerPartition::ProcessMacUpdateList() {
    CHECK_CONCURRENCY("db::DBTable");

    BgpTable *table = evpn_manager_->table();
    int listener_id = evpn_manager_->listener_id();

    BOOST_FOREACH(EvpnRoute *route, mac_update_list_) {
        // Skip if the route is on the change list. We will get another
        // chance to process it after the MacAdvertisement listener sees
        // it and changes the EvpnMacState to point to the updated value
        // of EvpnSegment.
        if (route->is_onlist())
            continue;

        EvpnMacState *mac_state =
            dynamic_cast<EvpnMacState *>(route->GetState(table, listener_id));
        assert(mac_state);
        bool modified = mac_state->ProcessMacRouteAliasing();
        if (route->HasPaths()) {
            if (!mac_state->segment()) {
                route->ClearState(table, listener_id);
                delete mac_state;
            }
            if (modified) {
                table_partition_->Notify(route);
            }
        } else {
            route->ClearState(table, listener_id);
            table_partition_->Delete(route);
            delete mac_state;
        }
    }

    mac_update_list_.clear();
    evpn_manager_->RetryDelete();
    return true;
}

bool EvpnManagerPartition::GetForestNodeAddress(ErmVpnRoute *rt,
                                                 Ip4Address *address) const {
    if (!evpn_manager_->ermvpn_table()->tree_manager())
        return false;
    uint32_t label;
    vector<string> te;
    return evpn_manager_->ermvpn_table()->tree_manager()->GetForestNodePMSI(
                                           rt, &label, address, &te);
}

EvpnStatePtr EvpnManagerPartition::GetState(const SG &sg) const {
    EvpnState::StatesMap::const_iterator iter = states_.find(sg);
    return iter != states_.end() ?  iter->second : NULL;
}

EvpnStatePtr EvpnManagerPartition::GetState(const SG &sg) {
    EvpnState::StatesMap::iterator iter = states_.find(sg);
    return iter != states_.end() ?  iter->second : NULL;
}

EvpnStatePtr EvpnManagerPartition::GetState(EvpnRoute *rt) {
    EvpnState::SG sg = EvpnState::SG(rt->GetPrefix().source(),
                                     rt->GetPrefix().group());
    return GetState(sg);
}

EvpnStatePtr EvpnManagerPartition::CreateState(const SG &sg) {
    EvpnStatePtr state(new EvpnState(sg, &states_, evpn_manager_));
    assert(states_.insert(make_pair(sg, state.get())).second);
    EVPN_TRACE(EvpnStateCreate, sg.source.to_string(), sg.group.to_string());
    return state;
}

EvpnStatePtr EvpnManagerPartition::LocateState(const SG &sg) {
    EvpnStatePtr evpn_state = GetState(sg);
    if (evpn_state)
        return evpn_state;
    evpn_state = CreateState(sg);
    assert(evpn_state);
    return evpn_state;
}

EvpnStatePtr EvpnManagerPartition::LocateState(EvpnRoute *rt) {
    EvpnState::SG sg = EvpnState::SG(rt->GetPrefix().source(),
                                     rt->GetPrefix().group());
    return LocateState(sg);
}

//
// Disable processing of the update list.
// For testing only.
//
void EvpnManagerPartition::DisableMacUpdateProcessing() {
    mac_update_trigger_->set_disable();
}

//
// Enable processing of the update list.
// For testing only.
//
void EvpnManagerPartition::EnableMacUpdateProcessing() {
    mac_update_trigger_->set_enable();
}

//
// Return true if the EvpnManagerPartition is empty i.e. it has no local
// or remote EvpnMcastNodes and no MAC routes that need to be updated.
//
bool EvpnManagerPartition::empty() const {
    if (!local_mcast_node_list_.empty())
        return false;
    if (!remote_mcast_node_list_.empty())
        return false;
    if (!mac_update_list_.empty())
        return false;
    assert(leaf_node_list_.empty());
    assert(replicator_node_list_.empty());
    assert(regular_node_list_.empty());
    assert(ir_client_node_list_.empty());
    return true;
}

//
// Return the BgpServer for the EvpnManagerPartition.
//
BgpServer *EvpnManagerPartition::server() {
    return evpn_manager_->server();
}

//
// Return the EvpnTable for the EvpnManagerPartition.
//
const EvpnTable *EvpnManagerPartition::table() const {
    return evpn_manager_->table();
}

//
// Constructor for EvpnManager.
//
EvpnManager::EvpnManager(EvpnTable *table)
    : table_(table),
      listener_id_(DBTable::kInvalidId),
      segment_delete_trigger_(new TaskTrigger(
          boost::bind(&EvpnManager::ProcessSegmentDeleteSet, this),
          TaskScheduler::GetInstance()->GetTaskId("bgp::EvpnSegment"), 0)),
      segment_update_trigger_(new TaskTrigger(
          boost::bind(&EvpnManager::ProcessSegmentUpdateSet, this),
          TaskScheduler::GetInstance()->GetTaskId("bgp::EvpnSegment"), 0)),
      table_delete_ref_(this, table->deleter()) {
    deleter_.reset(new DeleteActor(this));
    db_states_count_ = 0;
}

//
// Destructor for EvpnManager.
//
EvpnManager::~EvpnManager() {
    assert(segment_map_.empty());
    assert(segment_delete_set_.empty());
    assert(segment_update_set_.empty());
}

//
// Initialize the EvpnManager. We allocate the EvpnManagerPartitions
// and register a DBListener for the EvpnTable.
//
void EvpnManager::Initialize() {
    AllocPartitions();
    listener_id_ = table_->Register(
        boost::bind(&EvpnManager::RouteListener, this, _1, _2),
        "EvpnManager");
    ermvpn_table_ = dynamic_cast<ErmVpnTable *>(
        table_->routing_instance()->GetTable(Address::ERMVPN));
    if (ermvpn_table_)
        ermvpn_listener_id_ = ermvpn_table_->Register(
            boost::bind(&EvpnManager::ErmVpnRouteListener, this, _1, _2),
            "EvpnManager");
}

//
// Terminate the EvpnManager. We free the EvpnManagerPartitions
// and unregister from the EvpnTable.
//
void EvpnManager::Terminate() {
    CHECK_CONCURRENCY("bgp::Config");
    table_->Unregister(listener_id_);
    listener_id_ = DBTable::kInvalidId;
    if (ermvpn_listener_id_ != DBTable::kInvalidId) {
        ermvpn_table_->Unregister(ermvpn_listener_id_);
        ermvpn_listener_id_ = DBTable::kInvalidId;
    }
    FreePartitions();
}

//
// Allocate the EvpnManagerPartitions.
//
void EvpnManager::AllocPartitions() {
    for (int part_id = 0; part_id < DB::PartitionCount(); part_id++) {
        partitions_.push_back(new EvpnManagerPartition(this, part_id));
    }
}

//
// Free the EvpnManagerPartitions.
//
void EvpnManager::FreePartitions() {
    STLDeleteValues(&partitions_);
}

//
// Disable processing of the update lists in all partitions.
// For testing only.
//
void EvpnManager::DisableMacUpdateProcessing() {
    for (int part_id = 0; part_id < DB::PartitionCount(); part_id++) {
        partitions_[part_id]->DisableMacUpdateProcessing();
    }
}

//
// Enable processing of the update lists in all partitions.
// For testing only.
//
void EvpnManager::EnableMacUpdateProcessing() {
    for (int part_id = 0; part_id < DB::PartitionCount(); part_id++) {
        partitions_[part_id]->EnableMacUpdateProcessing();
    }
}

//
// Get the EvpnManagerPartition for the given partition id.
//
EvpnManagerPartition *EvpnManager::GetPartition(size_t part_id) {
    return partitions_[part_id];
}

//
// Get the DBTablePartition for the EvpnTable for given partition id.
//
DBTablePartition *EvpnManager::GetTablePartition(size_t part_id) {
    return static_cast<DBTablePartition *>(table_->GetTablePartition(part_id));
}

//
// Construct export state for the given EvpnRoute. Note that the route
// only needs to be exported to the IPeer from which it was learnt.
//
UpdateInfo *EvpnManager::GetUpdateInfo(EvpnRoute *route) {
    CHECK_CONCURRENCY("db::DBTable");

    DBState *dbstate = route->GetState(table_, listener_id_);
    EvpnLocalMcastNode *local_node =
        dynamic_cast<EvpnLocalMcastNode *>(dbstate);

    if (!local_node)
        return NULL;

    return local_node->GetUpdateInfo(route);
}

BgpServer *EvpnManager::server() {
    return table_->server();
}

//
// Find or create the EvpnSegment for the given EthernetSegmentId.
//
EvpnSegment *EvpnManager::LocateSegment(const EthernetSegmentId &esi) {
    assert(!esi.IsZero());
    tbb::spin_rw_mutex::scoped_lock write_lock(segment_rw_mutex_, true);
    SegmentMap::iterator loc = segment_map_.find(esi);
    EvpnSegment *segment = (loc != segment_map_.end()) ? loc->second : NULL;
    if (!segment) {
        segment = new EvpnSegment(this, esi);
        segment_map_.insert(esi, segment);
    }
    return segment;
}

//
// Find the EvpnSegment for the given EthernetSegmentId.
//
EvpnSegment *EvpnManager::FindSegment(const EthernetSegmentId &esi) {
    assert(!esi.IsZero());
    tbb::spin_rw_mutex::scoped_lock read_lock(segment_rw_mutex_, false);
    SegmentMap::iterator loc = segment_map_.find(esi);
    return (loc != segment_map_.end()) ? loc->second : NULL;
}

//
// Trigger deletion of the given EvpnSegment.
// The EvpnSegment is added to a set of EvpnSegments that can potentially
// be deleted. This method can be invoked from multiple db::DBTable tasks
// in parallel when a MAC routes are removed from the dependency list in an
// EvpnSegment. Hence we ensure exclusive access using a write lock.
//
// The list is processed from the context of bgp::EvpnSegment task which is
// mutually exclusive with db::DBTable task.
//
void EvpnManager::TriggerSegmentDelete(EvpnSegment *segment) {
    CHECK_CONCURRENCY("db::DBTable", "bgp::EvpnSegment");

    tbb::spin_rw_mutex::scoped_lock write_lock(segment_rw_mutex_, true);
    segment_delete_set_.insert(segment);
    segment_delete_trigger_->Set();
}

//
// Process the set of EvpnSegments that can potentially be deleted.
// Remove the EvpnSegment from the map and destroy if it's fine to
// to delete the EvpnSegment.
//
bool EvpnManager::ProcessSegmentDeleteSet() {
    CHECK_CONCURRENCY("bgp::EvpnSegment");

    BOOST_FOREACH(EvpnSegment *segment, segment_delete_set_) {
        if (segment->MayDelete()) {
            segment_update_set_.erase(segment);
            EthernetSegmentId esi = segment->esi();
            segment_map_.erase(esi);
        }
    }
    segment_delete_set_.clear();
    RetryDelete();
    return true;
}

//
// Disable processing of the delete list.
// For testing only.
//
void EvpnManager::DisableSegmentDeleteProcessing() {
    segment_delete_trigger_->set_disable();
}

//
// Enable processing of the delete list.
// For testing only.
//
void EvpnManager::EnableSegmentDeleteProcessing() {
    segment_delete_trigger_->set_enable();
}

//
// Trigger update of the given EvpnSegment.
// The EvpnSegment is added to a set of EvpnSegments for which updates
// need triggered. This method is called in the context of db::DBTable
// task and a task instance of 0 since all AutoDisocvery routes always
// get sharded to partition 0.
//
// The set is processed in the context of bgp::EvpnSegment task, which
// is mutually exclusive with db::DBTable task.
//
void EvpnManager::TriggerSegmentUpdate(EvpnSegment *segment) {
    CHECK_CONCURRENCY("db::DBTable");

    segment_update_set_.insert(segment);
    segment_update_trigger_->Set();
}

//
// Process the set of EvpnSegments that need to be updated.
//
// Go through each EvpnSegment and update it's PE list. Trigger updates
// of all it's dependent MAC routes if there's a change in the PE list.
//
bool EvpnManager::ProcessSegmentUpdateSet() {
    CHECK_CONCURRENCY("bgp::EvpnSegment");

    BOOST_FOREACH(EvpnSegment *segment, segment_update_set_) {
        EvpnRoute *esi_ad_route = segment->esi_ad_route();
        bool changed = segment->UpdatePeList();
        if (changed)
            segment->TriggerMacRouteUpdate();
        if (!esi_ad_route->IsValid()) {
            segment->clear_esi_ad_route();
            esi_ad_route->ClearState(table_, listener_id_);
            TriggerSegmentDelete(segment);
        }
    }
    segment_update_set_.clear();
    RetryDelete();
    return true;
}

//
// Disable processing of the update list.
// For testing only.
//
void EvpnManager::DisableSegmentUpdateProcessing() {
    segment_update_trigger_->set_disable();
}

//
// Enable processing of the update list.
// For testing only.
//
void EvpnManager::EnableSegmentUpdateProcessing() {
    segment_update_trigger_->set_enable();
}

//
// DBListener callback handler for AutoDisocvery routes in the EvpnTable.
//
void EvpnManager::AutoDiscoveryRouteListener(EvpnRoute *route) {
    CHECK_CONCURRENCY("db::DBTable");

    DBState *dbstate = route->GetState(table_, listener_id_);
    if (!dbstate) {
        // We have no previous DBState for this route.
        // Bail if the route is not valid.
        if (!route->IsValid())
            return;

        // Locate the EvpnSegment and associate it with the route.
        // Trigger an update of EvpnSegment so that it's PE list
        // gets updated.
        EvpnSegment *segment = LocateSegment(route->GetPrefix().esi());
        segment->set_esi_ad_route(route);
        route->SetState(table_, listener_id_, segment);
        TriggerSegmentUpdate(segment);
    } else {
        // Trigger an update of EvpnSegment so that it's PE list
        // gets updated.
        EvpnSegment *segment = dynamic_cast<EvpnSegment *>(dbstate);
        assert(segment);
        TriggerSegmentUpdate(segment);
    }
}

//
// DBListener callback handler for MacAdvertisement routes in the EvpnTable.
//
void EvpnManager::MacAdvertisementRouteListener(
    EvpnManagerPartition *partition, EvpnRoute *route) {
    CHECK_CONCURRENCY("db::DBTable");

    DBState *dbstate = route->GetState(table_, listener_id_);
    if (!dbstate) {
        // We have no previous DBState for this route.
        // Bail if the route is not valid or if it doesn't have an ESI.
        if (!route->IsValid())
            return;
        const BgpPath *path = route->BestPath();
        if (path->GetAttr()->esi().IsZero())
            return;

        // Create a new EvpnMacState and associate it with the route.
        EvpnMacState *mac_state = new EvpnMacState(this, route);
        route->SetState(table_, listener_id_, mac_state);

        // Locate the EvpnSegment and add the MAC route as a dependent
        // of the EvpnSegment.
        EvpnSegment *segment = LocateSegment(path->GetAttr()->esi());
        mac_state->set_segment(segment);
        segment->AddMacRoute(partition->part_id(), route);
        partition->TriggerMacRouteUpdate(route);
    } else {
        EvpnMacState *mac_state = dynamic_cast<EvpnMacState *>(dbstate);
        assert(mac_state);
        EvpnSegment *segment = mac_state->segment();

        // Handle change in the ESI and update the dependency on the
        // EvpnSegment as appropriate.
        // Note that the DBState on the EvpnRoute doesn't get cleared
        // here. That only happens when aliasing for the route is being
        // processed.
        const BgpPath *path = route->BestPath();
        if (!route->IsValid() || (path && path->GetAttr()->esi().IsZero())) {
            if (segment) {
                segment->DeleteMacRoute(partition->part_id(), route);
                mac_state->clear_segment();
            }
        } else if (!segment && !path->GetAttr()->esi().IsZero()) {
            segment = LocateSegment(path->GetAttr()->esi());
            mac_state->set_segment(segment);
            segment->AddMacRoute(partition->part_id(), route);
        } else if (segment && segment->esi() != path->GetAttr()->esi()) {
            segment->DeleteMacRoute(partition->part_id(), route);
            mac_state->clear_segment();
            segment = LocateSegment(path->GetAttr()->esi());
            mac_state->set_segment(segment);
            segment->AddMacRoute(partition->part_id(), route);
        }
        partition->TriggerMacRouteUpdate(route);
    }
}

//
// DBListener callback handler for InclusiveMulticast routes in the EvpnTable.
//
void EvpnManager::InclusiveMulticastRouteListener(
    EvpnManagerPartition *partition, EvpnRoute *route) {
    CHECK_CONCURRENCY("db::DBTable");

    DBState *dbstate = route->GetState(table_, listener_id_);
    if (!dbstate) {
        // We have no previous DBState for this route.
        // Bail if the route is not valid.
        if (!route->IsValid())
            return;

        // Create a new EvpnMcastNode and associate it with the route.
        EvpnMcastNode *node;
        if (route->GetPrefix().type() == EvpnPrefix::MacAdvertisementRoute) {
            node = new EvpnLocalMcastNode(partition, route);
        } else {
            node = new EvpnRemoteMcastNode(partition, route);
        }
        partition->AddMcastNode(node, route);
        route->SetState(table_, listener_id_, node);
    } else {
        EvpnMcastNode *node = dynamic_cast<EvpnMcastNode *>(dbstate);
        assert(node);

        if (!route->IsValid()) {
            // Delete the EvpnMcastNode associated with the route.
            route->ClearState(table_, listener_id_);
            partition->DeleteMcastNode(node, route);
            delete node;
        } else if (node->UpdateAttributes(route)) {
            // Update the EvpnMcastNode associated with the route.
            partition->UpdateMcastNode(node, route);
        }
    }
}

//
// DBListener callback handler for SelectiveMulticast routes in the EvpnTable.
//
void EvpnManager::SelectiveMulticastRouteListener(
    EvpnManagerPartition *partition, EvpnRoute *route) {

    CHECK_CONCURRENCY("db::DBTable");
    EvpnMcastNode *dbstate = dynamic_cast<EvpnMcastNode *>(
            route->GetState(table_, listener_id_));
    bool is_usable = route->IsUsable();
    bool is_deleted = route->IsDeleted();
    bool checkErmvpnRoute = false;
    if (route->BestPath()) {
        checkErmvpnRoute = route->BestPath()->GetFlags() &
            BgpPath::CheckGlobalErmVpnRoute;
    }
    if ((!is_usable && !checkErmvpnRoute) || is_deleted) {
        if (!dbstate)
            return;

        EvpnStatePtr evpn_state = partition->GetState(route);
        if (evpn_state)
            evpn_state->smet_routes().erase(route);
        EVPN_RT_LOG(route, "Processed Smet route deletion");
        ClearDBState(route);
        partition->NotifyForestNode(route, ermvpn_table());
        partition->DeleteMcastNode(dbstate, route);
        delete dbstate;
        return;
    }

    EvpnStatePtr evpn_state = partition->LocateState(route);
    assert(evpn_state);
    evpn_state->smet_routes().insert(route);
    BgpPath *path = const_cast<BgpPath *>(route->BestPath());
    if (path && path->CheckErmVpn()) {
        ErmVpnRoute *global_rt = evpn_state->global_ermvpn_tree_rt();
        Ip4Address address;
        bool nh_found = partition->GetForestNodeAddress(global_rt, &address);
        if (nh_found) {
            BgpAttrPtr attr = path->GetAttr();
            BgpAttrPtr new_attr = partition->server()->attr_db()->
                                  ReplaceNexthopAndLocate(attr.get(), address);
            path->SetAttr(new_attr, attr);
            path->ResetCheckErmVpn();
            partition->NotifyForestNode(route, ermvpn_table());
        }
    }

    if (!dbstate) {
        // Create a new DBState and associate it with the route.
        EvpnMcastNode *node;
        if (route->GetPrefix().ip_address() ==
                Ip4Address(table_->server()->bgp_identifier())) {
            node = new EvpnLocalMcastNode(partition, route, evpn_state);
        } else {
            node = new EvpnRemoteMcastNode(partition, route, evpn_state);
        }
        SetDBState(route, node);
        partition->AddMcastNode(node, route);
        EVPN_RT_LOG(route, "Processed Smet route creation");
    }
}

void EvpnManagerPartition::NotifyForestNode(EvpnRoute *route,
                                            ErmVpnTable *table) {
    const Ip4Address &source = route->GetPrefix().source().to_v4();
    const Ip4Address &group = route->GetPrefix().group().to_v4();
    if (table->tree_manager())
        table->tree_manager()->NotifyForestNode(part_id_, source, group);
}

// Set DB State and update count.
void EvpnManager::SetDBState(EvpnRoute *route, EvpnMcastNode *dbstate) {
    route->SetState(table_, listener_id_, dbstate);
    db_states_count_++;
}

// Create DB State and update count. If there is no DB State associated in the
// table, resume table deletion if the deletion was pending.
void EvpnManager::ClearDBState(EvpnRoute *route) {
    route->ClearState(table_, listener_id_);
    assert(db_states_count_);
    db_states_count_--;

    // Retry deletion now as there is no more attached db state in the table.
    if (!db_states_count_ && deleter_->IsDeleted())
        deleter_->RetryDelete();
}

// Check whether an ErmVpnRoute is locally originated GlobalTreeRoute.
bool EvpnManager::IsUsableGlobalTreeRootRoute(
        ErmVpnRoute *ermvpn_route) const {
    if (!ermvpn_route || !ermvpn_route->IsUsable())
        return false;
    if (!ermvpn_table()->tree_manager())
        return false;
    if (ermvpn_table()->tree_manager()->begin() ==
            ermvpn_table()->tree_manager()->end())
        return false;
    ErmVpnRoute *global_rt = ermvpn_table()->tree_manager()->
                               GetGlobalTreeRootRoute(
                               ermvpn_route->GetPrefix().source(),
                               ermvpn_route->GetPrefix().group());
    return (global_rt && global_rt == ermvpn_route);
}

// ErmVpnTable route listener callback function.
//
// Process changes (create/update/delete) to GlobalErmVpnRoute in vrf.ermvpn.0
void EvpnManager::ErmVpnRouteListener(DBTablePartBase *tpart,
                                      DBEntryBase *db_entry) {
    CHECK_CONCURRENCY("db::DBTable");

    ErmVpnRoute *ermvpn_route = dynamic_cast<ErmVpnRoute *>(db_entry);
    assert(ermvpn_route);

    // We only care about global tree routes for evpn stitching.
    if (ermvpn_route->GetPrefix().type() != ErmVpnPrefix::GlobalTreeRoute) {
        return;
    }

    EvpnMcastNode *dbstate = dynamic_cast<EvpnMcastNode *>(
        ermvpn_route->GetState(ermvpn_table(), ermvpn_listener_id()));

    // Handle GlobalTreeRoute route deletion.
    if (!IsUsableGlobalTreeRootRoute(ermvpn_route)) {
        // Ignore if there is no DB State associated with route.
        if (!dbstate)
            return;
        EvpnStatePtr evpn_state = dbstate->state();
        evpn_state->set_global_ermvpn_tree_rt(NULL);

        // Notify all received smet routes for PMSI re-computation.
        // Since usable global ermvpn is no longer available, any advertised
        // smet routes must now be withdrawn.
        BOOST_FOREACH(EvpnRoute *route, evpn_state->smet_routes()) {
            BgpPath *path = const_cast<BgpPath *>(route->BestPath());
            if (path && (path->GetPathId() == 0)) {
                path->SetCheckErmVpn();
            }
            route->Notify();
        }
        ermvpn_route->ClearState(ermvpn_table(), ermvpn_listener_id());
        EVPN_ERMVPN_RT_LOG(ermvpn_route,
                           "Processed EVPN GlobalErmVpnRoute deletion");
        delete dbstate;
        return;
    }

    // Set DB State in the route if not already done so before.
    EvpnManagerPartition *partition = GetPartition(tpart->index());
    EvpnStatePtr evpn_state;
    if (!dbstate) {
        EvpnState::SG sg(ermvpn_route);
        evpn_state = partition->LocateState(sg);
        dbstate = new EvpnLocalMcastNode(partition, NULL, evpn_state);
        ermvpn_route->SetState(ermvpn_table(), ermvpn_listener_id(), dbstate);
    } else {
        evpn_state = dbstate->state();
    }

    // Note down current usable ermvpn route for stitching to evpn.
    dbstate->state()->set_global_ermvpn_tree_rt(ermvpn_route);

    // Notify all originated Type6 routes.
    BOOST_FOREACH(EvpnRoute *route, evpn_state->smet_routes()) {
        route->Notify();
    }
    EVPN_ERMVPN_RT_LOG(ermvpn_route,
                       "Processed EVPN GlobalErmVpnRoute creation");
}

//
// DBListener callback handler for the EvpnTable.
//
void EvpnManager::RouteListener(DBTablePartBase *tpart, DBEntryBase *db_entry) {
    CHECK_CONCURRENCY("db::DBTable");

    EvpnManagerPartition *partition = GetPartition(tpart->index());
    EvpnRoute *route = dynamic_cast<EvpnRoute *>(db_entry);
    assert(route);

    switch (route->GetPrefix().type()) {
    case EvpnPrefix::AutoDiscoveryRoute:
        AutoDiscoveryRouteListener(route);
        break;
    case EvpnPrefix::MacAdvertisementRoute:
        if (route->GetPrefix().mac_addr().IsBroadcast()) {
            InclusiveMulticastRouteListener(partition, route);
        } else {
            MacAdvertisementRouteListener(partition, route);
        }
        break;
    case EvpnPrefix::InclusiveMulticastRoute:
        InclusiveMulticastRouteListener(partition, route);
        break;
    case EvpnPrefix::SelectiveMulticastRoute:
        SelectiveMulticastRouteListener(partition, route);
        break;
    default:
        break;
    }
}

//
// Fill information for introspect command.
// Note that all IM routes are always in partition 0.
//
void EvpnManager::FillShowInfo(ShowEvpnTable *sevt) const {
    CHECK_CONCURRENCY("db::DBTable");

    vector<string> regular_nves;
    vector<string> ar_replicators;
    vector<ShowEvpnMcastLeaf> ar_leafs;
    EvpnManagerPartition::EvpnMcastNodeList::const_iterator it =
                      partitions_[0]->remote_mcast_node_list()->begin();
    for (; it != partitions_[0]->remote_mcast_node_list()->end(); it++) {
        BOOST_FOREACH(const EvpnMcastNode *node, it->second) {
            if (node->assisted_replication_leaf()) {
                ShowEvpnMcastLeaf leaf;
                leaf.set_address(node->address().to_string());
                leaf.set_replicator(node->replicator_address().to_string());
                ar_leafs.push_back(leaf);
            } else if (node->assisted_replication_supported()) {
                ar_replicators.push_back(node->address().to_string());
            } else if (node->edge_replication_not_supported()) {
                regular_nves.push_back(node->address().to_string());
            }
        }
    }

    sort(regular_nves.begin(), regular_nves.end());
    sort(ar_replicators.begin(), ar_replicators.end());
    sort(ar_leafs.begin(), ar_leafs.end());
    sevt->set_regular_nves(regular_nves);
    sevt->set_ar_replicators(ar_replicators);
    sevt->set_ar_leafs(ar_leafs);
}

//
// Check if the EvpnManager can be deleted. This can happen only if all the
// EvpnManagerPartitions are empty.
//
bool EvpnManager::MayDelete() const {
    CHECK_CONCURRENCY("bgp::Config");

    if (!segment_map_.empty())
        return false;

    if (!segment_update_set_.empty())
        return false;
    if (segment_update_trigger_->IsSet())
        return false;

    if (!segment_delete_set_.empty())
        return false;
    if (segment_delete_trigger_->IsSet())
        return false;

    BOOST_FOREACH(const EvpnManagerPartition *partition, partitions_) {
        if (!partition->empty())
            return false;
        if (!partition->states().empty())
            return false;
    }
    if (db_states_count_)
        return false;
    return true;
}

//
// Initiate shutdown for the EvpnManager.
//
void EvpnManager::Shutdown() {
    CHECK_CONCURRENCY("bgp::Config");
}

//
// Trigger deletion of the EvpnManager and propagate the delete to any
// dependents.
//
void EvpnManager::ManagedDelete() {
    deleter_->Delete();
}

//
// Attempt to enqueue a delete for the EvpnManager.
//
void EvpnManager::RetryDelete() {
    if (!deleter()->IsDeleted())
        return;
    deleter()->RetryDelete();
}

//
// Return the LifetimeActor for the EvpnManager.
//
LifetimeActor *EvpnManager::deleter() {
    return deleter_.get();
}

EvpnState::SG::SG(const Ip4Address &source, const Ip4Address &group) :
    source(IpAddress(source)), group(IpAddress(group)) {
}

EvpnState::SG::SG(const ErmVpnRoute *route) :
        source(route->GetPrefix().source()),
        group(route->GetPrefix().group()) {
}

EvpnState::SG::SG(const EvpnRoute *route) :
        source(route->GetPrefix().source()), group(route->GetPrefix().group()) {
}

EvpnState::SG::SG(const IpAddress &source, const IpAddress &group) :
    source(source), group(group) {
}

bool EvpnState::SG::operator<(const SG &other) const {
    if (source < other.source)
        return true;
    if (source > other.source)
        return false;
    if (group < other.group)
        return true;
    if (group > other.group)
        return false;
    return false;
}

const EvpnState::SG &EvpnState::sg() const {
    return sg_;
}

void EvpnState::set_global_ermvpn_tree_rt(ErmVpnRoute *global_ermvpn_tree_rt) {
    global_ermvpn_tree_rt_ = global_ermvpn_tree_rt;
}

ErmVpnRoute *EvpnState::global_ermvpn_tree_rt() {
    return global_ermvpn_tree_rt_;
}

const ErmVpnRoute *EvpnState::global_ermvpn_tree_rt() const {
    return global_ermvpn_tree_rt_;
}
