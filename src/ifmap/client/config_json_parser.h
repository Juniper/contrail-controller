/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_config_json_parser_h
#define ctrlplane_config_json_parser_h

#include <list>
#include <map>
#include <string>

#include "ifmap/ifmap_table.h"
#include "ifmap/ifmap_origin.h"

#include <boost/function.hpp>
#include "rapidjson/document.h"

struct AutogenProperty;
class DB;
struct DBRequest;

class ConfigJsonParser {
public:
    typedef boost::function<
        bool(const rapidjson::Value &, std::auto_ptr<AutogenProperty > *)
    > MetadataParseFn;
    typedef std::map<std::string, MetadataParseFn> MetadataParseMap;
    typedef std::list<struct DBRequest *> RequestList;

    ConfigJsonParser(DB *db);

    void MetadataRegister(const std:: string &metadata, MetadataParseFn parser);
    void MetadataClear(const std::string &module);
    bool Receive(const std::string &in_message, IFMapOrigin::Origin origin);

    void TmpParseDocument(const rapidjson::Document &document);

private:
    bool ParseDocument(const rapidjson::Document &document,
                       IFMapOrigin::Origin origin, RequestList *req_list) const;
    bool ParseNameType(const rapidjson::Document &document,
                       IFMapTable::RequestKey *key) const;
    bool ParseProperties(const rapidjson::Document &document,
            const IFMapTable::RequestKey &key, IFMapOrigin::Origin origin,
            RequestList *req_list) const;
    IFMapTable::RequestKey *CloneKey(const IFMapTable::RequestKey &src) const;
    void EnqueueListToTables(RequestList *req_list) const;

    DB *db_;
    MetadataParseMap metadata_map_;
};

#endif // ctrlplane_config_json_parser_h
