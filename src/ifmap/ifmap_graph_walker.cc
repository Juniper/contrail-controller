/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "ifmap/ifmap_graph_walker.h"

#include <boost/bind.hpp>
#include "base/logging.h"
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
      work_queue_(TaskScheduler::GetInstance()->GetTaskId("db::DBTable"), 0,
                  boost::bind(&IFMapGraphWalker::Worker, this, _1)) {
    work_queue_.SetExitCallback(
        boost::bind(&IFMapGraphWalker::WorkBatchEnd, this, _1));
    traversal_white_list_.reset(new IFMapTypenameWhiteList());
    AddNodesToWhitelist();
    AddLinksToWhitelist();
}

IFMapGraphWalker::~IFMapGraphWalker() {
}

void IFMapGraphWalker::JoinVertex(DBGraphVertex *vertex, const BitSet &bset) {
    IFMAP_DEBUG(JoinVertex, vertex->ToString());
    IFMapNode *node = static_cast<IFMapNode *>(vertex);
    IFMapNodeState *state = exporter_->NodeStateLocate(node);
    state->InterestOr(bset);
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
    QueueEntry entry;
    entry.set = bset;
    work_queue_.Enqueue(entry);
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
}

bool IFMapGraphWalker::Worker(QueueEntry work_entry) {
    const BitSet &bset = work_entry.set;
    IFMapServer *server = exporter_->server();
    for (size_t i = bset.find_first(); i != BitSet::npos;
         i = bset.find_next(i)) {
        IFMapClient *client = server->GetClient(i);
        if (client == NULL) {
            continue;
        }
        // TODO: In order to handle interest based on the vswitch registration
        // there need to be links in the graph that correspond to these.
        IFMapTable *table = IFMapTable::FindTable(server->database(),
                                                  "virtual-router");
        IFMapNode *node = table->FindNode(client->identifier());
        if ((node != NULL) && node->IsVertexValid()) {
            graph_->Visit(node,
                boost::bind(&IFMapGraphWalker::RecomputeInterest, this, _1, i),
                0, *traversal_white_list_.get());
        }
    }
    rm_mask_ |= work_entry.set;
    return true;
}

void IFMapGraphWalker::CleanupInterest(DBGraphVertex *vertex) {
    // interest = interest - rm_mask_ + nmask
    IFMapNode *node = static_cast<IFMapNode *>(vertex);
    IFMapNodeState *state = exporter_->NodeStateLookup(node);
    if (state == NULL) {
        return;
    }

    if (!state->interest().empty() && !state->nmask().empty()) {
        IFMAP_DEBUG(CleanupInterest, node->ToString(),
                    state->interest().ToString(), rm_mask_.ToString(),
                    state->nmask().ToString());
    }
    BitSet ninterest;
    ninterest.BuildComplement(state->interest(), rm_mask_);
    ninterest |= state->nmask();
    state->nmask_clear();
    if (state->interest() == ninterest) {
        return;
    }

    state->SetInterest(ninterest);
    node->table()->Change(node);

    // Mark all dependent links as potentially modified.
    for (IFMapNodeState::iterator iter = state->begin();
         iter != state->end(); ++iter) {
        DBTable *table = exporter_->link_table();
        table->Change(iter.operator->());
    }
}

// Cleanup all graph nodes that a bit set in the remove mask (rm_mask_) but
// where not visited by the walker.
void IFMapGraphWalker::WorkBatchEnd(bool done) {
    for (DBGraph::vertex_iterator iter = graph_->vertex_list_begin();
         iter != graph_->vertex_list_end(); ++iter) {
        DBGraphVertex *vertex = iter.operator->();
        CleanupInterest(vertex);
    }
    rm_mask_.clear();
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
    traversal_white_list_->include_vertex.insert("customer-attachment");
    traversal_white_list_->include_vertex.insert(
        "virtual-machine-interface-routing-instance");
    traversal_white_list_->include_vertex.insert("physical-interface");
    traversal_white_list_->include_vertex.insert("domain");
    traversal_white_list_->include_vertex.insert("floating-ip-pool");
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
        "source=virtual-machine-interface,target=instance-ip");
    traversal_white_list_->include_edge.insert(
        "source=virtual-machine-interface,target=virtual-network");
    traversal_white_list_->include_edge.insert(
        "source=virtual-machine-interface,target=security-group");
    traversal_white_list_->include_edge.insert(
        "source=virtual-machine-interface,target=floating-ip");
    traversal_white_list_->include_edge.insert(
        "source=virtual-machine-interface,target=customer-attachment");
    traversal_white_list_->include_edge.insert(
        "source=virtual-machine-interface,target=virtual-machine-interface-routing-instance");
    traversal_white_list_->include_edge.insert(
        "source=virtual-machine-interface,target=interface-route-table");       
    traversal_white_list_->include_edge.insert(
        "source=physical-router,target=physical-interface");
    traversal_white_list_->include_edge.insert(
        "source=service-template,target=domain");
    traversal_white_list_->include_edge.insert(
        "source=virtual-network,target=floating-ip-pool");
    traversal_white_list_->include_edge.insert(
        "source=virtual-network,target=logical-interface");
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

    // Manually add required links not picked by the
    // IFMapGraphTraversalFilterCalculator
    traversal_white_list_->include_edge.insert(
        "source=floating-ip,target=floating-ip-pool");
    traversal_white_list_->include_edge.insert(
        "source=virtual-machine-interface-routing-instance,target=routing-instance");
    // VDNS needs dns/dhcp info from IPAM and FQN from Domain.
    traversal_white_list_->include_edge.insert(
        "source=network-ipam,target=virtual-DNS");
    // Need this to get from floating-ip-pool to the virtual-network we are
    // getting the pool from. EG: public-network (might not have any VMs)
    traversal_white_list_->include_edge.insert(
        "source=floating-ip-pool,target=virtual-network");
}

