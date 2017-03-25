/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#include "config_cass2json_adapter.h"

#include <assert.h>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/assign/list_of.hpp>
#include <iostream>

#include "base/string_util.h"
#include "config_cassandra_client.h"
#include "config_json_parser.h"
#include "ifmap_log.h"
#include "client/config_log_types.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

using boost::assign::list_of;
using contrail_rapidjson::Document;
using contrail_rapidjson::StringBuffer;
using contrail_rapidjson::Value;
using contrail_rapidjson::Writer;
using std::cout;
using std::endl;
using std::set;
using std::string;

const string ConfigCass2JsonAdapter::fq_name_prefix = "fq_name";
const string ConfigCass2JsonAdapter::prop_prefix = "prop:";
const string ConfigCass2JsonAdapter::list_prop_prefix = "propl:";
const string ConfigCass2JsonAdapter::map_prop_prefix = "propm:";
const string ConfigCass2JsonAdapter::ref_prefix = "ref:";
const string ConfigCass2JsonAdapter::parent_prefix = "parent:";
const string ConfigCass2JsonAdapter::parent_type_prefix = "parent_type";

const set<string> ConfigCass2JsonAdapter::allowed_properties =
             list_of(prop_prefix)(map_prop_prefix)
                    (list_prop_prefix)(ref_prefix)(parent_prefix);

bool ConfigCass2JsonAdapter::assert_on_parse_error_ =
    getenv("CONTRAIL_CONFIG_ASSERT_ON_PARSE_ERROR") != NULL;

#define CONFIG_PARSE_ASSERT(t, condition)                                      \
    do {                                                                       \
        if (condition)                                                         \
            break;                                                             \
        IFMAP_WARN_LOG(ConfigurationMalformed ## t ## Warning ## Log,          \
                       Category::IFMAP, c.key, c.value, type_, uuid_);         \
        IFMAP_TRACE(ConfigurationMalformed ## t ## Warning ## Trace,           \
                    c.key, c.value, type_, uuid_);                             \
        cout << "CONFIG_PARSE_ERROR " << __FILE__ << ":" << __LINE__ << " ";   \
        cout << type_ << " " << c.key << " " << c.value << " ";                \
        cout << uuid_ << endl;                                                 \
        if (assert_on_parse_error_)                                            \
            assert(false);                                                     \
        return;                                                                \
    } while (false)

ConfigCass2JsonAdapter::ConfigCass2JsonAdapter(const string &uuid,
       ConfigCassandraClient *cassandra_client, const string &obj_type,
       const CassColumnKVVec &cdvec) : cassandra_client_(cassandra_client),
       uuid_(uuid) {
    CreateJsonString(obj_type, cdvec);
}

string ConfigCass2JsonAdapter::GetJsonString(const Value &attr_value) {
    StringBuffer buffer;
    Writer<StringBuffer> writer(buffer);
    attr_value.Accept(writer);
    return buffer.GetString();
}

