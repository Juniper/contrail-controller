/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "vizd_table_desc.h"

#include <boost/assign/list_of.hpp>
#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh.h>
#include "viz_constants.h"
#include <database/gendb_constants.h>

std::vector<GenDb::NewCf> vizd_tables;
std::vector<GenDb::NewCf> vizd_flow_tables;
std::vector<GenDb::NewCf> vizd_stat_tables;
FlowTypeMap flow_msg2type_map;

void init_vizd_tables(bool use_cql) {
    static bool init_done = false;

    if (init_done)
        return;
    init_done = true;

// usage of GenDb::_DbDataType_VALUES_TO_NAMES[GenDb::DbDataType::LexicalUUIDType])) didn't
// compile, hence using raw values
    vizd_tables = boost::assign::list_of<GenDb::NewCf>
        (GenDb::NewCf(g_viz_constants.COLLECTOR_GLOBAL_TABLE,
                      boost::assign::list_of
                      (GenDb::DbDataType::LexicalUUIDType),
                      boost::assign::map_list_of
                      (g_viz_constants.SOURCE,
                       GenDb::DbDataType::UTF8Type)
                      (g_viz_constants.NAMESPACE,
                       GenDb::DbDataType::UTF8Type)
                      (g_viz_constants.MODULE,
                       GenDb::DbDataType::UTF8Type)
                      (g_viz_constants.INSTANCE_ID,
                       GenDb::DbDataType::UTF8Type)
                      (g_viz_constants.NODE_TYPE,
                       GenDb::DbDataType::UTF8Type)
                      (g_viz_constants.CONTEXT,
                       GenDb::DbDataType::UTF8Type)
                      (g_viz_constants.TIMESTAMP,
                       GenDb::DbDataType::Unsigned64Type)
                      (g_viz_constants.CATEGORY,
                       GenDb::DbDataType::UTF8Type)
                      (g_viz_constants.LEVEL,
                       GenDb::DbDataType::Unsigned32Type)
                      (g_viz_constants.MESSAGE_TYPE,
                       GenDb::DbDataType::UTF8Type)
                      (g_viz_constants.SEQUENCE_NUM,
                       GenDb::DbDataType::Unsigned32Type)
                      (g_viz_constants.VERSION,
                       GenDb::DbDataType::Unsigned32Type)
                      (g_viz_constants.SANDESH_TYPE,
                       GenDb::DbDataType::Unsigned8Type)
                      (g_viz_constants.IPADDRESS,
                       GenDb::DbDataType::UTF8Type)
                      (g_viz_constants.PID,
                       GenDb::DbDataType::Unsigned32Type)
                      (g_viz_constants.DATA,
                       GenDb::DbDataType::UTF8Type)))
        (GenDb::NewCf(g_viz_constants.MESSAGE_TABLE_SOURCE,
                      boost::assign::list_of
                      (GenDb::DbDataType::Unsigned32Type)
#ifdef USE_CASSANDRA_CQL
                      (GenDb::DbDataType::Unsigned32Type),
                      boost::assign::list_of
                      (GenDb::DbDataType::UTF8Type)
                      (GenDb::DbDataType::Unsigned32Type)
                      (GenDb::DbDataType::LexicalUUIDType),
                      std::vector<GenDb::DbDataType::type>()))
#else
                      (GenDb::DbDataType::UTF8Type),
                      boost::assign::list_of
                      (GenDb::DbDataType::Unsigned32Type),
                      boost::assign::list_of
                      (GenDb::DbDataType::LexicalUUIDType)))
#endif
        (GenDb::NewCf(g_viz_constants.MESSAGE_TABLE_MODULE_ID,
                      boost::assign::list_of
                      (GenDb::DbDataType::Unsigned32Type)
#ifdef USE_CASSANDRA_CQL
                      (GenDb::DbDataType::Unsigned32Type),
                      boost::assign::list_of
                      (GenDb::DbDataType::UTF8Type)
                      (GenDb::DbDataType::Unsigned32Type)
                      (GenDb::DbDataType::LexicalUUIDType),
                      std::vector<GenDb::DbDataType::type>()))
#else
                      (GenDb::DbDataType::UTF8Type),
                      boost::assign::list_of
                      (GenDb::DbDataType::Unsigned32Type),
                      boost::assign::list_of
                      (GenDb::DbDataType::LexicalUUIDType)))
#endif
        (GenDb::NewCf(g_viz_constants.MESSAGE_TABLE_MESSAGE_TYPE,
                      boost::assign::list_of
                      (GenDb::DbDataType::Unsigned32Type)
#ifdef USE_CASSANDRA_CQL
                      (GenDb::DbDataType::Unsigned32Type),
                      boost::assign::list_of
                      (GenDb::DbDataType::UTF8Type)
                      (GenDb::DbDataType::Unsigned32Type)
                      (GenDb::DbDataType::LexicalUUIDType),
                      std::vector<GenDb::DbDataType::type>()))
