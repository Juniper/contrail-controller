/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

/*
 * This file details various data structures used for query 
 * engine processing
 * 
 * Overall the data structure heirarchy is as following:
 *
 * AnalyticsQuery--
 *                |
 *                |--SelectQuery
 *                |
 *                |--PostProcessingQuery
 *                |
 *                |--WhereQuery (multiple ANDs)
 *                       |
 *                       |-DbQueryUnit
 *                       |
 *                       |-DbQueryUnit
 *                       ...
 *
 */
#ifndef QUERY_H_
#define QUERY_H_

#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <sstream>
#include <iostream>
#include <string>
#include <map>
#include <vector>
#include <set>
#include <algorithm>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/algorithm/string.hpp>
#include <exception>
#include "io/event_manager.h"
#include <boost/bind.hpp>
#include <boost/assign/list_of.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/ptr_container/ptr_map.hpp>
#include "base/util.h"
#include "base/task.h"
#include "net/address.h"
#include "base/parse_object.h"
#include <tbb/mutex.h>
#include <boost/bind.hpp>
#include <boost/asio.hpp>
#include <boost/tuple/tuple.hpp>
#include "base/util.h"
#include "base/logging.h"
#include <cstdlib>
#include <utility>
#include "hiredis/hiredis.h"
#include "hiredis/boostasio.hpp"
#include <list>
#include "../analytics/redis_connection.h"
#include "base/work_pipeline.h"
#include "database/gendb_if.h"
#include "database/gendb_statistics.h"
#include "../analytics/viz_message.h"
#include "json_parse.h"
#include "QEOpServerProxy.h"
#include "base/logging.h"
#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh.h>
#include <sandesh/sandesh_constants.h>
#include <sandesh/sandesh_ctrl_types.h>
#include <sandesh/sandesh_trace.h>
#include <sandesh/common/vns_constants.h>
#include "viz_constants.h"
#include "query_engine/qe_constants.h"
#include "query_engine/qe_types.h"
#include "rapidjson/document.h"
#include <boost/regex.hpp>
#include <boost/shared_ptr.hpp>

extern std::map< std::string, int > trace_enable_map;
#define IS_TRACE_ENABLED(trace_flag_string) \
    ((trace_enable_map.find(trace_flag_string) != trace_enable_map.end()))

// following is the list of trace flags
#define WHERE_RESULT_TRACE "where_result_trace"
#define SELECT_RESULT_TRACE "select_result_trace"
#define POSTPROCESS_RESULT_TRACE "postprocess_result_trace"

#define QE_ASSERT(cond) assert((cond))

#define TIMESTAMP_FROM_T2T1(t2, t1) (((uint64_t)(t2)) << g_viz_constants.RowTimeInBits) |\
    ((t1) & g_viz_constants.RowTimeInMask);

// for Sandesh trace
#define QE_TRACE_BUF "QeTraceBuf"

extern SandeshTraceBufferPtr QeTraceBuf;

// fix this later
#define QE_LOG(level, log_msg) { \
    std::string qid("");\
    std::string table("");\
    int batch_num = 0;\
    if ((AnalyticsQuery *)(this->main_query))\
    {\
        qid = (((AnalyticsQuery *)(this->main_query))->query_id);\
        table = (((AnalyticsQuery *)(this->main_query))->table());\
        batch_num = (((AnalyticsQuery *)(this->main_query))->parallel_batch_num);\
    }\
    std::stringstream ss; ss << ":" << batch_num << ":" << log_msg; \
    Q_E_QUERY_LOG_SEND(qid, table, ss.str());\
}

// Following log message will be printed for only one thread among all threads
#define QE_LOG_GLOBAL(level, log_msg) { \
    std::string qid("");\
    std::string table("");\
    int batch_num = 0;\
    if ((AnalyticsQuery *)(this->main_query))\
    {\
        qid = (((AnalyticsQuery *)(this->main_query))->query_id);\
        table = (((AnalyticsQuery *)(this->main_query))->table());\
        batch_num = (((AnalyticsQuery *)(this->main_query))->parallel_batch_num);\
    }\
    if (batch_num == 0) {\
    std::stringstream ss; ss << ":" << batch_num << ":" << log_msg; \
    Q_E_QUERY_LOG_SEND(qid, table, ss.str());\
    }\
}

#define QE_LOG_NOQID(level, log_msg) { \
    std::stringstream ss; ss << log_msg; \
    Q_E_LOG_SEND(ss.str());\
}

#define QE_TRACE_NOQID(level, trace_msg) {\
    std::stringstream ss; ss << level << ":" << trace_msg; \
    QUERY_TRACE_TRACE(QeTraceBuf, std::string(""), ss.str());\
}

#define QE_TRACE(level, trace_msg) {\
    std::stringstream ss; ss << level << ":" << trace_msg; \
    QUERY_TRACE_TRACE(QeTraceBuf, \
            (((AnalyticsQuery *)(this->main_query))? \
             (((AnalyticsQuery *)(this->main_query))->query_id):\
             std::string("")), ss.str());\
}

