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

void init_tables(std::vector<GenDb::NewCf>& table,
                std::vector<table_schema> schema, bool use_thrift_flow = false) {

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

    for(size_t i = 0; i < schema.size(); i++) {
        GenDb::DbDataTypeVec key_types;
        GenDb::DbDataTypeVec comp_type;
        GenDb::DbDataTypeVec valid_class;
        std::map<std::string, GenDb::DbDataType::type> cols;
        if (schema[i].is_static) {
            for(size_t j = 0; j < schema[i].columns.size(); j++) {
                if (schema[i].columns[j].key) {
                    key_types.push_back(schema[i].columns[j].datatype);
                } else {
#ifndef USE_CASSANDRA_CQL
                    if(schema[i].columns[j].datatype ==
                            GenDb::DbDataType::InetType) {
                        schema[i].columns[j].datatype = GenDb::DbDataType::Unsigned32Type;
                    }
#endif
                    cols[schema[i].columns[j].name] =
                        static_cast<GenDb::DbDataType::type>(
                                schema[i].columns[j].datatype);
                }
            }
            table.push_back(GenDb::NewCf(schema[i].table_name,
                            key_types, cols));
        } else {
            for(size_t j = 0; j < schema[i].columns.size(); j++) {
                if(boost::starts_with(schema[i].columns[j].name, "key")) {
                    key_types.push_back(schema[i].columns[j].datatype);
                } else if (schema[i].columns[j].name == "value") {
                    valid_class.push_back(schema[i].columns[j].datatype);
                } else {
                    comp_type.push_back(schema[i].columns[j].datatype);
                }
            }
            if (use_thrift_flow) {
                table.push_back(GenDb::NewCf(schema[i].table_name,
                            key_types, comp_type, flow_series_value_thrift));
            } else {
                table.push_back(GenDb::NewCf(schema[i].table_name,
                            key_types, comp_type, valid_class));
            }
        }
    }
}

void init_vizd_tables(bool use_cql) {
    static bool init_done = false;

    if (init_done)
        return;
    init_done = true;

// usage of GenDb::_DbDataType_VALUES_TO_NAMES[GenDb::DbDataType::LexicalUUIDType])) didn't
// compile, hence using raw values
#ifdef USE_CASSANDRA_CQL
    init_tables(vizd_tables, g_viz_constants._VIZD_TABLE_SCHEMA);
#else
    init_tables(vizd_tables, g_viz_constants._VIZD_THRIFT_TABLE_SCHEMA);
#endif

/* flow records table and flow series table are created in the code path itself
 * the following are flow index tables - for SVN:SIP, DVN:DIP, ...
 *
 */
#ifdef USE_CASSANDRA_CQL
    init_tables(vizd_flow_tables, g_viz_constants._VIZD_FLOW_TABLE_SCHEMA);
#else
    init_tables(vizd_flow_tables, g_viz_constants._VIZD_FLOW_TABLE_SCHEMA, true);
#endif

    init_tables(vizd_stat_tables, g_viz_constants._VIZD_STAT_TABLE_SCHEMA);

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
