/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "ifmap_link.h"

#include <sstream>
#include <pugixml/pugixml.hpp>
#include "ifmap/ifmap_table.h"

using namespace std;

IFMapLink::IFMapLink(DBGraphBase::edge_descriptor edge_id)
    : DBGraphEdge(edge_id) {
}

void IFMapLink::SetProperties(IFMapNode *left, IFMapNode *right,
                              const string &metadata, uint64_t sequence_number,
                              const IFMapOrigin &origin) {
    left_node_ = left;
    left_id_ = make_pair(left->table()->Typename(), left->name());
    right_node_ = right;
    right_id_ = make_pair(right->table()->Typename(), right->name());
    metadata_ = metadata;
    LinkOriginInfo origin_info(origin, sequence_number);
    origin_info_.push_back(origin_info);
}

void IFMapLink::UpdateProperties(const IFMapOrigin &in_origin,
                                 uint64_t sequence_number) {
    for (std::vector<LinkOriginInfo>::iterator iter = origin_info_.begin();
         iter != origin_info_.end(); ++iter) {
        LinkOriginInfo *origin_info = iter.operator->();
        if (origin_info->origin == in_origin) {
            origin_info->sequence_number = sequence_number;
        }
    }
}

void IFMapLink::ClearNodes() {
    left_node_ = NULL;
    right_node_ = NULL;
}

IFMapNode *IFMapLink::LeftNode(DB *db) {
    if (IsDeleted()) {
        return IFMapNode::DescriptorLookup(db, left_id_);
    }
    return left_node_;
}

const IFMapNode *IFMapLink::LeftNode(DB *db) const {
    if (IsDeleted()) {
        return IFMapNode::DescriptorLookup(db, left_id_);
    }
    return left_node_;
}

IFMapNode *IFMapLink::RightNode(DB *db) {
    if (IsDeleted()) {
        return IFMapNode::DescriptorLookup(db, right_id_);
    }
    return right_node_;
}

const IFMapNode *IFMapLink::RightNode(DB *db) const {
    if (IsDeleted()) {
        return IFMapNode::DescriptorLookup(db, right_id_);
    }
    return right_node_;
}

DBEntry::KeyPtr IFMapLink::GetDBRequestKey() const {
    DBEntry::KeyPtr key;
    return key;
}

void IFMapLink::SetKey(const DBRequestKey *genkey) {
}

string IFMapLink::ToString() const {
    ostringstream repr;
    repr << "link <" << left_id_.first << ':' << left_id_.second;
    repr << "," << right_id_.first << ':' << right_id_.second << ">";
    return repr.str();
}

bool IFMapLink::IsLess(const DBEntry &rgen) const {
    const IFMapLink &rhs = static_cast<const IFMapLink &>(rgen);
    return edge_id() < rhs.edge_id();
}

void IFMapLink::AddOriginInfo(const IFMapOrigin &in_origin, uint64_t seq_num) {
    for (std::vector<LinkOriginInfo>::iterator iter = origin_info_.begin();
         iter != origin_info_.end(); ++iter) {
        LinkOriginInfo *origin_info = iter.operator->();
        if (origin_info->origin == in_origin) {
            origin_info->sequence_number = seq_num;
            return;
        }
    }
    LinkOriginInfo origin_info(in_origin, seq_num);
    origin_info_.push_back(origin_info);
}

void IFMapLink::RemoveOriginInfo(IFMapOrigin::Origin in_origin) {
    for (std::vector<LinkOriginInfo>::iterator iter = origin_info_.begin();
         iter != origin_info_.end(); ++iter) {
        LinkOriginInfo *origin_info = iter.operator->();
        if (origin_info->origin.origin == in_origin) {
            origin_info_.erase(iter);
            break;
        }
    }
}

IFMapLink::LinkOriginInfo IFMapLink::GetOriginInfo(
        IFMapOrigin::Origin in_origin, bool *exists) {
    for (std::vector<LinkOriginInfo>::iterator iter = origin_info_.begin();
         iter != origin_info_.end(); ++iter) {
        LinkOriginInfo *origin_info = iter.operator->();
        if (origin_info->origin.origin == in_origin) {
            *exists = true;
            return *origin_info;
        }
    }

    LinkOriginInfo origin_info;
    *exists = false;
    return origin_info;
}

bool IFMapLink::HasOrigin(IFMapOrigin::Origin in_origin) {
    for (std::vector<LinkOriginInfo>::iterator iter = origin_info_.begin();
         iter != origin_info_.end(); ++iter) {
        LinkOriginInfo *origin_info = iter.operator->();
        if (origin_info->origin.origin == in_origin) {
            return true;
        }
    }
    return false;
}

uint64_t IFMapLink::sequence_number(IFMapOrigin::Origin in_origin,
                                    bool *exists) {
    for (std::vector<LinkOriginInfo>::iterator iter = origin_info_.begin();
         iter != origin_info_.end(); ++iter) {
        LinkOriginInfo *origin_info = iter.operator->();
        if (origin_info->origin.origin == in_origin) {
            *exists = true;
            return origin_info->sequence_number;
        }
    }
    *exists = false;
    return 0;
}

void IFMapLink::EncodeLinkInfo(pugi::xml_node *parent) const {
    pugi::xml_node metadata_node = parent->append_child("metadata");
    metadata_node.append_attribute("type") = metadata_.c_str();
}
