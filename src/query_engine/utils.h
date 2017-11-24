/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_QUERY_ENGINE_UTILS_H_
#define SRC_QUERY_ENGINE_UTILS_H_

#include <stdint.h>
#include <database/gendb_constants.h>
#include <database/gendb_if.h>
#include "viz_constants.h"
#include <analytics/viz_types.h>
#include "analytics/vizd_table_desc.h"

bool parse_time(const std::string& relative_time, uint64_t *usec_time);
GenDb::Op::type get_gendb_op_from_op(int op);
std::string MsgTableIndexToColumn(const int index);
ColIndexType::type MsgTableIndexToIndexType(const int index);
std::string MsgTableQueryColumnToColumn(const std::string query_column);
std::string MsgTableColumnToQueryColumn(const std::string columnN);

#endif //SRC_QUERY_ENGINE_UTILS_H_

