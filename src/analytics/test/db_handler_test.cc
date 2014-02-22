/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/bind.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/assign/list_of.hpp>
#include "testing/gunit.h"
#include "base/logging.h"
#include "sandesh/sandesh_types.h"
#include "sandesh/sandesh.h"
#include "../viz_types.h"
#include "../viz_constants.h"
#include "../db_handler.h"
#include "cdb_if_mock.h"

#include "../vizd_table_desc.h"

using ::testing::Return;
using ::testing::Field;
using ::testing::AnyOf;
using ::testing::AnyNumber;
using ::testing::_;
using ::testing::Eq;
using ::testing::ElementsAre;
using ::testing::Pointee;
using ::testing::ElementsAreArray;
using ::testing::Matcher;

class DbHandlerTest : public ::testing::Test {
public:
    DbHandlerTest() :
        dbif_mock_(new CdbIfMock()),
        db_handler_(new DbHandler(dbif_mock_)) {
    }

    ~DbHandlerTest() {
        delete db_handler_;
    }

    virtual void SetUp() {
    }

    virtual void TearDown() {
    }

    CdbIfMock *dbif_mock() {
        return dbif_mock_;
    }

    DbHandler *db_handler() {
        return db_handler_;
    }

private:
    void DbErrorHandlerFn() {
        assert(0);
    }

    EventManager evm_;
    CdbIfMock *dbif_mock_;
    DbHandler *db_handler_;
};

TEST_F(DbHandlerTest, MessageTableOnlyInsertTest) {
    SandeshHeader hdr;

    hdr.Source = "127.0.0.1";
    hdr.Module = "VizdTest";
    std::string messagetype("SandeshAsyncTest2");
    std::string xmlmessage = "<SandeshAsyncTest2 type=\"sandesh\"><file type=\"string\" identifier=\"-32768\">src/analytics/test/viz_collector_test.cc</file><line type=\"i32\" identifier=\"-32767\">80</line><f1 type=\"struct\" identifier=\"1\"><SAT2_struct><f1 type=\"string\" identifier=\"1\">sat2string101</f1><f2 type=\"i32\" identifier=\"2\">101</f2></SAT2_struct></f1><f2 type=\"i32\" identifier=\"2\">101</f2></SandeshAsyncTest2>";

    boost::uuids::uuid unm = boost::uuids::random_generator()();
    boost::shared_ptr<VizMsg> vmsgp(new VizMsg(hdr, messagetype, xmlmessage, unm)); 

    GenDb::DbDataValueVec rowkey;
    rowkey.push_back(unm);

    Matcher<GenDb::NewCol> msg_table_expected_vector[] = {
        GenDb::NewCol(g_viz_constants.SOURCE, hdr.get_Source()),
        _,
        GenDb::NewCol(g_viz_constants.MODULE, hdr.get_Module()),
        _,
        _,
        _,
        GenDb::NewCol(g_viz_constants.MESSAGE_TYPE, messagetype),
        _,
        _,
        _,
        GenDb::NewCol(g_viz_constants.DATA, xmlmessage) };

    EXPECT_CALL(*dbif_mock(),
            NewDb_AddColumnProxy(
                Pointee(
                    AllOf(Field(&GenDb::ColList::cfname_, g_viz_constants.COLLECTOR_GLOBAL_TABLE),
                        Field(&GenDb::ColList::rowkey_, rowkey),
                        Field(&GenDb::ColList::columns_,
                            ElementsAreArray(msg_table_expected_vector))))))
        .Times(1)
        .WillOnce(Return(true));

    db_handler()->MessageTableOnlyInsert(vmsgp);
}

