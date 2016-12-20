/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_config_cass2json_adapter_h
#define ctrlplane_config_cass2json_adapter_h

#include "json_adapter_data.h"

// The purpose of this class is to convert key-value pairs received from
// cassandra into one single json string.
// The user will pass a vector of key-value while creating the object. The
// constructor will create a json string, which will then be accessible via the
// doc_string() accessor.
class ConfigCass2JsonAdapter {
public:
    ConfigCass2JsonAdapter(const CassColumnKVVec &cdvec);
    const std::string &doc_string() { return doc_string_; }

private:
    static const std::string prop_prefix;
    static const std::string meta_prefix;
    static const std::string comma_str;
    bool CreateJsonString(const CassColumnKVVec &cdvec);
    bool AddOneEntry(const CassColumnKVVec &cdvec, int i);

    std::string doc_string_;
    int prop_plen_;
    int meta_plen_;
};

#endif // ctrlplane_config_cass2json_adapter_h
