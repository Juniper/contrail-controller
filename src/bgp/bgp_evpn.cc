/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/bgp_evpn.h"

#include <boost/foreach.hpp>

#include <algorithm>
#include <string>

#include "base/task_annotations.h"
#include "bgp/bgp_log.h"
#include "bgp/bgp_peer_types.h"
#include "bgp/bgp_server.h"
#include "bgp/bgp_update.h"
#include "bgp/evpn/evpn_table.h"

using std::sort;
using std::string;
using std::vector;

class EvpnManager::DeleteActor : public LifetimeActor {
public:
    explicit DeleteActor(EvpnManager *evpn_manager)
        : LifetimeActor(evpn_manager->server()->lifetime_manager()),
          evpn_manager_(evpn_manager) {
    }
    virtual ~DeleteActor() {
    }

    virtual bool MayDelete() const {
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
      route_(route),
      type_(type),
      label_(0),
      edge_replication_not_supported_(false),
      assisted_replication_supported_(false),
      assisted_replication_leaf_(false) {
    UpdateAttributes(route);
}

//
// Destructor for EvpnMcastNode.
//
EvpnMcastNode::~EvpnMcastNode() {
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
    uint8_t ar_type =
        pmsi_tunnel->tunnel_flags() & PmsiTunnelSpec::AssistedReplicationType;

    bool edge_replication_not_supported = false;
    if ((pmsi_tunnel->tunnel_flags() &
         PmsiTunnelSpec::EdgeReplicationSupported) == 0) {
        edge_replication_not_supported = true;
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
    if (replicator_address_ != pmsi_tunnel->identifier()) {
        replicator_address_ = pmsi_tunnel->identifier();
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

    if (!inclusive_mcast_route_->BestPath()) {
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
UpdateInfo *EvpnLocalMcastNode::GetUpdateInfo() {
    CHECK_CONCURRENCY("db::DBTable");

    // Nothing to send for a leaf as it already knows the replicator-address.
    if (assisted_replication_leaf_)
        return NULL;

    // Go through list of EvpnRemoteMcastNodes and build the BgpOList.
    BgpOListSpec olist_spec(BgpAttribute::OList);
    BOOST_FOREACH(EvpnMcastNode *node, partition_->remote_mcast_node_list()) {
        if (node->address() == address_)
            continue;
        if (node->assisted_replication_leaf())
            continue;
        if (!edge_replication_not_supported_ &&
            !node->edge_replication_not_supported())
            continue;

        const ExtCommunity *extcomm = node->attr()->ext_community();
        BgpOListElem elem(node->address(), node->label(),
            extcomm ? extcomm->GetTunnelEncap() : vector<string>());
        olist_spec.elements.push_back(elem);
    }

    // Go through list of leaf EvpnMcastNodes and build the leaf BgpOList.
    BgpOListSpec leaf_olist_spec(BgpAttribute::LeafOList);
    if (assisted_replication_supported_) {
        BOOST_FOREACH(EvpnMcastNode *node, partition_->leaf_node_list()) {
            if (node->replicator_address() != address_)
                continue;

            const ExtCommunity *extcomm = node->attr()->ext_community();
            BgpOListElem elem(node->address(), node->label(),
                extcomm ? extcomm->GetTunnelEncap() : vector<string>());
            leaf_olist_spec.elements.push_back(elem);
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
// Constructor for EvpnManagerPartition.
//
EvpnManagerPartition::EvpnManagerPartition(EvpnManager *evpn_manager,
    size_t part_id)
    : evpn_manager_(evpn_manager), part_id_(part_id) {
}

//
// Destructor for EvpnManagerPartition.
//
EvpnManagerPartition::~EvpnManagerPartition() {
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
    BOOST_FOREACH(EvpnMcastNode *node, replicator_node_list_) {
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
    BOOST_FOREACH(EvpnMcastNode *node, ir_client_node_list_) {
        if (exclude_edge_replication_supported &&
            !node->edge_replication_not_supported()) {
            continue;
        }
        tbl_partition->Notify(node->route());
    }
}

//
// Add an EvpnMcastNode to the EvpnManagerPartition.
//
void EvpnManagerPartition::AddMcastNode(EvpnMcastNode *node) {
    if (node->type() == EvpnMcastNode::LocalNode) {
        local_mcast_node_list_.insert(node);
        if (node->assisted_replication_supported())
            replicator_node_list_.insert(node);
        if (!node->assisted_replication_leaf())
            ir_client_node_list_.insert(node);
        NotifyNodeRoute(node);
    } else {
        remote_mcast_node_list_.insert(node);
        if (node->assisted_replication_leaf()) {
            leaf_node_list_.insert(node);
            NotifyReplicatorNodeRoutes();
        } else if (node->edge_replication_not_supported()) {
            regular_node_list_.insert(node);
            NotifyIrClientNodeRoutes(false);
        } else if (!node->assisted_replication_leaf()) {
            NotifyIrClientNodeRoutes(true);
        }
    }
}

//
// Delete an EvpnMcastNode from the EvpnManagerPartition.
//
void EvpnManagerPartition::DeleteMcastNode(EvpnMcastNode *node) {
    if (node->type() == EvpnMcastNode::LocalNode) {
        local_mcast_node_list_.erase(node);
        replicator_node_list_.erase(node);
        ir_client_node_list_.erase(node);
    } else {
        remote_mcast_node_list_.erase(node);
        if (leaf_node_list_.erase(node) > 0) {
            NotifyReplicatorNodeRoutes();
        } else {
            NotifyIrClientNodeRoutes(true);
        }
        if (regular_node_list_.erase(node) > 0)
            NotifyIrClientNodeRoutes(false);
    }
    if (empty())
        evpn_manager_->RetryDelete();
}

//
// Update an EvpnMcastNode in the EvpnManagerPartition.
// Need to remove/add EvpnMcastNode from the replicator, leaf and ir client
// lists as appropriate.
//
void EvpnManagerPartition::UpdateMcastNode(EvpnMcastNode *node) {
    node->TriggerUpdate();
    if (node->type() == EvpnMcastNode::LocalNode) {
        replicator_node_list_.erase(node);
        if (node->assisted_replication_supported())
            replicator_node_list_.insert(node);
        ir_client_node_list_.erase(node);
        if (!node->assisted_replication_leaf())
            ir_client_node_list_.insert(node);
        NotifyNodeRoute(node);
    } else {
        bool was_leaf = leaf_node_list_.erase(node) > 0;
        if (node->assisted_replication_leaf())
            leaf_node_list_.insert(node);
        if (was_leaf || node->assisted_replication_leaf())
            NotifyReplicatorNodeRoutes();
        if (!was_leaf || !node->assisted_replication_leaf())
            NotifyIrClientNodeRoutes(true);
        bool was_regular = regular_node_list_.erase(node) > 0;
        if (node->edge_replication_not_supported())
            regular_node_list_.insert(node);
        if (was_regular || node->edge_replication_not_supported())
            NotifyIrClientNodeRoutes(false);
    }
}

//
// Return true if the EvpnManagerPartition is empty i.e. it has no local or
// remote EvpnMcastNodes.
//
bool EvpnManagerPartition::empty() const {
    if (!local_mcast_node_list_.empty())
        return false;
    if (!remote_mcast_node_list_.empty())
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
      table_delete_ref_(this, table->deleter()) {
    deleter_.reset(new DeleteActor(this));
}

//
// Destructor for EvpnManager.
//
EvpnManager::~EvpnManager() {
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
}

//
// Terminate the EvpnManager. We free the EvpnManagerPartitions
// and unregister from the EvpnTable.
//
void EvpnManager::Terminate() {
    table_->Unregister(listener_id_);
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

    return local_node->GetUpdateInfo();
}

BgpServer *EvpnManager::server() {
    return table_->server();
}

//
// DBListener callback handler for the EvpnTable.
//
void EvpnManager::RouteListener(DBTablePartBase *tpart, DBEntryBase *db_entry) {
    CHECK_CONCURRENCY("db::DBTable");

    EvpnManagerPartition *partition = partitions_[tpart->index()];
    EvpnRoute *route = dynamic_cast<EvpnRoute *>(db_entry);
    assert(route);

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
        partition->AddMcastNode(node);
        route->SetState(table_, listener_id_, node);
    } else {
        EvpnMcastNode *node = dynamic_cast<EvpnMcastNode *>(dbstate);
        assert(node);

        if (!route->IsValid()) {
            // Delete the EvpnMcastNode associated with the route.
            route->ClearState(table_, listener_id_);
            partition->DeleteMcastNode(node);
            delete node;
        } else if (node->UpdateAttributes(route)) {
            // Update the EvpnMcastNode associated with the route.
            partition->UpdateMcastNode(node);
        }
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
    BOOST_FOREACH(const EvpnMcastNode *node,
                  partitions_[0]->remote_mcast_node_list()) {
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

    BOOST_FOREACH(const EvpnManagerPartition *parition, partitions_) {
        if (!parition->empty())
            return false;
    }
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
