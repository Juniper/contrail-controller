#include "ifmap/test/ifmap_test_util.h"

#include <map>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/random_generator.hpp>

#include "db/db.h"
#include "ifmap/ifmap_agent_table.h"
#include "schema/vnc_cfg_types.h"

namespace ifmap_test_util {

using std::string;

void UuidTypeSet(const boost::uuids::uuid &uuid, autogen::UuidType *idpair) {
    idpair->uuid_lslong = 0;
    idpair->uuid_mslong = 0;
    for (int i = 0; i < 8; i++) {
        uint64_t value = uuid.data[16 - (i + 1)];
        idpair->uuid_lslong |= value << (8 * i);
    }
    for (int i = 0; i < 8; i++) {
        uint64_t value = uuid.data[8 - (i + 1)];
        idpair->uuid_mslong |= value << (8 * i);
    }
}

void IFMapLinkCommon(DBRequest *request,
                     const string &lhs, const string &lid,
                     const string &rhs, const string &rid,
                     const string &metadata, uint64_t sequence_number) {

    IFMapAgentLinkTable::RequestKey *key =
            new IFMapAgentLinkTable::RequestKey();
    request->key.reset(key);
    key->left_key.id_name = lid;
    key->left_key.id_type = lhs;
    key->left_key.id_seq_num = sequence_number;

    key->right_key.id_name = rid;
    key->right_key.id_type = rhs;
    key->right_key.id_seq_num = sequence_number;
    key->metadata = metadata;
}

void IFMapMsgLink(DB *db, const string &ltype, const string &lid,
                  const string &rtype, const string &rid,
                  const string &metadata, uint64_t sequence_number) {
    DBRequest request;
    request.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    IFMapLinkCommon(&request, ltype, lid, rtype, rid, metadata,
                    sequence_number);
    DBTable *link_table = static_cast<DBTable *>(
        db->FindTable(IFMAP_AGENT_LINK_DB_NAME));
    assert(link_table != NULL);
    link_table->Enqueue(&request);
}

void IFMapMsgUnlink(DB *db, const string &lhs, const string &lid,
                    const string &rhs, const string &rid,
                    const string &metadata) {
    DBRequest request;
    request.oper = DBRequest::DB_ENTRY_DELETE;
    IFMapLinkCommon(&request, lhs, lid, rhs, rid, metadata, 0);

    DBTable *link_table = static_cast<DBTable *>(
        db->FindTable(IFMAP_AGENT_LINK_DB_NAME));
    assert(link_table != NULL);
    link_table->Enqueue(&request);
}

void IFMapNodeCommon(IFMapTable *table, DBRequest *request, const string &type,
                     const string &id, uint64_t sequence_number,
                     const string &metadata, AutogenProperty *in_content) {
    IFMapTable::RequestKey *key = new IFMapTable::RequestKey();
    request->key.reset(key);
    key->id_type = type;
    key->id_name = id;
    key->id_seq_num = sequence_number;

    if (request->oper == DBRequest::DB_ENTRY_ADD_CHANGE) {
        IFMapAgentTable::IFMapAgentData *data =
                new IFMapAgentTable::IFMapAgentData();
        request->data.reset(data);
        data->content.reset(table->AllocObject());

        IFMapIdentifier *mapid = static_cast<IFMapIdentifier *>(
            data->content.get());
        autogen::IdPermsType id_perms;
        id_perms.Clear();
        boost::uuids::random_generator gen;
        UuidTypeSet(gen(), &id_perms.uuid);
        mapid->SetProperty("id-perms", &id_perms);
    }
}

void IFMapMsgPropertySet(DB *db,
                         const std::string &type,
                         const std::string &id,
                         const std::map<std::string, AutogenProperty *> &pmap,
                         uint64_t sequence_number) {
    DBRequest request;
    request.oper = DBRequest::DB_ENTRY_ADD_CHANGE;

    IFMapTable::RequestKey *key = new IFMapTable::RequestKey();
    request.key.reset(key);
    key->id_type = type;
    key->id_name = id;
    key->id_seq_num = sequence_number;

    IFMapTable *table = IFMapTable::FindTable(db, type);

    IFMapAgentTable::IFMapAgentData *data =
            new IFMapAgentTable::IFMapAgentData();
    request.data.reset(data);
    data->content.reset(table->AllocObject());

    IFMapIdentifier *mapid = static_cast<IFMapIdentifier *>(
        data->content.get());
    autogen::IdPermsType id_perms;
    id_perms.Clear();
    boost::uuids::string_generator gen;
    UuidTypeSet(gen(id), &id_perms.uuid);
    mapid->SetProperty("id-perms", &id_perms);

    for (std::map<std::string, AutogenProperty *>::const_iterator iter =
                 pmap.begin(); iter != pmap.end(); ++iter) {
        mapid->SetProperty(iter->first, iter->second);
    }

    table->Enqueue(&request);
}

}  // namespace ifmap_test_util
