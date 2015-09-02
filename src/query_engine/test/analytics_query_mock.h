/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "gmock/gmock.h"

#include "query.h"
#include "analytics/test/cdb_if_mock.h"

class AnalyticsQueryMock : public AnalyticsQuery {
public:
    AnalyticsQueryMock() : 
        AnalyticsQuery(std::string(""), new CdbIfMock(),
                       std::map<std::string, std::string>(),
                       uint64_t(0), int(0), int(0)) {
    }
    
    ~AnalyticsQueryMock() {
    }

    MOCK_CONST_METHOD0(table, std::string());
    MOCK_CONST_METHOD0(from_time, uint64_t());
    MOCK_CONST_METHOD0(end_time, uint64_t());
    MOCK_CONST_METHOD0(req_from_time, uint64_t());
    MOCK_CONST_METHOD0(req_end_time, uint64_t());
    MOCK_CONST_METHOD0(direction_ing, uint32_t());
    MOCK_METHOD0(where_query_result, 
                 std::vector<query_result_unit_t>&());
    MOCK_METHOD0(is_object_table_query, bool());
    MOCK_METHOD0(is_stat_table_query, bool());
    MOCK_METHOD0(is_flow_query, bool());
    MOCK_METHOD0(is_query_parallelized, bool());
    MOCK_METHOD4(Init, void(GenDb::GenDbIf*, std::string, 
                 std::map<std::string, std::string>&, uint64_t));
};
