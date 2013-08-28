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
    IFMapLink *link = new IFMapLink(edge);
    link->SetProperties(left, right, metadata, sequence_number, origin);
    graph_->SetEdgeProperty(link);

    DBTablePartition *partition =
        static_cast<DBTablePartition *>(GetTablePartition(0));
    partition->Add(link);
}

void IFMapLinkTable::DeleteLink(DBGraphEdge *edge) {
    IFMapLink *link = static_cast<IFMapLink *>(edge);
    link->ClearNodes();
    DBTablePartition *partition =
        static_cast<DBTablePartition *>(GetTablePartition(0));
    partition->Delete(edge);    
}

void IFMapLinkTable::DeleteLink(DBGraphEdge *edge, IFMapNode *lhs,
                                IFMapNode *rhs) {
    IFMapLink *link = static_cast<IFMapLink *>(edge);
    link->ClearNodes();
    DBTablePartition *partition =
        static_cast<DBTablePartition *>(GetTablePartition(0));
    partition->Delete(edge);
    graph_->Unlink(lhs, rhs);
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
