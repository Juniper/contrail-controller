/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "db/db_graph.h"

#include <ostream>
#include <boost/bind.hpp>
#include <boost/foreach.hpp>

#include "base/logging.h"
#include "base/util.h"
#include "db/db.h"
#include "db/db_graph_edge.h"
#include "testing/gunit.h"

using namespace std;

class TestVertex : public DBGraphVertex {
  public:
    TestVertex(const string &name) : name_(name) { }
    virtual string ToString() const {
        string repr("vertex:");
        repr += name_;
        return repr;
    }

    virtual KeyPtr GetDBRequestKey() const {
        KeyPtr nil;
        return nil;
    }
    virtual void SetKey(const DBRequestKey *key) {
    }
    virtual bool IsLess(const DBEntry &rhs) const {
        const TestVertex &v = static_cast<const TestVertex &>(rhs);
        return name_ < v.name_;
    }

    const string &name() const { return name_; }

  private:
    string name_;
    DISALLOW_COPY_AND_ASSIGN(TestVertex);
};

class TestEdge : public DBGraphEdge {
  public:
    explicit TestEdge(const string &name) : DBGraphEdge(), name_(name) { }
    virtual string ToString() const {
        ostringstream ss;
        ss << edge_id();
        return ss.str();
    }
    virtual KeyPtr GetDBRequestKey() const {
        KeyPtr nil;
        return nil;
    }
    virtual void SetKey(const DBRequestKey *key) {
    }
    virtual bool IsLess(const DBEntry &rhs) const {
        const TestEdge &e = static_cast<const TestEdge &>(rhs);
        return edge_id() < e.edge_id();
    }

    const std::string &name() const {
        return name_;
    }

  private:
    const string name_;
    DISALLOW_COPY_AND_ASSIGN(TestEdge);
};

class DBGraphTest : public ::testing::Test {
  protected:

    void CreateVertex(const string &name) {
        TestVertex *v = new TestVertex(name);
        vertices_.push_back(v);
        graph_.AddNode(v);
    }

    void CreateEdge(TestVertex *lhs, TestVertex *rhs) {
        ostringstream ss;
        ss << "TestEdge" << edges_.size();
        TestEdge *e = new TestEdge(ss.str());
        graph_.Link(lhs, rhs, e);
        edges_.push_back(e);
    }

    virtual void TearDown() {
        STLDeleteValues(&edges_);
        STLDeleteValues(&vertices_);
        graph_.clear();
    }

    bool HasEdge(const set<DBGraphEdge *> &edge_list, const char *s,
                 const char *t) {
        string sstr("vertex:"); sstr += s;
        string tstr("vertex:"); tstr += t;
        for (set<DBGraphEdge *>::const_iterator iter = edge_list.begin();
             iter != edge_list.end(); ++iter) {
            DBGraphEdge *edge = *iter;
            DBGraphVertex *src = edge->source(&graph_);
            DBGraphVertex *tgt = edge->target(&graph_);
            if (src->ToString() == sstr && tgt->ToString() == tstr) {
                return true;
            }
        }
        return false;
    }

    DBGraph graph_;
    vector<TestVertex *> vertices_;
    vector<TestEdge *> edges_;
};

TEST_F(DBGraphTest, VertexRemove) {
    CreateVertex("a");
    CreateVertex("b");
    CreateVertex("c");
    CreateVertex("d");

    CreateEdge(vertices_[0], vertices_[2]);
    CreateEdge(vertices_[0], vertices_[3]);

    graph_.RemoveNode(vertices_[1]);
    delete vertices_[1];
    vertices_.erase(vertices_.begin() + 1);

    int count = 0;
    for (DBGraph::edge_iterator iter = graph_.edge_list_begin();
         iter != graph_.edge_list_end(); ++iter) {
        const DBGraph::DBEdgeInfo &tuple = *iter;
        switch (count) {
            case 0:
                EXPECT_EQ("vertex:a", boost::get<0>(tuple)->ToString());
                EXPECT_EQ("vertex:c", boost::get<1>(tuple)->ToString());
                break;
            case 1:
                EXPECT_EQ("vertex:a", boost::get<0>(tuple)->ToString());
                EXPECT_EQ("vertex:d", boost::get<1>(tuple)->ToString());
                break;
            default:
                EXPECT_TRUE(false);
        }
        count++;
    }
    EXPECT_EQ(2, count);
}

