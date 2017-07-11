/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __SRC_ANALYTICS_TEST_USRDEF_COUNTERS_MOCK_H__
#define __SRC_ANALYTICS_TEST_USRDEF_COUNTERS_MOCK_H__

#include "usrdef_counters.h"

class UserDefinedCountersMock : public UserDefinedCounters {
  public:
    UserDefinedCountersMock(boost::shared_ptr<ConfigDBConnection> cfgdb_connection) :
      UserDefinedCounters(cfgdb_connection) {
    }
    
    MOCK_METHOD2(MatchFilter, void(std::string text, LineParser::WordListType *w));
};

#endif//__SRC_ANALYTICS_TEST_USRDEF_COUNTERS_MOCK_H__


