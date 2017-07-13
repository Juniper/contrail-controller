/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */
#include "config_json_parser.h"

#include <boost/lexical_cast.hpp>

#include "config_cassandra_client.h"

#include "base/autogen_util.h"
#include "client/config_log_types.h"

ConfigJsonParserBase::ConfigJsonParserBase(){
    setup_schema_graph_filter();
    setup_schema_wrapper_property_info();
    setup_objector_filter(GetObjectList());
}

void ConfigJsonParserBase::setup_schema_graph_filter(){
    vnc_cfg_FilterInfo vnc_filter_info;
    bgp_schema_FilterInfo bgp_schema_filter_info;

    bgp_schema_Server_GenerateGraphFilter(&bgp_schema_filter_info);
    vnc_cfg_Server_GenerateGraphFilter(&vnc_filter_info);

    for (vnc_cfg_FilterInfo::iterator it = vnc_filter_info.begin();
         it != vnc_filter_info.end(); it++) {
        if (it->is_ref_) {
            link_name_map_.insert(make_pair(make_pair(it->left_, it->right_),
                                    make_pair(it->metadata_, it->linkattr_)));
        } else {
            parent_name_map_.insert(make_pair(make_pair(it->left_, it->right_),
                                              it->metadata_));
        }
    }

    for (bgp_schema_FilterInfo::iterator it = bgp_schema_filter_info.begin();
         it != bgp_schema_filter_info.end(); it++) {
        if (it->is_ref_) {
            link_name_map_.insert(make_pair(make_pair(it->left_, it->right_),
                                    make_pair(it->metadata_, it->linkattr_)));
        } else {
            parent_name_map_.insert(make_pair(make_pair(it->left_, it->right_),
                                              it->metadata_));
        }
    }
}

void ConfigJsonParserBase::setup_schema_wrapper_property_info() {
    bgp_schema_Server_GenerateWrapperPropertyInfo(&wrapper_field_map_);
    vnc_cfg_Server_GenerateWrapperPropertyInfo(&wrapper_field_map_);
}

virtual void ConfigJsonParserBase::setup_objector_filter(ObjectTypeList *FilterList){
    bgp_schema_Server_GenerateObjectTypeList(FilterList);
    vnc_cfg_Server_GenerateObjectTypeList(FilterList);
}


string ConfigJsonParserBase::GetParentName(const string &left,
                                          const string &right) const {
    ParentNameMap::const_iterator it =
        parent_name_map_.find(make_pair(left, right));
    if (it == parent_name_map_.end())
        return "";
    return it->second;
}

string ConfigJsonParserBase::GetLinkName(const string &left,
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

string ConfigJsonParserBase::GetWrapperFieldName(const string &type_name,
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

virtual bool ConfigJsonParserBase::Receive(const ConfigCass2JsonAdapter &adapter, bool add_change) {
}
