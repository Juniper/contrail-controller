/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "vizd_table_desc.h"

#include <boost/assign/list_of.hpp>
#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh.h>
#include "viz_constants.h"
#include "gendb_constants.h"

std::vector<GenDb::NewCf> vizd_tables;
std::vector<GenDb::NewCf> vizd_flow_tables;

void init_vizd_tables() {
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
                       GenDb::DbDataType::AsciiType)
                      (g_viz_constants.NAMESPACE,
                       GenDb::DbDataType::AsciiType)
                      (g_viz_constants.MODULE,
                       GenDb::DbDataType::AsciiType)
                      (g_viz_constants.INSTANCE_ID,
                       GenDb::DbDataType::AsciiType)
                      (g_viz_constants.NODE_TYPE,
                       GenDb::DbDataType::AsciiType)
                      (g_viz_constants.CONTEXT,
                       GenDb::DbDataType::AsciiType)
                      (g_viz_constants.TIMESTAMP,
                       GenDb::DbDataType::Unsigned64Type)
                      (g_viz_constants.CATEGORY,
                       GenDb::DbDataType::AsciiType)
                      (g_viz_constants.LEVEL,
                       GenDb::DbDataType::Unsigned32Type)
                      (g_viz_constants.MESSAGE_TYPE,
                       GenDb::DbDataType::AsciiType)
                      (g_viz_constants.SEQUENCE_NUM,
                       GenDb::DbDataType::Unsigned32Type)
                      (g_viz_constants.VERSION,
                       GenDb::DbDataType::Unsigned32Type)
                      (g_viz_constants.SANDESH_TYPE,
                       GenDb::DbDataType::Unsigned8Type)
                      (g_viz_constants.DATA,
                       GenDb::DbDataType::AsciiType)))
        (GenDb::NewCf(g_viz_constants.MESSAGE_TABLE_SOURCE,
                      boost::assign::list_of
                      (GenDb::DbDataType::Unsigned32Type)
                      (GenDb::DbDataType::AsciiType),
                      boost::assign::list_of
                      (GenDb::DbDataType::Unsigned32Type),
                      boost::assign::list_of
                      (GenDb::DbDataType::LexicalUUIDType)))
        (GenDb::NewCf(g_viz_constants.MESSAGE_TABLE_MODULE_ID,
                      boost::assign::list_of
                      (GenDb::DbDataType::Unsigned32Type)
                      (GenDb::DbDataType::AsciiType),
                      boost::assign::list_of
                      (GenDb::DbDataType::Unsigned32Type),
                      boost::assign::list_of
                      (GenDb::DbDataType::LexicalUUIDType)))
        (GenDb::NewCf(g_viz_constants.MESSAGE_TABLE_MESSAGE_TYPE,
                      boost::assign::list_of
                      (GenDb::DbDataType::Unsigned32Type)
                      (GenDb::DbDataType::AsciiType),
                      boost::assign::list_of
                      (GenDb::DbDataType::Unsigned32Type),
                      boost::assign::list_of
                      (GenDb::DbDataType::LexicalUUIDType)))
        (GenDb::NewCf(g_viz_constants.MESSAGE_TABLE_CATEGORY,
                      boost::assign::list_of
                      (GenDb::DbDataType::Unsigned32Type)
                      (GenDb::DbDataType::AsciiType),
                      boost::assign::list_of
                      (GenDb::DbDataType::Unsigned32Type),
                      boost::assign::list_of
                      (GenDb::DbDataType::LexicalUUIDType)))
        (GenDb::NewCf(g_viz_constants.MESSAGE_TABLE_TIMESTAMP,
                      boost::assign::list_of
                      (GenDb::DbDataType::Unsigned32Type),
                      boost::assign::list_of
                      (GenDb::DbDataType::Unsigned32Type),
                      boost::assign::list_of
                (GenDb::DbDataType::LexicalUUIDType)))
        (GenDb::NewCf(g_viz_constants.OBJECT_VALUE_TABLE,
                      boost::assign::list_of
                      (GenDb::DbDataType::Unsigned32Type)
                      (GenDb::DbDataType::AsciiType),
                      boost::assign::list_of
                      (GenDb::DbDataType::Unsigned32Type),
                      boost::assign::list_of
                      (GenDb::DbDataType::AsciiType)))
        (GenDb::NewCf(g_viz_constants.SYSTEM_OBJECT_TABLE,
                      boost::assign::list_of
                      (GenDb::DbDataType::AsciiType),
                      boost::assign::map_list_of
                      (g_viz_constants.SYSTEM_OBJECT_START_TIME,
                       GenDb::DbDataType::Unsigned64Type)))
        ;

