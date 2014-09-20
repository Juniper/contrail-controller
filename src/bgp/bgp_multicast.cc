/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/bgp_multicast.h"

#include <boost/bind.hpp>

#include "base/task_annotations.h"
#include "base/util.h"
#include "bgp/bgp_path.h"
#include "bgp/bgp_route.h"
#include "bgp/ipeer.h"
#include "bgp/ermvpn/ermvpn_table.h"
#include "bgp/origin-vn/origin_vn.h"
#include "bgp/routing-instance/routing_instance.h"

class McastTreeManager::DeleteActor : public LifetimeActor {
public:
    DeleteActor(McastTreeManager *tree_manager)
        : LifetimeActor(tree_manager->table_->routing_instance()->server()->
                lifetime_manager()),
          tree_manager_(tree_manager) {
    }
    virtual ~DeleteActor() {
    }

    virtual bool MayDelete() const {
        return tree_manager_->MayDelete();
    }

    virtual void Shutdown() {
        tree_manager_->Shutdown();
    }

    virtual void Destroy() {
        tree_manager_->table_->DestroyTreeManager();
    }

private:
    McastTreeManager *tree_manager_;
};

//
// Constructor for McastForwarder.  The level is determined by the route type.
// We get the address of the forwarder and the label_block from the attributes
// of the active path.  The LabelBLockPtr needs to be copied so that we can
// release the label when processing a delete notification - we won't have the
// path at that point.
//
// The RD will be zero for BGP learnt routes and the RouterId will be zero for
// XMPP learnt routes.
//
McastForwarder::McastForwarder(McastSGEntry *sg_entry, ErmVpnRoute *route)
    : sg_entry_(sg_entry),
      route_(route),
      global_tree_route_(NULL),
      label_(0),
      address_(0),
      rd_(route->GetPrefix().route_distinguisher()),
      router_id_(route->GetPrefix().router_id()) {
    const BgpPath *path = route->BestPath();
    const BgpAttr *attr = path->GetAttr();

    if (route_->GetPrefix().type() == ErmVpnPrefix::NativeRoute) {
        level_ = McastTreeManager::LevelNative;
        address_ = attr->nexthop().to_v4();
        label_block_ = attr->label_block();
    } else {
        level_ = McastTreeManager::LevelLocal;
        const EdgeDiscovery::Edge *edge = attr->edge_discovery()->edge_list[0];
        address_ = edge->address;
        label_block_ = edge->label_block;
    }

    if (path->GetAttr()->ext_community())
        encap_ = path->GetAttr()->ext_community()->GetTunnelEncap();
}

//
// Destructor for McastForwarder. Flushes forward and reverse links to and
// from other McastForwarders.
//
McastForwarder::~McastForwarder() {
    DeleteGlobalTreeRoute();
    FlushLinks();
    ReleaseLabel();
}

//
// Update the McastForwarder based on information in the ErmVpnRoute.
// Return true if something changed.
//
bool McastForwarder::Update(ErmVpnRoute *route) {
    McastForwarder forwarder(sg_entry_, route);

    bool changed = false;
    if (label_block_ != forwarder.label_block_) {
        ReleaseLabel();
        label_block_ = forwarder.label_block_;
        changed = true;
    }
    if (address_ != forwarder.address_) {
        address_ = forwarder.address_;
        changed = true;
    }
    if (encap_ != forwarder.encap_) {
        encap_ = forwarder.encap_;
        changed = true;
    }

    return changed;
}

//
// Printable string for McastForwarder.
//
std::string McastForwarder::ToString() const {
    if (level_ == McastTreeManager::LevelNative) {
        return rd_.ToString() + " -> " + integerToString(label_);
    } else {
        return router_id_.to_string() + " -> " + integerToString(label_);
    }
}

//
// Find a link to the given McastForwarder.
//
McastForwarder *McastForwarder::FindLink(McastForwarder *forwarder) {
    for (McastForwarderList::iterator it = tree_links_.begin();
         it != tree_links_.end(); ++it) {
        if (*it == forwarder) return forwarder;
    }
    return NULL;
}

//
// Add a link to the given McastForwarder.
//
void McastForwarder::AddLink(McastForwarder *forwarder) {
    assert(!FindLink(forwarder));
    tree_links_.push_back(forwarder);
}

