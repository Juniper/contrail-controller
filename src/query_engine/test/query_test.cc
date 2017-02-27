/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "query_test.h"

// Message table
void CdbIfMock::initialize_tables()
{
    // MessageTable
    MessageTable = boost::assign::list_of<std::map<std::string, std::string> >
    // Table row #1
    (boost::assign::map_list_of
    // following is straight from tabledump.py output
("UuidKey","6e6c7dcc-800f-4e98-8838-b6e9d9fc21eb") ("counters.instance", "0") ("counters.partitions", "5") ("counters.keys", "28") ("counters.updates", "36")
    )
    // End table row #1
    // Table row #2
    (boost::assign::map_list_of
    // following is straight from tabledump.py output
("UuidKey", "6e6c7dcc-800f-4e98-8838-b6e9d9fc21eb") ("counters.instance", "0") ("counters.partitions", "4") ("counters.keys", "27") ("counters.updates", "35") 
    )
    // End table row #2
    // Table row #3
    (boost::assign::map_list_of
    // following is straight from tabledump.py output
("UuidKey", "49509ec8-f1e8-4e57-9a3c-22249b429697") ("counters.instance", "0") ("counters.partitions", "3") ("counters.keys", "26") ("counters.updates", "34") 
    )
    // End table row #3
    // Table row #4
    (boost::assign::map_list_of
    // following is straight from tabledump.py output
("UuidKey", "378ded22-b727-4bfc-8049-4278da18546d") ("counters.instance", "0") ("counters.partitions", "2") ("counters.keys", "25") ("counters.updates", "33") 
    )
    // End table row #4
    // Table row #5
    (boost::assign::map_list_of
    // following is straight from tabledump.py output
("UuidKey", "96069c43-63c4-460f-95b9-87a50c5ee685") ("counters.instance", "0") ("counters.partitions", "1") ("counters.keys", "24") ("counters.updates", "32") 
    )
    // End table row #5
    // Table row #6
    (boost::assign::map_list_of
    // following is straight from tabledump.py output
("UuidKey", "f2994085-d98c-4047-8876-4f435fd6a7a0") ("counters.instance", "0") ("counters.partitions", "0") ("counters.keys", "23") ("counters.updates", "31") 
    )
    // End table row #6
 ;
    // End MessageTable
}

bool CdbIfMock::Db_Init() {return true;}

bool CdbIfMock::Db_AddSetTablespace(const std::string& tablespace, const std::string& replication_factor)
{ 
    QE_ASSERT(tablespace == g_viz_constants.COLLECTOR_KEYSPACE);
    return true;
}

bool CdbIfMock::Db_GetMultiRow(GenDb::ColListVec *col_list,
        const std::string &cfname,
        const std::vector<GenDb::DbDataValueVec> &v_rowkey)
{
    {
        // Lookup in MessageTable
        for (unsigned int i = 0; i < v_rowkey.size(); i++)
        {
	    GenDb::DbDataValueVec u;
            std::stringstream ss; 
            for (unsigned int k = 0; k < v_rowkey[i].size(); k++) {
		ss << boost::get<boost::uuids::uuid>(v_rowkey[i][k]);
	    }

            for (unsigned int j = 0; j < MessageTable.size(); j++)
            {
                std::map<std::string, std::string>::iterator it;
                {
                    // matching table row
                    std::map<std::string, std::string>::iterator iter;
                    for (iter = MessageTable[j].begin();
                            iter != MessageTable[j].end(); iter++)
                    {
			GenDb::ColList *col = new GenDb::ColList;
			col->rowkey_ = v_rowkey[i];
			col->cfname_ = cfname;

			GenDb::NewColVec colvec;

                        if (iter->first == "counters.instance") {
                            int64_t val;
                            stringToInteger(iter->second, val);
			    GenDb::NewCol *newcol = new GenDb::NewCol(iter->first, (const char *)&val, 0);
			    col->columns_.push_back(newcol);
                        } else {
			    GenDb::NewCol *newcol = new GenDb::NewCol(iter->first, iter->second, 0);
			    col->columns_.push_back(newcol);
                        }
                        col_list->push_back(col);
                    }
                } 
            } // finished iterating over all table rows
        } // finished iterating over all keys 
    } // End Message Table Simulation

    return true;
}


// actual google test classes
#include <boost/bind.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/assign/list_of.hpp>
#include "testing/gunit.h"
#include "base/logging.h"
#include "sandesh/sandesh_types.h"
#include "sandesh/sandesh.h"