TEST_F(DbHandlerTest, MessageIndexTableInsertTest) {
    SandeshHeader hdr;

    hdr.Source = "127.0.0.1";
    boost::uuids::uuid unm = boost::uuids::random_generator()();

    DbDataValueVec colname;
    colname.push_back((uint32_t)(hdr.get_Timestamp() & g_viz_constants.RowTimeInMask));
    DbDataValueVec colvalue;
    colvalue.push_back(unm);
    Matcher<GenDb::NewCol> idx_expected_vector[] = {
        GenDb::NewCol(colname, colvalue)
    };

    GenDb::DbDataValueVec src_idx_rowkey;
    src_idx_rowkey.push_back((uint32_t)(hdr.get_Timestamp() >> g_viz_constants.RowTimeInBits));
    src_idx_rowkey.push_back(hdr.get_Source());
    EXPECT_CALL(*dbif_mock(),
            NewDb_AddColumnProxy(
                Pointee(
                    AllOf(Field(&GenDb::ColList::cfname_, g_viz_constants.MESSAGE_TABLE_SOURCE),
                        Field(&GenDb::ColList::rowkey_, src_idx_rowkey),
                        Field(&GenDb::ColList::columns_,
                            ElementsAreArray(idx_expected_vector))))))
        .Times(1)
        .WillOnce(Return(true));

    db_handler()->MessageIndexTableInsert(g_viz_constants.MESSAGE_TABLE_SOURCE, hdr, "", unm);
}

