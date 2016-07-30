/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "ifmap/ifmap_graph_walker.h"

#include <boost/bind.hpp>
#include "base/logging.h"
#include "base/task_trigger.h"
#include "db/db_graph.h"
#include "db/db_table.h"
#include "ifmap/ifmap_client.h"
#include "ifmap/ifmap_exporter.h"
#include "ifmap/ifmap_link.h"
#include "ifmap/ifmap_log.h"
#include "ifmap/ifmap_table.h"
#include "ifmap/ifmap_server.h"
#include "ifmap/ifmap_log_types.h"
#include "ifmap/ifmap_update.h"
#include "ifmap/ifmap_util.h"
#include "ifmap/ifmap_whitelist.h"
#include "schema/vnc_cfg_types.h"

class GraphPropagateFilter : public DBGraph::VisitorFilter {
public:
    GraphPropagateFilter(IFMapExporter *exporter,
                         const IFMapTypenameWhiteList *type_filter,
                         const BitSet &bitset)
            : exporter_(exporter),
              type_filter_(type_filter),
              bset_(bitset) {
    }

    bool VertexFilter(const DBGraphVertex *vertex) const {
        return type_filter_->VertexFilter(vertex);
    }

    bool EdgeFilter(const DBGraphVertex *source, const DBGraphVertex *target,
                    const DBGraphEdge *edge) const {
        bool accept = type_filter_->EdgeFilter(source, target, edge);
        if (!accept) {
            return false;
        }
        const IFMapNode *tgt = static_cast<const IFMapNode *>(target);
        const IFMapNodeState *state = NodeStateLookup(tgt);
        if (state != NULL && state->interest().Contains(bset_)) {
            return false;
        }

        return true;
    }

    const IFMapNodeState *NodeStateLookup(const IFMapNode *node) const {
        const DBTable *table = node->table();
        const DBState *state =
                node->GetState(table, exporter_->TableListenerId(table));
        return static_cast<const IFMapNodeState *>(state);
    }

private:
    IFMapExporter *exporter_;
    const IFMapTypenameWhiteList *type_filter_;
    const BitSet &bset_;
};

IFMapGraphWalker::IFMapGraphWalker(DBGraph *graph, IFMapExporter *exporter)
    : graph_(graph),
      exporter_(exporter),
      link_delete_walk_trigger_(new TaskTrigger(
          boost::bind(&IFMapGraphWalker::LinkDeleteWalk, this),
          TaskScheduler::GetInstance()->GetTaskId("db::IFMapTable"), 0)),
      walk_client_index_(BitSet::npos) {
    traversal_white_list_.reset(new IFMapTypenameWhiteList());
    AddNodesToWhitelist();
    AddLinksToWhitelist();
}

IFMapGraphWalker::~IFMapGraphWalker() {
}

void IFMapGraphWalker::JoinVertex(DBGraphVertex *vertex, const BitSet &bset) {
    IFMapNode *node = static_cast<IFMapNode *>(vertex);
    IFMapNodeState *state = exporter_->NodeStateLocate(node);
    IFMAP_DEBUG(JoinVertex, vertex->ToString(), state->interest().ToString(),
                bset.ToString());
    exporter_->StateInterestOr(state, bset);
    node->table()->Change(node);
    // Mark all dependent links as potentially modified.
    for (IFMapNodeState::iterator iter = state->begin(); iter != state->end();
         ++iter) {
        DBTable *table = exporter_->link_table();
        table->Change(iter.operator->());
    }
}

void IFMapGraphWalker::ProcessLinkAdd(IFMapNode *lnode, IFMapNode *rnode,
                                      const BitSet &bset) {
    GraphPropagateFilter filter(exporter_, traversal_white_list_.get(), bset);
    graph_->Visit(rnode,
                  boost::bind(&IFMapGraphWalker::JoinVertex, this, _1, bset),
                  0,
                  filter);
}

