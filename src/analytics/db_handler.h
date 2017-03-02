/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef DB_HANDLER_H_
#define DB_HANDLER_H_

#include <boost/scoped_ptr.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/ptr_container/ptr_map.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/name_generator.hpp>
#include <boost/bind.hpp>

#if __GNUC_PREREQ(4, 6)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-result"
#endif
#include <boost/uuid/uuid_generators.hpp>
#if __GNUC_PREREQ(4, 6)
#pragma GCC diagnostic pop
#endif

#include <boost/tuple/tuple.hpp>

#include "base/parse_object.h"
#include "io/event_manager.h"
#include "base/random_generator.h"
#include <base/watermark.h>
#include "gendb_if.h"
#include "gendb_statistics.h"
#include "sandesh/sandesh.h"
#include "viz_message.h"
#include "uflow_types.h"
#include "viz_constants.h"
#include <database/cassandra/cql/cql_types.h>
#include "configdb_connection.h"
#include "usrdef_counters.h"
#include "options.h"

class Options;
class DbHandler {
public:
    static const int DefaultDbTTL = 0;
    static boost::uuids::uuid seed_uuid;

    typedef enum {
        INVALID = 0,
        UINT64 = 1,
        STRING = 2,
        DOUBLE = 3,
        MAXVAL 
    } VarType;

    struct Var {
        Var() : type(INVALID), str(""), num(0), dbl(0) {}
        Var(const std::string &s) : type(STRING), str(s), num(0), dbl(0) {}
        Var(uint64_t v) : type(UINT64), str(""), num(v), dbl(0) {}
        Var(double d) : type(DOUBLE), str(""), num(0), dbl(d) {}
        VarType type;
        std::string str;
        uint64_t num;
        double dbl;
        bool operator==(const Var &other) const {
            if (type!=other.type) return false;
            switch (type) {
                case STRING:
                    if (str!=other.str) return false;
                    break;
                case UINT64:
                    if (num!=other.num) return false;
                    break;
                case DOUBLE:
                    if (dbl!=other.dbl) return false;
                    break;
                default:
                    break; 
            }
            return true;
        }
        friend inline std::ostream& operator<<(std::ostream& out,
            const Var& value);
    };

    typedef std::map<std::string, std::string> RuleMap;

    typedef std::map<std::string, Var > AttribMap;
    typedef std::multimap<std::string, std::pair<Var, AttribMap> > TagMap;

    DbHandler(EventManager *evm, GenDb::GenDbIf::DbErrorHandler err_handler,
        std::string name,
        const Options::Cassandra &cassandra_options,
        const std::string &zookeeper_server_list,
        bool use_zookeeper,
        bool use_db_write_options,
        const DbWriteOptions &db_write_options,
        const ConfigDBConnection::ApiServerList &api_server_list,
        const VncApiConfig &api_config);
    DbHandler(GenDb::GenDbIf *dbif, const TtlMap& ttl_map);
    virtual ~DbHandler();

    static uint64_t GetTtlInHourFromMap(const TtlMap& ttl_map,
            TtlType::type type);
    static uint64_t GetTtlFromMap(const TtlMap& ttl_map,
            TtlType::type type);
    bool DropMessage(const SandeshHeader &header, const VizMsg *vmsg);
    bool Init(bool initial);
    void UnInit();
    void UnInitUnlocked();
    void GetRuleMap(RuleMap& rulemap);

    virtual void MessageTableInsert(const VizMsg *vmsgp,
        GenDb::GenDbIf::DbAddColumnCb db_cb);
    void ObjectTableInsert(const std::string &table, const std::string &rowkey,
        uint64_t &timestamp, const boost::uuids::uuid& unm,
        const VizMsg *vmsgp, GenDb::GenDbIf::DbAddColumnCb db_cb);
    void StatTableInsert(uint64_t ts, 
            const std::string& statName,
            const std::string& statAttr,
            const TagMap & attribs_tag,
            const AttribMap & attribs_all,
            GenDb::GenDbIf::DbAddColumnCb db_cb);
    bool FlowTableInsert(const pugi::xml_node& parent,
        const SandeshHeader &header, GenDb::GenDbIf::DbAddColumnCb db_cb);
    bool UnderlayFlowSampleInsert(const UFlowData& flow_data,
        uint64_t timestamp, GenDb::GenDbIf::DbAddColumnCb db_cb);

    bool GetStats(uint64_t *queue_count, uint64_t *enqueues) const;

