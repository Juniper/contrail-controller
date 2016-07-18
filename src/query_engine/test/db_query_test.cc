// actual google test classes
#include <boost/bind.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/assign/list_of.hpp>
#include "testing/gunit.h"
#include "base/logging.h"
#include <query.h>
#include <analytics_query_mock.h>
#include "sandesh/sandesh_types.h"
#include "sandesh/sandesh.h"

using ::testing::Return;
using ::testing::AnyNumber;

class DbQueryUnitTest: public ::testing::Test {
public:
    static boost::shared_ptr<DbQueryUnit::Output> resp() {
        boost::shared_ptr<DbQueryUnit::Output> out(new DbQueryUnit::Output());
        query_result_unit_t *qru1 = new query_result_unit_t();
        qru1->timestamp = 2;
        query_result_unit_t *qru2 = new query_result_unit_t();
        qru2->timestamp = 1;
        out->query_result.push_back(*qru1);
        out->query_result.push_back(*qru2);
        return out;
    }

};


template<typename T0, typename T1>
class PipelineMock : public WorkPipeline<T0, T1> {
public:
     PipelineMock(
            WorkStageIf<T0,T1> * s0):
         WorkPipeline<T0,T1>(s0) {
    }

    ~PipelineMock() {
    }
    
    static boost::shared_ptr<DbQueryUnit::Output> resp() {
        boost::shared_ptr<DbQueryUnit::Output> out(new DbQueryUnit::Output());
        query_result_unit_t *qru1 = new query_result_unit_t();
        qru1->timestamp = 2;
        query_result_unit_t *qru2 = new query_result_unit_t();
        qru2->timestamp = 1;
        out->query_result.push_back(*qru1);
        out->query_result.push_back(*qru2);
        return out;
    }

    MOCK_CONST_METHOD0(Result, boost::shared_ptr<DbQueryUnit::Output>());
};

TEST_F(DbQueryUnitTest, WPCompleteCbTest) {
    AnalyticsQueryMock analytics_query_mock;
    //select_fs_query_default_expect_init(analytics_query_mock);

    DbQueryUnit *dbq = new DbQueryUnit(&analytics_query_mock, &analytics_query_mock);
    typedef PipelineMock<DbQueryUnit::Input, DbQueryUnit::Output> QEPipeT;
    int max_tasks = 5;
    std::vector<std::pair<int,int> > tinfo;
    for (uint idx=0; idx<(uint)max_tasks; idx++) {
        tinfo.push_back(make_pair(0, -1));
    }
    std::auto_ptr<QEPipeT> wp(new QEPipeT(
                                  new WorkStage<DbQueryUnit::Input, DbQueryUnit::Output, DbQueryUnit::q_result, DbQueryUnit::Stage0Out>(
            tinfo,
            boost::bind(&DbQueryUnit::QueryExec, dbq, _1, _2, _3, _4),
            boost::bind(&DbQueryUnit::QueryMerge, dbq, _1, _2, _3))));
    boost::shared_ptr<DbQueryUnit::Output> out(new DbQueryUnit::Output());
    query_result_unit_t *qru1 = new query_result_unit_t();
    qru1->timestamp = 2;
    query_result_unit_t *qru2 = new query_result_unit_t();
    qru2->timestamp = 1;
    out->query_result.push_back(*qru1);
    out->query_result.push_back(*qru2);
    EXPECT_CALL(*(wp.get()), Result()).Times(AnyNumber()).WillRepeatedly(Return(out));
    dbq->WPCompleteCb(wp.get(), true);
    // the query_result should be sorted
    EXPECT_EQ(dbq->query_result[0].timestamp, 1);
    EXPECT_EQ(dbq->query_result[1].timestamp, 2);
}

int main(int argc, char **argv) {
    LoggingInit();
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