#if !defined(DEBUG)
#define DEBUG "DEBUG"
#endif

// this is to return errors from the member functions of the QueryUnit class
// and its derived classes. These macros just set the status_details field 
// and return from the function
#define QE_PARSE_ERROR(cond) if (!(cond)) \
    { QE_LOG(DEBUG, "EBADMSG"); this->status_details = EBADMSG; return;}
#define QE_INVALIDARG_ERROR(cond) if (!(cond)) \
    { QE_LOG(DEBUG, "EINVAL"); this->status_details = EINVAL; return;}
#define QE_IO_ERROR(cond) if (!(cond)) \
    { QE_LOG(DEBUG, "EIO"); this->status_details = EIO; return;}
#define QE_NOENT_ERROR(cond) if (!(cond)) \
    { QE_LOG(DEBUG, "ENOENT"); this->status_details = ENOENT; return;}
#define QE_PARSE_ERROR_RETURN(cond, ret_val) if (!(cond)) \
    { this->status_details = EBADMSG; return ret_val;}
#define QE_INVALIDARG_ERROR_RETURN(cond, ret_val) if (!(cond)) \
    { this->status_details = EINVAL; return ret_val;}
#define QE_IO_ERROR_RETURN(cond, ret_val) if (!(cond)) \
    { this->status_details = EIO; return ret_val;}
#define QE_NOENT_ERROR_RETURN(cond, ret_val) if (!(cond)) \
    { this->status_details = ENOENT; return ret_val;}
#define QE_QUERY_FETCH_ERROR() {\
     this->status_details = EIO; \
     AnalyticsQuery *m_query = (AnalyticsQuery *)main_query; \
     if (m_query) { \
         m_query->qperf_.error = status_details; \
         m_query->status_details = status_details; \
         m_query->query_status = QUERY_FAILURE; \
     } \
     QE_LOG(ERROR,  "QUERY failed to get rows " << QUERY_FAILURE); \
}

typedef boost::shared_ptr<GenDb::GenDbIf> GenDbIfPtr;

// flow sample stats which is stored in Cassandra flow index tables
struct flow_stats {
    flow_stats(uint64_t ibytes=0, uint64_t ipkts=0, bool ishort_flow=false) : 
        bytes(ibytes), pkts(ipkts), short_flow(ishort_flow) {
    }
    bool operator==(const flow_stats &rhs) const {
        return bytes == rhs.bytes &&
            pkts == rhs.pkts &&
            short_flow == rhs.short_flow &&
            flow_list == rhs.flow_list;
    }
    uint64_t bytes;
    uint64_t pkts;
    bool short_flow;
    std::set<boost::uuids::uuid> flow_list;
};

// 8-tuple corresponding to a flow
struct flow_tuple {
    flow_tuple() : protocol(0), source_port(0), dest_port(0), direction(0) {
    }

    flow_tuple(const std::string& vr, const std::string& svn,
               const std::string& dvn, const IpAddress& sip,
               const IpAddress& dip, uint32_t proto,
               uint32_t sport, uint32_t dport, uint32_t dir) :
        vrouter(vr), source_vn(svn), dest_vn(dvn), source_ip(sip),
        dest_ip(dip), protocol(proto), source_port(sport),
        dest_port(dport), direction(dir) {
    }
    
    bool operator<(const flow_tuple& rhs) const {
        if (vrouter < rhs.vrouter) return true;
        if (vrouter > rhs.vrouter) return false;

        if (source_vn < rhs.source_vn) return true;
        if (source_vn > rhs.source_vn) return false;

        if (dest_vn < rhs.dest_vn) return true;
        if (dest_vn > rhs.dest_vn) return false;

        if (source_ip < rhs.source_ip) return true;
        if (source_ip > rhs.source_ip) return false;
        
        if (dest_ip < rhs.dest_ip) return true;
        if (dest_ip > rhs.dest_ip) return false;

        if (protocol < rhs.protocol) return true;
        if (protocol > rhs.protocol) return false;

        if (source_port < rhs.source_port) return true;
        if (source_port > rhs.source_port) return false;

        if (dest_port < rhs.dest_port) return true;
        if (dest_port > rhs.dest_port) return false;

        if (direction < rhs.direction) return true;
        if (direction > rhs.direction) return false;

        return false;
    }

    bool operator==(const flow_tuple& rhs) const {
        return (vrouter == rhs.vrouter) &&
            (source_vn == rhs.source_vn) &&
            (dest_vn == rhs.dest_vn) &&
            (source_ip == rhs.source_ip) &&
            (dest_ip == rhs.dest_ip) &&
            (protocol == rhs.protocol) &&
            (source_port == rhs.source_port) &&
            (dest_port == rhs.dest_port) &&
            (direction == rhs.direction);
    }

