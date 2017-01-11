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
    std::string UUIDToFQName(const std::string &uuid_str) const;

    virtual void EnqueueUUIDRequest(std::string uuid_str, std::string obj_type,
                                    std::string oper);

    ConfigJsonParser *json_parser() const { return parser_; }

    virtual void FormDeleteRequestList(const string &uuid,
                                  ConfigClientManager::RequestList *req_list,
                                  IFMapTable::RequestKey *key, bool add_change);

    virtual void AddFQNameCache(const string &uuid, const string &obj_name);

    void DeleteFQNameCache(const string &uuid);

    const ConfigClientManager *mgr() const {
        return mgr_;
    }

protected:
    virtual bool ReadUuidTableRow(const std::string &obj_type,
                                  const std::string &uuid);
    void ParseUuidTableRowJson(const string &uuid, const string &key,
                               const string &value,
                               CassColumnKVVec *cass_data_vec);
    bool ParseRowAndEnqueueToParser(const string &obj_type,
                                    const string &uuid_key,
                                    const GenDb::ColList &col_list);

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

    typedef std::pair<string, string> ObjTypeUUIDType;
    typedef std::list<ObjTypeUUIDType> ObjTypeUUIDList;

    typedef std::pair<uint32_t, bool> FieldTimeStampInfo;
    typedef std::map<std::string, FieldTimeStampInfo> FieldDetailMap;

    // Map of UUID to Field mapping
    typedef std::map<string, FieldDetailMap> ObjectCacheMap;

    typedef boost::shared_ptr<WorkQueue<ObjectProcessReq *> > ObjProcessWorkQType;
    void InitRetry();
    virtual bool ParseUuidTableRowResponse(const std::string &uuid,
        const GenDb::ColList &col_list, CassColumnKVVec *cass_data_vec);
    void AddUuidEntry(const string &uuid);
    bool BulkDataSync();
    bool ReadAllUuidTableRows();
    bool ParseFQNameRowGetUUIDList(const GenDb::ColList &col_list,
                                   ObjTypeUUIDList &uuid_list);
    int HashUUID(const string &uuid_str) const;
    bool ConfigReader(int worker_id);
    void AddUUIDToRequestList(const string &oper, const string &obj_type,
                              const string &uuid_str);
    void Enqueue(int worker_id, ObjectProcessReq *req);
    bool RequestHandler(ObjectProcessReq *req);
    bool StoreKeyIfUpdated(int worker_id, const string &uuid, const string &key,
                           uint32_t timestamp);
    void HandleObjectDelete(const std::string &obj_type,
                            const std::string &uuid);

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
};

#endif // ctrlplane_config_cass_client_h
