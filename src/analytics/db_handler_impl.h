//
// Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
//

#ifndef ANALYTICS_DB_HANDLER_IMPL_H_
#define ANALYTICS_DB_HANDLER_IMPL_H_

#include <vector>

#include <boost/array.hpp>
#include <boost/function.hpp>

#include <analytics/viz_types.h>
#include <database/gendb_if.h>

typedef boost::array<GenDb::DbDataValue,
    FlowRecordFields::FLOWREC_MAX> FlowValueArray;

typedef boost::function<void (const std::string&, const std::string&, int)>
    FlowFieldValuesCb;

void PopulateFlowIndexTableColumnValues(
    const std::vector<FlowRecordFields::type> &frvt,
    const FlowValueArray &fvalues, GenDb::DbDataValueVec *cvalues,
    int ttl, FlowFieldValuesCb fncb);

#endif // ANALYTICS_DB_HANDLER_IMPL_H_
