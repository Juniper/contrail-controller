/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "testing/gunit.h"
#include "base/logging.h"
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/random/linear_congruential.hpp>
#include <boost/random/uniform_int.hpp>

#include "query.h"
#include "analytics_query_mock.h"

using boost::get;

using ::testing::Return;
using ::testing::AnyNumber;

class SelectTest : public ::testing::Test {
public:
    // bool indicates whether the ResultRowT is verified or not
    typedef std::pair<QEOpServerProxy::ResultRowT,bool> RowT;
    typedef std::vector<RowT> BufferT;

    SelectTest() {
    }

    ~SelectTest() {
    }

    virtual void SetUp() {
    }

    virtual void TearDown() {
    }

    void select_fs_query_default_expect_init(AnalyticsQueryMock& aqmock) {
        EXPECT_CALL(aqmock, table())
            .Times(AnyNumber())
            .WillRepeatedly(Return(g_viz_constants.FLOW_SERIES_TABLE));
        EXPECT_CALL(aqmock, is_object_table_query())
            .Times(AnyNumber())
            .WillRepeatedly(Return(false));
        EXPECT_CALL(aqmock, is_stat_table_query())
            .Times(AnyNumber())
            .WillRepeatedly(Return(false));
        EXPECT_CALL(aqmock, is_flow_query())
            .Times(AnyNumber())
            .WillRepeatedly(Return(true));
        EXPECT_CALL(aqmock, direction_ing())
            .Times(AnyNumber())
            .WillRepeatedly(Return(1));
    }
};

TEST_F(SelectTest, SelectValidQuery) {
    AnalyticsQueryMock analytics_query_mock;
    select_fs_query_default_expect_init(analytics_query_mock);

    std::map<std::string, std::string> json_select;
    json_select.insert(std::pair<std::string, std::string>(
        "select_fields", "[\"T\", \"sum(bytes)\", \"sum(packets)\"]"));
    SelectQuery* select_query = new SelectQuery(&analytics_query_mock,
                                                json_select);
    EXPECT_TRUE(select_query->result_.get());
}

int main(int argc, char **argv) {
    LoggingInit();
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