void IFMapGraphWalker::LinkAdd(IFMapNode *lnode, const BitSet &lhs,
                               IFMapNode *rnode, const BitSet &rhs) {
    IFMAP_DEBUG(LinkOper, "LinkAdd", lnode->ToString(), rnode->ToString(),
                lhs.ToString(), rhs.ToString());
    if (!lhs.empty() && !rhs.Contains(lhs) &&
        traversal_white_list_->VertexFilter(rnode) &&
        traversal_white_list_->EdgeFilter(lnode, rnode, NULL)) {
        ProcessLinkAdd(lnode, rnode, lhs);
    }
    if (!rhs.empty() && !lhs.Contains(rhs) &&
        traversal_white_list_->VertexFilter(lnode) &&
        traversal_white_list_->EdgeFilter(rnode, lnode, NULL)) {
        ProcessLinkAdd(rnode, lnode, rhs);
    }
}

void IFMapGraphWalker::LinkRemove(const BitSet &bset) {
    OrLinkDeleteClients(bset);          // link_delete_clients_ | bset
    link_delete_walk_trigger_->Set();
}

// Check if the neighbor or link to neighbor should be filtered. Returns true 
// if rnode or link to rnode should be filtered.
bool IFMapGraphWalker::FilterNeighbor(IFMapNode *lnode, IFMapNode *rnode) {
    if (!traversal_white_list_->VertexFilter(rnode) ||
        !traversal_white_list_->EdgeFilter(lnode, rnode, NULL)) {
        return true;
    }
    return false;
}

void IFMapGraphWalker::RecomputeInterest(DBGraphVertex *vertex, int bit) {
    IFMapNode *node = static_cast<IFMapNode *>(vertex);
    IFMapNodeState *state = exporter_->NodeStateLocate(node);
    state->nmask_set(bit);
    UpdateNewReachableNodesTracker(bit, state);
}

bool IFMapGraphWalker::LinkDeleteWalk() {
    if (link_delete_clients_.empty()) {
        walk_client_index_ = BitSet::npos;
        return true;
    }

    IFMapServer *server = exporter_->server();
    size_t i;

    // Get the index of the client we want to start with.
    if (walk_client_index_ == BitSet::npos) {
        i = link_delete_clients_.find_first();
    } else {
        // walk_client_index_ was the last client that we finished processing.
        i = link_delete_clients_.find_next(walk_client_index_);
    }
    int count = 0;
    BitSet done_set;
    while (i != BitSet::npos) {
        IFMapClient *client = server->GetClient(i);
        assert(client);
        AddNewReachableNodesTracker(client->index());

        IFMapTable *table = IFMapTable::FindTable(server->database(),
                                                  "virtual-router");
        IFMapNode *node = table->FindNode(client->identifier());
        if ((node != NULL) && node->IsVertexValid()) {
            graph_->Visit(node,
                boost::bind(&IFMapGraphWalker::RecomputeInterest, this, _1, i),
                0, *traversal_white_list_.get());
        }
        done_set.set(i);
        if (++count == kMaxLinkDeleteWalks) {
            // client 'i' has been processed. If 'i' is the last bit set, we
            // will return true below. Else we will return false and there
            // is atleast one more bit left to process.
            break;
        }

        i = link_delete_clients_.find_next(i);
    }
    // Remove the subset of clients that we have finished processing.
    ResetLinkDeleteClients(done_set);

    LinkDeleteWalkBatchEnd(done_set);

    if (link_delete_clients_.empty()) {
        walk_client_index_ = BitSet::npos;
        return true;
    } else {
        walk_client_index_ = i;
        return false;
    }
}

void IFMapGraphWalker::OrLinkDeleteClients(const BitSet &bset) {
    link_delete_clients_.Set(bset);     // link_delete_clients_ | bset
}

void IFMapGraphWalker::ResetLinkDeleteClients(const BitSet &bset) {
    link_delete_clients_.Reset(bset);
}

void IFMapGraphWalker::CleanupInterest(int client_index, IFMapNode *node,
                                       IFMapNodeState *state) {
    BitSet rm_mask;
    rm_mask.set(client_index);

    // interest = interest - rm_mask + nmask

    if (!state->interest().empty() && !state->nmask().empty()) {
        IFMAP_DEBUG(CleanupInterest, node->ToString(),
                    state->interest().ToString(), rm_mask.ToString(),
                    state->nmask().ToString());
    }
    BitSet ninterest;
    ninterest.BuildComplement(state->interest(), rm_mask);
    ninterest |= state->nmask();
    state->nmask_clear();
    if (state->interest() == ninterest) {
        return;
    }

    exporter_->StateInterestSet(state, ninterest);
    node->table()->Change(node);

    // Mark all dependent links as potentially modified.
    for (IFMapNodeState::iterator iter = state->begin();
         iter != state->end(); ++iter) {
        DBTable *table = exporter_->link_table();
        table->Change(iter.operator->());
    }
}

