/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __UDC_MOCK_H__
#define __UDC_MOCK_H__

#include "usrdef_counters.h"

class UserDefinedCountersMock : public UserDefinedCounters {
  public:
    UserDefinedCountersMock(boost::shared_ptr<ConfigDBConnection> cfgdb_connection) :
      UserDefinedCounters(cfgdb_connection) {
    }
    

    MOCK_METHOD2(MatchFilter, void(std::string text, LineParser::WordListType *w));

};

#endif


