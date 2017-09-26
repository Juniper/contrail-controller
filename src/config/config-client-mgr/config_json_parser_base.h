/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_config_json_parser_base_h
#define ctrlplane_config_json_parser_base_h

#include <list>
#include <map>
#include <set>
#include <string>

#include "config_cass2json_adapter.h"
#include "config_client_manager.h"
#include "base/queue_task.h"

#include "rapidjson/document.h"

#include <boost/function.hpp>


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
    // This fucntion is used to setup object graphy concerned
    // by user, please follow the step:
    // 1. add concerned object with AddObjectType API one by one
    // 2. add relationship of object with AddParentName or 
    //    AddLinkName, if do not set this, ref/parent info will be 
    //    filtered out
    // 3. AddWrapperField for property map/list, if do no set this
    //    this property map/list will be filtered out.
    // please see ifmap/client/config_json_parser.cc as example.
    virtual void SetupGraphFilter() = 0;
    virtual bool Receive(const ConfigCass2JsonAdapter &adapter, 
                         bool add_change) = 0;
    virtual void EndOfConfig();
    const ObjectTypeList &ObjectTypeListToRead() const {
        return obj_type_to_read_;
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
        SetupGraphFilter();
        mgr_ = mgr;
    }

    uint64_t GetGenerationNumber() const {
        return mgr_->GetGenerationNumber();
    }

    void AddLinkName(LinkMemberPair member_pair, LinkDataPair data_pair) {
        link_name_map_.insert(make_pair(member_pair, data_pair));
    }

    void AddParentName(LinkMemberPair member_pair, std::string s) {
        parent_name_map_.insert(make_pair(member_pair, s));
    }

    void AddWrapperField(std::string key, std::string value) {
        wrapper_field_map_.insert(make_pair(key, value));
    }

    void AddObjectType(std::string object) {
        obj_type_to_read_.insert(object);
    }

    const bool IsReadObjectType(std::string objectType) {
        if (obj_type_to_read_.find(objectType) == obj_type_to_read_.end()) {
            return false;
        }
        return true;
    }

    bool IsListOrMapPropEmpty(const std::string &uuid_key,
                           const std::string &lookup_key) const;
private:
    ConfigClientManager *mgr_;
    LinkNameMap link_name_map_;
    ParentNameMap parent_name_map_;

    WrapperFieldMap wrapper_field_map_;
    ObjectTypeList obj_type_to_read_;
};

#endif // ctrlplane_config_json_parser_h
