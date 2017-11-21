/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef JSON_PARSE_H_
#define JSON_PARSE_H_

/************************************
 *********** WARNING ****************
 * Following section is derived from
 * "query_rest.idl" file
 * DO NOT EDIT this section before 
 * cross-referencing query_rest.idl
 * file
 * **********************************/

enum match_op {
    EQUAL = 1,
    NOT_EQUAL = 2,
    IN_RANGE = 3,
    NOT_IN_RANGE = 4,   // not supported currently
    // following are only for numerical column fields
    LEQ = 5, // column value is less than or equal to filter value
    GEQ = 6, // column value is greater than or equal to filter value
    PREFIX = 7, // column value has the "value" field as prefix
    REGEX_MATCH = 8, // for filters only
    CONTAINS = 9
};

enum sort_op {
    ASCENDING = 1,
    DESCENDING = 2,
};

enum flow_dir_t {
    EGRESS = 0,
    INGRESS = 1
};

#define WHERE_MATCH_TERM                "term"

#define QUERY               "query"
#define QUERY_TABLE             "table"
#define QUERY_START_TIME        "start_time"
#define QUERY_END_TIME          "end_time"
#define QUERY_SELECT            "select_fields"
#define QUERY_WHERE             "where"
#define WHERE_MATCH_NAME            "name"
#define WHERE_MATCH_SUFFIX          "suffix"
#define WHERE_MATCH_VALUE           "value"
#define WHERE_MATCH_VALUE2          "value2"
#define WHERE_MATCH_OP              "op"
#define QUERY_SORT_OP           "sort"
#define QUERY_SORT_FIELDS       "sort_fields"
#define QUERY_LIMIT             "limit"
#define QUERY_AGG_STATS         "agg_stats"
#define QUERY_AGG_STATS_OP          "agg_op"
#define QUERY_AGG_STATS_TYPE        "stat_type"
#define QUERY_FLOW_DIR          "dir"
#define QUERY_SESSION_IS_SI         "is_service_instance"
#define QUERY_SESSION_TYPE          "session_type"
#define QUERY_FILTER            "filter"
#define QUERY_TABLE_SCHEMA                "table_schema"
#define QUERY_TABLE_SCHEMA_DATATYPE        "datatype"
#define QUERY_TABLE_SCHEMA_INDEX        "index"
#define QUERY_TABLE_SCHEMA_SUFFIXES        "suffixes"

 
/**** END DERIVED SECTION ***********/

/* SELECT fields */
#define TIMESTAMP_FIELD "T"
#define TIMESTAMP_GRANULARITY "T="
#define SELECT_PACKETS "packets"
#define SELECT_BYTES "bytes"
#ifdef USE_SESSION
#define SELECT_SUM_PACKETS "SUM(packets)"
#define SELECT_SUM_BYTES "SUM(bytes)"
#else
#define SELECT_SUM_PACKETS "sum(packets)"
#define SELECT_SUM_BYTES "sum(bytes)"
#endif
#define SELECT_FLOW_CLASS_ID "flow_class_id"
#define SELECT_FLOW_COUNT "flow_count"
#define SELECT_SESSION_CLASS_ID "session_class_id"
#define SELECT_SESSION_CLASS_T "CLASS(T)"
#define SELECT_SESSION_CLASS_TS "CLASS(T=)"
#define SELECT_SAMPLE_COUNT "sample_count"
#define SELECT_SESSION_SAMPLE_COUNT_T "COUNT(T)"
#define SELECT_SESSION_SAMPLE_COUNT_TS "COUNT(T=)"
/*** Query String to Cassandra column name mappings
 */
std::string get_column_name(std::string query_string);
std::string get_query_string(std::string column_name);

/***
 * Key identifying the object
 */
#define OBJECTID "ObjectId"

#endif