    friend std::ostream& operator<<(std::ostream& out, const flow_tuple& ft);

    std::string vrouter;
    std::string source_vn;
    std::string dest_vn;
    IpAddress source_ip;
    IpAddress dest_ip;
    uint32_t protocol;
    uint32_t source_port;
    uint32_t dest_port;
    uint32_t direction;
};

struct uuid_flow_stats {
    uuid_flow_stats(uint64_t t, flow_stats stats) : 
        last_timestamp(t), last_stats(stats) {
    }
    // indiates the timestamp when the last_stats was updated
    uint64_t   last_timestamp; 
    // indicates the aggregate value of the stats seen @ last_ts
    flow_stats last_stats; 
};

// Result of basic unit of any analytics event query
struct query_result_unit_t {
    // timestamp of an analytics event (log, flow record sample etc..)
    uint64_t timestamp; 

    // this string will be 
    // UUID is in case of database queries for flow-records/flow-series
    // stats+UUID after database queries for flow-records WHERE queries
    // stats+UUID+8-tuple afer flow-series WHERE query
    // AttribJSON+UUID for StatsTable queries
    GenDb::DbDataValueVec info;

    // Following APIs will be invoked based on the table being queried

    void set_stattable_info(
            const std::string& attribstr,
            const boost::uuids::uuid& uuid);

    void get_stattable_info(
            std::string& attribstr,
            boost::uuids::uuid& uuid) const;

    // Get UUID from the info field
    void get_uuid(boost::uuids::uuid& u) const;
    // Get UUID and stats
    void get_uuid_stats(boost::uuids::uuid& u, flow_stats& stats) const;
    // Get UUID and stats and 8-tuple
    void get_uuid_stats_8tuple(boost::uuids::uuid& u,
            flow_stats& stats, flow_tuple& tuple) const;
    void get_objectid(std::string&) const;
    // for sorting and set operations
    bool operator<(const query_result_unit_t& rhs) const;

    // for printing
    friend std::ostream &operator<<(std::ostream &out, 
            query_result_unit_t&);
} ;

typedef std::vector<query_result_unit_t> WhereResultT;

// Different status codes of query processing
enum query_status_t {
    QUERY_PROCESSING_NOT_STARTED = 0,
    QUERY_IN_PROGRESS = 1,
    QUERY_SUCCESS = 2,
    QUERY_FAILURE = 3
};

// basic query unit (other classes will inherit from this)
class QueryUnit {
public:
    QueryUnit(QueryUnit *p_query, QueryUnit *m_query);
    virtual ~QueryUnit();
    virtual query_status_t process_query() = 0;

    typedef QEOpServerProxy::BufferT BufT;
    typedef QEOpServerProxy::OutRowMultimapT MapBufT;

    // following callback function is called whenever a subquery 
    // is prcoessed
    virtual void subquery_processed(QueryUnit *subquery) {};

    // list of child queries
    std::vector<QueryUnit *> sub_queries;

    // if this is a child/sub query, then following is pointer to 
    // the parent query
    QueryUnit *parent_query;
    // Pointer to the highest level query
    QueryUnit *main_query;

    int pending_subqueries;
    query_status_t query_status;
    uint32_t status_details;

    // After query is processed following vector is populated
    boost::shared_ptr<WhereResultT> query_result;
};

class QueryResultMetaData {
public:
    QueryResultMetaData() { 
    }
    virtual ~QueryResultMetaData() = 0;
};

class fsMetaData : public QueryResultMetaData {
public:
    fsMetaData(const std::set<boost::uuids::uuid>& flows) 
    : uuids(flows.begin(), flows.end()) {
    }
    ~fsMetaData() {
    }
    std::set<boost::uuids::uuid> uuids;
};

struct GetRowInput {
    GenDb::DbDataValueVec rowkey;
    std::string cfname;
    GenDb::ColumnNameRange crange;
    int chunk_no;
    std::string qid;
    int sub_qid;
    int row_no;
    int inst;
};

// max number of entries to extract from db
#define MAX_DB_QUERY_ENTRIES 100000000
// This class provides interface for doing single index database query
class DbQueryUnit : public QueryUnit {
public:
    DbQueryUnit(QueryUnit *p_query, QueryUnit *m_query):
        QueryUnit(p_query, m_query) 
        { cr.count_ = MAX_DB_QUERY_ENTRIES;
            t_only_col = false; t_only_row = false;
           sub_query_id = (p_query->sub_queries.size())-1;
           query_fetch_error = false;
           };
    virtual query_status_t process_query();