#else
                      (GenDb::DbDataType::UTF8Type),
                      boost::assign::list_of
                      (GenDb::DbDataType::Unsigned32Type),
                      boost::assign::list_of
                      (GenDb::DbDataType::LexicalUUIDType)))
#endif
        (GenDb::NewCf(g_viz_constants.MESSAGE_TABLE_CATEGORY,
                      boost::assign::list_of
                      (GenDb::DbDataType::Unsigned32Type)
#ifdef USE_CASSANDRA_CQL
                      (GenDb::DbDataType::Unsigned32Type),
                      boost::assign::list_of
                      (GenDb::DbDataType::UTF8Type)
                      (GenDb::DbDataType::Unsigned32Type)
                      (GenDb::DbDataType::LexicalUUIDType),
                      std::vector<GenDb::DbDataType::type>()))
#else
                      (GenDb::DbDataType::UTF8Type),
                      boost::assign::list_of
                      (GenDb::DbDataType::Unsigned32Type),
                      boost::assign::list_of
                      (GenDb::DbDataType::LexicalUUIDType)))
#endif
        (GenDb::NewCf(g_viz_constants.MESSAGE_TABLE_TIMESTAMP,
                      boost::assign::list_of
                      (GenDb::DbDataType::Unsigned32Type)
#ifdef USE_CASSANDRA_CQL
                      (GenDb::DbDataType::Unsigned32Type),
                      boost::assign::list_of
                      (GenDb::DbDataType::Unsigned32Type)
                      (GenDb::DbDataType::LexicalUUIDType),
                      std::vector<GenDb::DbDataType::type>()))
#else
                      boost::assign::list_of
                      (GenDb::DbDataType::Unsigned32Type),
                      boost::assign::list_of
                      (GenDb::DbDataType::LexicalUUIDType)))
#endif
        (GenDb::NewCf(g_viz_constants.OBJECT_VALUE_TABLE,
                      boost::assign::list_of
                      (GenDb::DbDataType::Unsigned32Type)
                      (GenDb::DbDataType::UTF8Type),
                      boost::assign::list_of
                      (GenDb::DbDataType::Unsigned32Type),
                      boost::assign::list_of
                      (GenDb::DbDataType::UTF8Type)))
        (GenDb::NewCf(g_viz_constants.SYSTEM_OBJECT_TABLE,
                      boost::assign::list_of
                      (GenDb::DbDataType::UTF8Type),
                      boost::assign::map_list_of
                      (g_viz_constants.SYSTEM_OBJECT_START_TIME,
                       GenDb::DbDataType::Unsigned64Type)
                      (g_viz_constants.SYSTEM_OBJECT_FLOW_START_TIME,
                       GenDb::DbDataType::Unsigned64Type)
                      (g_viz_constants.SYSTEM_OBJECT_MSG_START_TIME,
                       GenDb::DbDataType::Unsigned64Type)
                      (g_viz_constants.SYSTEM_OBJECT_STAT_START_TIME,
                       GenDb::DbDataType::Unsigned64Type)
                      (g_viz_constants.SYSTEM_OBJECT_FLOW_DATA_TTL,
                       GenDb::DbDataType::Unsigned64Type)
                      (g_viz_constants.SYSTEM_OBJECT_STATS_DATA_TTL,
                       GenDb::DbDataType::Unsigned64Type)
                      (g_viz_constants.SYSTEM_OBJECT_CONFIG_AUDIT_TTL,
                       GenDb::DbDataType::Unsigned64Type)
                      (g_viz_constants.SYSTEM_OBJECT_GLOBAL_DATA_TTL,
                       GenDb::DbDataType::Unsigned64Type)
                      ))
        (GenDb::NewCf(g_viz_constants.OBJECT_TABLE,
                      boost::assign::list_of
                      (GenDb::DbDataType::Unsigned32Type)
                      (GenDb::DbDataType::Unsigned8Type)
                      (GenDb::DbDataType::UTF8Type),
                      boost::assign::list_of
                      (GenDb::DbDataType::UTF8Type)
                      (GenDb::DbDataType::Unsigned32Type),
                      boost::assign::list_of
                      (GenDb::DbDataType::LexicalUUIDType)))
        (GenDb::NewCf(g_viz_constants.MESSAGE_TABLE_KEYWORD,
                      boost::assign::list_of
                      (GenDb::DbDataType::Unsigned32Type)
#ifdef USE_CASSANDRA_CQL
                      (GenDb::DbDataType::Unsigned32Type),
                      boost::assign::list_of
                      (GenDb::DbDataType::UTF8Type)
                      (GenDb::DbDataType::Unsigned32Type)
                      (GenDb::DbDataType::LexicalUUIDType),
                      std::vector<GenDb::DbDataType::type>()))
#else
                      (GenDb::DbDataType::UTF8Type),
                      boost::assign::list_of
                      (GenDb::DbDataType::Unsigned32Type),
                      boost::assign::list_of
                      (GenDb::DbDataType::LexicalUUIDType)))
