// actual google test classes
#include <boost/bind.hpp>
#include <boost/function.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/assign/list_of.hpp>
#include <boost/assign/ptr_list_of.hpp>
#include <boost/uuid/uuid.hpp>
#include "testing/gunit.h"
#include "base/logging.h"
#include <query.h>
#include <analytics_query_mock.h>
#include "sandesh/sandesh_types.h"
#include "sandesh/sandesh.h"
#include "../../analytics/db_handler.h"
#include "../../analytics/test/cql_if_mock.h"
#include "base/test/task_test_util.h"

using ::testing::_;
using ::testing::Return;
using ::testing::AnyNumber;

TtlMap ttl_map = g_viz_constants.TtlValuesDefault;

struct map_value {
    GenDb::GenDbIf::DbGetRowCb cb;
    const void * ctx;
    void * privdata;
};

class DbQueryUnitTest: public ::testing::Test {
 public:
    bool GetRowAsyncSuccess(const std::string& cfname,
        const GenDb::DbDataValueVec& rowkey,
        const GenDb::ColumnNameRange &crange,
        GenDb::DbConsistency::type dconsistency,
        GenDb::GenDbIf::DbGetRowCb cb);
    bool GetRowAsyncFailure(const std::string& cfname,
        const GenDb::DbDataValueVec& rowkey,
        const GenDb::ColumnNameRange &crange,
        GenDb::DbConsistency::type dconsistency,
        GenDb::GenDbIf::DbGetRowCb cb);
    bool StatTableGetRowAsyncSuccess(const std::string& cfname,
        const GenDb::DbDataValueVec& rowkey,
        const GenDb::ColumnNameRange &crange,
        GenDb::DbConsistency::type dconsistency,
        GenDb::GenDbIf::DbGetRowCb cb);
    bool TestFailureHandling(const std::string& cfname,
             const GenDb::DbDataValueVec& rowkey,
             const GenDb::ColumnNameRange &crange,
             GenDb::DbConsistency::type dconsistency,
             GenDb::GenDbIf::DbGetRowCb cb);
    void subquery_processed(QueryUnit *subquery);
    void cb(void *handle, QEOpServerProxy::QPerfInfo qpi, std::auto_ptr<WhereResultT> where_result);

    std::map<uint64_t, map_value> m;
    std::map<uint64_t, const std::auto_ptr<GenDb::NewColVec> > kv;
};

bool DbQueryUnitTest::TestFailureHandling(const std::string& cfname,
             const GenDb::DbDataValueVec& rowkey,
             const GenDb::ColumnNameRange &crange,
             GenDb::DbConsistency::type dconsistency,
             GenDb::GenDbIf::DbGetRowCb cb) {
    std::auto_ptr<GetRowInput> rip(new GetRowInput());
    rip->rowkey = rowkey;
    std::auto_ptr<GenDb::ColList> collist;
    cb(GenDb::DbOpResult::ERROR, collist);
    return true;
}

bool DbQueryUnitTest::GetRowAsyncSuccess(const std::string& cfname,
    const GenDb::DbDataValueVec& rowkey, const GenDb::ColumnNameRange &crange,
    GenDb::DbConsistency::type dconsistency, GenDb::GenDbIf::DbGetRowCb cb) {
    // Return the following for every row
    boost::uuids::random_generator rgen_;
    boost::uuids::uuid unm(rgen_());
    GenDb::DbDataValueVec *colname(new GenDb::DbDataValueVec());
    colname->reserve(3);
    colname->push_back("a6s9");
    colname->push_back(unm);
    colname->push_back((uint32_t)(21212));
    GenDb::DbDataValueVec *colvalue(new GenDb::DbDataValueVec());
    colvalue->push_back(unm);
    std::auto_ptr<GenDb::ColList> columns(new GenDb::ColList());
    int ttl = 5;
    columns->columns_ = boost::assign::ptr_list_of<GenDb::NewCol>
        (GenDb::NewCol(colname, colvalue, ttl));
    cb(GenDb::DbOpResult::OK, columns);
    return true;
}

