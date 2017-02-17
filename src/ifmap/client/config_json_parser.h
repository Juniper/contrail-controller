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
class ConfigCass2JsonAdapter;

class ConfigJsonParser {
public:
    typedef boost::function<
        bool(const contrail_rapidjson::Value &, std::auto_ptr<AutogenProperty > *)
    > MetadataParseFn;
    typedef std::map<std::string, MetadataParseFn> MetadataParseMap;

    ConfigJsonParser(ConfigClientManager *mgr);

    void MetadataRegister(const std::string &metadata, MetadataParseFn parser);
    void MetadataClear(const std::string &module);
    bool Receive(const ConfigCass2JsonAdapter &adapter,
                 IFMapOrigin::Origin origin);
    ConfigClientManager *config_mgr() const { return mgr_; }
    ConfigClientManager *config_mgr() { return mgr_; }

private:
    bool ParseDocument(const ConfigCass2JsonAdapter &adapter,
        IFMapOrigin::Origin origin, ConfigClientManager::RequestList *req_list,
        IFMapTable::RequestKey *key) const;
    bool ParseNameType(const ConfigCass2JsonAdapter &adapter,
                       IFMapTable::RequestKey *key) const;
    bool ParseProperties(const ConfigCass2JsonAdapter &adapter,
        const IFMapTable::RequestKey &key, IFMapOrigin::Origin origin,
        ConfigClientManager::RequestList *req_list) const;
    bool ParseOneProperty(const ConfigCass2JsonAdapter &adapter,
        const contrail_rapidjson::Value &key_node,
        const contrail_rapidjson::Value &value_node,
        const IFMapTable::RequestKey &key, IFMapOrigin::Origin origin,
        ConfigClientManager::RequestList *req_list) const;
    bool ParseLinks(const ConfigCass2JsonAdapter &adapter,
        const IFMapTable::RequestKey &key, IFMapOrigin::Origin origin,
        ConfigClientManager::RequestList *req_list) const;
    bool ParseRef(const ConfigCass2JsonAdapter &adapter,
        const contrail_rapidjson::Value &ref_entry,
        IFMapOrigin::Origin origin, const std::string &refer,
        const IFMapTable::RequestKey &key,
        ConfigClientManager::RequestList *req_list) const;
    bool ParseOneRef(const ConfigCass2JsonAdapter &adapter,
        const contrail_rapidjson::Value &arr,
        const IFMapTable::RequestKey &key, IFMapOrigin::Origin origin,
        ConfigClientManager::RequestList *req_list, const std::string &key_str,
        size_t pos) const;

    ConfigClientManager *mgr_;
    MetadataParseMap metadata_map_;
};

#endif // ctrlplane_config_json_parser_h
