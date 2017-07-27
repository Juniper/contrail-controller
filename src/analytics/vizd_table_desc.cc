/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "vizd_table_desc.h"

#include <boost/assign/list_of.hpp>
#include "viz_constants.h"
#include <database/gendb_constants.h>

std::vector<GenDb::NewCf> vizd_tables;
std::vector<GenDb::NewCf> vizd_flow_tables;
std::vector<GenDb::NewCf> vizd_stat_tables;
std::vector<GenDb::NewCf> vizd_session_tables;
FlowTypeMap flow_msg2type_map;
SessionTypeMap session_msg2type_map;
TagsIdxMap tags_name2idx_map;

void init_tables(std::vector<GenDb::NewCf>& table,
                std::map<std::string, table_schema> schema) {

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

    for (std::map<std::string, table_schema>::const_iterator it = schema.begin();
         it != schema.end(); ++it) {

        GenDb::DbDataTypeVec key_types;
        GenDb::DbDataTypeVec clustering_columns;
        GenDb::DbDataTypeVec columns;
        GenDb::DbDataTypeVec values;
        GenDb::NewCf::ColumnMap cols;
        if (it->second.is_static) {
            for(size_t j = 0; j < it->second.columns.size(); j++) {
                if (it->second.columns[j].key) {
                    key_types.push_back(it->second.columns[j].datatype);
                } else {
                    cols[it->second.columns[j].name] =
                        static_cast<GenDb::DbDataType::type>(
                                it->second.columns[j].datatype);
                }
            }
            table.push_back(GenDb::NewCf(it->first,
                            key_types, cols));
        } else {
            for(size_t j = 0; j < it->second.columns.size(); j++) {
                if(boost::starts_with(it->second.columns[j].name, "key")) {
                    key_types.push_back(it->second.columns[j].datatype);
                } else if (it->second.columns[j].name == "value") {
                    values.push_back(it->second.columns[j].datatype);
                } else if (it->second.columns[j].clustering) {
                    clustering_columns.push_back(it->second.columns[j].datatype);
                } else {
                    columns.push_back(it->second.columns[j].datatype);
                }
            }
            table.push_back(GenDb::NewCf(it->first,
                        key_types, clustering_columns, columns, values));
        }
    }
}

