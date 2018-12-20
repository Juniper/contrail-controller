/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "ifmap/ifmap_graph_walker.h"

#include <boost/assign/list_of.hpp>
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
#include "schema/vnc_cfg_types.h"

using boost::assign::list_of;
using boost::assign::map_list_of;

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

    DBGraph::VisitorFilter::AllowedEdgeRetVal AllowedEdges(
                                       const DBGraphVertex *vertex) const {
        return type_filter_->AllowedEdges(vertex);
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
}

IFMapGraphWalker::~IFMapGraphWalker() {
}

void IFMapGraphWalker::NotifyEdge(DBGraphEdge *edge, const BitSet &bset) {
    DBTable *table = exporter_->link_table();
    table->Change(edge);
}

void IFMapGraphWalker::JoinVertex(DBGraphVertex *vertex, const BitSet &bset) {
    IFMapNode *node = static_cast<IFMapNode *>(vertex);
    IFMapNodeState *state = exporter_->NodeStateLocate(node);
    IFMAP_DEBUG(JoinVertex, vertex->ToString(), state->interest().ToString(),
               bset.ToString());
    exporter_->StateInterestOr(state, bset);
    node->table()->Change(node);
}

void IFMapGraphWalker::ProcessLinkAdd(IFMapNode *lnode, IFMapNode *rnode,
                                      const BitSet &bset) {
    GraphPropagateFilter filter(exporter_, traversal_white_list_.get(), bset);
    graph_->Visit(rnode,
                  boost::bind(&IFMapGraphWalker::JoinVertex, this, _1, bset),
                  boost::bind(&IFMapGraphWalker::NotifyEdge, this, _1, bset),
                  filter);
}

void IFMapGraphWalker::LinkAdd(IFMapLink *link, IFMapNode *lnode, const BitSet &lhs,
                               IFMapNode *rnode, const BitSet &rhs) {
    IFMAP_DEBUG(LinkOper, "LinkAdd", lnode->ToString(), rnode->ToString(),
                lhs.ToString(), rhs.ToString());
    if (!lhs.empty() && !rhs.Contains(lhs) &&
        traversal_white_list_->VertexFilter(rnode) &&
        traversal_white_list_->EdgeFilter(lnode, rnode, link))  {
        ProcessLinkAdd(lnode, rnode, lhs);
    }
    if (!rhs.empty() && !lhs.Contains(rhs) &&
        traversal_white_list_->VertexFilter(lnode) &&
        traversal_white_list_->EdgeFilter(rnode, lnode, link)) {
        ProcessLinkAdd(rnode, lnode, rhs);
    }
}

void IFMapGraphWalker::LinkRemove(const BitSet &bset) {
    OrLinkDeleteClients(bset);          // link_delete_clients_ | bset
    link_delete_walk_trigger_->Set();
}

