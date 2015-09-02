/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_db_graph_base_h
#define ctrlplane_db_graph_base_h

#include <boost/graph/graph_traits.hpp>
#include <boost/graph/adjacency_list.hpp>

class DBGraphVertex;
class DBGraphEdge;

class DBGraphBase {
public:
    struct VertexProperties {
        VertexProperties() : entry(NULL) {
        }
        DBGraphVertex *entry;
    };
    struct EdgeProperties {
        EdgeProperties() :  edge(NULL) {
        }
        DBGraphEdge *edge;
    };

    typedef boost::adjacency_list<
        boost::setS, boost::listS, boost::undirectedS,
        VertexProperties, EdgeProperties
    > graph_t;
    typedef boost::graph_traits<graph_t >::vertex_descriptor vertex_descriptor;
    typedef boost::graph_traits<graph_t >::edge_descriptor edge_descriptor;
    typedef boost::graph_traits<graph_t >::adjacency_iterator adjacency_iterator;
    typedef boost::graph_traits<graph_t >::edge_iterator edge_iterator;
    typedef boost::graph_traits<graph_t >::out_edge_iterator out_edge_iterator;
    
};

#endif