/* flow records table and flow series table are created in the code path itself
 * the following are flow index tables - for SVN:SIP, DVN:DIP, ...
 *
 */
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
                      (g_viz_constants.FlowRecordNames.find(FlowRecordFields::FLOWREC_VROUTER)->second,
                       GenDb::DbDataType::AsciiType)
                      (g_viz_constants.FlowRecordNames.find(FlowRecordFields::FLOWREC_DIRECTION_ING)->second,
                       GenDb::DbDataType::Unsigned8Type)
                      (g_viz_constants.FlowRecordNames.find(FlowRecordFields::FLOWREC_SOURCEVN)->second,
                       GenDb::DbDataType::AsciiType)
                      (g_viz_constants.FlowRecordNames.find(FlowRecordFields::FLOWREC_SOURCEIP)->second,
                       GenDb::DbDataType::Unsigned32Type)
                      (g_viz_constants.FlowRecordNames.find(FlowRecordFields::FLOWREC_DESTVN)->second,
                       GenDb::DbDataType::AsciiType)
                      (g_viz_constants.FlowRecordNames.find(FlowRecordFields::FLOWREC_DESTIP)->second,
                       GenDb::DbDataType::Unsigned32Type)
                      (g_viz_constants.FlowRecordNames.find(FlowRecordFields::FLOWREC_PROTOCOL)->second,
                       GenDb::DbDataType::Unsigned8Type)
                      (g_viz_constants.FlowRecordNames.find(FlowRecordFields::FLOWREC_SPORT)->second,
                       GenDb::DbDataType::Unsigned16Type)
                      (g_viz_constants.FlowRecordNames.find(FlowRecordFields::FLOWREC_DPORT)->second,
                       GenDb::DbDataType::Unsigned16Type)
                      (g_viz_constants.FlowRecordNames.find(FlowRecordFields::FLOWREC_TOS)->second,
                       GenDb::DbDataType::Unsigned8Type)
    (g_viz_constants.FlowRecordNames.find(FlowRecordFields::FLOWREC_TCP_FLAGS)->second,
     GenDb::DbDataType::Unsigned8Type)
    (g_viz_constants.FlowRecordNames.find(FlowRecordFields::FLOWREC_VM)->second,
     GenDb::DbDataType::AsciiType)
    (g_viz_constants.FlowRecordNames.find(FlowRecordFields::FLOWREC_INPUT_INTERFACE)->second,
     GenDb::DbDataType::AsciiType)
    (g_viz_constants.FlowRecordNames.find(FlowRecordFields::FLOWREC_OUTPUT_INTERFACE)->second,
     GenDb::DbDataType::AsciiType)
    (g_viz_constants.FlowRecordNames.find(FlowRecordFields::FLOWREC_MPLS_LABEL)->second,
     GenDb::DbDataType::Unsigned32Type)
    (g_viz_constants.FlowRecordNames.find(FlowRecordFields::FLOWREC_REVERSE_UUID)->second,
     GenDb::DbDataType::AsciiType)
    (g_viz_constants.FlowRecordNames.find(FlowRecordFields::FLOWREC_SETUP_TIME)->second,
     GenDb::DbDataType::Unsigned64Type)
    (g_viz_constants.FlowRecordNames.find(FlowRecordFields::FLOWREC_TEARDOWN_TIME)->second,
     GenDb::DbDataType::Unsigned64Type)
    (g_viz_constants.FlowRecordNames.find(FlowRecordFields::FLOWREC_MIN_INTERARRIVAL)->second,
     GenDb::DbDataType::Unsigned32Type)
    (g_viz_constants.FlowRecordNames.find(FlowRecordFields::FLOWREC_MAX_INTERARRIVAL)->second,
     GenDb::DbDataType::Unsigned32Type)
    (g_viz_constants.FlowRecordNames.find(FlowRecordFields::FLOWREC_MEAN_INTERARRIVAL)->second,
     GenDb::DbDataType::Unsigned32Type)
    (g_viz_constants.FlowRecordNames.find(FlowRecordFields::FLOWREC_STDDEV_INTERARRIVAL)->second,
     GenDb::DbDataType::Unsigned32Type)
    (g_viz_constants.FlowRecordNames.find(FlowRecordFields::FLOWREC_BYTES)->second,
     GenDb::DbDataType::Unsigned64Type)
    (g_viz_constants.FlowRecordNames.find(FlowRecordFields::FLOWREC_PACKETS)->second,
     GenDb::DbDataType::Unsigned64Type)
    (g_viz_constants.FlowRecordNames.find(FlowRecordFields::FLOWREC_DATA_SAMPLE)->second,
     GenDb::DbDataType::AsciiType)))
        (GenDb::NewCf(g_viz_constants.FLOW_TABLE_SVN_SIP,
                      boost::assign::list_of
                      (GenDb::DbDataType::Unsigned32Type)
                      (GenDb::DbDataType::Unsigned8Type)
                      (GenDb::DbDataType::Unsigned8Type),
                      boost::assign::list_of
                      (GenDb::DbDataType::AsciiType)
                      (GenDb::DbDataType::Unsigned32Type)
                      (GenDb::DbDataType::Unsigned32Type)
                      (GenDb::DbDataType::LexicalUUIDType),
                      boost::assign::list_of
                      (GenDb::DbDataType::Unsigned64Type)
                      (GenDb::DbDataType::Unsigned64Type)
                      (GenDb::DbDataType::Unsigned8Type)
                      (GenDb::DbDataType::LexicalUUIDType)
                      (GenDb::DbDataType::AsciiType)
                      (GenDb::DbDataType::AsciiType)
                      (GenDb::DbDataType::AsciiType)
                      (GenDb::DbDataType::Unsigned32Type)
                      (GenDb::DbDataType::Unsigned32Type)
                      (GenDb::DbDataType::Unsigned8Type)
                      (GenDb::DbDataType::Unsigned16Type)
                      (GenDb::DbDataType::Unsigned16Type)
                      (GenDb::DbDataType::AsciiType)
                     ))

    /* (DVN,DIP) index table */
    (GenDb::NewCf(g_viz_constants.FLOW_TABLE_DVN_DIP,
                  boost::assign::list_of
                  (GenDb::DbDataType::Unsigned32Type)
                  (GenDb::DbDataType::Unsigned8Type)
                  (GenDb::DbDataType::Unsigned8Type),
                  boost::assign::list_of
                  (GenDb::DbDataType::AsciiType)
                  (GenDb::DbDataType::Unsigned32Type)
                  (GenDb::DbDataType::Unsigned32Type)
                  (GenDb::DbDataType::LexicalUUIDType),
                  boost::assign::list_of
                  (GenDb::DbDataType::Unsigned64Type)
                  (GenDb::DbDataType::Unsigned64Type)
                  (GenDb::DbDataType::Unsigned8Type)
                  (GenDb::DbDataType::LexicalUUIDType)
                  (GenDb::DbDataType::AsciiType)
                  (GenDb::DbDataType::AsciiType)
                  (GenDb::DbDataType::AsciiType)
                  (GenDb::DbDataType::Unsigned32Type)
                  (GenDb::DbDataType::Unsigned32Type)
                  (GenDb::DbDataType::Unsigned8Type)
                  (GenDb::DbDataType::Unsigned16Type)
                  (GenDb::DbDataType::Unsigned16Type)
                  (GenDb::DbDataType::AsciiType)
                 ))

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
                boost::assign::list_of
                (GenDb::DbDataType::Unsigned64Type)
                (GenDb::DbDataType::Unsigned64Type)
                (GenDb::DbDataType::Unsigned8Type)
                (GenDb::DbDataType::LexicalUUIDType)
                (GenDb::DbDataType::AsciiType)
                (GenDb::DbDataType::AsciiType)
                (GenDb::DbDataType::AsciiType)
                (GenDb::DbDataType::Unsigned32Type)
                (GenDb::DbDataType::Unsigned32Type)
                (GenDb::DbDataType::Unsigned8Type)
                (GenDb::DbDataType::Unsigned16Type)
                (GenDb::DbDataType::Unsigned16Type)
                (GenDb::DbDataType::AsciiType)
                ))
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
                      boost::assign::list_of
                      (GenDb::DbDataType::Unsigned64Type)
                      (GenDb::DbDataType::Unsigned64Type)
                      (GenDb::DbDataType::Unsigned8Type)
                      (GenDb::DbDataType::LexicalUUIDType)
                      (GenDb::DbDataType::AsciiType)
                      (GenDb::DbDataType::AsciiType)
                      (GenDb::DbDataType::AsciiType)
                      (GenDb::DbDataType::Unsigned32Type)
                      (GenDb::DbDataType::Unsigned32Type)
                      (GenDb::DbDataType::Unsigned8Type)
                      (GenDb::DbDataType::Unsigned16Type)
                      (GenDb::DbDataType::Unsigned16Type)
                      (GenDb::DbDataType::AsciiType)
                     ))
 /* (VROUTER) index table */
        (GenDb::NewCf(g_viz_constants.FLOW_TABLE_VROUTER,
                      boost::assign::list_of
                      (GenDb::DbDataType::Unsigned32Type)
                      (GenDb::DbDataType::Unsigned8Type)
                      (GenDb::DbDataType::Unsigned8Type),
                      boost::assign::list_of
                      (GenDb::DbDataType::AsciiType)
                      (GenDb::DbDataType::Unsigned32Type)
                      (GenDb::DbDataType::LexicalUUIDType),
                      boost::assign::list_of
                      (GenDb::DbDataType::Unsigned64Type)
                      (GenDb::DbDataType::Unsigned64Type)
                      (GenDb::DbDataType::Unsigned8Type)
                      (GenDb::DbDataType::LexicalUUIDType)
                      (GenDb::DbDataType::AsciiType)
                      (GenDb::DbDataType::AsciiType)
                      (GenDb::DbDataType::AsciiType)
                      (GenDb::DbDataType::Unsigned32Type)
                      (GenDb::DbDataType::Unsigned32Type)
                      (GenDb::DbDataType::Unsigned8Type)
                      (GenDb::DbDataType::Unsigned16Type)
                      (GenDb::DbDataType::Unsigned16Type)
                      (GenDb::DbDataType::AsciiType)
                     ))