//
// Remove a link to the given McastForwarder.
//
void McastForwarder::RemoveLink(McastForwarder *forwarder) {
    for (McastForwarderList::iterator it = tree_links_.begin();
         it != tree_links_.end(); ++it) {
        if (*it == forwarder) {
            tree_links_.erase(it);
            return;
        }
    }
}

//
// Flush all links from this McastForwarder.  Takes care of removing the
// reverse links as well.
//
void McastForwarder::FlushLinks() {
    for (McastForwarderList::iterator it = tree_links_.begin();
         it != tree_links_.end(); ++it) {
        (*it)->RemoveLink(this);
    }
    tree_links_.clear();
}

//
// Allocate a label for this McastForwarder.  The label gets allocated from
// the LabelBlock corresponding to the label range advertised by the peer.
// This is used when updating the distribution tree for the McastSGEntry to
// this McastForwarder belongs.
//
void McastForwarder::AllocateLabel() {
    label_ = label_block_->AllocateLabel();
}

//
// Release the label, if any, for this McastForwarder. This is required when
// updating the distribution tree for the McastSGEntry to which we belong.
//
void McastForwarder::ReleaseLabel() {
    if (label_ != 0) {
        label_block_->ReleaseLabel(label_);
        label_ = 0;
    }
}

//
// Add the GlobalTreeRoute for this McastForwarder. The GlobalTreeRoute is
// used by the tree builder to tell the associated control-node about the
// forwarding edges for Native McastForwarders attached it.
//
void McastForwarder::AddGlobalTreeRoute() {
    assert(level_ == McastTreeManager::LevelLocal);
    assert(!global_tree_route_);

    // Bail if there's no distribution tree.
    if (label_ == 0 || tree_links_.empty())
        return;

    // Bail if we can't build a source RD.
    if (sg_entry_->GetSourceRd().IsZero())
        return;

    // Construct the prefix and route key.
    BgpTable *table = static_cast<BgpTable *>(route_->get_table());
    ErmVpnPrefix prefix(ErmVpnPrefix::GlobalTreeRoute,
        RouteDistinguisher::kZeroRd, router_id_,
        sg_entry_->group(), sg_entry_->source());
    ErmVpnRoute rt_key(prefix);

    // Find or create the route.
    McastManagerPartition *partition = sg_entry_->partition();
    DBTablePartition *tbl_partition =
        static_cast<DBTablePartition *>(partition->GetTablePartition());
    ErmVpnRoute *route =
        static_cast<ErmVpnRoute *>(tbl_partition->Find(&rt_key));
    if (!route) {
        route = new ErmVpnRoute(prefix);
        tbl_partition->Add(route);
    } else {
        route->ClearDelete();
    }

    // Build the attributes.  Need to go through the tree links to build the
    // EdgeForwardingSpec.
    const RoutingInstance *rt_instance = table->routing_instance();
    BgpServer *server = table->routing_instance()->server();
    BgpAttrSpec attr_spec;
    BgpAttrNextHop nexthop(server->bgp_identifier());
    attr_spec.push_back(&nexthop);
    BgpAttrSourceRd source_rd(sg_entry_->GetSourceRd());
    attr_spec.push_back(&source_rd);
    ExtCommunitySpec extcomm_spec;
    OriginVn origin_vn(server->autonomous_system(),
        rt_instance->virtual_network_index());
    extcomm_spec.communities.push_back(origin_vn.GetExtCommunityValue());
    attr_spec.push_back(&extcomm_spec);
    EdgeForwardingSpec efspec;
    for (McastForwarderList::const_iterator it = tree_links_.begin();
         it != tree_links_.end(); ++it) {
        EdgeForwardingSpec::Edge *edge = new EdgeForwardingSpec::Edge;
        edge->SetInboundIp4Address(address_);
        edge->inbound_label = label_;
        edge->SetOutboundIp4Address((*it)->address());
        edge->outbound_label = (*it)->label();
        efspec.edge_list.push_back(edge);
    }
    attr_spec.push_back(&efspec);
    BgpAttrPtr attr = server->attr_db()->Locate(attr_spec);

    // Add a path with source BgpPath::Local.
    BgpPath *path = new BgpPath(0, BgpPath::Local, attr);
    route->InsertPath(path);
    tbl_partition->Notify(route);
    global_tree_route_ = route;
}