#endif
        ;

/* flow records table and flow series table are created in the code path itself
 * the following are flow index tables - for SVN:SIP, DVN:DIP, ...
 *
 */
    GenDb::DbDataTypeVec flow_series_value_cql = boost::assign::list_of
        (GenDb::DbDataType::UTF8Type);

    GenDb::DbDataTypeVec flow_series_value_thrift = boost::assign::list_of
        (GenDb::DbDataType::Unsigned64Type)
        (GenDb::DbDataType::Unsigned64Type)
        (GenDb::DbDataType::Unsigned8Type)
        (GenDb::DbDataType::LexicalUUIDType)
        (GenDb::DbDataType::UTF8Type)
        (GenDb::DbDataType::UTF8Type)
        (GenDb::DbDataType::UTF8Type)
        (GenDb::DbDataType::Unsigned32Type)
        (GenDb::DbDataType::Unsigned32Type)
        (GenDb::DbDataType::Unsigned8Type)
        (GenDb::DbDataType::Unsigned16Type)
        (GenDb::DbDataType::Unsigned16Type)
        (GenDb::DbDataType::UTF8Type);

    GenDb::DbDataTypeVec flow_series_value;
    if (use_cql) {
        flow_series_value = flow_series_value_cql;
    } else {
        flow_series_value = flow_series_value_thrift;
    }

    vizd_flow_tables =
        boost::assign::list_of<GenDb::NewCf>

 /* (SVN,SIP) index table
 * This index table maintains index on flow record stats samples for queries
 * based on source vn and source ip address
 *
 * Row Key: T2+DIR 
 * T2 is the MSB timestamp portion as defined earlier for message/object index
 * tables and DIR is the value of direction field for the flow (either “INGRESS”
 * or “EGRESS”)
 *
 * Column Name: Composite column composed of following (in the order given):
 * SVN: Id of the source VN of the flow
 * SIP: Source IP address of the flow
 * T1: LSB portion of the timestamp
 *
 * Column Value: Column value is a composite value made up by appending
 * following values:
 * UUID: UUID corresponding to the flow record
 * Pkt-Stats: # of packets seen for this flow
 * Byte-Stats: # of bytes seen for this flow
 */
        (GenDb::NewCf(g_viz_constants.FLOW_TABLE,
                      boost::assign::list_of
                      (GenDb::DbDataType::LexicalUUIDType),
                      boost::assign::map_list_of
                      (g_viz_constants.FlowRecordNames[FlowRecordFields::FLOWREC_VROUTER],
                       GenDb::DbDataType::UTF8Type)
                      (g_viz_constants.FlowRecordNames[FlowRecordFields::FLOWREC_DIRECTION_ING],
                       GenDb::DbDataType::Unsigned8Type)
                      (g_viz_constants.FlowRecordNames[FlowRecordFields::FLOWREC_SOURCEVN],
                       GenDb::DbDataType::UTF8Type)
                      (g_viz_constants.FlowRecordNames[FlowRecordFields::FLOWREC_SOURCEIP],
#ifdef USE_CASSANDRA_CQL
                       GenDb::DbDataType::InetType)
#else // USE_CASSANDRA_CQL
                       GenDb::DbDataType::Unsigned32Type)
#endif // !USE_CASSANDRA_CQL
                      (g_viz_constants.FlowRecordNames[FlowRecordFields::FLOWREC_DESTVN],
                       GenDb::DbDataType::UTF8Type)
                      (g_viz_constants.FlowRecordNames[FlowRecordFields::FLOWREC_DESTIP],
#ifdef USE_CASSANDRA_CQL
                       GenDb::DbDataType::InetType)
#else // USE_CASSANDRA_CQL
                       GenDb::DbDataType::Unsigned32Type)