TEST_F(DbHandlerTest, MessageTableInsertTest) {
    SandeshHeader hdr;

    hdr.Source = "127.0.0.1";
    hdr.Module = "VizdTest";
    std::string messagetype("SandeshAsyncTest2");
    std::string xmlmessage = "<SandeshAsyncTest2 type=\"sandesh\"><file type=\"string\" identifier=\"-32768\">src/analytics/test/viz_collector_test.cc</file><line type=\"i32\" identifier=\"-32767\">80</line><f1 type=\"struct\" identifier=\"1\"><SAT2_struct><f1 type=\"string\" identifier=\"1\">sat2string101</f1><f2 type=\"i32\" identifier=\"2\">101</f2></SAT2_struct></f1><f2 type=\"i32\" identifier=\"2\">101</f2></SandeshAsyncTest2>";

    boost::uuids::uuid unm = boost::uuids::random_generator()();
    boost::shared_ptr<VizMsg> vmsgp(new VizMsg(hdr, messagetype, xmlmessage, unm)); 

    GenDb::DbDataValueVec rowkey;
    rowkey.push_back(unm);

    Matcher<GenDb::NewCol> msg_table_expected_vector[] = {
        GenDb::NewCol(g_viz_constants.SOURCE, hdr.get_Source()),
        _,
        GenDb::NewCol(g_viz_constants.MODULE, hdr.get_Module()),
        _,
        _,
        _,
        GenDb::NewCol(g_viz_constants.MESSAGE_TYPE, messagetype),
        _,
        _,
        _,
        GenDb::NewCol(g_viz_constants.DATA, xmlmessage) };

    EXPECT_CALL(*dbif_mock(),
            NewDb_AddColumnProxy(
                Pointee(
                    AllOf(Field(&GenDb::ColList::cfname_, g_viz_constants.COLLECTOR_GLOBAL_TABLE),
                        Field(&GenDb::ColList::rowkey_, rowkey),
                        Field(&GenDb::ColList::columns_,
                            ElementsAreArray(msg_table_expected_vector))))))
        .Times(1)
        .WillOnce(Return(true));

    DbDataValueVec colname;
    colname.push_back((uint32_t)(hdr.get_Timestamp() & g_viz_constants.RowTimeInMask));
    DbDataValueVec colvalue;
    colvalue.push_back(unm);
    Matcher<GenDb::NewCol> idx_expected_vector[] = {
        GenDb::NewCol(colname, colvalue)
    };

    GenDb::DbDataValueVec src_idx_rowkey;
    src_idx_rowkey.push_back((uint32_t)(hdr.get_Timestamp() >> g_viz_constants.RowTimeInBits));
    src_idx_rowkey.push_back(hdr.get_Source());
    EXPECT_CALL(*dbif_mock(),
            NewDb_AddColumnProxy(
                Pointee(
                    AllOf(Field(&GenDb::ColList::cfname_, g_viz_constants.MESSAGE_TABLE_SOURCE),
                        Field(&GenDb::ColList::rowkey_, src_idx_rowkey),
                        Field(&GenDb::ColList::columns_,
                            ElementsAreArray(idx_expected_vector))))))
        .Times(1)
        .WillOnce(Return(true));

    GenDb::DbDataValueVec mod_idx_rowkey;
    mod_idx_rowkey.push_back((uint32_t)(hdr.get_Timestamp() >> g_viz_constants.RowTimeInBits));
    mod_idx_rowkey.push_back(hdr.get_Module());
    EXPECT_CALL(*dbif_mock(),
            NewDb_AddColumnProxy(
                Pointee(
                    AllOf(Field(&GenDb::ColList::cfname_, g_viz_constants.MESSAGE_TABLE_MODULE_ID),
                        Field(&GenDb::ColList::rowkey_, mod_idx_rowkey),
                        Field(&GenDb::ColList::columns_,
                            ElementsAreArray(idx_expected_vector))))))
        .Times(1)
        .WillOnce(Return(true));

    GenDb::DbDataValueVec cat_idx_rowkey;
    cat_idx_rowkey.push_back((uint32_t)(hdr.get_Timestamp() >> g_viz_constants.RowTimeInBits));
    cat_idx_rowkey.push_back(hdr.get_Category());
    EXPECT_CALL(*dbif_mock(),
            NewDb_AddColumnProxy(
                Pointee(
                    AllOf(Field(&GenDb::ColList::cfname_, g_viz_constants.MESSAGE_TABLE_CATEGORY),
                        Field(&GenDb::ColList::rowkey_, cat_idx_rowkey),
                        Field(&GenDb::ColList::columns_,
                            ElementsAreArray(idx_expected_vector))))))
        .Times(1)
        .WillOnce(Return(true));

    GenDb::DbDataValueVec msgtype_idx_rowkey;
    msgtype_idx_rowkey.push_back((uint32_t)(hdr.get_Timestamp() >> g_viz_constants.RowTimeInBits));
    msgtype_idx_rowkey.push_back(messagetype);
    EXPECT_CALL(*dbif_mock(),
            NewDb_AddColumnProxy(
                Pointee(
                    AllOf(Field(&GenDb::ColList::cfname_, g_viz_constants.MESSAGE_TABLE_MESSAGE_TYPE),
                        Field(&GenDb::ColList::rowkey_, msgtype_idx_rowkey),
                        Field(&GenDb::ColList::columns_,
                            ElementsAreArray(idx_expected_vector))))))
        .Times(1)
        .WillOnce(Return(true));

    GenDb::DbDataValueVec ts_idx_rowkey;
    ts_idx_rowkey.push_back((uint32_t)(hdr.get_Timestamp() >> g_viz_constants.RowTimeInBits));
    EXPECT_CALL(*dbif_mock(),
            NewDb_AddColumnProxy(
                Pointee(
                    AllOf(Field(&GenDb::ColList::cfname_, g_viz_constants.MESSAGE_TABLE_TIMESTAMP),
                        Field(&GenDb::ColList::rowkey_, ts_idx_rowkey),
                        Field(&GenDb::ColList::columns_,
                            ElementsAreArray(idx_expected_vector))))))
        .Times(1)
        .WillOnce(Return(true));

    EXPECT_CALL(*dbif_mock(),
            NewDb_AddColumnProxy(
                Pointee(
                    AllOf(Field(&GenDb::ColList::cfname_, g_viz_constants.STATS_TABLE_BY_STR_STR_TAG),
                        _,
                        _))))
        .Times(3)
        .WillRepeatedly(Return(true));

    db_handler()->MessageTableInsert(vmsgp);
}

