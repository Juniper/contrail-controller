/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_config_json_parser_h
#define ctrlplane_config_json_parser_h

#include <list>
#include <map>
#include <string>

#include "config-client-mgr/config_json_parser_base.h"

#include "base/queue_task.h"
#include "ifmap/ifmap_table.h"
#include "ifmap/ifmap_origin.h"

#include "rapidjson/document.h"
#include "ifmap/ifmap_server_table.h"
#include "ifmap/ifmap_server.h"

#include <boost/function.hpp>

struct AutogenProperty;
class ConfigCass2JsonAdapter;

class ConfigJsonParser : public ConfigJsonParserBase {
public:
    typedef boost::function<
        bool(const contrail_rapidjson::Value &, std::auto_ptr<AutogenProperty > *)
    > MetadataParseFn;
    typedef std::map<std::string, MetadataParseFn> MetadataParseMap;
    typedef std::list<struct DBRequest *> RequestList;

    ConfigJsonParser();
    ~ConfigJsonParser();

    virtual void SetupGraphFilter();
    virtual void EndOfConfig();

    void MetadataRegister(const std::string &metadata, MetadataParseFn parser);
    void MetadataClear(const std::string &module);
    virtual bool Receive(const ConfigCass2JsonAdapter &adapter,
                 bool add_change);
    void ifmap_server_set(IFMapServer *ifmap_server) {
             ifmap_server_ = ifmap_server;
         };

private:
    void SetupObjectFilter();
    void SetupSchemaGraphFilter();
    void SetupSchemaWrapperPropertyInfo();
    bool ParseDocument(const ConfigCass2JsonAdapter &adapter,
        IFMapOrigin::Origin origin, RequestList *req_list,
        IFMapTable::RequestKey *key, bool add_change) const;
    bool ParseNameType(const ConfigCass2JsonAdapter &adapter,
                       IFMapTable::RequestKey *key) const;
    bool ParseProperties(const ConfigCass2JsonAdapter &adapter,
        const IFMapTable::RequestKey &key, IFMapOrigin::Origin origin,
        RequestList *req_list, bool add_change) const;
    bool ParseOneProperty(const ConfigCass2JsonAdapter &adapter,
        const contrail_rapidjson::Value &key_node,
        const contrail_rapidjson::Value &value_node,
        const IFMapTable::RequestKey &key, IFMapOrigin::Origin origin,
        RequestList *req_list, bool add_change) const;
    bool ParseLinks(const ConfigCass2JsonAdapter &adapter,
        const IFMapTable::RequestKey &key, IFMapOrigin::Origin origin,
        RequestList *req_list, bool add_change) const;
    bool ParseRef(const ConfigCass2JsonAdapter &adapter,
        const contrail_rapidjson::Value &ref_entry,
        IFMapOrigin::Origin origin, const std::string &refer,
        const IFMapTable::RequestKey &key,
        RequestList *req_list, bool add_change) const;
    bool ParseOneRef(const ConfigCass2JsonAdapter &adapter,
        const contrail_rapidjson::Value &arr,
        const IFMapTable::RequestKey &key, IFMapOrigin::Origin origin,
        RequestList *req_list, const std::string &key_str,
        size_t pos, bool add_change) const;
    void EnqueueListToTables(RequestList *req_list) const;
    void InsertRequestIntoQ(IFMapOrigin::Origin origin,
        const std::string &neigh_type, const std::string &neigh_name,
        const std::string &metaname, std::auto_ptr<AutogenProperty > pvalue,
        const IFMapTable::RequestKey &key, bool add_change,
        RequestList *req_list) const;

    IFMapTable::RequestKey *CloneKey(const IFMapTable::RequestKey &src) const;
    IFMapServer *ifmap_server_;
    MetadataParseMap metadata_map_;
};

#endif // ctrlplane_config_json_parser_h