    // portion of column family name other than T1
    std::string cfname;
    // all type of match operations can be representated as 
    // getrangeslice operations on Cassandra DB. For e.g. 
    // NOT_EQUAL operation will be two getrangeslice operation
    GenDb::ColumnNameRange cr;
    // row key suffix will be used to append DIR to flow 
    // series/records query
    GenDb::DbDataValueVec row_key_suffix;
    bool t_only_col;    // only T is in column name
    bool t_only_row;    // only T2 is in row key
    int sub_query_id;
    typedef std::vector<query_result_unit_t> q_result;
    struct Input {
        tbb::atomic<uint32_t> row_count;
        tbb::atomic<uint32_t> total_rows;
        std::string cf_name;
        GenDb::ColumnNameRange cr;
        std::vector<GenDb::DbDataValueVec> keys;
    };

    struct Output {
        boost::shared_ptr<WhereResultT> query_result;
    };
    struct Stage0Out {
        WhereResultT query_result;
        uint32_t current_row;
    };
    ExternalBase::Efn QueryExec(uint32_t inst,
        const std::vector<q_result *> & exts,
        const Input & inp, Stage0Out & res);
    typedef WorkPipeline<Input, Output> QEPipeT;
    bool QueryMerge(const std::vector<boost::shared_ptr<Stage0Out> > & subs,
         const boost::shared_ptr<Input> & inp, Output & res);
    void cb(GenDb::DbOpResult::type dresult, std::auto_ptr<GenDb::ColList>
         columns, GetRowInput *get_row_ctx, void *privdata);
    void WPCompleteCb(QEPipeT *wp, bool ret_code);
    std::vector<GenDb::DbDataValueVec> populate_row_keys();
    bool PipelineCb(std::string &, GenDb::DbDataValueVec &,
        GenDb::ColumnNameRange &, GetRowInput *, void *);
    bool query_fetch_error;
};

struct SetOperationUnit {
    static void op_and(std::string qi, WhereResultT& res,
        std::vector<WhereResultT*> inp);
    static void op_or(std::string qi, WhereResultT& res,
        std::vector<WhereResultT*> inp);
};

typedef boost::function<void (void *, QEOpServerProxy::QPerfInfo,
     std::auto_ptr<WhereResultT>)> WhereQueryCbT;
// Where processing class
    // Result is available for SELECT processing in query_result field
    // It will be an array of timestamp and 
    // UUID is in case of messages/object-trace WHERE queries
    // stats+UUID after database queries for flow-records WHERE queries
    // stats+UUID+8-tuple for flow-series WHERE query

class WhereQuery : public QueryUnit {
public:

    bool StatTermParse(QueryUnit *main_query, const contrail_rapidjson::Value& where_term,
        std::string& pname, match_op& pop,
        GenDb::DbDataValue& pval, GenDb::DbDataValue& pval2,
        std::string& sname, match_op& sop,
        GenDb::DbDataValue& sval, GenDb::DbDataValue& sval2);

    bool StatTermProcess(const contrail_rapidjson::Value& where_term,
        QueryUnit* pnode, QueryUnit *main_query);
 
    WhereQuery(const std::string& where_json_string, int direction,
            int32_t or_number, QueryUnit *main_query);
    // construtor for UT
    WhereQuery(QueryUnit *mq);
    virtual query_status_t process_query();
    virtual void subquery_processed(QueryUnit *subquery);

    // 0 is for egress and 1 for ingress
    int32_t direction_ing;
    const std::string json_string_;
    uint32_t wterms_;
    std::auto_ptr<WhereResultT> where_result_;
    // Used to store the sub_query(Each DbQueryUnits) results
    std::vector<WhereResultT*> inp;
    typedef boost::function<void(void *, QEOpServerProxy::QPerfInfo,
          std::auto_ptr<WhereResultT> )> WhereQueryCbT;
    WhereQueryCbT where_query_cb_;
    
private:
    tbb::mutex vector_push_mutex_;
};

typedef std::vector<std::string> final_result_row_t;

struct final_result_t {
    // These are actual names used in the database
    std::vector<std::string> columns;
    std::vector<final_result_row_t> final_result_table;
    uint status;
};

enum agg_op_t {
    RAW = 1,
    SUM = 2,
    AGG_OP_INVALID = 3,
};

enum stat_type_t {
    PKT_STATS = 1,
    BYTE_STATS = 2,
};

// Aggregated stats
struct agg_stats_t {
    agg_op_t agg_op;
    stat_type_t stat_type;
};

class StatsSelect;

// following data structure does the processing of SELECT portion of query
class SelectQuery : public QueryUnit {
public:
    SelectQuery(QueryUnit *main_query,
            std::map<std::string, std::string> json_api_data);

    virtual query_status_t process_query();

    // Query related fields
    std::string json_string_;
    std::vector<std::string> select_column_fields;
    std::vector<agg_stats_t> agg_stats;
    // Whethere timestamp will be one of the columns
    bool provide_timeseries;
    // relevant only if provide_timeseries is set
    uint granularity; 

    // column family name (column family to query to get column 
    // fields/NULL for flow series)
    std::string cfname; 

    std::auto_ptr<BufT> result_;
    std::auto_ptr<MapBufT> mresult_;
   
