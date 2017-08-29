/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */
#include "config_json_parser_base.h"

#include <boost/lexical_cast.hpp>

#include "config_cassandra_client.h"

#include "base/autogen_util.h"
#include "config_client_log_types.h"

ConfigJsonParserBase::ConfigJsonParserBase(){
}

ConfigJsonParserBase::~ConfigJsonParserBase() {
} 

void ConfigJsonParserBase::EndOfConfig() {
    return;
}

std::string ConfigJsonParserBase::GetParentName(const string &left,
                                          const string &right) const {
    ParentNameMap::const_iterator it =
        parent_name_map_.find(make_pair(left, right));
    if (it == parent_name_map_.end())
        return "";
    return it->second;
}

std::string ConfigJsonParserBase::GetLinkName(const string &left,
                                        const string &right) const {
    LinkNameMap::const_iterator it =
        link_name_map_.find(make_pair(left, right));
    if (it == link_name_map_.end())
        return "";
    return it->second.first;
}

bool ConfigJsonParserBase::IsLinkWithAttr(const string &left,
                                         const string &right) const {
    LinkNameMap::const_iterator it =
        link_name_map_.find(make_pair(left, right));
    if (it == link_name_map_.end())
        return false;
    return it->second.second;
}

std::string ConfigJsonParserBase::GetWrapperFieldName(const string &type_name,
                                          const string &property_name) const {
    WrapperFieldMap::const_iterator it =
        wrapper_field_map_.find(type_name+':'+property_name);
    if (it == wrapper_field_map_.end()) {
        return "";
    } else {
        // TODO: Fix the autogen to have _ instead of -
        string temp_str = it->second;
        std::replace(temp_str.begin(), temp_str.end(), '-', '_');
        return temp_str;
    }
}

bool ConfigJsonParserBase::IsListOrMapPropEmpty(const string &uuid_key,
      const string &lookup_key) const {
    return mgr_->config_db_client()->IsListOrMapPropEmpty(uuid_key, lookup_key); 
}

