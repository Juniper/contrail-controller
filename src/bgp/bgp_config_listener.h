/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __ctrlplane__bgp_config_listener__
#define __ctrlplane__bgp_config_listener__

#include <map>
#include <set>
#include <vector>

#include <boost/scoped_ptr.hpp>
#include "base/util.h"
#include "db/db_table.h"

struct BgpConfigDelta;
class BgpConfigManager;
class DB;
class DBGraph;
class IFMapNode;

//
// This class implements an observer for events on the IFMapTables associated
// with BGP configuration items. It listens to the IFMapTables in question and
// puts BgpConfigDeltas on the change list.  TableMap is a list of IFMapTable
// names and corresponding DBTable::ListenerIds that this class has registered.
//
// The DependencyTracker recursively evaluates dependencies as specified via a
// policy and pushes additional BgpConfigDeltas to the change list. This takes
// the burden of dependency tracking away from the consumers and automates it
// instead of being individually hand coded for each type of object.
//
// The ChangeList of BgpConfigDeltas is processed by the BgpConfigManager with
// which this BgpConfigListener is associated.
//
class BgpConfigListener {
public:
    typedef std::vector<BgpConfigDelta> ChangeList;

    explicit BgpConfigListener(BgpConfigManager *manager);
    virtual ~BgpConfigListener();

    void Initialize(DB *database);
    void Terminate(DB *database);

    virtual void GetChangeList(ChangeList *change_list);

private:
    friend class BgpConfigListenerTest;

    typedef std::map<std::string, DBTable::ListenerId> TableMap;
    class DependencyTracker;

    void NodeObserver(DBTablePartBase *root, DBEntryBase *db_entry);
    void LinkObserver(DBTablePartBase *root, DBEntryBase *db_entry);
    void ChangeListAdd(ChangeList *change_list, IFMapNode *node) const;

    DB *database();
    DBGraph *graph();

    BgpConfigManager *manager_;
    boost::scoped_ptr<DependencyTracker> tracker_;
    TableMap table_map_;
    ChangeList change_list_;

    DISALLOW_COPY_AND_ASSIGN(BgpConfigListener);
};

//
// The DependencyTracker recursively evaluates dependencies as specified via a
// policy and pushes additional BgpConfigDeltas to the ChangeList.  This takes
// the burden of dependency tracking away from the consumers and automates it
// instead of being individually hand coded for each type of object.
//
// Elements are added onto the change list when they need to be reevaluated as
// a result of add/delete/change of other nodes and/or edges in the graph. The
// change list list is ultimately processed by the BgpConfigManager.
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
// up the change list of Nodes that needs to be processed by BgpConfigManager.
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
// Node and Link events are processed in the context of the dB::DBTable task.
// The propagation of the NodeList and EdgeDescriptorList happens in context
// of the bgp::Config task when the BgpConfigManager's config handler asks the
// BgpConfigListener to build up the ChangeList.
//
// The vertex list is used to make sure that we don't add duplicate entries to
// the ChangeList.
//
class BgpConfigListener::DependencyTracker {
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

    DependencyTracker(BgpConfigListener *listener);

    void Initialize();
    void NodeEvent(ChangeList *change_list, IFMapNode *node);
    bool LinkEvent(const std::string metadata,
        IFMapNode *left, IFMapNode *right);
    void PropagateChanges(ChangeList *change_list);
    void Clear();

private:
    friend class BgpConfigListenerTest;

    typedef std::set<std::pair<IFMapNode *, std::string> > InEdgeSet;

    const PropagateList *GetPropagateList(const std::string &type,
        const std::string &metadata) const;
    bool IsInterestingEvent(const IFMapNode *node,
        const std::string &metadata) const;

    void PropagateNode(IFMapNode *node, InEdgeSet *in_edges,
        ChangeList *change_list);
    void PropagateEdge(IFMapNode *node, const std::string &metadata,
        InEdgeSet *in_edges, ChangeList *change_list);
    void AddChangeEvent(ChangeList *change_list, IFMapNode *node);

    BgpConfigListener *listener_;
    NodeEventPolicy policy_;
    std::set<std::string> vertex_list_;
    EdgeDescriptorList edge_list_;
    NodeList node_list_;
};

#endif /* defined(__ctrlplane__bgp_config_listener__) */