// Check if the neighbor or link to neighbor should be filtered. Returns true
// if rnode or link to rnode should be filtered.
bool IFMapGraphWalker::FilterNeighbor(IFMapNode *lnode, IFMapLink *link) {
    IFMapNode *rnode = link->left();
    if (rnode == lnode)
        rnode = link->right();
    if (!traversal_white_list_->VertexFilter(rnode) ||
        !traversal_white_list_->EdgeFilter(lnode, NULL, link)) {
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
    traversal_white_list_->include_vertex = map_list_of<std::string, std::set<std::string> >
        ("virtual-router",
         list_of("physical-router-virtual-router")
                ("virtual-router-virtual-machine")
                ("virtual-router-network-ipam")
                ("global-system-config-virtual-router")
                ("provider-attachment-virtual-router")
                ("virtual-router-virtual-machine-interface"))
        ("virtual-router-network-ipam", list_of("virtual-router-network-ipam"))
        ("virtual-machine",
         list_of("virtual-machine-service-instance")
                ("virtual-machine-interface-virtual-machine")
                ("virtual-machine-tag"))
        ("control-node-zone", std::set<std::string>())
        ("bgp-router",
         list_of("instance-bgp-router")
                ("physical-router-bgp-router")
                ("bgp-router-control-node-zone"))
        ("bgp-as-a-service",
         list_of("bgpaas-bgp-router")
                ("bgpaas-health-check")
                ("bgpaas-control-node-zone"))
        ("bgpaas-control-node-zone", list_of("bgpaas-control-node-zone"))
        ("global-system-config",
         list_of("global-system-config-global-vrouter-config")
                ("global-system-config-global-qos-config")
                ("qos-config-global-system-config"))
        ("provider-attachment", std::set<std::string>())
        ("service-instance", list_of("service-instance-service-template")
                                    ("service-instance-port-tuple"))
        ("global-vrouter-config",
          list_of("application-policy-set-global-vrouter-config")
                 ("global-vrouter-config-security-logging-object"))
        ("virtual-machine-interface",
         list_of("virtual-machine-virtual-machine-interface")
                ("virtual-machine-interface-sub-interface")
                ("instance-ip-virtual-machine-interface")
                ("virtual-machine-interface-virtual-network")
                ("virtual-machine-interface-security-group")
                ("floating-ip-virtual-machine-interface")
                ("alias-ip-virtual-machine-interface")
                ("customer-attachment-virtual-machine-interface")
                ("virtual-machine-interface-routing-instance")
                ("virtual-machine-interface-route-table")
                ("subnet-virtual-machine-interface")
                ("service-port-health-check")
                ("bgpaas-virtual-machine-interface")
                ("virtual-machine-interface-qos-config")
                ("virtual-machine-interface-bridge-domain")
                ("virtual-machine-interface-security-logging-object")
                ("project-virtual-machine-interface")
                ("port-tuple-interface")
                ("virtual-machine-interface-tag")
                ("virtual-machine-interface-bgp-router"))
        ("virtual-machine-interface-bridge-domain",
         list_of("virtual-machine-interface-bridge-domain"))
        ("security-group", list_of("security-group-access-control-list"))
        ("physical-router",
         list_of("physical-router-physical-interface")
         ("physical-router-logical-interface")
         ("physical-router-virtual-network"))
        ("service-template", list_of("domain-service-template"))
        ("instance-ip", list_of("instance-ip-virtual-network"))
        ("virtual-network",
         list_of("virtual-network-floating-ip-pool")
         ("virtual-network-alias-ip-pool")
         ("virtual-network-network-ipam")
         ("virtual-network-access-control-list")
         ("virtual-network-routing-instance")
         ("virtual-network-qos-config")
         ("virtual-network-bridge-domain")
         ("virtual-network-security-logging-object")
         ("virtual-network-tag")
         ("virtual-network-provider-network")
         ("virtual-network-multicast-policy"))
        ("floating-ip", list_of("floating-ip-pool-floating-ip")
         ("instance-ip-floating-ip"))
        ("alias-ip", list_of("alias-ip-pool-alias-ip"))
        ("customer-attachment", std::set<std::string>())
        ("virtual-machine-interface-routing-instance",
         list_of("virtual-machine-interface-routing-instance"))
        ("physical-interface", list_of("physical-interface-logical-interface"))
        ("domain", list_of("domain-namespace")("domain-virtual-DNS"))
        ("floating-ip-pool", list_of("virtual-network-floating-ip-pool"))
        ("alias-ip-pool", list_of("virtual-network-alias-ip-pool"))
        ("logical-interface", list_of("logical-interface-virtual-machine-interface"))
        ("logical-router-virtual-network", list_of("logical-router-virtual-network"))
        ("logical-router", list_of("logical-router-virtual-network")
                                  ("logical-router-interface"))
        ("virtual-network-network-ipam", list_of("virtual-network-network-ipam"))
        ("access-control-list", std::set<std::string>())
        ("routing-instance", list_of("instance-bgp-router"))
        ("namespace", std::set<std::string>())
        ("virtual-DNS", list_of("virtual-DNS-virtual-DNS-record"))
        ("network-ipam", list_of("network-ipam-virtual-DNS"))
        ("virtual-DNS-record", std::set<std::string>())
        ("interface-route-table", std::set<std::string>())
        ("subnet", std::set<std::string>())
        ("service-health-check", std::set<std::string>())
        ("qos-config", std::set<std::string>())
        ("qos-queue", std::set<std::string>())
        ("forwarding-class", list_of("forwarding-class-qos-queue"))
        ("global-qos-config",
         list_of("global-qos-config-forwarding-class")
         ("global-qos-config-qos-queue")
         ("global-qos-config-qos-config"))
        ("bridge-domain", std::set<std::string>())
        ("security-logging-object",
         list_of("virtual-network-security-logging-object")
                ("virtual-machine-interface-security-logging-object")
                ("global-vrouter-config-security-logging-object")
                ("security-logging-object-network-policy")
                ("security-logging-object-security-group"))
        ("tag", list_of("application-policy-set-tag"))
        ("application-policy-set", list_of("application-policy-set-firewall-policy")
                                          ("policy-management-application-policy-set"))
        ("application-policy-set-firewall-policy",
                                   list_of("application-policy-set-firewall-policy"))
        ("firewall-policy", list_of("firewall-policy-firewall-rule")
                                   ("firewall-policy-security-logging-object"))
        ("firewall-policy-firewall-rule",
                            list_of("firewall-policy-firewall-rule"))
        ("firewall-policy-security-logging-object",
                            list_of("firewall-policy-security-logging-object"))
        ("firewall-rule", list_of("firewall-rule-tag")
                                 ("firewall-rule-service-group")
                                 ("firewall-rule-address-group")
                                 ("firewall-rule-security-logging-object"))
        ("firewall-rule-security-logging-object",
                            list_of("firewall-rule-security-logging-object"))
        ("service-group", std::set<std::string>())
        ("address-group", list_of("address-group-tag"))
        ("project", list_of("project-tag")
                           ("project-logical-router"))
        ("port-tuple", list_of("service-instance-port-tuple")
                              ("port-tuple-interface"))
        ("policy-management", std::set<std::string>())
        ("multicast-policy", list_of("virtual-network-multicast-policy"));
}
