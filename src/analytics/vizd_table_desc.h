/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __VIZD_TABLE_DESC_H__
#define __VIZD_TABLE_DESC_H__

#include <boost/tuple/tuple.hpp>
#include "viz_types.h"
#include "database/gendb_if.h"

extern std::vector<GenDb::NewCf> vizd_tables;
extern std::vector<GenDb::NewCf> vizd_flow_tables;
extern std::vector<GenDb::NewCf> vizd_stat_tables;
extern std::vector<GenDb::NewCf> vizd_session_tables;

typedef boost::tuple<FlowRecordFields::type, GenDb::DbDataType::type> FlowTypeInfo;
typedef boost::tuple<SessionRecordCols::type, GenDb::DbDataType::type> SessionTypeInfo;
typedef std::map<std::string, FlowTypeInfo> FlowTypeMap;
extern FlowTypeMap flow_msg2type_map;
typedef std::map<std::string, SessionTypeInfo> SessionTypeMap;
typedef std::map<std::string, uint8_t> TagsIdxMap;
extern TagsIdxMap tags_name2idx_map;
extern SessionTypeMap session_msg2type_map;

void init_vizd_tables(void);

#endif // __VIZD_TABLE_DESC_H__
