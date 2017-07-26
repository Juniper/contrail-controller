/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#ifndef config_cass_client_h
#define config_cass_client_h

#include <boost/ptr_container/ptr_map.hpp>
#include <boost/shared_ptr.hpp>
#include <tbb/spin_rw_mutex.h>

#include <list>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "base/queue_task.h"
#include "base/timer.h"

#include "config_db_client.h"
#include "config_json_parser_base.h"
#include "config_cassandra_client.h"
#include "database/gendb_if.h"
#include "json_adapter_data.h"

class EventManager;
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
    ConfigCassandraPartition(ConfigCassandraClient *client, size_t idx);
    virtual ~ConfigCassandraPartition();

    typedef boost::shared_ptr<WorkQueue<ObjectProcessReq *> >
        ObjProcessWorkQType;

    struct FieldTimeStampInfo {
        uint64_t time_stamp;
        bool refreshed;
    };

    struct cmp_json_key {
        bool operator()(const JsonAdapterDataType &k1,
                         const JsonAdapterDataType &k2) const {
            return k1.key < k2.key;
        }
    };
    typedef std::map<JsonAdapterDataType, FieldTimeStampInfo, cmp_json_key>
                                                             FieldDetailMap;
    class ObjectCacheEntry {
     public:
        ObjectCacheEntry(ConfigCassandraPartition *parent,
                const std::string &obj_type, uint64_t last_read_tstamp)
            : obj_type_(obj_type), retry_count_(0), retry_timer_(NULL),
              last_read_tstamp_(last_read_tstamp), parent_(parent) {
        }

        ~ObjectCacheEntry();

        void EnableCassandraReadRetry(const std::string uuid);
        void DisableCassandraReadRetry(const std::string uuid);
        FieldDetailMap &GetFieldDetailMap() { return field_detail_map_; }
        const FieldDetailMap &GetFieldDetailMap() const {
            return field_detail_map_;
        }
        uint32_t GetRetryCount() const { return retry_count_; }
        void SetLastReadTimeStamp(uint64_t ts) { last_read_tstamp_ = ts; }
        bool IsRetryTimerCreated() const { return (retry_timer_ != NULL); }
        Timer *GetRetryTimer() { return retry_timer_; }

     private:
        friend class ConfigCassandraPartitionTest;

        bool CassReadRetryTimerExpired(const std::string uuid);
        void CassReadRetryTimerErrorHandler();
        std::string obj_type_;
        uint32_t retry_count_;
        Timer *retry_timer_;
        uint64_t last_read_tstamp_;
        FieldDetailMap field_detail_map_;
        ConfigCassandraPartition *parent_;
    };

    static const uint32_t kMaxUUIDRetryTimePowOfTwo = 16;
    typedef boost::ptr_map<std::string, ObjectCacheEntry> ObjectCacheMap;

    ObjProcessWorkQType obj_process_queue() {
        return obj_process_queue_;
    }

    virtual int UUIDRetryTimeInMSec(const ObjectCacheEntry *obj) const;
    ObjectCacheEntry *GetObjectCacheEntry(const std::string &uuid);
    const ObjectCacheEntry *GetObjectCacheEntry(const std::string &uuid) const;
    bool StoreKeyIfUpdated(const std::string &uuid, const std::string &key,
                           const std::string &value, uint64_t timestamp,
                           ConfigCassandraParseContext &context);
    void ListMapPropReviseUpdateList(const std::string &uuid,
                                     ConfigCassandraParseContext &context);
    ObjectCacheEntry *MarkCacheDirty(const std::string &uuid,
            ConfigCassandraParseContext &context);
    void Enqueue(ObjectProcessReq *req);

    void HandleObjectDelete(const string &uuid, bool add_change);

    bool UUIDToObjCacheShow(const std::string &uuid,
                            ConfigDBUUIDCacheEntry &entry) const;
    bool UUIDToObjCacheShow(const std::string &start_uuid,
                            uint32_t num_entries,
                            std::vector<ConfigDBUUIDCacheEntry> &entries) const;
    int GetInstanceId() const { return worker_id_; }

    boost::asio::io_service *ioservice();

    bool IsPropsEmpty(const string &uuid_key, const string &lookup_key);
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

    bool RequestHandler(ObjectProcessReq *req);
    void AddUUIDToRequestList(const std::string &oper,
                      const std::string &obj_type, const std::string &uuid_str);
    bool ConfigReader();
    bool BunchReadReq(const std::set<std::string> &req_list);
    void RemoveObjReqEntries(std::set<std::string> &req_list);
    void RemoveObjReqEntry(std::string &uuid);

    ConfigCassandraClient *client() { return config_client_; }

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
                          const ConfigClientOptions &options,
                          int num_workers);
    virtual ~ConfigCassandraClient();

    virtual void InitDatabase();
    void BulkSyncDone();
    virtual void GetConnectionInfo(ConfigDBConnInfo &status) const;
    virtual uint32_t GetNumReadRequestToBunch() const;
    ConfigClientManager *mgr() { return mgr_; }
    const ConfigClientManager *mgr() const { return mgr_; }
    ConfigCassandraPartition *GetPartition(const std::string &uuid);
    const ConfigCassandraPartition *GetPartition(const std::string &uuid) const;
    const ConfigCassandraPartition *GetPartition(int worker_id) const;

    void EnqueueUUIDRequest(std::string oper, std::string obj_type,
                                    std::string uuid_str);

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
    virtual std::string uuid_str(const std::string &uuid);

    virtual bool IsPropsEmpty(const string &uuid_key,
                               const string &lookup_key); 
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

    virtual void GenerateAndPushJson(
            const string &uuid_key, const string &obj_type,
            const CassColumnKVVec &cass_data_vec, bool add_change);

    void UpdateFQNameCache(const std::string &key, const std::string &obj_type,
                           ObjTypeUUIDList &uuid_list);
    virtual bool BulkDataSync();
    bool EnqueueDBSyncRequest(const ObjTypeUUIDList &uuid_list);
    virtual std::string FetchUUIDFromFQNameEntry(const std::string &key) const;

    virtual int HashUUID(const std::string &uuid_str) const;
    virtual void HandleObjectDelete(const std::string &uuid, bool add_change);
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
    PartitionList &partitions() { return partitions_; }
    virtual void PostShutdown();

    EventManager *event_manager() { return  evm_; }

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

    bool InitRetry();

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
    int num_workers_;
    PartitionList partitions_;
    boost::scoped_ptr<TaskTrigger> fq_name_reader_;
    mutable tbb::spin_rw_mutex rw_mutex_;
    FQNameCacheMap fq_name_cache_;
    tbb::atomic<long> bulk_sync_status_;
    tbb::atomic<bool> cassandra_connection_up_;
    tbb::atomic<uint64_t> connection_status_change_at_;
};

#endif  // config_cass_client_h