TEST_F(DbHandlerTest, ObjectTableInsertTest) {
    SandeshHeader hdr;
    hdr.Module = "VizdTest";
    hdr.Source = "127.0.0.1";
    std::string messagetype("ObjectTableInsertTest");
    std::string xmlmessage = "<ObjectTableInsertTest type=\"sandesh\"><file type=\"string\" identifier=\"-32768\">src/analytics/test/viz_collector_test.cc</file><line type=\"i32\" identifier=\"-32767\">80</line><f1 type=\"struct\" identifier=\"1\"><SAT2_struct><f1 type=\"string\" identifier=\"1\">sat2string101</f1><f2 type=\"i32\" identifier=\"2\">101</f2></SAT2_struct></f1><f2 type=\"i32\" identifier=\"2\">101</f2></ObjectTableInsertTest>";
    boost::uuids::uuid unm = boost::uuids::random_generator()();
    boost::shared_ptr<VizMsg> vmsgp(new VizMsg(hdr, messagetype, xmlmessage, unm)); 
    RuleMsg rmsg(vmsgp);

      {
        DbDataValueVec colname;
        colname.push_back((uint32_t)(hdr.get_Timestamp() & g_viz_constants.RowTimeInMask));
        DbDataValueVec colvalue;
        colvalue.push_back(unm);
        Matcher<GenDb::NewCol> expected_vector[] = {
            GenDb::NewCol(colname, colvalue)
        };

        GenDb::DbDataValueVec rowkey;
        rowkey.push_back((uint32_t)(hdr.get_Timestamp() >> g_viz_constants.RowTimeInBits));
        rowkey.push_back("ObjectTableInsertTestRowkey");
        EXPECT_CALL(*dbif_mock(),
                NewDb_AddColumnProxy(
                    Pointee(
                        AllOf(Field(&GenDb::ColList::cfname_, "ObjectTableInsertTest"),
                            Field(&GenDb::ColList::rowkey_, rowkey),
                            Field(&GenDb::ColList::columns_,
                                ElementsAreArray(expected_vector))))))
            .Times(1)
            .WillOnce(Return(true));
      }

      {
        DbDataValueVec colname;
        colname.push_back((uint32_t)(hdr.get_Timestamp() & g_viz_constants.RowTimeInMask));
        DbDataValueVec colvalue;
        colvalue.push_back("ObjectTableInsertTestRowkey");
        Matcher<GenDb::NewCol> expected_vector[] = {
            GenDb::NewCol(colname, colvalue)
        };

        GenDb::DbDataValueVec rowkey;
        rowkey.push_back((uint32_t)(hdr.get_Timestamp() >> g_viz_constants.RowTimeInBits));
        rowkey.push_back("ObjectTableInsertTest");
        EXPECT_CALL(*dbif_mock(),
                NewDb_AddColumnProxy(
                    Pointee(
                        AllOf(Field(&GenDb::ColList::cfname_, g_viz_constants.OBJECT_VALUE_TABLE),
                            Field(&GenDb::ColList::rowkey_, rowkey),
                            Field(&GenDb::ColList::columns_,
                                ElementsAreArray(expected_vector))))))
            .Times(1)
            .WillOnce(Return(true));
      }

      {
        boost::uuids::string_generator gen;
        boost::uuids::uuid unm_allf = gen(std::string("ffffffffffffffffffffffffffffffff"));
        DbDataValueVec colname;
        colname.push_back("ObjectTableInsertTest:Objecttype");
        colname.push_back("");
        colname.push_back((uint32_t)0);
        colname.push_back(unm_allf);
        DbDataValueVec colvalue;
        colvalue.push_back("{\"fields.value\":\"ObjectTableInsertTestRowkey\",\"name\":\"ObjectTableInsertTest:Objecttype\"}");
        Matcher<GenDb::NewCol> expected_vector[] = {
            GenDb::NewCol(colname, colvalue)
        };

        GenDb::DbDataValueVec rowkey;
        rowkey.push_back((uint32_t)(hdr.get_Timestamp() >> g_viz_constants.RowTimeInBits));
        rowkey.push_back("FieldNames");
        rowkey.push_back("fields");
        rowkey.push_back("name");
        EXPECT_CALL(*dbif_mock(),
                NewDb_AddColumnProxy(
                    Pointee(
                        AllOf(Field(&GenDb::ColList::cfname_, g_viz_constants.STATS_TABLE_BY_STR_STR_TAG),
                            Field(&GenDb::ColList::rowkey_, rowkey),
                            Field(&GenDb::ColList::columns_,
                                ElementsAreArray(expected_vector))))))
            .Times(1)
            .WillOnce(Return(true));
      }

      {
        boost::uuids::string_generator gen;
        boost::uuids::uuid unm_allf = gen(std::string("ffffffffffffffffffffffffffffffff"));
        DbDataValueVec colname;
        colname.push_back("ObjectTableInsertTestRowkey");
        colname.push_back("");
        colname.push_back((uint32_t)0);
        colname.push_back(unm_allf);
        DbDataValueVec colvalue;
        colvalue.push_back("{\"fields.value\":\"ObjectTableInsertTestRowkey\",\"name\":\"ObjectTableInsertTest:Objecttype\"}");
        Matcher<GenDb::NewCol> expected_vector[] = {
            GenDb::NewCol(colname, colvalue)
        };

        GenDb::DbDataValueVec rowkey;
        rowkey.push_back((uint32_t)(hdr.get_Timestamp() >> g_viz_constants.RowTimeInBits));
        rowkey.push_back("FieldNames");
        rowkey.push_back("fields");
        rowkey.push_back("fields.value");
        EXPECT_CALL(*dbif_mock(),
                NewDb_AddColumnProxy(
                    Pointee(
                        AllOf(Field(&GenDb::ColList::cfname_, g_viz_constants.STATS_TABLE_BY_STR_STR_TAG),
                            Field(&GenDb::ColList::rowkey_, rowkey),
                            Field(&GenDb::ColList::columns_,
                                ElementsAreArray(expected_vector))))))
            .Times(1)
            .WillOnce(Return(true));
      }

    db_handler()->ObjectTableInsert("ObjectTableInsertTest",
            "ObjectTableInsertTestRowkey", rmsg, unm);
}

