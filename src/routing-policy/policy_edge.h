/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_policy_graph_edge_h
#define ctrlplane_policy_graph_edge_h

#include "base/util.h"

#include "routing-policy/policy_graph_base.h"

class PolicyGraph;
class PolicyGraphVertex;

class PolicyGraphEdge {
public:
    typedef PolicyGraphBase::edge_descriptor Edge;
    typedef PolicyGraphBase::vertex_descriptor Vertex;

    explicit PolicyGraphEdge(Edge edge_id);

    Edge edge_id() const { return edge_id_; }

    PolicyGraphVertex *source(PolicyGraph *graph);
    const PolicyGraphVertex *source(PolicyGraph *graph) const;

    PolicyGraphVertex *target(PolicyGraph *graph);
    const PolicyGraphVertex *target(PolicyGraph *graph) const;

    virtual bool IsMatchValue() {
        return false;
    }

    virtual bool IsNilMatch() {
        return false;
    }

    virtual bool IsNextFeed() {
        return false;
    }

    bool operator ==(const PolicyGraphEdge &rhs) const {
        return IsSame(rhs);
    }

    virtual bool IsSame(const PolicyGraphEdge &rhs) const = 0;

    // Comparator used for NextFeeder
    virtual bool IsLess(const PolicyGraphEdge &rhs) const = 0;

    bool operator<(const PolicyGraphEdge &rhs) const {
        return IsLess(rhs);
    }

private:
    Edge edge_id_;
    
    DISALLOW_COPY_AND_ASSIGN(PolicyGraphEdge);
};

class PolicyMatchValueEdge : public PolicyGraphEdge {
    virtual bool IsMatchValue() {
        return true;
    }
};

class PolicyNextFeederEdge : public PolicyGraphEdge {
    virtual bool IsNextFeed() {
        return false;
    }
};

class PolicyNilMatch : public PolicyGraphEdge {
    virtual bool IsNilMatch() {
        return false;
    }
};

#endif // ctrlplane_policy_graph_edge_h