#endif // !USE_CASSANDRA_CQL
                      (g_viz_constants.FlowRecordNames[FlowRecordFields::FLOWREC_PROTOCOL],
                       GenDb::DbDataType::Unsigned8Type)
                      (g_viz_constants.FlowRecordNames[FlowRecordFields::FLOWREC_SPORT],
                       GenDb::DbDataType::Unsigned16Type)
                      (g_viz_constants.FlowRecordNames[FlowRecordFields::FLOWREC_DPORT],
                       GenDb::DbDataType::Unsigned16Type)
                      (g_viz_constants.FlowRecordNames[FlowRecordFields::FLOWREC_TOS],
                       GenDb::DbDataType::Unsigned8Type)
                      (g_viz_constants.FlowRecordNames[FlowRecordFields::FLOWREC_TCP_FLAGS],
                       GenDb::DbDataType::Unsigned8Type)
                      (g_viz_constants.FlowRecordNames[FlowRecordFields::FLOWREC_VM],
                       GenDb::DbDataType::UTF8Type)
                      (g_viz_constants.FlowRecordNames[FlowRecordFields::FLOWREC_INPUT_INTERFACE],
                       GenDb::DbDataType::UTF8Type)
                      (g_viz_constants.FlowRecordNames[FlowRecordFields::FLOWREC_OUTPUT_INTERFACE],
                       GenDb::DbDataType::UTF8Type)
                      (g_viz_constants.FlowRecordNames[FlowRecordFields::FLOWREC_MPLS_LABEL],
                       GenDb::DbDataType::Unsigned32Type)
                      (g_viz_constants.FlowRecordNames[FlowRecordFields::FLOWREC_REVERSE_UUID],
                       GenDb::DbDataType::UTF8Type)
                      (g_viz_constants.FlowRecordNames[FlowRecordFields::FLOWREC_SETUP_TIME],
                       GenDb::DbDataType::Unsigned64Type)
                      (g_viz_constants.FlowRecordNames[FlowRecordFields::FLOWREC_TEARDOWN_TIME],
                       GenDb::DbDataType::Unsigned64Type)
                      (g_viz_constants.FlowRecordNames[FlowRecordFields::FLOWREC_MIN_INTERARRIVAL],
                       GenDb::DbDataType::Unsigned32Type)
                      (g_viz_constants.FlowRecordNames[FlowRecordFields::FLOWREC_MAX_INTERARRIVAL],
                       GenDb::DbDataType::Unsigned32Type)
                      (g_viz_constants.FlowRecordNames[FlowRecordFields::FLOWREC_MEAN_INTERARRIVAL],
                       GenDb::DbDataType::Unsigned32Type)
                      (g_viz_constants.FlowRecordNames[FlowRecordFields::FLOWREC_STDDEV_INTERARRIVAL],
                       GenDb::DbDataType::Unsigned32Type)
                      (g_viz_constants.FlowRecordNames[FlowRecordFields::FLOWREC_BYTES],
                       GenDb::DbDataType::Unsigned64Type)
                      (g_viz_constants.FlowRecordNames[FlowRecordFields::FLOWREC_PACKETS],
                       GenDb::DbDataType::Unsigned64Type)
                      (g_viz_constants.FlowRecordNames[FlowRecordFields::FLOWREC_DATA_SAMPLE],
                       GenDb::DbDataType::UTF8Type)
                      (g_viz_constants.FlowRecordNames[FlowRecordFields::FLOWREC_ACTION],
                       GenDb::DbDataType::UTF8Type)
                      (g_viz_constants.FlowRecordNames[FlowRecordFields::FLOWREC_SG_RULE_UUID],
                       GenDb::DbDataType::LexicalUUIDType)
                      (g_viz_constants.FlowRecordNames[FlowRecordFields::FLOWREC_NW_ACE_UUID],
                       GenDb::DbDataType::LexicalUUIDType)
                      (g_viz_constants.FlowRecordNames[FlowRecordFields::FLOWREC_VROUTER_IP],
                       GenDb::DbDataType::UTF8Type)
                      (g_viz_constants.FlowRecordNames[FlowRecordFields::FLOWREC_OTHER_VROUTER_IP],
                       GenDb::DbDataType::UTF8Type)
                      (g_viz_constants.FlowRecordNames[FlowRecordFields::FLOWREC_UNDERLAY_PROTO],
                       GenDb::DbDataType::Unsigned16Type)
                      (g_viz_constants.FlowRecordNames[FlowRecordFields::FLOWREC_UNDERLAY_SPORT],
                       GenDb::DbDataType::Unsigned16Type)
                      (g_viz_constants.FlowRecordNames[FlowRecordFields::FLOWREC_VMI_UUID],
                       GenDb::DbDataType::LexicalUUIDType)
                      (g_viz_constants.FlowRecordNames[FlowRecordFields::FLOWREC_DROP_REASON],
                       GenDb::DbDataType::UTF8Type)
                     ))

        /* (SVN, SIP) index  table */
        (GenDb::NewCf(g_viz_constants.FLOW_TABLE_SVN_SIP,
                      boost::assign::list_of
                      (GenDb::DbDataType::Unsigned32Type)
                      (GenDb::DbDataType::Unsigned8Type)
                      (GenDb::DbDataType::Unsigned8Type),
                      boost::assign::list_of
                      (GenDb::DbDataType::UTF8Type)
#ifdef USE_CASSANDRA_CQL
                      (GenDb::DbDataType::InetType)
#else // USE_CASSANDRA_CQL
                      (GenDb::DbDataType::Unsigned32Type)
#endif // !USE_CASSANDRA_CQL
                      (GenDb::DbDataType::Unsigned32Type)
                      (GenDb::DbDataType::LexicalUUIDType),
                      flow_series_value))
        /* (DVN,DIP) index table */
        (GenDb::NewCf(g_viz_constants.FLOW_TABLE_DVN_DIP,
                  boost::assign::list_of
                  (GenDb::DbDataType::Unsigned32Type)
                  (GenDb::DbDataType::Unsigned8Type)
                  (GenDb::DbDataType::Unsigned8Type),
                  boost::assign::list_of
                  (GenDb::DbDataType::UTF8Type)
#ifdef USE_CASSANDRA_CQL
                  (GenDb::DbDataType::InetType)
#else // USE_CASSANDRA_CQL
                  (GenDb::DbDataType::Unsigned32Type)
#endif // !USE_CASSANDRA_CQL
                  (GenDb::DbDataType::Unsigned32Type)
                  (GenDb::DbDataType::LexicalUUIDType),
                  flow_series_value))
        /* (PROT, SP) index table */
        (GenDb::NewCf(g_viz_constants.FLOW_TABLE_PROT_SP,
                boost::assign::list_of
                (GenDb::DbDataType::Unsigned32Type)
                (GenDb::DbDataType::Unsigned8Type)
                (GenDb::DbDataType::Unsigned8Type),
                boost::assign::list_of
                (GenDb::DbDataType::Unsigned8Type)
                (GenDb::DbDataType::Unsigned16Type)
                (GenDb::DbDataType::Unsigned32Type)
                (GenDb::DbDataType::LexicalUUIDType),
                flow_series_value))
        /* (PROT, DP) index table */
        (GenDb::NewCf(g_viz_constants.FLOW_TABLE_PROT_DP,
                      boost::assign::list_of
                      (GenDb::DbDataType::Unsigned32Type)
                      (GenDb::DbDataType::Unsigned8Type)
                      (GenDb::DbDataType::Unsigned8Type),
                      boost::assign::list_of
                      (GenDb::DbDataType::Unsigned8Type)
                      (GenDb::DbDataType::Unsigned16Type)
                      (GenDb::DbDataType::Unsigned32Type)
                      (GenDb::DbDataType::LexicalUUIDType),
                      flow_series_value))
        /* (VROUTER) index table */
        (GenDb::NewCf(g_viz_constants.FLOW_TABLE_VROUTER,
                      boost::assign::list_of
                      (GenDb::DbDataType::Unsigned32Type)
                      (GenDb::DbDataType::Unsigned8Type)
                      (GenDb::DbDataType::Unsigned8Type),
                      boost::assign::list_of
                      (GenDb::DbDataType::UTF8Type)
                      (GenDb::DbDataType::Unsigned32Type)
                      (GenDb::DbDataType::LexicalUUIDType),
                      flow_series_value))
        ;

