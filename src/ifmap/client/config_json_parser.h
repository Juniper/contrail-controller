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
        bool(const rapidjson::Value &, std::auto_ptr<AutogenProperty > *)
    > MetadataParseFn;
    typedef std::map<std::string, MetadataParseFn> MetadataParseMap;

    ConfigJsonParser(ConfigClientManager *mgr);

    void MetadataRegister(const std::string &metadata, MetadataParseFn parser);
    void MetadataClear(const std::string &module);
    bool Receive(const std::string &uuid, const std::string &in_message,
                 bool add_change, IFMapOrigin::Origin origin);
    ConfigClientManager *config_mgr() const { return mgr_; }
    ConfigClientManager *config_mgr() { return mgr_; }

private:
    bool ParseDocument(const rapidjson::Document &document, bool add_change,
        IFMapOrigin::Origin origin, ConfigClientManager::RequestList *req_list,
        IFMapTable::RequestKey *key) const;
    bool ParseNameType(const rapidjson::Document &document,
                       IFMapTable::RequestKey *key) const;
    bool ParseProperties(const rapidjson::Document &document, bool add_change,
        const IFMapTable::RequestKey &key, IFMapOrigin::Origin origin,
        ConfigClientManager::RequestList *req_list) const;
    bool ParseOneProperty(const rapidjson::Value &key_node,
        const rapidjson::Value &value_node, bool add_change,
        const IFMapTable::RequestKey &key, IFMapOrigin::Origin origin,
        ConfigClientManager::RequestList *req_list) const;
    bool ParseLinks(const rapidjson::Document &document, bool add_change,
        const IFMapTable::RequestKey &key, IFMapOrigin::Origin origin,
        ConfigClientManager::RequestList *req_list) const;
    bool ParseRef(const rapidjson::Value &ref_entry, bool add_change,
        IFMapOrigin::Origin origin, const std::string &to_underscore,
        const IFMapTable::RequestKey &key, ConfigClientManager::RequestList *req_list) const;

    ConfigClientManager *mgr_;
    MetadataParseMap metadata_map_;
};

#endif // ctrlplane_config_json_parser_h
