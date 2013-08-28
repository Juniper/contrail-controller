/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __ctrlplane__ifmap_graph_walker__
#define __ctrlplane__ifmap_graph_walker__

#include "base/bitset.h"
#include "base/queue_task.h"
#include "schema/vnc_cfg_types.h"

class DBGraph;
class DBGraphEdge;
class DBGraphVertex;
class IFMapExporter;
class IFMapNode;
struct IFMapTypenameFilter;
struct IFMapTypenameWhiteList;

// Computes the interest graph for the ifmap clients (i.e. vnc agent).
class IFMapGraphWalker {
public:
    IFMapGraphWalker(DBGraph *graph, IFMapExporter *exporter);
    ~IFMapGraphWalker();

    // When a link is added, for each interest bit, find the corresponding
    // source node and walk the graph constructing the respective interest
    // list.
    void LinkAdd(IFMapNode *lnode, const BitSet &lhs,
                 IFMapNode *rnode, const BitSet &rhs);
    void LinkRemove(const BitSet &bset);

    bool FilterNeighbor(IFMapNode *lnode, IFMapNode *rnode);

private:
    struct QueueEntry {
        BitSet set;
    };

    bool Worker(QueueEntry entry);
    void WorkBatchEnd(bool done);

    void ProcessLinkAdd(IFMapNode *lnode, IFMapNode *rnode, const BitSet &bset);
    void JoinVertex(DBGraphVertex *vertex, const BitSet &bset);
    void RecomputeInterest(DBGraphVertex *vertex, int bit);
    void CleanupInterest(DBGraphVertex *vertex);
    void AddNodesToWhitelist();
    void AddLinksToWhitelist();

    DBGraph *graph_;
    IFMapExporter *exporter_;
    WorkQueue<QueueEntry> work_queue_;
    std::auto_ptr<IFMapTypenameWhiteList> traversal_white_list_;
    BitSet rm_mask_;
};

#endif /* defined(__ctrlplane__ifmap_graph_walker__) */
