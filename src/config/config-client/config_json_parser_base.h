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
    typedef std::pair<std::string, std::string> LinkMemberPair;
    typedef std::pair<std::string, bool> LinkDataPair;
    typedef std::map<LinkMemberPair, std::string> ParentNameMap;
    typedef std::map<LinkMemberPair, LinkDataPair> LinkNameMap;
    typedef std::map<std::string, std::string> WrapperFieldMap;

    ConfigJsonParserBase();
    virtual ~ConfigJsonParserBase();
    virtual void setup_schema_graph_filter();
    virtual void setup_schema_wrapper_property_info();
    virtual void setup_objector_filter();
    virtual bool Receive(const ConfigCass2JsonAdapter &adapter, bool add_change);
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

    bool IsLinkWithAttr(const std::string &left,
                        const std::string &right) const;

    void Init(ConfigClientManager *mgr) {
        setup_schema_graph_filter();
        setup_schema_wrapper_property_info();
        setup_objector_filter();   
        mgr_ = mgr;
    }

    uint64_t GetGenerationNumber() const {
        return mgr_->GetGenerationNumber();
    }

    void AddLinkName(LinkMemberPair member_pair, LinkDataPair data_pair){
        link_name_map_.insert(make_pair(member_pair, data_pair));
    }

    void AddParentName(LinkMemberPair member_pair, std::string s){
        parent_name_map_.insert(make_pair(member_pair, s));
    }

    void AddWrapperField(std::string key, std::string value){
        wrapper_field_map_.insert(make_pair(key, value));
    }

    void AddObjectType(std::string object){
        obj_type_to_read_.insert(object);
    }

    bool IsPropsEmpty(const std::string &uuid_key, 
                           const std::string &lookup_key) const;
private:

    ConfigClientManager *mgr_;
    LinkNameMap link_name_map_;
    ParentNameMap parent_name_map_;

    WrapperFieldMap wrapper_field_map_;
    ObjectTypeList obj_type_to_read_;
};

#endif // ctrlplane_config_json_parser_h