    bool GetStats(std::vector<GenDb::DbTableInfo> *vdbti,
        GenDb::DbErrors *dbe, std::vector<GenDb::DbTableInfo> *vstats_dbti);
    bool GetCumulativeStats(std::vector<GenDb::DbTableInfo> *vdbti,
        GenDb::DbErrors *dbe, std::vector<GenDb::DbTableInfo> *vstats_dbti)
        const;
    void GetSandeshStats(std::string *drop_level,
        std::vector<SandeshStats> *vdropmstats) const;
    bool GetCqlMetrics(cass::cql::Metrics *metrics) const;
    bool GetCqlStats(cass::cql::DbStats *stats) const;
    void SetDbQueueWaterMarkInfo(Sandesh::QueueWaterMarkInfo &wm,
        boost::function<void (void)> defer_undefer_cb);
    void ResetDbQueueWaterMarkInfo();
    std::vector<boost::asio::ip::tcp::endpoint> GetEndpoints() const;
    std::string GetName() const;
    boost::shared_ptr<ConfigDBConnection> GetConfigDBConnection() {
        return cfgdb_connection_;
    }
    bool IsAllWritesDisabled() const;
    bool IsStatisticsWritesDisabled() const;
    bool IsMessagesWritesDisabled() const;
    bool IsMessagesKeywordWritesDisabled() const;
    void DisableAllWrites(bool disable);
    void DisableStatisticsWrites(bool disable);
    void DisableMessagesWrites(bool disable);
    void DisableMessagesKeywordWrites(bool disable);

    // Disk Usage Percentage
    void SetDiskUsagePercentageDropLevel(size_t count,
                                       SandeshLevel::type drop_level);
    SandeshLevel::type GetDiskUsagePercentageDropLevel() const {
        return disk_usage_percentage_drop_level_;
    }
    void SetDiskUsagePercentage(size_t disk_usage_percentage);
    uint32_t GetDiskUsagePercentage()  const { return disk_usage_percentage_; }
    void SetDiskUsagePercentageHighWaterMark(uint32_t disk_usage_percentage,
                                           SandeshLevel::type level);
    void SetDiskUsagePercentageLowWaterMark(uint32_t disk_usage_percentage,
                                          SandeshLevel::type level);
    void ProcessDiskUsagePercentage(uint32_t disk_usage_percentage);

    // Pending Compaction Tasks
    void SetPendingCompactionTasksDropLevel(size_t count,
                                            SandeshLevel::type drop_level);
    SandeshLevel::type GetPendingCompactionTasksDropLevel() const {
        return pending_compaction_tasks_drop_level_;
    }
    void SetPendingCompactionTasks(size_t pending_compaction_tasks);
    uint32_t GetPendingCompactionTasks() const {
        return pending_compaction_tasks_;
    }
    void SetPendingCompactionTasksHighWaterMark(
                                        uint32_t pending_compaction_tasks,
                                        SandeshLevel::type level);
    void SetPendingCompactionTasksLowWaterMark(
                                        uint32_t pending_compaction_tasks,
                                        SandeshLevel::type level);
    void ProcessPendingCompactionTasks(uint32_t pending_compaction_tasks);

private:
    void MessageTableKeywordInsert(const VizMsg *vmsgp,
        GenDb::GenDbIf::DbAddColumnCb db_cb);
    void StatTableInsertTtl(uint64_t ts,
        const std::string& statName,
        const std::string& statAttr,
        const TagMap & attribs_tag,
        const AttribMap & attribs_all, int ttl,
        GenDb::GenDbIf::DbAddColumnCb db_cb);
    void FieldNamesTableInsert(uint64_t timestamp,
        const std::string& table_name, const std::string& field_name,
        const std::string& field_val, int ttl,
        GenDb::GenDbIf::DbAddColumnCb db_cb);
    void MessageTableOnlyInsert(const VizMsg *vmsgp,
        GenDb::GenDbIf::DbAddColumnCb db_cb);
    bool MessageIndexTableInsert(const std::string& cfname,
        const SandeshHeader& header, const std::string& message_type,
        const boost::uuids::uuid& unm, const std::string keyword,
        GenDb::GenDbIf::DbAddColumnCb db_cb);
    bool AllowMessageTableInsert(const SandeshHeader &header);
    bool CreateTables();
    void SetDropLevel(size_t queue_count, SandeshLevel::type level,
        boost::function<void (void)> cb);
    bool Setup();
    bool Initialize();
    bool InitializeInternal();
    bool InitializeInternalLocked();
    bool StatTableWrite(uint32_t t2,
        const std::string& statName, const std::string& statAttr,
        const std::pair<std::string,DbHandler::Var>& ptag,
        const std::pair<std::string,DbHandler::Var>& stag,
        uint32_t t1, const boost::uuids::uuid& unm,
        const std::string& jsonline, int ttl,
        GenDb::GenDbIf::DbAddColumnCb db_cb);
    bool FlowSampleAdd(const pugi::xml_node& flowdata,
        const SandeshHeader& header,
        GenDb::GenDbIf::DbAddColumnCb db_cb);
    uint64_t GetTtl(TtlType::type type) {
        return GetTtlFromMap(ttl_map_, type);
    }
    bool CanRecordDataForT2(uint32_t, std::string);
    bool PollUDCCfg() { if(udc_) udc_->PollCfg(); return true; }
    void PollUDCCfgErrorHandler(std::string err_name, std::string err_message);
    bool InsertIntoDb(std::auto_ptr<GenDb::ColList> col_list,
        GenDb::DbConsistency::type dconsistency,
        GenDb::GenDbIf::DbAddColumnCb db_cb);

