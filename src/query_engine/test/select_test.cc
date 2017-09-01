/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#include "testing/gunit.h"

#include "query.h"
#include "analytics_query_mock.h"
#include <boost/uuid/uuid.hpp>

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
        EXPECT_CALL(aqmock, is_session_query())
            .Times(AnyNumber())
            .WillRepeatedly(Return(false));

    }

    bool test_process_object_query_specific_select_params(
                        const std::string& sel_field,
                        std::map<std::string, GenDb::DbDataValue>& col_res_map,
                        std::map<std::string, std::string>& cmap,
                        const boost::uuids::uuid& uuid,
                        std::map<boost::uuids::uuid, std::string>&
                        uuid_to_objid_map, SelectQuery *sq) {
         return sq->process_object_query_specific_select_params(sel_field,
             col_res_map, cmap, uuid, uuid_to_objid_map);
    }
};

// Invalid Selection of timeseries and sum(bytes)
TEST_F(SelectTest, InvalidFlowseriesSelectTQuery) {
    AnalyticsQueryMock analytics_query_mock;
    select_fs_query_default_expect_init(analytics_query_mock);

    std::map<std::string, std::string> json_select;
    json_select.insert(std::pair<std::string, std::string>(
        "select_fields", "[\"T\", \"sum(bytes)\", \"destvn\"]"));
    SelectQuery* select_query = new SelectQuery(&analytics_query_mock,
                                                json_select);
    EXPECT_TRUE(select_query->status_details == EINVAL);
}

// Invalid Selection of time granularity and bytes
TEST_F(SelectTest, InvalidFlowseriesSelectTSStatsQuery) {
    AnalyticsQueryMock analytics_query_mock;
    select_fs_query_default_expect_init(analytics_query_mock);

    std::map<std::string, std::string> json_select;
    int granularity = 10;
    std::stringstream ss;
    ss << "[\"T=" << granularity << "\", " << "\"bytes\"]";
    json_select.insert(std::pair<std::string, std::string>(
        "select_fields", ss.str()));

    SelectQuery* select_query = new SelectQuery(&analytics_query_mock,
                                                json_select);
    EXPECT_TRUE(select_query->status_details == EINVAL);
}

// Invalid Selection of time granularity and packets
TEST_F(SelectTest, InvalidFlowseriesSelectTSStatsTupleQuery) {
    AnalyticsQueryMock analytics_query_mock;
    select_fs_query_default_expect_init(analytics_query_mock);

    std::map<std::string, std::string> json_select;
    int granularity = 5;
    std::stringstream ss;
    ss << "[\"T=" << granularity << "\", " << "\"sourcevn\", "
       << "\"packets\"]";
    json_select.insert(std::pair<std::string, std::string>(
        "select_fields", ss.str()));

    SelectQuery* select_query = new SelectQuery(&analytics_query_mock,
                                                json_select);
    EXPECT_TRUE(select_query->status_details == EINVAL);
}

// Invalid Selection of sum(bytes) and raw packets
TEST_F(SelectTest, InvalidFlowseriesSelectStatsQuery) {
    AnalyticsQueryMock analytics_query_mock;
    select_fs_query_default_expect_init(analytics_query_mock);

    std::map<std::string, std::string> json_select;
    json_select.insert(std::pair<std::string, std::string>(
        "select_fields", "[\"sum(bytes)\", \"packets\"]"));
    SelectQuery* select_query = new SelectQuery(&analytics_query_mock,
                                                json_select);
    EXPECT_TRUE(select_query->status_details == EINVAL);
}

/* This test checks if process_object_query_specific_select_params
 * function can work correctly when it is passed a object_id as a
 * select field
 */

TEST_F(SelectTest, TestProcessObjectQuerySelectParams) {
    AnalyticsQueryMock analytics_query_mock;
    select_fs_query_default_expect_init(analytics_query_mock);

    std::map<std::string, std::string> json_select;
    json_select.insert(std::pair<std::string, std::string>(
        "select_fields", "[\"sum(bytes)\", \"packets\", \"ObjecId\"]"));
    SelectQuery* select_query = new SelectQuery(&analytics_query_mock,
                                                json_select);
    std::string select_field("ObjectId");
    // This represents the columns fetched from the
    // MessageTable.
    std::map<std::string, GenDb::DbDataValue> col_res_map;
    GenDb::DbDataValue sandesh_type;
    sandesh_type=(uint32_t)7; // corresposnds to SandeshType::Object
    col_res_map.insert(std::make_pair("Type",sandesh_type));
    std::map<std::string, std::string> cmap;
    boost::uuids::random_generator rgen_;
    boost::uuids::uuid unm(rgen_());
    // This is the map looked up to get the object id based on uuid
    std::map<boost::uuids::uuid, std::string> uuid_to_objectid;
    uuid_to_objectid.insert(std::make_pair(unm, "id1"));
    if(test_process_object_query_specific_select_params(select_field,
        col_res_map, cmap, unm, uuid_to_objectid, select_query)) {
        std::map<std::string, std::string>::iterator cit;
        cit = cmap.find(select_field);
        ASSERT_TRUE(cit != cmap.end());
        std::string actual_val = cit->second;
        EXPECT_EQ(actual_val, "id1");
   } else {
        ASSERT_TRUE(0);
   }

}

int main(int argc, char **argv) {
    LoggingInit();
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