using ::testing::Return;
using ::testing::Field;
using ::testing::AnyOf;
using ::testing::AnyNumber;
using ::testing::_;
using ::testing::Eq;
using ::testing::ElementsAre;

class AnalyticsQueryTest: public ::testing::Test {
public:
    AnalyticsQueryTest() :
        dbif_mock_(new CdbIfMock(&evm_)) { }

    ~AnalyticsQueryTest() {
    }

    virtual void SetUp() {
    }

    virtual void TearDown() {
    }

    CdbIfMock *dbif_mock() {
        return dbif_mock_;
    }

    CdbIfMock *dbif_mock_;
private:
    void QueryErrorHandlerFn() {
        assert(0);
    }

    EventManager evm_;
};

TEST_F(AnalyticsQueryTest, ApplyLimitTest) {
    // Create the query first
    std::string qid("TEST-QUERY");
    std::map<std::string, std::string> json_api_data;
    json_api_data.insert(std::pair<std::string, std::string>(
                "table", "\"StatTable.AlarmgenStatus.counters\""
    ));

    json_api_data.insert(std::pair<std::string, std::string>(
    "start_time", "1365791500164229"
    ));
    json_api_data.insert(std::pair<std::string, std::string>(
    "end_time",   "1365997500164232" 
    ));
    json_api_data.insert(std::pair<std::string, std::string>(
    "select_fields", "[\"counters.partitions\"]"
    ));
    json_api_data.insert(std::pair<std::string, std::string>(
    "limit", "1"
    ));
    // 2 results are passed to AnalyticsQuery with limit 1 to make sure
    // that output result count is 1
    TtlMap ttlmap_;
    std::vector<query_result_unit_t> where_info;
    query_result_unit_t query_result;
    boost::uuids::uuid uuid = StringToUuid("6e6c7dcc-800f-4e98-8838-b6e9d9fc21eb");
    query_result.info.push_back("\{\"counters.instancess\":\"*\"}");
    query_result.info.push_back(uuid);
    where_info.push_back(query_result);
    query_result_unit_t query_result2;
    query_result2.info.push_back("\{\"counters.partitionsss\":\"*\"}");
    uuid = StringToUuid("some-random-string");
    query_result2.info.push_back(uuid);
    where_info.push_back(query_result2);
    AnalyticsQuery *q = new AnalyticsQuery(qid, (boost::shared_ptr<GenDb::GenDbIf>)dbif_mock_, json_api_data, -1, &where_info, ttlmap_, 0, 1, NULL);
    EXPECT_EQ(QUERY_SUCCESS, q->process_query()); // query was parsed and successful

    EXPECT_EQ(1, q->final_mresult->size()); // one row as result due to limit of 1
    // Make sure the stat name and attributes are stored in stat_name_attr
    std::string expected_stat_name("AlarmgenStatus:counters");
    EXPECT_EQ(q->stat_name_attr, expected_stat_name);
    delete q;
}

TEST_F(AnalyticsQueryTest, TestQueryTimeAdjustment) {
    // Create the query first
    std::string qid("TestQueryTimeAdjustment");
    std::map<std::string, std::string> json_api_data;
    json_api_data.insert(std::pair<std::string, std::string>(
                "table", "\"StatTable.FieldNames.fields\""
    ));

    uint64_t et = UTCTimestampUsec() - 10*60*1000*1000; /* keep endtime 10min */
    uint64_t st = et-30*1000*1000;
    std::string et_s = integerToString(et);
    std::string st_s = integerToString(st);

    json_api_data.insert(std::pair<std::string, std::string>(
    "start_time", st_s
    ));
    json_api_data.insert(std::pair<std::string, std::string>(
    "end_time",   et_s
    ));
    json_api_data.insert(std::pair<std::string, std::string>(
    "select_fields", "[\"fields.value\"]"
    ));
    // 2 results are passed to AnalyticsQuery with limit 1 to make sure
    // that output result count is 1
    TtlMap ttlmap_ = g_viz_constants.TtlValuesDefault;
    AnalyticsQuery *q = new AnalyticsQuery(qid, (boost::shared_ptr<GenDb::GenDbIf>)dbif_mock_, json_api_data, -1, 0, ttlmap_, 0, 1, NULL);
    uint64_t diff_usec = (1<<(g_viz_constants.RowTimeInBits + g_viz_constants.CacheTimeInAdditionalBits)) - (et-st);
    uint64_t st_exp = st - diff_usec;
    EXPECT_EQ(q->from_time(), st_exp);
    EXPECT_EQ(q->end_time(), et);

    delete q;
}

int main(int argc, char **argv) {
    LoggingInit();
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

