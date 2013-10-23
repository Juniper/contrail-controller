/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/bgp_config_listener.h"

#include <boost/assign/list_of.hpp>
#include <boost/bind.hpp>

#include "base/task_annotations.h"
#include "bgp/bgp_config.h"
#include "db/db.h"
#include "ifmap/ifmap_link.h"
#include "ifmap/ifmap_node.h"
#include "ifmap/ifmap_table.h"

class DBTablePartBase;

using namespace boost::assign;
using namespace std;

//
// Initialize the policy for the DependencyTracker.
//
BgpConfigListener::DependencyTracker::DependencyTracker(
    BgpConfigListener *listener) : listener_(listener) {
    Initialize();
}

//
// Populate the policy with entries for each interesting identifier type.
//
// Note that identifier types and metadata types will have the same name
// when there's a link with attributes.  This happens because we represent
// such links with a "middle node" which stores all the attributes and add
// plain links between the original nodes and the middle node. An example
// is "bgp-peering".
//
// Additional unit tests should be added to bgp_config_listener_test.cc as
// and when this policy is modified.
//
void BgpConfigListener::DependencyTracker::Initialize() {
    ReactionMap bgp_peering_react = map_list_of<string, PropagateList>
        ("bgp-peering", list_of("self"));
    policy_.insert(make_pair("bgp-peering", bgp_peering_react));

    ReactionMap bgp_router_react = map_list_of<string, PropagateList>
        ("self", list_of("bgp-peering"));
    policy_.insert(make_pair("bgp-router", bgp_router_react));

    ReactionMap rt_instance_react = map_list_of<string, PropagateList>
        ("instance-target", list_of("self")("connection"))
        ("connection", list_of("self"))
        ("virtual-network-routing-instance", list_of("self"));
    policy_.insert(make_pair("routing-instance", rt_instance_react));

    ReactionMap virtual_network_react = map_list_of<string, PropagateList>
        ("self", list_of("virtual-network-routing-instance"));
    policy_.insert(make_pair("virtual-network", virtual_network_react));
}

//
// Add an IFMapNode to the NodeList for further propagation if it's an
// interesting node. It's interesting if there's an entry for self in the
// ReactionMap.
//
// The node is always added to the ChangeList even if it's not interesting.
//
void BgpConfigListener::DependencyTracker::NodeEvent(
    ChangeList *change_list, IFMapNode *node) {
    CHECK_CONCURRENCY("db::DBTable");

    AddChangeEvent(change_list, node);
    if (IsInterestingEvent(node, "self")) {
        node_list_.push_back(
            make_pair(node->table()->Typename(), node->name()));
    }
}

//
// Check both the edges corresponding to the IFMapLink and add them to the
// EdgeDescriptorList if interesting. An edge is considered interesting if
// there's an entry for the metadata in the ReactionMap for the IFMapNode's
// identifier type.
//
bool BgpConfigListener::DependencyTracker::LinkEvent(const string metadata,
    IFMapNode *left, IFMapNode *right) {
    CHECK_CONCURRENCY("db::DBTable");

    bool interest = false;

    if ((left != NULL) && IsInterestingEvent(left, metadata)) {
        const char *type_left = left->table()->Typename();
        edge_list_.push_back(
            EdgeDescriptor(metadata, type_left, left->name()));
        interest = true;
    }
    if ((right != NULL) && IsInterestingEvent(right, metadata)) {
        const char *type_right = right->table()->Typename();
        edge_list_.push_back(
            EdgeDescriptor(metadata, type_right, right->name()));
        interest = true;
    }

    return interest;
}

//
// Walk the NodeList and EdgeDescriptorList and evaluate the NodeEventPolicy
// to build up the change list. The InEdgeSet is used to avoid evaluating the
// same EdgeDescriptor more than once, hence avoiding any loops in the graph.
//
void BgpConfigListener::DependencyTracker::PropagateChanges(
    ChangeList *change_list) {
    CHECK_CONCURRENCY("bgp::Config");
    InEdgeSet in_edges;

    for (NodeList::iterator iter = node_list_.begin();
         iter != node_list_.end(); ++iter) {
        IFMapTable *table =
            IFMapTable::FindTable(listener_->database(), iter->first);
        if (table == NULL) {
            continue;
        }
        IFMapNode *node = table->FindNode(iter->second);
        if ((node == NULL) || node->IsDeleted()) {
            continue;
        }
        PropagateNode(node, &in_edges, change_list);
    }

    for (EdgeDescriptorList::iterator iter = edge_list_.begin();
         iter != edge_list_.end(); ++iter) {
        const EdgeDescriptor &edge = *iter;
        IFMapTable *table =
            IFMapTable::FindTable(listener_->database(), edge.id_type);
        if (table == NULL) {
            continue;
        }
        IFMapNode *node = table->FindNode(edge.id_name);
        if ((node == NULL) || node->IsDeleted()) {
            continue;
        }
        PropagateEdge(node, edge.metadata, &in_edges, change_list);
    }
}