//
// Delete the GlobalTreeRoute for this McastForwarder.
//
void McastForwarder::DeleteGlobalTreeRoute() {
    if (!global_tree_route_)
        return;

    McastManagerPartition *partition = sg_entry_->partition();
    DBTablePartition *tbl_partition =
        static_cast<DBTablePartition *>(partition->GetTablePartition());
    global_tree_route_->RemovePath(BgpPath::Local);

    if (!global_tree_route_->BestPath()) {
        tbl_partition->Delete(global_tree_route_);
    } else {
        tbl_partition->Notify(global_tree_route_);
    }
    global_tree_route_ = NULL;
}

//
// Append list of BgpOListElems from the Local tree to the BgpOListPtr. The
// list is built based on the tree links in this McastForwarder.
//
void McastForwarder::AddLocalOListElems(BgpOListPtr olist) {
    assert(level_ == McastTreeManager::LevelNative);

    for (McastForwarderList::const_iterator it = tree_links_.begin();
         it != tree_links_.end(); ++it) {
        BgpOListElem elem((*it)->address(), (*it)->label(), (*it)->encap());
        olist->elements.push_back(elem);
    }
}

//
// Append list of BgpOListElems from the Global tree to the BgpOListPtr. The
// list is built based on EdgeForwarding attribute in the GlobalTreeRoute.
//
void McastForwarder::AddGlobalOListElems(BgpOListPtr olist) {
    assert(level_ == McastTreeManager::LevelNative);

    // Bail if this is not the forest node for the Local tree.
    if (!sg_entry_->IsForestNode(this))
        return;

    const ErmVpnRoute *route = sg_entry_->tree_result_route();
    if (!route)
        return;

    const BgpPath *path = route->BestPath();
    if (!path)
        return;

    // Go through each forwarding edge and add it to the list.
    const EdgeForwarding *eforwarding = path->GetAttr()->edge_forwarding();
    for (EdgeForwarding::EdgeList::const_iterator it =
         eforwarding->edge_list.begin(); it != eforwarding->edge_list.end();
         ++it) {
        const EdgeForwarding::Edge *edge = *it;
        if (edge->inbound_address == address_) {
            BgpOListElem elem(edge->outbound_address, edge->outbound_label);
            olist->elements.push_back(elem);
        }
    }
}

//
// Construct an UpdateInfo with the RibOutAttr that needs to be advertised to
// the IPeer for the ErmVpnRoute associated with this McastForwarder. This is
// used the Export method of the ErmVpnTable.  It is expected that the caller
// fills in the target RibPeerSet in the UpdateInfo.
//
// The main functionality here is to transform the McastForwarderList for the
// distribution tree and the EdgeForwarding attribute from the GlobalTreeRoute
// into a BgpOList.
//
UpdateInfo *McastForwarder::GetUpdateInfo(ErmVpnTable *table) {
    CHECK_CONCURRENCY("db::DBTable");

    assert(level_ == McastTreeManager::LevelNative);

    BgpOListPtr olist(new BgpOList);
    AddLocalOListElems(olist);
    AddGlobalOListElems(olist);

    // Bail if we've never built the tree or if the BgpOList is empty.
    if (label_ == 0 || olist->elements.empty())
        return NULL;

    BgpAttrOList olist_attr = BgpAttrOList(olist);
    BgpAttrSpec attr_spec;
    attr_spec.push_back(&olist_attr);
    BgpAttrPtr attr = table->server()->attr_db()->Locate(attr_spec);

    UpdateInfo *uinfo = new UpdateInfo;
    uinfo->roattr = RibOutAttr(attr.get(), label_);
    return uinfo;
}

//
// Constructor for McastSGEntry.
//
McastSGEntry::McastSGEntry(McastManagerPartition *partition,
        Ip4Address group, Ip4Address source)
    : partition_(partition),
      group_(group),
      source_(source),
      forest_node_(NULL),
      local_tree_route_(NULL),
      tree_result_route_(NULL),
      on_work_queue_(false) {
    for (int level = McastTreeManager::LevelFirst;
         level < McastTreeManager::LevelCount; ++level) {
        ForwarderSet *forwarders = new ForwarderSet;
        forwarder_sets_.push_back(forwarders);
        update_needed_.push_back(false);
    }
}

