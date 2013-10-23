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
 *                |--WhereQuery-|
 *                              |-SetOperationUnit (for OR)
 *                                  |----
 *                                      |---SetOperationUnit (for AND)
 *                                          |
 *                                          |-DbQueryUnit
 *                                          |
 *                                          | .....
 *                                          |
 *                                          |-DbQueryUnit
 *                                      .....
 *                                      |---SetOperationUnit (for AND)
 *                                          |
 *                                          | .....
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
#include "gendb_if.h"
#include "../analytics/viz_message.h"
#include "json_parse.h"
#include "QEOpServerProxy.h"
#include "base/logging.h"
#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh.h>
#include <sandesh/sandesh_session.h>
#include <sandesh/sandesh_constants.h>
#include <sandesh/sandesh_ctrl_types.h>
#include <sandesh/sandesh_trace.h>
#include <sandesh/common/vns_constants.h>
#include "viz_constants.h"
#include "query_engine/qe_constants.h"
#include "query_engine/qe_types.h"
#include "sandesh/common/query_types.h"
#include <boost/regex.hpp>

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
        table = (((AnalyticsQuery *)(this->main_query))->table);\
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
        table = (((AnalyticsQuery *)(this->main_query))->table);\
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



// flow sample stats which is stored in Cassandra flow index tables
struct flow_stats {
    flow_stats(uint64_t ibytes=0, uint64_t ipkts=0, bool ishort_flow=false) : 
        bytes(ibytes), pkts(ipkts), short_flow(ishort_flow) {
    }
    uint64_t bytes;
    uint64_t pkts;
    bool short_flow;
    std::set<boost::uuids::uuid> flow_list;
};

// 8-tuple corresponding to a flow
struct flow_tuple {
    flow_tuple() : source_ip(0), dest_ip(0), protocol(0), source_port(0),
                   dest_port(0), direction(0) {
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
    uint32_t source_ip;
    uint32_t dest_ip;
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
    GenDb::DbDataValueVec info;

    // Following APIs will be invoked based on the table being queried

    // Get UUID from the info field
    void get_uuid(boost::uuids::uuid& u);
    // Get UUID and stats
    void get_uuid_stats(boost::uuids::uuid& u, flow_stats& stats);
    // Get UUID and stats and 8-tuple
    void get_uuid_stats_8tuple(boost::uuids::uuid& u,
            flow_stats& stats, flow_tuple& tuple);
    // for sorting and set operations
    bool operator<(const query_result_unit_t& rhs) const;

    // for printing
    friend std::ostream &operator<<(std::ostream &out, 
            query_result_unit_t&);

    static GenDb::GenDbIf *dbif; // just to access decode functions
} ;

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
    std::vector<query_result_unit_t> query_result;
}; 

// max number of entries to extract from db
#define MAX_DB_QUERY_ENTRIES 10000000
// This class provides interface for doing single index database query
class DbQueryUnit : public QueryUnit {
public:
    DbQueryUnit(QueryUnit *p_query, QueryUnit *m_query):
        QueryUnit(p_query, m_query) 
        { cr.count = MAX_DB_QUERY_ENTRIES; 
            t_only_col = false; t_only_row = false;};
    virtual query_status_t process_query();


    // portion of column family name other than T1
    std::string cfname;
    // all type of match operations can be representated as 
    // getrangeslice operations on Cassandra DB. For e.g. 
    // NOT_EQUAL operation will be two getrangeslice operation
    GenDb::ColumnNameRange cr;
    // row key suffix will be used to append DIR to flow 
    // series/records query
    GenDb::DbDataValue row_key_suffix;
    bool t_only_col;    // only T is in column name
    bool t_only_row;    // only T2 is in row key
};

// This class provides interface to process SET operations involved in the 
// WHERE part of the query
// leaf node's child nodes are DbQueryUnit
class SetOperationUnit: public QueryUnit {
public:
    SetOperationUnit(QueryUnit *p_query, QueryUnit *m_query):
        QueryUnit(p_query, m_query), set_operation(UNION_OP), 
        is_leaf_node(false) {};
    virtual query_status_t process_query();

    enum {UNION_OP, INTERSECTION_OP} set_operation;
    bool is_leaf_node;

private:
    void or_operation();
    void and_operation();
};


// Where processing class
    // Result is available for SELECT processing in query_result field
    // It will be an array of timestamp and 
    // UUID is in case of messages/object-trace WHERE queries
    // stats+UUID after database queries for flow-records WHERE queries
    // stats+UUID+8-tuple for flow-series WHERE query

class WhereQuery : public QueryUnit {
public:
    WhereQuery(std::string where_json_string, int direction,
            QueryUnit *main_query);
    virtual query_status_t process_query();

