/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_policy_graph_vertex_h
#define ctrlplane_policy_graph_vertex_h

#include <boost/intrusive/list.hpp>
#include "routing-policy/policy_graph_base.h"

class PolicyGraph;
class PolicyGraphEdge;
class PolicyGraphVertex;

namespace boost {
template <>
struct is_POD<PolicyGraphEdge> : public false_type {};

template <>
struct is_POD<PolicyGraphVertex> : public false_type {};
}

class PolicyGraphVertex {
public:
    typedef PolicyGraphBase::vertex_descriptor Vertex;
    typedef PolicyGraphBase::edge_descriptor Edge;
    enum VertexType {
        POLICY_MATCH = 1,
        POLICY_ACTION = 2
    };

    // Graph Vertex is stored in OpenVertexList for quick access
    boost::intrusive::list_member_hook<> open_vertex_;

    class adjacency_iterator : public boost::iterator_facade<
    adjacency_iterator, PolicyGraphVertex, boost::forward_traversal_tag
    > {
    public:
        adjacency_iterator();
        adjacency_iterator(PolicyGraph *graph, Vertex vertex);
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
        PolicyGraphVertex &dereference() const;
        PolicyGraph *graph_;
        PolicyGraphBase::adjacency_iterator iter_;
        PolicyGraphBase::adjacency_iterator end_;
    };

    class edge_iterator : public boost::iterator_facade<
    edge_iterator, PolicyGraphEdge, boost::forward_traversal_tag
    > {
    public:
        edge_iterator();
        edge_iterator(PolicyGraph *graph, PolicyGraphVertex *vertex);
        PolicyGraphVertex *target() const;
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
        PolicyGraphEdge &dereference() const;
        PolicyGraph *graph_;
        PolicyGraphVertex *vertex_;
        PolicyGraphBase::out_edge_iterator iter_;
        PolicyGraphBase::out_edge_iterator end_;
    };

    adjacency_iterator begin(PolicyGraph *graph) {
        return adjacency_iterator(graph, vertex_id_);
    }
    adjacency_iterator end(PolicyGraph *graph) {
        return adjacency_iterator();
    }

    PolicyGraphVertex *parent_vertex(PolicyGraph *graph);

    PolicyGraphEdge  *in_edge(PolicyGraph *graph);

    edge_iterator edge_list_begin(PolicyGraph *graph) {
        return edge_iterator(graph, this);
    }
    edge_iterator edge_list_end(PolicyGraph *graph) {
        return edge_iterator();
    }

    void set_vertex(const Vertex &vertex_id) {
        vertex_id_ = vertex_id;
    }

    Vertex vertex() const { return vertex_id_; }

    virtual VertexType Type() const = 0;

private:
    Vertex vertex_id_;
};

#endif // ctrlplane_policy_graph_vertex_h
