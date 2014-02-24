/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "db/db_graph.h"

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-variable"
#pragma clang diagnostic ignored "-Wunneeded-internal-declaration"
#endif

#include <boost/graph/breadth_first_search.hpp>
#include <boost/graph/filtered_graph.hpp>

#ifdef __clang__
#pragma clang diagnostic pop
#endif

#include "base/logging.h"
#include "db/db_graph_vertex.h"
#include "db/db_graph_edge.h"

using namespace std;
using namespace boost;

void DBGraph::AddNode(DBGraphVertex *entry) {
    entry->set_vertex(add_vertex(graph_));
    DBGraphBase::VertexProperties &vertex = graph_[entry->vertex()];
    vertex.entry = entry;
}

void DBGraph::RemoveNode(DBGraphVertex *entry) {
    remove_vertex(entry->vertex(), graph_);
    entry->VertexInvalidate();
}

DBGraph::Edge DBGraph::Link(DBGraphVertex *lhs, DBGraphVertex *rhs) {
    DBGraph::Edge edge_id;
    bool added;
    boost::tie(edge_id, added) = add_edge(lhs->vertex(), rhs->vertex(), graph_);
    assert(added);
    return edge_id;
}

void DBGraph::Unlink(DBGraphVertex *lhs, DBGraphVertex *rhs) {
    remove_edge(lhs->vertex(), rhs->vertex(), graph_);
}

void DBGraph::SetEdgeProperty(DBGraphEdge *edge) {
    DBGraphBase::EdgeProperties &properties = graph_[edge->edge_id()];
    properties.edge = edge;
}

DBGraphEdge *DBGraph::GetEdge(const DBGraphVertex *src,
                              const DBGraphVertex *tgt) {
    edge_descriptor edge_id;
    bool exists;
    boost::tie(edge_id, exists) = edge(src->vertex(), tgt->vertex(), graph_);
    if (!exists) {
        return NULL;
    }
    return graph_[edge_id].edge;
}

template <typename GraphType>
class BFSVisitor : public default_bfs_visitor {
public:
    typedef DBGraphBase::VertexProperties Properties;

    BFSVisitor(DBGraph::VertexVisitor vertex_visit,
               DBGraph::EdgeVisitor edge_visit)
        : vertex_visit_(vertex_visit), edge_visit_(edge_visit) {
    }

    BFSVisitor(DBGraph::VertexVisitor vertex_visit,
               DBGraph::EdgeVisitor edge_visit,
               DBGraph::VertexFinish vertex_finish)
        : vertex_visit_(vertex_visit), edge_visit_(edge_visit),
          vertex_finish_(vertex_finish) {
    }

    void discover_vertex(DBGraph::Vertex u, const GraphType &graph) const {
        if (vertex_visit_) {
            vertex_visit_(graph[u].entry);
        }
    }

    void finish_vertex(DBGraph::Vertex u, const GraphType &graph) const {
        if (vertex_finish_) {
            vertex_finish_(graph[u].entry);
        }
    }

    // Edges are examined twice: once for the source and once for the target
    // node.
    void examine_edge(DBGraph::Edge e, const GraphType &graph) const {
        const DBGraphBase::EdgeProperties &properties = graph[e];
        if (edge_visit_) {
            edge_visit_(properties.edge);
        }
    }

private:
    DBGraph::VertexVisitor vertex_visit_;
    DBGraph::EdgeVisitor edge_visit_;
    DBGraph::VertexFinish vertex_finish_;
};

typedef std::map<DBGraph::Vertex, default_color_type> ColorMap;

void DBGraph::Visit(DBGraphVertex *start, VertexVisitor vertex_visit_fn,
                    EdgeVisitor edge_visit_fn, VertexFinish vertex_finish_fn) {
    BFSVisitor<graph_t> vis(vertex_visit_fn, edge_visit_fn, vertex_finish_fn);
    ColorMap color_map;
    breadth_first_search(graph_, start->vertex(),
        visitor(vis).color_map(make_assoc_property_map(color_map)));
}

void DBGraph::Visit(DBGraphVertex *start, VertexVisitor vertex_visit_fn,
                    EdgeVisitor edge_visit_fn) {
    BFSVisitor<graph_t> vis(vertex_visit_fn, edge_visit_fn);
    ColorMap color_map;
    breadth_first_search(graph_, start->vertex(),
        visitor(vis).color_map(make_assoc_property_map(color_map)));
}