void init_vizd_tables() {
    static bool init_done = false;

    if (init_done)
        return;
    init_done = true;

// usage of GenDb::_DbDataType_VALUES_TO_NAMES[GenDb::DbDataType::LexicalUUIDType])) didn't
// compile, hence using raw values
    init_tables(vizd_tables, g_viz_constants._VIZD_TABLE_SCHEMA);

/* flow records table and flow series table are created in the code path itself
 * the following are flow index tables - for SVN:SIP, DVN:DIP, ...
 *
 */
    init_tables(vizd_flow_tables, g_viz_constants._VIZD_FLOW_TABLE_SCHEMA);

    init_tables(vizd_stat_tables, g_viz_constants._VIZD_STAT_TABLE_SCHEMA);

    init_tables(vizd_session_tables, g_viz_constants._VIZD_SESSION_TABLE_SCHEMA);

    session_msg2type_map[g_viz_constants.SessionRecordNames[SessionRecordFields::SESSION_IS_SI]] =
         SessionTypeInfo(SessionRecordCols::SESSION_IS_SI, GenDb::DbDataType::Unsigned8Type);
    session_msg2type_map[g_viz_constants.SessionRecordNames[SessionRecordFields::SESSION_PROTOCOL]] =
         SessionTypeInfo(SessionRecordCols::SESSION_PROTOCOL, GenDb::DbDataType::Unsigned16Type);
    session_msg2type_map[g_viz_constants.SessionRecordNames[SessionRecordFields::SESSION_SPORT]] =
         SessionTypeInfo(SessionRecordCols::SESSION_SPORT, GenDb::DbDataType::Unsigned16Type);
    session_msg2type_map[g_viz_constants.SessionRecordNames[SessionRecordFields::SESSION_DEPLOYMENT]] =
         SessionTypeInfo(SessionRecordCols::SESSION_TAGS, GenDb::DbDataType::UTF8Type);
    session_msg2type_map[g_viz_constants.SessionRecordNames[SessionRecordFields::SESSION_TIER]] =
         SessionTypeInfo(SessionRecordCols::SESSION_TAGS, GenDb::DbDataType::UTF8Type);
    session_msg2type_map[g_viz_constants.SessionRecordNames[SessionRecordFields::SESSION_APPLICATION]] =
         SessionTypeInfo(SessionRecordCols::SESSION_TAGS, GenDb::DbDataType::UTF8Type);
    session_msg2type_map[g_viz_constants.SessionRecordNames[SessionRecordFields::SESSION_SITE]] =
         SessionTypeInfo(SessionRecordCols::SESSION_TAGS, GenDb::DbDataType::UTF8Type);
    session_msg2type_map[g_viz_constants.SessionRecordNames[SessionRecordFields::SESSION_LABELS]] =
         SessionTypeInfo(SessionRecordCols::SESSION_LABELS, GenDb::DbDataType::SetTextType);
    session_msg2type_map[g_viz_constants.SessionRecordNames[SessionRecordFields::SESSION_REMOTE_DEPLOYMENT]] =
         SessionTypeInfo(SessionRecordCols::SESSION_REMOTE_TAGS, GenDb::DbDataType::UTF8Type);
    session_msg2type_map[g_viz_constants.SessionRecordNames[SessionRecordFields::SESSION_REMOTE_TIER]] =
         SessionTypeInfo(SessionRecordCols::SESSION_REMOTE_TAGS, GenDb::DbDataType::UTF8Type);
    session_msg2type_map[g_viz_constants.SessionRecordNames[SessionRecordFields::SESSION_REMOTE_APPLICATION]] =
         SessionTypeInfo(SessionRecordCols::SESSION_REMOTE_TAGS, GenDb::DbDataType::UTF8Type);
    session_msg2type_map[g_viz_constants.SessionRecordNames[SessionRecordFields::SESSION_REMOTE_SITE]] =
         SessionTypeInfo(SessionRecordCols::SESSION_REMOTE_TAGS, GenDb::DbDataType::UTF8Type);
    session_msg2type_map[g_viz_constants.SessionRecordNames[SessionRecordFields::SESSION_REMOTE_LABELS]] =
         SessionTypeInfo(SessionRecordCols::SESSION_REMOTE_LABELS, GenDb::DbDataType::SetTextType);
    session_msg2type_map[g_viz_constants.SessionRecordNames[SessionRecordFields::SESSION_REMOTE_PREFIX]] =
         SessionTypeInfo(SessionRecordCols::SESSION_REMOTE_PREFIX, GenDb::DbDataType::UTF8Type);
    session_msg2type_map[g_viz_constants.SessionRecordNames[SessionRecordFields::SESSION_VMI]] =
         SessionTypeInfo(SessionRecordCols::SESSION_VMI, GenDb::DbDataType::LexicalUUIDType);
    session_msg2type_map[g_viz_constants.SessionRecordNames[SessionRecordFields::SESSION_IP]] =
         SessionTypeInfo(SessionRecordCols::SESSION_IP, GenDb::DbDataType::InetType);
    session_msg2type_map[g_viz_constants.SessionRecordNames[SessionRecordFields::SESSION_VROUTER_IP]] =
         SessionTypeInfo(SessionRecordCols::SESSION_VROUTER_IP, GenDb::DbDataType::InetType);
    session_msg2type_map[g_viz_constants.SessionRecordNames[SessionRecordFields::SESSION_VN]] =
         SessionTypeInfo(SessionRecordCols::SESSION_VN, GenDb::DbDataType::UTF8Type);
    session_msg2type_map[g_viz_constants.SessionRecordNames[SessionRecordFields::SESSION_REMOTE_VN]] =
         SessionTypeInfo(SessionRecordCols::SESSION_REMOTE_VN, GenDb::DbDataType::UTF8Type);
    session_msg2type_map[g_viz_constants.SessionRecordNames[SessionRecordFields::SESSION_SAMPLED_TX_BYTES]] =
         SessionTypeInfo(SessionRecordCols::SESSION_SAMPLED_TX_BYTES, GenDb::DbDataType::Unsigned64Type);
    session_msg2type_map[g_viz_constants.SessionRecordNames[SessionRecordFields::SESSION_SAMPLED_TX_PKTS]] =
         SessionTypeInfo(SessionRecordCols::SESSION_SAMPLED_TX_PKTS, GenDb::DbDataType::Unsigned64Type);
    session_msg2type_map[g_viz_constants.SessionRecordNames[SessionRecordFields::SESSION_SAMPLED_RX_BYTES]] =
         SessionTypeInfo(SessionRecordCols::SESSION_SAMPLED_RX_BYTES, GenDb::DbDataType::Unsigned64Type);
    session_msg2type_map[g_viz_constants.SessionRecordNames[SessionRecordFields::SESSION_SAMPLED_RX_PKTS]] =
         SessionTypeInfo(SessionRecordCols::SESSION_SAMPLED_RX_PKTS, GenDb::DbDataType::Unsigned64Type);
    session_msg2type_map[g_viz_constants.SessionRecordNames[SessionRecordFields::SESSION_LOGGED_TX_BYTES]] =
         SessionTypeInfo(SessionRecordCols::SESSION_LOGGED_TX_BYTES, GenDb::DbDataType::Unsigned64Type);
    session_msg2type_map[g_viz_constants.SessionRecordNames[SessionRecordFields::SESSION_LOGGED_TX_PKTS]] =
         SessionTypeInfo(SessionRecordCols::SESSION_LOGGED_TX_PKTS, GenDb::DbDataType::Unsigned64Type);
    session_msg2type_map[g_viz_constants.SessionRecordNames[SessionRecordFields::SESSION_LOGGED_RX_BYTES]] =
         SessionTypeInfo(SessionRecordCols::SESSION_LOGGED_RX_BYTES, GenDb::DbDataType::Unsigned64Type);
    session_msg2type_map[g_viz_constants.SessionRecordNames[SessionRecordFields::SESSION_LOGGED_RX_PKTS]] =
         SessionTypeInfo(SessionRecordCols::SESSION_LOGGED_RX_PKTS, GenDb::DbDataType::Unsigned64Type);
    session_msg2type_map[g_viz_constants.SessionRecordNames[SessionRecordFields::SESSION_MAP]] =
         SessionTypeInfo(SessionRecordCols::SESSION_MAP, GenDb::DbDataType::UTF8Type);

    tags_name2idx_map[g_viz_constants.SessionRecordNames[SessionRecordFields::SESSION_DEPLOYMENT]] = 0;
    tags_name2idx_map[g_viz_constants.SessionRecordNames[SessionRecordFields::SESSION_TIER]] = 1;
    tags_name2idx_map[g_viz_constants.SessionRecordNames[SessionRecordFields::SESSION_APPLICATION]] = 2;
    tags_name2idx_map[g_viz_constants.SessionRecordNames[SessionRecordFields::SESSION_SITE]] = 3;
    tags_name2idx_map[g_viz_constants.SessionRecordNames[SessionRecordFields::SESSION_REMOTE_DEPLOYMENT]] = 0;
    tags_name2idx_map[g_viz_constants.SessionRecordNames[SessionRecordFields::SESSION_REMOTE_TIER]] = 1;
    tags_name2idx_map[g_viz_constants.SessionRecordNames[SessionRecordFields::SESSION_REMOTE_APPLICATION]] = 2;
    tags_name2idx_map[g_viz_constants.SessionRecordNames[SessionRecordFields::SESSION_REMOTE_SITE]] = 3;
 
    flow_msg2type_map[g_viz_constants.FlowRecordNames[FlowRecordFields::FLOWREC_FLOWUUID]] =
         FlowTypeInfo(FlowRecordFields::FLOWREC_FLOWUUID, GenDb::DbDataType::LexicalUUIDType);
    flow_msg2type_map[g_viz_constants.FlowRecordNames[FlowRecordFields::FLOWREC_DIRECTION_ING]] =
         FlowTypeInfo(FlowRecordFields::FLOWREC_DIRECTION_ING, GenDb::DbDataType::Unsigned8Type);
    flow_msg2type_map[g_viz_constants.FlowRecordNames[FlowRecordFields::FLOWREC_SOURCEVN]] =
         FlowTypeInfo(FlowRecordFields::FLOWREC_SOURCEVN, GenDb::DbDataType::UTF8Type);
    flow_msg2type_map[g_viz_constants.FlowRecordNames[FlowRecordFields::FLOWREC_SOURCEIP]] =
         FlowTypeInfo(FlowRecordFields::FLOWREC_SOURCEIP, GenDb::DbDataType::InetType);
    flow_msg2type_map[g_viz_constants.FlowRecordNames[FlowRecordFields::FLOWREC_DESTVN]] =
         FlowTypeInfo(FlowRecordFields::FLOWREC_DESTVN, GenDb::DbDataType::UTF8Type);
    flow_msg2type_map[g_viz_constants.FlowRecordNames[FlowRecordFields::FLOWREC_DESTIP]] =
         FlowTypeInfo(FlowRecordFields::FLOWREC_DESTIP, GenDb::DbDataType::InetType);
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