bool DbQueryUnitTest::StatTableGetRowAsyncSuccess(const std::string& cfname,
    const GenDb::DbDataValueVec& rowkey, const GenDb::ColumnNameRange &crange,
    GenDb::DbConsistency::type dconsistency, GenDb::GenDbIf::DbGetRowCb cb) {
    // Return the following for every row
    boost::uuids::random_generator rgen_;
    boost::uuids::uuid unm(rgen_());
    GenDb::DbDataValueVec *colname(new GenDb::DbDataValueVec());
    colname->reserve(3);
    colname->push_back("a6s9");
    //colname->push_back(unm);
    colname->push_back((uint32_t)(21212));
    colname->push_back(unm);
    GenDb::DbDataValueVec *colvalue(new GenDb::DbDataValueVec());
    colvalue->push_back("Attrval1");
    std::auto_ptr<GenDb::ColList> columns(new GenDb::ColList());
    int ttl = 5;
    columns->columns_ = boost::assign::ptr_list_of<GenDb::NewCol>
        (GenDb::NewCol(colname, colvalue, ttl));
    cb(GenDb::DbOpResult::OK, columns);
    return true;
}

bool DbQueryUnitTest::GetRowAsyncFailure(const std::string& cfname,
    const GenDb::DbDataValueVec& rowkey, const GenDb::ColumnNameRange &crange,
    GenDb::DbConsistency::type dconsistency, GenDb::GenDbIf::DbGetRowCb cb) {
    std::auto_ptr<GenDb::ColList> columns(new GenDb::ColList());
    cb(GenDb::DbOpResult::ERROR, columns);
    return true;
}

void DbQueryUnitTest::subquery_processed(QueryUnit *subquery) {
    EXPECT_EQ(QUERY_SUCCESS, subquery->query_status);
    EXPECT_EQ(119, subquery->query_result->size());
    return;
}

void DbQueryUnitTest::cb(void *handle, QEOpServerProxy::QPerfInfo qpi, std::auto_ptr<WhereResultT> where_result) {
    EXPECT_EQ(1, where_result->size());
    return;
}

TEST_F(DbQueryUnitTest, ProcessQuery) {
    AnalyticsQueryMock analytics_query_mock;
    AnalyticsQueryMock parent;
    analytics_query_mock.parallel_batch_num = 1;
    analytics_query_mock.query_id = "abcd";
    std::auto_ptr<QueryEngine> qe(new QueryEngine());
    analytics_query_mock.qe_ = qe.get();
    analytics_query_mock.qe_->max_tasks_ = 15;

    DbQueryUnit *dbq = new DbQueryUnit(&analytics_query_mock, &analytics_query_mock);
    EXPECT_CALL(*(CqlIfMock *)(analytics_query_mock.dbif_.get()),
        Db_GetRowAsync(_,_,_,_,_))
            .Times(AnyNumber())
            .WillRepeatedly(Invoke(this,
                &DbQueryUnitTest::GetRowAsyncSuccess));
    EXPECT_CALL(analytics_query_mock, table()).Times(AnyNumber()).WillRepeatedly(Return("table1"));
    EXPECT_CALL(analytics_query_mock, end_time()).Times(AnyNumber()).WillRepeatedly(Return(1473385977637609));
    EXPECT_CALL(analytics_query_mock, from_time()).Times(AnyNumber()).WillRepeatedly(Return(1473384977637609));
    EXPECT_CALL(analytics_query_mock, subquery_processed(_)).Times(1).WillOnce(Invoke(this, &DbQueryUnitTest::subquery_processed));
    EXPECT_CALL(analytics_query_mock, is_stat_table_query()).Times(AnyNumber())
        .WillRepeatedly(Return(false));
    bool status = dbq->process_query();
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(QUERY_IN_PROGRESS, status);
}

TEST_F(DbQueryUnitTest, ProcessQueryFailure) {
    AnalyticsQueryMock analytics_query_mock;
    AnalyticsQueryMock parent;
    analytics_query_mock.parallel_batch_num = 1;
    analytics_query_mock.query_id = "abcd";
    std::auto_ptr<QueryEngine> qe(new QueryEngine());
    analytics_query_mock.qe_ = qe.get();
    analytics_query_mock.qe_->max_tasks_ = 15;

    DbQueryUnit *dbq = new DbQueryUnit(&analytics_query_mock, &analytics_query_mock);
    EXPECT_CALL(*(CqlIfMock *)(analytics_query_mock.dbif_.get()),
        Db_GetRowAsync(_,_,_,_,_))
            .Times(AnyNumber())
            .WillRepeatedly(Invoke(this,
                &DbQueryUnitTest::GetRowAsyncFailure));
    EXPECT_CALL(analytics_query_mock, table()).Times(AnyNumber()).WillRepeatedly(Return("table1"));
    EXPECT_CALL(analytics_query_mock, end_time()).Times(AnyNumber()).WillRepeatedly(Return(1473385977637609));
    EXPECT_CALL(analytics_query_mock, from_time()).Times(AnyNumber()).WillRepeatedly(Return(1473384977637609));
    EXPECT_CALL(analytics_query_mock, subquery_processed(_)).Times(1).WillOnce(Return());
    bool status = dbq->process_query();
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(QUERY_IN_PROGRESS, status);
    TASK_UTIL_EXPECT_EQ(QUERY_FAILURE, dbq->query_status);
}