/* Stats Table by String,Str tag
 * The schema is as follows:
 *   RowKey      : T2, StatName, StatAttr, TagName
 *   ColumnName  : String TagValue, String Tag2 Value, T1, SampleUUID 
 *   ColumnValue : JSON of attrib:value */
        (GenDb::NewCf(g_viz_constants.STATS_TABLE_BY_STR_STR_TAG,
                      boost::assign::list_of
                      (GenDb::DbDataType::Unsigned32Type)
                      (GenDb::DbDataType::AsciiType)
                      (GenDb::DbDataType::AsciiType)
                      (GenDb::DbDataType::AsciiType),
                      boost::assign::list_of
                      (GenDb::DbDataType::AsciiType)
                      (GenDb::DbDataType::AsciiType)
                      (GenDb::DbDataType::Unsigned32Type)
                      (GenDb::DbDataType::LexicalUUIDType),
                      boost::assign::list_of
                      (GenDb::DbDataType::AsciiType)
                     ))
/* Stats Table by String,U64 tag
 * The schema is as follows:
 *   RowKey      : T2, StatName, StatAttr, TagName
 *   ColumnName  : String TagValue, U64 Tag2 Value, T1, SampleUUID 
 *   ColumnValue : JSON of attrib:value */
        (GenDb::NewCf(g_viz_constants.STATS_TABLE_BY_STR_U64_TAG,
                      boost::assign::list_of
                      (GenDb::DbDataType::Unsigned32Type)
                      (GenDb::DbDataType::AsciiType)
                      (GenDb::DbDataType::AsciiType)
                      (GenDb::DbDataType::AsciiType),
                      boost::assign::list_of
                      (GenDb::DbDataType::AsciiType)
                      (GenDb::DbDataType::Unsigned64Type)
                      (GenDb::DbDataType::Unsigned32Type)
                      (GenDb::DbDataType::LexicalUUIDType),
                      boost::assign::list_of
                      (GenDb::DbDataType::AsciiType)
                     ))
