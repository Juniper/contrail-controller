/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "ifmap/ifmap_whitelist.h"

#include <boost/bind.hpp>
#include "ifmap/ifmap_util.h"

IFMapGTFVisitor::IFMapGTFVisitor(IFMapTypenameWhiteList *white_list) 
    : white_list_(white_list) {
}

IFMapGTFVisitor::~IFMapGTFVisitor() {
    visited_node_map_.clear();
    unfinished_list_.clear();
    finished_list_.clear();
}

void IFMapGTFVisitor::PreBfsSetup(IFMapGTFNode *node) {
    // The beginning node is already visited
    current_node_ = node;
    visited_node_map_.insert(make_pair(node->name(), node));
}

void IFMapGTFVisitor::VertexVisitor(DBGraphVertex *v) {
}

// Called 'once' for each node AFTER all its edges have been visited
void IFMapGTFVisitor::VertexFinish(DBGraphVertex *v) {
    IFMapGTFNode *node = static_cast<IFMapGTFNode *>(v);
    // Add the node to the white list and mark it finished
    white_list_->include_vertex.push_back(node->name());
    finished_list_.push_back(node);
    // Pop the next node from the unfinished list so that we can use it as the
    // 'left' in the filter string
    if (!unfinished_list_.empty()) {
        current_node_ = unfinished_list_.front();
        unfinished_list_.pop_front();
    }
}

void IFMapGTFVisitor::InsertIntoUnfinished(IFMapGTFNode *node) {
    // Ignore nodes that have been completely processed
    for (NodeList::iterator iter = finished_list_.begin();
         iter != finished_list_.end(); ++iter) {
        IFMapGTFNode *curr = *iter;
        if (curr == node) {
            return;
        }
    }
    // Ignore nodes that have already been seen as neighbors and are waiting to
    // be processed
    for (NodeList::iterator iter = unfinished_list_.begin();
         iter != unfinished_list_.end(); ++iter) {
        IFMapGTFNode *curr = *iter;
        if (curr == node) {
            return;
        }
    }
    unfinished_list_.push_back(node);
}

void IFMapGTFVisitor::EdgeVisitor(DBGraphEdge *e) {
    IFMapGTFLink *link = static_cast<IFMapGTFLink *>(e);

    // Setup 'left' and 'right' correctly so that we create the correct filter
    // string below. One of 'left' or 'right' must be the current_node_
    IFMapGTFNode *left, *right;
    if (link->left()->name().compare(current_node_->name()) == 0) {
        left = link->left();
        right = link->right();
    } else {
        left = link->right();
        right = link->left();
    }

    // If the neighbor has not been visited yet...
    if (visited_node_map_.find(right->name()) == visited_node_map_.end()) {
        // Include the link in the white list if the right node was not
        // visited yet
        // EG: "source=virtual-network,target=virtual-machine-interface");
        std::string edge = 
            "source=" + left->name() + ",target=" + right->name();
        white_list_->include_edge.push_back(edge);
        // Color the neighbor to indicate it was visited
        visited_node_map_.insert(make_pair(right->name(), right));
        InsertIntoUnfinished(right);
    }
}

IFMapGraphTraversalFilterCalculator::IFMapGraphTraversalFilterCalculator(
    vnc_cfg_FilterInfo &filter_info, IFMapTypenameWhiteList *white_list) 
        : filter_info_(filter_info), visitor_(white_list) {
    graph_.reset(new DBGraph());
    CreateNodeBlackList();
    CreateGraph();
    RunBFS();
}

// Hardcoded
void IFMapGraphTraversalFilterCalculator::CreateNodeBlackList() {
    node_blacklist_map_.insert(std::make_pair("config-root", "config-root"));
    node_blacklist_map_.insert(std::make_pair("network-policy",
                                              "network-policy"));
}

bool IFMapGraphTraversalFilterCalculator::NodeInBlacklist(std::string name) {
    if (node_blacklist_map_.find(name) == node_blacklist_map_.end()) {
        return false;
    } else {
        return true;
    }
}

void IFMapGraphTraversalFilterCalculator::CreateGraph() {
    for (vnc_cfg_FilterInfo::iterator iter = filter_info_.begin();
         iter != filter_info_.end(); ++iter) {
        vnc_cfg_GraphFilterInfo *filter = iter.operator->();

        if (NodeInBlacklist(filter->left_) || NodeInBlacklist(filter->right_)
            || NodeInBlacklist(filter->metadata_)) {
            continue;
        }

        AddNode(filter->left_);
        AddNode(filter->right_);
        if (filter->linkattr_) {
            AddNode(filter->metadata_);
            AddLink(filter->left_, filter->metadata_, filter->metadata_);
            AddLink(filter->metadata_, filter->right_, filter->metadata_);
        } else {
            AddLink(filter->left_, filter->right_, filter->metadata_);
        }
    }
}

void IFMapGraphTraversalFilterCalculator::RunBFS() {
    DBGraph *graph = graph_.get();
    // Start the walk from the 'virtual-router' node
    IFMapGTFNode *vr_node = FindNode("virtual-router");
    PreBfsSetup(vr_node);
    graph->Visit(vr_node,
                 boost::bind(&IFMapGTFVisitor::VertexVisitor, &visitor_, _1),
                 boost::bind(&IFMapGTFVisitor::EdgeVisitor, &visitor_, _1),
                 boost::bind(&IFMapGTFVisitor::VertexFinish, &visitor_, _1));
}

void IFMapGraphTraversalFilterCalculator::PreBfsSetup(IFMapGTFNode *node) {
    visitor_.PreBfsSetup(node);
}

IFMapGTFNode *IFMapGraphTraversalFilterCalculator::FindNode(std::string name) {
    NodePtrMap::iterator iter = node_map_.find(name);
    if (iter == node_map_.end()) {
        return NULL;
    }
    return iter->second;
}

void IFMapGraphTraversalFilterCalculator::AddNode(std::string name) {
    DBGraph *graph = graph_.get();
    NodePtrMap::iterator iter = node_map_.find(name);
    if (iter == node_map_.end()) {
        IFMapGTFNode *node = new IFMapGTFNode(name);
        graph->AddNode(node);
        node_map_.insert(name, node); // node will be automatically disposed
    }
}

// Precondition: nodes for left and right must exist
void IFMapGraphTraversalFilterCalculator::AddLink(std::string from,
                                                  std::string to,
                                                  std::string metadata) {
    DBGraph *graph = graph_.get();
    IFMapGTFNode *left = FindNode(from);
    assert(left);
    IFMapGTFNode *right = FindNode(to);
    assert(right);
    IFMapGTFLink *link = 
        static_cast<IFMapGTFLink *>(graph->GetEdge(left, right));
    if (link == NULL) {
        DBGraph::Edge edge = graph->Link(left, right);
        IFMapGTFLink *link = new IFMapGTFLink(edge, left, right, metadata);
        graph->SetEdgeProperty(link);
        link_store_.push_back(link); // 'link' will be automatically disposed
    }
}