// Cleanup all the graph nodes that were reachable before this link delete.
// After this link delete, these nodes may still be reachable. But, its
// also possible that the link delete has made them unreachable.
void IFMapGraphWalker::OldReachableNodesCleanupInterest(int client_index) {
    IFMapState *state = NULL;
    IFMapNode *node = NULL;
    IFMapExporter::Cs_citer iter = exporter_->ClientConfigTrackerBegin(
        IFMapExporter::INTEREST, client_index);
    IFMapExporter::Cs_citer end_iter = exporter_->ClientConfigTrackerEnd(
        IFMapExporter::INTEREST, client_index);

    while (iter != end_iter) {
        state = *iter;
        // Get the iterator to the next element before calling
        // CleanupInterest() since the state might be removed from the
        // client's config-tracker, thereby invalidating the iterator of the
        // container we are iterating over.
        ++iter;
        if (state->IsNode()) {
            node = state->GetIFMapNode();
            assert(node);
            IFMapNodeState *nstate = exporter_->NodeStateLookup(node);
            assert(state == nstate);
            CleanupInterest(client_index, node, nstate);
        }
    }
}

// Cleanup all the graph nodes that were not reachable before the link delete
// but are reachable now. Note, we store nodes in new_reachable_nodes_tracker_
// only if we visited them during the graph-walk via RecomputeInterest() and if
// their interest bit was not set i.e. they were not reachable before we
// started the walk.
void IFMapGraphWalker::NewReachableNodesCleanupInterest(int client_index) {
    IFMapState *state = NULL;
    IFMapNode *node = NULL;
    ReachableNodesSet *rnset = new_reachable_nodes_tracker_.at(client_index);

    for (Rns_citer iter = rnset->begin(); iter != rnset->end(); ++iter) {
        state = *iter;
        assert(state->IsNode());
        node = state->GetIFMapNode();
        assert(node);
        IFMapNodeState *nstate = exporter_->NodeStateLookup(node);
        assert(state == nstate);
        CleanupInterest(client_index, node, nstate);
    }
    DeleteNewReachableNodesTracker(client_index);
}

void IFMapGraphWalker::LinkDeleteWalkBatchEnd(const BitSet &done_set) {
    for (size_t i = done_set.find_first(); i != BitSet::npos;
            i = done_set.find_next(i)) {
        // Examine all the nodes that were reachable before the link delete.
        OldReachableNodesCleanupInterest(i);
        // Examine all the nodes that were not reachable before the link
        // delete but are now reachable.
        NewReachableNodesCleanupInterest(i);
    }
}

void IFMapGraphWalker::AddNewReachableNodesTracker(int client_index) {
    if (client_index >= (int)new_reachable_nodes_tracker_.size()) {
        new_reachable_nodes_tracker_.resize(client_index + 1, NULL);
    }
    assert(new_reachable_nodes_tracker_[client_index] == NULL);
    ReachableNodesSet *rnset = new ReachableNodesSet();
    new_reachable_nodes_tracker_[client_index] = rnset;
}

void IFMapGraphWalker::DeleteNewReachableNodesTracker(int client_index) {
    ReachableNodesSet *rnset = new_reachable_nodes_tracker_.at(client_index);
    assert(rnset);
    delete rnset;
    new_reachable_nodes_tracker_[client_index] = NULL;
}

// Keep track of this node if it was unreachable earlier.
void IFMapGraphWalker::UpdateNewReachableNodesTracker(int client_index,
                                                      IFMapState *state) {
    ReachableNodesSet *rnset = new_reachable_nodes_tracker_.at(client_index);
    assert(rnset);
    // If the interest is not set, the node was not reachable earlier but is
    // reachable now.
    if (!state->interest().test(client_index)) {
        rnset->insert(state);
    }
}

const IFMapTypenameWhiteList &IFMapGraphWalker::get_traversal_white_list()
        const {
    return *traversal_white_list_.get();
}