    std::auto_ptr<StatsSelect> stats_;

    enum fs_query_type {
        FS_SELECT_INVALID = 0x0,
        FS_SELECT_T = 0x1,
        FS_SELECT_TS = 0x2,
        FS_SELECT_FLOW_TUPLE = 0x4,
        FS_SELECT_STATS = 0x8,
        FS_SELECT_T_FLOW_TUPLE = 0x5,
        FS_SELECT_T_STATS = 0x9,
        FS_SELECT_FLOW_TUPLE_STATS = 0xC,
        FS_SELECT_TS_FLOW_TUPLE = 0x6,
        FS_SELECT_TS_STATS = 0xA,
        FS_SELECT_T_FLOW_TUPLE_STATS = 0xD,
        FS_SELECT_TS_FLOW_TUPLE_STATS = 0xE
    };

    uint8_t flowseries_query_type() {
        return fs_query_type_;
    }

    bool is_present_in_select_column_fields(const std::string& field) {
        std::vector<std::string>::iterator it;
        it = std::find(select_column_fields.begin(), 
                       select_column_fields.end(),
                       field);
        if (it == select_column_fields.end()) {
            return false;
        }
        return true;
    }
    bool ObjectIdQuery() {
        return ((select_column_fields.size() == 1 &&
                    select_column_fields[0] == g_viz_constants.OBJECT_ID));
    }
    friend class SelectTest;
private:
    bool is_valid_select_field(const std::string& select_field) const;
    // 
    // Object table query
    //
    bool process_object_query_specific_select_params(
                        const std::string& sel_field,
                        std::map<std::string, GenDb::DbDataValue>& col_res_map,
                        std::map<std::string, std::string>& cmap,
                        const boost::uuids::uuid& uuid,
                        std::map<boost::uuids::uuid, std::string>&);

    // For flow class id in select field

    // flow class id to flow tuple map
    std::map<size_t, flow_tuple> flow_class_id_map;

    //
    // Flow Series Query
    //
    static const uint64_t kMicrosecInSec = 1000 * 1000;
  
    uint8_t fs_query_type_;
    bool is_flow_tuple_specified();
    void evaluate_fs_query_type();
    typedef void (SelectQuery::*process_fs_query_callback)(const uint64_t&, 
            const boost::uuids::uuid&, const flow_stats&, const flow_tuple&);
    typedef void (SelectQuery::*populate_fs_result_callback)();
    typedef std::map<uint8_t, process_fs_query_callback> 
        process_fs_query_cb_map_t;
    static process_fs_query_cb_map_t process_fs_query_cb_map_;
    static process_fs_query_cb_map_t process_fs_query_cb_map_init();
    typedef std::map<uint8_t, populate_fs_result_callback> 
        populate_fs_result_cb_map_t;
    static populate_fs_result_cb_map_t populate_fs_result_cb_map_;
    static populate_fs_result_cb_map_t populate_fs_result_cb_map_init();
    typedef std::map<const boost::uuids::uuid, uuid_flow_stats> 
        fs_uuid_stats_map_t;

    // Called from process_query() for FLOW SERIES Query 
    query_status_t process_fs_query(process_fs_query_callback, 
            populate_fs_result_callback);

    // flowclass is populated from tuple based on the 
    // tuple fields in the select_column_fields
    void get_flow_class(const flow_tuple& tuple, flow_tuple& flowclass);

    void fs_write_final_result_row(const uint64_t *t, const flow_tuple *tuple,
            const flow_stats *raw_stats, const flow_stats *sum_stats, 
            const flow_stats *avg_stats,
            const std::set<boost::uuids::uuid> *flow_list = NULL);
    
    uint64_t fs_get_time_slice(const uint64_t& t);
    // Common Flow series queries

    // 1. SELECT with T=<granularity>, stats fields
    typedef std::map<const uint64_t, flow_stats> fs_ts_stats_map_t;
    fs_ts_stats_map_t fs_ts_stats_map_;
    void process_fs_query_with_ts_stats_fields(const uint64_t&, 
            const boost::uuids::uuid&, const flow_stats&, const flow_tuple&);
    void populate_fs_query_result_with_ts_stats_fields();

    // 2. SELECT with flow tuple fields, stats fields
    typedef std::map<const flow_tuple, flow_stats> fs_tuple_stats_map_t;
    fs_tuple_stats_map_t fs_tuple_stats_map_;
    void process_fs_query_with_tuple_stats_fields(const uint64_t&, 
            const boost::uuids::uuid&, const flow_stats&, const flow_tuple&);
    void populate_fs_query_result_with_tuple_stats_fields();

