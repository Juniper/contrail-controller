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
#include "cdb_if.h"
#include "cdb_if_mock.h"

#include "../vizd_table_desc.h"

using ::testing::Return;
using ::testing::Field;
using ::testing::AnyOf;
using ::testing::AnyNumber;
using ::testing::_;
using ::testing::Eq;
using ::testing::ElementsAre;

class DbHandlerTest : public ::testing::Test {
public:
    DbHandlerTest() :
        dbif_mock_(new CdbIfMock(evm_.io_service(), boost::bind(&DbHandlerTest::DbErrorHandlerFn, this))),
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

TEST_F(DbHandlerTest, MessageTableInsertTest) {
    SandeshHeader hdr;
    hdr.Module = "VizdTest";
    hdr.Source = "127.0.0.1";
    std::string messagetype("SandeshAsyncTest2");
    std::string xmlmessage = "<SandeshAsyncTest2 type=\"sandesh\"><file type=\"string\" identifier=\"-32768\">src/analytics/test/viz_collector_test.cc</file><line type=\"i32\" identifier=\"-32767\">80</line><f1 type=\"struct\" identifier=\"1\"><SAT2_struct><f1 type=\"string\" identifier=\"1\">sat2string101</f1><f2 type=\"i32\" identifier=\"2\">101</f2></SAT2_struct></f1><f2 type=\"i32\" identifier=\"2\">101</f2></SandeshAsyncTest2>";
    boost::uuids::uuid unm = boost::uuids::random_generator()();
    boost::shared_ptr<VizMsg> vmsgp(new VizMsg(hdr, messagetype, xmlmessage, unm)); 

    GenDb::ColElement rowkey;
    std::string unm_s(unm.size(), 0);
    std::copy(unm.begin(), unm.end(), unm_s.begin());
    rowkey.elem_value = unm_s;

    EXPECT_CALL(*dbif_mock(),
            Db_AddColumn(AllOf(Field(&GenDb::Column::cfname_, g_viz_constants.COLLECTOR_GLOBAL_TABLE),
                Field(&GenDb::Column::rowkey_, rowkey),
                Field(&GenDb::Column::columns_,
                    ElementsAre(
                        AllOf(Field(&GenDb::ColElement::elem_name, g_viz_constants.SOURCE),
                            Field(&GenDb::ColElement::elem_value, "127.0.0.1")),
                        _,
                        AllOf(Field(&GenDb::ColElement::elem_name, g_viz_constants.MODULE),
                            Field(&GenDb::ColElement::elem_value, "VizdTest")),
                        _,
                        AllOf(Field(&GenDb::ColElement::elem_name, g_viz_constants.MESSAGE_TYPE),
                            Field(&GenDb::ColElement::elem_value, "SandeshAsyncTest2")),
                        _, _, _, _)))))
        .Times(1)
        .WillOnce(Return(true));
    EXPECT_CALL(*dbif_mock(),
            Db_AddColumn(Field(&GenDb::Column::cfname_,
                    g_viz_constants.MESSAGE_TABLE_SOURCE)))
        .Times(1)
        .WillOnce(Return(true));
    EXPECT_CALL(*dbif_mock(),
            Db_AddColumn(Field(&GenDb::Column::cfname_,
                    g_viz_constants.MESSAGE_TABLE_MODULE_ID)))
        .Times(1)
        .WillOnce(Return(true));
    EXPECT_CALL(*dbif_mock(),
            Db_AddColumn(Field(&GenDb::Column::cfname_,
                    g_viz_constants.MESSAGE_TABLE_MESSAGE_TYPE)))
        .Times(1)
        .WillOnce(Return(true));

    db_handler()->MessageTableInsert(vmsgp);
}

TEST_F(DbHandlerTest, ObjectTraceTableInsertTest) {
    GenDb::Cf cf;
    GenDb::CfElement cf_elem;

    cf.cfname_ = "ObjectTraceTableInsertTest";
    cf.comparator_type = "IntegerType";
    cf.default_validation_class = "LexicalUUIDType";

    cf_elem.elem_name =  "dummy1";
    cf_elem.elem_type = "IntegerType";
    cf.cfkey_.push_back(cf_elem);
    cf_elem.elem_name =  "dummy2";
    cf_elem.elem_type = "AsciiType";
    cf.cfkey_.push_back(cf_elem);

    EXPECT_CALL(*dbif_mock(),
            Db_AddColumnfamily(cf))
        .Times(1)
        .WillOnce(Return(true));

    const std::vector<GenDb::Cf> obj_table =
    boost::assign::list_of<GenDb::Cf>
        (GenDb::Cf("ObjectTraceTableInsertTest",
                   "IntegerType",
                   "LexicalUUIDType",
                   boost::assign::list_of
                   (GenDb::CfElement("dummy1",
                                     "IntegerType"))
                   (GenDb::CfElement("dummy2",
                                     "AsciiType"))));

    for (std::vector<GenDb::Cf>::const_iterator it = obj_table.begin();
            it != obj_table.end(); it++) {

        if (db_handler()->Find_Dbinfo_cf(g_viz_constants.COLLECTOR_KEYSPACE,
                    it->cfname_)) {
            continue;
        }
        if (!dbif_mock()->Db_AddColumnfamily(*it)) {
            LOG(ERROR, __func__ << ": Addition of CF: " <<
                it->cfname_ <<
                " FAILED");
            assert(0);
        }

        db_handler()->Add_Dbinfo_cf(g_viz_constants.COLLECTOR_KEYSPACE, it->cfname_);
    }

    SandeshHeader hdr;
    hdr.Module = "VizdTest";
    hdr.Source = "127.0.0.1";
    std::string messagetype("ObjectTraceTableInsertTest");
    std::string xmlmessage = "<ObjectTraceTableInsertTest type=\"sandesh\"><file type=\"string\" identifier=\"-32768\">src/analytics/test/viz_collector_test.cc</file><line type=\"i32\" identifier=\"-32767\">80</line><f1 type=\"struct\" identifier=\"1\"><SAT2_struct><f1 type=\"string\" identifier=\"1\">sat2string101</f1><f2 type=\"i32\" identifier=\"2\">101</f2></SAT2_struct></f1><f2 type=\"i32\" identifier=\"2\">101</f2></ObjectTraceTableInsertTest>";
    boost::uuids::uuid unm = boost::uuids::random_generator()();
    boost::shared_ptr<VizMsg> vmsgp(new VizMsg(hdr, messagetype, xmlmessage, unm)); 
    RuleMsg rmsg(vmsgp);

    uint64_t temp_u64;
    uint32_t temp_u32;
    GenDb::ColElement rowkey;

    temp_u64 = rmsg.hdr.get_Timestamp();
    temp_u32 = temp_u64/DbHandler::RowTimeInUSec;
    rowkey.elem_value.append(dbif_mock()->Db_encode_Integer(temp_u32));
    rowkey.elem_value.append(dbif_mock()->Db_encode_string("ObjectTraceTableInsertTestRowkey"));

    EXPECT_CALL(*dbif_mock(),
            Db_AddColumn(AllOf(Field(&GenDb::Column::cfname_, "ObjectTraceTableInsertTest"), 
                    Field(&GenDb::Column::rowkey_, rowkey))))
        .Times(1)
        .WillOnce(Return(true));

    db_handler()->ObjectTraceTableInsert("ObjectTraceTableInsertTest",
            "ObjectTraceTableInsertTestRowkey", rmsg, unm);
}

TEST_F(DbHandlerTest, FlowTableInsertTest) {
    SandeshHeader hdr;
    hdr.Module = "VizdTest";
    hdr.Source = "127.0.0.1";
    std::string messagetype("");
    std::string xmlmessage = "<FlowDataIpv4Object type=\"sandesh\"><flowdata type=\"struct\" identifier=\"1\"><FlowDataIpv4><flowuuid type=\"string\" identifier=\"1\">d6ab8614-7745-4211-b6e3-a33b3dfcc270</flowuuid><direction_ing type=\"byte\" identifier=\"2\">1</direction_ing><sourcevn type=\"string\" identifier=\"3\">default-domain:admin:vn0</sourcevn><sourceip type=\"i32\" identifier=\"4\">167837706</sourceip><destvn type=\"string\" identifier=\"5\">default-domain:admin:vn0</destvn><destip type=\"i32\" identifier=\"6\">167837706</destip><protocol type=\"byte\" identifier=\"7\">17</protocol><sport type=\"i16\" identifier=\"8\">-32768</sport><dport type=\"i16\" identifier=\"9\">80</dport><setup_time type=\"i64\" identifier=\"17\">1357843963698076</setup_time><bytes type=\"i64\" identifier=\"23\">10000</bytes><packets type=\"i64\" identifier=\"24\">100</packets></FlowDataIpv4></flowdata><file type=\"string\" identifier=\"-32768\">src/analytics/test/viz_flow_test.cc</file><line type=\"i32\" identifier=\"-32767\">214</line></FlowDataIpv4Object>";
    boost::uuids::uuid unm = boost::uuids::random_generator()();
    boost::shared_ptr<VizMsg> vmsgp(new VizMsg(hdr, messagetype, xmlmessage, unm)); 
    RuleMsg rmsg(vmsgp);

    EXPECT_CALL(*dbif_mock(),
            Db_AddColumnfamily(Field(&GenDb::Cf::cfname_,
                    g_viz_constants.FLOW_TABLE)))
        .Times(1)
        .WillOnce(Return(true));

    EXPECT_CALL(*dbif_mock(),
            Db_AddColumnfamily(Field(&GenDb::Cf::cfname_,
                    g_viz_constants.FLOW_TABLE_VN2VN)))
        .Times(1)
        .WillOnce(Return(true));

    EXPECT_CALL(*dbif_mock(),
            Db_AddColumnfamily(Field(&GenDb::Cf::cfname_,
                    g_viz_constants.FLOW_TABLE_SVN_SIP)))
        .Times(1)
        .WillOnce(Return(true));

    EXPECT_CALL(*dbif_mock(),
            Db_AddColumnfamily(Field(&GenDb::Cf::cfname_,
                    g_viz_constants.FLOW_TABLE_DVN_DIP)))
        .Times(1)
        .WillOnce(Return(true));

    EXPECT_CALL(*dbif_mock(),
            Db_AddColumnfamily(Field(&GenDb::Cf::cfname_,
                    g_viz_constants.FLOW_TABLE_PROT_SP)))
        .Times(1)
        .WillOnce(Return(true));

    EXPECT_CALL(*dbif_mock(),
            Db_AddColumnfamily(Field(&GenDb::Cf::cfname_,
                    g_viz_constants.FLOW_TABLE_PROT_DP)))
        .Times(1)
        .WillOnce(Return(true));
    
    EXPECT_CALL(*dbif_mock(),
            Db_AddColumnfamily(Field(&GenDb::Cf::cfname_,
                    g_viz_constants.FLOW_TABLE_ALL_FIELDS)))
        .Times(1)
        .WillOnce(Return(true));

    GenDb::ColElement rowkey;
    std::string flowu_str = "d6ab8614-7745-4211-b6e3-a33b3dfcc270";
    boost::uuids::uuid flowu = boost::uuids::string_generator()(flowu_str);
    std::string flowu_s(flowu.size(), 0);
    std::copy(flowu.begin(), flowu.end(), flowu_s.begin());
    rowkey.elem_value = flowu_s;
    EXPECT_CALL(*dbif_mock(),
            Db_AddColumn(AllOf(Field(&GenDb::Column::cfname_, g_viz_constants.FLOW_TABLE),
                    Field(&GenDb::Column::rowkey_, rowkey))))
        .Times(1)
        .WillOnce(Return(true));

    uint64_t temp_u64;
    uint32_t temp_u32;
    temp_u64 = rmsg.hdr.get_Timestamp();
    temp_u32 = temp_u64/DbHandler::RowTimeInUSec;
    uint32_t temp2_u32;
    put_value((uint8_t *)&temp2_u32, sizeof(temp2_u32), temp_u32);
    rowkey.elem_value.assign((const char *)&temp2_u32, sizeof(temp2_u32));
    EXPECT_CALL(*dbif_mock(),
            Db_AddColumn(AllOf(Field(&GenDb::Column::cfname_, g_viz_constants.FLOW_TABLE_VN2VN),
                    Field(&GenDb::Column::rowkey_, rowkey))))
        .Times(1)
        .WillOnce(Return(true));

    EXPECT_CALL(*dbif_mock(),
            Db_AddColumn(Field(&GenDb::Column::cfname_, g_viz_constants.FLOW_TABLE_SVN_SIP)))
        .Times(1)
        .WillOnce(Return(true));

    EXPECT_CALL(*dbif_mock(),
            Db_AddColumn(Field(&GenDb::Column::cfname_, g_viz_constants.FLOW_TABLE_DVN_DIP)))
        .Times(1)
        .WillOnce(Return(true));

    EXPECT_CALL(*dbif_mock(),
            Db_AddColumn(Field(&GenDb::Column::cfname_, g_viz_constants.FLOW_TABLE_PROT_SP)))
        .Times(1)
        .WillOnce(Return(true));

    EXPECT_CALL(*dbif_mock(),
            Db_AddColumn(Field(&GenDb::Column::cfname_, g_viz_constants.FLOW_TABLE_PROT_DP)))
        .Times(1)
        .WillOnce(Return(true));

    EXPECT_CALL(*dbif_mock(),
            Db_AddColumn(Field(&GenDb::Column::cfname_, g_viz_constants.FLOW_TABLE_ALL_FIELDS)))
        .Times(1)
        .WillOnce(Return(true));

    db_handler()->VizCreateFlowTables();

    db_handler()->FlowTableInsert(rmsg);
}

int main(int argc, char **argv) {
    LoggingInit();
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

