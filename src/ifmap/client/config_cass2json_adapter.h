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

    ConfigCass2JsonAdapter(const std::string &uuid,
                           ConfigCassandraClient *cassandra_client,
                           const std::string &obj_type,
                           const CassColumnKVVec &cdvec);
    const contrail_rapidjson::Document &document() const {
        return json_document_;
    }
    static bool assert_on_parse_error() { return assert_on_parse_error_; }
    static void set_assert_on_parse_error(bool flag) {
        assert_on_parse_error_ = flag;
    }
    const std::string &uuid() const { return uuid_; }
    const std::string &type() const { return type_; }

private:
    void CreateJsonString(const std::string &obj_type,
                          const CassColumnKVVec &cdvec);
    void AddOneEntry(contrail_rapidjson::Value *jsonObject,
                     const std::string &obj_type,
                     const JsonAdapterDataType &c,
                     contrail_rapidjson::Document::AllocatorType &a);
    static std::string GetJsonString(
            const contrail_rapidjson::Value &attr_value);

    ConfigCassandraClient *cassandra_client_;
    std::string uuid_;
    std::string type_;
    contrail_rapidjson::Document json_document_;
    static bool assert_on_parse_error_;
};

#endif // ctrlplane_config_cass2json_adapter_h