TEST_F(DbQueryUnitTest, QueryFailureCallback) {
    AnalyticsQueryMock analytics_query_mock;
    AnalyticsQueryMock parent;
    analytics_query_mock.parallel_batch_num = 1;
    analytics_query_mock.query_id = "abcd";
    std::auto_ptr<QueryEngine> qe(new QueryEngine());
    analytics_query_mock.qe_ = qe.get();
    analytics_query_mock.qe_->max_tasks_ = 15;

    DbQueryUnit *dbq = new DbQueryUnit(&analytics_query_mock,
        &analytics_query_mock);
    GenDb::DbDataValueVec rowkey;
    uint32_t t2=12345;
    rowkey.push_back(t2);
    std::vector<GenDb::DbDataValueVec> keys;
    EXPECT_CALL(*(CqlIfMock *)(analytics_query_mock.dbif_.get()),
        Db_GetRowAsync(_,_,_,_,_)).Times(AnyNumber()).WillRepeatedly(
        Invoke(this, &DbQueryUnitTest::TestFailureHandling));
    EXPECT_CALL(analytics_query_mock, table()).Times(AnyNumber()).
        WillRepeatedly(Return("table1"));
    EXPECT_CALL(analytics_query_mock, end_time()).Times(AnyNumber()).
        WillRepeatedly(Return(1473385977637609));
    EXPECT_CALL(analytics_query_mock, from_time()).Times(AnyNumber()).
        WillRepeatedly(Return(1473384977637609));
    EXPECT_CALL(analytics_query_mock, subquery_processed(_)).Times(1).
        WillOnce(Return());
    bool status = dbq->process_query();
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(QUERY_IN_PROGRESS, status);
    TASK_UTIL_EXPECT_EQ(QUERY_FAILURE, dbq->query_status);
}

TEST_F(DbQueryUnitTest, WhereQueryProcessing) {
    AnalyticsQueryMock mq;
    mq.query_id = "abcd";
    std::auto_ptr<QueryEngine> qe(new QueryEngine());
    mq.qe_ = qe.get();
    mq.qe_->max_tasks_ = 15;

    DbQueryUnit *mdbq = new DbQueryUnit(&mq,&mq);
    EXPECT_CALL(mq, table()).Times(AnyNumber()).WillRepeatedly(Return("table1"));
    WhereQuery *wq= new WhereQuery(mdbq);
    wq->main_query = &mq;
    wq->where_result_.reset(new std::vector<query_result_unit_t>);
    wq->where_query_cb_ = boost::bind(&DbQueryUnitTest::cb, this, _1, _2, _3);
    AnalyticsQueryMock *q1 = new AnalyticsQueryMock();
    AnalyticsQueryMock *q2 = new AnalyticsQueryMock();
    DbQueryUnit *dbq1 = new DbQueryUnit(wq, q1);
    DbQueryUnit *dbq2 = new DbQueryUnit(wq, q2);
    EXPECT_CALL(*q1, table()).Times(AnyNumber()).WillRepeatedly(Return("table1"));
    EXPECT_CALL(*q2, table()).Times(AnyNumber()).WillRepeatedly(Return("table1"));
    dbq1->sub_query_id = 0;
    dbq2->sub_query_id = 1;
    wq->query_status = QUERY_SUCCESS;
    dbq1->query_status = QUERY_SUCCESS;
    dbq2->query_status = QUERY_SUCCESS;

    // Populate the subquery1 result
    boost::shared_ptr<std::vector<query_result_unit_t> >where_query_result_;
    where_query_result_ = boost::shared_ptr<std::vector<query_result_unit_t> >
                              (new std::vector<query_result_unit_t>());
    query_result_unit_t res1, res2, res3, res4;
    res1.timestamp = 121212;
    res2.timestamp = 121213;
    where_query_result_->push_back(res1);
    where_query_result_->push_back(res2);
    (dbq1->query_result) = where_query_result_;
    wq->subquery_processed(dbq1);

    // populate the subquery2 result
    //std::vector<query_result_unit_t> where_query_result_2;
    boost::shared_ptr<std::vector<query_result_unit_t> >where_query_result_2;
    where_query_result_2 = boost::shared_ptr<std::vector<query_result_unit_t> >
                              (new std::vector<query_result_unit_t>());
    res3.timestamp = 121213;
    res4.timestamp = 121215;
    where_query_result_2->push_back(res3);
    where_query_result_2->push_back(res4);
    (dbq2->query_result) = where_query_result_2;
    wq->subquery_processed(dbq2);
    delete q1;
    delete q2;
}

