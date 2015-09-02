/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_policy_graph_base_h
#define ctrlplane_policy_graph_base_h

#include <boost/graph/graph_traits.hpp>
#include <boost/graph/adjacency_list.hpp>

class PolicyGraphVertex;
class PolicyGraphEdge;

class PolicyGraphBase {
public:
    struct VertexProperties {
        VertexProperties() : entry(NULL) {
        }
        PolicyGraphVertex *entry;
    };
    struct EdgeProperties {
        EdgeProperties() :  edge(NULL) {
        }
        PolicyGraphEdge *edge;
    };

    typedef boost::adjacency_list<
        boost::setS, boost::listS, boost::bidirectionalS,
        VertexProperties, EdgeProperties
    > graph_t;
    typedef boost::graph_traits<graph_t >::vertex_descriptor vertex_descriptor;
    typedef boost::graph_traits<graph_t >::edge_descriptor edge_descriptor;
    typedef boost::graph_traits<graph_t >::adjacency_iterator adjacency_iterator;
    typedef boost::graph_traits<graph_t >::edge_iterator edge_iterator;
    typedef boost::graph_traits<graph_t >::out_edge_iterator out_edge_iterator;
    typedef boost::graph_traits<graph_t >::in_edge_iterator in_edge_iterator;
    typedef graph_t::inv_adjacency_iterator inv_adjacency_iterator;
};

#endif // ctrlplane_policy_graph_base_h