struct GraphVisitor {
    void VertexVisitor(DBGraphVertex *v) {
        vertices.push_back(v);
    }
    void EdgeVisitor(DBGraphEdge *e) {
        edges.insert(e);
    }
    void clear() {
        vertices.clear();
        edges.clear();
    }
    vector<DBGraphVertex *> vertices;
    set<DBGraphEdge *> edges;
};

TEST_F(DBGraphTest, EdgeRemove) {
    CreateVertex("a");
    CreateVertex("b");
    CreateVertex("c");
    CreateVertex("d");

    CreateEdge(vertices_[0], vertices_[2]);
    CreateEdge(vertices_[0], vertices_[3]);
    CreateEdge(vertices_[1], vertices_[2]);
    CreateEdge(vertices_[1], vertices_[3]);

    GraphVisitor visitor;
    graph_.Visit(vertices_[0],
                 boost::bind(&GraphVisitor::VertexVisitor, &visitor, _1),
                 boost::bind(&GraphVisitor::EdgeVisitor, &visitor, _1));
    EXPECT_EQ(4U, visitor.vertices.size());
    EXPECT_EQ(4U, visitor.edges.size());

    EXPECT_TRUE(HasEdge(visitor.edges, "b", "c"));

    graph_.Unlink(edges_[2]);
    delete edges_[2];
    edges_.erase(edges_.begin() + 2);

    visitor.clear();
    graph_.Visit(vertices_[0],
                 boost::bind(&GraphVisitor::VertexVisitor, &visitor, _1),
                 boost::bind(&GraphVisitor::EdgeVisitor, &visitor, _1));
    EXPECT_EQ(4U, visitor.vertices.size());
    EXPECT_EQ(3U, visitor.edges.size());
    EXPECT_FALSE(HasEdge(visitor.edges, "b", "c"));

    graph_.Unlink(edges_[2]);
    delete edges_[2];
    edges_.erase(edges_.begin() + 2);

    graph_.RemoveNode(vertices_[1]);
    delete vertices_[1];
    vertices_.erase(vertices_.begin() + 1);

    visitor.clear();
    graph_.Visit(vertices_[0],
                 boost::bind(&GraphVisitor::VertexVisitor, &visitor, _1),
                 boost::bind(&GraphVisitor::EdgeVisitor, &visitor, _1));
    EXPECT_EQ(3U, visitor.vertices.size());
    EXPECT_EQ(2U, visitor.edges.size());

    EXPECT_TRUE(HasEdge(visitor.edges, "a", "c"));
    EXPECT_TRUE(HasEdge(visitor.edges, "a", "d"));
}

struct TestVisitorFilter : public DBGraph::VisitorFilter {
    virtual ~TestVisitorFilter() { }
    virtual bool VertexFilter(const DBGraphVertex *vertex) const {
        const TestVertex *node = static_cast<const TestVertex *>(vertex);
        std::cout << "TestVisitorFilter: node is " << node->name() << std::endl;
        BOOST_FOREACH(const string &incl, include_vertex) {
            if (node->name() == incl) {
                std::cout << "TestVisitorFilter: node " << node->name()
                          << " in filter" << std::endl;
                return true;
            }
        }
        return false;
    }
    virtual bool EdgeFilter(const DBGraphVertex *source,
                            const DBGraphVertex *target,
                            const DBGraphEdge *edge) const {
        return true;
    }

    AllowedEdgeRetVal AllowedEdges(const DBGraphVertex *source) const {
        return std::make_pair(true, AllowedEdgeSet());
    }

    std::vector<std::string> include_vertex;
    std::vector<std::string> include_edge;
};

TEST_F(DBGraphTest, GraphWithFilterTraversal) {
    CreateVertex("a");
    CreateVertex("b");
    CreateVertex("c");
    CreateVertex("d");

    CreateEdge(vertices_[0], vertices_[2]);
    CreateEdge(vertices_[0], vertices_[3]);
    CreateEdge(vertices_[1], vertices_[2]);
    CreateEdge(vertices_[1], vertices_[3]);

    TestVisitorFilter test_filter;
    test_filter.include_vertex.push_back("a");
    test_filter.include_vertex.push_back("d");

    GraphVisitor test_visitor;
    test_visitor.clear();
    graph_.Visit(vertices_[0],
                 boost::bind(&GraphVisitor::VertexVisitor, &test_visitor, _1),
                 boost::bind(&GraphVisitor::EdgeVisitor, &test_visitor, _1),
                 test_filter);
    EXPECT_EQ(2U, test_visitor.vertices.size());
}

int main(int argc, char **argv) {
    LoggingInit();
    ::testing::InitGoogleTest(&argc, argv);

    return RUN_ALL_TESTS();
}
