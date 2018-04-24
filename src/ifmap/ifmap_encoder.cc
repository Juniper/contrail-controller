/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "ifmap/ifmap_encoder.h"

#include <sstream>
#include "ifmap/ifmap_link.h"
#include "ifmap/ifmap_object.h"
#include "ifmap/ifmap_update.h"

using namespace pugi;
using namespace std;

IFMapMessage::IFMapMessage() : op_type_(NONE), node_count_(0),
    objects_per_message_(kObjectsPerMessage) {
    // init empty document
    Open();
}

void IFMapMessage::Open() {
    xml_node iq = doc_.append_child("iq");
    // set iq (type, from, to) attributes
    iq.append_attribute("type") = "set";
    iq.append_attribute("from") = "network-control@contrailsystems.com";
    iq.append_attribute("to") = "";
    config_ = iq.append_child("config");
}

void IFMapMessage::Close() {
    ostringstream oss;
    doc_.save(oss);
    str_ = oss.str();
}

void IFMapMessage::SetReceiverInMsg(const std::string &cli_identifier) {
    xml_node iq = doc_.child("iq");
    assert(iq);
    xml_attribute iqattr = iq.attribute("to");
    assert(iqattr);
    std::string str(cli_identifier);
    str += "/config";
    iqattr.set_value(str.c_str());
}

void IFMapMessage::SetObjectsPerMessage(int num) {
    objects_per_message_ = num;
}

void IFMapMessage::EncodeUpdate(const IFMapUpdate *update) {
    // update is either of type UPDATE OR DELETE
    if (update->IsUpdate()) {
        if (op_type_ != UPDATE) {
            op_node_ = config_.append_child("update");
            op_type_ = UPDATE;
        }
        if (update->data().type == IFMapObjectPtr::NODE) {
            EncodeNode(update);
        } else if (update->data().type == IFMapObjectPtr::LINK) {
            EncodeLink(update);
        } else {
            assert(0);
        }
    } else {
        if (op_type_ != DEL) {
            op_node_ = config_.append_child("delete");
            op_type_ = DEL;
        }
        if (update->data().type == IFMapObjectPtr::NODE) {
            EncodeNode(update);
        } else if (update->data().type == IFMapObjectPtr::LINK) {
            EncodeLink(update);
        } else {
            assert(0);
        }
    }
    node_count_++;
}

void IFMapMessage::EncodeNode(const IFMapUpdate *update) {
    IFMapNode *node = update->data().u.node;
    if (update->IsUpdate()) {
        node->EncodeNodeDetail(&op_node_);
    } else {
        node->EncodeNode(&op_node_);
    }
}

void IFMapMessage::EncodeLink(const IFMapUpdate *update) {
    xml_node link_node = op_node_.append_child("link");

    const IFMapLink *link = update->data().u.link;

    IFMapNode::EncodeNode(link->left_id(), &link_node);
    IFMapNode::EncodeNode(link->right_id(), &link_node);
    link->EncodeLinkInfo(&link_node);

    node_count_++;
}

bool IFMapMessage::IsFull() {
    return ((node_count_ >= objects_per_message_) ? true : false);
}

bool IFMapMessage::IsEmpty() {
    return ((node_count_ == 0) ? true : false);
}

//
// Reset the IFMapMessage to initial state so that it can be used to build
// the next config message.
//
// Using remove_child to remove the only child of the document is a better
// way to clear the document than using reset. The pugixml library allocates
// memory for a document in increments of 32KB pages and then manages smaller
// allocations (nodes or attributes) using these pages. Calling reset method
// on a document frees all the pages. In contrast, removing the only child
// node of the document returns the smaller allocations to the free pool of
// memory for the document, but doesn't free the pages themselves. This lets
// the library reuse the same memory when building the tree for each message.
//
void IFMapMessage::Reset() {
    doc_.remove_child("iq");
    node_count_ = 0;
    op_type_ = NONE;
    Open();
}
