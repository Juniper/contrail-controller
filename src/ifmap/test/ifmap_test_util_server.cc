#include "ifmap/test/ifmap_test_util.h"

#include "db/db.h"
#include "ifmap/ifmap_server_table.h"

namespace ifmap_test_util {

using std::string;

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
    DBRequest request;
    request.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    IFMapLinkCommon(&request, ltype, lid, rtype, rid, metadata,
                    sequence_number);
    IFMapTable *tbl = IFMapTable::FindTable(db, ltype);
    tbl->Enqueue(&request);
}

void IFMapMsgUnlink(DB *db, const string &lhs, const string &lid,
                    const string &rhs, const string &rid,
                    const string &metadata) {
    DBRequest request;
    request.oper = DBRequest::DB_ENTRY_DELETE;
    IFMapLinkCommon(&request, lhs, lid, rhs, rid, metadata, 0);
    IFMapTable *tbl = IFMapTable::FindTable(db, lhs);
    tbl->Enqueue(&request);
}

void IFMapNodeCommon(IFMapTable *table, DBRequest *request, const string &type,
                     const string &id, uint64_t sequence_number, 
                     const string &metadata, AutogenProperty *content) {
    IFMapTable::RequestKey *key = new IFMapTable::RequestKey();
    request->key.reset(key);
    key->id_type = type;
    key->id_name = id;
    key->id_seq_num = sequence_number;

    IFMapServerTable::RequestData *data = new IFMapServerTable::RequestData();
    request->data.reset(data);
    data->origin.set_origin(IFMapOrigin::MAP_SERVER);
    if (content != NULL) {
        data->metadata = metadata;
        data->content.reset(content);
    }
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
    DBRequest request;
    request.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    IFMapPropertyCommon(&request, type, id, metadata, content,
                        sequence_number);
    IFMapTable *tbl = IFMapTable::FindTable(db, type);
    tbl->Enqueue(&request);
}

void IFMapMsgPropertyDelete(DB *db, const string &type, const string &id,
                            const string &metadata) {
    DBRequest request;
    request.oper = DBRequest::DB_ENTRY_DELETE;
    IFMapPropertyCommon(&request, type, id, metadata, NULL, 0);
    IFMapTable *tbl = IFMapTable::FindTable(db, type);
    tbl->Enqueue(&request);
}

}
