/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "ifmap/ifmap_server_table.h"

#include <boost/algorithm/string.hpp>
#include <boost/checked_delete.hpp>
#include <boost/type_traits.hpp>

#include "base/compiler.h"
#include "base/logging.h"
#include "db/db.h"
#include "db/db_graph.h"
#include "db/db_table_partition.h"
#include "ifmap/autogen.h"
#include "ifmap/ifmap_link.h"
#include "ifmap/ifmap_link_table.h"
#include "ifmap/ifmap_log.h"
#include "ifmap/ifmap_log_types.h"

using namespace std;

IFMapServerTable::RequestData::RequestData() {
}

// Warning: std::auto_ptr<> will not call the destructor if the type is
// incomplete at the time the auto_ptr destructor is generated. Depending
// on the compiler this may occur at different times. With clang the
// auto_ptr appears to be generated when needed by an enclosing type.
// gcc appears to behave differently.
IFMapServerTable::RequestData::~RequestData() {
#if defined(__GNUC__) && (__GNUC_PREREQ(4, 2) > 0)
    boost::has_virtual_destructor<AutogenProperty>::type has_destructor;
    assert(has_destructor);
#endif
    boost::checked_delete(content.release());
}


IFMapServerTable::IFMapServerTable(DB *db, const string &name, DBGraph *graph)
        : IFMapTable(db, name), graph_(graph) {
}

auto_ptr<DBEntry> IFMapServerTable::AllocEntry(const DBRequestKey *key) const {
    auto_ptr<DBEntry> entry(
        new IFMapNode(const_cast<IFMapServerTable *>(this)));
    entry->SetKey(key);
    return entry;
}

static IFMapServerTable *TableFind(DB *db, const string &metadata) {
    string name = metadata;
    boost::replace_all(name, "-", "_");
    name = "__ifmap__." + name + ".0";
    IFMapServerTable *table =
            static_cast<IFMapServerTable *>(db->FindTable(name));
    return table;
}

IFMapNode *IFMapServerTable::EntryLookup(RequestKey *request) {
    auto_ptr<DBEntry> key(AllocEntry(request));
    IFMapNode *node = static_cast<IFMapNode *>(Find(key.get()));
    if ((node == NULL) || node->IsDeleted()) {
        return NULL;
    }
    return node;
}

IFMapNode *IFMapServerTable::EntryLocate(RequestKey *request, bool *changep) {
    auto_ptr<DBEntry> key(AllocEntry(request));
    IFMapNode *node = static_cast<IFMapNode *>(Find(key.get()));
    if (node != NULL) {
        if (node->IsDeleted()) {
            node->ClearDelete();
            graph_->AddNode(node);
            IFMAP_DEBUG(IFMapNodeOperation, "Re-creating", node->ToString());
            *changep = true;
        }
        return node;
    }
    *changep = true;
    node = const_cast<IFMapNode *>(
        static_cast<const IFMapNode *>(key.release()));
    DBTablePartition *partition =
            static_cast<DBTablePartition *>(GetTablePartition(0));
    partition->Add(node);
    graph_->AddNode(node);
    IFMAP_DEBUG(IFMapNodeOperation, "Creating", node->ToString());
    return node;
}

IFMapNode *IFMapServerTable::TableEntryLookup(IFMapServerTable *table,
                                              const string &id_name) {
    RequestKey request;
    request.id_name = id_name;
    return table->EntryLookup(&request);
}

IFMapNode *IFMapServerTable::TableEntryLocate(IFMapServerTable *table,
                                              const string &id_name,
                                              bool *changep) {
    RequestKey request;
    request.id_name = id_name;
    return table->EntryLocate(&request, changep);
}

void IFMapServerTable::LinkNodeAdd(DBGraphBase::edge_descriptor edge,
                                   IFMapNode *first, IFMapNode *second,
                                   const string &metadata,
                                   uint64_t sequence_number, 
                                   const IFMapOrigin &origin) {
    IFMapLinkTable *table = static_cast<IFMapLinkTable *>(
        database()->FindTable("__ifmap_metadata__.0"));
    assert(table != NULL);
    table->AddLink(edge, first, second, metadata, sequence_number, origin);
}

void IFMapServerTable::LinkNodeUpdate(IFMapLink *link, uint64_t sequence_number,
                                      const IFMapOrigin &origin) {
    link->set_last_change_at_to_now();
    link->UpdateProperties(origin, sequence_number);
}

