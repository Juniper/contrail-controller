/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */
#include "config_json_parser.h"

#include "ifmap/ifmap_server_table.h"
#include "ifmap/ifmap_log.h"
#include "ifmap/ifmap_log_types.h"

using namespace rapidjson;
using namespace std;

ConfigJsonParser::ConfigJsonParser(DB *db) : db_(db) {

}

void ConfigJsonParser::MetadataRegister(const string &metadata,
                                        MetadataParseFn parser) {
    pair<MetadataParseMap::iterator, bool> result =
            metadata_map_.insert(make_pair(metadata, parser));
    assert(result.second);
}

void ConfigJsonParser::MetadataClear(const string &module) {
    metadata_map_.clear();
}

bool ConfigJsonParser::ParseNameType(const Document &document,
                                     IFMapTable::RequestKey *key) const {
    // Type is the name of the document.
    Value::ConstMemberIterator itr = document.MemberBegin();
    assert(itr->name.IsString());
    key->id_type = itr->name.GetString();

    // Name is the fq_name field in the document.
    const Value &value_node = itr->value;
    assert(value_node.HasMember("fq_name"));
    const Value &fq_name_node = value_node["fq_name"];
    assert(fq_name_node.IsArray());
    assert(fq_name_node.Size() != 0);
    size_t i = 0;

    // Iterate over all items except the last one.
    for (; i < fq_name_node.Size() - 1; ++i) {
        key->id_name += fq_name_node[i].GetString();
        key->id_name += string(":");
    }
    key->id_name += fq_name_node[i].GetString();

    cout << "fq-name is " << key->id_name << endl;
    cout << "type is " << key->id_type << endl;

    return true;
}

IFMapTable::RequestKey *ConfigJsonParser::CloneKey(
        const IFMapTable::RequestKey &src) const {
    IFMapTable::RequestKey *retkey = new IFMapTable::RequestKey();
    retkey->id_type = src.id_type;
    retkey->id_name = src.id_name;
    // TODO
    //retkey->id_seq_num = what?
    return retkey;
}

bool ConfigJsonParser::ParseProperties(const Document &document,
        const IFMapTable::RequestKey &key, IFMapOrigin::Origin origin,
        RequestList *req_list) const {
    Value::ConstMemberIterator doc_itr = document.MemberBegin();
    const Value &value_node = doc_itr->value;
    for (Value::ConstMemberIterator itr = value_node.MemberBegin();
         itr != value_node.MemberEnd(); ++itr) {
        MetadataParseMap::const_iterator loc =
            metadata_map_.find(itr->name.GetString());
        if (loc == metadata_map_.end()) {
            continue;
        }
        cout << "Property: " << itr->name.GetString() << endl;
        auto_ptr<AutogenProperty> pvalue;
        bool success = (loc->second)(itr->value, &pvalue);
        if (!success) {
            cout << "loc->second call failure\n";
            return false;
        }
        IFMapServerTable::RequestData *data =
            new IFMapServerTable::RequestData(origin);
        data->metadata = itr->name.GetString();
        data->content.reset(pvalue.release());

        DBRequest *db_request = new DBRequest();
        db_request->oper = DBRequest::DB_ENTRY_ADD_CHANGE;
        db_request->key.reset(CloneKey(key));
        db_request->data.reset(data);

        req_list->push_back(db_request);
    }

    return true;
}

void ConfigJsonParser::EnqueueListToTables(RequestList *req_list) const {
    while (!req_list->empty()) {
        auto_ptr<DBRequest> req(req_list->front());
        req_list->pop_front();

        IFMapTable::RequestKey *key =
            static_cast<IFMapTable::RequestKey *>(req->key.get());

        IFMapTable *table = IFMapTable::FindTable(db_, key->id_type);
        if (table != NULL) {
            table->Enqueue(req.get());
        } else {
            cout << "Cant find table\n";
        }
    }
}

bool ConfigJsonParser::ParseDocument(const Document &document,
        IFMapOrigin::Origin origin, RequestList *req_list) const {
    auto_ptr<IFMapTable::RequestKey> key(new IFMapTable::RequestKey());

    // Update the name and the type into 'key'.
    if (!ParseNameType(document, key.get())) {
        return false;
    }

    // For each property, we will clone 'key' to create our DBRequest's i.e.
    // 'key' will never become part of any DBRequest.
    if (!ParseProperties(document, *(key.get()), origin, req_list)) {
        return false;
    }

    EnqueueListToTables(req_list);
    return true;
}

bool ConfigJsonParser::Receive(const string &in_message,
                               IFMapOrigin::Origin origin) {
    ConfigJsonParser::RequestList req_list;

    Document document;
    document.Parse<0>(in_message.c_str());

    if (document.HasParseError()) {
        size_t pos = document.GetErrorOffset();
        // GetParseError returns const char *
        IFMAP_WARN(IFMapJsonLoadError,
                   "Error in parsing JSON message at position",
                   pos, "with error description", document.GetParseError());
    } else {
        cout << "No parse error\n";
        ParseDocument(document, origin, &req_list);
        //TmpParseDocument(document);
    }
    return true;
}

// For testing purposes only. Delete before release.
void ConfigJsonParser::TmpParseDocument(const rapidjson::Document &document) {
    for (Value::ConstMemberIterator itr = document.MemberBegin();
         itr != document.MemberEnd(); ++itr) {
        cout << "Key:" << itr->name.GetString();
        if (itr->value.IsNull()) cout << endl;
        else {
            cout << "Value" << endl;
        }
    }
}

