/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_config_cass2json_adapter_h
#define ctrlplane_config_cass2json_adapter_h
#include <map>
#include <set>
#include <vector>

#include "rapidjson/document.h"

#include "json_adapter_data.h"

class ConfigCassandraClient;
// The purpose of this class is to convert key-value pairs received from
// cassandra into one single json string.
// The user will pass a vector of key-value while creating the object. The
// constructor will create a json string, which will then be accessible via the
// doc_string() accessor.
class ConfigCass2JsonAdapter {
public:
    static const std::string fq_name_prefix;
    static const std::string prop_prefix;
    static const std::string list_prop_prefix;
    static const std::string map_prop_prefix;
    static const std::string ref_prefix;
    static const std::string parent_prefix;
    static const std::string parent_type_prefix;

    static const std::set<std::string> allowed_properties;

    ConfigCass2JsonAdapter(ConfigCassandraClient *cassandra_client,
                           const std::string &obj_type,
                           const CassColumnKVVec &cdvec);
    const rapidjson::Document &document() { return json_document_; }

private:
    bool CreateJsonString(const std::string &obj_type,
                          const CassColumnKVVec &cdvec);
    void AddOneEntry(rapidjson::Value *jsonObject, const std::string &obj_type,
                     const JsonAdapterDataType &c,
                     rapidjson::Document::AllocatorType &a);
    static std::string GetJsonString(const rapidjson::Value &attr_value);

    ConfigCassandraClient *cassandra_client_;
    std::string type_;
    rapidjson::Document json_document_;
};

#endif // ctrlplane_config_cass2json_adapter_h