// The nodes listed below and the nodes in 
// IFMapGraphTraversalFilterCalculator::CreateNodeBlackList() are mutually 
// exclusive
void IFMapGraphWalker::AddNodesToWhitelist() {
    traversal_white_list_->include_vertex.insert("virtual-router");
    traversal_white_list_->include_vertex.insert("virtual-machine");
    traversal_white_list_->include_vertex.insert("bgp-router");
    traversal_white_list_->include_vertex.insert("global-system-config");
    traversal_white_list_->include_vertex.insert("provider-attachment");
    traversal_white_list_->include_vertex.insert("service-instance");
    traversal_white_list_->include_vertex.insert("global-vrouter-config");
    traversal_white_list_->include_vertex.insert(
        "virtual-machine-interface");
    traversal_white_list_->include_vertex.insert("security-group");
    traversal_white_list_->include_vertex.insert("physical-router");
    traversal_white_list_->include_vertex.insert("service-template");
    traversal_white_list_->include_vertex.insert("instance-ip");
    traversal_white_list_->include_vertex.insert("virtual-network");
    traversal_white_list_->include_vertex.insert("floating-ip");
    traversal_white_list_->include_vertex.insert("alias-ip");
    traversal_white_list_->include_vertex.insert("customer-attachment");
    traversal_white_list_->include_vertex.insert(
        "virtual-machine-interface-routing-instance");
    traversal_white_list_->include_vertex.insert("physical-interface");
    traversal_white_list_->include_vertex.insert("domain");
    traversal_white_list_->include_vertex.insert("floating-ip-pool");
    traversal_white_list_->include_vertex.insert("alias-ip-pool");
    traversal_white_list_->include_vertex.insert("logical-interface");
    traversal_white_list_->include_vertex.insert(
        "virtual-network-network-ipam");
    traversal_white_list_->include_vertex.insert("access-control-list");
    traversal_white_list_->include_vertex.insert("routing-instance");
    traversal_white_list_->include_vertex.insert("namespace");
    traversal_white_list_->include_vertex.insert("virtual-DNS");
    traversal_white_list_->include_vertex.insert("network-ipam");
    traversal_white_list_->include_vertex.insert("virtual-DNS-record");
    traversal_white_list_->include_vertex.insert("interface-route-table");
    traversal_white_list_->include_vertex.insert("subnet");
    traversal_white_list_->include_vertex.insert("service-health-check");
    traversal_white_list_->include_vertex.insert("bgp-as-a-service");
    traversal_white_list_->include_vertex.insert("qos-config");
    traversal_white_list_->include_vertex.insert("qos-queue");
    traversal_white_list_->include_vertex.insert("forwarding-class");
    traversal_white_list_->include_vertex.insert("global-qos-config");
}

