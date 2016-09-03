/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_db_graph_base_h
#define ctrlplane_db_graph_base_h

#include <boost/graph/graph_traits.hpp>
#include <boost/graph/adjacency_list.hpp>
#include <boost/graph/properties.hpp>

class DBGraphVertex;
class DBGraphEdge;

template <class StoredEdge>
struct order_by_name : public std::binary_function<StoredEdge, StoredEdge, bool>
{
  bool operator()(const StoredEdge& e1, const StoredEdge& e2) const;
};

struct ordered_set_by_nameS { };

namespace boost {
  template <class ValueType>
  struct container_gen<ordered_set_by_nameS, ValueType> {
    typedef std::multiset<ValueType, order_by_name<ValueType> > type;
  };
  template <>
  struct parallel_edge_traits<ordered_set_by_nameS> {
      typedef disallow_parallel_edge_tag type;
  };
}

class DBGraphBase {
public:
    DBGraphBase() : graph_walk_num_(0) {
    }

    struct VertexProperties {
        VertexProperties() : entry(NULL) {
        }
        DBGraphVertex *entry;
    };

    struct EdgeProperties {
        EdgeProperties(std::string name, DBGraphEdge *e) : name_(name), edge(e) {
        }
        const std::string &name() const {
            return name_;
        }
        std::string name_;
        DBGraphEdge *edge;
    };

    typedef boost::adjacency_list<
        ordered_set_by_nameS, boost::listS, boost::undirectedS,
        VertexProperties, EdgeProperties> graph_t;
    typedef boost::graph_traits<graph_t >::vertex_descriptor vertex_descriptor;
    typedef boost::graph_traits<graph_t >::edge_descriptor edge_descriptor;
    typedef boost::graph_traits<graph_t >::adjacency_iterator adjacency_iterator;
    typedef boost::graph_traits<graph_t >::edge_iterator edge_iterator;
    typedef boost::graph_traits<graph_t >::out_edge_iterator out_edge_iterator;

    typedef graph_t::StoredEdge StoredEdge;
    typedef boost::container_gen<graph_t::out_edge_list_selector,
            StoredEdge>::type OutEdgeListType;
    typedef OutEdgeListType::iterator OutEdgeIterator;

    typedef graph_t::EdgeContainer EdgeContainer;
    typedef EdgeContainer::value_type EdgeType;

    uint64_t get_graph_walk_num() {
        return ++graph_walk_num_;
    }

private:
    uint64_t graph_walk_num_;
};

#endif