void IFMapServerTable::LinkNodeDelete(IFMapNode *first, IFMapNode *second,
                                      const IFMapOrigin &origin) {
    IFMapLinkTable *table = static_cast<IFMapLinkTable *>(
        database()->FindTable("__ifmap_metadata__.0"));
    assert(table != NULL);
    table->DeleteLink(first, second, origin);
}

// Generate an unique key for a Link Attribute element. The generated key should
// be independent of the order in which the parameters are specified.
std::string IFMapServerTable::LinkAttrKey(IFMapNode *first, IFMapNode *second) {
    ostringstream oss;
    oss << "attr(";
    if (first->IsLess(*second)) {
        oss << first->name() << "," << second->name();
    } else {
        oss << second->name() << "," << first->name();
    }
    oss << ")";
    return oss.str();
}

void IFMapServerTable::DeleteNode(IFMapNode *node) {
    IFMAP_DEBUG(IFMapNodeOperation, "Deleting", node->ToString());
    DBTablePartition *partition =
        static_cast<DBTablePartition *>(GetTablePartition(0));
    graph_->RemoveNode(node);
    partition->Delete(node);
}

void IFMapServerTable::Notify(IFMapNode *node) {
    DBTablePartition *partition =
        static_cast<DBTablePartition *>(GetTablePartition(0));
    partition->Change(node);
}

bool IFMapServerTable::DeleteIfEmpty(IFMapNode *node) {
    if ((node->GetObject() == NULL) && !node->HasAdjacencies(graph_)) {
        DeleteNode(node);
        return true;
    }
    return false;
}

IFMapObject *IFMapServerTable::LocateObject(IFMapNode *node,
                                            IFMapOrigin origin) {
    IFMapObject *object = node->Find(origin);
    if (object == NULL) {
        object = AllocObject();
        object->set_origin(origin);
        node->Insert(object);
    }
    return object;
}

IFMapIdentifier *IFMapServerTable::LocateIdentifier(IFMapNode *node,
                                                    IFMapOrigin origin,
                                                    uint64_t sequence_number) {
    IFMapObject *object = LocateObject(node, origin);
    assert(object);

    // If the sequence number has changed, we are processing updates in a new
    // connection to the ifmap server. Save the current properties and check
    // later with the updated properties to find any stale ones.
    if (object->sequence_number() != sequence_number) {
        IFMapIdentifier *identifier = static_cast<IFMapIdentifier *>(object);
        identifier->TransferPropertyToOldProperty();
        object->set_sequence_number(sequence_number);
    }
    return static_cast<IFMapIdentifier *>(object);
}

IFMapLinkAttr *IFMapServerTable::LocateLinkAttr(IFMapNode *node,
                                                IFMapOrigin origin,
                                                uint64_t sequence_number) {
    IFMapObject *object = LocateObject(node, origin);
    assert(object);
    object->set_sequence_number(sequence_number);

    return static_cast<IFMapLinkAttr *>(object);
}