//
// Destructor for McastSGEntry.
//
McastSGEntry::~McastSGEntry() {
    STLDeleteValues(&forwarder_sets_);
}

//
// Printable string for McastSGEntry.
//
std::string McastSGEntry::ToString() const {
    return group_.to_string() + "," + source_.to_string();
}

//
// Add the given McastForwarder under this McastSGEntry and trigger update
// of the distribution tree.
//
void McastSGEntry::AddForwarder(McastForwarder *forwarder) {
    uint8_t level = forwarder->level();
    forwarder_sets_[level]->insert(forwarder);
    update_needed_[level] = true;
    partition_->EnqueueSGEntry(this);
}

//
// Handle change for the given McastForwarder under this McastSGEntry. Trigger
// update of the distribution tree.
//
// Note that this method only handles the change = the caller determines that
// there has been a change.
//
void McastSGEntry::ChangeForwarder(McastForwarder *forwarder) {
    uint8_t level = forwarder->level();
    update_needed_[level] = true;
    partition_->EnqueueSGEntry(this);
}

//
// Delete the given McastForwarder from this McastSGEntry and trigger update
// of the distribution tree.
//
void McastSGEntry::DeleteForwarder(McastForwarder *forwarder) {
    if (forwarder == forest_node_)
        forest_node_ = NULL;
    uint8_t level = forwarder->level();
    forwarder_sets_[level]->erase(forwarder);
    update_needed_[level] = true;
    partition_->EnqueueSGEntry(this);
}

//
// Get the SourceRD to be used when adding [Local|Global]TreeRoutes.  This
// SourceRD gets used as the RD when the ErmVpnRoute is replicated from the
// VRF table to the VPN table.
//
// We simply use the RD for the forest node.
//
RouteDistinguisher McastSGEntry::GetSourceRd() {
    if (!forest_node_)
        return RouteDistinguisher::kZeroRd;
    return forest_node_->route()->GetPrefix().route_distinguisher();
}

//
// Add the LocalTreeRoute for this McastSGEntry.  This route advertises a set
// of candidate edges from McastForwarders attached to this control-node that
// can be used by the tree builder to build the higher level tree.  We simply
// advertise edges McastTreeManager::kDegree - 1 edges from the forest node.
//
// We advertise kDegree-1 candidate edges via the EdgeDiscovery attribute. All
// the edges are for the forest node for the tree of native McastForwarders.
// The label block for each edge in the EdgeDiscovery attribute is of size 1 -
// this is label that has been allocated for the forest node.  Using a single
// label is acceptable because the tree builder algorithm does not change the
// relative order of nodes in the tree.
//
void McastSGEntry::AddLocalTreeRoute() {
    assert(!forest_node_);
    assert(!local_tree_route_);

    // Select the last leaf in the distribution tree as the forest node.
    uint8_t level = McastTreeManager::LevelNative;
    ForwarderSet *forwarders = forwarder_sets_[level];
    if (forwarders->rbegin() == forwarders->rend())
        return;
    forest_node_ = *forwarders->rbegin();

    // Construct the prefix and route key.
    BgpServer *server = partition_->server();
    Ip4Address router_id(server->bgp_identifier());
    ErmVpnPrefix prefix(ErmVpnPrefix::LocalTreeRoute,
        RouteDistinguisher::kZeroRd, router_id, group_, source_);
    ErmVpnRoute rt_key(prefix);

    // Find or create the route.
    DBTablePartition *tbl_partition =
        static_cast<DBTablePartition *>(partition_->GetTablePartition());
    ErmVpnRoute *route =
        static_cast<ErmVpnRoute *>(tbl_partition->Find(&rt_key));
    if (!route) {
        route = new ErmVpnRoute(prefix);
        tbl_partition->Add(route);
    } else {
        route->ClearDelete();
    }

    // Build the attributes.
    const RoutingInstance *rt_instance = partition_->routing_instance();
    BgpAttrSpec attr_spec;
    BgpAttrNextHop nexthop(server->bgp_identifier());
    attr_spec.push_back(&nexthop);
    BgpAttrSourceRd source_rd(GetSourceRd());
    attr_spec.push_back(&source_rd);
    ExtCommunitySpec extcomm_spec;
    OriginVn origin_vn(server->autonomous_system(),
        rt_instance->virtual_network_index());
    extcomm_spec.communities.push_back(origin_vn.GetExtCommunityValue());
    attr_spec.push_back(&extcomm_spec);
    EdgeDiscoverySpec edspec;
    for (int idx = 1; idx <= McastTreeManager::kDegree - 1; ++idx) {
        EdgeDiscoverySpec::Edge *edge = new EdgeDiscoverySpec::Edge;
        edge->SetIp4Address(forest_node_->address());
        edge->SetLabels(forest_node_->label(), forest_node_->label());
        edspec.edge_list.push_back(edge);
    }
    attr_spec.push_back(&edspec);
    BgpAttrPtr attr = server->attr_db()->Locate(attr_spec);

    // Add a path with source BgpPath::Local.
    BgpPath *path = new BgpPath(0, BgpPath::Local, attr);
    route->InsertPath(path);
    tbl_partition->Notify(route);
    local_tree_route_ = route;
}

