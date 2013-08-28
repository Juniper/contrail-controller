/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_db_graph_h
#define ctrlplane_db_graph_h

#include <boost/function.hpp>
#include <boost/iterator/iterator_facade.hpp>
#include "db/db_graph_base.h"
#include "db/db_graph_vertex.h"

class DBGraphEdge;

class DBGraph : public DBGraphBase {
public:
    typedef DBGraphBase::edge_descriptor Edge;
    typedef DBGraphBase::vertex_descriptor Vertex;
    typedef boost::function<void(DBGraphVertex *)> VertexVisitor;
    typedef boost::function<void(DBGraphEdge *)> EdgeVisitor;
    typedef boost::function<void(DBGraphVertex *)> VertexFinish;

    struct VisitorFilter {
        virtual ~VisitorFilter() { }
        virtual bool VertexFilter(const DBGraphVertex *vertex) const {
            return true;
        }
        virtual bool EdgeFilter(const DBGraphVertex *source,
                                const DBGraphVertex *target,
                                const DBGraphEdge *edge) const {
            return true;
        }
    };

    typedef std::pair<DBGraphVertex *, DBGraphVertex *> DBVertexPair;
    class edge_iterator : public boost::iterator_facade<
    edge_iterator, DBVertexPair, boost::forward_traversal_tag, DBVertexPair
    > {
    public:
        explicit edge_iterator(DBGraph *graph);
    private:
        friend class boost::iterator_core_access;
        void increment() {
            ++iter_;
        }
        bool equal(const edge_iterator &rhs) const;
        DBVertexPair dereference() const;
        DBGraph *graph_;
        DBGraphBase::edge_iterator iter_;
        DBGraphBase::edge_iterator end_;
    };

    class vertex_iterator : public boost::iterator_facade<
        vertex_iterator, DBGraphVertex, boost::forward_traversal_tag> {
    public:
        explicit vertex_iterator(DBGraph *graph);

    private:
        friend class boost::iterator_core_access;
        void increment() {
            ++iter_;
        }
        bool equal(const vertex_iterator &rhs) const;
        DBGraphVertex &dereference() const {
            return *graph_->vertex_data(*iter_);
        }

        DBGraph *graph_;
        graph_t::vertex_iterator iter_;
        graph_t::vertex_iterator end_;
    };

    void AddNode(DBGraphVertex *entry);

    void RemoveNode(DBGraphVertex *entry);

    Edge Link(DBGraphVertex *lhs, DBGraphVertex *rhs);

    void Unlink(DBGraphVertex *lhs, DBGraphVertex *rhs);

    void SetEdgeProperty(DBGraphEdge *edge);
    
    DBGraphEdge *GetEdge(const DBGraphVertex *src, const DBGraphVertex *tgt);

    const graph_t *graph() const { return &graph_; }

    DBGraphVertex *vertex_data(DBGraphBase::vertex_descriptor vertex) const {
        return graph_[vertex].entry;
    }

    DBGraphEdge *edge_data(DBGraph::Edge edge) const {
        return graph_[edge].edge;
    }

    void Visit(DBGraphVertex *start, VertexVisitor vertex_visit_fn,
               EdgeVisitor edge_visit_fn);
    void Visit(DBGraphVertex *start, VertexVisitor vertex_visit_fn,
               EdgeVisitor edge_visit_fn, const VisitorFilter &filter);
    void Visit(DBGraphVertex *start, VertexVisitor vertex_visit_fn,
               EdgeVisitor edge_visit_fn, VertexFinish vertex_finish_fn);

    edge_iterator edge_list_begin();
    edge_iterator edge_list_end();

    vertex_iterator vertex_list_begin();
    vertex_iterator vertex_list_end();

    void clear();

    size_t vertex_count() const;
    size_t edge_count() const;

private:
    struct EdgePredicate;
    struct VertexPredicate;

    graph_t graph_;
};

#endif