    // 3. SELECT with T=<granularity>, flow tuple fields, stats fields
    typedef std::map<const flow_tuple, fs_ts_stats_map_t> 
        fs_ts_tuple_stats_map_t;
    fs_ts_tuple_stats_map_t fs_ts_tuple_stats_map_;
    void process_fs_query_with_ts_tuple_stats_fields(const uint64_t&, 
            const boost::uuids::uuid&, const flow_stats&, const flow_tuple&);
    void populate_fs_query_result_with_ts_tuple_stats_fields();

    // Rare Flow series queries

    // 1. SELECT with stats fields
    flow_stats fs_flow_stats_;
    void process_fs_query_with_stats_fields(const uint64_t&,
            const boost::uuids::uuid&, const flow_stats&, const flow_tuple&);
    void populate_fs_query_result_with_stats_fields();
    
    // 2. SELECT with T=<granularity>, flow tuple fields
    typedef std::map<const uint64_t, std::set<flow_tuple> > fs_ts_tuple_map_t;
    fs_ts_tuple_map_t fs_ts_tuple_map_;
    void process_fs_query_with_ts_tuple_fields(const uint64_t&,
            const boost::uuids::uuid&, const flow_stats&, const flow_tuple&);
    void populate_fs_query_result_with_ts_tuple_fields();

    // 3. SELECT with flow tuple fields
    void process_fs_query_with_tuple_fields(const uint64_t&,
            const boost::uuids::uuid&, const flow_stats&, const flow_tuple&);
    void populate_fs_query_result_with_tuple_fields();

    void process_fs_query_with_time(const uint64_t&,
            const boost::uuids::uuid&, const flow_stats&, const flow_tuple&);
    void populate_fs_query_result_with_time();
    
    // 4. SELECT with T=<granularity>
    typedef std::set<uint64_t> fs_ts_t;
    fs_ts_t fs_ts_list_;
    void process_fs_query_with_ts(const uint64_t&,
            const boost::uuids::uuid&, const flow_stats&, const flow_tuple&);
    void populate_fs_query_result_with_ts();

    // 5. SELECT with T 
    // 6. SELECT with T, flow tuple fields
    // 7. SELECT with T, stats fields 
    // 8. SELECT with T, flow tuple, stats
    void process_fs_query_with_time_tuple_stats_fields(const uint64_t&,
            const boost::uuids::uuid&, const flow_stats&, const flow_tuple&);
    void populate_fs_query_result_with_time_tuple_stats_fields();
};

struct filter_match_t {
    std::string name;   // column name of match and filter
    std::string value;  // column value to compare with
    match_op op;        // matching op
    bool ignore_col_absence; // ignore (i.e. do not delete) if col is absent
    boost::regex match_e;   // matching regex expression

    filter_match_t():ignore_col_absence(false) {};
};

struct sort_field_t {
    sort_field_t(const std::string& sort_name, const std::string& datatype) :
        name(sort_name), type(datatype) {
    }
    std::string name;
    std::string type;
};

// this data structure is passed on to post-processing module
class PostProcessingQuery: public QueryUnit {
public:
    // Initialize with the JSON string hashset received from REDIS
    PostProcessingQuery(std::map<std::string, std::string>& json_api_data,
            QueryUnit *main_query);

    virtual query_status_t process_query();

    // Query related fields

    std::string json_string_;

    // filter list is an OR of ANDs
    std::vector<std::vector<filter_match_t> > filter_list;

    // Whether to sort the table or not
    bool sorted;

    // Type of sorting
    sort_op sorting_type;
    // fields to sort on (these are actual Cassandra column names)
    std::vector<sort_field_t> sort_fields;
    int limit; // number of entries 

    // result after post processing

    std::auto_ptr<BufT> result_;
    std::auto_ptr<MapBufT> mresult_;

    bool sort_field_comparator(const QEOpServerProxy::ResultRowT& lhs,
                               const QEOpServerProxy::ResultRowT& rhs);

    // compare flow records based on UUID
    static bool flow_record_comparator(const QEOpServerProxy::ResultRowT& lhs,
                                       const QEOpServerProxy::ResultRowT& rhs);

    bool merge_processing(
        const QEOpServerProxy::BufferT& input, 
        QEOpServerProxy::BufferT& output);
    bool final_merge_processing(
const std::vector<boost::shared_ptr<QEOpServerProxy::BufferT> >& inputs,
                        QEOpServerProxy::BufferT& output);
private:
    typedef std::map<uint64_t, QEOpServerProxy::ResultRowT> fcid_rrow_map_t;
    bool flowseries_merge_processing(
                const QEOpServerProxy::BufferT *raw_result,
                QEOpServerProxy::BufferT *merged_result,
                fcid_rrow_map_t *fcid_rrow_map = NULL);
    void fs_merge_stats(const QEOpServerProxy::ResultRowT& input, 
                        QEOpServerProxy::ResultRowT& output);
    void fs_stats_merge_processing(
                const QEOpServerProxy::BufferT *input,
                QEOpServerProxy::BufferT *output);
    void fs_tuple_stats_merge_processing(
                const QEOpServerProxy::BufferT *input,
                QEOpServerProxy::BufferT* output,
                fcid_rrow_map_t *fcid_rrow_map = NULL);
    void fs_update_flow_count(QEOpServerProxy::ResultRowT& rrow);
};

