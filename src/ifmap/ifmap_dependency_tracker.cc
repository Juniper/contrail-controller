/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "ifmap/ifmap_dependency_tracker.h"

#include <boost/assign/list_of.hpp>

#include "ifmap/ifmap_link.h"
#include "ifmap/ifmap_node.h"
#include "ifmap/ifmap_table.h"

using namespace boost::assign;
using namespace std;

IFMapDependencyTracker::IFMapDependencyTracker(
    DB *db, DBGraph *graph, ChangeObserver observer)
        : database_(db), graph_(graph), observer_(observer) {
}

//
// Add an IFMapNode to the NodeList for further propagation if it's an
// interesting node. It's interesting if there's an entry for self in the
// ReactionMap.
//
// The node is always added to the ChangeList even if it's not interesting.
//
void IFMapDependencyTracker::NodeEvent(IFMapNode *node) {
    AddChangeEvent(node);
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
bool IFMapDependencyTracker::LinkEvent(const string metadata,
    IFMapNode *left, IFMapNode *right) {
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
void IFMapDependencyTracker::PropagateChanges() {
    InEdgeSet in_edges;

    for (NodeList::iterator iter = node_list_.begin();
         iter != node_list_.end(); ++iter) {
        IFMapTable *table =
            IFMapTable::FindTable(database_, iter->first);
        if (table == NULL) {
            continue;
        }
        IFMapNode *node = table->FindNode(iter->second);
        if ((node == NULL) || node->IsDeleted()) {
            continue;
        }
        PropagateNode(node, &in_edges);
    }

    for (EdgeDescriptorList::iterator iter = edge_list_.begin();
         iter != edge_list_.end(); ++iter) {
        const EdgeDescriptor &edge = *iter;
        IFMapTable *table =
            IFMapTable::FindTable(database_, edge.id_type);
        if (table == NULL) {
            continue;
        }
        IFMapNode *node = table->FindNode(edge.id_name);
        if ((node == NULL) || node->IsDeleted()) {
            continue;
        }
        PropagateEdge(node, edge.metadata, &in_edges);
    }
}

//
// Clear all intermediate state used during propagation. This is called after
// we're done propagating all accumulated node and edge triggers to the change
// list.
//
void IFMapDependencyTracker::Clear() {
    vertex_list_.clear();
    node_list_.clear();
    edge_list_.clear();
}

//
// Get the PropagateList for the given identifier type and metadata.
//
const IFMapDependencyTracker::PropagateList *
IFMapDependencyTracker::GetPropagateList(
    const string &type, const string &metadata) const {

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
bool IFMapDependencyTracker::IsInterestingEvent(
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
void IFMapDependencyTracker::PropagateNode(
    IFMapNode *node, InEdgeSet *in_edges) {

    const PropagateList *plist =
        GetPropagateList(node->table()->Typename(), "self");
    assert(plist);

    // Iterate through the edges of node. If the metadata for an edge is in
    // the PropagateList, we need to propagate changes for the edge itself.
    for (DBGraphVertex::edge_iterator iter =
         node->edge_list_begin(graph_);
         iter != node->edge_list_end(graph_); ++iter) {
        IFMapLink *link = static_cast<IFMapLink *>(iter.operator->());
        IFMapNode *target = static_cast<IFMapNode *>(iter.target());
        if (plist->find(link->metadata()) == plist->end()) {
            continue;
        }
        PropagateEdge(target, link->metadata(), in_edges);
    }
}

//
// Propagate changes for an edge on the EdgeDescriptorList.
//
void IFMapDependencyTracker::PropagateEdge(
    IFMapNode *node, const string &metadata, InEdgeSet *in_edges) {
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
        AddChangeEvent(node);
    }

    // Iterate through the edges of node. If the metadata for an edge is in
    // the PropagateList, we need to propagate changes for the edge itself.
    for (DBGraphVertex::edge_iterator iter =
         node->edge_list_begin(graph_);
         iter != node->edge_list_end(graph_); ++iter) {
        IFMapLink *link = static_cast<IFMapLink *>(iter.operator->());
        if (plist->find(link->metadata()) == plist->end()) {
            continue;
        }
        IFMapNode *target = static_cast<IFMapNode *>(iter.target());
        PropagateEdge(target, link->metadata(), in_edges);
    }
}

//
// Add the IFMapNode to the change list it's not already on there.
//
void IFMapDependencyTracker::AddChangeEvent(IFMapNode *node) {
    ostringstream identifier;
    identifier << node->table()->Typename() << ':' << node->name();
    if (vertex_list_.count(identifier.str()) > 0) {
        return;
    }
    observer_(node);
    vertex_list_.insert(identifier.str());
}