void IFMapServerTable::Input(DBTablePartition *partition, DBClient *client,
                             DBRequest *request) {
    assert(request->oper == DBRequest::DB_ENTRY_ADD_CHANGE ||
           request->oper == DBRequest::DB_ENTRY_DELETE);
    RequestKey *key = static_cast<RequestKey *>(request->key.get());
    RequestData *data = static_cast<RequestData *>(request->data.get());
    assert(data != NULL);

    IFMapServerTable *rtable = NULL;
    IFMapServerTable *mtable = NULL;

    // Sanity checks before allocation resources.
    if (!data->id_name.empty()) {
        rtable = TableFind(database(), data->id_type);
        if (!rtable) {
            IFMAP_TRACE(IFMapTblNotFoundTrace, "Cant find table",
                        data->id_type);
            return;
        }
        mtable = TableFind(database(), data->metadata);
        if (mtable == NULL && request->oper == DBRequest::DB_ENTRY_ADD_CHANGE &&
            data->content.get() != NULL) {
            IFMAP_TRACE(IFMapTblNotFoundTrace, "Cant find table",
                        data->metadata);
            return;
        }
    }

    IFMapNode *first = NULL;
    bool lchanged = false;
    if (request->oper == DBRequest::DB_ENTRY_DELETE) {
        first = EntryLookup(key);
        if (first == NULL) {
            IFMAP_WARN(IFMapIdentifierNotFound, "Cant find identifier",
                       key->id_name);
            return;
        }
    } else {
        first = EntryLocate(key, &lchanged);
    }

    if (data->id_name.empty()) {
        // property
        first->set_last_change_at_to_now();
        if (request->oper == DBRequest::DB_ENTRY_ADD_CHANGE) {
            IFMapIdentifier *identifier = LocateIdentifier(first, data->origin,
                                                           key->id_seq_num);
            identifier->SetProperty(data->metadata, data->content.get());
            partition->Change(first);
        } else {
            IFMapIdentifier *identifier = static_cast<IFMapIdentifier *>(
                    first->Find(data->origin));
            if (identifier == NULL) {
                return;
            }
            identifier->ClearProperty(data->metadata);
            // Figure out whether to delete the identifier.
            if (identifier->empty()) {
                first->Remove(identifier);
            }
            if (DeleteIfEmpty(first) == false) {
                partition->Change(first);
            }
        }
        return;
    }

    IFMapNode *second = NULL;
    bool rchanged = false;
    if (request->oper == DBRequest::DB_ENTRY_DELETE) {
        second = TableEntryLookup(rtable, data->id_name);
        if (second == NULL) {
            IFMAP_WARN(IFMapIdentifierNotFound, "Cant find identifier",
                       data->id_name);
            return;
        }
    } else {
        second = TableEntryLocate(rtable, data->id_name, &rchanged);
    }

    IFMapNode *midnode = NULL;
    bool mchanged = false;

    if (mtable != NULL) {
        // link with attribute
        string id_mid = LinkAttrKey(first, second);
        if (request->oper == DBRequest::DB_ENTRY_DELETE) {
            midnode = TableEntryLookup(mtable, id_mid);
            if (midnode == NULL) {
                IFMAP_WARN(IFMapIdentifierNotFound, "Cant find identifier",
                           id_mid);
                return;
            }
        } else {
            midnode = TableEntryLocate(mtable, id_mid, &mchanged);
        }
        midnode->set_last_change_at_to_now();
        if (request->oper == DBRequest::DB_ENTRY_ADD_CHANGE) {
            IFMapLink *glink = 
                static_cast<IFMapLink *>(graph_->GetEdge(first, midnode));
            if (glink == NULL) {
                DBGraph::Edge edge = graph_->Link(first, midnode);
                LinkNodeAdd(edge, first, midnode, data->metadata,
                            key->id_seq_num, data->origin);
            } else {
                LinkNodeUpdate(glink, key->id_seq_num, data->origin);
            }
            glink = static_cast<IFMapLink *>(graph_->GetEdge(midnode, second));
            if (glink == NULL) {
                DBGraph::Edge edge = graph_->Link(midnode, second);
                LinkNodeAdd(edge, midnode, second, data->metadata,
                            key->id_seq_num, data->origin);
            } else {
                LinkNodeUpdate(glink, key->id_seq_num, data->origin);
            }
            IFMapLinkAttr *link_attr = mtable->LocateLinkAttr(midnode,
                                                              data->origin,
                                                              key->id_seq_num);
            mchanged |= link_attr->SetData(data->content.get());
        } else {
            IFMapObject *object = midnode->Find(data->origin);
            if (object == NULL) {
                return;
            }
            midnode->Remove(object);
            if (midnode->GetObject() != NULL) {
                return;
            }
            IFMapOrigin origin(IFMapOrigin::MAP_SERVER);
            LinkNodeDelete(first, midnode, origin);
            LinkNodeDelete(midnode, second, origin);
            DeleteIfEmpty(first);
            rtable->DeleteIfEmpty(second);
            mtable->DeleteIfEmpty(midnode);
        }
    } else {
        // link
        if (request->oper == DBRequest::DB_ENTRY_ADD_CHANGE) {
            // Link is added if not present
            IFMapLink *glink = 
                static_cast<IFMapLink *>(graph_->GetEdge(first, second));
            if (glink == NULL) {
                DBGraph::Edge edge = graph_->Link(first, second);
                LinkNodeAdd(edge, first, second, data->metadata,
                            key->id_seq_num, data->origin);
            } else {
                LinkNodeUpdate(glink, key->id_seq_num, data->origin);
            }
        } else {
            // TODO: check if the edge is present and ignore otherwise.
            if (graph_->GetEdge(first, second) != NULL) {
                IFMapOrigin origin(IFMapOrigin::MAP_SERVER);
                LinkNodeDelete(first, second, origin);
                // check whether any of the identifiers can be deleted.
                DeleteIfEmpty(first);
                rtable->DeleteIfEmpty(second);
            }
        }
    }

    if (lchanged) {
        partition->Change(first);
    }
    if (rchanged) {
        rtable->Notify(second);
    }
    if (mchanged) {
        mtable->Notify(midnode);
    }
}

