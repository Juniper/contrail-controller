/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */
#ifndef IFMAP_IFMAP_DEPENDENCY_TRACKER_H__
#define IFMAP_IFMAP_DEPENDENCY_TRACKER_H__

#include <list>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <boost/function.hpp>

class DB;
class DBGraph;
class IFMapNode;

//
// The DependencyTracker recursively evaluates dependencies as specified via a
// policy and pushes additional ConfigDeltas to the ChangeList.  This takes
// the burden of dependency tracking away from the consumers and automates it
// instead of being individually hand coded for each type of object.
//
// Elements are added onto the change list when they need to be reevaluated as
// a result of add/delete/change of other nodes and/or edges in the graph. The
// change list list is ultimately processed by the client
// (e.g. BgpConfigManager).
//
// The NodeEventPolicy is defined as a list of identifier types and associated
// ReactionMaps.
//
// A ReactionMap in turn is a mapping from a metadata type to a PropagateList,
// with the semantics that an add or delete of a edge of that type triggers a
// propagation across all edges with a type in the PropagateList. The keyword
// self is used instead of a metadata type to denote that the trigger is any
// change in the properties of the node itself. Note that the type of the node
// will always be the identifier type with which the ReactionMap is associated.
//
// A PropagateList is list of metadata types that need to be processed when
// propagating a change for a node or edge. They keyword self is used in this
// list to denote that the node associated with the edge should itself also be
// put on the ChangeList.
//
// The DependencyTracker is notified of Node and Link events by the listener.
// A Node event is considered interesting i.e. worthy of further propagation,
// if there's an entry for self in the ReactionMap for the identifier type.
//
// A Link is broken broken down into 2 unidirectional Edges.  An Edge consists
// of a Node and the associated metadata for the Link.  An Edge is considered
// interesting if there's an entry for the metadata in the ReactionMap for the
// Node's identifier type.
//
// A list of interesting Nodes and Edges is constructed based on Node and Link
// events as described above. Eventually, the DependencyTracker evaluates all
// the Nodes and Edges and recursively applies the NodeEventPolicy and builds
// up the change list of Nodes that needs to be processed by the client
// (e.g. BgpConfigManager).
// When applying the PropagateList, if the keyword self is present in the list
// the Node itself is added to the change list.  In any case, the rest of the
// metadata elements in the list are used to further propagate the changes.
//
// The NodeList and EdgeDescriptorList are both maintained using string names
// for the id type, id name and metadata.  Thus the DependencyTracker doesn't
// take any references on the IFMapNode objects. If an IFMapNode gets deleted
// before the changes in the NodeList and EdgeDescriptorList are propagated,
// we simply ignore the relevant nodes and edges when propagating the changes.
//
// Node and Link events are processed in the context of the db::DBTable task.
// The propagation of the NodeList and EdgeDescriptorList happens in context
// of the client task (e.g. bgp::Config) when the its requesst the ChangeList.
//
// The vertex list is used to make sure that we don't add duplicate entries to
// the ChangeList.
//
// CONCURRENCY: Not thread-safe. The class assumes that the caller ensures
// that only a single method can run at a time.
//
class IFMapDependencyTracker {
public:
    struct EdgeDescriptor {
        EdgeDescriptor(const std::string &meta, const std::string &type,
            const std::string &name)
            : metadata(meta), id_type(type), id_name(name) {
        }
        std::string metadata;
        std::string id_type;
        std::string id_name;
    };
    typedef std::list<EdgeDescriptor> EdgeDescriptorList;
    typedef std::list<std::pair<std::string, std::string> > NodeList;

    // identifier type -> (incoming metadata, outgoing metadata list)
    typedef std::set<std::string> PropagateList;
    typedef std::map<std::string, PropagateList> ReactionMap;
    typedef std::map<std::string, ReactionMap> NodeEventPolicy;

    typedef boost::function<void(IFMapNode *node)> ChangeObserver;
    IFMapDependencyTracker(DB *db, DBGraph *graph, ChangeObserver observer);

    NodeEventPolicy *policy_map() { return &policy_; }

    void NodeEvent(IFMapNode *node);
    bool LinkEvent(const std::string metadata,
        IFMapNode *left, IFMapNode *right);
    void PropagateChanges();
    void Clear();

    const NodeList &node_list() const { return node_list_; }
    const EdgeDescriptorList& edge_list() const { return edge_list_; }

private:
    typedef std::set<std::pair<IFMapNode *, std::string> > InEdgeSet;

    const PropagateList *GetPropagateList(const std::string &type,
        const std::string &metadata) const;
    bool IsInterestingEvent(const IFMapNode *node,
        const std::string &metadata) const;

    void PropagateNode(IFMapNode *node, InEdgeSet *in_edges);
    void PropagateEdge(IFMapNode *node, const std::string &metadata,
        InEdgeSet *in_edges);
    void AddChangeEvent(IFMapNode *node);

    DB *database_;
    DBGraph *graph_;
    ChangeObserver observer_;
    NodeEventPolicy policy_;
    std::set<std::string> vertex_list_;
    EdgeDescriptorList edge_list_;
    NodeList node_list_;
};

#endif