void ConfigCass2JsonAdapter::AddOneEntry(Value *jsonObject,
    const string &obj_type, const JsonAdapterDataType &c,
    Document::AllocatorType &a) {

    // Process scalar property values.
    if (boost::starts_with(c.key, prop_prefix)) {
        if (c.value == "null")
            return;

        string c_value = c.value;
        if (c.key == "prop:bgpaas_session_attributes")
            c_value = "\"\"";
        Document prop_document(&a);
        prop_document.Parse<0>(c_value.c_str());
        CONFIG_PARSE_ASSERT(Property, !prop_document.HasParseError());
        Value vk;
        jsonObject->AddMember(
            vk.SetString(c.key.substr(prop_prefix.size()).c_str(), a),
                         prop_document, a);
        return;
    }

    // Process property list and  property map values.
    bool is_map = false;
    if ((is_map = boost::starts_with(c.key, map_prop_prefix)) ||
            boost::starts_with(c.key, list_prop_prefix)) {
        size_t from_front_pos = c.key.find(':');
        size_t from_back_pos = c.key.rfind(':');
        string prop_map = c.key.substr(from_front_pos + 1,
                                       from_back_pos - from_front_pos - 1);
        string wrapper = cassandra_client_->mgr()->GetWrapperFieldName(type_,
                                                       prop_map);
        if (!jsonObject->HasMember(prop_map.c_str())) {
            Value v;
            Value vk;
            jsonObject->AddMember(
                vk.SetString(prop_map.c_str(), a), v.SetObject(), a);
            Value va;
            Value vak;
            (*jsonObject)[prop_map.c_str()].AddMember(
                vak.SetString(wrapper.c_str(), a), va.SetArray(), a);
        }

        Document map_document(&a);
        map_document.Parse<0>(c.value.c_str());
        if (is_map)
            CONFIG_PARSE_ASSERT(PropertyMap, !map_document.HasParseError());
        else
            CONFIG_PARSE_ASSERT(PropertyList, !map_document.HasParseError());

        (*jsonObject)[prop_map.c_str()][wrapper.c_str()].PushBack(
            map_document, a);
        return;
    }

    if (boost::starts_with(c.key, ref_prefix)) {
        size_t from_front_pos = c.key.find(':');
        size_t from_back_pos = c.key.rfind(':');
        CONFIG_PARSE_ASSERT(Reference, from_front_pos != string::npos);
        CONFIG_PARSE_ASSERT(Reference, from_back_pos != string::npos);
        string ref_type = c.key.substr(from_front_pos + 1,
                                       from_back_pos-from_front_pos - 1);
        string ref_uuid = c.key.substr(from_back_pos + 1);

        string fq_name_ref = cassandra_client_->UUIDToFQName(ref_uuid).second;
        if (fq_name_ref == "ERROR")
            return;
        string r = ref_type + "_refs";
        if (!jsonObject->HasMember(r.c_str())) {
            Value v;
            Value vk;
            jsonObject->AddMember(vk.SetString(r.c_str(), a), v.SetArray(), a);
        }

        Value v;
        v.SetObject();

        Value vs1;
        Value vs2;
        v.AddMember("to", vs1.SetString(fq_name_ref.c_str(), a), a);
        v.AddMember("uuid", vs2.SetString(ref_uuid.c_str(), a), a);

        bool link_with_attr =
            cassandra_client_->mgr()->IsLinkWithAttr(obj_type, ref_type);
        if (link_with_attr) {
            Document ref_document(&a);
            ref_document.Parse<0>(c.value.c_str());
            CONFIG_PARSE_ASSERT(ReferenceLinkAttributes,
                                !ref_document.HasParseError());
            CONFIG_PARSE_ASSERT(ReferenceLinkAttributes,
                                ref_document.HasMember("attr"));
            Value &attr_value = ref_document["attr"];
            v.AddMember("attr", attr_value, a);
        } else {
            Value vm;
            v.AddMember("attr", vm.SetObject(), a);
        }
        (*jsonObject)[r.c_str()].PushBack(v, a);
        return;
    }

    if (boost::starts_with(c.key, parent_prefix)) {
        size_t pos = c.key.rfind(':');
        CONFIG_PARSE_ASSERT(Parent, pos != string::npos);
        size_t type_pos = c.key.find(':');
        CONFIG_PARSE_ASSERT(Parent, type_pos != string::npos);
        Value v;
        Value vk;
        jsonObject->AddMember(vk.SetString(parent_type_prefix.c_str(), a),
                    v.SetString(c.key.substr(type_pos + 1,
                                             pos-type_pos - 1).c_str(), a), a);
        return;
    }

    if (!c.key.compare(fq_name_prefix)) {
        Document fq_name_document(&a);
        fq_name_document.Parse<0>(c.value.c_str());
        CONFIG_PARSE_ASSERT(FqName, !fq_name_document.HasParseError());
        Value vk;
        jsonObject->AddMember(vk.SetString(c.key.c_str(), a),
                              fq_name_document, a);
        return;
    }

    if (!c.key.compare("type")) {
        // Prepend the 'type'. This is "our key", with value being the json
        // sub-document containing all other columns.
        CONFIG_PARSE_ASSERT(Type, type_ != obj_type);
        type_ = c.value;
        type_.erase(remove(type_.begin(), type_.end(), '\"' ), type_.end());
        return;
    }
}

void ConfigCass2JsonAdapter::CreateJsonString(const string &obj_type,
                                              const CassColumnKVVec &cdvec) {
    if (cdvec.empty())
        return;
    Document::AllocatorType &a = json_document_.GetAllocator();
    Value jsonObject;
    jsonObject.SetObject();

    // First look for and part "type" field. We usually expect it to be at the
    // end as column names are suppose to be allways sorted lexicographically.
    size_t type_index = -1;
    size_t i = cdvec.size();
    while (i--) {
        if (!cdvec[i].key.compare("type")) {
            AddOneEntry(&jsonObject, obj_type, cdvec[i], a);
            type_index = i;
            break;
        }
    }

    if (type_.empty()) {
        IFMAP_WARN_LOG(ConfigurationMissingTypeWarningLog, Category::IFMAP,
                       uuid_, obj_type);
        IFMAP_TRACE(ConfigurationMissingTypeWarningTrace, uuid_, obj_type);
        return;
    }

    for (size_t i = 0; i < cdvec.size(); ++i) {
        if (i != type_index)
            AddOneEntry(&jsonObject, obj_type, cdvec[i], a);
    }

    Value vk;
    json_document_.SetObject().AddMember(vk.SetString(type_.c_str(), a),
                                         jsonObject, a);
}
