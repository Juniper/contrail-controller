/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#include "testing/gunit.h"

#include "query.h"
#include "analytics_query_mock.h"

using ::testing::Return;
using ::testing::AnyNumber;

class SelectTest : public ::testing::Test {
public:
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

// Invalid Selection of timeseries and sum(bytes)
TEST_F(SelectTest, InvalidFlowseriesSelectTQuery) {
    AnalyticsQueryMock analytics_query_mock;
    select_fs_query_default_expect_init(analytics_query_mock);

    std::map<std::string, std::string> json_select;
    json_select.insert(std::pair<std::string, std::string>(
        "select_fields", "[\"T\", \"sum(bytes)\", \"destv\"]"));
    SelectQuery* select_query = new SelectQuery(&analytics_query_mock,
                                                json_select);
    EXPECT_TRUE(select_query->status_details != 0);
}

// Invalid Selection of time granularity and bytes
TEST_F(SelectTest, InvalidFlowseriesSelectTSStats) {
    AnalyticsQueryMock analytics_query_mock;
    select_fs_query_default_expect_init(analytics_query_mock);

    std::map<std::string, std::string> json_select;
    int granularity = 10;
    std::stringstream ss;
    ss << "[\"T=" << granularity << "\", " << "\"bytes\"]";
    json_select.insert(std::pair<std::string, std::string>(
        "select_fields", ss.str()));
    granularity *= 1000000; // convert to microseconds

    SelectQuery* select_query = new SelectQuery(&analytics_query_mock,
                                                json_select);
    EXPECT_TRUE(select_query->status_details != 0);
}

// Invalid Selection of time granularity and packets
TEST_F(SelectTest, InvalidFlowseriesSelectTSStatsQuery) {
    AnalyticsQueryMock analytics_query_mock;
    select_fs_query_default_expect_init(analytics_query_mock);

    std::map<std::string, std::string> json_select;
    int granularity = 5;
    std::stringstream ss;
    ss << "[\"T=" << granularity << "\", " << "\"sourcevn\", "
       << "\"packets\"]";
    json_select.insert(std::pair<std::string, std::string>(
        "select_fields", ss.str()));
    granularity *= 1000000;

    SelectQuery* select_query = new SelectQuery(&analytics_query_mock,
                                                json_select);
    EXPECT_TRUE(select_query->status_details != 0);
}

<<<<<<< HEAD
// Invalid Selection of time sum(bytes) and raw packets
=======
// Invalid Selection of sum(bytes) and raw packets
>>>>>>> 6c1150e... Safeguard checks for qed crash when given invalid parameters Closes-Bug: #1344534
TEST_F(SelectTest, InvalidFlowseriesSelectStatsQuery) {
    AnalyticsQueryMock analytics_query_mock;
    select_fs_query_default_expect_init(analytics_query_mock);

    std::map<std::string, std::string> json_select;
    json_select.insert(std::pair<std::string, std::string>(
        "select_fields", "[\"sum(bytes)\", \"packets\"]"));
    SelectQuery* select_query = new SelectQuery(&analytics_query_mock,
                                                json_select);
    EXPECT_TRUE(select_query->status_details != 0);
}

int main(int argc, char **argv) {
    LoggingInit();
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
