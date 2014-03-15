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

using ::testing::Return;
using ::testing::AnyNumber;

boost::random::mt19937 gen(std::time(NULL));
boost::random::uniform_int_distribution<> boost_rand(1, 10000);

class SelectFSQueryTest : public ::testing::Test {
public:
    SelectFSQueryTest() {
    }
    
    ~SelectFSQueryTest() {
    }
    
    virtual void SetUp() {
        populate_where_query_result();
    }
    
    virtual void TearDown() {
    }

    class FlowSeriesData {
    public:
        typedef std::vector<std::pair<uint64_t, flow_stats> > tstats_t;

        FlowSeriesData(flow_tuple tuple, uint64_t start_time,
                       int duration, int tdiff, int pdiff, int psize) :
            tuple_(tuple), start_time_(start_time), duration_(duration),
            tdiff_(tdiff*kSecInMsec), pdiff_(pdiff), psize_(psize),
            flow_uuid_(boost::uuids::random_generator()()) {
            uint64_t end_time = start_time_ + duration_*kSecInMsec;
            flow_stats stats;
            for (uint64_t t = start_time_; t < end_time; t += tdiff_) {
                stats.pkts = pdiff_;
                stats.bytes = pdiff_*psize_;
                uint64_t tdelta = boost_rand(gen);
                t_stats_.push_back(std::make_pair(t+tdelta, stats));
            }
        }

        ~FlowSeriesData() {}

        flow_tuple& tuple() {
            return tuple_;
        }

        tstats_t& t_stats() {
            return t_stats_;
        }

        boost::uuids::uuid& flow_uuid() {
            return flow_uuid_;
        }
    private:
        flow_tuple tuple_;
        uint64_t start_time_;
        int duration_; // in seconds
        int tdiff_;
        int pdiff_;
        int psize_;
        boost::uuids::uuid flow_uuid_; 
        std::vector<std::pair<uint64_t, flow_stats> > t_stats_;
        static const uint64_t kSecInMsec = 1000*1000;
    };

    void populate_where_query_result() {
        for (int i = 0; i < kNumFlows; i++) {
            flow_tuple& tuple = flowseries_data_[i].tuple();
            boost::uuids::uuid& fuuid = flowseries_data_[i].flow_uuid();
            FlowSeriesData::tstats_t& tstats = flowseries_data_[i].t_stats();
            FlowSeriesData::tstats_t::const_iterator it = tstats.begin();
            for (; it != tstats.end(); it++) {
                query_result_unit_t qres;
                qres.timestamp = it->first;
                qres.info.push_back(GenDb::DbDataValue(it->second.bytes));
                qres.info.push_back(GenDb::DbDataValue(it->second.pkts));
                qres.info.push_back(GenDb::DbDataValue(
                    static_cast<uint8_t>(0)));
                qres.info.push_back(GenDb::DbDataValue(fuuid));
                qres.info.push_back(GenDb::DbDataValue(tuple.vrouter));
                qres.info.push_back(GenDb::DbDataValue(tuple.source_vn));
                qres.info.push_back(GenDb::DbDataValue(tuple.dest_vn));
                qres.info.push_back(GenDb::DbDataValue(tuple.source_ip));
                qres.info.push_back(GenDb::DbDataValue(tuple.dest_ip));
                qres.info.push_back(GenDb::DbDataValue(
                    static_cast<uint8_t>(tuple.protocol)));
                qres.info.push_back(GenDb::DbDataValue(
                    static_cast<uint16_t>(tuple.source_port)));
                qres.info.push_back(GenDb::DbDataValue(
                    static_cast<uint16_t>(tuple.dest_port)));
                where_query_result_.push_back(qres);
            }
        }
    }

