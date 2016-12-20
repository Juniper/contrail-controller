/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_config_json_parser_h
#define ctrlplane_config_json_parser_h

#include <list>
#include <map>
#include <string>

#include "base/queue_task.h"
#include "ifmap/ifmap_table.h"
#include "ifmap/ifmap_origin.h"

#include "rapidjson/document.h"

#include <boost/function.hpp>

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
    typedef std::map<std::string, std::string> UuidRecordMap;
    typedef UuidRecordMap::iterator Udmap_Iter;

    ConfigJsonParser(DB *db);

    void MetadataRegister(const std::string &metadata, MetadataParseFn parser);
    void MetadataClear(const std::string &module);
    bool Receive(const std::string &in_message, bool add_change,
                 IFMapOrigin::Origin origin);

private:
    bool ParseDocument(const rapidjson::Document &document, bool add_change,
        IFMapOrigin::Origin origin, RequestList *req_list,
        IFMapTable::RequestKey *key) const;
    bool ParseNameType(const rapidjson::Document &document,
                       IFMapTable::RequestKey *key) const;
    bool ParseProperties(const rapidjson::Document &document, bool add_change,
        const IFMapTable::RequestKey &key, IFMapOrigin::Origin origin,
        RequestList *req_list) const;
    bool ParseOneProperty(const rapidjson::Value &key_node,
        const rapidjson::Value &value_node, bool add_change,
        const IFMapTable::RequestKey &key, IFMapOrigin::Origin origin,
        RequestList *req_list) const;
    bool ParseLinks(const rapidjson::Document &document, bool add_change,
        const IFMapTable::RequestKey &key, IFMapOrigin::Origin origin,
        RequestList *req_list) const;
    bool ParseRef(const rapidjson::Value &ref_entry, bool add_change,
        IFMapOrigin::Origin origin, const std::string &to_underscore,
        const std::string &neigh_type, const IFMapTable::RequestKey &key,
        RequestList *req_list) const;
    void InsertRequestIntoQ(IFMapOrigin::Origin origin,
        const std::string &neigh_type, const std::string &neigh_name,
        const std::string &metaname, std::auto_ptr<AutogenProperty > pvalue,
        const IFMapTable::RequestKey &key, bool add_change,
        RequestList *req_list) const;
    void CompareOldAndNewDocuments(const rapidjson::Document &document,
        const std::string &in_message, const IFMapTable::RequestKey &key,
        IFMapOrigin::Origin origin, RequestList *req_list);
    IFMapTable::RequestKey *CloneKey(const IFMapTable::RequestKey &src) const;
    void EnqueueListToTables(RequestList *req_list) const;

    DB *db_;
    MetadataParseMap metadata_map_;
    UuidRecordMap uuid_doc_map_;
};

#endif // ctrlplane_config_json_parser_h
