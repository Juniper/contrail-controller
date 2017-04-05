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
class ConfigCassandraClient;
struct ConfigCassandraParseContext;
class ConfigDBFQNameCacheEntry;
class ConfigDBUUIDCacheEntry;

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

class ConfigCassandraPartition {
public:
    typedef boost::shared_ptr<WorkQueue<ObjectProcessReq *> >
        ObjProcessWorkQType;
    ConfigCassandraPartition(ConfigCassandraClient *client, size_t idx);
    ~ConfigCassandraPartition();

    ObjProcessWorkQType obj_process_queue() {
        return obj_process_queue_;
    }

    void FormDeleteRequestList(const std::string &uuid,
                              ConfigClientManager::RequestList *req_list,
                              IFMapTable::RequestKey *key, bool add_change);

    bool StoreKeyIfUpdated(const std::string &uuid, const std::string &key,
                           const std::string &value, uint64_t timestamp,
                           ConfigCassandraParseContext &context);

    void MarkCacheDirty(const std::string &uuid);

    void Enqueue(ObjectProcessReq *req);

    bool UUIDToObjCacheShow(const std::string &uuid,
                            ConfigDBUUIDCacheEntry &entry) const;

    bool UUIDToObjCacheShow(const std::string &start_uuid,
                            uint32_t num_entries,
                            std::vector<ConfigDBUUIDCacheEntry> &entries) const;

private:
    friend class ConfigCassandraClient;

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

    typedef std::map<std::string, ObjectProcessRequestType *> UUIDProcessSet;
    typedef std::pair<uint64_t, bool> FieldTimeStampInfo;
    typedef std::map<std::string, FieldTimeStampInfo> FieldDetailMap;
    // Map of UUID to Field mapping
    typedef std::map<std::string, FieldDetailMap> ObjectCacheMap;

    bool RequestHandler(ObjectProcessReq *req);
    void AddUUIDToRequestList(const std::string &oper,
                      const std::string &obj_type, const std::string &uuid_str);
    bool ConfigReader();
    bool BunchReadReq(const std::set<std::string> &req_list);
    void RemoveObjReqEntries(std::set<std::string> &req_list);
    void RemoveObjReqEntry(std::string &uuid);

    void UpdatePropertyDeleteToReqList(IFMapTable::RequestKey * key,
       ObjectCacheMap::iterator uuid_iter, const std::string &lookup_key,
       ConfigClientManager::RequestList *req_list);


    ConfigCassandraClient *client() {
        return config_client_;
    }

    void FillUUIDToObjCacheInfo(const std::string &uuid,
                                ObjectCacheMap::const_iterator uuid_iter,
                                ConfigDBUUIDCacheEntry &entry) const;

    ObjProcessWorkQType obj_process_queue_;
    UUIDProcessSet uuid_read_set_;
    ObjectCacheMap object_cache_map_;
    boost::shared_ptr<TaskTrigger> config_reader_;
    ConfigCassandraClient *config_client_;
    int worker_id_;
};

/*
 * This class has the functionality to interact with the cassandra servers that
 * store the user configuration.
 */
class ConfigCassandraClient : public ConfigDbClient {
public:
    // Cassandra table names
    static const std::string kUuidTableName;
    static const std::string kFqnTableName;

    // Task names
    static const std::string kCassClientTaskId;
    static const std::string kObjectProcessTaskId;

    // wait time before retrying in seconds
    static const uint64_t kInitRetryTimeUSec = 5000000;

    // Number of UUID requests to handle in one config reader task execution
    static const int kMaxRequestsToYield = 512;

    // Number of UUIDs to read in one read request
    static const int kMaxNumUUIDToRead = 64;

    // Number of FQName entries to read in one read request
    static const int kNumFQNameEntriesToRead = 4096;

    typedef boost::scoped_ptr<GenDb::GenDbIf> GenDbIfPtr;
    typedef std::pair<std::string, std::string> ObjTypeFQNPair;
    typedef std::vector<ConfigCassandraPartition *> PartitionList;

    ConfigCassandraClient(ConfigClientManager *mgr, EventManager *evm,
                          const IFMapConfigOptions &options,
                          ConfigJsonParser *in_parser, int num_workers);
    virtual ~ConfigCassandraClient();

    virtual void InitDatabase();
    void BulkSyncDone();
    virtual void GetConnectionInfo(ConfigDBConnInfo &status) const;
    virtual uint32_t GetNumReadRequestToBunch() const;
    ConfigJsonParser *json_parser() const { return parser_; }
    ConfigClientManager *mgr() { return mgr_; }
    const ConfigClientManager *mgr() const { return mgr_; }
    ConfigCassandraPartition *GetPartition(const std::string &uuid);
    const ConfigCassandraPartition *GetPartition(const std::string &uuid) const;
    const ConfigCassandraPartition *GetPartition(int worker_id) const;