//
// Delete the LocalTreeRoute for this McastSGEntry.
//
void McastSGEntry::DeleteLocalTreeRoute() {
    if (!local_tree_route_)
        return;

    forest_node_ = NULL;
    DBTablePartition *tbl_partition =
        static_cast<DBTablePartition *>(partition_->GetTablePartition());
    local_tree_route_->RemovePath(BgpPath::Local);
    if (!local_tree_route_->BestPath()) {
        tbl_partition->Delete(local_tree_route_);
    } else {
        tbl_partition->Notify(local_tree_route_);
    }
    local_tree_route_ = NULL;
}

//
// Update the LocalTreeRoute for this McastSGEntry if RouterId has changed.
//
void McastSGEntry::UpdateLocalTreeRoute() {
    if (!local_tree_route_)
        return;

    // Bail if the RouterId hasn't changed.
    const BgpServer *server = partition_->server();
    Ip4Address router_id = local_tree_route_->GetPrefix().router_id();
    if (router_id.to_ulong() == server->bgp_identifier())
        return;

    // Add and delete the route.
    DeleteLocalTreeRoute();
    AddLocalTreeRoute();
}

//
// Update relevant [Local|Global]TreeRoutes for the McastSGEntry.
//
void McastSGEntry::UpdateRoutes(uint8_t level) {
    if (level == McastTreeManager::LevelNative) {
        DeleteLocalTreeRoute();
        AddLocalTreeRoute();
    } else {
        ForwarderSet *forwarders = forwarder_sets_[level];
        for (ForwarderSet::iterator it = forwarders->begin();
             it != forwarders->end(); ++it) {
            (*it)->DeleteGlobalTreeRoute();
            (*it)->AddGlobalTreeRoute();
        }
    }
}

//
// Implement tree builder election.
//
bool McastSGEntry::IsTreeBuilder(uint8_t level) {
    if (level == McastTreeManager::LevelNative)
        return true;

    ForwarderSet *forwarders = forwarder_sets_[level];
    ForwarderSet::iterator it = forwarders->begin();
    if (it == forwarders->end())
        return false;

    Ip4Address router_id(partition_->server()->bgp_identifier());
    if ((*it)->router_id() != router_id)
        return false;

    return true;
}

