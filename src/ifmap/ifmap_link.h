/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __ctrlplane__ifmap_link__
#define __ctrlplane__ifmap_link__

#include "db/db_graph_edge.h"
#include "db/db_table.h"
#include "ifmap/ifmap_node.h"
#include "ifmap/ifmap_origin.h"

// An IFMapLink represents an edge in the ifmap configuration graph.
// When links are deleted, the cached left and right node_ members are
// cleared.
class IFMapLink : public DBGraphEdge {
public:
    struct LinkOriginInfo {
        explicit LinkOriginInfo() : 
            origin(IFMapOrigin::UNKNOWN), sequence_number(0) {
        }
        explicit LinkOriginInfo(IFMapOrigin origin, uint64_t seq_num) :
            origin(origin), sequence_number(seq_num) {
        }
        IFMapOrigin origin;
        uint64_t sequence_number;
    };

    IFMapLink(DBGraphBase::edge_descriptor edge_id);
    
    // Initialize the link.
    void SetProperties(IFMapNode *left, IFMapNode *right,
                       const std::string &metadata, uint64_t sequence_number,
                       const IFMapOrigin &origin);
    // Update some fields
    void UpdateProperties(const IFMapOrigin &in_origin, 
                          uint64_t sequence_number);
    // Called by IFMapLinkTable when the node is deleted.
    void ClearNodes();
    
    virtual KeyPtr GetDBRequestKey() const;
    virtual void SetKey(const DBRequestKey *genkey);
    virtual std::string ToString() const;
    virtual bool IsLess(const DBEntry &rgen) const;
    
    // Return the left node. If the link is deleted, the node is retrieved
    // by doing a database table lookup iff db in non-NULL. If db is NULL,
    // the method returns NULL. The actual node may have already been deleted.
    IFMapNode *LeftNode(DB *db);
    const IFMapNode *LeftNode(DB *db) const;
    
    // Return the right node. As with the corresponding Left methods these
    // return the cached value if the node is not deleted and perform a DB
    // lookup (when the parameter db is non-NULL) when the node is deleted.
    IFMapNode *RightNode(DB *db);
    const IFMapNode *RightNode(DB *db) const;
    
    IFMapNode *left() { return left_node_; }
    const IFMapNode *left() const { return left_node_; }
    IFMapNode *right() { return right_node_; }
    const IFMapNode *right() const { return right_node_; }

    const IFMapNode::Descriptor &left_id() const { return left_id_; }
    const IFMapNode::Descriptor &right_id() const { return right_id_; }
    
    const std::string &metadata() const { return metadata_; }

    void AddOriginInfo(const IFMapOrigin &in_origin, uint64_t seq_num);
    void RemoveOriginInfo(IFMapOrigin::Origin in_origin);
    bool HasOrigin(IFMapOrigin::Origin in_origin);
    bool is_origin_empty() { return origin_info_.empty(); }
    void EncodeLinkInfo(pugi::xml_node *parent) const;

    // if exists is true, return value will have relevant entry
    IFMapLink::LinkOriginInfo GetOriginInfo(IFMapOrigin::Origin in_origin,
                                            bool *exists);
    // if exists is true, return value will have relevant sequence number
    uint64_t sequence_number(IFMapOrigin::Origin in_origin, bool *exists);

private:
    friend class ShowIFMapLinkTable;

    std::string metadata_;
    IFMapNode::Descriptor left_id_;
    IFMapNode::Descriptor right_id_;
    IFMapNode *left_node_;
    IFMapNode *right_node_;
    std::vector<LinkOriginInfo> origin_info_;
    DISALLOW_COPY_AND_ASSIGN(IFMapLink);
};

#endif /* defined(__ctrlplane__ifmap_link__) */
