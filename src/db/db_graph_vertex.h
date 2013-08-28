/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_db_graph_entry_h
#define ctrlplane_db_graph_entry_h

#include "db/db_entry.h"
#include "db/db_graph_base.h"

class DBGraph;
class DBGraphEdge;
class DBGraphVertex;

namespace boost {
template <>
struct is_POD<DBGraphEdge> : public false_type {};

template <>
struct is_POD<DBGraphVertex> : public false_type {};
}

class DBGraphVertex : public DBEntry {
public:
    typedef DBGraphBase::vertex_descriptor Vertex;
    typedef DBGraphBase::edge_descriptor Edge;

    DBGraphVertex() : vertex_id_(NULL) { }

    class adjacency_iterator : public boost::iterator_facade<
    adjacency_iterator, DBGraphVertex, boost::forward_traversal_tag
    > {
    public:
        adjacency_iterator();
        adjacency_iterator(DBGraph *graph, Vertex vertex);
    private:
        friend class boost::iterator_core_access;
        void increment() {
            ++iter_;
        }
        bool equal(const adjacency_iterator &rhs) const {
            if (graph_ == NULL) {
                return (rhs.graph_ == NULL);
            }
            if (rhs.graph_ == NULL) {
                return iter_ == end_;
            }
            return iter_ == rhs.iter_;
        }
        DBGraphVertex &dereference() const;
        DBGraph *graph_;
        DBGraphBase::adjacency_iterator iter_;
        DBGraphBase::adjacency_iterator end_;
    };

    class edge_iterator : public boost::iterator_facade<
    edge_iterator, DBGraphEdge, boost::forward_traversal_tag
    > {
    public:
        edge_iterator();
        edge_iterator(DBGraph *graph, DBGraphVertex *vertex);
        DBGraphVertex *target() const;
    private:
        friend class boost::iterator_core_access;
        void increment() {
            ++iter_;
        }
        bool equal(const edge_iterator &rhs) const {
            if (graph_ == NULL) {
                return (rhs.graph_ == NULL);
            }
            if (rhs.graph_ == NULL) {
                return iter_ == end_;
            }
            return iter_ == rhs.iter_;
        }
        DBGraphEdge &dereference() const;
        DBGraph *graph_;
        DBGraphVertex *vertex_;
        DBGraphBase::out_edge_iterator iter_;
        DBGraphBase::out_edge_iterator end_;
    };

    adjacency_iterator begin(DBGraph *graph) {
        return adjacency_iterator(graph, vertex_id_);
    }
    adjacency_iterator end(DBGraph *graph) {
        return adjacency_iterator();
    }

    edge_iterator edge_list_begin(DBGraph *graph) {
        return edge_iterator(graph, this);
    }
    edge_iterator edge_list_end(DBGraph *graph) {
        return edge_iterator();
    }

    bool HasAdjacencies(DBGraph *graph) const;

    void set_vertex(const Vertex &vertex_id) {
        vertex_id_ = vertex_id;
    }

    void VertexInvalidate() {
        vertex_id_ = NULL;
    }

    bool IsVertexValid() {
        return (vertex_id_ != NULL);
    }

    Vertex vertex() const { return vertex_id_; }

private:
    Vertex vertex_id_;
};

#endif