    void EnqueueUUIDRequest(std::string oper, std::string obj_type,
                                    std::string uuid_str);
    virtual void FormDeleteRequestList(const std::string &uuid,
                                  ConfigClientManager::RequestList *req_list,
                                  IFMapTable::RequestKey *key, bool add_change);

    // FQ Name Cache
    ObjTypeFQNPair UUIDToFQName(const std::string &uuid_str,
                             bool deleted_ok = true) const;
    virtual void AddFQNameCache(const std::string &uuid,
                                const std::string &obj_name,
                                const std::string &obj_type);
    virtual void InvalidateFQNameCache(const std::string &uuid);
    void PurgeFQNameCache(const std::string &uuid);

    virtual bool UUIDToFQNameShow(const std::string &uuid,
                                  ConfigDBFQNameCacheEntry &entry) const;

    virtual bool UUIDToFQNameShow(const std::string &start_uuid,
                      uint32_t num_entries,
                      std::vector<ConfigDBFQNameCacheEntry> &entries) const;

    virtual bool UUIDToObjCacheShow(int inst_num, const std::string &uuid,
                                  ConfigDBUUIDCacheEntry &entry) const;

    virtual bool UUIDToObjCacheShow(int inst_num, const std::string &start_uuid,
                      uint32_t num_entries,
                      std::vector<ConfigDBUUIDCacheEntry> &entries) const;

protected:
    typedef std::pair<std::string, std::string> ObjTypeUUIDType;
    typedef std::list<ObjTypeUUIDType> ObjTypeUUIDList;

    virtual bool ReadObjUUIDTable(std::set<std::string> *uuid_list);
    bool ProcessObjUUIDTableEntry(const std::string &uuid_key,
                                  const GenDb::ColList &col_list);
    void ParseObjUUIDTableEachColumnBuildContext(const std::string &uuid,
           const std::string &key, const std::string &value,
           uint64_t timestamp, CassColumnKVVec *cass_data_vec,
           ConfigCassandraParseContext &context);

    virtual void ParseContextAndPopulateIFMapTable(const std::string &uuid_key,
                                   const ConfigCassandraParseContext &context,
                                   const CassColumnKVVec &cass_data_vec);


    void UpdateFQNameCache(const std::string &key, const std::string &obj_type,
                           ObjTypeUUIDList &uuid_list);
    virtual bool BulkDataSync();
    bool EnqueueDBSyncRequest(const ObjTypeUUIDList &uuid_list);
    virtual std::string FetchUUIDFromFQNameEntry(const std::string &key) const;
    virtual void EnqueueDelete(const string &uuid,
                      ConfigClientManager::RequestList req_list) const;

    virtual int HashUUID(const std::string &uuid_str) const;
    virtual void HandleObjectDelete(const std::string &uuid);
    virtual std::string GetUUID(const std::string &key) const { return key; }
    virtual bool SkipTimeStampCheckForTypeAndFQName() const { return true; }
    virtual uint32_t GetFQNameEntriesToRead() const {
        return kNumFQNameEntriesToRead;
    }
    virtual const int GetMaxRequestsToYield() const {
        return kMaxRequestsToYield;
    }
    virtual const uint64_t GetInitRetryTimeUSec() const {
        return kInitRetryTimeUSec;
    }
    int num_workers() const { return num_workers_; }
    PartitionList &partitions() {
        return partitions_;
    }

private:
    friend class ConfigCassandraPartition;

    // UUID to FQName mapping
    struct FQNameCacheType {
        FQNameCacheType(std::string in_obj_type, std::string in_obj_name)
            : obj_type(in_obj_type), obj_name(in_obj_name), deleted(false) {
        }
        std::string obj_type;
        std::string obj_name;
        bool deleted;
    };
    typedef std::map<std::string, FQNameCacheType> FQNameCacheMap;

    void InitRetry();

    virtual void ParseObjUUIDTableEntry(const std::string &uuid,
        const GenDb::ColList &col_list, CassColumnKVVec *cass_data_vec,
        ConfigCassandraParseContext &context);

    bool FQNameReader();
    bool ParseFQNameRowGetUUIDList(const std::string &obj_type,
               const GenDb::ColList &col_list, ObjTypeUUIDList &uuid_list,
               std::string *last_column);

    void HandleCassandraConnectionStatus(bool success);
    void FillFQNameCacheInfo(const std::string &uuid,
      FQNameCacheMap::const_iterator it, ConfigDBFQNameCacheEntry &entry) const;

    ConfigClientManager *mgr_;
    EventManager *evm_;
    GenDbIfPtr dbif_;
    ConfigJsonParser *parser_;
    int num_workers_;
    PartitionList partitions_;
    boost::scoped_ptr<TaskTrigger> fq_name_reader_;
    mutable tbb::spin_rw_mutex rw_mutex_;
    FQNameCacheMap fq_name_cache_;
    tbb::atomic<long> bulk_sync_status_;
    tbb::atomic<bool> cassandra_connection_up_;
    tbb::atomic<uint64_t> connection_status_change_at_;
};

#endif // ctrlplane_config_cass_client_h
