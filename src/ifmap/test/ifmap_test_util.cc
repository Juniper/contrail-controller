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
#include "ifmap/ifmap_server_table.h"

using namespace std;

namespace ifmap_test_util {

void IFMapLinkCommon(DBRequest *request,
                     const string &lhs, const string &lid,
                     const string &rhs, const string &rid,
                     const string &metadata, uint64_t sequence_number) {
    IFMapTable::RequestKey *key = new IFMapTable::RequestKey();
    request->key.reset(key);
    key->id_type = lhs;
    key->id_name = lid;
    key->id_seq_num = sequence_number;
    IFMapServerTable::RequestData *data = new IFMapServerTable::RequestData();
    request->data.reset(data);
    data->metadata = metadata;
    data->id_type = rhs;
    data->id_name = rid;
    data->origin.set_origin(IFMapOrigin::MAP_SERVER);
}

void IFMapMsgLink(DB *db, const string &ltype, const string &lid,
                  const string &rtype, const string &rid,
                  const string &metadata, uint64_t sequence_number) {
    auto_ptr<DBRequest> request(new DBRequest());
    request->oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    IFMapLinkCommon(request.get(), ltype, lid, rtype, rid, metadata,
                    sequence_number);
    IFMapTable *tbl = IFMapTable::FindTable(db, ltype);
    tbl->Enqueue(request.get());
}

void IFMapMsgUnlink(DB *db, const string &lhs, const string &lid,
                    const string &rhs, const string &rid,
                    const string &metadata) {
    auto_ptr<DBRequest> request(new DBRequest());
    request->oper = DBRequest::DB_ENTRY_DELETE;
    IFMapLinkCommon(request.get(), lhs, lid, rhs, rid, metadata, 0);
    IFMapTable *tbl = IFMapTable::FindTable(db, lhs);
    tbl->Enqueue(request.get());
}

void IFMapNodeCommon(DBRequest *request, const string &type,
                     const string &id, uint64_t sequence_number) {
    IFMapTable::RequestKey *key = new IFMapTable::RequestKey();
    request->key.reset(key);
    key->id_type = type;
    key->id_name = id;
    key->id_seq_num = sequence_number;

    IFMapServerTable::RequestData *data = new IFMapServerTable::RequestData();
    request->data.reset(data);
    data->origin.set_origin(IFMapOrigin::MAP_SERVER);
}

void IFMapMsgNodeAdd(DB *db, const string &type, const string &id,
                     uint64_t sequence_number) {
    auto_ptr<DBRequest> request(new DBRequest());
    request->oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    IFMapNodeCommon(request.get(), type, id, sequence_number);
    IFMapTable *tbl = IFMapTable::FindTable(db, type);
    tbl->Enqueue(request.get());
}

void IFMapMsgNodeDelete(DB *db, const string &type, const string &id) {
    auto_ptr<DBRequest> request(new DBRequest());
    request->oper = DBRequest::DB_ENTRY_DELETE;
    IFMapNodeCommon(request.get(), type, id, 0);
    IFMapTable *tbl = IFMapTable::FindTable(db, type);
    tbl->Enqueue(request.get());
}

void IFMapPropertyCommon(DBRequest *request, const string &type,
                         const string &id, const string &metadata,
                         AutogenProperty *content, uint64_t sequence_number) {
    IFMapTable::RequestKey *key = new IFMapTable::RequestKey();
    request->key.reset(key);
    key->id_type = type;
    key->id_name = id;
    key->id_seq_num = sequence_number;

    IFMapServerTable::RequestData *data = new IFMapServerTable::RequestData();
    request->data.reset(data);
    data->metadata = metadata;
    data->origin.set_origin(IFMapOrigin::MAP_SERVER);
    if (content != NULL) {
        data->content.reset(content);
    }
}

void IFMapMsgPropertyAdd(DB *db, const string &type, const string &id,
                         const string &metadata, AutogenProperty *content,
                         uint64_t sequence_number) {
    auto_ptr<DBRequest> request(new DBRequest());
    request->oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    IFMapPropertyCommon(request.get(), type, id, metadata, content,
                        sequence_number);
    IFMapTable *tbl = IFMapTable::FindTable(db, type);
    tbl->Enqueue(request.get());
}

void IFMapMsgPropertyDelete(DB *db, const string &type, const string &id,
                            const string &metadata) {
    auto_ptr<DBRequest> request(new DBRequest());
    request->oper = DBRequest::DB_ENTRY_DELETE;
    IFMapPropertyCommon(request.get(), type, id, metadata, NULL, 0);
    IFMapTable *tbl = IFMapTable::FindTable(db, type);
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
