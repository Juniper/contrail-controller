/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_config_json_parser_h
#define ctrlplane_config_json_parser_h

#include <list>
#include <map>
#include <string>

#include "config_client_manager.h"

#include "base/queue_task.h"
#include "ifmap/ifmap_table.h"
#include "ifmap/ifmap_origin.h"

#include "rapidjson/document.h"

#include <boost/function.hpp>

struct AutogenProperty;

class ConfigJsonParser {
public:
    typedef boost::function<
        bool(const RAPIDJSON_NAMESPACE::Value &, std::auto_ptr<AutogenProperty > *)
    > MetadataParseFn;
    typedef std::map<std::string, MetadataParseFn> MetadataParseMap;

    ConfigJsonParser(ConfigClientManager *mgr);

    void MetadataRegister(const std::string &metadata, MetadataParseFn parser);
    void MetadataClear(const std::string &module);
    bool Receive(const std::string &uuid, const RAPIDJSON_NAMESPACE::Document &document,
                 IFMapOrigin::Origin origin);
    ConfigClientManager *config_mgr() const { return mgr_; }
    ConfigClientManager *config_mgr() { return mgr_; }

private:
    bool ParseDocument(const RAPIDJSON_NAMESPACE::Document &document,
        IFMapOrigin::Origin origin, ConfigClientManager::RequestList *req_list,
        IFMapTable::RequestKey *key) const;
    bool ParseNameType(const RAPIDJSON_NAMESPACE::Document &document,
                       IFMapTable::RequestKey *key) const;
    bool ParseProperties(const RAPIDJSON_NAMESPACE::Document &document,
        const IFMapTable::RequestKey &key, IFMapOrigin::Origin origin,
        ConfigClientManager::RequestList *req_list) const;
    bool ParseOneProperty(const RAPIDJSON_NAMESPACE::Value &key_node,
        const RAPIDJSON_NAMESPACE::Value &value_node,
        const IFMapTable::RequestKey &key, IFMapOrigin::Origin origin,
        ConfigClientManager::RequestList *req_list) const;
    bool ParseLinks(const RAPIDJSON_NAMESPACE::Document &document,
        const IFMapTable::RequestKey &key, IFMapOrigin::Origin origin,
        ConfigClientManager::RequestList *req_list) const;
    bool ParseRef(const RAPIDJSON_NAMESPACE::Value &ref_entry,
        IFMapOrigin::Origin origin, const std::string &to_underscore,
        const IFMapTable::RequestKey &key, ConfigClientManager::RequestList *req_list) const;

    ConfigClientManager *mgr_;
    MetadataParseMap metadata_map_;
};

#endif // ctrlplane_config_json_parser_h
