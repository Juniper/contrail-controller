/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "vizd_table_desc.h"

#include <boost/assign/list_of.hpp>
#include "viz_constants.h"
#include <database/gendb_constants.h>

std::vector<GenDb::NewCf> vizd_tables;
std::vector<GenDb::NewCf> vizd_stat_tables;
std::vector<GenDb::NewCf> vizd_session_tables;
SessionTypeMap session_msg2type_map;

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

    init_tables(vizd_stat_tables, g_viz_constants._VIZD_STAT_TABLE_SCHEMA);

    init_tables(vizd_session_tables, g_viz_constants._VIZD_SESSION_TABLE_SCHEMA);

    session_msg2type_map[g_viz_constants.SessionRecordNames[
            SessionRecordFields::SESSION_IS_CLIENT_SESSION]] =
        SessionTypeInfo(SessionRecordFields::SESSION_IS_CLIENT_SESSION,
            GenDb::DbDataType::Unsigned8Type);
    session_msg2type_map[g_viz_constants.SessionRecordNames[
            SessionRecordFields::SESSION_IS_SI]] =
        SessionTypeInfo(SessionRecordFields::SESSION_IS_SI,
            GenDb::DbDataType::Unsigned8Type);
    session_msg2type_map[g_viz_constants.SessionRecordNames[
            SessionRecordFields::SESSION_PROTOCOL]] =
        SessionTypeInfo(SessionRecordFields::SESSION_PROTOCOL,
            GenDb::DbDataType::Unsigned16Type);
    session_msg2type_map[g_viz_constants.SessionRecordNames[
            SessionRecordFields::SESSION_SPORT]] =
        SessionTypeInfo(SessionRecordFields::SESSION_SPORT,
            GenDb::DbDataType::Unsigned16Type);
    session_msg2type_map[g_viz_constants.SessionRecordNames[
            SessionRecordFields::SESSION_DEPLOYMENT]] =
        SessionTypeInfo(SessionRecordFields::SESSION_DEPLOYMENT,
            GenDb::DbDataType::UTF8Type);
    session_msg2type_map[g_viz_constants.SessionRecordNames[
            SessionRecordFields::SESSION_TIER]] =
        SessionTypeInfo(SessionRecordFields::SESSION_TIER,
            GenDb::DbDataType::UTF8Type);
    session_msg2type_map[g_viz_constants.SessionRecordNames[
            SessionRecordFields::SESSION_APPLICATION]] =
        SessionTypeInfo(SessionRecordFields::SESSION_APPLICATION,
            GenDb::DbDataType::UTF8Type);
    session_msg2type_map[g_viz_constants.SessionRecordNames[
            SessionRecordFields::SESSION_SITE]] =
        SessionTypeInfo(SessionRecordFields::SESSION_SITE,
            GenDb::DbDataType::UTF8Type);
    session_msg2type_map[g_viz_constants.SessionRecordNames[
            SessionRecordFields::SESSION_LABELS]] =
        SessionTypeInfo(SessionRecordFields::SESSION_LABELS,
            GenDb::DbDataType::UTF8Type);
    session_msg2type_map[g_viz_constants.SessionRecordNames[
            SessionRecordFields::SESSION_REMOTE_DEPLOYMENT]] =
        SessionTypeInfo(SessionRecordFields::SESSION_REMOTE_DEPLOYMENT,
            GenDb::DbDataType::UTF8Type);
    session_msg2type_map[g_viz_constants.SessionRecordNames[
            SessionRecordFields::SESSION_REMOTE_TIER]] =
        SessionTypeInfo(SessionRecordFields::SESSION_REMOTE_TIER,
            GenDb::DbDataType::UTF8Type);
    session_msg2type_map[g_viz_constants.SessionRecordNames[
            SessionRecordFields::SESSION_REMOTE_APPLICATION]] =
        SessionTypeInfo(SessionRecordFields::SESSION_REMOTE_APPLICATION,
            GenDb::DbDataType::UTF8Type);
    session_msg2type_map[g_viz_constants.SessionRecordNames[
            SessionRecordFields::SESSION_REMOTE_SITE]] =
        SessionTypeInfo(SessionRecordFields::SESSION_REMOTE_SITE,
            GenDb::DbDataType::UTF8Type);
    session_msg2type_map[g_viz_constants.SessionRecordNames[
            SessionRecordFields::SESSION_REMOTE_LABELS]] =
        SessionTypeInfo(SessionRecordFields::SESSION_REMOTE_LABELS,
            GenDb::DbDataType::UTF8Type);
    session_msg2type_map[g_viz_constants.SessionRecordNames[
            SessionRecordFields::SESSION_SECURITY_POLICY_RULE]] =
        SessionTypeInfo(SessionRecordFields::SESSION_SECURITY_POLICY_RULE,
            GenDb::DbDataType::UTF8Type);
    session_msg2type_map[g_viz_constants.SessionRecordNames[
            SessionRecordFields::SESSION_REMOTE_PREFIX]] =
        SessionTypeInfo(SessionRecordFields::SESSION_REMOTE_PREFIX,
            GenDb::DbDataType::UTF8Type);
    session_msg2type_map[g_viz_constants.SessionRecordNames[
            SessionRecordFields::SESSION_VMI]] =
        SessionTypeInfo(SessionRecordFields::SESSION_VMI,
            GenDb::DbDataType::AsciiType);
    session_msg2type_map[g_viz_constants.SessionRecordNames[
            SessionRecordFields::SESSION_IP]] =
        SessionTypeInfo(SessionRecordFields::SESSION_IP,
            GenDb::DbDataType::AsciiType);
    session_msg2type_map[g_viz_constants.SessionRecordNames[
            SessionRecordFields::SESSION_VROUTER_IP]] =
        SessionTypeInfo(SessionRecordFields::SESSION_VROUTER_IP,
            GenDb::DbDataType::InetType);
    session_msg2type_map[g_viz_constants.SessionRecordNames[
            SessionRecordFields::SESSION_VN]] =
        SessionTypeInfo(SessionRecordFields::SESSION_VN,
            GenDb::DbDataType::UTF8Type);
    session_msg2type_map[g_viz_constants.SessionRecordNames[
            SessionRecordFields::SESSION_REMOTE_VN]] =
        SessionTypeInfo(SessionRecordFields::SESSION_REMOTE_VN,
            GenDb::DbDataType::UTF8Type);
    session_msg2type_map[g_viz_constants.SessionRecordNames[
            SessionRecordFields::SESSION_SAMPLED_FORWARD_BYTES]] =
        SessionTypeInfo(SessionRecordFields::SESSION_SAMPLED_FORWARD_BYTES,
            GenDb::DbDataType::Unsigned64Type);
    session_msg2type_map[g_viz_constants.SessionRecordNames[
            SessionRecordFields::SESSION_SAMPLED_FORWARD_PKTS]] =
        SessionTypeInfo(SessionRecordFields::SESSION_SAMPLED_FORWARD_PKTS,
            GenDb::DbDataType::Unsigned64Type);
    session_msg2type_map[g_viz_constants.SessionRecordNames[
            SessionRecordFields::SESSION_SAMPLED_REVERSE_BYTES]] =
        SessionTypeInfo(SessionRecordFields::SESSION_SAMPLED_REVERSE_BYTES,
            GenDb::DbDataType::Unsigned64Type);
    session_msg2type_map[g_viz_constants.SessionRecordNames[
            SessionRecordFields::SESSION_SAMPLED_REVERSE_PKTS]] =
        SessionTypeInfo(SessionRecordFields::SESSION_SAMPLED_REVERSE_PKTS,
            GenDb::DbDataType::Unsigned64Type);
    session_msg2type_map[g_viz_constants.SessionRecordNames[
            SessionRecordFields::SESSION_LOGGED_FORWARD_BYTES]] =
        SessionTypeInfo(SessionRecordFields::SESSION_LOGGED_FORWARD_BYTES,
            GenDb::DbDataType::Unsigned64Type);
    session_msg2type_map[g_viz_constants.SessionRecordNames[
            SessionRecordFields::SESSION_LOGGED_FORWARD_PKTS]] =
        SessionTypeInfo(SessionRecordFields::SESSION_LOGGED_FORWARD_PKTS,
            GenDb::DbDataType::Unsigned64Type);
    session_msg2type_map[g_viz_constants.SessionRecordNames[
            SessionRecordFields::SESSION_LOGGED_REVERSE_BYTES]] =
        SessionTypeInfo(SessionRecordFields::SESSION_LOGGED_REVERSE_BYTES,
            GenDb::DbDataType::Unsigned64Type);
    session_msg2type_map[g_viz_constants.SessionRecordNames[
            SessionRecordFields::SESSION_LOGGED_REVERSE_PKTS]] =
        SessionTypeInfo(SessionRecordFields::SESSION_LOGGED_REVERSE_PKTS,
            GenDb::DbDataType::Unsigned64Type);
}