void IFMapServerTable::Clear() {
    DBTablePartition *partition = static_cast<DBTablePartition *>(
        GetTablePartition(0));
    assert(!HasListeners());
    for (IFMapNode *node = static_cast<IFMapNode *>(partition->GetFirst()),
                 *next = NULL;
         node != NULL; node = next) {
        next = static_cast<IFMapNode *>(partition->GetNext(node));
        if (node->IsDeleted()) {
            continue;
        }
        graph_->RemoveNode(node);
        partition->Delete(node);
    }
}

// This is called in the context of the virtual_router table i.e. 'this' points
// to __ifmap__.virtual_router.0
void IFMapServerTable::IFMapVmSubscribe(const std::string &vr_name,
                                        const std::string &vm_name,
                                        bool subscribe, bool has_vms) {
    if (subscribe) {
        IFMapProcVmSubscribe(vr_name, vm_name);
    } else {
        IFMapProcVmUnsubscribe(vr_name, vm_name, has_vms);
    }
}

void IFMapServerTable::IFMapAddVrVmLink(IFMapNode *vr_node,
                                        IFMapNode *vm_node) {
    // Add the link if it does not exist. If it does, add XMPP as origin
    uint64_t sequence_number = 0;
    IFMapOrigin origin(IFMapOrigin::XMPP);

    IFMapLink *glink = 
        static_cast<IFMapLink *>(graph_->GetEdge(vr_node, vm_node));
    if (glink == NULL) {
        DBGraph::Edge edge = graph_->Link(vr_node, vm_node);
        std::string metadata = std::string("virtual-router-virtual-machine");
        LinkNodeAdd(edge, vr_node, vm_node, metadata, sequence_number, origin);
    } else {
        glink->AddOriginInfo(origin, sequence_number);
    }
}

// Process the vm-subscribe only after a config-add of the vm
void IFMapServerTable::IFMapProcVmSubscribe(const std::string &vr_name,
                                            const std::string &vm_name) {
    bool changed = false;

    // Lookup the node corresponding to vr_name
    RequestKey request;
    request.id_name = vr_name;
    IFMapNode *vr_node = EntryLookup(&request);
    if (vr_node == NULL) {
        vr_node = EntryLocate(&request, &changed);
    }
    LocateIdentifier(vr_node, IFMapOrigin(IFMapOrigin::XMPP), 0);

    // Lookup the node corresponding to vm_name
    IFMapServerTable *vm_table = static_cast<IFMapServerTable *>(
            database()->FindTable("__ifmap__.virtual_machine.0"));
    assert(vm_table != NULL);
    request.id_name = vm_name;
    IFMapNode *vm_node = vm_table->EntryLookup(&request);
    assert(vm_node != NULL);
    vm_table->LocateIdentifier(vm_node, IFMapOrigin(IFMapOrigin::XMPP), 0);

    IFMapAddVrVmLink(vr_node, vm_node);
}

void IFMapServerTable::IFMapRemoveVrVmLink(IFMapNode *vr_node,
                                           IFMapNode *vm_node) {
    // Remove XMPP as origin. If there are no more origin's, delete the link.
    IFMapOrigin origin(IFMapOrigin::XMPP);
    LinkNodeDelete(vr_node, vm_node, origin);
}

void IFMapServerTable::IFMapProcVmUnsubscribe(const std::string &vr_name,
                                              const std::string &vm_name,
                                              bool has_vms) {
    // Lookup the node corresponding to vr_name
    RequestKey request;
    request.id_name = vr_name;
    IFMapNode *vr_node = EntryLookup(&request);
    assert(vr_node != NULL);

    // Lookup the node corresponding to vm_name
    IFMapServerTable *vm_table = static_cast<IFMapServerTable *>(
            database()->FindTable("__ifmap__.virtual_machine.0"));
    assert(vm_table != NULL);
    request.id_name = vm_name;
    IFMapNode *vm_node = vm_table->EntryLookup(&request);
    assert(vm_node != NULL);

    IFMapRemoveVrVmLink(vr_node, vm_node);

    IFMapOrigin origin(IFMapOrigin::XMPP);
    RemoveObjectAndDeleteNode(vm_node, origin);

    // Remove XMPP as origin from the VR only if all the VMs are gone
    if (!has_vms) {
        RemoveObjectAndDeleteNode(vr_node, origin);
    }
}

void IFMapServerTable::RemoveObjectAndDeleteNode(IFMapNode *node,
                                                 const IFMapOrigin &origin) {
    IFMapServerTable *table = static_cast<IFMapServerTable *>(node->table());
    assert(table);
    IFMapObject *object = node->Find(origin);
    if (object) {
        node->Remove(object);
    }
    table->DeleteIfEmpty(node);
}

