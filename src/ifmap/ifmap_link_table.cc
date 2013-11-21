/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "ifmap/ifmap_link_table.h"

#include <boost/bind.hpp>

#include "db/db.h"
#include "db/db_graph.h"
#include "db/db_table_partition.h"
#include "ifmap/ifmap_link.h"

using namespace std;

IFMapLinkTable::IFMapLinkTable(DB *db, const string &name, DBGraph *graph)
        : DBTable(db, name), graph_(graph) {
}

void IFMapLinkTable::Input(DBTablePartition *partition, DBClient *client,
                           DBRequest *req) {
    assert(false);
}

std::auto_ptr<DBEntry> IFMapLinkTable::AllocEntry(
        const DBRequestKey *key) const {
    const RequestKey *rkey = static_cast<const RequestKey *>(key);
    auto_ptr<DBEntry> entry(new IFMapLink(rkey->edge));
    return entry;
}

void IFMapLinkTable::AddLink(DBGraphBase::edge_descriptor edge,
                             IFMapNode *left, IFMapNode *right,
                             const string &metadata, uint64_t sequence_number,
                             const IFMapOrigin &origin) {
    DBTablePartition *partition =
        static_cast<DBTablePartition *>(GetTablePartition(0));

    IFMapLink *link = FindLink(edge);
    if (link) {
        assert(link->IsDeleted());
        link->ClearDelete();
        link->set_last_change_at_to_now();
        partition->Change(link);
    } else {
        link = new IFMapLink(edge);
        partition->Add(link);
    }
    link->SetProperties(left, right, metadata, sequence_number, origin);
    graph_->SetEdgeProperty(link);
}

IFMapLink *IFMapLinkTable::FindLink(DBGraphBase::edge_descriptor edge) {

    DBTablePartition *partition =
        static_cast<DBTablePartition *>(GetTablePartition(0));
    RequestKey key;
    key.edge = edge;
    return static_cast<IFMapLink *>(partition->Find(&key));
}

void IFMapLinkTable::DeleteLink(DBGraphEdge *edge) {
    IFMapLink *link = static_cast<IFMapLink *>(edge);
    link->set_last_change_at_to_now();
    link->ClearNodes();
    DBTablePartition *partition =
        static_cast<DBTablePartition *>(GetTablePartition(0));
    partition->Delete(edge);    
}

void IFMapLinkTable::DeleteLink(DBGraphEdge *edge, IFMapNode *lhs,
                                IFMapNode *rhs) {
    DeleteLink(edge);
    graph_->Unlink(lhs, rhs);
}

void IFMapLinkTable::DeleteLink(IFMapNode *lhs, IFMapNode *rhs,
                                const IFMapOrigin &origin) {
    DBGraphEdge *edge = graph_->GetEdge(lhs, rhs);
    IFMapLink *link = static_cast<IFMapLink *>(edge);
    link->RemoveOriginInfo(origin.origin);
    if (link->is_origin_empty()) {
        DeleteLink(edge);
        graph_->Unlink(lhs, rhs);
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
        graph_->Unlink(link->source(graph_), link->target(graph_));
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
