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

    //cout << "fq-name is " << key->id_name << endl;
    //cout << "type is " << key->id_type << endl;

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

bool ConfigJsonParser::ParseOneProperty(const Value &key_node,
        const Value &value_node, bool add_change,
        const IFMapTable::RequestKey &key, IFMapOrigin::Origin origin,
        RequestList *req_list) const {
    string metaname = key_node.GetString();
    MetadataParseMap::const_iterator loc = metadata_map_.find(metaname);
    if (loc == metadata_map_.end()) {
        return true;
    }
    cout << "Property: " << metaname << endl;
    auto_ptr<AutogenProperty> pvalue;
    bool success = (loc->second)(value_node, &pvalue);
    if (!success) {
        cout << "loc->second call failure\n";
        return false;
    }
    // Empty id_type and id_name strings.
    IFMapServerTable::RequestData *data =
        new IFMapServerTable::RequestData(origin, "", "");

    // For properties, the autogen-code is expecting dashes in the name.
    std::replace(metaname.begin(), metaname.end(), '_', '-');
    data->metadata = metaname;
    data->content.reset(pvalue.release());

    DBRequest *db_request = new DBRequest();
    db_request->oper = (add_change ? DBRequest::DB_ENTRY_ADD_CHANGE :
                        DBRequest::DB_ENTRY_DELETE);
    db_request->key.reset(CloneKey(key));
    db_request->data.reset(data);

    req_list->push_back(db_request);
    return true;
}

bool ConfigJsonParser::ParseProperties(const Document &document,
        bool add_change, const IFMapTable::RequestKey &key,
        IFMapOrigin::Origin origin, RequestList *req_list) const {

    Value::ConstMemberIterator doc_itr = document.MemberBegin();
    const Value &value_node = doc_itr->value;
    for (Value::ConstMemberIterator itr = value_node.MemberBegin();
         itr != value_node.MemberEnd(); ++itr) {
        ParseOneProperty(itr->name, itr->value, add_change, key, origin,
                         req_list);
    }

    return true;
}

void ConfigJsonParser::InsertRequestIntoQ(IFMapOrigin::Origin origin,
        const string &neigh_type, const string &neigh_name,
        const string &metaname, auto_ptr<AutogenProperty > pvalue,
        const IFMapTable::RequestKey &key, bool add_change,
        RequestList *req_list) const {

    IFMapServerTable::RequestData *data =
        new IFMapServerTable::RequestData(origin, neigh_type, neigh_name);
    data->metadata = metaname;
    data->content.reset(pvalue.release());

    DBRequest *db_request = new DBRequest();
    db_request->oper = (add_change ? DBRequest::DB_ENTRY_ADD_CHANGE :
                        DBRequest::DB_ENTRY_DELETE);
    db_request->key.reset(CloneKey(key));
    db_request->data.reset(data);

    req_list->push_back(db_request);
}

bool ConfigJsonParser::ParseRef(const Value &ref_entry, bool add_change,
        IFMapOrigin::Origin origin, const string &to_underscore,
        const string &neigh_type, const IFMapTable::RequestKey &key,
        RequestList *req_list) const {
    const Value& to_node = ref_entry["to"];
    assert(to_node.IsArray());

    string from_underscore = key.id_type;
    std::replace(from_underscore.begin(), from_underscore.end(), '-', '_');
    string metaname = from_underscore + "_" + to_underscore;

    MetadataParseMap::const_iterator loc = metadata_map_.find(metaname);
    if (loc == metadata_map_.end()) {
        cout << metaname << " not found in map" << endl;
        return false;
    }
    const Value& attr_node = ref_entry["attr"];
    auto_ptr<AutogenProperty> pvalue;
    bool success = (loc->second)(attr_node, &pvalue);
    if (!success) {
        cout << "loc->second call failure" << endl;
        return false;
    }

    string neigh_name;
    size_t i = 0;
    for (; i < to_node.Size() - 1; ++i) {
        neigh_name += to_node[i].GetString();
        neigh_name += string(":");
    }
    neigh_name += to_node[i].GetString();
    cout << "neigh type " << neigh_type
         << " ----- neigh name is " << neigh_name << endl;

    string metaname_dash = metaname;
    std::replace(metaname_dash.begin(), metaname_dash.end(), '_', '-');
    InsertRequestIntoQ(origin, neigh_type, neigh_name, metaname_dash, pvalue,
                       key, add_change, req_list);

    return true;
}

