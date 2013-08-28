/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "routing-policy/policy_graph.h"

#include <boost/graph/graphviz.hpp>

#include "routing-policy/policy_edge.h"
#include "routing-policy/policy_vertex.h"

using namespace std;
using namespace boost;

void PolicyGraph::WriteDot(const string &fname) const {
    std::ofstream f(fname.c_str());
#if 0
    write_graphviz(f, root_);
#endif
    f.close();
}

void PolicyGraph::AddNode(PolicyGraphVertex *entry) {
    entry->set_vertex(add_vertex(root_));
    PolicyGraphBase::VertexProperties &vertex = root_[entry->vertex()];
    vertex.entry = entry;
}

void PolicyGraph::RemoveNode(PolicyGraphVertex *entry) {
    remove_vertex(entry->vertex(), root_);
}

PolicyGraph::Edge 
PolicyGraph::Link(PolicyGraphVertex *lhs, PolicyGraphVertex *rhs) {
    PolicyGraph::Edge edge_id;
    bool added;
    boost::tie(edge_id, added) = add_edge(lhs->vertex(), rhs->vertex(), root_);
    assert(added);
    return edge_id;
}

void PolicyGraph::Unlink(PolicyGraphVertex *lhs, PolicyGraphVertex *rhs) {
    remove_edge(lhs->vertex(), rhs->vertex(), root_);
}

void PolicyGraph::SetEdgeProperty(PolicyGraphEdge *edge) {
    PolicyGraphBase::EdgeProperties &properties = root_[edge->edge_id()];
    properties.edge = edge;
}

PolicyGraphEdge *PolicyGraph::GetEdge(const PolicyGraphVertex *src,
                                      const PolicyGraphVertex *tgt) {
    edge_descriptor edge_id;
    bool exists;
    boost::tie(edge_id, exists) = edge(src->vertex(), tgt->vertex(), root_);
    if (!exists) {
        return NULL;
    }
    return root_[edge_id].edge;
}

void PolicyGraph::clear() {
    root_.clear();
}

size_t PolicyGraph::vertex_count() const {
    return num_vertices(root_);
}

size_t PolicyGraph::edge_count() const {
    return num_edges(root_);
}

PolicyGraph::OpenVertexIterator PolicyGraph::open_vertex_begin() {
    return open_vertex_list_.begin();
}

PolicyGraph::OpenVertexIterator PolicyGraph::open_vertex_end() {
    return open_vertex_list_.end();
}

void PolicyGraph::AddOpenVertex(PolicyGraphVertex *vertex) {
    open_vertex_list_.push_back(*vertex);
}

void PolicyGraph::RemoveOpenVertex(PolicyGraphVertex *vertex) {
    assert(vertex->open_vertex_.is_linked());
    open_vertex_list_.erase(open_vertex_list_.iterator_to(*vertex));
}

PolicyGraphVertex *PolicyGraph::GetRoot() const {
    Vertex v = *(vertices(root_).first);
    return root_[v].entry;
}

void PolicyGraph::open_vertex_reverse_iterator::increment() {
    PolicyGraphEdge *edge = open_->in_edge(graph_); 
    open_ = edge->source(graph_);
}

bool 
PolicyGraph::open_vertex_reverse_iterator::equal(
                         const open_vertex_reverse_iterator &rhs) const {
    if (graph_ == NULL) {
        return (graph_ == rhs.graph_);
    }

    if (rhs.graph_ == NULL) {
        return open_ == graph_->GetRoot();
    }

    return open_ == rhs.open_;
}

PolicyGraph::OpenVertexPathPair 
PolicyGraph::open_vertex_reverse_iterator::dereference() const {
    if (graph_ == NULL || open_ == NULL) {
        return make_pair((PolicyGraphVertex *)0, (PolicyGraphEdge *)0);
    }
    PolicyGraphVertex *vertex = NULL;
    PolicyGraphEdge *edge = NULL;
    edge = open_->in_edge(graph_); 
    vertex = edge->source(graph_);
    return std::make_pair(vertex, edge);
}