//
// Clear all intermediate state used during propagation. This is called after
// we're done propagating all accumulated node and edge triggers to the change
// list.
//
void BgpConfigListener::DependencyTracker::Clear() {
    CHECK_CONCURRENCY("bgp::Config");

    vertex_list_.clear();
    node_list_.clear();
    edge_list_.clear();
}

//
// Get the PropagateList for the given identifier type and metadata.
//
const BgpConfigListener::DependencyTracker::PropagateList *
BgpConfigListener::DependencyTracker::GetPropagateList(
    const string &type, const string &metadata) const {
    CHECK_CONCURRENCY("bgp::Config", "db::DBTable");

    NodeEventPolicy::const_iterator ploc = policy_.find(type);
    if (ploc == policy_.end()) {
        return NULL;
    }
    ReactionMap::const_iterator rloc = ploc->second.find(metadata);
    if (rloc == ploc->second.end()) {
        return NULL;
    }
    return &rloc->second;
}

//
// Determine if the event specified by the node and metadata is interesting.
// It's interesting if the NodeEventPolicy has non-empty propagate list for
// the event.
//
bool BgpConfigListener::DependencyTracker::IsInterestingEvent(
    const IFMapNode *node, const string &metadata) const {
    if (node->IsDeleted()) {
        return false;
    }
    return GetPropagateList(node->table()->Typename(), metadata) != NULL;
}

//
// Propagate changes for a IFMapNode on the NodeList.  The fact that it's on
// the NodeList means that the node must have been deemed interesting and so
// it's PropagateList must be non-empty.
//
void BgpConfigListener::DependencyTracker::PropagateNode(
    IFMapNode *node, InEdgeSet *in_edges, ChangeList *change_list) {
    CHECK_CONCURRENCY("bgp::Config");

    const PropagateList *plist =
        GetPropagateList(node->table()->Typename(), "self");
    assert(plist);

    // Iterate through the edges of node. If the metadata for an edge is in
    // the PropagateList, we need to propagate changes for the edge itself.
    for (DBGraphVertex::edge_iterator iter =
         node->edge_list_begin(listener_->graph());
         iter != node->edge_list_end(listener_->graph()); ++iter) {
        IFMapLink *link = static_cast<IFMapLink *>(iter.operator->());
        IFMapNode *target = static_cast<IFMapNode *>(iter.target());
        if (plist->find(link->metadata()) == plist->end()) {
            continue;
        }
        PropagateEdge(target, link->metadata(), in_edges, change_list);
    }
}

//
// Propagate changes for an edge on the EdgeDescriptorList.
//
void BgpConfigListener::DependencyTracker::PropagateEdge(
    IFMapNode *node, const string &metadata,
    InEdgeSet *in_edges, ChangeList *change_list) {
    CHECK_CONCURRENCY("bgp::Config");
    assert(!node->IsDeleted());

    // Make a bidirectional check i.e. policy terms that apply to the two
    // edges for a link must be symmetrical.
    const PropagateList *plist =
        GetPropagateList(node->table()->Typename(), metadata);
    assert(plist);

    // Skip if this edge in already in the InEdgeSet i.e. it's a duplicate.
    if (in_edges->count(make_pair(node, metadata)) > 0) {
        return;
    }

    // Add entry to InEdgeSet for loop prevention.
    in_edges->insert(make_pair(node, metadata));

    // Add the node corresponding to this edge to the change list if there's
    // an entry for self in the PropagateList.
    PropagateList::const_iterator self = plist->find("self");
    if (self != plist->end()) {
        AddChangeEvent(change_list, node);
    }

    // Iterate through the edges of node. If the metadata for an edge is in
    // the PropagateList, we need to propagate changes for the edge itself.
    for (DBGraphVertex::edge_iterator iter =
         node->edge_list_begin(listener_->graph());
         iter != node->edge_list_end(listener_->graph()); ++iter) {
        IFMapLink *link = static_cast<IFMapLink *>(iter.operator->());
        if (plist->find(link->metadata()) == plist->end()) {
            continue;
        }
        IFMapNode *target = static_cast<IFMapNode *>(iter.target());
        PropagateEdge(target, link->metadata(), in_edges, change_list);
    }
}

//
// Add the IFMapNode to the change list it's not already on there.
//
void BgpConfigListener::DependencyTracker::AddChangeEvent(
    ChangeList *change_list, IFMapNode *node) {
    CHECK_CONCURRENCY("bgp::Config", "db::DBTable");

    ostringstream identifier;
    identifier << node->table()->Typename() << ':' << node->name();
    if (vertex_list_.count(identifier.str()) > 0) {
        return;
    }
    listener_->ChangeListAdd(change_list, node);
    vertex_list_.insert(identifier.str());
}

//
// Constructor, create and initialize the DependencyTracker.
//
BgpConfigListener::BgpConfigListener(BgpConfigManager *manager)
    : manager_(manager), tracker_(new DependencyTracker(this)) {
}

