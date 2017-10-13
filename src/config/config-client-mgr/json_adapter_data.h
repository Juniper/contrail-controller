/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_json_adapter_data_h
#define ctrlplane_json_adapter_data_h

#include <string>
#include <vector>

struct JsonAdapterDataType {
    JsonAdapterDataType(const std::string &k, const std::string &v)
        : key(k), value(v) {
        ref_fq_name = "";
    }
    std::string key;
    std::string value;
    std::string ref_fq_name;
};

typedef std::vector<JsonAdapterDataType> CassColumnKVVec;

#endif // ctrlplane_json_adapter_data_h
