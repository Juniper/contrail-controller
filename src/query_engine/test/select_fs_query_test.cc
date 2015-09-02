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
using boost::make_tuple;

using ::testing::Return;
using ::testing::AnyNumber;

class SelectFSQueryTest : public ::testing::Test {
public:
    // bool indicates whether the ResultRowT is verified or not
    typedef std::pair<QEOpServerProxy::ResultRowT,bool> RowT;
    typedef std::vector<RowT> BufferT;

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
            sum_bytes_(0), sum_pkts_(0),
            flow_uuid_(boost::uuids::random_generator()()) {
            uint64_t end_time = start_time_ + duration_*kSecInMsec;
            flow_stats stats;
            for (uint64_t t = start_time_; t < end_time; t += tdiff_) {
                stats.pkts = pdiff_;
                stats.bytes = pdiff_*psize_;
                sum_pkts_ += stats.pkts;
                sum_bytes_ += stats.bytes;
                t_stats_.push_back(std::make_pair(t, stats));
            }
        }

        ~FlowSeriesData() {}

        flow_tuple& tuple() {
            return tuple_;
        }

        tstats_t& t_stats() {
            return t_stats_;
        }

        uint64_t sum_bytes() {
            return sum_bytes_;
        }

        uint64_t sum_pkts() {
            return sum_pkts_;
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
        uint64_t sum_bytes_;
        uint64_t sum_pkts_;
        boost::uuids::uuid flow_uuid_; 
        std::vector<std::pair<uint64_t, flow_stats> > t_stats_;
        static const uint64_t kSecInMsec = 1000*1000;
    };

    void update_where_query_result(FlowSeriesData& fs_data, 
            std::vector<query_result_unit_t>& where_query_result) {
            flow_tuple& tuple = fs_data.tuple();
            boost::uuids::uuid& fuuid = fs_data.flow_uuid();
            FlowSeriesData::tstats_t& tstats = fs_data.t_stats();
            FlowSeriesData::tstats_t::const_iterator it;
            for (it = tstats.begin(); it != tstats.end(); it++) {
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
                where_query_result.push_back(qres);
            }
    }

    void populate_where_query_result() {
        for (int i = 0; i < kNumFlows; i++) {
            update_where_query_result(flowseries_data_[i], 
                                      where_query_result_);
        }
    }

    std::vector<query_result_unit_t>& wherequery_result() {
        return where_query_result_;
    }

