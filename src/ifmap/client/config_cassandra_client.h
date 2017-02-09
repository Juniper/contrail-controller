/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_config_cass_client_h
#define ctrlplane_config_cass_client_h

#include <boost/shared_ptr.hpp>
#include <tbb/spin_rw_mutex.h>

#include "base/queue_task.h"

#include "config_db_client.h"
#include "database/gendb_if.h"
#include "json_adapter_data.h"

class EventManager;
class ConfigJsonParser;
class ConfigClientManager;
struct ConfigDBConnInfo;
class TaskTrigger;

class ObjectProcessReq {
public:
    ObjectProcessReq(std::string oper, std::string obj_type,
                     std::string uuid_str) : oper_(oper),
    obj_type_(obj_type), uuid_str_(uuid_str) {
    }

    std::string oper_;
    std::string obj_type_;
    std::string uuid_str_;

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
    static const int kMaxRequestsToYield = 64;

    typedef boost::scoped_ptr<GenDb::GenDbIf> GenDbIfPtr;

    ConfigCassandraClient(ConfigClientManager *mgr, EventManager *evm,
                          const IFMapConfigOptions &options,
                          ConfigJsonParser *in_parser, int num_workers);
    virtual ~ConfigCassandraClient();
    virtual void InitDatabase();
    std::string UUIDToFQName(const std::string &uuid_str) const;

    void EnqueueUUIDRequest(std::string oper, std::string obj_type,
                                    std::string uuid_str);

    ConfigJsonParser *json_parser() const { return parser_; }

    virtual void FormDeleteRequestList(const string &uuid,
                                  ConfigClientManager::RequestList *req_list,
                                  IFMapTable::RequestKey *key, bool add_change);

    virtual void AddFQNameCache(const string &uuid, const string &obj_name);

    void DeleteFQNameCache(const string &uuid);

    ConfigClientManager *mgr() {
        return mgr_;
    }

    const ConfigClientManager *mgr() const {
        return mgr_;
    }

    void BulkSyncDone(int worker_id);

    virtual void GetConnectionInfo(ConfigDBConnInfo &status) const;

protected:
    struct ConfigCassandraParseContext {
        std::multimap<string, JsonAdapterDataType> list_map_properties;
        std::set<string> updated_list_map_properties;
    };

    virtual bool ReadUuidTableRow(const std::string &obj_type,
                                  const std::string &uuid);
    void ParseUuidTableRowJson(const string &uuid, const string &key,
           const string &value, uint64_t timestamp,
           CassColumnKVVec *cass_data_vec, ConfigCassandraParseContext &context);
    bool ParseRowAndEnqueueToParser(const string &obj_type,
                                    const string &uuid_key,
                                    const GenDb::ColList &col_list);
    virtual int HashUUID(const string &uuid_str) const;

    virtual void HandleObjectDelete(const std::string &obj_type,
                            const std::string &uuid);

    typedef std::pair<string, string> ObjTypeUUIDType;
    typedef std::list<ObjTypeUUIDType> ObjTypeUUIDList;
    void UpdateCache(const std::string &key, const std::string &obj_type,
                     ObjTypeUUIDList &uuid_list);
    bool BulkDataSync();
    bool EnqueueUUIDRequest(const ObjTypeUUIDList &uuid_list);
    virtual std::string GetUUID(const std::string &key,
                                const std::string &obj_type);

private:
    class ConfigReader;

    // UUID to FQName mapping
    typedef std::map<std::string, std::string> FQNameCacheMap;
    struct ObjectProcessRequestType {
        ObjectProcessRequestType(const std::string &in_oper,
                                 const std::string &in_obj_type,
                                 const std::string &in_uuid)
            : oper(in_oper), obj_type(in_obj_type), uuid(in_uuid) {
        }
        std::string oper;
        std::string obj_type;
        std::string uuid;
    };


    struct ObjectProcessRequestCompare {
        bool operator()(const ObjectProcessRequestType *lhs,
                        const ObjectProcessRequestType *rhs) const {
            if (lhs->uuid < rhs->uuid)
                return true;
            return false;
        }
    };

    typedef std::list<ObjectProcessRequestType *> UUIDProcessList;
    typedef std::map<std::string, ObjectProcessRequestType *> UUIDProcessSet;


    typedef std::pair<uint64_t, bool> FieldTimeStampInfo;
    typedef std::map<std::string, FieldTimeStampInfo> FieldDetailMap;

    // Map of UUID to Field mapping
    typedef std::map<string, FieldDetailMap> ObjectCacheMap;

    typedef boost::shared_ptr<WorkQueue<ObjectProcessReq *> > ObjProcessWorkQType;
    void InitRetry();
    virtual bool ParseUuidTableRowResponse(const std::string &uuid,
        const GenDb::ColList &col_list, CassColumnKVVec *cass_data_vec,
        ConfigCassandraParseContext &context);
    void AddUuidEntry(const string &uuid);
    bool ReadAllFqnTableRows();
    bool ParseFQNameRowGetUUIDList(const GenDb::ColList &col_list,
                                   ObjTypeUUIDList &uuid_list);
    bool ConfigReader(int worker_id);
    void AddUUIDToRequestList(int worker_id, const string &oper,
                              const string &obj_type, const string &uuid_str);
    void Enqueue(int worker_id, ObjectProcessReq *req);
    bool RequestHandler(int worker_id, ObjectProcessReq *req);
    bool StoreKeyIfUpdated(int worker_id, const string &uuid, const string &key,
                           const string &value, uint64_t timestamp,
                           ConfigCassandraParseContext &context);

    void MarkCacheDirty(const string &uuid);
    void HandleCassandraConnectionStatus(bool success);
    void UpdatePropertyDeleteToReqList(IFMapTable::RequestKey * key,
       ObjectCacheMap::iterator uuid_iter, const string &lookup_key,
       ConfigClientManager::RequestList *req_list);

    ConfigClientManager *mgr_;
    EventManager *evm_;
    GenDbIfPtr dbif_;
    ConfigJsonParser *parser_;
    int num_workers_;
    std::vector<UUIDProcessList> uuid_read_list_;
    std::vector<UUIDProcessSet> uuid_read_set_;
    std::vector<ObjectCacheMap> object_cache_map_;
    std::vector<boost::shared_ptr<TaskTrigger> > config_readers_;
    mutable tbb::spin_rw_mutex rw_mutex_;
    FQNameCacheMap fq_name_cache_;
    std::vector<ObjProcessWorkQType> obj_process_queue_;
    tbb::atomic<long> bulk_sync_status_;
    tbb::atomic<bool> cassandra_connection_up_;
    tbb::atomic<uint64_t> connection_status_change_at_;
};

#endif // ctrlplane_config_cass_client_h
