/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */
#include "config_json_parser.h"

#include <boost/lexical_cast.hpp>

#include "config_cassandra_client.h"

#include "ifmap_log.h"
#include "base/autogen_util.h"
#include "client/config_log_types.h"
#include "ifmap/client/config_cass2json_adapter.h"
#include "ifmap/ifmap_log_types.h"

using contrail_rapidjson::Value;
using std::cout;
using std::endl;
using std::string;

#define CONFIG_PARSE_ASSERT(t, condition, key, value)                          \
    do {                                                                       \
        if (condition)                                                         \
            break;                                                             \
        IFMAP_WARN_LOG(ConfigurationMalformed ## t ## Warning ## Log,          \
                       Category::IFMAP, key, value, adapter.type(),            \
                       adapter.uuid());                                        \
        IFMAP_TRACE(ConfigurationMalformed ## t ## Warning ## Trace,           \
                    key, value, adapter.type(), adapter.uuid());               \
        cout << "CONFIG_PARSE_ERROR " << __FILE__ << ":" << __LINE__ << " ";   \
        cout << adapter.type() << " " << key << " " << value << " ";           \
        cout << adapter.uuid() << endl;                                        \
        if (ConfigCass2JsonAdapter::assert_on_parse_error())                   \
            assert(false);                                                     \
        return false;                                                          \
    } while (false)

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

bool ConfigJsonParser::ParseNameType(const ConfigCass2JsonAdapter &adapter,
                                     IFMapTable::RequestKey *key) const {
    // Type is the name of the document.
    Value::ConstMemberIterator itr = adapter.document().MemberBegin();
    CONFIG_PARSE_ASSERT(Type, autogen::ParseString(itr->name, &key->id_type),
                        "Name", "Bad name");

    key->id_type = itr->name.GetString();

    // Name is the fq_name field in the document.
    const Value &value_node = itr->value;
    CONFIG_PARSE_ASSERT(FqName, value_node.HasMember("fq_name"), key->id_type,
                        "Missing FQ name");
    const Value &fq_name_node = value_node["fq_name"];
    CONFIG_PARSE_ASSERT(FqName, fq_name_node.IsArray(), key->id_type,
                        "FQ name is not an array");
    CONFIG_PARSE_ASSERT(FqName, fq_name_node.Size(),
                        key->id_type, "FQ name array is empty");

    size_t i = 0;

    // Iterate over all items except the last one.
    for (; i < fq_name_node.Size() - 1; ++i) {
        key->id_name += fq_name_node[i].GetString();
        key->id_name += string(":");
    }
    key->id_name += fq_name_node[i].GetString();

    return true;
}

bool ConfigJsonParser::ParseOneProperty(const ConfigCass2JsonAdapter &adapter,
        const Value &key_node, const Value &value_node,
        const IFMapTable::RequestKey &key, IFMapOrigin::Origin origin,
        ConfigClientManager::RequestList *req_list) const {
    string metaname = key_node.GetString();
    MetadataParseMap::const_iterator loc = metadata_map_.find(metaname);
    if (loc == metadata_map_.end()) {
        return true;
    }
    auto_ptr<AutogenProperty> pvalue;
    bool success = (loc->second)(value_node, &pvalue);
    CONFIG_PARSE_ASSERT(Property, success, metaname,
                        "No entry in metadata map");
    std::replace(metaname.begin(), metaname.end(), '_', '-');
    config_mgr()->InsertRequestIntoQ(origin, "", "", metaname, pvalue, key,
                                     true, req_list);
    return true;
}

bool ConfigJsonParser::ParseProperties(const ConfigCass2JsonAdapter &adapter,
        const IFMapTable::RequestKey &key, IFMapOrigin::Origin origin,
        ConfigClientManager::RequestList *req_list) const {

    Value::ConstMemberIterator doc_itr = adapter.document().MemberBegin();
    const Value &value_node = doc_itr->value;
    for (Value::ConstMemberIterator itr = value_node.MemberBegin();
         itr != value_node.MemberEnd(); ++itr) {
        ParseOneProperty(adapter, itr->name, itr->value, key, origin,
                         req_list);
    }

    return true;
}