//
//
// Update specified distribution tree for the McastSGEntry.  We traverse all
// McastForwarders in sorted order and arrange them in breadth first fashion
// in a k-ary tree.  Building the tree in this manner guarantees that we get
// the same tree for a given set of forwarders, independent of the order in
// in which they joined. This predictability is deemed to be more important
// than other criteria such as minimizing disruption of traffic, minimizing
// the cost/weight of the tree etc.
//
void McastSGEntry::UpdateTree(uint8_t level) {
    CHECK_CONCURRENCY("db::DBTable");

    if (!update_needed_[level])
        return;
    update_needed_[level] = false;

    int degree;
    if (level == McastTreeManager::LevelNative) {
        degree = McastTreeManager::kDegree;
    } else {
        degree = McastTreeManager::kDegree - 1;
    }

    // First get rid of the previous distribution tree and enqueue all the
    // associated ErmVpnRoutes for notification. Note that DBListeners will
    // not get invoked until after this routine is done.
    ForwarderSet *forwarders = forwarder_sets_[level];
    for (ForwarderSet::iterator it = forwarders->begin();
         it != forwarders->end(); ++it) {
       (*it)->FlushLinks();
       (*it)->ReleaseLabel();
       partition_->GetTablePartition()->Notify((*it)->route());
    }

    // Bail if we're not the tree builder.
    if (!IsTreeBuilder(level)) {
        UpdateRoutes(level);
        return;
    }

    // Create a vector of pointers to the McastForwarders in sorted order.
    // We do this because std::set doesn't support random access iterators.
    McastForwarderList vec;
    vec.reserve(forwarders->size());
    for (ForwarderSet::iterator it = forwarders->begin();
         it != forwarders->end(); ++it) {
        vec.push_back(*it);
    }

    // Go through each McastForwarder in the vector and link it to it's parent
    // McastForwarder in the k-ary tree. We also add a link from the parent to
    // the entry in question.
    for (McastForwarderList::iterator it = vec.begin(); it != vec.end(); ++it) {
        (*it)->AllocateLabel();
        int idx = it - vec.begin();
        if (idx == 0)
            continue;

        int parent_idx = (idx - 1) / degree;
        McastForwarderList::iterator parent_it = vec.begin() + parent_idx;
        assert(parent_it != vec.end());
        (*it)->AddLink(*parent_it);
        (*parent_it)->AddLink(*it);
    }

    // Update [Local|Global]TreeRoutes.
    UpdateRoutes(level);
}

//
// Update distribution trees for both levels.
//
void McastSGEntry::UpdateTree() {
    for (uint8_t level = McastTreeManager::LevelFirst;
         level < McastTreeManager::LevelCount; ++level) {
        UpdateTree(level);
    }
}

//
// Trigger notification of the ErmVpnRoute associated with the McastForwarder
// that is the forest node. This is used to trigger a rebuild of the BgpOlist
// when the GlobalTreeRoute is updated.
//
void McastSGEntry::NotifyForestNode() {
    if (!forest_node_)
        return;
    partition_->GetTablePartition()->Notify(forest_node_->route());
}

bool McastSGEntry::IsForestNode(McastForwarder *forwarder) {
    return (forwarder == forest_node_);
}

bool McastSGEntry::empty() const {
    if (local_tree_route_ || tree_result_route_)
        return false;
    if (!forwarder_sets_[McastTreeManager::LevelNative]->empty())
        return false;
    if (!forwarder_sets_[McastTreeManager::LevelLocal]->empty())
        return false;
    return true;
}

//
// Constructor for McastManagerPartition.
//
McastManagerPartition::McastManagerPartition(McastTreeManager *tree_manager,
        size_t part_id)
    : tree_manager_(tree_manager),
      part_id_(part_id),
      update_count_(0),
      work_queue_(TaskScheduler::GetInstance()->GetTaskId("db::DBTable"),
              part_id_,
              boost::bind(&McastManagerPartition::ProcessSGEntry, this, _1)) {
}

//
// Destructor for McastManagerPartition.
//
McastManagerPartition::~McastManagerPartition() {
    work_queue_.Shutdown();
}

//
// Find the McastSGEntry for the given group and source.
//
McastSGEntry *McastManagerPartition::FindSGEntry(
        Ip4Address group, Ip4Address source) {
    McastSGEntry temp_sg_entry(this, group, source);
    SGList::iterator it = sg_list_.find(&temp_sg_entry);
    return (it != sg_list_.end() ? *it : NULL);
}

//
// Find or create the McastSGEntry for the given group and source.
//
McastSGEntry *McastManagerPartition::LocateSGEntry(
        Ip4Address group, Ip4Address source) {
    McastSGEntry *sg_entry = FindSGEntry(group, source);
    if (!sg_entry) {
        sg_entry = new McastSGEntry(this, group, source);
        sg_list_.insert(sg_entry);
    }
    return sg_entry;
}

//
// Enqueue the given McastSGEntry on the WorkQueue if it's not already on it.
//
void McastManagerPartition::EnqueueSGEntry(McastSGEntry *sg_entry) {
    if (sg_entry->on_work_queue())
        return;
    work_queue_.Enqueue(sg_entry);
    sg_entry->set_on_work_queue();
}

