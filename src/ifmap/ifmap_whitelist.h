/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __ctrlplane__ifmap_whitelist__
#define __ctrlplane__ifmap_whitelist__

#include <boost/iterator/iterator_facade.hpp>
#include <boost/ptr_container/ptr_map.hpp>
#include <boost/ptr_container/ptr_vector.hpp>
#include "db/db_graph_edge.h"
#include "db/db_graph_vertex.h"
#include "schema/vnc_cfg_types.h"

struct IFMapTypenameWhiteList;
struct IFMapGTFVisitor;

// IFMap graph traversal filter node - used to create the traversal filter
class IFMapGTFNode : public DBGraphVertex {
public:
    IFMapGTFNode(std::string name) : name_(name) {
    }

    virtual KeyPtr GetDBRequestKey() const {
        KeyPtr nil;
        return nil;
    }
    virtual void SetKey(const DBRequestKey *key) {
    }
    virtual bool IsLess(const DBEntry &rhs) const {
        const IFMapGTFNode &v = static_cast<const IFMapGTFNode &>(rhs);
        return name_ < v.name_;
    }

    std::string& name() { return name_; }
    virtual std::string ToString() const { return name_; }

private:
    std::string name_;
};

// IFMap graph traversal filter link - used to create the traversal filter
class IFMapGTFLink : public DBGraphEdge {
public:
    IFMapGTFLink(DBGraphBase::edge_descriptor edge_id, IFMapGTFNode* left,
                 IFMapGTFNode *right, std::string metadata) :
        DBGraphEdge(edge_id), left_(left), right_(right), metadata_(metadata) {
    }
    virtual std::string ToString() const {
        std::ostringstream ss;
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
        const IFMapGTFLink &e = static_cast<const IFMapGTFLink &>(rhs);
        return edge_id() < e.edge_id();
    }
    IFMapGTFNode *left() { return left_; };
    IFMapGTFNode *right() { return right_; };
    std::string metadata() { return metadata_; };

private:
    IFMapGTFNode *left_;
    IFMapGTFNode *right_;
    std::string metadata_;
};

struct IFMapGTFVisitor {
    typedef std::list<IFMapGTFNode *> NodeList;
    IFMapGTFVisitor(IFMapTypenameWhiteList *white_list);
    ~IFMapGTFVisitor();
    void VertexVisitor(DBGraphVertex *v);
    void VertexFinish(DBGraphVertex *v);
    void EdgeVisitor(DBGraphEdge *e);
    void InsertIntoUnfinished(IFMapGTFNode *node);
    void PreBfsSetup(IFMapGTFNode *node);

    std::map<std::string, IFMapGTFNode *> visited_node_map_;
    NodeList unfinished_list_;
    NodeList finished_list_;
    IFMapTypenameWhiteList *white_list_;
    IFMapGTFNode *current_node_;
};

class IFMapGraphTraversalFilterCalculator {
public:
    typedef boost::ptr_map<std::string, IFMapGTFNode> NodePtrMap;
    typedef boost::ptr_vector<IFMapGTFLink> LinkPtrStore;
    typedef std::map<std::string, std::string> StringMap;
    IFMapGraphTraversalFilterCalculator(vnc_cfg_FilterInfo &filter_info,
                                        IFMapTypenameWhiteList *white_list);

private:
    std::auto_ptr<DBGraph> graph_;
    vnc_cfg_FilterInfo filter_info_;
    IFMapGTFVisitor visitor_;
    // pointer-maps for automatic disposal of allocated memory
    NodePtrMap node_map_;
    LinkPtrStore link_store_;
    StringMap node_blacklist_map_; // nodes we dont want in the graph

    void CreateNodeBlackList();
    bool NodeInBlacklist(std::string name);
    void CreateGraph();
    void RunBFS();
    IFMapGTFNode *FindNode(std::string name);
    void AddNode(std::string name);
    void AddLink(std::string from, std::string to, std::string metadata);
    void PreBfsSetup(IFMapGTFNode *node);
};

#endif /* defined(__ctrlplane__ifmap_whitelist__) */
