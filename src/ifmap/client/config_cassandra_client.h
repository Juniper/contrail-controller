/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_config_cass_client_h
#define ctrlplane_config_cass_client_h

#include <boost/shared_ptr.hpp>

#include "base/queue_task.h"

#include "config_db_client.h"
#include "config_json_parser.h"
#include "database/gendb_if.h"
#include "json_adapter_data.h"

class EventManager;
class ConfigJsonParser;
class ConfigClientManager;
class TaskTrigger;

class ObjectProcessReq {
public:
    ObjectProcessReq(std::string uuid_str, std::string obj_type,
                     std::string oper) : uuid_str_(uuid_str),
    obj_type_(obj_type), oper_(oper) {
    }

    std::string uuid_str_;
    std::string obj_type_;
    std::string oper_;

private:
    DISALLOW_COPY_AND_ASSIGN(ObjectProcessReq);
};


/*
 * This class has the functionality to interact with the cassandra servers that
 * store the user configuration.
 */
class ConfigCassandraClient : public ConfigDbClient {
public:
    static const std::string kUuidTableName;
    static const std::string kFqnTableName;
    static const std::string kCassClientTaskId;
    static const std::string kObjectProcessTaskId;
    // wait time before retrying in seconds
    static const int kInitRetryTimeSec = 5;

    typedef boost::scoped_ptr<GenDb::GenDbIf> GenDbIfPtr;

    ConfigCassandraClient(ConfigClientManager *mgr, EventManager *evm,
                          const IFMapConfigOptions &options,
                          ConfigJsonParser *in_parser, int num_workers);
    virtual ~ConfigCassandraClient();
    virtual void InitDatabase();
    bool ReadUuidTableRow(const std::string &uuid);
    std::string UUIDToFQName(const std::string &uuid_str) const;

    std::string GetWrapperFieldName(const std::string &type_name,
                                    const std::string &property_name) const;
    void Enqueue(ObjectProcessReq *req);

    virtual void EnqueueUUIDRequest(std::string uuid_str, std::string obj_type,
                                    std::string oper);

private:
    class ConfigReader;

    typedef std::set<std::string> UUIDReadSet;
    typedef std::list<std::string> UUIDReadList;
    typedef std::map<std::string, std::string> WrapperFieldMap;
    // UUID to FQName mapping
    typedef std::map<std::string, std::string> FQNameCacheMap;
    void InitRetry();
    bool ParseUuidTableRowResponse(const std::string &uuid,
                const GenDb::ColList &col_list, CassColumnKVVec *cass_data_vec);
    void AddUuidEntry(const string &uuid);
    bool BulkDataSync();
    bool ReadAllUuidTableRows();
    bool ParseRowAndEnqueueToParser(const string &uuid_key,
                                    const GenDb::ColList &col_list);
    bool ParseFQNameRowGetUUIDList(const GenDb::ColList &col_list,
                                   std::list<std::string> &uuid_list);
    const ConfigClientManager *mgr() const;
    int HashUUID(const string &uuid_str) const;
    bool ConfigReader(int worker_id);
    void AddUUIDToRequestList(const string &uuid_str);
    bool RequestHandler(ObjectProcessReq *req);

    ConfigClientManager *mgr_;
    EventManager *evm_;
    GenDbIfPtr dbif_;
    ConfigJsonParser *parser_;
    int num_workers_;
    std::vector<UUIDReadList> uuid_read_list_;
    std::vector<UUIDReadSet> uuid_read_set_;
    std::vector<boost::shared_ptr<TaskTrigger> > config_readers_;
    FQNameCacheMap fq_name_cache_;
    WrapperFieldMap wrapper_field_map_;
    boost::scoped_ptr<WorkQueue<ObjectProcessReq *> > obj_process_queue_;
};

#endif // ctrlplane_config_cass_client_h