/*  For Stat Tables that have a single tag
 *    RowKey      : T2, Partition #, StatName, StatAttr, TagName
 */
    GenDb::DbDataTypeVec stat_row_onetag = boost::assign::list_of
            (GenDb::DbDataType::Unsigned32Type)
            (GenDb::DbDataType::Unsigned8Type)
            (GenDb::DbDataType::UTF8Type)
            (GenDb::DbDataType::UTF8Type)
            (GenDb::DbDataType::UTF8Type)
        ;

/*  For Stat Tables that have two tags
 *    RowKey      : T2, Partition #, StatName, StatAttr, PrefixTagName, SuffixTagName
 */
    GenDb::DbDataTypeVec stat_row_twotag = boost::assign::list_of
            (GenDb::DbDataType::Unsigned32Type)
            (GenDb::DbDataType::Unsigned8Type)
            (GenDb::DbDataType::UTF8Type)
            (GenDb::DbDataType::UTF8Type)
            (GenDb::DbDataType::UTF8Type)
            (GenDb::DbDataType::UTF8Type)
        ;

    vizd_stat_tables =
        boost::assign::list_of<GenDb::NewCf>
/* Stats Table by String,Str tag
 * The schema is as follows:
 *   RowKey      : T2, Partition #, StatName, StatAttr, PrefixTagName, SuffixTagName
 *   ColumnName  : String TagValue, String Tag2 Value, T1, SampleUUID
 *   ColumnValue : JSON of attrib:value */
        (GenDb::NewCf(g_viz_constants.STATS_TABLE_BY_STR_STR_TAG,
                      stat_row_twotag,
                      boost::assign::list_of
                      (GenDb::DbDataType::UTF8Type)
                      (GenDb::DbDataType::UTF8Type)
                      (GenDb::DbDataType::Unsigned32Type)
                      (GenDb::DbDataType::LexicalUUIDType),
                      boost::assign::list_of
                      (GenDb::DbDataType::UTF8Type)
                     ))
/* Stats Table by String,U64 tag
 * The schema is as follows:
 *   RowKey      : T2, Partition #, StatName, StatAttr, PrefixTagName, SuffixTagName
 *   ColumnName  : String TagValue, U64 Tag2 Value, T1, SampleUUID
 *   ColumnValue : JSON of attrib:value */
        (GenDb::NewCf(g_viz_constants.STATS_TABLE_BY_STR_U64_TAG,
                      stat_row_twotag,
                      boost::assign::list_of
                      (GenDb::DbDataType::UTF8Type)
                      (GenDb::DbDataType::Unsigned64Type)
                      (GenDb::DbDataType::Unsigned32Type)
                      (GenDb::DbDataType::LexicalUUIDType),
                      boost::assign::list_of
                      (GenDb::DbDataType::UTF8Type)
                     ))