    // 0 is for egress and 1 for ingress
    int32_t direction_ing;

private:
    // Create UUID to 8-tuple map by querying special flow table for
    // the given time range
    void create_uuid_tuple_map(
            std::map<boost::uuids::uuid, GenDb::DbDataValueVec>& uuid_map);
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
    AVG = 3,
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


// following data structure does the processing of SELECT portion of query
class SelectQuery : public QueryUnit {
public:
    SelectQuery(QueryUnit *main_query,
            std::map<std::string, std::string> json_api_data);

    virtual query_status_t process_query();

    // Query related fields
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

private:
    // 
    // Object table query
    //
    bool process_object_query_specific_select_params(
                        const std::string& sel_field,
                        std::map<std::string, GenDb::DbDataValue>& col_res_map,
                        std::map<std::string, std::string>& cmap);
 
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
            const flow_stats *avg_stats, const size_t flow_count = 0);
    
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
    typedef std::set<flow_tuple> fs_flow_tuple_list_t;
    fs_flow_tuple_list_t fs_flow_tuple_list_;
    void process_fs_query_with_tuple_fields(const uint64_t&,
            const boost::uuids::uuid&, const flow_stats&, const flow_tuple&);
    void populate_fs_query_result_with_tuple_fields();

    // 4. SELECT with T 
    typedef std::set<uint64_t> fs_time_t;
    fs_time_t fs_time_list_;
    void process_fs_query_with_time(const uint64_t&,
            const boost::uuids::uuid&, const flow_stats&, const flow_tuple&);
    void populate_fs_query_result_with_time();
    
    // 5. SELECT with T=<granularity>
    typedef std::set<uint64_t> fs_ts_t;
    fs_ts_t fs_ts_list_;
    void process_fs_query_with_ts(const uint64_t&,
            const boost::uuids::uuid&, const flow_stats&, const flow_tuple&);
    void populate_fs_query_result_with_ts();

    // 6. SELECT with T, flow tuple fields
    typedef std::map<const uint64_t, std::set<flow_tuple> > fs_time_tuple_map_t;
    fs_time_tuple_map_t fs_time_tuple_map_;
    void process_fs_query_with_time_tuple_fields(const uint64_t&,
            const boost::uuids::uuid&, const flow_stats&, const flow_tuple&);
    void populate_fs_query_result_with_time_tuple_fields();

    // 7. SELECT with T, stats fields 
    typedef std::map<const uint64_t, flow_stats> fs_time_stats_map_t;
    fs_time_stats_map_t fs_time_stats_map_;
    void process_fs_query_with_time_stats_fields(const uint64_t&,
            const boost::uuids::uuid&, const flow_stats&, const flow_tuple&);
    void populate_fs_query_result_with_time_stats_fields();

    // 8. SELECT with T, flow tuple, stats
    typedef std::map<const flow_tuple, fs_time_stats_map_t> 
        fs_time_tuple_stats_map_t;
    fs_time_tuple_stats_map_t fs_time_tuple_stats_map_;
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

    // filter list
    std::vector<filter_match_t> filter_list;

    // Whether to sort the table or not
    bool sorted;
    // Type of sorting
    sort_op sorting_type;
    // fields to sort on (these are actual Cassandra column names)
    std::vector<sort_field_t> sort_fields;
    int limit; // number of entries 

    // result after post processing

    std::auto_ptr<BufT> result_;

    bool sort_field_comparator(const std::map<std::string, std::string>& lhs,
                               const std::map<std::string, std::string>& rhs);

    // compare flow records based on UUID
    static bool flow_record_comparator(const QEOpServerProxy::OutRowT& lhs,
                               const QEOpServerProxy::OutRowT& rhs);

    bool merge_processing(
        const QEOpServerProxy::BufferT& input, 
        QEOpServerProxy::BufferT& output);
    bool final_merge_processing(
const std::vector<boost::shared_ptr<QEOpServerProxy::BufferT> >& inputs,
                        QEOpServerProxy::BufferT& output);
private:
    bool flowseries_merge_processing(
                const std::vector<QEOpServerProxy::OutRowT> *raw_result,
                std::vector<QEOpServerProxy::OutRowT> *merged_result);
    void fs_merge_stats(const QEOpServerProxy::OutRowT& input, 
                        QEOpServerProxy::OutRowT& output);
    void fs_stats_merge_processing(
                const std::vector<QEOpServerProxy::OutRowT> *input,
                std::vector<QEOpServerProxy::OutRowT> *output);
    void fs_tuple_stats_merge_processing(
                const std::vector<QEOpServerProxy::OutRowT> *input,
                std::vector<QEOpServerProxy::OutRowT> *output);
};

class AnalyticsQuery: public QueryUnit {
public:
    AnalyticsQuery(GenDb::GenDbIf *db_if, std::string qid,
    std::map<std::string, std::string>& json_api_data, 
    uint64_t analytics_start_time);

