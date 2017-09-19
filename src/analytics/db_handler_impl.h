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

typedef boost::array<GenDb::DbDataValue,
    SessionRecordFields::SESSION_MAX> SessionValueArray;

typedef boost::function<void (const std::string&, const std::string&, int)>
    FlowFieldValuesCb;
typedef boost::function<bool (std::auto_ptr<GenDb::ColList>)> DbInsertCb;

void PopulateFlowIndexTableColumnValues(
    const std::vector<FlowRecordFields::type> &frvt,
    const FlowValueArray &fvalues, GenDb::DbDataValueVec *cvalues,
    int ttl, FlowFieldValuesCb fncb);

struct T2IpIndex {
    uint32_t t2_;
    IpAddress ip_;

    T2IpIndex(uint32_t t2, IpAddress ip): t2_(t2), ip_(ip) {}
    T2IpIndex(): t2_(0), ip_(IpAddress()) {}
};
#endif // ANALYTICS_DB_HANDLER_IMPL_H_