    boost::scoped_ptr<GenDb::GenDbIf> dbif_;
    // Random generator for UUIDs
    ThreadSafeUuidGenerator umn_gen_;
    std::string name_;
    std::string col_name_;
    SandeshLevel::type drop_level_;
    VizMsgStatistics dropped_msg_stats_;
    GenDb::DbTableStatistics stable_stats_;
    mutable tbb::mutex smutex_;
    TtlMap ttl_map_;
    static uint32_t field_cache_index_;
    static std::set<std::string> field_cache_set_;
    static tbb::mutex fmutex_;
    std::string tablespace_;
    std::string compaction_strategy_;
    std::string flow_tables_compaction_strategy_;
    UniformInt8RandomGenerator gen_partition_no_;
    std::string zookeeper_server_list_;
    bool use_zookeeper_;
    bool disable_all_writes_;
    bool disable_statistics_writes_;
    bool disable_messages_writes_;
    bool disable_messages_keyword_writes_;
    boost::shared_ptr<ConfigDBConnection> cfgdb_connection_;
    boost::scoped_ptr<UserDefinedCounters> udc_;
    Timer *udc_cfg_poll_timer_;
    static const int kUDCPollInterval = 120 * 1000; // in ms
    bool use_db_write_options_;
    uint32_t disk_usage_percentage_;
    SandeshLevel::type disk_usage_percentage_drop_level_;
    uint32_t pending_compaction_tasks_;
    SandeshLevel::type pending_compaction_tasks_drop_level_;
    mutable tbb::mutex disk_usage_percentage_water_mutex_;
    mutable tbb::mutex pending_compaction_tasks_water_mutex_;
    WaterMarkTuple disk_usage_percentage_watermark_tuple_;
    WaterMarkTuple pending_compaction_tasks_watermark_tuple_;

    friend class DbHandlerTest;

    DISALLOW_COPY_AND_ASSIGN(DbHandler);
};

typedef boost::shared_ptr<DbHandler> DbHandlerPtr;

inline std::ostream& operator<<(std::ostream& out, const DbHandler::Var& value) {
    switch (value.type) {
      case DbHandler::STRING:
	out << value.str;
	break;
      case DbHandler::UINT64:
	out << value.num;
	break;
      case DbHandler::DOUBLE:
	out << value.dbl;
	break;
      default:
	out << "Invalid type: " << value.type;
	break;
    }
    return out;
}

//
// DbHandlerInitializer - Wrapper to perform DbHandler initialization
//
class DbHandlerInitializer {
 public:
    typedef boost::function<void(void)> InitializeDoneCb;
    DbHandlerInitializer(EventManager *evm,
        const std::string &db_name,
        const std::string &timer_task_name, InitializeDoneCb callback,
        const Options::Cassandra &cassandra_options,
        const std::string &zookeeper_server_list,
        bool use_zookeeper,
        const DbWriteOptions &db_write_options,
        const ConfigDBConnection::ApiServerList &api_server_list,
        const VncApiConfig &api_config);
    DbHandlerInitializer(EventManager *evm,
        const std::string &db_name,
        const std::string &timer_task_name, InitializeDoneCb callback,
        DbHandlerPtr db_handler);
    virtual ~DbHandlerInitializer();
    bool Initialize();
    void Shutdown();
    DbHandlerPtr GetDbHandler() const;

 private:
    bool InitTimerExpired();
    void InitTimerErrorHandler(std::string error_name,
        std::string error_message);
    void StartInitTimer();
    void ScheduleInit();

    static const int kInitRetryInterval = 10 * 1000; // in ms
    const std::string db_name_;
    DbHandlerPtr db_handler_;
    InitializeDoneCb callback_;
    Timer *db_init_timer_;
};

/*
 * pugi walker to process flow message
 */
template <typename T>
class FlowLogDataObjectWalker : public pugi::xml_tree_walker {
public:
    FlowLogDataObjectWalker(T &values) :
        values_(values) {
    }
    ~FlowLogDataObjectWalker() {}

    // Callback that is called when traversal begins
    virtual bool begin(pugi::xml_node& node) {
        return true;
    }

    // Callback that is called for each node traversed
    virtual bool for_each(pugi::xml_node& node);

    // Callback that is called when traversal ends
    virtual bool end(pugi::xml_node& node) {
        return true;
    }

private:
    T &values_;
};

#endif /* DB_HANDLER_H_ */