    AnalyticsQuery(std::string qid, std::map<std::string, 
            std::string>& json_api_data, uint64_t analytics_start_time,
            EventManager *evm, const std::string & cassandra_ip, 
            unsigned short cassandra_port);

    AnalyticsQuery(std::string qid, std::map<std::string, 
            std::string>& json_api_data, uint64_t analytics_start_time,
            EventManager *evm, const std::string & cassandra_ip, 
            unsigned short cassandra_port, int batch, int total_batches);


    virtual query_status_t process_query();

        // Interface to Cassandra
    GenDb::GenDbIf *dbif;
    boost::scoped_ptr<GenDb::GenDbIf> dbif_;
    void db_err_handler() {};
    
    //Query related fields

    // Query Id
    std::string query_id;
    
    // Analytics table to query
    std::string table; 

    // if the req is to get objectids, the following will contain the key
    std::string object_value_key; 

    // query start time requested by the user
    uint64_t req_from_time;
    // start time of the time range for the queried records.
    // If the start time requested by the user is earlier than the 
    // analytics start time, then this field holds the analytics start time.
    // Else, from_time is same as req_from_time.
    uint64_t from_time; 
    // query end time requested by the user
    uint64_t req_end_time;
    // end time of the time range for the queried records.
    // If the end time requested by the user is later than the time @ which the 
    // query was received, then this field holds the time @ which the query
    // was received. Else, end_time is same as req_end_time.
    uint64_t end_time; 

    std::string sandesh_moduleid; // module id of the query engine itself
    bool  filter_qe_logs;   // whether to filter query engine logs

    // final result of the query
    std::auto_ptr<QEOpServerProxy::BufferT> final_result;

    std::map<std::string, std::string> json_api_data_; 
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
    // this is for merge between multiple instances running on same core
    bool merge_processing(const QEOpServerProxy::BufferT& input,
                            QEOpServerProxy::BufferT& output);
    // this is for merge between instances running on multiple cores
    bool final_merge_processing(
const std::vector<boost::shared_ptr<QEOpServerProxy::BufferT> >& inputs,
                        QEOpServerProxy::BufferT& output);
    // this is to get parallelization details once the query is parsed
    void get_query_details(bool& merge_needed, std::vector<uint64_t>& chunk_sizes, int& parse_status);


    // validation functions
    bool is_valid_from_field(const std::string& from_field);
    bool is_object_table_query();
    bool is_valid_select_field(const std::string& select_field);
    bool is_valid_where_field(const std::string& where_field);
    bool is_valid_sort_field(const std::string& sort_field);
    std::string get_column_field_datatype(const std::string& col_field);
    bool is_flow_query(); // either flow-series or flow-records query
    bool is_query_parallelized() { return parallelize_query_; }

    private:
    bool parallelize_query_;
    // Init function
    void Init(GenDb::GenDbIf *db_if, std::string qid,
    std::map<std::string, std::string>& json_api_data, 
    uint64_t analytics_start_time);
    bool can_parallelize_query();
};

// limit on the size of query result we can handle
static const int query_result_size_limit = 25000000;

// main class
class QueryEngine {
public:
    static const uint64_t StartTimeDiffInSec = 12*3600;

    struct QueryParams {
        QueryParams(std::string qi, 
                std::map<std::string, std::string> qu, uint32_t ch, uint64_t tm) :
            qid(qi), terms(qu), maxChunks(ch), query_starttm(tm) {}  
        QueryParams() {}
        std::string qid;
        std::map<std::string, std::string> terms;
        uint32_t maxChunks;
        uint64_t query_starttm;
    };
    uint64_t stime;

    QueryEngine(EventManager *evm,
            const std::string & cassandra_ip, unsigned short cassandra_port,
            const std::string & redis_ip, unsigned short redis_port);

    QueryEngine(EventManager *evm,
            const std::string & redis_ip, unsigned short redis_port);

    int
    QueryPrepare(QueryParams qp,
        std::vector<uint64_t> &chunk_size, bool & need_merge);

    bool
    QueryExec(void * handle, QueryParams qp, uint32_t chunk);

    bool
    QueryAccumulate(QueryParams qp,
        const QEOpServerProxy::BufferT& input,
        QEOpServerProxy::BufferT& output);

    bool
    QueryFinalMerge(QueryParams qp,
        const std::vector<boost::shared_ptr<QEOpServerProxy::BufferT> >& inputs,
        QEOpServerProxy::BufferT& output);

    // Unit test function
    void QueryEngine_Test();

    void db_err_handler() {};
private:
    boost::scoped_ptr<GenDb::GenDbIf> dbif_;
    boost::scoped_ptr<QEOpServerProxy> qosp_;
    EventManager *evm_;
    unsigned short cassandra_port_;
    std::string cassandra_ip_;
};

#endif