/* Stats Table by String,Double tag
 * The schema is as follows:
 *   RowKey      : T2, StatName, StatAttr, TagName
 *   ColumnName  : String TagValue, Double Tag2 Value, T1, SampleUUID
 *   ColumnValue : JSON of attrib:value */
        (GenDb::NewCf(g_viz_constants.STATS_TABLE_BY_STR_DBL_TAG,
                      boost::assign::list_of
                      (GenDb::DbDataType::Unsigned32Type)
                      (GenDb::DbDataType::AsciiType)
                      (GenDb::DbDataType::AsciiType)
                      (GenDb::DbDataType::AsciiType),
                      boost::assign::list_of
                      (GenDb::DbDataType::AsciiType)
                      (GenDb::DbDataType::DoubleType)
                      (GenDb::DbDataType::Unsigned32Type)
                      (GenDb::DbDataType::LexicalUUIDType),
                      boost::assign::list_of
                      (GenDb::DbDataType::AsciiType)
                     ))
/* Stats Table by U64,Str tag
 * The schema is as follows:
 *   RowKey      : T2, StatName, StatAttr, TagName
 *   ColumnName  : U64 TagValue, String Tag2 Value, T1, SampleUUID
 *   ColumnValue : JSON of attrib:value */
        (GenDb::NewCf(g_viz_constants.STATS_TABLE_BY_U64_STR_TAG,
                      boost::assign::list_of
                      (GenDb::DbDataType::Unsigned32Type)
                      (GenDb::DbDataType::AsciiType)
                      (GenDb::DbDataType::AsciiType)
                      (GenDb::DbDataType::AsciiType),
                      boost::assign::list_of
                      (GenDb::DbDataType::Unsigned64Type)
                      (GenDb::DbDataType::AsciiType)
                      (GenDb::DbDataType::Unsigned32Type)
                      (GenDb::DbDataType::LexicalUUIDType),
                      boost::assign::list_of
                      (GenDb::DbDataType::AsciiType)
                     ))
