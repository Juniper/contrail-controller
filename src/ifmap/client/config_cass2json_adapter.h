/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_config_cass2json_adapter_h
#define ctrlplane_config_cass2json_adapter_h
#include <map>
#include <vector>
#include "json_adapter_data.h"

class ConfigCassandraClient;
// The purpose of this class is to convert key-value pairs received from
// cassandra into one single json string.
// The user will pass a vector of key-value while creating the object. The
// constructor will create a json string, which will then be accessible via the
// doc_string() accessor.
class ConfigCass2JsonAdapter {
public:
    ConfigCass2JsonAdapter(ConfigCassandraClient *cassandra_client,
                           const CassColumnKVVec &cdvec);
    const std::string &doc_string() { return doc_string_; }

private:
    typedef std::vector<std::string> RefDataList;
    typedef std::map<std::string, RefDataList> RefTypeMap;

    typedef std::map<int, std::string> PropertyListDataMap;
    typedef std::map<std::string, PropertyListDataMap> ListPropertyMap;

    typedef std::vector<std::string> PropertyMapDataList;
    typedef std::map<std::string, PropertyMapDataList> MapPropertyMap;

    static const std::string prop_prefix;
    static const std::string list_prop_prefix;
    static const std::string map_prop_prefix;
    static const std::string meta_prefix;
    static const std::string backref_prefix;
    static const std::string child_prefix;
    static const std::string ref_prefix;
    static const std::string parent_type;
    static const std::string comma_str;
    bool CreateJsonString(const CassColumnKVVec &cdvec);
    bool AddOneEntry(const CassColumnKVVec &cdvec, int i);

    ConfigCassandraClient *cassandra_client_;
    std::string doc_string_;
    std::string type_;
    int prop_plen_;
    int propl_plen_;
    int propm_plen_;
    int meta_plen_;
    int backref_plen_;
    int child_plen_;
    int ref_plen_;
    int parent_plen_;

    RefTypeMap ref_type_map_;
    MapPropertyMap map_property_map_;
    ListPropertyMap list_property_map_;
};

#endif // ctrlplane_config_cass2json_adapter_h
