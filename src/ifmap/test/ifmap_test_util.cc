/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "ifmap/test/ifmap_test_util.h"

#include "db/db.h"
#include "db/db_graph.h"
#include "db/db_graph_edge.h"
#include "db/db_table.h"
#include "db/db_table_partition.h"
#include "ifmap/ifmap_link.h"
#include "ifmap/ifmap_node.h"
#include "ifmap/ifmap_table.h"

using namespace std;

namespace ifmap_test_util {

void IFMapMsgNodeAdd(DB *db, const string &type, const string &id,
                     uint64_t sequence_number) {
    auto_ptr<DBRequest> request(new DBRequest());
    request->oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    IFMapTable *tbl = IFMapTable::FindTable(db, type);
    IFMapNodeCommon(tbl, request.get(), type, id, sequence_number);
    tbl->Enqueue(request.get());
}

void IFMapMsgNodeDelete(DB *db, const string &type, const string &id) {
    auto_ptr<DBRequest> request(new DBRequest());
    request->oper = DBRequest::DB_ENTRY_DELETE;
    IFMapTable *tbl = IFMapTable::FindTable(db, type);
    IFMapNodeCommon(tbl, request.get(), type, id, 0);
    tbl->Enqueue(request.get());
}

IFMapNode *IFMapNodeLookup(DB *db, const string &type, const string &name) {
    IFMapTable *table = IFMapTable::FindTable(db, type);
    if (table == NULL) {
        return NULL;
    }
    return table->FindNode(name);
}

void IFMapNodeNotify(DB *db, const string &type, const string &name) {
    IFMapNode *node = IFMapNodeLookup(db, type, name);
    if (!node)
        return;
    IFMapTable *table = node->table();
    table->GetTablePartition(0)->Notify(node);
}

IFMapLink *IFMapLinkLookup(DB *db, DBGraph *graph,
                           const string &ltype, const string &lid,
                           const string &rtype, const string &rid) {
    IFMapNode *lnode = IFMapNodeLookup(db, ltype, lid);
    IFMapNode *rnode = IFMapNodeLookup(db, rtype, rid);
    if (lnode == NULL || rnode == NULL) {
        return NULL;
    }
    return static_cast<IFMapLink *>(graph->GetEdge(lnode, rnode));
}

void IFMapLinkNotify(DB *db, DBGraph *graph,
                     const string &ltype, const string &lid,
                     const string &rtype, const string &rid) {
    IFMapLink *link = IFMapLinkLookup(db, graph, ltype, lid, rtype, rid);
    DBTable *table =
        static_cast<DBTable *>(db->FindTable("__ifmap_metadata__.0"));
    table->GetTablePartition(0)->Notify(link);
}

}  // namespace ifmap_test_util