/* Stats Table by U64,U64 tag
 * The schema is as follows:
 *   RowKey      : T2, StatName, StatAttr, TagName
 *   ColumnName  : U64 TagValue, U64 Tag2 Value, T1, SampleUUID
 *   ColumnValue : JSON of attrib:value */
        (GenDb::NewCf(g_viz_constants.STATS_TABLE_BY_U64_U64_TAG,
                      boost::assign::list_of
                      (GenDb::DbDataType::Unsigned32Type)
                      (GenDb::DbDataType::AsciiType)
                      (GenDb::DbDataType::AsciiType)
                      (GenDb::DbDataType::AsciiType),
                      boost::assign::list_of
                      (GenDb::DbDataType::Unsigned64Type)
                      (GenDb::DbDataType::Unsigned64Type)
                      (GenDb::DbDataType::Unsigned32Type)
                      (GenDb::DbDataType::LexicalUUIDType),
                      boost::assign::list_of
                      (GenDb::DbDataType::AsciiType)
                     ))
/* Stats Table by Double,Str tag
 * The schema is as follows:
 *   RowKey      : T2, StatName, StatAttr, TagName
 *   ColumnName  : Double TagValue, String Tag2 Value, T1, SampleUUID
 *   ColumnValue : JSON of attrib:value */
        (GenDb::NewCf(g_viz_constants.STATS_TABLE_BY_DBL_STR_TAG,
                      boost::assign::list_of
                      (GenDb::DbDataType::Unsigned32Type)
                      (GenDb::DbDataType::AsciiType)
                      (GenDb::DbDataType::AsciiType)
                      (GenDb::DbDataType::AsciiType),
                      boost::assign::list_of
                      (GenDb::DbDataType::DoubleType)
                      (GenDb::DbDataType::AsciiType)
                      (GenDb::DbDataType::Unsigned32Type)
                      (GenDb::DbDataType::LexicalUUIDType),
                      boost::assign::list_of
                      (GenDb::DbDataType::AsciiType)
                     ))
        ;
}