    std::vector<query_result_unit_t>& wherequery_result() {
        return where_query_result_;
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
        EXPECT_CALL(aqmock, from_time())
            .Times(AnyNumber())
            .WillRepeatedly(Return(SelectFSQueryTest::start_time()));
        EXPECT_CALL(aqmock, end_time())
            .Times(AnyNumber())
            .WillRepeatedly(Return(SelectFSQueryTest::end_time()));
        EXPECT_CALL(aqmock, req_from_time())
            .Times(AnyNumber())
            .WillRepeatedly(Return(SelectFSQueryTest::start_time()));
        EXPECT_CALL(aqmock, req_end_time())
            .Times(AnyNumber())
            .WillRepeatedly(Return(SelectFSQueryTest::end_time()));
    }

    static uint64_t start_time() {
        return start_time_;
    }

    static uint64_t end_time() {
        return end_time_;
    }
private:
    std::vector<query_result_unit_t> where_query_result_;
    const static uint64_t end_time_ = 1393915103000000; 
    const static uint64_t start_time_ = end_time_ - (60*1000*1000);
    const static int kNumFlows = 2;
    static FlowSeriesData* flowseries_data_;
    static FlowSeriesData* flowseries_data_init();
};

SelectFSQueryTest::FlowSeriesData*
SelectFSQueryTest::flowseries_data_init() {
    std::string vr1("vrouter1");
    std::string svn1("vn1");
    std::string dvn1("vn2");
    flow_tuple tuple1(vr1, svn1, dvn1, 0x0A0A0A01, 0x0B0B0B01, 
                      1, 10, 11, 1);
    SelectFSQueryTest::FlowSeriesData fs_data1(tuple1, 
        SelectFSQueryTest::start_time_, 60, 5, 1, 50);
    flow_tuple tuple2(vr1, svn1, dvn1, 0x0A0A0A02, 0x0B0B0B02,
                      2, 20, 21, 1);
    SelectFSQueryTest::FlowSeriesData fs_data2(tuple2, 
        SelectFSQueryTest::start_time_, 30, 4, 2, 100);
    static FlowSeriesData fs_data[] = {fs_data1, fs_data2};
    return fs_data;
}

SelectFSQueryTest::FlowSeriesData* SelectFSQueryTest::flowseries_data_ = 
    SelectFSQueryTest::flowseries_data_init(); 

TEST_F(SelectFSQueryTest, SelectT) {
    AnalyticsQueryMock analytics_query_mock;
    select_fs_query_default_expect_init(analytics_query_mock);
    
    std::map<std::string, std::string> json_select;
    json_select.insert(std::pair<std::string, std::string>(
        "select_fields", "[\"T\"]"));
    SelectQuery* select_query = new SelectQuery(&analytics_query_mock, 
                                                json_select);
    EXPECT_CALL(analytics_query_mock, where_query_result())
        .Times(1)
        .WillOnce(Invoke(this, &SelectFSQueryTest::wherequery_result));
    select_query->process_query();
    ASSERT_EQ(wherequery_result().size(), select_query->result_->size());
}

TEST_F(SelectFSQueryTest, SelectTS) {
    AnalyticsQueryMock analytics_query_mock;
    select_fs_query_default_expect_init(analytics_query_mock);

    EXPECT_CALL(analytics_query_mock, where_query_result())
        .Times(2)
        .WillRepeatedly(Invoke(this, &SelectFSQueryTest::wherequery_result));
    
    std::map<std::string, std::string> json_select;
    json_select.insert(std::pair<std::string, std::string>(
        "select_fields", "[\"T=30\"]"));
    SelectQuery* select_query = new SelectQuery(&analytics_query_mock, 
                                                json_select);
    select_query->process_query();
    ASSERT_EQ(2, select_query->result_->size());

    // granularity > (end_time - from_time)
    json_select.clear();
    json_select.insert(std::pair<std::string, std::string>(
        "select_fields", "[\"T=70\"]"));
    SelectQuery* select_query1 = new SelectQuery(&analytics_query_mock,
                                                 json_select);
    select_query1->process_query();
    ASSERT_EQ(1, select_query1->result_->size());
}

int main(int argc, char **argv) {
    LoggingInit();
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