struct DBGraph::EdgePredicate {
    EdgePredicate() : graph_(NULL), filter_(NULL) { }
    EdgePredicate(const DBGraph *graph, const VisitorFilter &filter)
        : graph_(graph), filter_(&filter) {
    }

    bool operator()(const Edge &edge) const {
        const DBGraphVertex *src = graph_->vertex_data(
            source(edge, graph_->graph_));
        const DBGraphVertex *tgt = graph_->vertex_data(
            target(edge, graph_->graph_));
        const DBGraphEdge *entry = graph_->edge_data(edge);
        if (entry->IsDeleted()) {
            return false;
        }
        return filter_->EdgeFilter(src, tgt, entry);
    }

private:
    const DBGraph *graph_;
    const VisitorFilter *filter_;
};

struct DBGraph::VertexPredicate {
    VertexPredicate() : graph_(NULL), filter_(NULL) { }
    VertexPredicate(const DBGraph *graph, const VisitorFilter &filter)
        : graph_(graph), filter_(&filter) {
    }
    bool operator()(const Vertex &vertex) const {
        const DBGraphVertex *entry = graph_->vertex_data(vertex);
        if (entry->IsDeleted()) {
            return false;
        }
        return filter_->VertexFilter(entry);
    }
private:
    const DBGraph *graph_;
    const VisitorFilter *filter_;
};

void DBGraph::Visit(DBGraphVertex *start, VertexVisitor vertex_visit_fn,
                    EdgeVisitor edge_visit_fn, const VisitorFilter &filter) {
    typedef filtered_graph<graph_t, EdgePredicate, VertexPredicate>
        filtered_graph_t;
    EdgePredicate edge_test(this, filter);
    VertexPredicate vertex_test(this, filter);
    filtered_graph_t gfiltered(graph_, edge_test, vertex_test);

    const BFSVisitor<filtered_graph_t> vis(vertex_visit_fn, edge_visit_fn);
    ColorMap color_map;
    breadth_first_search(gfiltered, start->vertex(),
            visitor(vis).color_map(make_assoc_property_map(color_map)));
}

DBGraph::edge_iterator::edge_iterator(DBGraph *graph) : graph_(graph) {
    if (graph_) {
        boost::tie(iter_, end_) = edges(*graph_->graph());
    }
}

DBGraph::DBVertexPair DBGraph::edge_iterator::dereference() const {
    Vertex src = source(*iter_, *graph_->graph());
    Vertex tgt = target(*iter_, *graph_->graph());
    return make_pair(graph_->vertex_data(src), graph_->vertex_data(tgt));
}

bool DBGraph::edge_iterator::equal(const edge_iterator &rhs) const {
    if (graph_ != NULL) {
        if (rhs.graph_ == NULL) {
            return (iter_ == end_);
        }
        return iter_ == rhs.iter_;
    } else {
        if (rhs.graph_ != NULL) {
            return (rhs.iter_ == rhs.end_);
        }
        return true;
    }
}

DBGraph::edge_iterator DBGraph::edge_list_begin() {
    return edge_iterator(this);
}

DBGraph::edge_iterator DBGraph::edge_list_end() {
    return edge_iterator(NULL);
}

DBGraph::vertex_iterator::vertex_iterator(DBGraph *graph)
    : graph_(graph) {
    if (graph_ != NULL) {
        boost::tie(iter_, end_) = vertices(*graph_->graph());
    }
}

bool DBGraph::vertex_iterator::equal(const vertex_iterator &rhs) const {
    if (graph_ != NULL) {
        if (rhs.graph_ != NULL) {
            return iter_ == rhs.iter_;
        } else {
            return (iter_ == end_);
        }
    } else {
        if (rhs.graph_ != NULL) {
            return rhs.iter_ == rhs.end_;
        } else {
            return true;
        }
    }
}

DBGraph::vertex_iterator DBGraph::vertex_list_begin() {
    return vertex_iterator(this);
}

DBGraph::vertex_iterator DBGraph::vertex_list_end() {
    return vertex_iterator(NULL);
}

void DBGraph::clear() {
    graph_.clear();
}

size_t DBGraph::vertex_count() const {
    return num_vertices(graph_);
}

size_t DBGraph::edge_count() const {
    return num_edges(graph_);
}