/*
 * push uuid and object_id into query_result_unit_t.info
 * check if GetObjectId returns the object id correctly
 */
TEST_F(DbQueryUnitTest, GetObjectId) {
    query_result_unit_t res1;
    boost::uuids::random_generator rgen_;
    boost::uuids::uuid unm(rgen_());
    std::string object_id("id1");
    res1.info.push_back(unm);
    res1.info.push_back(object_id);
    std::string returned_val;
    res1.get_objectid(returned_val);
    EXPECT_EQ(object_id, returned_val);
}

/*
 * check if the stats attribute are updated properly for successful and
 * unsuccessful stats table reads
 */
TEST_F(DbQueryUnitTest, TestStatsUpdate) {
    AnalyticsQueryMock analytics_query_mock;
    AnalyticsQueryMock parent;
    analytics_query_mock.parallel_batch_num = 1;
    analytics_query_mock.query_id = "abcd";
    std::auto_ptr<QueryEngine> qe(new QueryEngine());
    analytics_query_mock.qe_ = qe.get();
    analytics_query_mock.qe_->max_tasks_ = 15;
    analytics_query_mock.stat_name_attr = "Statattr";

    DbQueryUnit *dbq = new DbQueryUnit(&analytics_query_mock, &analytics_query_mock);
    EXPECT_CALL(*(CqlIfMock *)(analytics_query_mock.dbif_.get()),
        Db_GetRowAsync(_,_,_,_,_))
            .Times(AnyNumber())
            .WillRepeatedly(Invoke(this,
                &DbQueryUnitTest::StatTableGetRowAsyncSuccess));
    EXPECT_CALL(analytics_query_mock, table()).Times(AnyNumber()).WillRepeatedly(Return("StatTable1"));
    EXPECT_CALL(analytics_query_mock, end_time()).Times(AnyNumber()).WillRepeatedly(Return(1473385977637609));
    EXPECT_CALL(analytics_query_mock, from_time()).Times(AnyNumber()).WillRepeatedly(Return(1473384977637609));
    EXPECT_CALL(analytics_query_mock, subquery_processed(_)).Times(1).WillOnce(Invoke(this, &DbQueryUnitTest::subquery_processed));
    EXPECT_CALL(analytics_query_mock, is_stat_table_query())
            .Times(AnyNumber())
            .WillRepeatedly(Return(true));
    bool status = dbq->process_query();
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(QUERY_IN_PROGRESS, status);
    std::vector<GenDb::DbTableInfo> vstats_dbti, vstats_dbti_fail;
    // Only StatsTable stats can be tested in UT,
    // stats for other tables are checked in systemtest
    analytics_query_mock.qe_->stable_stats_.GetDiffs(&vstats_dbti);
    // Only one stat attribute is being read
    TASK_UTIL_EXPECT_EQ(vstats_dbti.size(), 1);
    // Each read increments the read count of the stat attribute
    TASK_UTIL_EXPECT_EQ(vstats_dbti[0].table_name, "Statattr" );
    TASK_UTIL_EXPECT_EQ(vstats_dbti[0].reads, 120);
    EXPECT_CALL(*(CqlIfMock *)(analytics_query_mock.dbif_.get()),
        Db_GetRowAsync(_,_,_,_,_))
            .Times(AnyNumber())
            .WillRepeatedly(Invoke(this,
                &DbQueryUnitTest::GetRowAsyncFailure));
    EXPECT_CALL(analytics_query_mock, table()).Times(AnyNumber()).WillRepeatedly(Return("StatTable1"));
    EXPECT_CALL(analytics_query_mock, end_time()).Times(AnyNumber()).WillRepeatedly(Return(1473385977637609));
    EXPECT_CALL(analytics_query_mock, from_time()).Times(AnyNumber()).WillRepeatedly(Return(1473384977637609));
    EXPECT_CALL(analytics_query_mock, subquery_processed(_)).Times(1).WillOnce(Return());
    status = dbq->process_query();
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(QUERY_IN_PROGRESS, status);
    TASK_UTIL_EXPECT_EQ(QUERY_FAILURE, dbq->query_status);
    // stats for other tables are checked in systemtest
    analytics_query_mock.qe_->stable_stats_.GetDiffs(&vstats_dbti_fail);
    // Read failures should be recorded
    TASK_UTIL_EXPECT_GT(vstats_dbti_fail[0].read_fails, 0);
}

int main(int argc, char **argv) {
    LoggingInit();
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

