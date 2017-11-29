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
    SessionRecordFields::SESSION_MAX> SessionValueArray;

typedef boost::function<bool (std::auto_ptr<GenDb::ColList>)> DbInsertCb;

#endif // ANALYTICS_DB_HANDLER_IMPL_H_