/* Stats Table by U64,Str tag
 * The schema is as follows:
 *   RowKey      : T2, Partition #, StatName, StatAttr, PrefixTagName, SuffixTagName
 *   ColumnName  : U64 TagValue, String Tag2 Value, T1, SampleUUID
 *   ColumnValue : JSON of attrib:value */
        (GenDb::NewCf(g_viz_constants.STATS_TABLE_BY_U64_STR_TAG,
                      stat_row_twotag,
                      boost::assign::list_of
                      (GenDb::DbDataType::Unsigned64Type)
                      (GenDb::DbDataType::UTF8Type)
                      (GenDb::DbDataType::Unsigned32Type)
                      (GenDb::DbDataType::LexicalUUIDType),
                      boost::assign::list_of
                      (GenDb::DbDataType::UTF8Type)
                     ))
/* Stats Table by U64,U64 tag
 * The schema is as follows:
 *   RowKey      : T2, Partition #, StatName, StatAttr, PrefixTagName, SuffixTagName
 *   ColumnName  : U64 TagValue, U64 Tag2 Value, T1, SampleUUID
 *   ColumnValue : JSON of attrib:value */
        (GenDb::NewCf(g_viz_constants.STATS_TABLE_BY_U64_U64_TAG,
                      stat_row_twotag,
                      boost::assign::list_of
                      (GenDb::DbDataType::Unsigned64Type)
                      (GenDb::DbDataType::Unsigned64Type)
                      (GenDb::DbDataType::Unsigned32Type)
                      (GenDb::DbDataType::LexicalUUIDType),
                      boost::assign::list_of
                      (GenDb::DbDataType::UTF8Type)
                     ))
/* Stats Table by U64 tag
 * The schema is as follows:
 *   RowKey      : T2, Partition #, StatName, StatAttr, TagName
 *   ColumnName  : U64 TagValue, T1, SampleUUID 
 *   ColumnValue : JSON of attrib:value */
        (GenDb::NewCf(g_viz_constants.STATS_TABLE_BY_U64_TAG,
                      stat_row_onetag,
                      boost::assign::list_of
                      (GenDb::DbDataType::Unsigned64Type)
                      (GenDb::DbDataType::Unsigned32Type)
                      (GenDb::DbDataType::LexicalUUIDType),
                      boost::assign::list_of
                      (GenDb::DbDataType::UTF8Type)
                     ))
/* Stats Table by Double tag
 * The schema is as follows:
 *   RowKey      : T2, Partition #, StatName, StatAttr, TagName
 *   ColumnName  : Double TagValue, T1, SampleUUID 
 *   ColumnValue : JSON of attrib:value */
        (GenDb::NewCf(g_viz_constants.STATS_TABLE_BY_DBL_TAG,
                      stat_row_onetag,
                      boost::assign::list_of
                      (GenDb::DbDataType::DoubleType)
                      (GenDb::DbDataType::Unsigned32Type)
                      (GenDb::DbDataType::LexicalUUIDType),
                      boost::assign::list_of
                      (GenDb::DbDataType::UTF8Type)
                     ))
/* Stats Table by String tag
 * The schema is as follows:
 *   RowKey      : T2, Partition #, StatName, StatAttr, TagName
 *   ColumnName  : String TagValue, T1, SampleUUID 
 *   ColumnValue : JSON of attrib:value */
        (GenDb::NewCf(g_viz_constants.STATS_TABLE_BY_STR_TAG,
                      stat_row_onetag,
                      boost::assign::list_of
                      (GenDb::DbDataType::UTF8Type)
                      (GenDb::DbDataType::Unsigned32Type)
                      (GenDb::DbDataType::LexicalUUIDType),
                      boost::assign::list_of
                      (GenDb::DbDataType::UTF8Type)
                     ))
        ;

    flow_msg2type_map[g_viz_constants.FlowRecordNames[FlowRecordFields::FLOWREC_FLOWUUID]] =
         FlowTypeInfo(FlowRecordFields::FLOWREC_FLOWUUID, GenDb::DbDataType::LexicalUUIDType);
    flow_msg2type_map[g_viz_constants.FlowRecordNames[FlowRecordFields::FLOWREC_DIRECTION_ING]] =
         FlowTypeInfo(FlowRecordFields::FLOWREC_DIRECTION_ING, GenDb::DbDataType::Unsigned8Type);
    flow_msg2type_map[g_viz_constants.FlowRecordNames[FlowRecordFields::FLOWREC_SOURCEVN]] =
         FlowTypeInfo(FlowRecordFields::FLOWREC_SOURCEVN, GenDb::DbDataType::UTF8Type);
    flow_msg2type_map[g_viz_constants.FlowRecordNames[FlowRecordFields::FLOWREC_SOURCEIP]] =