bool ConfigJsonParser::ParseLinks(const Document &document, bool add_change,
        const IFMapTable::RequestKey &key, IFMapOrigin::Origin origin,
        RequestList *req_list) const {

    Value::ConstMemberIterator doc_itr = document.MemberBegin();
    const Value &properties = doc_itr->value;
    for (Value::ConstMemberIterator itr = properties.MemberBegin();
         itr != properties.MemberEnd(); ++itr) {
        string key_str = itr->name.GetString();
        // Skip all the back-refs.
        if (key_str.find("back_refs") != string::npos) {
            continue;
        }
        size_t pos = key_str.find("_refs");
        if (pos != string::npos) {
            string to_underscore = key_str.substr(0, pos);
            string neigh_type = to_underscore;
            std::replace(neigh_type.begin(), neigh_type.end(), '_', '-');
            //cout << "found ref " << key_str << " type " << neigh_type;
            const Value& arr = itr->value;
            assert(arr.IsArray());
            //cout << " size is " << arr.Size() << endl;
            for (size_t i = 0; i < arr.Size(); ++i) {
                //const Value& ref_entry = arr[i];
                ParseRef(arr[i], add_change, origin, to_underscore, neigh_type,
                         key, req_list);
            }
        }
        if (key_str.compare("parent_type") == 0) {
            const Value& ptype_node = itr->value;
            assert(ptype_node.IsString());
            pos = key.id_name.find_last_of(":");
            if (pos != string::npos) {
                string parent_type = ptype_node.GetString();
                // Get the parent name from our name.
                string parent_name = key.id_name.substr(0, pos);
                cout << "parent name is " << parent_name;
                string metaname = parent_type + "-" + key.id_type;
                cout << " metaname is " << metaname << endl;
                auto_ptr<AutogenProperty > pvalue;
                InsertRequestIntoQ(origin, parent_type, parent_name, metaname,
                                   pvalue, key, add_change, req_list);
            } else {
                continue;
            }
        }
    }

    return true;
}

bool ConfigJsonParser::ParseDocument(const Document &document, bool add_change,
        IFMapOrigin::Origin origin, RequestList *req_list,
        IFMapTable::RequestKey *key) const {

    // Update the name and the type into 'key'.
    if (!ParseNameType(document, key)) {
        return false;
    }

    // For each property, we will clone 'key' to create our DBRequest's i.e.
    // 'key' will never become part of any DBRequest.
    if (!ParseProperties(document, add_change, *key, origin, req_list)){
        return false;
    }

    if (!ParseLinks(document, add_change, *key, origin, req_list)) {
        return false;
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
            IFMAP_TRACE(IFMapTblNotFoundTrace, "Cant find table", key->id_type);
        }
    }
}

bool ConfigJsonParser::Receive(const string &in_message, bool add_change,
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
        return false;
    } else {
        cout << "No parse error\n";
        auto_ptr<IFMapTable::RequestKey> key(new IFMapTable::RequestKey());
        if (ParseDocument(document, add_change, origin, &req_list, key.get())) {
            CompareOldAndNewDocuments(document, in_message, *(key.get()),
                                      origin, &req_list);
        } else {
            return false;
        }
        EnqueueListToTables(&req_list);
        //TmpParseDocument(document);
    }
    return true;
}

void ConfigJsonParser::CompareOldAndNewDocuments(
        const rapidjson::Document &document, const string &in_message,
        const IFMapTable::RequestKey &key, IFMapOrigin::Origin origin,
        RequestList *req_list) {
    Value::ConstMemberIterator itr = document.MemberBegin();
    const Value &new_properties = itr->value;
    assert(new_properties.HasMember("uuid"));
    const Value &uuid_node = new_properties["uuid"];
    assert(uuid_node.IsString());

    string uuid_key = uuid_node.GetString();
    Udmap_Iter iter = uuid_doc_map_.find(uuid_key);
    if (iter == uuid_doc_map_.end()) {
        std::pair<Udmap_Iter, bool> ret;
        ret = uuid_doc_map_.insert(make_pair(uuid_key, in_message));
        assert(ret.second);
        return;
    }

    // This object already exists in our database. Compare the old and the new.
    string old_message = iter->second;
    Document old_document;
    old_document.Parse<0>(old_message.c_str());

    Value::ConstMemberIterator old_doc_itr = old_document.MemberBegin();
    const Value &old_properties = old_doc_itr->value;
    for (Value::ConstMemberIterator oitr = old_properties.MemberBegin();
         oitr != old_properties.MemberEnd(); ++oitr) {
        if (new_properties.HasMember(oitr->name.GetString())) {
            continue;
        } else {
            // If the new update does not have a property that we had before,
            // send a property-delete to the table.
            ParseOneProperty(oitr->name, oitr->value, false, key, origin,
                             req_list);
        }
    }
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