class StatsQuery;

class AnalyticsQuery: public QueryUnit {
public:
    AnalyticsQuery(std::string qid, std::map<std::string, 
            std::string>& json_api_data,
            int or_number,
            const std::vector<query_result_unit_t> * where_info,
            const TtlMap& ttlmap,
            EventManager *evm, std::vector<std::string> cassandra_ips, 
            std::vector<int> cassandra_ports, int batch,
            int total_batches, const std::string& cassandra_user,
            const std::string &cassandra_password,
            QueryEngine *qe, void * pipeline_handle = NULL);
    AnalyticsQuery(std::string qid, GenDbIfPtr dbif, 
            std::map<std::string, std::string> json_api_data,
            int or_number,
            const std::vector<query_result_unit_t> * where_info,
            const TtlMap& ttlmap, int batch, int total_batches,
            QueryEngine *qe, void * pipeline_handle = NULL);
    virtual ~AnalyticsQuery() {}

    virtual query_status_t process_query();

    // Interface to Cassandra
    GenDbIfPtr dbif_;
    void db_err_handler() {};
    
    //Query related fields

    // Query Id
    std::string query_id;
    
    // if the req is to get objectids, the following will contain the key
    std::string object_value_key; 

    std::string sandesh_moduleid; // module id of the query engine itself
    bool  filter_qe_logs;   // whether to filter query engine logs

    // final result of the query
    std::auto_ptr<QEOpServerProxy::BufferT> final_result;
    std::auto_ptr<QEOpServerProxy::OutRowMultimapT> final_mresult;

    std::map<std::string, std::string> json_api_data_;
    const std::vector<query_result_unit_t> * where_info_;
    
    TtlMap ttlmap_;
    uint64_t where_start_;
    uint64_t select_start_;
    uint64_t postproc_start_;
    QEOpServerProxy::QPerfInfo qperf_;

    WhereQuery *wherequery_;
    SelectQuery *selectquery_;
    PostProcessingQuery *postprocess_;

    // For parallelization
    // Values from the original query
    // start time of the time range for the queried records
    uint64_t original_from_time;
    // end time of the time range for the queried records 
    uint64_t original_end_time; 
    // whether results from parallel instances need to be merged 
    bool merge_needed; // whether sorting/limit operations are needed
    // which parallel batch number is this
    int parallel_batch_num;
    // total number of parallel batches
    int total_parallel_batches;
    // whether any processing is needed
    bool processing_needed;
    // time slice for each parallel instance
    uint64_t time_slice;
    // shared ptr to query engine needed when where_query winds up
    QueryEngine* qe_;
    // outer pipeline handle, needed while calling the QEResult from
    // where_query context
    void *handle_;
    // this is for merge between multiple instances running on same core
    bool merge_processing(const QEOpServerProxy::BufferT& input,
                            QEOpServerProxy::BufferT& output);
    // this is for merge between instances running on multiple cores
    bool final_merge_processing(
const std::vector<boost::shared_ptr<QEOpServerProxy::BufferT> >& inputs,
                        QEOpServerProxy::BufferT& output);

    // this is to get parallelization details once the query is parsed
    void get_query_details(bool& is_merge_needed, bool& is_map_output,
        std::vector<uint64_t>& chunk_sizes,
        std::string& where, uint32_t& wterms,
        std::string& select,
        std::string& post,
        uint64_t& time_period,
        int& parse_status);

    virtual std::string table() const {
        return table_;
    }
    virtual uint64_t req_from_time() const {
        return req_from_time_;
    }
    virtual uint64_t req_end_time() const {
        return req_end_time_;
    }
    virtual uint64_t from_time() const {
        return from_time_;
    }
    virtual uint64_t end_time() const {
        return end_time_;
    }
    virtual const std::vector<query_result_unit_t>& where_query_result() {
        return *where_info_;
    }
    virtual uint32_t direction_ing() const {
        return wherequery_->direction_ing;
    }
    
    // validation functions
    static bool is_object_table_query(const std::string& tname);
    static bool is_stat_table_query(const std::string& tname);
    static bool is_stat_fieldnames_table_query(const std::string& tname);
    static bool is_flow_query(const std::string& tname); // either flow-series or flow-records query
    bool is_valid_where_field(const std::string& where_field);
    bool is_valid_sort_field(const std::string& sort_field);
    std::string get_column_field_datatype(const std::string& col_field);
    virtual bool is_query_parallelized() { return parallelize_query_; }

