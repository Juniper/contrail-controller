/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __ctrlplane__ifmap_graph_walker__
#define __ctrlplane__ifmap_graph_walker__

#include "base/bitset.h"
#include "base/queue_task.h"

class DBGraph;
class DBGraphEdge;
class DBGraphVertex;
class IFMapExporter;
class IFMapNode;
class IFMapNodeState;
class IFMapState;
class TaskTrigger;
struct IFMapTypenameFilter;
struct IFMapTypenameWhiteList;

// Computes the interest graph for the ifmap clients (i.e. vnc agent).
class IFMapGraphWalker {
public:
    typedef std::set<IFMapState *> ReachableNodesSet;
    typedef ReachableNodesSet::const_iterator Rns_citer;
    typedef std::vector<ReachableNodesSet *> ReachableNodesTracker;

    IFMapGraphWalker(DBGraph *graph, IFMapExporter *exporter);
    ~IFMapGraphWalker();

    // When a link is added, for each interest bit, find the corresponding
    // source node and walk the graph constructing the respective interest
    // list.
    void LinkAdd(IFMapNode *lnode, const BitSet &lhs,
                 IFMapNode *rnode, const BitSet &rhs);
    void LinkRemove(const BitSet &bset);

    bool FilterNeighbor(IFMapNode *lnode, IFMapNode *rnode);
    const IFMapTypenameWhiteList &get_traversal_white_list() const;
    void ResetLinkDeleteClients(const BitSet &bset);

private:
    static const int kMaxLinkDeleteWalks = 1;

    void ProcessLinkAdd(IFMapNode *lnode, IFMapNode *rnode, const BitSet &bset);
    void JoinVertex(DBGraphVertex *vertex, const BitSet &bset);
    void RecomputeInterest(DBGraphVertex *vertex, int bit);
    void CleanupInterest(int client_index, IFMapNode *node,
                         IFMapNodeState *state);
    void AddNodesToWhitelist();
    void AddLinksToWhitelist();
    bool LinkDeleteWalk();
    void LinkDeleteWalkBatchEnd(const BitSet &done_set);
    void OrLinkDeleteClients(const BitSet &bset);
    void AddNewReachableNodesTracker(int client_index);
    void DeleteNewReachableNodesTracker(int client_index);
    void UpdateNewReachableNodesTracker(int client_index, IFMapState *state);
    void OldReachableNodesCleanupInterest(int client_index);
    void NewReachableNodesCleanupInterest(int client_index);

    DBGraph *graph_;
    IFMapExporter *exporter_;
    boost::scoped_ptr<TaskTrigger> link_delete_walk_trigger_;
    std::auto_ptr<IFMapTypenameWhiteList> traversal_white_list_;
    BitSet link_delete_clients_;
    size_t walk_client_index_;
    ReachableNodesTracker new_reachable_nodes_tracker_;
};

#endif /* defined(__ctrlplane__ifmap_graph_walker__) */
