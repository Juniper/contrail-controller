/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "ifmap/ifmap_link_table.h"

#include <boost/bind.hpp>

#include "db/db.h"
#include "db/db_graph.h"
#include "db/db_table_partition.h"
#include "ifmap/ifmap_link.h"
#include "ifmap/ifmap_log.h"
#include "ifmap/ifmap_log_types.h"

using namespace std;

IFMapLinkTable::IFMapLinkTable(DB *db, const string &name, DBGraph *graph)
        : DBGraphTable(db, name, graph) {
}

void IFMapLinkTable::Input(DBTablePartition *partition, DBClient *client,
                           DBRequest *req) {
    assert(false);
}

std::auto_ptr<DBEntry> IFMapLinkTable::AllocEntry(
        const DBRequestKey *key) const {
    const RequestKey *rkey = static_cast<const RequestKey *>(key);
    auto_ptr<DBEntry> entry(new IFMapLink(rkey->name));
    return entry;
}

// Generate an unique name for the link node and it should
// be independent of the order in which the right and left nodes are specified
std::string IFMapLinkTable::LinkKey(const string &metadata,
                                        IFMapNode *left, IFMapNode *right) {
    ostringstream oss;
    if (left->ToString() < right->ToString()) {
        oss << metadata << "," << left->ToString() << "," << right->ToString();
    } else {
        oss << metadata << "," << right->ToString() << "," << left->ToString();
    }
    return oss.str();
}

IFMapLink *IFMapLinkTable::AddLink(IFMapNode *left, IFMapNode *right,
                             const string &metadata, uint64_t sequence_number,
                             const IFMapOrigin &origin) {
    DBTablePartition *partition =
        static_cast<DBTablePartition *>(GetTablePartition(0));

    string link_name = LinkKey(metadata, left, right);
    IFMapLink *link = FindLink(link_name);
    if (link) {
        assert(link->IsDeleted());
        link->ClearDelete();
        link->set_last_change_at_to_now();
        partition->Change(link);
    } else {
        link = new IFMapLink(link_name);
        partition->Add(link);
    }
    link->SetProperties(left, right, metadata, sequence_number, origin);
    assert(dynamic_cast<IFMapNode *>(left));
    assert(dynamic_cast<IFMapNode *>(right));
    return link;
}

IFMapLink *IFMapLinkTable::FindLink(const string &metadata, IFMapNode *left, IFMapNode *right) {
    string link_name = LinkKey(metadata, left, right);
    return FindLink(link_name);
}

IFMapLink *IFMapLinkTable::FindLink(const string &name) {

    DBTablePartition *partition =
        static_cast<DBTablePartition *>(GetTablePartition(0));
    RequestKey key;
    key.name = name;
    return static_cast<IFMapLink *>(partition->Find(&key));
}

IFMapLink *IFMapLinkTable::FindNextLink(const string &name) {

    DBTablePartition *partition =
        static_cast<DBTablePartition *>(GetTablePartition(0));
    RequestKey key;
    key.name = name;
    return static_cast<IFMapLink *>(partition->FindNext(&key));
}

void IFMapLinkTable::DeleteLink(IFMapLink *link) {
    DBGraphEdge *edge = static_cast<DBGraphEdge *>(link);
    graph()->Unlink(edge);
    link->set_last_change_at_to_now();
    link->ClearNodes();
    DBTablePartition *partition =
        static_cast<DBTablePartition *>(GetTablePartition(0));
    partition->Delete(edge);
}

void IFMapLinkTable::DeleteLink(IFMapLink *link, const IFMapOrigin &origin) {
    link->RemoveOriginInfo(origin.origin);
    if (link->is_origin_empty()) {
        DeleteLink(link);
    }
}

DBTable *IFMapLinkTable::CreateTable(DB *db, const string &name,
                                     DBGraph *graph) {
    IFMapLinkTable *table = new IFMapLinkTable(db, name, graph);
    table->Init();
    return table;
}

void IFMapLinkTable::Clear() {
    DBTablePartition *partition = static_cast<DBTablePartition *>(
        GetTablePartition(0));

    assert(!HasListeners());
    for (IFMapLink *link = static_cast<IFMapLink *>(partition->GetFirst()),
                 *next = NULL; link != NULL; link = next) {
        next = static_cast<IFMapLink *>(partition->GetNext(link));
        if (link->IsDeleted()) {
            continue;
        }
        graph()->Unlink(link);
        partition->Delete(link);
    }
}

void IFMapLinkTable_Init(DB *db, DBGraph *graph) {
    DBTable *table =
            IFMapLinkTable::CreateTable(db, "__ifmap_metadata__.0", graph);
    db->AddTable(table);
}

void IFMapLinkTable_Clear(DB *db) {
    IFMapLinkTable *ltable = static_cast<IFMapLinkTable *>(
            db->FindTable("__ifmap_metadata__.0"));
    ltable->Clear();
}