void IFMapGraphWalker::AddLinksToWhitelist() {
    traversal_white_list_->include_edge.insert(
        "source=virtual-router,target=virtual-machine");
    traversal_white_list_->include_edge.insert(
        "source=virtual-router,target=bgp-router");
    traversal_white_list_->include_edge.insert(
        "source=virtual-router,target=global-system-config");
    traversal_white_list_->include_edge.insert(
        "source=virtual-router,target=provider-attachment");
    traversal_white_list_->include_edge.insert(
        "source=virtual-router,target=physical-router");
    traversal_white_list_->include_edge.insert(
        "source=virtual-machine,target=service-instance");
    traversal_white_list_->include_edge.insert(
        "source=virtual-machine,target=virtual-machine-interface");
    traversal_white_list_->include_edge.insert(
        "source=virtual-machine-interface,target=virtual-machine");
    traversal_white_list_->include_edge.insert(
        "source=bgp-router,target=physical-router");
    traversal_white_list_->include_edge.insert(
        "source=service-instance,target=service-template");
    traversal_white_list_->include_edge.insert(
        "source=global-system-config,target=global-vrouter-config");
    traversal_white_list_->include_edge.insert(
        "source=virtual-machine-interface,target=virtual-machine-interface");
    traversal_white_list_->include_edge.insert(
        "source=virtual-machine-interface,target=instance-ip");
    traversal_white_list_->include_edge.insert(
        "source=virtual-machine-interface,target=virtual-network");
    traversal_white_list_->include_edge.insert(
        "source=virtual-machine-interface,target=security-group");
    traversal_white_list_->include_edge.insert(
        "source=virtual-machine-interface,target=floating-ip");
    traversal_white_list_->include_edge.insert(
        "source=virtual-machine-interface,target=alias-ip");
    traversal_white_list_->include_edge.insert(
        "source=virtual-machine-interface,target=customer-attachment");
    traversal_white_list_->include_edge.insert(
        "source=virtual-machine-interface,target=virtual-machine-interface-routing-instance");
    traversal_white_list_->include_edge.insert(
        "source=virtual-machine-interface,target=interface-route-table");       
    traversal_white_list_->include_edge.insert(
        "source=logical-interface,target=virtual-machine-interface");
    traversal_white_list_->include_edge.insert(
        "source=physical-router,target=physical-interface");
    traversal_white_list_->include_edge.insert(
        "source=physical-router,target=logical-interface");
    traversal_white_list_->include_edge.insert(
        "source=physical-router,target=virtual-network");
    traversal_white_list_->include_edge.insert(
        "source=physical-interface,target=logical-interface");
    traversal_white_list_->include_edge.insert(
        "source=service-template,target=domain");
    traversal_white_list_->include_edge.insert(
        "source=virtual-network,target=floating-ip-pool");
    traversal_white_list_->include_edge.insert(
        "source=virtual-network,target=alias-ip-pool");
    traversal_white_list_->include_edge.insert(
        "source=virtual-network,target=virtual-network-network-ipam");
    traversal_white_list_->include_edge.insert(
        "source=virtual-network,target=access-control-list");
    traversal_white_list_->include_edge.insert(
        "source=virtual-network,target=routing-instance");
    traversal_white_list_->include_edge.insert(
        "source=domain,target=namespace");
    traversal_white_list_->include_edge.insert(
        "source=domain,target=virtual-DNS");
    traversal_white_list_->include_edge.insert(
        "source=virtual-network-network-ipam,target=network-ipam");
    traversal_white_list_->include_edge.insert(
        "source=virtual-DNS,target=virtual-DNS-record");
    traversal_white_list_->include_edge.insert(
        "source=security-group,target=access-control-list");
    traversal_white_list_->include_edge.insert(
        "source=virtual-machine-interface,target=subnet");

    // Manually add required links not picked by the
    // IFMapGraphTraversalFilterCalculator
    traversal_white_list_->include_edge.insert(
        "source=floating-ip,target=floating-ip-pool");
    traversal_white_list_->include_edge.insert(
        "source=alias-ip,target=alias-ip-pool");
    traversal_white_list_->include_edge.insert(
        "source=virtual-machine-interface-routing-instance,target=routing-instance");
    // VDNS needs dns/dhcp info from IPAM and FQN from Domain.
    traversal_white_list_->include_edge.insert(
        "source=network-ipam,target=virtual-DNS");
    // Need this to get from floating-ip-pool to the virtual-network we are
    // getting the pool from. EG: public-network (might not have any VMs)
    traversal_white_list_->include_edge.insert(
        "source=floating-ip-pool,target=virtual-network");
    // Need this to get from alias-ip-pool to the virtual-network we are
    // getting the pool from. since alias ip network might not have any VMs
    traversal_white_list_->include_edge.insert(
        "source=alias-ip-pool,target=virtual-network");
    traversal_white_list_->include_edge.insert(
        "source=virtual-machine-interface,target=service-health-check");
    traversal_white_list_->include_edge.insert(
        "source=virtual-machine-interface,target=bgp-as-a-service");
    traversal_white_list_->include_edge.insert(
        "source=bgp-as-a-service,target=bgp-router");
    traversal_white_list_->include_edge.insert(
        "source=bgp-router,target=routing-instance");


    traversal_white_list_->include_edge.insert(
                    "source=global-system-config,target=global-qos-config");
    traversal_white_list_->include_edge.insert(
                    "source=global-qos-config,target=forwarding-class");
    traversal_white_list_->include_edge.insert(
                    "source=global-qos-config,target=qos-queue");
    traversal_white_list_->include_edge.insert(
                    "source=forwarding-class,target=qos-queue");
    traversal_white_list_->include_edge.insert(
                    "source=global-qos-config,target=qos-config");
    traversal_white_list_->include_edge.insert(
                    "source=virtual-machine-interface,target=qos-config");
    traversal_white_list_->include_edge.insert(
                    "source=virtual-network,target=qos-config");
    traversal_white_list_->include_edge.insert(
                    "source=global-system-config,target=qos-config");
}

