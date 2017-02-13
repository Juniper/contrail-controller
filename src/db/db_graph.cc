/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "db/db_graph.h"

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-variable"
#pragma clang diagnostic ignored "-Wunneeded-internal-declaration"
#endif

#include <boost/foreach.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/graph/breadth_first_search.hpp>
#include <boost/graph/filtered_graph.hpp>

#ifdef __clang__
#pragma clang diagnostic pop
#endif

#include "base/logging.h"
#include "base/time_util.h"
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

DBGraph::Edge DBGraph::Link(DBGraphVertex *lhs, DBGraphVertex *rhs,
                            DBGraphEdge *edge) {
    DBGraph::Edge edge_id;
    bool added;
    boost::tie(edge_id, added) = add_edge(lhs->vertex(), rhs->vertex(),
                                    EdgeProperties(edge->name(), edge), graph_);
    assert(added);
    edge->SetEdge(edge_id);
    return edge_id;
}

void DBGraph::Unlink(DBGraphEdge *edge) {
    remove_edge(edge->edge_id(), graph_);
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
    const BFSVisitor<graph_t> vis(vertex_visit_fn, edge_visit_fn,
                                  vertex_finish_fn);
    ColorMap color_map;
    breadth_first_search(graph_, start->vertex(),
        visitor(vis).color_map(make_assoc_property_map(color_map)));
}

void DBGraph::Visit(DBGraphVertex *start, VertexVisitor vertex_visit_fn,
                    EdgeVisitor edge_visit_fn) {
    const BFSVisitor<graph_t> vis(vertex_visit_fn, edge_visit_fn);
    ColorMap color_map;
    breadth_first_search(graph_, start->vertex(),
        visitor(vis).color_map(make_assoc_property_map(color_map)));
}

struct DBGraph::EdgePredicate {
    EdgePredicate() : graph_(NULL), filter_(NULL) { }
    EdgePredicate(const DBGraph *graph, const VisitorFilter &filter)
        : graph_(graph), filter_(&filter) {
    }

    bool operator()(const DBGraphVertex *src, const DBGraphVertex *tgt,
                    const DBGraphEdge *edge) const {
        if (edge->IsDeleted()) {
            return false;
        }
        return filter_->EdgeFilter(src, tgt, edge);
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
    bool operator()(const DBGraphVertex *entry) const {
        if (entry->IsDeleted()) {
            return false;
        }
        return filter_->VertexFilter(entry);
    }
private:
    const DBGraph *graph_;
    const VisitorFilter *filter_;
};

DBGraphVertex *DBGraph::vertex_target(DBGraphVertex *current_vertex,
                                      DBGraphEdge *edge) {
    DBGraphVertex *adjacent_vertex = edge->target(this);
    if (adjacent_vertex == current_vertex) {
        adjacent_vertex = edge->source(this);
    }
    return adjacent_vertex;
}


void DBGraph::IterateEdges(DBGraphVertex *current_vertex,
                   OutEdgeIterator &iter_begin, OutEdgeIterator &iter_end,
                   VertexVisitor vertex_visit_fn, EdgeVisitor edge_visit_fn,
                   EdgePredicate &edge_test, VertexPredicate &vertex_test,
                   uint64_t curr_walk, VisitQ &visit_q,
                   bool match_name, const std::string &allowed_edge) {
    for (; iter_begin != iter_end; ++iter_begin) {
        const DBGraph::EdgeProperties &e_prop = get(edge_bundle, *iter_begin);
        DBGraphEdge *edge = e_prop.edge;
        if (match_name && e_prop.name() != allowed_edge) break;
        DBGraphVertex *adjacent_vertex = vertex_target(current_vertex, edge);
        if (edge_visit_fn) edge_visit_fn(edge);
        if (!edge_test(current_vertex, adjacent_vertex, edge)) continue;
        if (adjacent_vertex->visited(curr_walk)) continue;
        if (vertex_test(adjacent_vertex)) {
            vertex_visit_fn(adjacent_vertex);
            visit_q.push(adjacent_vertex);
        }
        adjacent_vertex->set_visited(curr_walk);
    }
}

void DBGraph::Visit(DBGraphVertex *start, VertexVisitor vertex_visit_fn,
                    EdgeVisitor edge_visit_fn, const VisitorFilter &filter) {
    // uint64_t t = ClockMonotonicUsec();
    uint64_t curr_walk = get_graph_walk_num();
    EdgePredicate edge_test(this, filter);
    VertexPredicate vertex_test(this, filter);

    VisitQ visit_q;

    visit_q.push(start);
    start->set_visited(curr_walk);
    if (vertex_test(start)) {
        vertex_visit_fn(start);
    }
    while (!visit_q.empty()) {
        DBGraphVertex *vertex = visit_q.front();
        visit_q.pop();
        // Get All out-edges from the node
        OutEdgeListType &out_edge_set = graph_.out_edge_list(vertex->vertex());
        if (out_edge_set.empty()) continue;

        OutEdgeListType::iterator it = out_edge_set.begin();
        OutEdgeListType::iterator it_end = out_edge_set.end();

        // Collect all allowed edges to visit
        DBGraph::VisitorFilter::AllowedEdgeRetVal allowed_edge_ret =
            filter.AllowedEdges(vertex);

        if (!allowed_edge_ret.first) {
            BOOST_FOREACH (std::string allowed_edge, allowed_edge_ret.second) {
                EdgeContainer fake_container;
                fake_container.push_back(
                         EdgeType(0, 0, EdgeProperties(allowed_edge, NULL)));
                StoredEdge es(vertex->vertex(), fake_container.begin());
                // Call lower_bound on out edge list and walk on selected edges
                it = out_edge_set.lower_bound(es);
                IterateEdges(vertex, it, it_end, vertex_visit_fn, edge_visit_fn,
                edge_test, vertex_test, curr_walk, visit_q, true, allowed_edge);
            }
        } else {
            IterateEdges(vertex, it, it_end, vertex_visit_fn, edge_visit_fn,
                         edge_test, vertex_test, curr_walk, visit_q);
        }
    }
    // uint64_t end_t = ClockMonotonicUsec();
    // std::cout << "Graph Walk time(in usec) " <<  (end_t-t) << std::endl;
}

DBGraph::edge_iterator::edge_iterator(DBGraph *graph) : graph_(graph) {
    if (graph_) {
        boost::tie(iter_, end_) = edges(*graph_->graph());
    }
}

DBGraph::DBEdgeInfo DBGraph::edge_iterator::dereference() const {
    Vertex src = source(*iter_, *graph_->graph());
    Vertex tgt = target(*iter_, *graph_->graph());
    return boost::make_tuple(graph_->vertex_data(src), graph_->vertex_data(tgt),
                             graph_->edge_data(*iter_));
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

template <class StoredEdge>
bool order_by_name<StoredEdge>::operator()(const StoredEdge& e1, const StoredEdge& e2) const {
    const DBGraph::EdgeProperties &edge1 = get(edge_bundle, e1);
    const DBGraph::EdgeProperties &edge2 = get(edge_bundle, e2);
    return edge1.name() < edge2.name();
}