//
// Callback for the WorkQueue. Updates distribution trees for the McastSGEntry.
// Also gets rid of the McastSGEntry if it is eligible to be deleted.
//
bool McastManagerPartition::ProcessSGEntry(McastSGEntry *sg_entry) {
    CHECK_CONCURRENCY("db::DBTable");

    sg_entry->clear_on_work_queue();
    sg_entry->UpdateTree();
    update_count_++;

    if (sg_entry->empty()) {
        sg_list_.erase(sg_entry);
        delete sg_entry;
    }

    if (sg_list_.empty())
        tree_manager_->RetryDelete();

    return true;
}

//
// Get the DBTablePartBase for the ErmVpnTable for our partition id.
//
DBTablePartBase *McastManagerPartition::GetTablePartition() {
    return tree_manager_->GetTablePartition(part_id_);
}

const RoutingInstance *McastManagerPartition::routing_instance() const {
    return tree_manager_->table()->routing_instance();
}

BgpServer *McastManagerPartition::server() {
    return tree_manager_->table()->server();
}

//
// Constructor for McastTreeManager.
//
McastTreeManager::McastTreeManager(ErmVpnTable *table)
    : table_(table), table_delete_ref_(this, table->deleter()) {
    deleter_.reset(new DeleteActor(this));
}

//
// Destructor for McastTreeManager.
//
McastTreeManager::~McastTreeManager() {
}

//
// Initialize the McastTreeManager. We allocate the McastManagerPartitions
// and register a DBListener for the ErmVpnTable.
//
void McastTreeManager::Initialize() {
    AllocPartitions();
    listener_id_ = table_->Register(
        boost::bind(&McastTreeManager::RouteListener, this, _1, _2));
}

//
// Terminate the McastTreeManager. We free the McastManagerPartitions
// and unregister from the ErmVpnTable.
//
void McastTreeManager::Terminate() {
    table_->Unregister(listener_id_);
    FreePartitions();
}

//
// Allocate the McastManagerPartitions.
//
void McastTreeManager::AllocPartitions() {
    for (int part_id = 0; part_id < DB::PartitionCount(); part_id++) {
        partitions_.push_back(new McastManagerPartition(this, part_id));
    }
}

//
// Free the McastManagerPartitions.
//
void McastTreeManager::FreePartitions() {
    for (size_t part_id = 0; part_id < partitions_.size(); part_id++) {
        delete partitions_[part_id];
    }
    partitions_.clear();
}

McastManagerPartition *McastTreeManager::GetPartition(int part_id) {
    return partitions_[part_id];
}

//
// Get the DBTablePartBase for the ErmVpnTable for given partition id.
//
DBTablePartBase *McastTreeManager::GetTablePartition(size_t part_id) {
    return table_->GetTablePartition(part_id);
}

//
// Construct export state for the given ErmVpnRoute. Note that the route
// only needs to be exported to the IPeer from which it was learnt.
//
UpdateInfo *McastTreeManager::GetUpdateInfo(ErmVpnRoute *route) {
    CHECK_CONCURRENCY("db::DBTable");

    DBState *dbstate = route->GetState(table_, listener_id_);
    McastForwarder *forwarder = dynamic_cast<McastForwarder *>(dbstate);

    if (!forwarder)
        return NULL;

    return forwarder->GetUpdateInfo(table_);
}

//
// DBListener callback handler for Native and Local routes in the ErmVpnTable.
// It creates, updates or deletes the associated McastForwarder as appropriate.
//
// Creates a McastSGEntry if one doesn't already exist. However, McastSGEntrys
// don't get deleted from here.  They only get deleted from WorkQueue callback
// routine i.e. McastManagerPartition::ProcessSGEntry.
//
void McastTreeManager::TreeNodeListener(McastManagerPartition *partition,
        ErmVpnRoute *route) {
    CHECK_CONCURRENCY("db::DBTable");

    DBState *dbstate = route->GetState(table_, listener_id_);
    if (!dbstate) {

        // We have no previous DBState for this route.
        // Bail if the route is not valid.
        if (!route->IsValid())
            return;

        // Create a new McastForwarder and associate it with the route.
        McastSGEntry *sg_entry = partition->LocateSGEntry(
            route->GetPrefix().group(), route->GetPrefix().source());
        McastForwarder *forwarder = new McastForwarder(sg_entry, route);
        sg_entry->AddForwarder(forwarder);
        route->SetState(table_, listener_id_, forwarder);

        // Update local tree route if our RouterId has changed. Ideally,
        // we should trigger an update of all local trees routes when we
        // detect a change in RouterId. Instead, we currently check and
        // update the local route when we detect a new local route from
        // another node.
        if (route->GetPrefix().type() == ErmVpnPrefix::LocalTreeRoute)
            sg_entry->UpdateLocalTreeRoute();

    } else {

        McastSGEntry *sg_entry = partition->FindSGEntry(
            route->GetPrefix().group(), route->GetPrefix().source());
        assert(sg_entry);
        McastForwarder *forwarder = dynamic_cast<McastForwarder *>(dbstate);
        assert(forwarder);

        if (!route->IsValid()) {

            // Delete the McastForwarder associated with the route.
            route->ClearState(table_, listener_id_);
            sg_entry->DeleteForwarder(forwarder);
            delete forwarder;

        } else if (forwarder->Update(route)) {

            // Trigger update of the distribution tree.
            sg_entry->ChangeForwarder(forwarder);
        }

    }
}