bool ConfigJsonParser::ParseRef(const ConfigCass2JsonAdapter &adapter,
        const Value &ref_entry, IFMapOrigin::Origin origin,
        const string &refer, const IFMapTable::RequestKey &key,
        ConfigClientManager::RequestList *req_list) const {
    const Value& to_node = ref_entry["to"];

    string from_underscore = key.id_type;
    std::replace(from_underscore.begin(), from_underscore.end(), '-', '_');
    string link_name =
        config_mgr()->GetLinkName(from_underscore, refer);
    CONFIG_PARSE_ASSERT(Reference, !link_name.empty(), refer,
                        "Link name is empty");
    string metaname = link_name;
    std::replace(metaname.begin(), metaname.end(), '-', '_');

    MetadataParseMap::const_iterator loc = metadata_map_.find(metaname);
    CONFIG_PARSE_ASSERT(Reference, loc != metadata_map_.end(), metaname,
                        "No entry in metadata map");

    auto_ptr<AutogenProperty> pvalue;
    if (ref_entry.HasMember("attr")) {
        const Value& attr_node = ref_entry["attr"];
        bool success = (loc->second)(attr_node, &pvalue);
        CONFIG_PARSE_ASSERT(ReferenceLinkAttributes, success, metaname,
                            "Link attribute parse error");
    }

    string neigh_name;
    neigh_name += to_node.GetString();

    config_mgr()->InsertRequestIntoQ(origin, refer, neigh_name,
                                 link_name, pvalue, key, true, req_list);

    return true;
}

bool ConfigJsonParser::ParseOneRef(const ConfigCass2JsonAdapter &adapter,
        const Value &arr, const IFMapTable::RequestKey &key,
        IFMapOrigin::Origin origin, ConfigClientManager::RequestList *req_list,
        const string &key_str, size_t pos) const {
    string refer = key_str.substr(0, pos);
    CONFIG_PARSE_ASSERT(Reference, arr.IsArray(), refer, "Invalid referene");
    for (size_t i = 0; i < arr.Size(); ++i)
        ParseRef(adapter, arr[i], origin, refer, key, req_list);
    return true;
}

bool ConfigJsonParser::ParseLinks(const ConfigCass2JsonAdapter &adapter,
        const IFMapTable::RequestKey &key, IFMapOrigin::Origin origin,
        ConfigClientManager::RequestList *req_list) const {
    Value::ConstMemberIterator doc_itr = adapter.document().MemberBegin();
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
            ParseOneRef(adapter, itr->value, key, origin, req_list, key_str,
                        pos);
            continue;
        }
        if (key_str.compare("parent_type") == 0) {
            const Value& ptype_node = itr->value;
            CONFIG_PARSE_ASSERT(Parent, ptype_node.IsString(), key_str,
                                "Invalid parent type");
            pos = key.id_name.find_last_of(":");
            if (pos != string::npos) {
                string parent_type = ptype_node.GetString();
                // Get the parent name from our name.
                string parent_name = key.id_name.substr(0, pos);
                string metaname =
                    config_mgr()->GetLinkName(parent_type,key.id_type);
                CONFIG_PARSE_ASSERT(Parent, !metaname.empty(), parent_type,
                                    "Missing link name");
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

bool ConfigJsonParser::ParseDocument(const ConfigCass2JsonAdapter &adapter,
        IFMapOrigin::Origin origin, ConfigClientManager::RequestList *req_list,
        IFMapTable::RequestKey *key) const {
    // Update the name and the type into 'key'.
    if (!ParseNameType(adapter, key)) {
        return false;
    }

    // For each property, we will clone 'key' to create our DBRequest's i.e.
    // 'key' will never become part of any DBRequest.
    if (!ParseProperties(adapter, *key, origin, req_list)){
        return false;
    }

    if (!ParseLinks(adapter, *key, origin, req_list)) {
        return false;
    }

    return true;
}

bool ConfigJsonParser::Receive(const ConfigCass2JsonAdapter &adapter,
                               IFMapOrigin::Origin origin) {
    ConfigClientManager::RequestList req_list;

    if (adapter.document().HasParseError() || !adapter.document().IsObject()) {
        size_t pos = adapter.document().GetErrorOffset();
        // GetParseError returns const char *
        IFMAP_WARN(IFMapJsonLoadError,
                   "Error in parsing JSON message at position",
                   pos, "with error description",
                   boost::lexical_cast<string>(
                       adapter.document().GetParseError()), adapter.uuid());
        return false;
    } else {
        auto_ptr<IFMapTable::RequestKey> key(new IFMapTable::RequestKey());
        if (!ParseDocument(adapter, origin, &req_list, key.get())) {
            STLDeleteValues(&req_list);
            return false;
        }
        config_mgr()->config_db_client()->FormDeleteRequestList(
                adapter.uuid(), &req_list, key.get(), true);
        config_mgr()->EnqueueListToTables(&req_list);
    }
    return true;
}
