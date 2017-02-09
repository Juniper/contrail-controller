/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */
#include "config_json_parser.h"

#include <boost/lexical_cast.hpp>

#include "config_cassandra_client.h"

#include "ifmap/ifmap_log.h"
#include "ifmap/ifmap_log_types.h"

using namespace rapidjson;
using namespace std;

ConfigJsonParser::ConfigJsonParser(ConfigClientManager *mgr)
    : mgr_(mgr) {
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

    return true;
}

bool ConfigJsonParser::ParseOneProperty(const Value &key_node,
        const Value &value_node, const IFMapTable::RequestKey &key,
        IFMapOrigin::Origin origin,
        ConfigClientManager::RequestList *req_list) const {
    string metaname = key_node.GetString();
    MetadataParseMap::const_iterator loc = metadata_map_.find(metaname);
    if (loc == metadata_map_.end()) {
        return true;
    }
    auto_ptr<AutogenProperty> pvalue;
    bool success = (loc->second)(value_node, &pvalue);
    if (!success) {
        cout << "Parsing value node for " << metaname << " failed" << endl;
        return false;
    }

    std::replace(metaname.begin(), metaname.end(), '_', '-');
    config_mgr()->InsertRequestIntoQ(origin, "", "", metaname, pvalue, key,
                                     true, req_list);

    return true;
}

bool ConfigJsonParser::ParseProperties(const Document &document,
        const IFMapTable::RequestKey &key, IFMapOrigin::Origin origin,
        ConfigClientManager::RequestList *req_list) const {

    Value::ConstMemberIterator doc_itr = document.MemberBegin();
    const Value &value_node = doc_itr->value;
    for (Value::ConstMemberIterator itr = value_node.MemberBegin();
         itr != value_node.MemberEnd(); ++itr) {
        ParseOneProperty(itr->name, itr->value, key, origin,
                         req_list);
    }

    return true;
}

bool ConfigJsonParser::ParseRef(const Value &ref_entry,
        IFMapOrigin::Origin origin, const string &to_underscore,
        const IFMapTable::RequestKey &key,
        ConfigClientManager::RequestList *req_list) const {
    const Value& to_node = ref_entry["to"];

    string from_underscore = key.id_type;
    std::replace(from_underscore.begin(), from_underscore.end(), '-', '_');
    string link_name =
        config_mgr()->GetLinkName(from_underscore, to_underscore);
    string metaname = link_name;
    std::replace(metaname.begin(), metaname.end(), '-', '_');

    MetadataParseMap::const_iterator loc = metadata_map_.find(metaname);
    if (loc == metadata_map_.end()) {
        cout << metaname << " not found in metadata map" << endl;
        return false;
    }

    auto_ptr<AutogenProperty> pvalue;
    if (ref_entry.HasMember("attr")) {
        const Value& attr_node = ref_entry["attr"];
        bool success = (loc->second)(attr_node, &pvalue);
        if (!success) {
            cout << "Parsing link attribute for " << metaname << " failed"
                 << endl;
            return false;
        }
    }

    string neigh_name;
    neigh_name += to_node.GetString();

    config_mgr()->InsertRequestIntoQ(origin, to_underscore, neigh_name,
                                 link_name, pvalue, key, true, req_list);

    return true;
}

bool ConfigJsonParser::ParseLinks(const Document &document, 
        const IFMapTable::RequestKey &key, IFMapOrigin::Origin origin,
        ConfigClientManager::RequestList *req_list) const {
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
            const Value& arr = itr->value;
            assert(arr.IsArray());
            for (size_t i = 0; i < arr.Size(); ++i) {
                ParseRef(arr[i], origin, to_underscore,
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
                string metaname =
                    config_mgr()->GetLinkName(parent_type,key.id_type);
                auto_ptr<AutogenProperty > pvalue;
                config_mgr()->InsertRequestIntoQ(origin, parent_type,
                     parent_name, metaname, pvalue, key, true, req_list);
            } else {
                continue;
            }
        }
    }

    return true;
}

bool ConfigJsonParser::ParseDocument(const Document &document,
        IFMapOrigin::Origin origin, ConfigClientManager::RequestList *req_list,
        IFMapTable::RequestKey *key) const {
    // Update the name and the type into 'key'.
    if (!ParseNameType(document, key)) {
        return false;
    }

    // For each property, we will clone 'key' to create our DBRequest's i.e.
    // 'key' will never become part of any DBRequest.
    if (!ParseProperties(document, *key, origin, req_list)){
        return false;
    }

    if (!ParseLinks(document, *key, origin, req_list)) {
        return false;
    }

    return true;
}

bool ConfigJsonParser::Receive(const string &uuid,
                               const rapidjson::Document &document,
                               IFMapOrigin::Origin origin) {
    ConfigClientManager::RequestList req_list;

    if (document.HasParseError()) {
        size_t pos = document.GetErrorOffset();
        // GetParseError returns const char *
        IFMAP_WARN(IFMapJsonLoadError,
                   "Error in parsing JSON message at position",
                   pos, "with error description",
                   boost::lexical_cast<string>(document.GetParseError()));
        return false;
    } else {
        auto_ptr<IFMapTable::RequestKey> key(new IFMapTable::RequestKey());
        if (!ParseDocument(document, origin, &req_list, key.get()))
            return false;
        config_mgr()->config_db_client()->FormDeleteRequestList(uuid, &req_list,
                                                            key.get(), true);
        config_mgr()->EnqueueListToTables(&req_list);
    }
    return true;
}
