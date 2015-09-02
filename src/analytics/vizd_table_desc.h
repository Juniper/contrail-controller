/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __VIZD_TABLE_DESC_H__
#define __VIZD_TABLE_DESC_H__

#include <boost/tuple/tuple.hpp>
#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh.h>
#include "viz_types.h"
#include "gendb_if.h"

extern std::vector<GenDb::NewCf> vizd_tables;
extern std::vector<GenDb::NewCf> vizd_flow_tables;
extern std::vector<GenDb::NewCf> vizd_stat_tables;

typedef boost::tuple<FlowRecordFields::type, GenDb::DbDataType::type> FlowTypeInfo;
typedef std::map<std::string, FlowTypeInfo> FlowTypeMap;
extern FlowTypeMap flow_msg2type_map;

void init_vizd_tables();

#endif // __VIZD_TABLE_DESC_H__