//
// DBListener callback handler for GlobalTreeRoutes in the ErmVpnTable. It
// updates the tree_result_route_ and triggers re-evaluation of the forest
// node McastForwarder's BgpOlist.
//
void McastTreeManager::TreeResultListener(McastManagerPartition *partition,
        ErmVpnRoute *route) {
    CHECK_CONCURRENCY("db::DBTable");

    DBState *dbstate = route->GetState(table_, listener_id_);
    if (!dbstate) {

        // We have no previous DBState for this route.
        // Bail if the route is not valid.
        if (!route->IsValid())
            return;

        // Ignore GlobalTreeRoute if it's not applicable to this control-node.
        BgpServer *server = table_->routing_instance()->server();
        if (route->GetPrefix().router_id().to_ulong() !=
            server->bgp_identifier())
            return;

        McastSGEntry *sg_entry = partition->LocateSGEntry(
            route->GetPrefix().group(), route->GetPrefix().source());
        route->SetState(table_, listener_id_, sg_entry);
        sg_entry->set_tree_result_route(route);
        sg_entry->NotifyForestNode();

    } else {

        McastSGEntry *sg_entry = dynamic_cast<McastSGEntry *>(dbstate);
        assert(sg_entry);

        if (!route->IsValid()) {
            sg_entry->clear_tree_result_route();
            route->ClearState(table_, listener_id_);
            partition->EnqueueSGEntry(sg_entry);
        }
        sg_entry->NotifyForestNode();
    }
}

//
// DBListener callback handler for the ErmVpnTable. GlobalTreeRoutes provide
// result information and hence are handled differently than Native and Local
// routes, which result in update of a McastForwarder.
//
void McastTreeManager::RouteListener(
        DBTablePartBase *tpart, DBEntryBase *db_entry) {
    CHECK_CONCURRENCY("db::DBTable");

    McastManagerPartition *partition = partitions_[tpart->index()];
    ErmVpnRoute *route = dynamic_cast<ErmVpnRoute *>(db_entry);
    if (route->GetPrefix().type() == ErmVpnPrefix::GlobalTreeRoute) {
        TreeResultListener(partition, route);
    } else {
        TreeNodeListener(partition, route);
    }
}


//
// Check if the McastTreeManager can be deleted. This can happen only if all
// the McastManagerPartitions are empty.
//
bool McastTreeManager::MayDelete() const {
    CHECK_CONCURRENCY("bgp::Config");

    for (PartitionList::const_iterator it = partitions_.begin();
         it != partitions_.end(); ++it) {
        if (!(*it)->empty())
            return false;
    }

    return true;
}

//
// Initiate shutdown for the McastTreeManager.
//
void McastTreeManager::Shutdown() {
    CHECK_CONCURRENCY("bgp::Config");
}

//
// Trigger deletion of the McastTreeManager and propagate the delete to any
// dependents.
//
void McastTreeManager::ManagedDelete() {
    deleter_->Delete();
}

//
// Attempt to enqueue a delete for the McastTreeManager.
//
void McastTreeManager::RetryDelete() {
    if (!deleter()->IsDeleted())
        return;
    deleter()->RetryDelete();
}

//
// Return the LifetimeActor for the McastTreeManager.
//
LifetimeActor *McastTreeManager::deleter() {
    return deleter_.get();
}

