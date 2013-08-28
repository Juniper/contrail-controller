/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "ifmap_node.h"

#include <pugixml/pugixml.hpp>

#include "ifmap/ifmap_table.h"

using namespace std;
using pugi::xml_node;


IFMapNode::IFMapNode(IFMapTable *table)
    : table_(table) {
}

struct IFMapObjectDeleter {
    void operator()(IFMapObject *obj) const {
        intrusive_ptr_release(obj);
    }
};

IFMapNode::~IFMapNode() {
    list_.clear_and_dispose(IFMapObjectDeleter());
}

string IFMapNode::ToString() const {
    string repr(table_->Typename());
    repr += ":";
    repr += name_;
    return repr;
}

IFMapObject *IFMapNode::Find(IFMapOrigin origin) {
    for (ObjectList::iterator iter = list_.begin(); iter != list_.end();
         ++iter) {
        IFMapObject *object = iter.operator->();
        if (object->origin() == origin) {
            return object;
        }
    }
    return NULL;
}

void IFMapNode::Insert(IFMapObject *obj) {
    intrusive_ptr_add_ref(obj);
    if (obj->origin().IsOriginXmpp()) {
        list_.push_back(*obj);
    } else {
        list_.push_front(*obj);
    }
}

void IFMapNode::Remove(IFMapObject *obj) {
    list_.erase(list_.iterator_to(*obj));
    intrusive_ptr_release(obj);
}

IFMapObject *IFMapNode::GetObject() {
    if (list_.empty()) {
        return NULL;
    }
    return &list_.front();
}

const IFMapObject *IFMapNode::GetObject() const {
    if (list_.empty()) {
        return NULL;
    }
    return &list_.front();
}

void IFMapNode::PrintAllObjects() {
    cout << name_ << ": " << list_.size() << " objects" << endl;
    for (ObjectList::iterator iter = list_.begin(); iter != list_.end();
         ++iter) {
        IFMapObject *object = iter.operator->();
        cout << "\t" << object->origin().ToString() << endl;
    }
}

void IFMapNode::EncodeNodeDetail(pugi::xml_node *parent) const {
    xml_node node = parent->append_child("node");
    node.append_attribute("type") = table_->Typename();
    node.append_child("name").text().set(name_.c_str());
    const IFMapObject *object = GetObject();
    if (object != NULL) {
        object->EncodeUpdate(&node);
    }
}

void IFMapNode::EncodeNode(xml_node *parent) const {
    xml_node node = parent->append_child("node");
    node.append_attribute("type") = table_->Typename();
    node.append_child("name").text().set(name_.c_str());
}

void IFMapNode::EncodeNode(const Descriptor &descriptor, xml_node *parent) {
    xml_node node = parent->append_child("node");
    node.append_attribute("type") = descriptor.first.c_str();
    node.append_child("name").text().set(descriptor.second.c_str());
}

DBEntryBase::KeyPtr IFMapNode::GetDBRequestKey() const {
    IFMapTable::RequestKey *keyptr = new IFMapTable::RequestKey();
    keyptr->id_name = name_;
    return KeyPtr(keyptr);
}

void IFMapNode::SetKey(const DBRequestKey *genkey) {
    const IFMapTable::RequestKey *keyptr =
    static_cast<const IFMapTable::RequestKey *>(genkey);
    name_ = keyptr->id_name;
}

IFMapNode *IFMapNode::DescriptorLookup(
        DB *db, const IFMapNode::Descriptor &descriptor) {
    if (db == NULL) {
        return NULL;
    }
    IFMapTable *table = IFMapTable::FindTable(db, descriptor.first);
    if (table == NULL) {
        return NULL;
    }
    return table->FindNode(descriptor.second);
}
