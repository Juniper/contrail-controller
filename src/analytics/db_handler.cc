/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <exception>
#include <string>
#include <boost/bind.hpp>
#include <boost/foreach.hpp>
#include <boost/assign/list_of.hpp>
#include <boost/asio/ip/host_name.hpp>
#include <boost/uuid/name_generator.hpp>
#include <boost/system/error_code.hpp>

#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include <base/string_util.h>
#include <base/logging.h>
#include <io/event_manager.h>
#include <base/connection_info.h>
#include <sandesh/sandesh_message_builder.h>
#include <sandesh/protocol/TXMLProtocol.h>
#include <database/cassandra/cql/cql_if.h>
#include <zookeeper/zookeeper_client.h>

#include "sandesh/common/flow_constants.h"
#include "sandesh/common/flow_types.h"
#include "viz_constants.h"
#include "vizd_table_desc.h"
#include "viz_collector.h"
#include "collector.h"
#include "db_handler.h"
#include "parser_util.h"
#include "db_handler_impl.h"
#include "viz_sandesh.h"

#define DB_LOG(_Level, _Msg)                                                   \
    do {                                                                       \
        if (LoggingDisabled()) break;                                          \
        log4cplus::Logger _Xlogger = log4cplus::Logger::getRoot();             \
        if (_Xlogger.isEnabledFor(log4cplus::_Level##_LOG_LEVEL)) {            \
            log4cplus::tostringstream _Xbuf;                                   \
            _Xbuf << name_ << ": " << __func__ << ": " << _Msg;                \
            _Xlogger.forcedLog(log4cplus::_Level##_LOG_LEVEL,                  \
                               _Xbuf.str());                                   \
        }                                                                      \
    } while (false)

using std::pair;
using std::string;
using boost::system::error_code;
using namespace pugi;
using namespace contrail::sandesh::protocol;
using process::ConnectionState;
using process::ConnectionType;
using process::ConnectionStatus;
using namespace boost::system;

uint32_t DbHandler::field_cache_index_ = 0;
std::set<std::string> DbHandler::field_cache_set_;
tbb::mutex DbHandler::fmutex_;

DbHandler::DbHandler(EventManager *evm,
        GenDb::GenDbIf::DbErrorHandler err_handler,
        std::string name,
        const Options::Cassandra &cassandra_options,
        bool use_db_write_options,
        const DbWriteOptions &db_write_options,
        ConfigClientCollector *config_client) :
    dbif_(new cass::cql::CqlIf(evm, cassandra_options.cassandra_ips_,
        cassandra_options.cassandra_ports_[0], cassandra_options.user_,
        cassandra_options.password_, true)),
    name_(name),
    drop_level_(SandeshLevel::INVALID),
    ttl_map_(cassandra_options.ttlmap_),
    compaction_strategy_(cassandra_options.compaction_strategy_),
    flow_tables_compaction_strategy_(
        cassandra_options.flow_tables_compaction_strategy_),
    gen_partition_no_((uint8_t)g_viz_constants.PARTITION_MIN,
        (uint8_t)g_viz_constants.PARTITION_MAX),
    disable_all_writes_(cassandra_options.disable_all_db_writes_),
    disable_statistics_writes_(cassandra_options.disable_db_stats_writes_),
    disable_messages_writes_(cassandra_options.disable_db_messages_writes_),
    config_client_(config_client),
    use_db_write_options_(use_db_write_options) {
    udc_.reset(new UserDefinedCounters());
    if (config_client) {
        config_client->RegisterConfigReceive("udc",
                             boost::bind(&DbHandler::ReceiveConfig, this, _1, _2));
    }
    error_code error;
    col_name_ = boost::asio::ip::host_name(error);

    if (cassandra_options.cluster_id_.empty()) {
        tablespace_ = g_viz_constants.COLLECTOR_KEYSPACE_CQL;
    } else {
        tablespace_ = g_viz_constants.COLLECTOR_KEYSPACE_CQL + '_' + cassandra_options.cluster_id_;
    }

    if (use_db_write_options_) {
        // Set disk-usage watermark defaults
        SetDiskUsagePercentageHighWaterMark(
            db_write_options.get_disk_usage_percentage_high_watermark0(),
            db_write_options.get_high_watermark0_message_severity_level());
        SetDiskUsagePercentageLowWaterMark(
            db_write_options.get_disk_usage_percentage_low_watermark0(),
            db_write_options.get_low_watermark0_message_severity_level());
        SetDiskUsagePercentageHighWaterMark(
            db_write_options.get_disk_usage_percentage_high_watermark1(),
            db_write_options.get_high_watermark1_message_severity_level());
        SetDiskUsagePercentageLowWaterMark(
            db_write_options.get_disk_usage_percentage_low_watermark1(),
            db_write_options.get_low_watermark1_message_severity_level());
        SetDiskUsagePercentageHighWaterMark(
            db_write_options.get_disk_usage_percentage_high_watermark2(),
            db_write_options.get_high_watermark2_message_severity_level());
        SetDiskUsagePercentageLowWaterMark(
            db_write_options.get_disk_usage_percentage_low_watermark2(),
            db_write_options.get_low_watermark2_message_severity_level());

        // Set cassandra pending tasks watermark defaults
        SetPendingCompactionTasksHighWaterMark(
            db_write_options.get_pending_compaction_tasks_high_watermark0(),
            db_write_options.get_high_watermark0_message_severity_level());
        SetPendingCompactionTasksLowWaterMark(
            db_write_options.get_pending_compaction_tasks_low_watermark0(),
            db_write_options.get_low_watermark0_message_severity_level());
        SetPendingCompactionTasksHighWaterMark(
            db_write_options.get_pending_compaction_tasks_high_watermark1(),
            db_write_options.get_high_watermark1_message_severity_level());
        SetPendingCompactionTasksLowWaterMark(
            db_write_options.get_pending_compaction_tasks_low_watermark1(),
            db_write_options.get_low_watermark1_message_severity_level());
        SetPendingCompactionTasksHighWaterMark(
            db_write_options.get_pending_compaction_tasks_high_watermark2(),
            db_write_options.get_high_watermark2_message_severity_level());
        SetPendingCompactionTasksLowWaterMark(
            db_write_options.get_pending_compaction_tasks_low_watermark2(),
            db_write_options.get_low_watermark2_message_severity_level());

        // Initialize drop-levels to lowest severity level.
        SetDiskUsagePercentageDropLevel(0,
            db_write_options.get_low_watermark2_message_severity_level());
        SetPendingCompactionTasksDropLevel(0,
            db_write_options.get_low_watermark2_message_severity_level());
    }
}


DbHandler::DbHandler(GenDb::GenDbIf *dbif, const TtlMap& ttl_map) :
    dbif_(dbif),
    ttl_map_(ttl_map),
    gen_partition_no_((uint8_t)g_viz_constants.PARTITION_MIN,
        (uint8_t)g_viz_constants.PARTITION_MAX),
    disable_all_writes_(false),
    disable_statistics_writes_(false),
    disable_messages_writes_(false),
    use_db_write_options_(false) {
    udc_.reset(new UserDefinedCounters());
}

DbHandler::~DbHandler() {
}

uint64_t DbHandler::GetTtlInHourFromMap(const TtlMap& ttl_map,
        TtlType::type type) {
    TtlMap::const_iterator it = ttl_map.find(type);
    if (it != ttl_map.end()) {
        return it->second;
    } else {
        return 0;
    }
}

uint64_t DbHandler::GetTtlFromMap(const TtlMap& ttl_map,
        TtlType::type type) {
    TtlMap::const_iterator it = ttl_map.find(type);
    if (it != ttl_map.end()) {
        return it->second*3600;
    } else {
        return 0;
    }
}

std::string DbHandler::GetName() const {
    return name_;
}

std::vector<boost::asio::ip::tcp::endpoint> DbHandler::GetEndpoints() const {
    return dbif_->Db_GetEndpoints();
}

void DbHandler::SetDiskUsagePercentageDropLevel(size_t count,
                                    SandeshLevel::type drop_level) {
    disk_usage_percentage_drop_level_ = drop_level;
}

void DbHandler::SetDiskUsagePercentage(size_t disk_usage_percentage) {
    disk_usage_percentage_ = disk_usage_percentage;
}

void DbHandler::SetDiskUsagePercentageHighWaterMark(
                                        uint32_t disk_usage_percentage,
                                        SandeshLevel::type level) {
    WaterMarkInfo wm = WaterMarkInfo(disk_usage_percentage,
        boost::bind(&DbHandler::SetDiskUsagePercentageDropLevel,
                    this, _1, level));
    disk_usage_percentage_watermark_tuple_.SetHighWaterMark(wm);
}

void DbHandler::SetDiskUsagePercentageLowWaterMark(
                                        uint32_t disk_usage_percentage,
                                        SandeshLevel::type level) {
    WaterMarkInfo wm = WaterMarkInfo(disk_usage_percentage,
        boost::bind(&DbHandler::SetDiskUsagePercentageDropLevel,
                    this, _1, level));
    disk_usage_percentage_watermark_tuple_.SetLowWaterMark(wm);
}

void DbHandler::ProcessDiskUsagePercentage(uint32_t disk_usage_percentage) {
    tbb::mutex::scoped_lock lock(disk_usage_percentage_water_mutex_);
    disk_usage_percentage_watermark_tuple_.ProcessWaterMarks(
                                        disk_usage_percentage,
                                        DbHandler::disk_usage_percentage_);
}

void DbHandler::SetPendingCompactionTasksDropLevel(size_t count,
                                    SandeshLevel::type drop_level) {
    pending_compaction_tasks_drop_level_ = drop_level;
}

void DbHandler::SetPendingCompactionTasks(size_t pending_compaction_tasks) {
    pending_compaction_tasks_ = pending_compaction_tasks;
}

void DbHandler::SetPendingCompactionTasksHighWaterMark(
                                        uint32_t pending_compaction_tasks,
                                        SandeshLevel::type level) {
    WaterMarkInfo wm = WaterMarkInfo(pending_compaction_tasks,
        boost::bind(&DbHandler::SetPendingCompactionTasksDropLevel, this,
                    _1, level));
    pending_compaction_tasks_watermark_tuple_.SetHighWaterMark(wm);
}

void DbHandler::SetPendingCompactionTasksLowWaterMark(
                                        uint32_t pending_compaction_tasks,
                                        SandeshLevel::type level) {
    WaterMarkInfo wm = WaterMarkInfo(pending_compaction_tasks,
        boost::bind(&DbHandler::SetPendingCompactionTasksDropLevel, this,
                    _1, level));
    pending_compaction_tasks_watermark_tuple_.SetLowWaterMark(wm);
}

void DbHandler::ProcessPendingCompactionTasks(
                                    uint32_t pending_compaction_tasks) {
    tbb::mutex::scoped_lock lock(pending_compaction_tasks_water_mutex_);
    pending_compaction_tasks_watermark_tuple_.ProcessWaterMarks(
                                    pending_compaction_tasks,
                                    DbHandler::pending_compaction_tasks_);
}

bool DbHandler::DropMessage(const SandeshHeader &header,
    const VizMsg *vmsg) {
    SandeshType::type stype(header.get_Type());
    // If Flow message, drop it
    if (stype == SandeshType::FLOW) {
        tbb::mutex::scoped_lock lock(smutex_);
        dropped_msg_stats_.Update(vmsg);
        return true;
    }
    if (!(stype == SandeshType::SYSTEM || stype == SandeshType::OBJECT ||
          stype == SandeshType::UVE ||
          stype == SandeshType::SESSION)) {
        return false;
    }
    // First check again the queue watermark drop level
    SandeshLevel::type slevel(static_cast<SandeshLevel::type>(
        header.get_Level()));
    if (slevel >= drop_level_) {
        tbb::mutex::scoped_lock lock(smutex_);
        dropped_msg_stats_.Update(vmsg);
        return true;
    }
    // Next check against the disk usage and pending compaction tasks
    // drop levels
    bool disk_usage_percentage_drop = false;
    bool pending_compaction_tasks_drop = false;
    if (use_db_write_options_) {
        SandeshLevel::type disk_usage_percentage_drop_level =
                                        GetDiskUsagePercentageDropLevel();
        SandeshLevel::type pending_compaction_tasks_drop_level =
                                        GetPendingCompactionTasksDropLevel();
        if (slevel >= disk_usage_percentage_drop_level) {
            disk_usage_percentage_drop = true;
        }
        if (slevel >= pending_compaction_tasks_drop_level) {
            pending_compaction_tasks_drop = true;
        }
    }

    bool drop(disk_usage_percentage_drop || pending_compaction_tasks_drop);
    if (drop) {
        tbb::mutex::scoped_lock lock(smutex_);
        dropped_msg_stats_.Update(vmsg);
    }
    return drop;
}
 
void DbHandler::SetDropLevel(size_t queue_count, SandeshLevel::type level,
    boost::function<void (void)> cb) {
    if (drop_level_ != level) {
        DB_LOG(INFO, "DB DROP LEVEL: [" << 
            Sandesh::LevelToString(drop_level_) << "] -> [" <<
            Sandesh::LevelToString(level) << "], DB QUEUE COUNT: " << 
            queue_count);
        drop_level_ = level;
    }
    // Always invoke the callback
    if (!cb.empty()) {
        cb();
    }
}

bool DbHandler::CreateTables() {
    for (std::vector<GenDb::NewCf>::const_iterator it = vizd_tables.begin();
            it != vizd_tables.end(); it++) {
        if (!dbif_->Db_AddColumnfamily(*it, compaction_strategy_)) {
            DB_LOG(ERROR, it->cfname_ << " FAILED");
            return false;
        }

        // find schema for it->cfname_
        // find all columns with index_type field set
        table_schema cfschema;
        cfschema = g_viz_constants._VIZD_TABLE_SCHEMA.find(it->cfname_)->second;
        BOOST_FOREACH (schema_column column, cfschema.columns) {
            if (column.index_mode != GenDb::ColIndexMode::NONE) {
                if (!dbif_->Db_CreateIndex(it->cfname_, column.name, "",
                                           column.index_mode)) {
                    DB_LOG(ERROR, it->cfname_ << ": CreateIndex FAILED for "
                           << column.name);
                    return false;
                }
            }
        }
    }

    for (std::vector<GenDb::NewCf>::const_iterator it = vizd_stat_tables.begin();
            it != vizd_stat_tables.end(); it++) {
        if (!dbif_->Db_AddColumnfamily(*it, compaction_strategy_)) {
            DB_LOG(ERROR, it->cfname_ << " FAILED");
            return false;
        }
    }

    for (std::vector<GenDb::NewCf>::const_iterator it = vizd_session_tables.begin();
            it != vizd_session_tables.end(); it++) {
        if (!dbif_->Db_AddColumnfamily(*it, flow_tables_compaction_strategy_)) {
            DB_LOG(ERROR, it->cfname_ << " FAILED");
            return false;
        }

        // will be un-commented once we start using cassandra-2.10 for systemless tests
        table_schema cfschema = g_viz_constants._VIZD_SESSION_TABLE_SCHEMA
                                                    .find(it->cfname_)->second;
        BOOST_FOREACH(schema_column column, cfschema.columns) {
            if (column.index_mode != GenDb::ColIndexMode::NONE) {
                if (!dbif_->Db_CreateIndex(it->cfname_, column.name, "",
                                           column.index_mode)) {
                    DB_LOG(ERROR, it->cfname_ << ": CreateIndex FAILED for "
                           << column.name);
                    return false;
                }
            }
        }
    }

    if (!dbif_->Db_SetTablespace(tablespace_)) {
        DB_LOG(ERROR, "Set KEYSPACE: " << tablespace_ << " FAILED");
        return false;
    }
    GenDb::ColList col_list;
    std::string cfname = g_viz_constants.SYSTEM_OBJECT_TABLE;
    GenDb::DbDataValueVec key;
    key.push_back(g_viz_constants.SYSTEM_OBJECT_ANALYTICS);

    bool init_done = false;
    if (dbif_->Db_GetRow(&col_list, cfname, key,
        GenDb::DbConsistency::LOCAL_ONE)) {
        for (GenDb::NewColVec::iterator it = col_list.columns_.begin();
                it != col_list.columns_.end(); it++) {
            std::string col_name;
            try {
                col_name = boost::get<std::string>(it->name->at(0));
            } catch (boost::bad_get& ex) {
                DB_LOG(ERROR, cfname << ": Column Name Get FAILED");
            }

            if (col_name == g_viz_constants.SYSTEM_OBJECT_START_TIME) {
                init_done = true;
            }
        }
    }

    if (!init_done) {
        std::auto_ptr<GenDb::ColList> col_list(new GenDb::ColList);
        col_list->cfname_ = g_viz_constants.SYSTEM_OBJECT_TABLE;
        // Rowkey
        GenDb::DbDataValueVec& rowkey = col_list->rowkey_;
        rowkey.reserve(1);
        rowkey.push_back(g_viz_constants.SYSTEM_OBJECT_ANALYTICS);
        // Columns
        GenDb::NewColVec& columns = col_list->columns_;
        columns.reserve(4);

        uint64_t current_tm = UTCTimestampUsec();

        GenDb::NewCol *col(new GenDb::NewCol(
            g_viz_constants.SYSTEM_OBJECT_START_TIME, current_tm, 0));
        columns.push_back(col);

        GenDb::NewCol *flow_col(new GenDb::NewCol(
            g_viz_constants.SYSTEM_OBJECT_FLOW_START_TIME, current_tm, 0));
        columns.push_back(flow_col);

        GenDb::NewCol *msg_col(new GenDb::NewCol(
            g_viz_constants.SYSTEM_OBJECT_MSG_START_TIME, current_tm, 0));
        columns.push_back(msg_col);

        GenDb::NewCol *stat_col(new GenDb::NewCol(
            g_viz_constants.SYSTEM_OBJECT_STAT_START_TIME, current_tm, 0));
        columns.push_back(stat_col);

        if (!dbif_->Db_AddColumnSync(col_list,
            GenDb::DbConsistency::LOCAL_ONE)) {
            DB_LOG(ERROR, g_viz_constants.SYSTEM_OBJECT_TABLE <<
                ": Start Time Column Add FAILED");
            return false;
        }
    }

    /*
     * add ttls to cassandra to be retrieved by other daemons
     */
    {
        std::auto_ptr<GenDb::ColList> col_list(new GenDb::ColList);
        col_list->cfname_ = g_viz_constants.SYSTEM_OBJECT_TABLE;
        // Rowkey
        GenDb::DbDataValueVec& rowkey = col_list->rowkey_;
        rowkey.reserve(1);
        rowkey.push_back(g_viz_constants.SYSTEM_OBJECT_ANALYTICS);
        // Columns
        GenDb::NewColVec& columns = col_list->columns_;
        columns.reserve(4);

        GenDb::NewCol *col(new GenDb::NewCol(
            g_viz_constants.SYSTEM_OBJECT_FLOW_DATA_TTL, (uint64_t)DbHandler::GetTtlInHourFromMap(ttl_map_, TtlType::FLOWDATA_TTL), 0));
        columns.push_back(col);

        GenDb::NewCol *flow_col(new GenDb::NewCol(
            g_viz_constants.SYSTEM_OBJECT_STATS_DATA_TTL, (uint64_t)DbHandler::GetTtlInHourFromMap(ttl_map_, TtlType::STATSDATA_TTL), 0));
        columns.push_back(flow_col);

        GenDb::NewCol *msg_col(new GenDb::NewCol(
            g_viz_constants.SYSTEM_OBJECT_CONFIG_AUDIT_TTL, (uint64_t)DbHandler::GetTtlInHourFromMap(ttl_map_, TtlType::CONFIGAUDIT_TTL), 0));
        columns.push_back(msg_col);

        GenDb::NewCol *stat_col(new GenDb::NewCol(
            g_viz_constants.SYSTEM_OBJECT_GLOBAL_DATA_TTL, (uint64_t)DbHandler::GetTtlInHourFromMap(ttl_map_, TtlType::GLOBAL_TTL), 0));
        columns.push_back(stat_col);

        if (!dbif_->Db_AddColumnSync(col_list,
            GenDb::DbConsistency::LOCAL_ONE)) {
            DB_LOG(ERROR, g_viz_constants.SYSTEM_OBJECT_TABLE <<
                ": TTL Column Add FAILED");
            return false;
        }
    }

    return true;
}

void DbHandler::UnInit() {
    dbif_->Db_Uninit();
    dbif_->Db_SetInitDone(false);
}

bool DbHandler::Init(bool initial) {
    SetDropLevel(0, SandeshLevel::INVALID, NULL);
    if (initial) {
        return Initialize();
    } else {
        return Setup();
    }
}

bool DbHandler::Initialize() {
    DB_LOG(DEBUG, "Initializing..");

    /* init of vizd table structures */
    init_vizd_tables();

    if (!dbif_->Db_Init()) {
        DB_LOG(ERROR, "Connection to DB FAILED");
        return false;
    }

    if (!dbif_->Db_AddSetTablespace(tablespace_, "2")) {
        DB_LOG(ERROR, "Create/Set KEYSPACE: " << tablespace_ << " FAILED");
        return false;
    }

    if (!CreateTables()) {
        DB_LOG(ERROR, "CreateTables FAILED");
        return false;
    }

    dbif_->Db_SetInitDone(true);
    DB_LOG(DEBUG, "Initializing Done");

    return true;
}

bool DbHandler::Setup() {
    DB_LOG(DEBUG, "Setup..");
    if (!dbif_->Db_Init()) {
        DB_LOG(ERROR, "Connection to DB FAILED");
        return false;
    }
    if (!dbif_->Db_SetTablespace(tablespace_)) {
        DB_LOG(ERROR, "Set KEYSPACE: " << tablespace_ << " FAILED");
        return false;
    }   
    for (std::vector<GenDb::NewCf>::const_iterator it = vizd_tables.begin();
            it != vizd_tables.end(); it++) {
        if (!dbif_->Db_UseColumnfamily(*it)) {
            DB_LOG(ERROR, it->cfname_ << 
                   ": Db_UseColumnfamily FAILED");
            return false;
        }
    }
    for (std::vector<GenDb::NewCf>::const_iterator it = vizd_stat_tables.begin();
            it != vizd_stat_tables.end(); it++) {
        if (!dbif_->Db_UseColumnfamily(*it)) {
            DB_LOG(ERROR, it->cfname_ << ": Db_UseColumnfamily FAILED");
            return false;
        }
    }
    dbif_->Db_SetInitDone(true);
    DB_LOG(DEBUG, "Setup Done");
    return true;
}

bool DbHandler::IsAllWritesDisabled() const {
    return disable_all_writes_;
}

bool DbHandler::IsStatisticsWritesDisabled() const {
    return disable_statistics_writes_;
}

bool DbHandler::IsMessagesWritesDisabled() const {
    return disable_messages_writes_;
}

void DbHandler::DisableAllWrites(bool disable) {
    disable_all_writes_ = disable;
}

void DbHandler::DisableStatisticsWrites(bool disable) {
    disable_statistics_writes_ = disable;
}

void DbHandler::DisableMessagesWrites(bool disable) {
    disable_messages_writes_ = disable;
}

void DbHandler::SetDbQueueWaterMarkInfo(Sandesh::QueueWaterMarkInfo &wm,
    boost::function<void (void)> defer_undefer_cb) {
    dbif_->Db_SetQueueWaterMark(boost::get<2>(wm),
        boost::get<0>(wm),
        boost::bind(&DbHandler::SetDropLevel, this, _1, boost::get<1>(wm),
        defer_undefer_cb));
}

void DbHandler::ResetDbQueueWaterMarkInfo() {
    dbif_->Db_ResetQueueWaterMarks();
}

void DbHandler::GetSandeshStats(std::string *drop_level,
    std::vector<SandeshStats> *vdropmstats) const {
    *drop_level = Sandesh::LevelToString(drop_level_);
    if (vdropmstats) {
        tbb::mutex::scoped_lock lock(smutex_);
        dropped_msg_stats_.Get(vdropmstats);
    }
}

bool DbHandler::GetStats(uint64_t *queue_count, uint64_t *enqueues) const {
    return dbif_->Db_GetQueueStats(queue_count, enqueues);
}

bool DbHandler::GetStats(std::vector<GenDb::DbTableInfo> *vdbti,
    GenDb::DbErrors *dbe, std::vector<GenDb::DbTableInfo> *vstats_dbti) {
    {
        tbb::mutex::scoped_lock lock(smutex_);
        stable_stats_.GetDiffs(vstats_dbti);
    }
    return dbif_->Db_GetStats(vdbti, dbe);
}

bool DbHandler::GetCumulativeStats(std::vector<GenDb::DbTableInfo> *vdbti,
    GenDb::DbErrors *dbe, std::vector<GenDb::DbTableInfo> *vstats_dbti) const {
    {
        tbb::mutex::scoped_lock lock(smutex_);
        stable_stats_.GetCumulative(vstats_dbti);
    }
    return dbif_->Db_GetCumulativeStats(vdbti, dbe);
}

bool DbHandler::GetCqlMetrics(cass::cql::Metrics *metrics) const {
    cass::cql::CqlIf *cql_if(dynamic_cast<cass::cql::CqlIf *>(dbif_.get()));
    if (cql_if == NULL) {
        return false;
    }
    return cql_if->Db_GetCqlMetrics(metrics);
}

bool DbHandler::GetCqlStats(cass::cql::DbStats *stats) const {
    cass::cql::CqlIf *cql_if(dynamic_cast<cass::cql::CqlIf *>(dbif_.get()));
    if (cql_if == NULL) {
        return false;
    }
    return cql_if->Db_GetCqlStats(stats);
}

bool DbHandler::InsertIntoDb(std::auto_ptr<GenDb::ColList> col_list,
    GenDb::DbConsistency::type dconsistency,
    GenDb::GenDbIf::DbAddColumnCb db_cb) {
    if (IsAllWritesDisabled()) {
        return true;
    }
    return dbif_->Db_AddColumn(col_list, dconsistency, db_cb);
}

bool DbHandler::AllowMessageTableInsert(const SandeshHeader &header) {
    return !IsMessagesWritesDisabled() && !IsAllWritesDisabled() &&
        (header.get_Type() != SandeshType::FLOW) &&
        (header.get_Type() != SandeshType::SESSION);
}

void DbHandler::MessageTableOnlyInsert(const VizMsg *vmsgp,
    const DbHandler::ObjectNamesVec &object_names,
    GenDb::GenDbIf::DbAddColumnCb db_cb) {
    const SandeshHeader &header(vmsgp->msg->GetHeader());
    const std::string &message_type(vmsgp->msg->GetMessageType());
    uint64_t timestamp;
    int ttl;
    if (message_type == "VncApiConfigLog") {
        ttl = GetTtl(TtlType::CONFIGAUDIT_TTL);
    } else {
        ttl = GetTtl(TtlType::GLOBAL_TTL);
    }
    std::auto_ptr<GenDb::ColList> col_list(new GenDb::ColList);
    col_list->cfname_ = g_viz_constants.COLLECTOR_GLOBAL_TABLE;
    timestamp = header.get_Timestamp();
    uint32_t T2(timestamp >> g_viz_constants.RowTimeInBits);
    uint32_t T1(timestamp & g_viz_constants.RowTimeInMask);

    // Rowkey
    GenDb::DbDataValueVec& rowkey = col_list->rowkey_;
    rowkey.reserve(2);
    rowkey.push_back(T2);
    rowkey.push_back(gen_partition_no_());

    // Columns
    GenDb::DbDataValueVec *col_name(new GenDb::DbDataValueVec());
    col_name->reserve(20);
    col_name->push_back(T1);
    col_name->push_back(vmsgp->unm);

    // Prepend T2: to secondary index columns
    col_name->push_back(PrependT2(T2, header.get_Source()));
    col_name->push_back(PrependT2(T2, message_type));
    col_name->push_back(PrependT2(T2, header.get_Module()));

    // if number of entries in object_names > MSG_TABLE_MAX_OBJECTS_PER_MSG
    // - print error
    // - increment counter + export on introspect - TODO
    // - let first 6 entries go through
    unsigned int count = 0;
    BOOST_FOREACH(const std::string &object_name, object_names) {
        count++;
        if (count > g_viz_constants.MSG_TABLE_MAX_OBJECTS_PER_MSG) {
            DB_LOG(ERROR, "Number of object_names in message > " <<
                   g_viz_constants.MSG_TABLE_MAX_OBJECTS_PER_MSG <<
                   ". Ignoring extra object_names");
            break;
        }
        col_name->push_back(PrependT2(T2, object_name));
    }
    // Set the value as BLANK for remaining entries if
    // object_names.size() < MSG_TABLE_MAX_OBJECTS_PER_MSG
    for (int i = object_names.size();
         i < g_viz_constants.MSG_TABLE_MAX_OBJECTS_PER_MSG; i++) {
        col_name->push_back(GenDb::DbDataValue());
    }
    if (header.__isset.IPAddress) {
        boost::system::error_code ec;
        IpAddress ipaddr(IpAddress::from_string(header.get_IPAddress(), ec));
        if (ec ) {
            LOG(ERROR, "MessageTable: INVALID IP address:"
                << header.get_IPAddress());
            col_name->push_back(GenDb::DbDataValue());
        } else {
            col_name->push_back(ipaddr);
        }
    } else {
        col_name->push_back(GenDb::DbDataValue());
    }
    if (header.__isset.Pid) {
        col_name->push_back((uint32_t)header.get_Pid());
    } else {
        col_name->push_back(GenDb::DbDataValue());
    }
    col_name->push_back(header.get_Category());
    col_name->push_back((uint32_t)header.get_Level());
    col_name->push_back(header.get_NodeType());
    col_name->push_back(header.get_InstanceId());
    col_name->push_back((uint32_t)header.get_SequenceNum());
    col_name->push_back((uint8_t)header.get_Type());
    GenDb::DbDataValueVec *col_value(new GenDb::DbDataValueVec(1,
        vmsgp->msg->ExtractMessage()));
    GenDb::NewCol *col(new GenDb::NewCol(col_name, col_value, ttl));
    GenDb::NewColVec& columns = col_list->columns_;
    columns.reserve(1);
    columns.push_back(col);
    if (!InsertIntoDb(col_list, GenDb::DbConsistency::LOCAL_ONE, db_cb)) {
        DB_LOG(ERROR, "Addition of message: " << message_type <<
                ", message UUID: " << vmsgp->unm << " COLUMN FAILED");
        return;
    }
}

void DbHandler::MessageTableInsert(const VizMsg *vmsgp,
    const DbHandler::ObjectNamesVec &object_names,
    GenDb::GenDbIf::DbAddColumnCb db_cb) {
    const SandeshHeader &header(vmsgp->msg->GetHeader());
    const std::string &message_type(vmsgp->msg->GetMessageType());

    if (!AllowMessageTableInsert(header))
        return;

    MessageTableOnlyInsert(vmsgp, object_names, db_cb);


    const SandeshType::type &stype(header.get_Type());

    /*
     * Insert the message types,module_id in the stat table
     * Construct the atttributes,attrib_tags beofore inserting
     * to the StatTableInsert
     */
    if ((stype == SandeshType::SYSLOG) || (stype == SandeshType::SYSTEM)) {
        //Insert only if sandesh type is a SYSTEM LOG or SYSLOG
        //Insert into the FieldNames stats table entries for Messagetype and Module ID
        int ttl = GetTtl(TtlType::GLOBAL_TTL);
        FieldNamesTableInsert(header.get_Timestamp(),
            g_viz_constants.MESSAGE_TABLE,
            ":Messagetype", message_type, ttl, db_cb);
        FieldNamesTableInsert(header.get_Timestamp(),
            g_viz_constants.MESSAGE_TABLE,
            ":ModuleId", header.get_Module(), ttl, db_cb);
        FieldNamesTableInsert(header.get_Timestamp(),
            g_viz_constants.MESSAGE_TABLE,
            ":Source", header.get_Source(), ttl, db_cb);
        if (!header.get_Category().empty()) {
            FieldNamesTableInsert(header.get_Timestamp(),
                g_viz_constants.MESSAGE_TABLE,
                ":Category", header.get_Category(), ttl, db_cb);
        }
    }
}

/*
 * This function takes field name and field value as arguments and inserts
 * into the FieldNames stats table
 */
void DbHandler::FieldNamesTableInsert(uint64_t timestamp,
    const std::string& table_prefix, 
    const std::string& field_name, const std::string& field_val, int ttl,
    GenDb::GenDbIf::DbAddColumnCb db_cb) {
    /*
     * Insert the message types in the stat table
     * Construct the atttributes,attrib_tags before inserting
     * to the StatTableInsert
     */
    uint32_t temp_u32 = timestamp >> g_viz_constants.RowTimeInBits;
    std::string table_name(table_prefix);
    table_name.append(field_name);

    /* Check if fieldname and value were already seen in this T2;
       2 caches are mainted one for  last T2 and T2-1.
       We only need to record them if they have NOT been seen yet */
    bool record = false;
    std::string fc_entry(table_name);
    fc_entry.append(":");
    fc_entry.append(field_val);
    {
        tbb::mutex::scoped_lock lock(fmutex_);
        record = CanRecordDataForT2(temp_u32, fc_entry);
    }

    if (!record) return;

    DbHandler::TagMap tmap;
    DbHandler::AttribMap amap;
    DbHandler::Var pv;
    DbHandler::AttribMap attribs;
    pv = table_name;
    tmap.insert(make_pair("name", make_pair(pv, amap)));
    attribs.insert(make_pair(string("name"), pv));
    string sattrname("fields.value");
    pv = string(field_val);
    attribs.insert(make_pair(sattrname,pv));

    //pv = string(header.get_Source());
    // Put the name of the collector, not the message source.
    // Using the message source will make queries slower
    pv = string(col_name_);
    tmap.insert(make_pair("Source",make_pair(pv,amap))); 
    attribs.insert(make_pair(string("Source"),pv));

    StatTableInsertTtl(timestamp, "FieldNames","fields", tmap, attribs, ttl,
        db_cb);
}

/*
 * This function checks if the data can be recorded or not
 * for the given t2. If t2 corresponding to the data is
 * older than field_cache_old_t2_ and field_cache_t2_
 * it is ignored
 */
bool DbHandler::CanRecordDataForT2(uint32_t temp_u32, std::string fc_entry) {
    bool record = false;

    uint32_t cacheindex = temp_u32 >> g_viz_constants.CacheTimeInAdditionalBits;
    if (cacheindex > field_cache_index_) {
            field_cache_index_ = cacheindex;
            field_cache_set_.clear();
            field_cache_set_.insert(fc_entry);
            record = true;
    } else if (cacheindex == field_cache_index_) {
        if (field_cache_set_.find(fc_entry) ==
            field_cache_set_.end()) {
            field_cache_set_.insert(fc_entry);
            record = true;
        }
    }
    return record;
}

void DbHandler::GetRuleMap(RuleMap& rulemap) {
}

/*
 * insert an entry into an ObjectTrace table
 * key is T2
 * column is
 *  name: <key>:T1 (value in timestamp)
 *  value: uuid (of the corresponding global message)
 */
void DbHandler::ObjectTableInsert(const std::string &table, const std::string &objectkey_str,
        uint64_t &timestamp, const boost::uuids::uuid& unm, const VizMsg *vmsgp,
        GenDb::GenDbIf::DbAddColumnCb db_cb) {
    if (IsMessagesWritesDisabled() || IsAllWritesDisabled()) {
        return;
    }
    uint32_t T2(timestamp >> g_viz_constants.RowTimeInBits);
    uint32_t T1(timestamp & g_viz_constants.RowTimeInMask);
    const std::string &message_type(vmsgp->msg->GetMessageType());
    int ttl;
    if (message_type == "VncApiConfigLog") {
        ttl = GetTtl(TtlType::CONFIGAUDIT_TTL);
    } else {
        ttl = GetTtl(TtlType::GLOBAL_TTL);
    }

    {
        std::auto_ptr<GenDb::ColList> col_list(new GenDb::ColList);
        col_list->cfname_ = g_viz_constants.OBJECT_VALUE_TABLE;
        GenDb::DbDataValueVec& rowkey = col_list->rowkey_;
        rowkey.reserve(2);
        rowkey.push_back(T2);
        rowkey.push_back(table);
        GenDb::DbDataValueVec *col_name(new GenDb::DbDataValueVec(1, T1));
        GenDb::DbDataValueVec *col_value(new GenDb::DbDataValueVec(1, objectkey_str));
        GenDb::NewCol *col(new GenDb::NewCol(col_name, col_value, ttl));
        GenDb::NewColVec& columns = col_list->columns_;
        columns.reserve(1);
        columns.push_back(col);
        if (!InsertIntoDb(col_list, GenDb::DbConsistency::LOCAL_ONE, db_cb)) {
            DB_LOG(ERROR, "Addition of " << objectkey_str <<
                    ", message UUID " << unm << " " << table << " into table "
                    << g_viz_constants.OBJECT_VALUE_TABLE << " FAILED");
            return;
        }

        /*
         * Inserting into the stat table
         */
        const SandeshHeader &header(vmsgp->msg->GetHeader());
        const std::string &message_type(vmsgp->msg->GetMessageType());
        //Insert into the FieldNames stats table entries for Messagetype and Module ID
        FieldNamesTableInsert(timestamp,
                table, ":ObjectId", objectkey_str, ttl, db_cb);
        FieldNamesTableInsert(timestamp,
                table, ":Messagetype", message_type, ttl, db_cb);
        FieldNamesTableInsert(timestamp,
                table, ":ModuleId", header.get_Module(), ttl, db_cb);
        FieldNamesTableInsert(timestamp,
                table, ":Source", header.get_Source(), ttl, db_cb);

        FieldNamesTableInsert(timestamp,
                "OBJECT:", table, table, ttl, db_cb);
    }
}

bool DbHandler::StatTableWrite(uint32_t t2,
        const std::string& statName, const std::string& statAttr,
        const std::pair<std::string,DbHandler::Var>& ptag,
        const std::pair<std::string,DbHandler::Var>& stag,
        uint32_t t1, const boost::uuids::uuid& unm,
        const std::string& jsonline, int ttl,
        GenDb::GenDbIf::DbAddColumnCb db_cb) {

    uint8_t part = 0;
    string cfname;
    const DbHandler::Var& pv = ptag.second;
    const DbHandler::Var& sv = stag.second;
    GenDb::DbDataValue pg,sg;

    bool bad_suffix = false;
    switch (pv.type) {
        case DbHandler::STRING : {
                pg = pv.str;
                if (sv.type==DbHandler::STRING) {
                    cfname = g_viz_constants.STATS_TABLE_BY_STR_STR_TAG;
                    sg = sv.str;
                } else if (sv.type==DbHandler::UINT64) {
                    cfname = g_viz_constants.STATS_TABLE_BY_STR_U64_TAG;
                    sg = sv.num;
                } else if (sv.type==DbHandler::INVALID) {
                    cfname = g_viz_constants.STATS_TABLE_BY_STR_TAG;
                } else {
                    bad_suffix = true;
                }
            }
            break;
        case DbHandler::UINT64 : {
                pg = pv.num;
                if (sv.type==DbHandler::STRING) {
                    cfname = g_viz_constants.STATS_TABLE_BY_U64_STR_TAG;
                    sg = sv.str;
                } else if (sv.type==DbHandler::UINT64) {
                    cfname = g_viz_constants.STATS_TABLE_BY_U64_U64_TAG;
                    sg = sv.num;
                } else if (sv.type==DbHandler::INVALID) {
                    cfname = g_viz_constants.STATS_TABLE_BY_U64_TAG;
                } else {
                    bad_suffix = true;
                }
            }
            break;
        case DbHandler::DOUBLE : {
                pg = pv.dbl;
                if (sv.type==DbHandler::INVALID) {
                    cfname = g_viz_constants.STATS_TABLE_BY_DBL_TAG;
                } else {
                    bad_suffix = true;
                }
            }
            break;
        default:
            tbb::mutex::scoped_lock lock(smutex_);
            stable_stats_.Update(statName + ":" + statAttr, true, true,
                false, 1);
            DB_LOG(ERROR, "Bad Prefix Tag " << statName <<
                    ", " << statAttr <<  " tag " << ptag.first <<
                    ":" << stag.first << " jsonline " << jsonline);
            return false;
    }
    if (bad_suffix) {
        tbb::mutex::scoped_lock lock(smutex_);
        stable_stats_.Update(statName + ":" + statAttr, true, true, false, 1);
        DB_LOG(ERROR, "Bad Suffix Tag " << statName <<
                ", " << statAttr <<  " tag " << ptag.first <<
                ":" << stag.first << " jsonline " << jsonline);
        return false;
    }
    std::auto_ptr<GenDb::ColList> col_list(new GenDb::ColList);
    col_list->cfname_ = cfname;
    
    GenDb::DbDataValueVec& rowkey = col_list->rowkey_;
    if (sv.type==DbHandler::INVALID) {
        rowkey.reserve(5);
        rowkey.push_back(t2);
        rowkey.push_back(part);
        rowkey.push_back(statName);
        rowkey.push_back(statAttr);
        rowkey.push_back(ptag.first);
    } else {
        rowkey.reserve(6);
        rowkey.push_back(t2);
        rowkey.push_back(part);
        rowkey.push_back(statName);
        rowkey.push_back(statAttr);
        rowkey.push_back(ptag.first);
        rowkey.push_back(stag.first);
    }

    GenDb::NewColVec& columns = col_list->columns_;

    GenDb::DbDataValueVec *col_name(new GenDb::DbDataValueVec);
    if (sv.type==DbHandler::INVALID) {
        col_name->reserve(3);
        col_name->push_back(pg);
        col_name->push_back(t1);
        col_name->push_back(unm);
    } else {
        col_name->reserve(4);
        col_name->push_back(pg);
        col_name->push_back(sg);
        col_name->push_back(t1);
        col_name->push_back(unm);
    }

    GenDb::DbDataValueVec *col_value(new GenDb::DbDataValueVec(1, jsonline));
    GenDb::NewCol *col(new GenDb::NewCol(col_name, col_value, ttl));
    columns.push_back(col);

    if (!InsertIntoDb(col_list, GenDb::DbConsistency::LOCAL_ONE, db_cb)) {
        DB_LOG(ERROR, "Addition of " << statName <<
                ", " << statAttr <<  " tag " << ptag.first <<
                ":" << stag.first << " into table " <<
                cfname <<" FAILED");
        tbb::mutex::scoped_lock lock(smutex_);
        stable_stats_.Update(statName + ":" + statAttr, true, true, false, 1);
        return false;
    } else {
        tbb::mutex::scoped_lock lock(smutex_);
        stable_stats_.Update(statName + ":" + statAttr, true, false, false, 1);
        return true;
    }
}

void
DbHandler::StatTableInsert(uint64_t ts, 
        const std::string& statName,
        const std::string& statAttr,
        const TagMap & attribs_tag,
        const AttribMap & attribs,
        GenDb::GenDbIf::DbAddColumnCb db_cb) {
    if (IsAllWritesDisabled() || IsStatisticsWritesDisabled()) {
        return;
    }
    int ttl = GetTtl(TtlType::STATSDATA_TTL);
    StatTableInsertTtl(ts, statName, statAttr, attribs_tag, attribs, ttl,
        db_cb);
}

// This function writes Stats samples to the DB.
void
DbHandler::StatTableInsertTtl(uint64_t ts, 
        const std::string& statName,
        const std::string& statAttr,
        const TagMap & attribs_tag,
        const AttribMap & attribs, int ttl,
        GenDb::GenDbIf::DbAddColumnCb db_cb) {

    uint64_t temp_u64 = ts;
    uint32_t temp_u32 = temp_u64 >> g_viz_constants.RowTimeInBits;
    boost::uuids::uuid unm;
    if (statName.compare("FieldNames") != 0) {
         unm = umn_gen_();
    }

    // This is very primitive JSON encoding.
    // Should replace with rapidJson at some point.

    // Encoding of all attribs

    contrail_rapidjson::Document dd;
    dd.SetObject();

    AttribMap attribs_buf;
    for (AttribMap::const_iterator it = attribs.begin();
            it != attribs.end(); it++) {
        switch (it->second.type) {
            case STRING: {
                    contrail_rapidjson::Value val(contrail_rapidjson::kStringType);
                    std::string nm = it->first + std::string("|s");
                    pair<AttribMap::iterator,bool> rt = 
                        attribs_buf.insert(make_pair(nm, it->second));
                    val.SetString(it->second.str.c_str(), dd.GetAllocator());
                    contrail_rapidjson::Value vk;
                    dd.AddMember(vk.SetString(rt.first->first.c_str(),
                                 dd.GetAllocator()), val, dd.GetAllocator());
                    string field_name = it->first;
                     if (field_name.compare("fields.value") == 0) {
                         if (statName.compare("FieldNames") == 0) {
                             //Make uuid a fn of the field.values
                             boost::uuids::name_generator gen(DbHandler::seed_uuid);
                             unm = gen(it->second.str.c_str());
                         }
                     }
                }
                break;
            case UINT64: {
                    contrail_rapidjson::Value val(contrail_rapidjson::kNumberType);
                    std::string nm = it->first + std::string("|n");
                    pair<AttribMap::iterator,bool> rt = 
                        attribs_buf.insert(make_pair(nm, it->second));
                    val.SetUint64(it->second.num);
                    contrail_rapidjson::Value vk;
                    dd.AddMember(vk.SetString(rt.first->first.c_str(),
                                 dd.GetAllocator()), val, dd.GetAllocator());
                }
                break;
            case DOUBLE: {
                    contrail_rapidjson::Value val(contrail_rapidjson::kNumberType);
                    std::string nm = it->first + std::string("|d");
                    pair<AttribMap::iterator,bool> rt = 
                        attribs_buf.insert(make_pair(nm, it->second));
                    val.SetDouble(it->second.dbl);
                    contrail_rapidjson::Value vk;
                    dd.AddMember(vk.SetString(rt.first->first.c_str(),
                                 dd.GetAllocator()), val, dd.GetAllocator());
                }
                break;                
            default:
                continue;
        }
    }

    contrail_rapidjson::StringBuffer sb;
    contrail_rapidjson::Writer<contrail_rapidjson::StringBuffer> writer(sb);
    dd.Accept(writer);
    string jsonline(sb.GetString());

    uint32_t t1;
    t1 = (uint32_t)(temp_u64& g_viz_constants.RowTimeInMask);

    if ( statName.compare("FieldNames") != 0) {
        std::string tablename(std::string("StatTable.") + statName + "." + statAttr);
        FieldNamesTableInsert(ts,
                    "STAT:", tablename, tablename, ttl, db_cb);
    }

    for (TagMap::const_iterator it = attribs_tag.begin();
            it != attribs_tag.end(); it++) {

        pair<string,DbHandler::Var> ptag;
        ptag.first = it->first;
        ptag.second = it->second.first;

        /* Record in the fieldNames table if we have a string tag,
           and if we are not recording a fieldNames stats entry itself */
        if ((ptag.second.type == DbHandler::STRING) &&
                (statName.compare("FieldNames") != 0)) {
            FieldNamesTableInsert(ts, std::string("StatTable.") +
                    statName + "." + statAttr,
                    std::string(":") + ptag.first, ptag.second.str, ttl,
                    db_cb);
        }

        if (it->second.second.empty()) {
            pair<string,DbHandler::Var> stag;
            StatTableWrite(temp_u32, statName, statAttr,
                                ptag, stag, t1, unm, jsonline, ttl, db_cb);
        } else {
            for (AttribMap::const_iterator jt = it->second.second.begin();
                    jt != it->second.second.end(); jt++) {
                StatTableWrite(temp_u32, statName, statAttr,
                                    ptag, *jt, t1, unm, jsonline, ttl, db_cb);
            }
        }

    }

}

static const std::vector<FlowRecordFields::type> FlowRecordTableColumns =
    boost::assign::list_of
    (FlowRecordFields::FLOWREC_VROUTER)
    (FlowRecordFields::FLOWREC_DIRECTION_ING)
    (FlowRecordFields::FLOWREC_SOURCEVN)
    (FlowRecordFields::FLOWREC_SOURCEIP)
    (FlowRecordFields::FLOWREC_DESTVN)
    (FlowRecordFields::FLOWREC_DESTIP)
    (FlowRecordFields::FLOWREC_PROTOCOL)
    (FlowRecordFields::FLOWREC_SPORT)
    (FlowRecordFields::FLOWREC_DPORT)
    (FlowRecordFields::FLOWREC_TOS)
    (FlowRecordFields::FLOWREC_TCP_FLAGS)
    (FlowRecordFields::FLOWREC_VM)
    (FlowRecordFields::FLOWREC_INPUT_INTERFACE)
    (FlowRecordFields::FLOWREC_OUTPUT_INTERFACE)
    (FlowRecordFields::FLOWREC_MPLS_LABEL)
    (FlowRecordFields::FLOWREC_REVERSE_UUID)
    (FlowRecordFields::FLOWREC_SETUP_TIME)
    (FlowRecordFields::FLOWREC_TEARDOWN_TIME)
    (FlowRecordFields::FLOWREC_MIN_INTERARRIVAL)
    (FlowRecordFields::FLOWREC_MAX_INTERARRIVAL)
    (FlowRecordFields::FLOWREC_MEAN_INTERARRIVAL)
    (FlowRecordFields::FLOWREC_STDDEV_INTERARRIVAL)
    (FlowRecordFields::FLOWREC_BYTES)
    (FlowRecordFields::FLOWREC_PACKETS)
    (FlowRecordFields::FLOWREC_DATA_SAMPLE)
    (FlowRecordFields::FLOWREC_ACTION)
    (FlowRecordFields::FLOWREC_SG_RULE_UUID)
    (FlowRecordFields::FLOWREC_NW_ACE_UUID)
    (FlowRecordFields::FLOWREC_VROUTER_IP)
    (FlowRecordFields::FLOWREC_OTHER_VROUTER_IP)
    (FlowRecordFields::FLOWREC_UNDERLAY_PROTO)
    (FlowRecordFields::FLOWREC_UNDERLAY_SPORT)
    (FlowRecordFields::FLOWREC_VMI)
    (FlowRecordFields::FLOWREC_DROP_REASON);

boost::uuids::uuid DbHandler::seed_uuid = StringToUuid(std::string("ffffffff-ffff-ffff-ffff-ffffffffffff"));

static void PopulateFlowFieldValues(FlowRecordFields::type ftype,
    const GenDb::DbDataValue &db_value, int ttl, FlowFieldValuesCb fncb) {
    if (ftype == FlowRecordFields::FLOWREC_VROUTER ||
        ftype == FlowRecordFields::FLOWREC_SOURCEVN ||
        ftype == FlowRecordFields::FLOWREC_DESTVN) {
        assert(db_value.which() == GenDb::DB_VALUE_STRING);
        std::string sval(boost::get<std::string>(db_value));
        if (!fncb.empty()) {
            fncb(std::string(":") + g_viz_constants.FlowRecordNames[ftype],
                sval, ttl);
        }
    }
}
 
static void PopulateFlowRecordTableColumns(
    const std::vector<FlowRecordFields::type> &frvt,
    FlowValueArray &fvalues, GenDb::NewColVec& columns, const TtlMap& ttl_map,
    FlowFieldValuesCb fncb) {
    int ttl = DbHandler::GetTtlFromMap(ttl_map, TtlType::FLOWDATA_TTL);
    columns.reserve(frvt.size());
    BOOST_FOREACH(const FlowRecordFields::type &fvt, frvt) {
        const GenDb::DbDataValue &db_value(fvalues[fvt]);
        if (db_value.which() != GenDb::DB_VALUE_BLANK) {
            GenDb::NewCol *col(new GenDb::NewCol(
                g_viz_constants.FlowRecordNames[fvt], db_value, ttl));
            columns.push_back(col);
            PopulateFlowFieldValues(fvt, db_value, ttl, fncb);
        }
    }
}

// FLOW_UUID
static void PopulateFlowRecordTableRowKey(
    FlowValueArray &fvalues, GenDb::DbDataValueVec &rkey) {
    rkey.reserve(1);
    GenDb::DbDataValue &flowu(fvalues[FlowRecordFields::FLOWREC_FLOWUUID]);
    assert(flowu.which() != GenDb::DB_VALUE_BLANK);
    rkey.push_back(flowu);
}

static bool PopulateFlowRecordTable(FlowValueArray &fvalues,
    DbInsertCb db_insert_cb, const TtlMap& ttl_map,
    FlowFieldValuesCb fncb) {
    std::auto_ptr<GenDb::ColList> colList(new GenDb::ColList);
    colList->cfname_ = g_viz_constants.FLOW_TABLE;
    PopulateFlowRecordTableRowKey(fvalues, colList->rowkey_);
    PopulateFlowRecordTableColumns(FlowRecordTableColumns, fvalues,
        colList->columns_, ttl_map, fncb);
    return db_insert_cb(colList);
}

static const std::vector<FlowRecordFields::type> FlowIndexTableColumnValues =
    boost::assign::list_of
    (FlowRecordFields::FLOWREC_DIFF_BYTES)
    (FlowRecordFields::FLOWREC_DIFF_PACKETS)
    (FlowRecordFields::FLOWREC_SHORT_FLOW)
    (FlowRecordFields::FLOWREC_FLOWUUID)
    (FlowRecordFields::FLOWREC_VROUTER)
    (FlowRecordFields::FLOWREC_SOURCEVN)
    (FlowRecordFields::FLOWREC_DESTVN)
    (FlowRecordFields::FLOWREC_SOURCEIP)
    (FlowRecordFields::FLOWREC_DESTIP)
    (FlowRecordFields::FLOWREC_PROTOCOL)
    (FlowRecordFields::FLOWREC_SPORT)
    (FlowRecordFields::FLOWREC_DPORT)
    (FlowRecordFields::FLOWREC_JSON);

enum FlowIndexTableType {
    FLOW_INDEX_TABLE_MIN,
    FLOW_INDEX_TABLE_SVN_SIP = FLOW_INDEX_TABLE_MIN,
    FLOW_INDEX_TABLE_DVN_DIP,
    FLOW_INDEX_TABLE_PROTOCOL_SPORT,
    FLOW_INDEX_TABLE_PROTOCOL_DPORT,
    FLOW_INDEX_TABLE_VROUTER,
    FLOW_INDEX_TABLE_MAX_PLUS_1,
};

static const std::string& FlowIndexTable2String(FlowIndexTableType ttype) {
    switch (ttype) {
    case FLOW_INDEX_TABLE_SVN_SIP:
        return g_viz_constants.FLOW_TABLE_SVN_SIP;
    case FLOW_INDEX_TABLE_DVN_DIP:
        return g_viz_constants.FLOW_TABLE_DVN_DIP;
    case FLOW_INDEX_TABLE_PROTOCOL_SPORT:
        return g_viz_constants.FLOW_TABLE_PROT_SP;
    case FLOW_INDEX_TABLE_PROTOCOL_DPORT:
        return g_viz_constants.FLOW_TABLE_PROT_DP;
    case FLOW_INDEX_TABLE_VROUTER:
        return g_viz_constants.FLOW_TABLE_VROUTER;
    default:
        return g_viz_constants.FLOW_TABLE_INVALID;
    }
}

class FlowValueJsonPrinter : public boost::static_visitor<> {
 public:
    FlowValueJsonPrinter() :
        name_() {
        dd_.SetObject();
    }
    void operator()(const boost::uuids::uuid &tuuid) {
        std::string tuuid_s(to_string(tuuid));
        contrail_rapidjson::Value val(contrail_rapidjson::kStringType);
        val.SetString(tuuid_s.c_str(), dd_.GetAllocator());
        contrail_rapidjson::Value vk;
        dd_.AddMember(vk.SetString(name_.c_str(), dd_.GetAllocator()), val,
                      dd_.GetAllocator());
    }
    void operator()(const std::string &tstring) {
        contrail_rapidjson::Value val(contrail_rapidjson::kStringType);
        val.SetString(tstring.c_str(), dd_.GetAllocator());
        contrail_rapidjson::Value vk;
        dd_.AddMember(vk.SetString(name_.c_str(), dd_.GetAllocator()), val,
                      dd_.GetAllocator());
    }
    void operator()(const uint8_t &t8) {
        contrail_rapidjson::Value val(contrail_rapidjson::kNumberType);
        val.SetUint(t8);
        contrail_rapidjson::Value vk;
        dd_.AddMember(vk.SetString(name_.c_str(), dd_.GetAllocator()), val,
                      dd_.GetAllocator());
    }
    void operator()(const uint16_t &t16) {
        contrail_rapidjson::Value val(contrail_rapidjson::kNumberType);
        val.SetUint(t16);
        contrail_rapidjson::Value vk;
        dd_.AddMember(vk.SetString(name_.c_str(), dd_.GetAllocator()), val,
                      dd_.GetAllocator());
    }
    void operator()(const uint32_t &tu32) {
        contrail_rapidjson::Value val(contrail_rapidjson::kNumberType);
        val.SetUint(tu32);
        contrail_rapidjson::Value vk;
        dd_.AddMember(vk.SetString(name_.c_str(), dd_.GetAllocator()), val,
                      dd_.GetAllocator());
    }
    void operator()(const uint64_t &tu64) {
        contrail_rapidjson::Value val(contrail_rapidjson::kNumberType);
        val.SetUint64(tu64);
        contrail_rapidjson::Value vk;
        dd_.AddMember(vk.SetString(name_.c_str(), dd_.GetAllocator()), val,
                      dd_.GetAllocator());
    }
    void operator()(const double &tdouble) {
        contrail_rapidjson::Value val(contrail_rapidjson::kNumberType);
        val.SetDouble(tdouble);
        contrail_rapidjson::Value vk;
        dd_.AddMember(vk.SetString(name_.c_str(), dd_.GetAllocator()), val,
                      dd_.GetAllocator());
    }
    void operator()(const IpAddress &tipaddr) {
        contrail_rapidjson::Value val(contrail_rapidjson::kStringType);
        val.SetString(tipaddr.to_string().c_str(), dd_.GetAllocator());
        contrail_rapidjson::Value vk;
        dd_.AddMember(vk.SetString(name_.c_str(), dd_.GetAllocator()), val,
                      dd_.GetAllocator());
    }
    void operator()(const GenDb::Blob &tblob) {
        contrail_rapidjson::Value val(contrail_rapidjson::kStringType);
        val.SetString(reinterpret_cast<const char *>(tblob.data()),
            tblob.size(), dd_.GetAllocator());
        contrail_rapidjson::Value vk;
        dd_.AddMember(vk.SetString(name_.c_str(), dd_.GetAllocator()), val,
                      dd_.GetAllocator());
    }
    void operator()(const boost::blank &tblank) {
    }
    void SetName(const std::string &name) {
        name_ = name;
    }
    std::string GetJson() {
        contrail_rapidjson::StringBuffer sb;
        contrail_rapidjson::Writer<contrail_rapidjson::StringBuffer> writer(sb);
        dd_.Accept(writer);
        return sb.GetString();
    }
 private:
    contrail_rapidjson::Document dd_;
    std::string name_;
};

void PopulateFlowIndexTableColumnValues(
    const std::vector<FlowRecordFields::type> &frvt,
    const FlowValueArray &fvalues, GenDb::DbDataValueVec *cvalues, int ttl,
    FlowFieldValuesCb fncb) {
    cvalues->reserve(1);
    FlowValueJsonPrinter jprinter;
    BOOST_FOREACH(const FlowRecordFields::type &fvt, frvt) {
        const GenDb::DbDataValue &db_value(fvalues[fvt]);
        if (db_value.which() != GenDb::DB_VALUE_BLANK) {
            jprinter.SetName(g_viz_constants.FlowRecordNames[fvt]);
            boost::apply_visitor(jprinter, db_value);
            PopulateFlowFieldValues(fvt, db_value, ttl, fncb);
        }
    }
    std::string jsonline(jprinter.GetJson());
    cvalues->push_back(jsonline);
}

// T2, Partition No, Direction
static void PopulateFlowIndexTableRowKey(const FlowValueArray &fvalues,
    const uint32_t &T2, const uint8_t &partition_no,
    GenDb::DbDataValueVec *rkey) {
    rkey->reserve(3);
    rkey->push_back(T2);
    rkey->push_back(partition_no);
    rkey->push_back(fvalues[FlowRecordFields::FLOWREC_DIRECTION_ING]);
}

// SVN/DVN/Protocol, SIP/DIP/SPORT/DPORT, T1, FLOW_UUID
static void PopulateFlowIndexTableColumnNames(FlowIndexTableType ftype,
    const FlowValueArray &fvalues, const uint32_t &T1,
    GenDb::DbDataValueVec *cnames) {
    cnames->reserve(4);
    switch(ftype) {
    case FLOW_INDEX_TABLE_SVN_SIP:
        cnames->push_back(fvalues[FlowRecordFields::FLOWREC_SOURCEVN]);
        cnames->push_back(fvalues[FlowRecordFields::FLOWREC_SOURCEIP]);
        break;
    case FLOW_INDEX_TABLE_DVN_DIP:
        cnames->push_back(fvalues[FlowRecordFields::FLOWREC_DESTVN]);
        cnames->push_back(fvalues[FlowRecordFields::FLOWREC_DESTIP]);
        break;
    case FLOW_INDEX_TABLE_PROTOCOL_SPORT:
        cnames->push_back(fvalues[FlowRecordFields::FLOWREC_PROTOCOL]);
        cnames->push_back(fvalues[FlowRecordFields::FLOWREC_SPORT]);
        break;
    case FLOW_INDEX_TABLE_PROTOCOL_DPORT:
        cnames->push_back(fvalues[FlowRecordFields::FLOWREC_PROTOCOL]);
        cnames->push_back(fvalues[FlowRecordFields::FLOWREC_DPORT]);
        break;
    case FLOW_INDEX_TABLE_VROUTER:
        cnames->push_back(fvalues[FlowRecordFields::FLOWREC_VROUTER]);
        break;
    default:
        VIZD_ASSERT(0);
        break;
    }
    cnames->push_back(T1);
    cnames->push_back(fvalues[FlowRecordFields::FLOWREC_FLOWUUID]);
}

static void PopulateFlowIndexTableColumns(FlowIndexTableType ftype,
    const FlowValueArray &fvalues, const uint32_t &T1,
    GenDb::NewColVec *columns, const GenDb::DbDataValueVec &cvalues,
    const TtlMap& ttl_map) {
    int ttl = DbHandler::GetTtlFromMap(ttl_map, TtlType::FLOWDATA_TTL);

    GenDb::DbDataValueVec *names(new GenDb::DbDataValueVec);
    PopulateFlowIndexTableColumnNames(ftype, fvalues, T1, names);
    GenDb::DbDataValueVec *values(new GenDb::DbDataValueVec(cvalues));
    GenDb::NewCol *col(new GenDb::NewCol(names, values, ttl));
    columns->reserve(1);
    columns->push_back(col);
}

static bool PopulateFlowIndexTables(const FlowValueArray &fvalues, 
    const uint32_t &T2, const uint32_t &T1, uint8_t partition_no,
    DbInsertCb db_insert_cb, const TtlMap& ttl_map,
    FlowFieldValuesCb fncb) {
    // Populate row key and column values (same for all flow index
    // tables)
    GenDb::DbDataValueVec rkey;
    PopulateFlowIndexTableRowKey(fvalues, T2, partition_no, &rkey);
    GenDb::DbDataValueVec cvalues;
    int ttl = DbHandler::GetTtlFromMap(ttl_map, TtlType::FLOWDATA_TTL);
    PopulateFlowIndexTableColumnValues(FlowIndexTableColumnValues, fvalues,
        &cvalues, ttl, fncb);
    // Populate the Flow Index Tables
    for (int tid = FLOW_INDEX_TABLE_MIN;
         tid < FLOW_INDEX_TABLE_MAX_PLUS_1; ++tid) {
        FlowIndexTableType fitt(static_cast<FlowIndexTableType>(tid));
        std::auto_ptr<GenDb::ColList> colList(new GenDb::ColList);
        colList->cfname_ = FlowIndexTable2String(fitt);
        colList->rowkey_ = rkey;
        PopulateFlowIndexTableColumns(fitt, fvalues, T1, &colList->columns_,
            cvalues, ttl_map);
        if (!db_insert_cb(colList)) {
            LOG(ERROR, "Populating " << FlowIndexTable2String(fitt) <<
                " FAILED");
        }
    }
    return true;
}

template <typename T>
bool FlowLogDataObjectWalker<T>::for_each(pugi::xml_node& node) {
    std::string col_name(node.name());
    FlowTypeMap::const_iterator it = flow_msg2type_map.find(col_name);
    if (it != flow_msg2type_map.end()) {
        // Extract the values and populate the value array
        const FlowTypeInfo &ftinfo(it->second);
        switch (ftinfo.get<1>()) {
        case GenDb::DbDataType::Unsigned8Type:
            {
                int8_t val;
                stringToInteger(node.child_value(), val);
                values_[ftinfo.get<0>()] = static_cast<uint8_t>(val);
                break;
            }
        case GenDb::DbDataType::Unsigned16Type:
            {
                int16_t val;
                stringToInteger(node.child_value(), val);
                values_[ftinfo.get<0>()] = static_cast<uint16_t>(val);
                break;
            }
        case GenDb::DbDataType::Unsigned32Type:
            {
                int32_t val;
                stringToInteger(node.child_value(), val);
                values_[ftinfo.get<0>()] = static_cast<uint32_t>(val);
                break;
            }
        case GenDb::DbDataType::Unsigned64Type:
            {
                int64_t val;
                stringToInteger(node.child_value(), val);
                values_[ftinfo.get<0>()] = static_cast<uint64_t>(val);
                break;
            }
        case GenDb::DbDataType::DoubleType:
            {
                double val;
                stringToInteger(node.child_value(), val);
                values_[ftinfo.get<0>()] = val;
                break;
            }
        case GenDb::DbDataType::LexicalUUIDType:
        case GenDb::DbDataType::TimeUUIDType:
            {
                std::stringstream ss;
                ss << node.child_value();
                boost::uuids::uuid u;
                if (!ss.str().empty()) {
                    ss >> u;
                    if (ss.fail()) {
                        LOG(ERROR, "FlowRecordTable: " << col_name << ": (" <<
                            node.child_value() << ") INVALID");
                    }
                }     
                values_[ftinfo.get<0>()] = u;
                break;
            }
        case GenDb::DbDataType::AsciiType:
        case GenDb::DbDataType::UTF8Type:
            {
                std::string val = node.child_value();
                TXMLProtocol::unescapeXMLControlChars(val);
                values_[ftinfo.get<0>()] = val;
                break;
            }
        case GenDb::DbDataType::InetType:
            {
                // Handle old datatype
                if (strcmp(node.attribute("type").value(), "i32") == 0) {
                    uint32_t v4;
                    stringToInteger(node.child_value(), v4);
                    Ip4Address ip4addr(v4);
                    values_[ftinfo.get<0>()] = ip4addr;
                } else {
                    boost::system::error_code ec;
                    IpAddress ipaddr(IpAddress::from_string(
                                     node.child_value(), ec));
                    if (ec) {
                        LOG(ERROR, "FlowRecordTable: " << col_name << ": (" <<
                            node.child_value() << ") INVALID");
                    }
                    values_[ftinfo.get<0>()] = ipaddr;
                }
                break;
            }
        default:
            VIZD_ASSERT(0);
            break;
        }
    }
    return true;
}

uint64_t SessionTableDbStats::num_messages = 0;
uint64_t SessionTableDbStats::num_writes = 0;
uint64_t SessionTableDbStats::num_samples = 0;
uint64_t SessionTableDbStats::curr_json_size = 0;

SessionValueArray default_col_values = boost::assign::list_of
    (GenDb::DbDataValue((uint32_t)0))
    (GenDb::DbDataValue((uint8_t)0))
    (GenDb::DbDataValue((uint8_t)0))
    (GenDb::DbDataValue((uint8_t)0))
    (GenDb::DbDataValue((uint16_t)0))
    (GenDb::DbDataValue((uint16_t)0))
    (GenDb::DbDataValue((uint32_t)0))
    (GenDb::DbDataValue(boost::uuids::nil_uuid()))
    (GenDb::DbDataValue("__UNKNOWN__"))
    (GenDb::DbDataValue("__UNKNOWN__"))
    (GenDb::DbDataValue("__UNKNOWN__"))
    (GenDb::DbDataValue("__UNKNOWN__"))
    (GenDb::DbDataValue(""))
    (GenDb::DbDataValue("__UNKNOWN__"))
    (GenDb::DbDataValue("__UNKNOWN__"))
    (GenDb::DbDataValue("__UNKNOWN__"))
    (GenDb::DbDataValue("__UNKNOWN__"))
    (GenDb::DbDataValue(""))
    (GenDb::DbDataValue("__UNKNOWN__"))
    (GenDb::DbDataValue("__UNKNOWN__"))
    (GenDb::DbDataValue("__UNKNOWN__"))
    (GenDb::DbDataValue("__UNKNOWN__"))
    (GenDb::DbDataValue("__UNKNOWN__"))
    (GenDb::DbDataValue("__UNKNOWN__"))
    (GenDb::DbDataValue(""))
    (GenDb::DbDataValue(IpAddress()))
    (GenDb::DbDataValue((uint64_t)0))
    (GenDb::DbDataValue((uint64_t)0))
    (GenDb::DbDataValue((uint64_t)0))
    (GenDb::DbDataValue((uint64_t)0))
    (GenDb::DbDataValue((uint64_t)0))
    (GenDb::DbDataValue((uint64_t)0))
    (GenDb::DbDataValue((uint64_t)0))
    (GenDb::DbDataValue((uint64_t)0))
    (GenDb::DbDataValue(""));

static bool PopulateSessionTable(uint32_t t2, SessionValueArray& svalues,
    DbInsertCb db_insert_cb, TtlMap& ttl_map) {

    std::auto_ptr<GenDb::ColList> colList(new GenDb::ColList);
    // RowKey
    colList->rowkey_.reserve(3);
    colList->rowkey_.push_back(svalues[SessionRecordFields::SESSION_T2]);
    colList->rowkey_.push_back(svalues[SessionRecordFields::SESSION_PARTITION_NO]);
    colList->rowkey_.push_back(svalues[SessionRecordFields::SESSION_IS_SI]);
    colList->rowkey_.push_back(svalues[
        SessionRecordFields::SESSION_IS_CLIENT_SESSION]);

    // Column Names
    GenDb::DbDataValueVec* cnames(new GenDb::DbDataValueVec);
    for (int sfield = g_viz_constants.SESSION_MIN;
            sfield != g_viz_constants.SESSION_MAX - 1; sfield++) {
        if (svalues[sfield].which() == GenDb::DB_VALUE_BLANK) {
            if (sfield >= SessionRecordFields::SESSION_DEPLOYMENT &&
                sfield <= SessionRecordFields::SESSION_REMOTE_VN &&
                sfield != SessionRecordFields::SESSION_LABELS &&
                sfield != SessionRecordFields::SESSION_REMOTE_LABELS) {
                cnames->push_back(integerToString(t2) + ":" + g_viz_constants.UNKNOWN);
            } else {
                cnames->push_back(default_col_values[sfield]);
            }
        }
        else {
            cnames->push_back(svalues[sfield]);
        }
    }

    // Column Values
    GenDb::DbDataValueVec* cvalue(new GenDb::DbDataValueVec);
    cvalue->reserve(1);
    cvalue->push_back(svalues[SessionRecordFields::SESSION_MAP]);

    int ttl = DbHandler::GetTtlFromMap(ttl_map, TtlType::FLOWDATA_TTL);

    colList->cfname_ = g_viz_constants.SESSION_TABLE;
    colList->columns_.push_back(new GenDb::NewCol(cnames, cvalue, ttl));
    if (!db_insert_cb(colList)) {
            LOG(ERROR, "Populating SessionTable FAILED");
    }

    return true;
}

void JsonifySessionMap(const pugi::xml_node& root, std::string *json_string) {

    contrail_rapidjson::Document session_map;
    session_map.SetObject();
    contrail_rapidjson::Document::AllocatorType& allocator =
        session_map.GetAllocator();

    for (pugi::xml_node ip_port = root.first_child(); ip_port; ip_port =
        ip_port.next_sibling().next_sibling()) {
        std::ostringstream ip_port_ss;
        ip_port_ss << ip_port.child(g_flow_constants.PORT.c_str()).child_value() << ":"
            << ip_port.child(g_flow_constants.IP.c_str()).child_value();
        contrail_rapidjson::Value session_val(contrail_rapidjson::kObjectType);
        pugi::xml_node session(ip_port.next_sibling());
        for (pugi::xml_node field = session.first_child(); field;
            field = field.next_sibling()) {
            contrail_rapidjson::Value fk(contrail_rapidjson::kStringType);
            std::string fname(field.name());
            uint64_t val;
            if (fname == "forward_flow_info" || fname == "reverse_flow_info") {
                contrail_rapidjson::Value flow_info(contrail_rapidjson::kObjectType);
                for (pugi::xml_node finfo = field.child("SessionFlowInfo").first_child();
                    finfo; finfo = finfo.next_sibling()) {
                    std::string name(finfo.name());
                    std::string value(finfo.child_value());
                    std::map<std::string, bool>::const_iterator it =
                        g_flow_constants.SessionFlowInfoField2Type.find(name);
                    assert(it != g_flow_constants.SessionFlowInfoField2Type.end());
                    if (it->second) {
                        stringToInteger(value, val);
                        contrail_rapidjson::Value fv(contrail_rapidjson::kNumberType);
                        flow_info.AddMember(fk.SetString(name.c_str(), allocator),
                            fv.SetUint64(val), allocator);
                    } else {
                        contrail_rapidjson::Value fv(contrail_rapidjson::kStringType);
                        flow_info.AddMember(fk.SetString(name.c_str(), allocator),
                            fv.SetString(value.c_str(), allocator), allocator);
                    }
                }
                session_val.AddMember(fk.SetString(fname.c_str(), allocator),
                    flow_info, allocator);
            } else {
                std::string fvalue(field.child_value());
                if (stringToInteger(fvalue, val)) {
                    contrail_rapidjson::Value fv(contrail_rapidjson::kNumberType);
                    session_val.AddMember(fk.SetString(fname.c_str(), allocator),
                        fv.SetUint64(val), allocator);
                } else {
                    contrail_rapidjson::Value fv(contrail_rapidjson::kStringType);
                    session_val.AddMember(fk.SetString(fname.c_str(), allocator),
                        fv.SetString(fvalue.c_str(), allocator), allocator);
                }
            }
        }
        contrail_rapidjson::Value vk(contrail_rapidjson::kStringType);
        session_map.AddMember(vk.SetString(ip_port_ss.str().c_str(),
            allocator), session_val, allocator);
    }

    contrail_rapidjson::StringBuffer sb;
    contrail_rapidjson::Writer<contrail_rapidjson::StringBuffer> writer(sb);
    session_map.Accept(writer);
    *json_string = sb.GetString();
}

/*
 * process the session sample and insert into the appropriate table
 */
bool DbHandler::SessionSampleAdd(const pugi::xml_node& session_sample,
                                 const SandeshHeader& header,
                                 GenDb::GenDbIf::DbAddColumnCb db_cb) {
    SessionValueArray session_entry_values;
    pugi::xml_node &mnode = const_cast<pugi::xml_node &>(session_sample);

    // Set T1 and T2 from timestamp
    uint64_t timestamp(header.get_Timestamp());
    uint32_t T2(timestamp >> g_viz_constants.RowTimeInBits);
    uint32_t T1(timestamp & g_viz_constants.RowTimeInMask);
    session_entry_values[SessionRecordFields::SESSION_T2] = T2;
    session_entry_values[SessionRecordFields::SESSION_T1] = T1;
    // vrouter
    session_entry_values[SessionRecordFields::SESSION_VROUTER] = header.get_Source();

    pugi::xml_node session_agg_info_node;
    // Populate session_entry_values from message
    for (pugi::xml_node sfield = mnode.first_child(); sfield;
            sfield = sfield.next_sibling()) {

        std::string col_type(sfield.attribute("type").value());
        std::string col_name(sfield.name());
        SessionTypeMap::const_iterator it = session_msg2type_map.find(col_name);
        if (it != session_msg2type_map.end()) {
            const SessionTypeInfo &stinfo(it->second);

            if (col_type == "set") {
                pugi::xml_node set = sfield.child("set");
                std::ostringstream set_value;
                int i = 0;
                for (pugi::xml_node set_elem = set.first_child(); set_elem;
                        set_elem = set_elem.next_sibling()) {
                    if (i) {
                        set_value << ";";
                    }
                    std::string val = set_elem.child_value();
                    TXMLProtocol::unescapeXMLControlChars(val);
                    set_value << val;
                    i++;
                }
                session_entry_values[stinfo.get<0>()] = set_value.str();
                continue;
            }

            switch(stinfo.get<1>()) {
            case GenDb::DbDataType::Unsigned8Type:
                {
                    uint8_t val;
                    stringToInteger(sfield.child_value(), val);
                    session_entry_values[stinfo.get<0>()] =
                        static_cast<uint8_t>(val);
                    break;
                }
            case GenDb::DbDataType::Unsigned16Type:
                {
                    uint16_t val;
                    stringToInteger(sfield.child_value(), val);
                    session_entry_values[stinfo.get<0>()] =
                        static_cast<uint16_t>(val);
                    break;
                }
            case GenDb::DbDataType::Unsigned32Type:
                {
                    uint32_t val;
                    stringToInteger(sfield.child_value(), val);
                    session_entry_values[stinfo.get<0>()] =
                        static_cast<uint32_t>(val);
                    break;
                }
            case GenDb::DbDataType::Unsigned64Type:
                {
                    uint64_t val;
                    stringToInteger(sfield.child_value(), val);
                    session_entry_values[stinfo.get<0>()] =
                        static_cast<uint64_t>(val);
                    break;
                }
            case GenDb::DbDataType::LexicalUUIDType:
            case GenDb::DbDataType::TimeUUIDType:
                {
                    std::stringstream ss;
                    ss << sfield.child_value();
                    boost::uuids::uuid u;
                    if (!ss.str().empty()) {
                        ss >> u;
                        if (ss.fail()) {
                            LOG(ERROR, "SessionTable: " << col_name << ": (" <<
                                sfield.child_value() << ") INVALID");
                        }
                    }
                    session_entry_values[stinfo.get<0>()] = u;
                    break;
                }
            case GenDb::DbDataType::AsciiType:
            case GenDb::DbDataType::UTF8Type:
                {
                    std::string val = sfield.child_value();
                    TXMLProtocol::unescapeXMLControlChars(val);
                    switch(stinfo.get<0>()) {
                    case SessionRecordFields::SESSION_DEPLOYMENT:
                    case SessionRecordFields::SESSION_TIER:
                    case SessionRecordFields::SESSION_APPLICATION:
                    case SessionRecordFields::SESSION_SITE:
                    case SessionRecordFields::SESSION_REMOTE_DEPLOYMENT:
                    case SessionRecordFields::SESSION_REMOTE_TIER:
                    case SessionRecordFields::SESSION_REMOTE_APPLICATION:
                    case SessionRecordFields::SESSION_REMOTE_SITE:
                    case SessionRecordFields::SESSION_REMOTE_PREFIX:
                    case SessionRecordFields::SESSION_SECURITY_POLICY_RULE:
                    case SessionRecordFields::SESSION_VMI:
                    case SessionRecordFields::SESSION_VN:
                    case SessionRecordFields::SESSION_REMOTE_VN:
                        {
                            std::ostringstream v;
                            v << T2 << ":" << val;
                            session_entry_values[stinfo.get<0>()] = v.str();
                            break;
                        }
                    default:
                        {
                            session_entry_values[stinfo.get<0>()] = val;
                            break;
                        }
                    }
                    break;
                }
            case GenDb::DbDataType::InetType:
                {
                    boost::system::error_code ec;
                    IpAddress ipaddr(IpAddress::from_string(
                                     sfield.child_value(), ec));
                    if (ec) {
                        LOG(ERROR, "SessionRecordTable: " << col_name << ": ("
                            << sfield.child_value() << ") INVALID");
                    }
                    session_entry_values[stinfo.get<0>()] = ipaddr;
                    break;
                }
            default:
                {
                    VIZD_ASSERT(0);
                    break;
                }
            }
        } else if (col_type == "map" && col_name == "sess_agg_info") {
            session_agg_info_node = sfield.child("map");
            continue;
        }
    }

    for (pugi::xml_node ip_port_proto = session_agg_info_node.first_child();
        ip_port_proto; ip_port_proto = ip_port_proto.next_sibling().next_sibling()) {
        uint16_t val;
        stringToInteger(ip_port_proto.child(g_flow_constants.SERVICE_PORT.c_str()).child_value(), val);
        session_entry_values[SessionRecordFields::SESSION_SPORT] = val;
        stringToInteger(ip_port_proto.child(g_flow_constants.PROTOCOL.c_str()).child_value(), val);
        session_entry_values[SessionRecordFields::SESSION_PROTOCOL] = val;
        session_entry_values[SessionRecordFields::SESSION_UUID] = umn_gen_();
        // Partition No
        uint8_t partition_no = gen_partition_no_();
        session_entry_values[SessionRecordFields::SESSION_PARTITION_NO] = partition_no;
        std::ostringstream oss;
        oss << T2 << ":" << ip_port_proto.child(g_flow_constants.LOCAL_IP.c_str()).child_value();
        session_entry_values[SessionRecordFields::SESSION_IP] = oss.str();
        pugi::xml_node sess_agg_info = ip_port_proto.next_sibling();
        for (pugi::xml_node agg_info = sess_agg_info.first_child();
            agg_info; agg_info = agg_info.next_sibling()) {
            if (strcmp(agg_info.attribute("type").value(), "map") == 0) {
                int16_t samples;
                stringToInteger(agg_info.child("map").attribute("size").value(), samples);
                SessionTableDbStats::num_samples += samples;
                std::string session_map;
                JsonifySessionMap(agg_info.child("map"), &session_map);
                SessionTableDbStats::curr_json_size += session_map.size(); 
                session_entry_values[SessionRecordFields::SESSION_MAP]
                    = session_map;
                continue;
            }
            std::string field_name(agg_info.name());
            SessionTypeMap::const_iterator it =
                session_msg2type_map.find(field_name);
            if (it != session_msg2type_map.end()) {
                const SessionTypeInfo &stinfo(it->second);
                uint64_t val;
                stringToInteger(agg_info.child_value(), val);
                session_entry_values[stinfo.get<0>()]
                    = static_cast<uint64_t>(val);
            }
        }
        DbInsertCb db_insert_cb =
            boost::bind(&DbHandler::InsertIntoDb, this, _1,
            GenDb::DbConsistency::LOCAL_ONE, db_cb);
        if (!PopulateSessionTable(T2, session_entry_values,
            db_insert_cb, ttl_map_)) {
                DB_LOG(ERROR, "Populating SessionRecordTable FAILED");
        }
        SessionTableDbStats::num_writes++;
    }

    int ttl = DbHandler::GetTtlFromMap(ttl_map_, TtlType::FLOWDATA_TTL);
    // insert into FieldNames table
    FieldNamesTableInsert(timestamp, g_viz_constants.SESSION_TABLE, ":vrouter",
        boost::get<std::string>(session_entry_values[
            SessionRecordFields::SESSION_VROUTER]), ttl, db_cb);
    FieldNamesTableInsert(timestamp, g_viz_constants.SESSION_TABLE, ":vn",
        boost::get<std::string>(session_entry_values[
            SessionRecordFields::SESSION_VN]), ttl, db_cb);
    FieldNamesTableInsert(timestamp, g_viz_constants.SESSION_TABLE, ":remote_vn",
        boost::get<std::string>(session_entry_values[
            SessionRecordFields::SESSION_REMOTE_VN]), ttl, db_cb);

    SessionTableDbStats::num_messages++;
    return true;
}

/*
 * process the flow sample and insert into the appropriate tables
 */
bool DbHandler::FlowSampleAdd(const pugi::xml_node& flow_sample,
                              const SandeshHeader& header,
                              GenDb::GenDbIf::DbAddColumnCb db_cb) {
    // Traverse and populate the flow entry values
    FlowValueArray flow_entry_values;
    FlowLogDataObjectWalker<FlowValueArray> flow_msg_walker(flow_entry_values);
    pugi::xml_node &mnode = const_cast<pugi::xml_node &>(flow_sample);
    if (!mnode.traverse(flow_msg_walker)) {
        VIZD_ASSERT(0);
    }
    // Populate FLOWREC_VROUTER from SandeshHeader source
    flow_entry_values[FlowRecordFields::FLOWREC_VROUTER] = header.get_Source();
    // Populate FLOWREC_JSON to empty string
    flow_entry_values[FlowRecordFields::FLOWREC_JSON] = std::string();
    // Populate FLOWREC_SHORT_FLOW based on setup_time and teardown_time
    GenDb::DbDataValue &setup_time(
        flow_entry_values[FlowRecordFields::FLOWREC_SETUP_TIME]);
    GenDb::DbDataValue &teardown_time(
        flow_entry_values[FlowRecordFields::FLOWREC_TEARDOWN_TIME]);
    if (setup_time.which() != GenDb::DB_VALUE_BLANK &&
        teardown_time.which() != GenDb::DB_VALUE_BLANK) {
        flow_entry_values[FlowRecordFields::FLOWREC_SHORT_FLOW] =
            static_cast<uint8_t>(1);
    } else {
        flow_entry_values[FlowRecordFields::FLOWREC_SHORT_FLOW] =
            static_cast<uint8_t>(0);
    }
    // Calculate T1 and T2 values from timestamp
    uint64_t timestamp(header.get_Timestamp());
    uint32_t T2(timestamp >> g_viz_constants.RowTimeInBits);
    uint32_t T1(timestamp & g_viz_constants.RowTimeInMask);
    // Partition no
    uint8_t partition_no = gen_partition_no_();
    // Populate Flow Record Table
    FlowFieldValuesCb fncb =
            boost::bind(&DbHandler::FieldNamesTableInsert,
            this, timestamp, g_viz_constants.FLOW_TABLE, _1, _2, _3, db_cb);
    DbInsertCb db_insert_cb =
        boost::bind(&DbHandler::InsertIntoDb, this, _1,
            GenDb::DbConsistency::LOCAL_ONE, db_cb);
    if (!PopulateFlowRecordTable(flow_entry_values, db_insert_cb, ttl_map_,
            fncb)) {
        DB_LOG(ERROR, "Populating FlowRecordTable FAILED");
    }
    GenDb::DbDataValue &diff_bytes(
        flow_entry_values[FlowRecordFields::FLOWREC_DIFF_BYTES]);
    GenDb::DbDataValue &diff_packets(
        flow_entry_values[FlowRecordFields::FLOWREC_DIFF_PACKETS]);
    // Populate Flow Index Tables only if FLOWREC_DIFF_BYTES and
    // FLOWREC_DIFF_PACKETS are present
    FlowFieldValuesCb fncb2 =
            boost::bind(&DbHandler::FieldNamesTableInsert,
            this, timestamp, g_viz_constants.FLOW_SERIES_TABLE, _1, _2, _3,
            db_cb);
    if (diff_bytes.which() != GenDb::DB_VALUE_BLANK &&
        diff_packets.which() != GenDb::DB_VALUE_BLANK) {
       if (!PopulateFlowIndexTables(flow_entry_values, T2, T1, partition_no,
                db_insert_cb, ttl_map_, fncb2)) {
           DB_LOG(ERROR, "Populating FlowIndexTables FAILED");
       }
    }
    return true;
}

/*
 * process the session sandesh message
 */

bool DbHandler::SessionTableInsert(const pugi::xml_node &parent,
    const SandeshHeader& header, GenDb::GenDbIf::DbAddColumnCb db_cb) {
    pugi::xml_node session_data(parent.child("session_data"));
    if (!session_data) {
        return true;
    }
    // Session sandesh message may contain a list of session samples or
    // a single session sample
    if (strcmp(session_data.attribute("type").value(), "list") == 0) {
        pugi::xml_node session_list = session_data.child("list");
        for (pugi::xml_node ssample = session_list.first_child(); ssample;
            ssample = ssample.next_sibling()) {
            SessionSampleAdd(ssample, header, db_cb);
        }
    } else {
        SessionSampleAdd(session_data.first_child(), header, db_cb);
    }
    return true;
}

bool DbHandler::GetSessionTableDbInfo(SessionTableDbInfo *session_table_info) const {
    {
        tbb::mutex::scoped_lock lock(smutex_);
        if (SessionTableDbStats::num_messages == 0) {
            return true;
        }
        double writes_per_message = (double)SessionTableDbStats::num_writes /
                                        SessionTableDbStats::num_messages;
        double session_per_db_write = (double)SessionTableDbStats::num_samples /
                                        SessionTableDbStats::num_writes;
        double json_size_per_write = (double)SessionTableDbStats::curr_json_size /
                                        SessionTableDbStats::num_writes;
        session_table_info->set_writes_per_message(writes_per_message);
        session_table_info->set_sessions_per_db_record(session_per_db_write);
        session_table_info->set_json_size_per_write(json_size_per_write);
        session_table_info->set_num_messages(SessionTableDbStats::num_messages);
        SessionTableDbStats::num_writes = 0;
        SessionTableDbStats::num_samples = 0;
        SessionTableDbStats::num_messages = 0;
        SessionTableDbStats::curr_json_size = 0;
    }
    return true;
}

/*
 * process the flow sandesh message
 */
bool DbHandler::FlowTableInsert(const pugi::xml_node &parent,
    const SandeshHeader& header, GenDb::GenDbIf::DbAddColumnCb db_cb) {
    pugi::xml_node flowdata(parent.child("flowdata"));
    if (!flowdata) {
        return true;
    }
    // Flow sandesh message may contain a list of flow samples or
    // a single flow sample
    if (strcmp(flowdata.attribute("type").value(), "list") == 0) {
        pugi::xml_node flow_list = flowdata.child("list");
        for (pugi::xml_node fsample = flow_list.first_child(); fsample;
            fsample = fsample.next_sibling()) {
            FlowSampleAdd(fsample, header, db_cb);
        }
    } else {
        FlowSampleAdd(flowdata.first_child(), header, db_cb);
    }
    return true;
}

bool DbHandler::UnderlayFlowSampleInsert(const UFlowData& flow_data,
                                         uint64_t timestamp,
                                         GenDb::GenDbIf::DbAddColumnCb db_cb) {
    const std::vector<UFlowSample>& flow = flow_data.get_flow();
    for (std::vector<UFlowSample>::const_iterator it = flow.begin();
         it != flow.end(); ++it) {
        // Add all attributes
        DbHandler::AttribMap amap;
        DbHandler::Var name(flow_data.get_name());
        amap.insert(std::make_pair("name", name));
        DbHandler::Var pifindex = it->get_pifindex();
        amap.insert(std::make_pair("flow.pifindex", pifindex));
        DbHandler::Var sip = it->get_sip();
        amap.insert(std::make_pair("flow.sip", sip));
        DbHandler::Var dip = it->get_dip();
        amap.insert(std::make_pair("flow.dip", dip));
        DbHandler::Var sport = static_cast<uint64_t>(it->get_sport());
        amap.insert(std::make_pair("flow.sport", sport));
        DbHandler::Var dport = static_cast<uint64_t>(it->get_dport());
        amap.insert(std::make_pair("flow.dport", dport));
        DbHandler::Var protocol = static_cast<uint64_t>(it->get_protocol());
        amap.insert(std::make_pair("flow.protocol", protocol));
        DbHandler::Var ft = it->get_flowtype();
        amap.insert(std::make_pair("flow.flowtype", ft));
        
        DbHandler::TagMap tmap;
        // Add tag -> name:.pifindex
        DbHandler::AttribMap amap_name_pifindex;
        amap_name_pifindex.insert(std::make_pair("flow.pifindex", pifindex));
        tmap.insert(std::make_pair("name", std::make_pair(name,
                amap_name_pifindex)));
        // Add tag -> .sip
        DbHandler::AttribMap amap_sip;
        tmap.insert(std::make_pair("flow.sip", std::make_pair(sip, amap_sip)));
        // Add tag -> .dip
        DbHandler::AttribMap amap_dip;
        tmap.insert(std::make_pair("flow.dip", std::make_pair(dip, amap_dip)));
        // Add tag -> .protocol:.sport
        DbHandler::AttribMap amap_protocol_sport;
        amap_protocol_sport.insert(std::make_pair("flow.sport", sport));
        tmap.insert(std::make_pair("flow.protocol",
                std::make_pair(protocol, amap_protocol_sport)));
        // Add tag -> .protocol:.dport
        DbHandler::AttribMap amap_protocol_dport;
        amap_protocol_dport.insert(std::make_pair("flow.dport", dport));
        tmap.insert(std::make_pair("flow.protocol",
                std::make_pair(protocol, amap_protocol_dport)));
        StatTableInsert(timestamp, "UFlowData", "flow", tmap, amap, db_cb);
    }
    return true;
}

using namespace zookeeper::client;

DbHandlerInitializer::DbHandlerInitializer(EventManager *evm,
    const std::string &db_name, const std::string &timer_task_name,
    DbHandlerInitializer::InitializeDoneCb callback,
    const Options::Cassandra &cassandra_options,
    const std::string &zookeeper_server_list,
    bool use_zookeeper,
    const DbWriteOptions &db_write_options,
    ConfigClientCollector *config_client) :
    db_name_(db_name),
    db_handler_(new DbHandler(evm,
        boost::bind(&DbHandlerInitializer::ScheduleInit, this),
        db_name, cassandra_options,
        true, db_write_options, config_client)),
    callback_(callback),
    db_init_timer_(TimerManager::CreateTimer(*evm->io_service(),
        db_name + " Db Init Timer",
        TaskScheduler::GetInstance()->GetTaskId(timer_task_name))),
    zookeeper_server_list_(zookeeper_server_list),
    use_zookeeper_(use_zookeeper),
    zoo_locked_(false) {
    if (use_zookeeper_) {
        zoo_client_.reset(new ZookeeperClient(db_name_.c_str(),
            zookeeper_server_list_.c_str()));
        zoo_mutex_.reset(new ZookeeperLock(zoo_client_.get(), "/collector"));
    }
}

DbHandlerInitializer::DbHandlerInitializer(EventManager *evm,
    const std::string &db_name, const std::string &timer_task_name,
    DbHandlerInitializer::InitializeDoneCb callback,
    DbHandlerPtr db_handler) :
    db_name_(db_name),
    db_handler_(db_handler),
    callback_(callback),
    db_init_timer_(TimerManager::CreateTimer(*evm->io_service(),
        db_name + " Db Init Timer",
        TaskScheduler::GetInstance()->GetTaskId(timer_task_name))) {
}

DbHandlerInitializer::~DbHandlerInitializer() {
}

bool DbHandlerInitializer::Initialize() {
    // Synchronize creation across nodes using zookeeper
    if (use_zookeeper_ && !zoo_locked_) {
        assert(zoo_mutex_->Lock());
        zoo_locked_ = true;
    }
    if (!db_handler_->Init(true)) {
        // Update connection info
        ConnectionState::GetInstance()->Update(ConnectionType::DATABASE,
            db_name_, ConnectionStatus::DOWN, db_handler_->GetEndpoints(),
            std::string());
        LOG(DEBUG, db_name_ << ": Db Initialization FAILED");
        ScheduleInit();
        return false;
    }
    if (use_zookeeper_ && zoo_locked_) {
        assert(zoo_mutex_->Release());
        zoo_locked_ = false;
    }
    // Update connection info
    ConnectionState::GetInstance()->Update(ConnectionType::DATABASE,
        db_name_, ConnectionStatus::UP, db_handler_->GetEndpoints(),
        std::string());

    if (callback_) {
       callback_();
    }

    LOG(DEBUG, db_name_ << ": Db Initialization DONE");
    return true;
}

DbHandlerPtr DbHandlerInitializer::GetDbHandler() const {
    return db_handler_;
}

void DbHandlerInitializer::Shutdown() {
    TimerManager::DeleteTimer(db_init_timer_);
    db_init_timer_ = NULL;
    db_handler_->UnInit();
}

bool DbHandlerInitializer::InitTimerExpired() {
    // Start the timer again if initialization is not done
    bool done = Initialize();
    return !done;
}

void DbHandlerInitializer::InitTimerErrorHandler(string error_name,
    string error_message) {
    LOG(ERROR, db_name_ << ": " << error_name << " " << error_message);
}

void DbHandlerInitializer::StartInitTimer() {
    db_init_timer_->Start(kInitRetryInterval,
        boost::bind(&DbHandlerInitializer::InitTimerExpired, this),
        boost::bind(&DbHandlerInitializer::InitTimerErrorHandler, this,
                    _1, _2));
}

void DbHandlerInitializer::ScheduleInit() {
    db_handler_->UnInit();
    StartInitTimer();
}

// Prepend T2 in decimal since T2 timestamp column is in decimal
std::string PrependT2(uint32_t T2, const std::string &str) {
    std::string tempstr = integerToString(T2);
    tempstr.append(":");
    tempstr.append(str);
    return tempstr;
}
