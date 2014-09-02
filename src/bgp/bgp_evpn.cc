/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/bgp_evpn.h"

#include <boost/foreach.hpp>

#include "base/task_annotations.h"
#include "base/util.h"
#include "bgp/bgp_path.h"
#include "bgp/bgp_route.h"
#include "bgp/evpn/evpn_table.h"
#include "bgp/origin-vn/origin_vn.h"

using namespace std;

class EvpnManager::DeleteActor : public LifetimeActor {
public:
    DeleteActor(EvpnManager *evpn_manager)
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
    : partition_(partition), route_(route), type_(type),
      edge_replication_not_supported_(false) {
    const BgpPath *path = route->BestPath();
    attr_ = path->GetAttr();
    label_ = path->GetLabel();
    address_ = path->GetAttr()->nexthop().to_v4();
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
bool EvpnMcastNode::Update(EvpnRoute *route) {
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

    return changed;
}

//
// Constructor for EvpnLocalMcastNode.
//
// Figure out if edge replication is supported and add an Inclusive Multicast
// route corresponding to the Broadcast MAC route.
//
// Need to Notify the Broadcast MAC route so that the table Export method
// can run and build the OList. OList is not built till EvpnLocalMcastNode
// has been created.
//
EvpnLocalMcastNode::EvpnLocalMcastNode(EvpnManagerPartition *partition,
    EvpnRoute *route)
    : EvpnMcastNode(partition, route, EvpnMcastNode::LocalNode),
      inclusive_mcast_route_(NULL) {
    if (attr_->params() & BgpAttrParams::EdgeReplicationNotSupported)
        edge_replication_not_supported_ = true;
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
    const EvpnPrefix &mac_prefix = route_->GetPrefix();
    EvpnPrefix prefix(mac_prefix.route_distinguisher(), mac_prefix.tag(),
        address_);
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

    // Add PmsiTunnel to attributes of the broadcast MAC route.
    PmsiTunnelSpec pmsi_spec;
    if (edge_replication_not_supported_) {
        pmsi_spec.tunnel_flags = 0;
    } else {
        pmsi_spec.tunnel_flags = PmsiTunnelSpec::EdgeReplicationSupported;
    }
    pmsi_spec.tunnel_type = PmsiTunnelSpec::IngressReplication;
    pmsi_spec.SetLabel(label_);
    pmsi_spec.SetIdentifier(address_);
    BgpAttrDB *attr_db = partition_->server()->attr_db();
    BgpAttrPtr attr =
        attr_db->ReplacePmsiTunnelAndLocate(attr_.get(), &pmsi_spec);

    // Add a path with source BgpPath::Local.
    BgpPath *path = new BgpPath(BgpPath::Local, attr, 0, label_);
    route->InsertPath(path);
    inclusive_mcast_route_ = route;
    tbl_partition->Notify(inclusive_mcast_route_);
}

//
// Delete Inclusive Multicast route for this EvpnLocalMcastNode.
//
void EvpnLocalMcastNode::DeleteInclusiveMulticastRoute() {
    if (!inclusive_mcast_route_)
        return;

    DBTablePartition *tbl_partition = partition_->GetTablePartition();
    inclusive_mcast_route_->RemovePath(BgpPath::Local);

    if (!inclusive_mcast_route_->BestPath()) {
        tbl_partition->Delete(inclusive_mcast_route_);
    } else {
        tbl_partition->Notify(inclusive_mcast_route_);
    }
    inclusive_mcast_route_ = NULL;
}

//
// Update the attributes for a EvpnLocalMcastNode.
// Update common attributes and edge_replication_not_supported_.
// Returns true if anything changed.
//
bool EvpnLocalMcastNode::Update(EvpnRoute *route) {
    bool changed = EvpnMcastNode::Update(route);

    bool edge_replication_not_supported = false;
    if (attr_->params() & BgpAttrParams::EdgeReplicationNotSupported)
        edge_replication_not_supported = true;
    if (edge_replication_not_supported != edge_replication_not_supported_) {
        edge_replication_not_supported_ = edge_replication_not_supported;
        changed = true;
    }
    return changed;
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

    // Go through list of EvpnRemoteMcastNodes and build the BgpOList.
    BgpOListPtr olist(new BgpOList);
    BOOST_FOREACH(EvpnMcastNode *node, partition_->remote_mcast_node_list()) {
        if (node->address() == address_)
            continue;
        if (!edge_replication_not_supported_ &&
            !node->edge_replication_not_supported())
            continue;

        const ExtCommunity *extcomm = node->attr()->ext_community();
        BgpOListElem elem(node->address(), node->label(),
            extcomm ? extcomm->GetTunnelEncap() : vector<string>());
        olist->elements.push_back(elem);
    }

    // Bail if the BgpOList is empty.
    if (olist->elements.empty())
        return NULL;

    // Add BgpOList to attributes of the broadcast MAC route.
    BgpAttrDB *attr_db = partition_->server()->attr_db();
    BgpAttrPtr attr = attr_db->ReplaceOListAndLocate(attr_.get(), olist);

    UpdateInfo *uinfo = new UpdateInfo;
    uinfo->roattr = RibOutAttr(attr.get(), 0, false);
    return uinfo;
}

//
// Constructor for EvpnRemoteMcastNode.
//
// Need to notify all Broadcast MAC routes so that their ingress replication
// OLists get updated.
//
EvpnRemoteMcastNode::EvpnRemoteMcastNode(EvpnManagerPartition *partition,
    EvpnRoute *route)
    : EvpnMcastNode(partition, route, EvpnMcastNode::RemoteNode) {
    const BgpAttr *attr = route->BestPath()->GetAttr();
    const PmsiTunnel *pmsi_tunnel = attr->pmsi_tunnel();
    if ((pmsi_tunnel->tunnel_flags &
         PmsiTunnelSpec::EdgeReplicationSupported) == 0) {
        edge_replication_not_supported_ = true;
    }

    partition_->NotifyBroadcastMacRoutes();
}

//
// Destructor for EvpnRemoteMcastNode.
//
// Need to notify all Broadcast MAC routes so that their ingress replication
// OLists get updated.
//
EvpnRemoteMcastNode::~EvpnRemoteMcastNode() {
    partition_->NotifyBroadcastMacRoutes();
}

//
// Update the attributes for a EvpnRemoteMcastNode.
//
// Update common attributes and edge_replication_not_supported_.
// Returns true if anything changed.
//
bool EvpnRemoteMcastNode::Update(EvpnRoute *route) {
    bool changed = EvpnMcastNode::Update(route);

    const BgpAttr *attr = route->BestPath()->GetAttr();
    const PmsiTunnel *pmsi_tunnel = attr->pmsi_tunnel();
    bool edge_replication_not_supported = false;
    if ((pmsi_tunnel->tunnel_flags &
         PmsiTunnelSpec::EdgeReplicationSupported) == 0) {
        edge_replication_not_supported = true;
    }
    if (edge_replication_not_supported != edge_replication_not_supported_) {
        edge_replication_not_supported_ = edge_replication_not_supported;
        changed = true;
    }
    return changed;
}

//
// Handle update of EvpnRemoteMcastNode.
//
void EvpnRemoteMcastNode::TriggerUpdate() {
    partition_->NotifyBroadcastMacRoutes();
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
// Go through all EvpnLocalMcastNodes and notify the associated Broadcast MAC
// route.
//
void EvpnManagerPartition::NotifyBroadcastMacRoutes() {
    DBTablePartition *tbl_partition = GetTablePartition();
    BOOST_FOREACH(EvpnMcastNode *local_node, local_mcast_node_list_) {
        tbl_partition->Notify(local_node->route());
    }
}

//
// Add an EvpnMcastNode to the EvpnManagerPartition.
//
void EvpnManagerPartition::AddMcastNode(EvpnMcastNode *node) {
    if (node->type() == EvpnMcastNode::LocalNode) {
        local_mcast_node_list_.insert(node);
    } else {
        remote_mcast_node_list_.insert(node);
    }
}

//
// Delete an EvpnMcastNode from the EvpnManagerPartition.
//
void EvpnManagerPartition::DeleteMcastNode(EvpnMcastNode *node) {
    if (node->type() == EvpnMcastNode::LocalNode) {
        local_mcast_node_list_.erase(node);
    } else {
        remote_mcast_node_list_.erase(node);
    }
}

//
// Return true if the EvpnManagerPartition is empty i.e. it has no local or
// remote EvpnMcastNodes.
//
bool EvpnManagerPartition::empty() const {
    return local_mcast_node_list_.empty() && remote_mcast_node_list_.empty();
}

//
// Return the BgpServer for the EvpnManagerPartition.
//
BgpServer *EvpnManagerPartition::server() {
    return evpn_manager_->server();
}

//
// Constructor for EvpnManager.
//
EvpnManager::EvpnManager(EvpnTable *table)
    : table_(table), table_delete_ref_(this, table->deleter()) {
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
        boost::bind(&EvpnManager::RouteListener, this, _1, _2));
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

            // Delete the EvpnLocalMcastNode associated with the route.
            route->ClearState(table_, listener_id_);
            partition->DeleteMcastNode(node);
            delete node;

        } else if (node->Update(route)) {

            // Trigger update of the EvpnMcastNode.
            node->TriggerUpdate();
        }
    }
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
    deleter()->RetryDelete();
}

//
// Return the LifetimeActor for the EvpnManager.
//
LifetimeActor *EvpnManager::deleter() {
    return deleter_.get();
}