#ifdef USE_CASSANDRA_CQL
         FlowTypeInfo(FlowRecordFields::FLOWREC_SOURCEIP, GenDb::DbDataType::InetType);
#else // USE_CASSANDRA_CQL
         FlowTypeInfo(FlowRecordFields::FLOWREC_SOURCEIP, GenDb::DbDataType::Unsigned32Type);
#endif // !USE_CASSANDRA_CQL
    flow_msg2type_map[g_viz_constants.FlowRecordNames[FlowRecordFields::FLOWREC_DESTVN]] =
         FlowTypeInfo(FlowRecordFields::FLOWREC_DESTVN, GenDb::DbDataType::UTF8Type);
    flow_msg2type_map[g_viz_constants.FlowRecordNames[FlowRecordFields::FLOWREC_DESTIP]] =
#ifdef USE_CASSANDRA_CQL
         FlowTypeInfo(FlowRecordFields::FLOWREC_DESTIP, GenDb::DbDataType::InetType);
#else // USE_CASSANDRA_CQL
         FlowTypeInfo(FlowRecordFields::FLOWREC_DESTIP, GenDb::DbDataType::Unsigned32Type);
#endif // !USE_CASSANDRA_CQL
    flow_msg2type_map[g_viz_constants.FlowRecordNames[FlowRecordFields::FLOWREC_PROTOCOL]] =
         FlowTypeInfo(FlowRecordFields::FLOWREC_PROTOCOL, GenDb::DbDataType::Unsigned8Type);
    flow_msg2type_map[g_viz_constants.FlowRecordNames[FlowRecordFields::FLOWREC_SPORT]] =
         FlowTypeInfo(FlowRecordFields::FLOWREC_SPORT, GenDb::DbDataType::Unsigned16Type);
    flow_msg2type_map[g_viz_constants.FlowRecordNames[FlowRecordFields::FLOWREC_DPORT]] =
         FlowTypeInfo(FlowRecordFields::FLOWREC_DPORT, GenDb::DbDataType::Unsigned16Type);
    flow_msg2type_map[g_viz_constants.FlowRecordNames[FlowRecordFields::FLOWREC_TOS]] =
         FlowTypeInfo(FlowRecordFields::FLOWREC_TOS, GenDb::DbDataType::Unsigned8Type);
    flow_msg2type_map[g_viz_constants.FlowRecordNames[FlowRecordFields::FLOWREC_TCP_FLAGS]] =
         FlowTypeInfo(FlowRecordFields::FLOWREC_TCP_FLAGS, GenDb::DbDataType::Unsigned8Type);
    flow_msg2type_map[g_viz_constants.FlowRecordNames[FlowRecordFields::FLOWREC_VM]] =
         FlowTypeInfo(FlowRecordFields::FLOWREC_VM, GenDb::DbDataType::UTF8Type);
    flow_msg2type_map[g_viz_constants.FlowRecordNames[FlowRecordFields::FLOWREC_INPUT_INTERFACE]] =
         FlowTypeInfo(FlowRecordFields::FLOWREC_INPUT_INTERFACE, GenDb::DbDataType::UTF8Type);
    flow_msg2type_map[g_viz_constants.FlowRecordNames[FlowRecordFields::FLOWREC_OUTPUT_INTERFACE]] =
         FlowTypeInfo(FlowRecordFields::FLOWREC_OUTPUT_INTERFACE, GenDb::DbDataType::UTF8Type);
    flow_msg2type_map[g_viz_constants.FlowRecordNames[FlowRecordFields::FLOWREC_MPLS_LABEL]] =
         FlowTypeInfo(FlowRecordFields::FLOWREC_MPLS_LABEL, GenDb::DbDataType::Unsigned32Type);
    flow_msg2type_map[g_viz_constants.FlowRecordNames[FlowRecordFields::FLOWREC_REVERSE_UUID]] =
         FlowTypeInfo(FlowRecordFields::FLOWREC_REVERSE_UUID, GenDb::DbDataType::UTF8Type);
    flow_msg2type_map[g_viz_constants.FlowRecordNames[FlowRecordFields::FLOWREC_SETUP_TIME]] =
         FlowTypeInfo(FlowRecordFields::FLOWREC_SETUP_TIME, GenDb::DbDataType::Unsigned64Type);
    flow_msg2type_map[g_viz_constants.FlowRecordNames[FlowRecordFields::FLOWREC_TEARDOWN_TIME]] =
         FlowTypeInfo(FlowRecordFields::FLOWREC_TEARDOWN_TIME, GenDb::DbDataType::Unsigned64Type);
    flow_msg2type_map[g_viz_constants.FlowRecordNames[FlowRecordFields::FLOWREC_MIN_INTERARRIVAL]] =
         FlowTypeInfo(FlowRecordFields::FLOWREC_MIN_INTERARRIVAL, GenDb::DbDataType::Unsigned32Type);
    flow_msg2type_map[g_viz_constants.FlowRecordNames[FlowRecordFields::FLOWREC_MAX_INTERARRIVAL]] =
         FlowTypeInfo(FlowRecordFields::FLOWREC_MAX_INTERARRIVAL, GenDb::DbDataType::Unsigned32Type);
    flow_msg2type_map[g_viz_constants.FlowRecordNames[FlowRecordFields::FLOWREC_MEAN_INTERARRIVAL]] =
         FlowTypeInfo(FlowRecordFields::FLOWREC_MEAN_INTERARRIVAL, GenDb::DbDataType::Unsigned32Type);
    flow_msg2type_map[g_viz_constants.FlowRecordNames[FlowRecordFields::FLOWREC_STDDEV_INTERARRIVAL]] =
         FlowTypeInfo(FlowRecordFields::FLOWREC_STDDEV_INTERARRIVAL, GenDb::DbDataType::Unsigned32Type);
    flow_msg2type_map[g_viz_constants.FlowRecordNames[FlowRecordFields::FLOWREC_BYTES]] =
         FlowTypeInfo(FlowRecordFields::FLOWREC_BYTES, GenDb::DbDataType::Unsigned64Type);
    flow_msg2type_map[g_viz_constants.FlowRecordNames[FlowRecordFields::FLOWREC_PACKETS]] =
         FlowTypeInfo(FlowRecordFields::FLOWREC_PACKETS, GenDb::DbDataType::Unsigned64Type);
    flow_msg2type_map[g_viz_constants.FlowRecordNames[FlowRecordFields::FLOWREC_DIFF_BYTES]] =
         FlowTypeInfo(FlowRecordFields::FLOWREC_DIFF_BYTES, GenDb::DbDataType::Unsigned64Type);
    flow_msg2type_map[g_viz_constants.FlowRecordNames[FlowRecordFields::FLOWREC_DIFF_PACKETS]] =
         FlowTypeInfo(FlowRecordFields::FLOWREC_DIFF_PACKETS, GenDb::DbDataType::Unsigned64Type);
    flow_msg2type_map[g_viz_constants.FlowRecordNames[FlowRecordFields::FLOWREC_DATA_SAMPLE]] =
         FlowTypeInfo(FlowRecordFields::FLOWREC_DATA_SAMPLE, GenDb::DbDataType::UTF8Type);
    flow_msg2type_map[g_viz_constants.FlowRecordNames[FlowRecordFields::FLOWREC_ACTION]] =
         FlowTypeInfo(FlowRecordFields::FLOWREC_ACTION, GenDb::DbDataType::UTF8Type);
    flow_msg2type_map[g_viz_constants.FlowRecordNames[FlowRecordFields::FLOWREC_SG_RULE_UUID]] =
         FlowTypeInfo(FlowRecordFields::FLOWREC_SG_RULE_UUID, GenDb::DbDataType::LexicalUUIDType);
    flow_msg2type_map[g_viz_constants.FlowRecordNames[FlowRecordFields::FLOWREC_NW_ACE_UUID]] =
         FlowTypeInfo(FlowRecordFields::FLOWREC_NW_ACE_UUID, GenDb::DbDataType::LexicalUUIDType);
    flow_msg2type_map[g_viz_constants.FlowRecordNames[FlowRecordFields::FLOWREC_VROUTER_IP]] =
         FlowTypeInfo(FlowRecordFields::FLOWREC_VROUTER_IP, GenDb::DbDataType::UTF8Type);
    flow_msg2type_map[g_viz_constants.FlowRecordNames[FlowRecordFields::FLOWREC_OTHER_VROUTER_IP]] =
         FlowTypeInfo(FlowRecordFields::FLOWREC_OTHER_VROUTER_IP, GenDb::DbDataType::UTF8Type);
    flow_msg2type_map[g_viz_constants.FlowRecordNames[FlowRecordFields::FLOWREC_UNDERLAY_PROTO]] =
         FlowTypeInfo(FlowRecordFields::FLOWREC_UNDERLAY_PROTO, GenDb::DbDataType::Unsigned16Type);
    flow_msg2type_map[g_viz_constants.FlowRecordNames[FlowRecordFields::FLOWREC_UNDERLAY_SPORT]] =
         FlowTypeInfo(FlowRecordFields::FLOWREC_UNDERLAY_SPORT, GenDb::DbDataType::Unsigned16Type);
    flow_msg2type_map[g_viz_constants.FlowRecordNames[FlowRecordFields::FLOWREC_VMI_UUID]] =
         FlowTypeInfo(FlowRecordFields::FLOWREC_VMI_UUID, GenDb::DbDataType::LexicalUUIDType);
    flow_msg2type_map[g_viz_constants.FlowRecordNames[FlowRecordFields::FLOWREC_DROP_REASON]] =
         FlowTypeInfo(FlowRecordFields::FLOWREC_DROP_REASON, GenDb::DbDataType::UTF8Type);
}