TEST_F(DbHandlerTest, FlowTableInsertTest) {
    init_vizd_tables();

    SandeshHeader hdr;
    hdr.Module = "VizdTest";
    hdr.Source = "127.0.0.1";
    std::string messagetype("");
    std::string xmlmessage = "<FlowDataIpv4Object type=\"sandesh\"><flowdata type=\"struct\" identifier=\"1\"><FlowDataIpv4><flowuuid type=\"string\" identifier=\"1\">555788e0-513c-4351-8711-3fc481cf2eb4</flowuuid><direction_ing type=\"byte\" identifier=\"2\">0</direction_ing><sourcevn type=\"string\" identifier=\"3\">default-domain:demo:vn1</sourcevn><sourceip type=\"i32\" identifier=\"4\">-1062731011</sourceip><destvn type=\"string\" identifier=\"5\">default-domain:demo:vn0</destvn><destip type=\"i32\" identifier=\"6\">-1062731267</destip><protocol type=\"byte\" identifier=\"7\">6</protocol><sport type=\"i16\" identifier=\"8\">5201</sport><dport type=\"i16\" identifier=\"9\">-24590</dport><vm type=\"string\" identifier=\"12\">04430130-664a-4b89-9287-39d71f351207</vm><reverse_uuid type=\"string\" identifier=\"16\">58745ee7-d616-4e59-b8f7-96f896487c9f</reverse_uuid><bytes type=\"i64\" identifier=\"23\">0</bytes><packets type=\"i64\" identifier=\"24\">0</packets><diff_bytes type=\"i64\" identifier=\"26\">0</diff_bytes><diff_packets type=\"i64\" identifier=\"27\">0</diff_packets></FlowDataIpv4></flowdata></FlowDataIpv4Object>";

    boost::uuids::uuid unm = boost::uuids::random_generator()();
    boost::shared_ptr<VizMsg> vmsgp(new VizMsg(hdr, messagetype, xmlmessage, unm)); 
    RuleMsg rmsg(vmsgp);

    std::string flowu_str = "555788e0-513c-4351-8711-3fc481cf2eb4";
    boost::uuids::uuid flowu = boost::uuids::string_generator()(flowu_str);

      {
        Matcher<GenDb::NewCol> expected_vector[] = {
        };

        GenDb::DbDataValueVec rowkey;
        rowkey.push_back(flowu);

        EXPECT_CALL(*dbif_mock(),
                NewDb_AddColumnProxy(
                    Pointee(
                        AllOf(Field(&GenDb::ColList::cfname_, g_viz_constants.FLOW_TABLE),
                            Field(&GenDb::ColList::rowkey_, rowkey)))))
            .Times(1)
            .WillOnce(Return(true));
      }

    GenDb::DbDataValueVec colvalue;
    colvalue.push_back((uint64_t)0); //bytes
    colvalue.push_back((uint64_t)0); //pkts
    colvalue.push_back((uint8_t)0); //dir
    colvalue.push_back(flowu); //flowuuid
    colvalue.push_back(rmsg.hdr.get_Source()); //vrouter
    colvalue.push_back("default-domain:demo:vn1"); //svn
    colvalue.push_back("default-domain:demo:vn0"); //dvn
    colvalue.push_back((uint32_t)-1062731011); //sip
    colvalue.push_back((uint32_t)-1062731267); //dip
    colvalue.push_back((uint8_t)6); //prot
    colvalue.push_back((uint16_t)5201); //sport
    colvalue.push_back((uint16_t)-24590); //dport
    colvalue.push_back(""); //json

      {
        GenDb::DbDataValueVec colname;
        colname.push_back("default-domain:demo:vn1");
        colname.push_back((uint32_t)-1062731011);
        colname.push_back((uint32_t)(hdr.get_Timestamp() & g_viz_constants.RowTimeInMask));
        colname.push_back(flowu);
        Matcher<GenDb::NewCol> expected_vector[] = {
            GenDb::NewCol(colname, colvalue)
        };

        GenDb::DbDataValueVec rowkey;
        rowkey.push_back((uint32_t)(hdr.get_Timestamp() >> g_viz_constants.RowTimeInBits));
        uint8_t partition_no = 0;
        rowkey.push_back(partition_no);
        rowkey.push_back((uint8_t)0); //direction

        EXPECT_CALL(*dbif_mock(),
                NewDb_AddColumnProxy(
                    Pointee(
                        AllOf(Field(&GenDb::ColList::cfname_, g_viz_constants.FLOW_TABLE_SVN_SIP),
                            Field(&GenDb::ColList::rowkey_, rowkey),
                            Field(&GenDb::ColList::columns_,
                                ElementsAreArray(expected_vector))))))
            .Times(1)
            .WillOnce(Return(true));
      }
      {
        GenDb::DbDataValueVec colname;
        colname.push_back("default-domain:demo:vn0");
        colname.push_back((uint32_t)-1062731267);
        colname.push_back((uint32_t)(hdr.get_Timestamp() & g_viz_constants.RowTimeInMask));
        colname.push_back(flowu);
        Matcher<GenDb::NewCol> expected_vector[] = {
            GenDb::NewCol(colname, colvalue)
        };

        GenDb::DbDataValueVec rowkey;
        rowkey.push_back((uint32_t)(hdr.get_Timestamp() >> g_viz_constants.RowTimeInBits));
        uint8_t partition_no = 0;
        rowkey.push_back(partition_no);
        rowkey.push_back((uint8_t)0); //direction

        EXPECT_CALL(*dbif_mock(),
                NewDb_AddColumnProxy(
                    Pointee(
                        AllOf(Field(&GenDb::ColList::cfname_, g_viz_constants.FLOW_TABLE_DVN_DIP),
                            Field(&GenDb::ColList::rowkey_, rowkey),
                            Field(&GenDb::ColList::columns_,
                                ElementsAreArray(expected_vector))))))
            .Times(1)
            .WillOnce(Return(true));
      }
      {
        GenDb::DbDataValueVec colname;
        colname.push_back((uint8_t)6);
        colname.push_back((uint16_t)5201);
        colname.push_back((uint32_t)(hdr.get_Timestamp() & g_viz_constants.RowTimeInMask));
        colname.push_back(flowu);
        Matcher<GenDb::NewCol> expected_vector[] = {
            GenDb::NewCol(colname, colvalue)
        };

        GenDb::DbDataValueVec rowkey;
        rowkey.push_back((uint32_t)(hdr.get_Timestamp() >> g_viz_constants.RowTimeInBits));
        uint8_t partition_no = 0;
        rowkey.push_back(partition_no);
        rowkey.push_back((uint8_t)0); //direction

        EXPECT_CALL(*dbif_mock(),
                NewDb_AddColumnProxy(
                    Pointee(
                        AllOf(Field(&GenDb::ColList::cfname_, g_viz_constants.FLOW_TABLE_PROT_SP),
                            Field(&GenDb::ColList::rowkey_, rowkey),
                            Field(&GenDb::ColList::columns_,
                                ElementsAreArray(expected_vector))))))
            .Times(1)
            .WillOnce(Return(true));
      }

      {
        GenDb::DbDataValueVec colname;
        colname.push_back((uint8_t)6);
        colname.push_back((uint16_t)-24590);
        colname.push_back((uint32_t)(hdr.get_Timestamp() & g_viz_constants.RowTimeInMask));
        colname.push_back(flowu);
        Matcher<GenDb::NewCol> expected_vector[] = {
            GenDb::NewCol(colname, colvalue)
        };

        GenDb::DbDataValueVec rowkey;
        rowkey.push_back((uint32_t)(hdr.get_Timestamp() >> g_viz_constants.RowTimeInBits));
        uint8_t partition_no = 0;
        rowkey.push_back(partition_no);
        rowkey.push_back((uint8_t)0); //direction

        EXPECT_CALL(*dbif_mock(),
                NewDb_AddColumnProxy(
                    Pointee(
                        AllOf(Field(&GenDb::ColList::cfname_, g_viz_constants.FLOW_TABLE_PROT_DP),
                            Field(&GenDb::ColList::rowkey_, rowkey),
                            Field(&GenDb::ColList::columns_,
                                ElementsAreArray(expected_vector))))))
            .Times(1)
            .WillOnce(Return(true));
      }

      {
        GenDb::DbDataValueVec colname;
        colname.push_back(rmsg.hdr.get_Source()); //vrouter
        colname.push_back((uint32_t)(hdr.get_Timestamp() & g_viz_constants.RowTimeInMask));
        colname.push_back(flowu);
        Matcher<GenDb::NewCol> expected_vector[] = {
            GenDb::NewCol(colname, colvalue)
        };

        GenDb::DbDataValueVec rowkey;
        rowkey.push_back((uint32_t)(hdr.get_Timestamp() >> g_viz_constants.RowTimeInBits));
        uint8_t partition_no = 0;
        rowkey.push_back(partition_no);
        rowkey.push_back((uint8_t)0); //direction

        EXPECT_CALL(*dbif_mock(),
                NewDb_AddColumnProxy(
                    Pointee(
                        AllOf(Field(&GenDb::ColList::cfname_, g_viz_constants.FLOW_TABLE_VROUTER),
                            Field(&GenDb::ColList::rowkey_, rowkey),
                            Field(&GenDb::ColList::columns_,
                                ElementsAreArray(expected_vector))))))
            .Times(1)
            .WillOnce(Return(true));
      }

    db_handler()->FlowTableInsert(rmsg);
}

int main(int argc, char **argv) {
    LoggingInit();
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

