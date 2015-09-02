/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_policy_graph_h
#define ctrlplane_policy_graph_h

#include <boost/function.hpp>
#include <boost/intrusive/list.hpp>

#include "routing-policy/policy_graph_base.h"
#include "routing-policy/policy_vertex.h"

class PolicyGraphEdge;
class PolicyGraphVertex;

class PolicyGraph : public PolicyGraphBase {
public:
    typedef PolicyGraphBase::edge_descriptor Edge;
    typedef PolicyGraphBase::vertex_descriptor Vertex;
    typedef boost::intrusive::member_hook<PolicyGraphVertex, 
            boost::intrusive::list_member_hook<>, 
            &PolicyGraphVertex::open_vertex_> OpenVertexListMember; 
    typedef boost::intrusive::list<PolicyGraphVertex, 
            OpenVertexListMember> OpenVertexList;
    typedef OpenVertexList::iterator OpenVertexIterator;

    void AddNode(PolicyGraphVertex *entry);

    void RemoveNode(PolicyGraphVertex *entry);

    Edge Link(PolicyGraphVertex *lhs, PolicyGraphVertex *rhs);

    void Unlink(PolicyGraphVertex *lhs, PolicyGraphVertex *rhs);

    void SetEdgeProperty(PolicyGraphEdge *edge);
    
    PolicyGraphEdge *GetEdge(const PolicyGraphVertex *src, 
                             const PolicyGraphVertex *tgt);

    const graph_t *graph() const { return &root_; }

    PolicyGraphVertex *vertex_data(Vertex vertex) const {
        return root_[vertex].entry;
    }

    PolicyGraphEdge *edge_data(PolicyGraph::Edge edge) const {
        return root_[edge].edge;
    }

    void clear();

    size_t vertex_count() const;
    size_t edge_count() const;

    void WriteDot(const std::string &fname) const;

    OpenVertexIterator open_vertex_begin();

    OpenVertexIterator open_vertex_end();

    void AddOpenVertex(PolicyGraphVertex *vertex);

    void RemoveOpenVertex(PolicyGraphVertex *vertex);

    typedef std::pair<PolicyGraphVertex *, 
            PolicyGraphEdge *> OpenVertexPathPair;
    class open_vertex_reverse_iterator : public boost::iterator_facade<
        open_vertex_reverse_iterator, OpenVertexPathPair, 
        boost::forward_traversal_tag, OpenVertexPathPair> {
    public:
        open_vertex_reverse_iterator() 
            : graph_(NULL), open_(NULL) {
        }

        open_vertex_reverse_iterator(PolicyGraph *graph, 
                                     PolicyGraphVertex *start) 
            : graph_(graph), open_(start) {
        }

    private:
        friend class boost::iterator_core_access;

        void increment();

        bool equal(const open_vertex_reverse_iterator &rhs) const;

        OpenVertexPathPair dereference() const;

        PolicyGraph *graph_;
        PolicyGraphVertex *open_;
    };

    open_vertex_reverse_iterator begin(PolicyGraphVertex *open) {
        return open_vertex_reverse_iterator(this, open);
    }

    open_vertex_reverse_iterator end(PolicyGraphVertex *open) {
        return open_vertex_reverse_iterator();
    }

    PolicyGraphVertex *GetRoot() const;
private:
    graph_t root_;
    OpenVertexList open_vertex_list_;
};

#endif // ctrlplane_policy_graph_h