//
// Destructor, empty for now.
//
BgpConfigListener::~BgpConfigListener() {
}

//
// List of identifier types that are relevant for BGP configuration.
//
// VirtualNetworks are relevant because we generate the OriginVn extended
// community based on the virtual network index in the VirtualNetwork. The
// OriginVn extended community is used for service chaining.
//
static const char *bgp_config_types[] = {
    "bgp-peering",
    "bgp-router",
    "routing-instance",
    "virtual-network",
};

//
// Initialize the BgpConfigListener.
//
// We register one listener for the IFMapLinkTable and a listener for each of
// the relevant IFMapTables.
//
void BgpConfigListener::Initialize(DB *database) {
    DBTable *link_table = static_cast<DBTable *>(
        database->FindTable("__ifmap_metadata__.0"));
    assert(link_table != NULL);

    DBTable::ListenerId id = link_table->Register(
        boost::bind(&BgpConfigListener::LinkObserver, this, _1, _2));
    table_map_.insert(make_pair(link_table->name(), id));

    const int n_types = sizeof(bgp_config_types) / sizeof(const char *);
    for (int i = 0; i < n_types; i++) {
        const char *bgp_typename = bgp_config_types[i];
        IFMapTable *table = IFMapTable::FindTable(database, bgp_typename);
        assert(table);
        DBTable::ListenerId id = table->Register(
                boost::bind(&BgpConfigListener::NodeObserver, this, _1, _2));
        table_map_.insert(make_pair(table->name(), id));
    }
}

//
// Unregister listeners for all the IFMapTables.
//
void BgpConfigListener::Terminate(DB *database) {
    for (TableMap::iterator iter = table_map_.begin();
         iter != table_map_.end(); ++iter) {
        IFMapTable *table =
            static_cast<IFMapTable *>(database->FindTable(iter->first));
        assert(table);
        table->Unregister(iter->second);
    }
    table_map_.clear();
}

//
// Get the the DB in the BgpConfigManager.
//
DB *BgpConfigListener::database() {
    return manager_->database();
}

//
// Get the DBGraph in the BgpConfigManager.
//
DBGraph *BgpConfigListener::graph() {
    return manager_->graph();
}

//
// Ask the DependencyTracker to build up the ChangeList.
//
void BgpConfigListener::GetChangeList(ChangeList *change_list) {
    CHECK_CONCURRENCY("bgp::Config");

    tracker_->PropagateChanges(&change_list_);
    tracker_->Clear();
    change_list->swap(change_list_);
}

//
// Add an IFMapNode to the ChangeList by creating a BgpConfigDelta.
//
// We take references on the IFMapNode and the IFMapObject.
//
void BgpConfigListener::ChangeListAdd(
    ChangeList *change_list, IFMapNode *node) const {
    CHECK_CONCURRENCY("bgp::Config", "db::DBTable");

    IFMapTable *table = node->table();
    TableMap::const_iterator tid = table_map_.find(table->name());
    if (tid == table_map_.end()) {
        return;
    }

    BgpConfigDelta delta;
    delta.id_type = table->Typename();
    delta.id_name = node->name();
    if (!node->IsDeleted()) {
        DBState *current = node->GetState(table, tid->second);
        if (current == NULL) {
            delta.node = IFMapNodeRef(new IFMapNodeProxy(node, tid->second));
        }
        delta.obj = IFMapObjectRef(node->GetObject());
    }
    change_list->push_back(delta);
}

//
// Callback to handle node events in the IFMapTables.
// Adds the node itself to the ChangeList and informs the DependencyTracker
// about the node event.
//
void BgpConfigListener::NodeObserver(
    DBTablePartBase *root, DBEntryBase *db_entry) {
    CHECK_CONCURRENCY("db::DBTable");

    // Ignore deleted nodes for which the configuration code doesn't hold
    // state. This is the case with BgpRouter objects other than the local
    // node.
    IFMapNode *node = static_cast<IFMapNode *>(db_entry);
    if (node->IsDeleted()) {
        IFMapTable *table = node->table();
        TableMap::const_iterator tid = table_map_.find(table->name());
        assert(tid != table_map_.end());
        if (node->GetState(table, tid->second) == NULL) {
            return;
        }
    }

    tracker_->NodeEvent(&change_list_, node);
    manager_->OnChange();
}

//
// Callback to handle link events in the IFMapLinkTable.
// Informs the DependencyTracker about the link event.
//
void BgpConfigListener::LinkObserver(
    DBTablePartBase *root, DBEntryBase *db_entry) {
    CHECK_CONCURRENCY("db::DBTable");

    IFMapLink *link = static_cast<IFMapLink *>(db_entry);
    IFMapNode *left = link->LeftNode(database());
    IFMapNode *right = link->RightNode(database());
    if (tracker_->LinkEvent(link->metadata(), left, right)) {
        manager_->OnChange();
    }
}
