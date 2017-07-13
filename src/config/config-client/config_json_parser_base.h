/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_config_json_parser_base_h
#define ctrlplane_config_json_parser_base_h

#include <list>
#include <map>
#include <string>

#include "config_client_manager.h"

#include "base/queue_task.h"
#include "ifmap/ifmap_table.h"
#include "ifmap/ifmap_origin.h"

#include "rapidjson/document.h"

#include <boost/function.hpp>

class ConfigCass2JsonAdapter;

class ConfigJsonParserBase {
public:
    typedef std::set<std::string> ObjectTypeList;

    ConfigJsonParserBase();
    virtual void setup_objector_filter(ObjectTypeList *FilterList);
    virtual bool Receive(const ConfigCass2JsonAdapter &adapter, bool del);
    const ObjectTypeList &ObjectTypeListToRead() const {
        return obj_type_to_read_;
    }
   
    ObjectTypeList *GetObjectList() {
        return &obj_type_to_read_;
    }
    std::string GetParentName(const std::string &left,
                              const std::string &right) const;
    std::string GetLinkName(const std::string &left,
                            const std::string &right) const;
    std::string GetWrapperFieldName(const std::string &type_name,
                                    const std::string &property_name) const;

private:
    typedef std::pair<std::string, std::string> LinkMemberPair;
    typedef std::pair<std::string, bool> LinkDataPair;
    typedef std::map<LinkMemberPair, std::string> ParentNameMap;
    typedef std::map<LinkMemberPair, LinkDataPair> LinkNameMap;
    typedef std::map<std::string, std::string> WrapperFieldMap;

    //ConfigClientManager *mgr_;
    LinkNameMap link_name_map_;
    ParentNameMap parent_name_map_;

    WrapperFieldMap wrapper_field_map_;
    ObjectTypeList obj_type_to_read_;
    void setup_schema_graph_filter();
    void setup_schema_wrapper_property_info();
};

#endif // ctrlplane_config_json_parser_h