    const StatsQuery& stats(void) const { return *stats_; }
    std::string stat_name_attr; // will be populated only for stats query
    private:
    std::auto_ptr<StatsQuery> stats_;
    // Analytics table to query
    std::string table_; 
    // query start time requested by the user
    uint64_t req_from_time_;
    // query end time requested by the user
    uint64_t req_end_time_;
    // start time of the time range for the queried records.
    // If the start time requested by the user is earlier than the 
    // analytics start time, then this field holds the analytics start time.
    // Else, from_time is same as req_from_time.
    uint64_t from_time_; 
    // end time of the time range for the queried records.
    // If the end time requested by the user is later than the time @ which the 
    // query was received, then this field holds the time @ which the query
    // was received. Else, end_time is same as req_end_time.
    uint64_t end_time_; 
    bool parallelize_query_;
    // Init function
    void Init(std::string qid,
        std::map<std::string, std::string>& json_api_data,
        int32_t or_number);
    bool can_parallelize_query();
    void ParseStatName(std::string &stat_table_name);
};

// limit on the size of query result we can handle
static const int query_result_size_limit = 25000000;

// main class
class QueryEngine {
public:
    static const uint64_t StartTimeDiffInSec = 12*3600;
    static int max_slice_;
    
    struct QueryParams {
        QueryParams(std::string qi, 
                std::map<std::string, std::string> qu,
                uint32_t ch, uint64_t tm) :
            qid(qi), terms(qu), maxChunks(ch), query_starttm(tm) {}
        QueryParams() {}
        std::string qid;
        std::map<std::string, std::string> terms;
        uint32_t maxChunks;
        uint64_t query_starttm;
    };

    uint64_t stime;
    int max_tasks_;

    QueryEngine(EventManager *evm,
            std::vector<std::string> cassandra_ips,
            std::vector<int> cassandra_ports,
            const std::string & redis_ip, unsigned short redis_port,
            const std::string & redis_password,
            int max_tasks, int max_slice,
            const std::string & cassandra_name,
            const std::string & cassandra_password,
            const std::string & cluster_id);

    QueryEngine(EventManager *evm,
            const std::string & redis_ip, unsigned short redis_port,
            const std::string & redis_password, int max_tasks,
            int max_slice,
            const std::string  & cassandra_user,
            const std::string  & cassandra_password);

    // This constructor used only for test purpose
    QueryEngine(){}

    virtual ~QueryEngine();
    
    int
    QueryPrepare(QueryParams qp,
        std::vector<uint64_t> &chunk_size,
        bool & need_merge, bool & map_output,
        std::string& where, uint32_t& wterms,
        std::string& select, std::string& post,
        uint64_t& time_period, 
        std::string &table);

    // Query Execution of WHERE term
    bool
    QueryExecWhere(void * handle, QueryParams qp, uint32_t chunk,
            uint32_t or_number);

    void
    WhereQueryResult(AnalyticsQuery *q);

    // Query Execution of SELECT and post-processing
    bool
    QueryExec(void * handle, QueryParams qp, uint32_t chunk,
            const std::vector<query_result_unit_t> *wi);

    bool
    QueryAccumulate(QueryParams qp,
        const QEOpServerProxy::BufferT& input,
        QEOpServerProxy::BufferT& output);

    bool
    QueryFinalMerge(QueryParams qp,
        const std::vector<boost::shared_ptr<QEOpServerProxy::BufferT> >& inputs,
        QEOpServerProxy::BufferT& output);

    bool
    QueryFinalMerge(QueryParams qp,
        const std::vector<boost::shared_ptr<QEOpServerProxy::OutRowMultimapT> >& inputs,
        QEOpServerProxy::OutRowMultimapT& output);

    // Unit test function
    void QueryEngine_Test();

    void db_err_handler() {};
    TtlMap& GetTTlMap() { return ttlmap_; }
    const std::string & keyspace() { return keyspace_; }
    GenDb::DbTableStatistics stable_stats_;
    mutable tbb::mutex smutex_;
    bool GetCumulativeStats(std::vector<GenDb::DbTableInfo> *vdbti,
        GenDb::DbErrors *dbe, std::vector<GenDb::DbTableInfo> *vstats_dbti)
        const;
    bool GetDiffStats(std::vector<GenDb::DbTableInfo> *vdbti,
        GenDb::DbErrors *dbe, std::vector<GenDb::DbTableInfo> *vstats_dbti);
    bool GetCqlStats(cass::cql::DbStats *stats) const;
    GenDbIfPtr GetDbHandler() { return dbif_; }

private:
    GenDbIfPtr dbif_;
    boost::scoped_ptr<QEOpServerProxy> qosp_;
    EventManager *evm_;
    std::vector<int> cassandra_ports_;
    std::vector<std::string> cassandra_ips_;
    std::string cassandra_user_;
    std::string cassandra_password_;
    TtlMap ttlmap_;
    std::string keyspace_;
};

void get_uuid_stats_8tuple_from_json(const std::string &jsonline,
    boost::uuids::uuid *u, flow_stats *stats, flow_tuple *tuple);

#endif