    void verify_select_fs_query_result(SelectFSQueryTest::BufferT *expected_res,
                                       QEOpServerProxy::BufferT *actual_res) {
        ASSERT_EQ(expected_res->size(), actual_res->size());
        
        for (QEOpServerProxy::BufferT::iterator ait = 
             actual_res->begin(); ait != actual_res->end(); ait++) {
            QEOpServerProxy::OutRowT& arow = ait->first;
            bool match = false;
            for (SelectFSQueryTest::BufferT::iterator eit = 
                 expected_res->begin(); eit != expected_res->end(); eit++) {
                // Skip the row if it is already verified.
                if (eit->second) {
                    continue;
                }
                QEOpServerProxy::OutRowT& erow = eit->first.first;
                QEOpServerProxy::OutRowT::iterator erow_it;
                match = true;
                for (QEOpServerProxy::OutRowT::iterator erow_it = 
                     erow.begin(); erow_it != erow.end(); erow_it++) {
                    QEOpServerProxy::OutRowT::iterator arow_it;
                    arow_it = erow.find(erow_it->first);
                    ASSERT_NE(arow_it, arow.end());
                    if (erow_it->second != arow_it->second) {
                        match = false;
                        break;
                    }
                }
                if (match) {
                    // mark the row as verified. 
                    // This aids in catching duplicate rows
                    eit->second = true;
                    break;
                }
            }
            ASSERT_EQ(true, match);
        }
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

    static int num_flows() {
        return kNumFlows;
    }

    static FlowSeriesData* flowseries_data() {
        return flowseries_data_;
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
    
    EXPECT_CALL(analytics_query_mock, where_query_result())
        .Times(1)
        .WillOnce(Invoke(this, &SelectFSQueryTest::wherequery_result));
    
    // Fill the expected result
    SelectFSQueryTest::BufferT expected_res;
    std::vector<query_result_unit_t>& where_qres = wherequery_result();
    for (std::vector<query_result_unit_t>::const_iterator it = 
         where_qres.begin(); it != where_qres.end(); it++) {
        boost::shared_ptr<fsMetaData> metadata;
        QEOpServerProxy::OutRowT cmap;
        cmap.insert(std::make_pair(TIMESTAMP_FIELD, 
                                   integerToString((*it).timestamp)));
        QEOpServerProxy::ResultRowT rrow(std::make_pair(cmap, metadata));
        expected_res.push_back(std::make_pair(rrow, false));
    }

    SelectQuery* select_query = new SelectQuery(&analytics_query_mock, 
                                                json_select);
    select_query->process_query();
    verify_select_fs_query_result(&expected_res, select_query->result_.get());
}

TEST_F(SelectFSQueryTest, SelectTS) {
    AnalyticsQueryMock analytics_query_mock;
    select_fs_query_default_expect_init(analytics_query_mock);

    int granularity = 7;
    std::stringstream ss;
    ss << "[\"T=" << granularity << "\"]";
    std::map<std::string, std::string> json_select;
    json_select.insert(std::pair<std::string, std::string>(
        "select_fields", ss.str()));
    granularity *= 1000000; // convert to microseconds 

    EXPECT_CALL(analytics_query_mock, where_query_result())
        .Times(1)
        .WillOnce(Invoke(this, &SelectFSQueryTest::wherequery_result));

    // Fill the expected result
    SelectFSQueryTest::BufferT expected_res;
    int num_ts = 8; // based on the generated flow samples
    for (int i = 0; i < num_ts; i++) {
        uint64_t ts = start_time() + (i*granularity);
        boost::shared_ptr<fsMetaData> metadata;
        QEOpServerProxy::OutRowT cmap;
        cmap.insert(std::make_pair(TIMESTAMP_FIELD, integerToString(ts)));
        QEOpServerProxy::ResultRowT rrow(std::make_pair(cmap, metadata));
        expected_res.push_back(std::make_pair(rrow, false));
    }
    
    SelectQuery* select_query = new SelectQuery(&analytics_query_mock, 
                                                json_select);
    select_query->process_query();
    verify_select_fs_query_result(&expected_res, select_query->result_.get());
}

TEST_F(SelectFSQueryTest, SelectFlowTuple) {
    AnalyticsQueryMock analytics_query_mock;
    select_fs_query_default_expect_init(analytics_query_mock);
   
    std::stringstream ss;
    ss << "[\"vrouter\", \"sourcevn\", \"sourceip\", \"destvn\", \"destip\"" 
       << ", \"protocol\", \"sport\", \"dport\"]";
    std::map<std::string, std::string> json_select;
    json_select.insert(std::pair<std::string, std::string>(
        "select_fields", ss.str())); 
   
    EXPECT_CALL(analytics_query_mock, where_query_result())
        .Times(1)
        .WillOnce(Invoke(this, &SelectFSQueryTest::wherequery_result));
    
    // Fill the expected result
    SelectFSQueryTest::BufferT expected_res;
    FlowSeriesData* fs_data = flowseries_data();
    for (int i = 0; i < num_flows(); i++) {
        FlowSeriesData::tstats_t& tstats = fs_data[i].t_stats();
        flow_tuple& tuple = fs_data[i].tuple();
        for (FlowSeriesData::tstats_t::const_iterator it = tstats.begin();
             it != tstats.end(); it++) {
            QEOpServerProxy::OutRowT cmap;
            cmap.insert(std::make_pair(g_viz_constants.FlowRecordNames[
                FlowRecordFields::FLOWREC_VROUTER], tuple.vrouter));
            cmap.insert(std::make_pair(g_viz_constants.FlowRecordNames[
                FlowRecordFields::FLOWREC_SOURCEVN], tuple.source_vn));
            cmap.insert(std::make_pair(g_viz_constants.FlowRecordNames[
                FlowRecordFields::FLOWREC_SOURCEIP], 
                integerToString(tuple.source_ip)));
            cmap.insert(std::make_pair(g_viz_constants.FlowRecordNames[
                FlowRecordFields::FLOWREC_DESTVN], tuple.dest_vn));
            cmap.insert(std::make_pair(g_viz_constants.FlowRecordNames[
                FlowRecordFields::FLOWREC_DESTIP], 
                integerToString(tuple.dest_ip)));
            cmap.insert(std::make_pair(g_viz_constants.FlowRecordNames[
                FlowRecordFields::FLOWREC_PROTOCOL], 
                integerToString(tuple.protocol)));
            cmap.insert(std::make_pair(g_viz_constants.FlowRecordNames[
                FlowRecordFields::FLOWREC_SPORT], 
                integerToString(tuple.source_port)));
            cmap.insert(std::make_pair(g_viz_constants.FlowRecordNames[
                FlowRecordFields::FLOWREC_DPORT],
                integerToString(tuple.dest_port)));
            boost::shared_ptr<fsMetaData> metadata;
            QEOpServerProxy::ResultRowT rrow(std::make_pair(cmap, metadata));
            expected_res.push_back(std::make_pair(rrow, false));
        }
    }

    SelectQuery* select_query = new SelectQuery(&analytics_query_mock, 
                                                json_select);
    select_query->process_query();
    verify_select_fs_query_result(&expected_res, select_query->result_.get());
}

TEST_F(SelectFSQueryTest, SelectStats) {
    AnalyticsQueryMock analytics_query_mock;
    select_fs_query_default_expect_init(analytics_query_mock);

    std::map<std::string, std::string> json_select;
    json_select.insert(std::pair<std::string, std::string>(
        "select_fields", "[\"sum(packets)\", \"sum(bytes)\"]"));

    EXPECT_CALL(analytics_query_mock, where_query_result())
        .Times(1)
        .WillRepeatedly(Invoke(this, &SelectFSQueryTest::wherequery_result));
    
    // Fill the expected result
    FlowSeriesData* fs_data = flowseries_data(); 
    flow_stats sum_stats;
    for (int i = 0; i < num_flows(); i++) {
        sum_stats.pkts += fs_data[i].sum_pkts();
        sum_stats.bytes += fs_data[i].sum_bytes();
    }
    QEOpServerProxy::OutRowT cmap;
    cmap.insert(std::make_pair(SELECT_SUM_PACKETS, 
                               integerToString(sum_stats.pkts)));
    cmap.insert(std::make_pair(SELECT_SUM_BYTES,
                               integerToString(sum_stats.bytes)));
    boost::shared_ptr<fsMetaData> metadata;
    QEOpServerProxy::ResultRowT rrow(std::make_pair(cmap, metadata));
    SelectFSQueryTest::BufferT expected_res;
    expected_res.push_back(std::make_pair(rrow, false));

    SelectQuery* select_query = new SelectQuery(&analytics_query_mock, 
                                                json_select);
    select_query->process_query();
    verify_select_fs_query_result(&expected_res, select_query->result_.get());
}

TEST_F(SelectFSQueryTest, SelectStatsWithFlowcount) {
    AnalyticsQueryMock analytics_query_mock;
    select_fs_query_default_expect_init(analytics_query_mock);

    std::map<std::string, std::string> json_select;
    json_select.insert(std::pair<std::string, std::string>(
        "select_fields", "[\"sum(packets)\", \"flow_count\"]"));

    EXPECT_CALL(analytics_query_mock, where_query_result())
        .Times(1)
        .WillRepeatedly(Invoke(this, &SelectFSQueryTest::wherequery_result));

    // Fill the expected result
    FlowSeriesData* fs_data = flowseries_data(); 
    flow_stats sum_stats;
    for (int i = 0; i < num_flows(); i++) {
        sum_stats.pkts += fs_data[i].sum_pkts();
        sum_stats.bytes += fs_data[i].sum_bytes();
    }
    QEOpServerProxy::OutRowT cmap;
    cmap.insert(std::make_pair(SELECT_SUM_PACKETS, 
                               integerToString(sum_stats.pkts)));
    cmap.insert(std::make_pair(SELECT_FLOW_COUNT, integerToString(2)));
    boost::shared_ptr<fsMetaData> metadata;
    QEOpServerProxy::ResultRowT rrow(std::make_pair(cmap, metadata));
    SelectFSQueryTest::BufferT expected_res;
    expected_res.push_back(std::make_pair(rrow, false));
    
    SelectQuery* select_query = new SelectQuery(&analytics_query_mock, 
                                                json_select);
    select_query->process_query();
    verify_select_fs_query_result(&expected_res, select_query->result_.get());
}

TEST_F(SelectFSQueryTest, SelectTFlowTuple) {
    AnalyticsQueryMock analytics_query_mock;
    select_fs_query_default_expect_init(analytics_query_mock);

    std::stringstream ss;
    ss << "[\"T\", \"vrouter\", \"sourcevn\", \"destvn\"]"; 
    std::map<std::string, std::string> json_select;
    json_select.insert(std::pair<std::string, std::string>(
        "select_fields", ss.str())); 
   
    EXPECT_CALL(analytics_query_mock, where_query_result())
        .Times(1)
        .WillOnce(Invoke(this, &SelectFSQueryTest::wherequery_result));
    
    // Fill the expected result
    SelectFSQueryTest::BufferT expected_res;
    FlowSeriesData* fs_data = flowseries_data(); 
    for (int i = 0; i < num_flows(); i++) {
        FlowSeriesData::tstats_t& tstats = fs_data[i].t_stats();
        flow_tuple& tuple = fs_data[i].tuple();
        for (FlowSeriesData::tstats_t::const_iterator it = tstats.begin();
             it != tstats.end(); it++) {
            QEOpServerProxy::OutRowT cmap;
            cmap.insert(std::make_pair(TIMESTAMP_FIELD,
                integerToString(it->first)));
            cmap.insert(std::make_pair(g_viz_constants.FlowRecordNames[
                FlowRecordFields::FLOWREC_VROUTER], tuple.vrouter));
            cmap.insert(std::make_pair(g_viz_constants.FlowRecordNames[
                FlowRecordFields::FLOWREC_SOURCEVN], tuple.source_vn));
            cmap.insert(std::make_pair(g_viz_constants.FlowRecordNames[
                FlowRecordFields::FLOWREC_DESTVN], tuple.dest_vn));
            boost::shared_ptr<fsMetaData> metadata;
            QEOpServerProxy::ResultRowT rrow(std::make_pair(cmap, metadata));
            expected_res.push_back(std::make_pair(rrow, false));
        }
    }

    SelectQuery* select_query = new SelectQuery(&analytics_query_mock, 
                                                json_select);
    select_query->process_query();
    verify_select_fs_query_result(&expected_res, select_query->result_.get());
}

TEST_F(SelectFSQueryTest, SelectTStats) {
    AnalyticsQueryMock analytics_query_mock;
    select_fs_query_default_expect_init(analytics_query_mock);
    
    std::map<std::string, std::string> json_select;
    json_select.insert(std::pair<std::string, std::string>(
        "select_fields", "[\"T\", \"bytes\", \"packets\"]")); 
   
    EXPECT_CALL(analytics_query_mock, where_query_result())
        .Times(1)
        .WillOnce(Invoke(this, &SelectFSQueryTest::wherequery_result));

    // Fill the expected result
    SelectFSQueryTest::BufferT expected_res;
    FlowSeriesData* fs_data = flowseries_data(); 
    for (int i = 0; i < num_flows(); i++) {
        FlowSeriesData::tstats_t& tstats = fs_data[i].t_stats();
        for (FlowSeriesData::tstats_t::const_iterator it = tstats.begin();
             it != tstats.end(); it++) {
            boost::shared_ptr<fsMetaData> metadata;
            QEOpServerProxy::OutRowT cmap;
            cmap.insert(std::make_pair(TIMESTAMP_FIELD, 
                                       integerToString(it->first)));
            cmap.insert(std::make_pair(SELECT_PACKETS, 
                                       integerToString(it->second.pkts)));
            cmap.insert(std::make_pair(SELECT_BYTES,
                                       integerToString(it->second.bytes)));
            QEOpServerProxy::ResultRowT rrow(std::make_pair(cmap, metadata));
            expected_res.push_back(std::make_pair(rrow, false));
        }
    }
    
    SelectQuery* select_query = new SelectQuery(&analytics_query_mock, 
                                                json_select);
    select_query->process_query();
    verify_select_fs_query_result(&expected_res, select_query->result_.get());
}

TEST_F(SelectFSQueryTest, SelectFlowTupleStats) {
    AnalyticsQueryMock analytics_query_mock;
    select_fs_query_default_expect_init(analytics_query_mock);

    std::stringstream ss;
    ss << "[\"sourcevn\", \"sourceip\", \"sum(bytes)\", \"sum(packets)\"]";
    std::map<std::string, std::string> json_select;
    json_select.insert(std::pair<std::string, std::string>(
        "select_fields", ss.str()));

    EXPECT_CALL(analytics_query_mock, where_query_result())
        .Times(1)
        .WillRepeatedly(Invoke(this, &SelectFSQueryTest::wherequery_result));

    // Fill the expected result
    SelectFSQueryTest::BufferT expected_res;
    FlowSeriesData* fs_data = flowseries_data(); 
    for (int i = 0; i < num_flows(); i++) {
        flow_tuple& tuple = fs_data[i].tuple();
        boost::shared_ptr<fsMetaData> metadata;
        QEOpServerProxy::OutRowT cmap;
        cmap.insert(std::make_pair(g_viz_constants.FlowRecordNames[
            FlowRecordFields::FLOWREC_SOURCEVN], tuple.source_vn));
        cmap.insert(std::make_pair(g_viz_constants.FlowRecordNames[
            FlowRecordFields::FLOWREC_SOURCEIP], 
            integerToString(tuple.source_ip)));
        cmap.insert(std::make_pair(SELECT_SUM_PACKETS, 
                                   integerToString(fs_data[i].sum_pkts())));
        cmap.insert(std::make_pair(SELECT_SUM_BYTES,
                                   integerToString(fs_data[i].sum_bytes())));
        QEOpServerProxy::ResultRowT rrow(std::make_pair(cmap, metadata));
        expected_res.push_back(std::make_pair(rrow, false));
    }

    SelectQuery* select_query = new SelectQuery(&analytics_query_mock, 
                                                json_select);
    select_query->process_query();
    verify_select_fs_query_result(&expected_res, select_query->result_.get());
}

TEST_F(SelectFSQueryTest, SelectFlowTupleStatsWithFlowcount) {
    AnalyticsQueryMock analytics_query_mock;
    select_fs_query_default_expect_init(analytics_query_mock);

    std::stringstream ss;
    ss << "[\"sourcevn\", \"sum(bytes)\", \"flow_count\"]";
    std::map<std::string, std::string> json_select;
    json_select.insert(std::pair<std::string, std::string>(
        "select_fields", ss.str()));

    EXPECT_CALL(analytics_query_mock, where_query_result())
        .Times(1)
        .WillRepeatedly(Invoke(this, &SelectFSQueryTest::wherequery_result));

    // Fill the expected result
    SelectFSQueryTest::BufferT expected_res;
    FlowSeriesData* fs_data = flowseries_data(); 
    uint64_t sum_bytes;
    for (int i = 0; i < num_flows(); i++) {
        sum_bytes += fs_data[i].sum_bytes();
    }
    flow_tuple& tuple = fs_data[0].tuple();
    boost::shared_ptr<fsMetaData> metadata;
    QEOpServerProxy::OutRowT cmap;
    cmap.insert(std::make_pair(g_viz_constants.FlowRecordNames[
        FlowRecordFields::FLOWREC_SOURCEVN], tuple.source_vn));
    cmap.insert(std::make_pair(SELECT_SUM_BYTES,
        integerToString(sum_bytes)));
    cmap.insert(std::make_pair(SELECT_FLOW_COUNT, 
        integerToString(num_flows())));
    QEOpServerProxy::ResultRowT rrow(std::make_pair(cmap, metadata));
    expected_res.push_back(std::make_pair(rrow, false));

    SelectQuery* select_query = new SelectQuery(&analytics_query_mock, 
                                                json_select);
    select_query->process_query();
    verify_select_fs_query_result(&expected_res, select_query->result_.get());
}

TEST_F(SelectFSQueryTest, SelectTSFlowTuple) {
    AnalyticsQueryMock analytics_query_mock;
    select_fs_query_default_expect_init(analytics_query_mock);

    int granularity = 25;
    std::stringstream ss;
    ss << "[\"T=" << granularity << "\", " << "\"sourcevn\", \"sourceip\""
       << ", \"sport\"]";
    std::map<std::string, std::string> json_select;
    json_select.insert(std::pair<std::string, std::string>(
        "select_fields", ss.str())); 
    
    EXPECT_CALL(analytics_query_mock, where_query_result())
        .Times(1)
        .WillOnce(Invoke(this, &SelectFSQueryTest::wherequery_result));
    
    // Fill the expected result
    SelectFSQueryTest::BufferT expected_res;
    FlowSeriesData* fs_data = flowseries_data();
    uint64_t gmsec = granularity*1000*1000;
    boost::tuple<uint64_t, flow_tuple> expt[] = {
        make_tuple(start_time(), fs_data[0].tuple()),
        make_tuple(start_time(), fs_data[1].tuple()),
        make_tuple(start_time()+gmsec, fs_data[0].tuple()),
        make_tuple(start_time()+gmsec, fs_data[1].tuple()),
        make_tuple(start_time()+(gmsec*2), fs_data[0].tuple())
    };
    for (int i = 0; i < 5; i++) {
        QEOpServerProxy::OutRowT cmap;
        cmap.insert(std::make_pair(TIMESTAMP_FIELD, 
            integerToString(get<0>(expt[i]))));
        cmap.insert(std::make_pair(g_viz_constants.FlowRecordNames[
            FlowRecordFields::FLOWREC_SOURCEVN], get<1>(expt[i]).source_vn));
        cmap.insert(std::make_pair(g_viz_constants.FlowRecordNames[
            FlowRecordFields::FLOWREC_SOURCEIP], 
            integerToString(get<1>(expt[i]).source_ip)));
        cmap.insert(std::make_pair(g_viz_constants.FlowRecordNames[
            FlowRecordFields::FLOWREC_SPORT], 
            integerToString(get<1>(expt[i]).source_port)));
        boost::shared_ptr<fsMetaData> metadata;
        QEOpServerProxy::ResultRowT rrow(std::make_pair(cmap, metadata));
        expected_res.push_back(std::make_pair(rrow, false));
    }

    SelectQuery* select_query = new SelectQuery(&analytics_query_mock, 
                                                json_select);
    select_query->process_query();
    verify_select_fs_query_result(&expected_res, select_query->result_.get());
}

TEST_F(SelectFSQueryTest, SelectTSStats) {
    AnalyticsQueryMock analytics_query_mock;
    select_fs_query_default_expect_init(analytics_query_mock);

    int granularity = 12;
    std::stringstream ss;
    ss << "[\"T=" << granularity << "\", " << "\"sum(packets)\"]";
    std::map<std::string, std::string> json_select;
    json_select.insert(std::pair<std::string, std::string>(
        "select_fields", ss.str())); 
    
    EXPECT_CALL(analytics_query_mock, where_query_result())
        .Times(1)
        .WillOnce(Invoke(this, &SelectFSQueryTest::wherequery_result));
    
    // Fill the expected result
    SelectFSQueryTest::BufferT expected_res;
    FlowSeriesData* fs_data = flowseries_data();
    uint64_t gmsec = granularity*1000*1000;
    boost::tuple<uint64_t, uint64_t> expt[] = {
        make_tuple(start_time(), 6),
        make_tuple(start_time()+gmsec, 8),
        make_tuple(start_time()+(gmsec*2), 5),
        make_tuple(start_time()+(gmsec*3), 2),
        make_tuple(start_time()+(gmsec*4), 2)
    };
    for (int i = 0; i < 5; i++) {
        QEOpServerProxy::OutRowT cmap;
        cmap.insert(std::make_pair(TIMESTAMP_FIELD, 
                    integerToString(get<0>(expt[i]))));
        cmap.insert(std::make_pair(SELECT_SUM_PACKETS, 
                    integerToString(get<1>(expt[i]))));
        boost::shared_ptr<fsMetaData> metadata;
        QEOpServerProxy::ResultRowT rrow(std::make_pair(cmap, metadata));
        expected_res.push_back(std::make_pair(rrow, false));
    }
    
    SelectQuery* select_query = new SelectQuery(&analytics_query_mock, 
                                                json_select);
    select_query->process_query();
    verify_select_fs_query_result(&expected_res, select_query->result_.get());
}

TEST_F(SelectFSQueryTest, SelectTFlowTupleStats) {
    AnalyticsQueryMock analytics_query_mock;
    select_fs_query_default_expect_init(analytics_query_mock);
   
    std::stringstream ss;
    ss << "[\"T\", \"destvn\", \"destip\", \"bytes\"]";
    std::map<std::string, std::string> json_select;
    json_select.insert(std::pair<std::string, std::string>(
        "select_fields", ss.str())); 
   
    EXPECT_CALL(analytics_query_mock, where_query_result())
        .Times(1)
        .WillOnce(Invoke(this, &SelectFSQueryTest::wherequery_result));
    
    // Fill the expected result
    SelectFSQueryTest::BufferT expected_res;
    FlowSeriesData* fs_data = flowseries_data(); 
    for (int i = 0; i < num_flows(); i++) {
        FlowSeriesData::tstats_t& tstats = fs_data[i].t_stats();
        flow_tuple& tuple = fs_data[i].tuple();
        for (FlowSeriesData::tstats_t::const_iterator it = tstats.begin();
             it != tstats.end(); it++) {
            boost::shared_ptr<fsMetaData> metadata;
            QEOpServerProxy::OutRowT cmap;
            cmap.insert(std::make_pair(TIMESTAMP_FIELD, 
                integerToString(it->first)));
            cmap.insert(std::make_pair(g_viz_constants.FlowRecordNames[
                FlowRecordFields::FLOWREC_DESTVN], tuple.dest_vn));
            cmap.insert(std::make_pair(g_viz_constants.FlowRecordNames[
                FlowRecordFields::FLOWREC_DESTIP], 
                integerToString(tuple.dest_ip)));
            cmap.insert(std::make_pair(SELECT_BYTES,
                integerToString(it->second.bytes)));
            QEOpServerProxy::ResultRowT rrow(std::make_pair(cmap, metadata));
            expected_res.push_back(std::make_pair(rrow, false));
        }
    }
    
    SelectQuery* select_query = new SelectQuery(&analytics_query_mock, 
                                                json_select);
    select_query->process_query();
    verify_select_fs_query_result(&expected_res, select_query->result_.get());
}

TEST_F(SelectFSQueryTest, SelectTSFlowTupleStats) {
    AnalyticsQueryMock analytics_query_mock;
    select_fs_query_default_expect_init(analytics_query_mock);

    int granularity = 45;
    std::stringstream ss;
    ss << "[\"T=" << granularity << "\", " << "\"sourcevn\", "
       << "\"sum(packets)\"]";
    std::map<std::string, std::string> json_select;
    json_select.insert(std::pair<std::string, std::string>(
        "select_fields", ss.str())); 
    
    EXPECT_CALL(analytics_query_mock, where_query_result())
        .Times(1)
        .WillOnce(Invoke(this, &SelectFSQueryTest::wherequery_result));
    
    // Fill the expected result
    SelectFSQueryTest::BufferT expected_res;
    FlowSeriesData* fs_data = flowseries_data();
    boost::tuple<uint64_t, flow_tuple, uint64_t> expt[] = {
        make_tuple(start_time(), fs_data[0].tuple(), 23),
        make_tuple(start_time()+granularity*1000*1000, fs_data[0].tuple(), 3)
    };
    for (int i = 0; i < 2; i++) {
        QEOpServerProxy::OutRowT cmap;
        cmap.insert(std::make_pair(TIMESTAMP_FIELD, 
            integerToString(get<0>(expt[i]))));
        cmap.insert(std::make_pair(g_viz_constants.FlowRecordNames[
            FlowRecordFields::FLOWREC_SOURCEVN], get<1>(expt[i]).source_vn));
        cmap.insert(std::make_pair(SELECT_SUM_PACKETS, 
            integerToString(get<2>(expt[i]))));
        boost::shared_ptr<fsMetaData> metadata;
        QEOpServerProxy::ResultRowT rrow(std::make_pair(cmap, metadata));
        expected_res.push_back(std::make_pair(rrow, false));
    }

    SelectQuery* select_query = new SelectQuery(&analytics_query_mock, 
                                                json_select);
    select_query->process_query();
    verify_select_fs_query_result(&expected_res, select_query->result_.get());
}

int main(int argc, char **argv) {
    LoggingInit();
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
