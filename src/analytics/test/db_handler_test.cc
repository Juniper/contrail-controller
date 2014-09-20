/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <pthread.h>
#include <boost/bind.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/assign/ptr_list_of.hpp>
#include <boost/uuid/uuid.hpp>
#include "testing/gunit.h"
#include "base/logging.h"
#include "sandesh/sandesh_types.h"
#include "sandesh/sandesh.h"
#include "sandesh/sandesh_message_builder.h"
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
using namespace pugi;
using namespace GenDb;

class DbHandlerTest : public ::testing::Test {
public:
    DbHandlerTest() :
        builder_(SandeshXMLMessageTestBuilder::GetInstance()),
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

protected:
    class SandeshXMLMessageTest : public SandeshXMLMessage {
    public:
        SandeshXMLMessageTest() {}
        virtual ~SandeshXMLMessageTest() {}

        virtual bool Parse(const uint8_t *xml_msg, size_t size) {
            xml_parse_result result = xdoc_.load_buffer(xml_msg, size,
                parse_default & ~parse_escapes);
            if (!result) {
                LOG(ERROR, __func__ << ": Unable to load Sandesh XML Test." <<
                    "(status=" << result.status << ", offset=" <<
                    result.offset << "): " << xml_msg);
                return false;
            }
            message_node_ = xdoc_.first_child();
            message_type_ = message_node_.name();
            size_ = size;
            return true;
        }

        void SetHeader(const SandeshHeader &header) { header_ = header; }
    };

    class SandeshXMLMessageTestBuilder : public SandeshMessageBuilder {
    public:
        SandeshXMLMessageTestBuilder() {}

        virtual SandeshMessage *Create(const uint8_t *xml_msg,
            size_t size) const {
            SandeshXMLMessageTest *msg = new SandeshXMLMessageTest;
            msg->Parse(xml_msg, size);
            return msg;
        }

        static SandeshXMLMessageTestBuilder *GetInstance() {
            return &instance_;
        }

    private:
        static SandeshXMLMessageTestBuilder instance_;
    };

    SandeshMessageBuilder *builder_;
    boost::uuids::random_generator rgen_;

private:
    void DbErrorHandlerFn() {
        assert(0);
    }

    EventManager evm_;
    CdbIfMock *dbif_mock_;
    DbHandler *db_handler_;
};


DbHandlerTest::SandeshXMLMessageTestBuilder
    DbHandlerTest::SandeshXMLMessageTestBuilder::instance_;

TEST_F(DbHandlerTest, MessageTableOnlyInsertTest) {
    SandeshHeader hdr;

    hdr.set_Source("127.0.0.1");
    hdr.set_Module("VizdTest");
    hdr.set_InstanceId("Test");
    hdr.set_NodeType("Test");
    hdr.set_Timestamp(UTCTimestampUsec());
    std::string messagetype("SandeshAsyncTest2");
    std::string xmlmessage = "<SandeshAsyncTest2 type=\"sandesh\"><file type=\"string\" identifier=\"-32768\">src/analytics/test/viz_collector_test.cc</file><line type=\"i32\" identifier=\"-32767\">80</line><f1 type=\"struct\" identifier=\"1\"><SAT2_struct><f1 type=\"string\" identifier=\"1\">sat2string101</f1><f2 type=\"i32\" identifier=\"2\">101</f2></SAT2_struct></f1><f2 type=\"i32\" identifier=\"2\">101</f2></SandeshAsyncTest2>";

    SandeshXMLMessageTest *msg = dynamic_cast<SandeshXMLMessageTest *>(
        builder_->Create(
            reinterpret_cast<const uint8_t *>(xmlmessage.c_str()),
            xmlmessage.size()));
    msg->SetHeader(hdr);
    boost::uuids::uuid unm(rgen_());
    VizMsg vmsgp(msg, unm); 

    GenDb::DbDataValueVec rowkey;
    rowkey.push_back(unm);

    boost::ptr_vector<GenDb::NewCol> msg_table_expected_vector = 
        boost::assign::ptr_list_of<GenDb::NewCol>
        (GenDb::NewCol(g_viz_constants.SOURCE, hdr.get_Source()))
        (GenDb::NewCol(g_viz_constants.NAMESPACE, std::string()))
        (GenDb::NewCol(g_viz_constants.MODULE, hdr.get_Module()))
        (GenDb::NewCol(g_viz_constants.INSTANCE_ID, hdr.get_InstanceId()))
        (GenDb::NewCol(g_viz_constants.NODE_TYPE, hdr.get_NodeType()))
        (GenDb::NewCol(g_viz_constants.TIMESTAMP,
            static_cast<uint64_t>(hdr.get_Timestamp())))
        (GenDb::NewCol(g_viz_constants.CATEGORY, std::string()))
        (GenDb::NewCol(g_viz_constants.LEVEL,
            static_cast<uint32_t>(0)))
        (GenDb::NewCol(g_viz_constants.MESSAGE_TYPE, messagetype))
        (GenDb::NewCol(g_viz_constants.SEQUENCE_NUM,
            static_cast<uint32_t>(0)))
        (GenDb::NewCol(g_viz_constants.VERSION,
            static_cast<uint32_t>(0)))
        (GenDb::NewCol(g_viz_constants.SANDESH_TYPE,
            static_cast<uint8_t>(0)))
        (GenDb::NewCol(g_viz_constants.DATA, xmlmessage));

    EXPECT_CALL(*dbif_mock(),
            Db_AddColumnProxy(
                Pointee(
                    AllOf(Field(&GenDb::ColList::cfname_, g_viz_constants.COLLECTOR_GLOBAL_TABLE),
                        Field(&GenDb::ColList::rowkey_, rowkey),
                        Field(&GenDb::ColList::columns_, msg_table_expected_vector)))))
        .Times(1)
        .WillOnce(Return(true));

    db_handler()->MessageTableOnlyInsert(&vmsgp);
    vmsgp.msg = NULL;
    delete msg;
}

TEST_F(DbHandlerTest, MessageIndexTableInsertTest) {
    SandeshHeader hdr;

    hdr.set_Source("127.0.0.1");
    hdr.set_Timestamp(UTCTimestampUsec());
    boost::uuids::uuid unm(rgen_());

    DbDataValueVec *colname(new DbDataValueVec(1,
        (uint32_t)(hdr.get_Timestamp() & g_viz_constants.RowTimeInMask)));
    DbDataValueVec *colvalue(new DbDataValueVec(1, unm));
    boost::ptr_vector<GenDb::NewCol> idx_expected_vector =
        boost::assign::ptr_list_of<GenDb::NewCol> 
        (GenDb::NewCol(colname, colvalue));

    GenDb::DbDataValueVec src_idx_rowkey;
    src_idx_rowkey.push_back((uint32_t)(hdr.get_Timestamp() >> g_viz_constants.RowTimeInBits));
    src_idx_rowkey.push_back(hdr.get_Source());
    EXPECT_CALL(*dbif_mock(),
            Db_AddColumnProxy(
                Pointee(
                    AllOf(Field(&GenDb::ColList::cfname_, g_viz_constants.MESSAGE_TABLE_SOURCE),
                        Field(&GenDb::ColList::rowkey_, src_idx_rowkey),
                        Field(&GenDb::ColList::columns_,
                            idx_expected_vector)))))
        .Times(1)
        .WillOnce(Return(true));

    db_handler()->MessageIndexTableInsert(g_viz_constants.MESSAGE_TABLE_SOURCE,
            hdr, "", unm, "");
}

TEST_F(DbHandlerTest, MessageTableInsertTest) {
    SandeshHeader hdr;

    hdr.set_Source("127.0.0.1");
    hdr.set_Module("VizdTest");
    std::string messagetype("SandeshAsyncTest2");
    hdr.set_InstanceId("Test");
    hdr.set_NodeType("Test");
    hdr.set_Timestamp(UTCTimestampUsec());
    std::string xmlmessage = "<SandeshAsyncTest2 type=\"sandesh\"><file type=\"string\" identifier=\"-32768\">src/analytics/test/viz_collector_test.cc</file><line type=\"i32\" identifier=\"-32767\">80</line><f1 type=\"struct\" identifier=\"1\"><SAT2_struct><f1 type=\"string\" identifier=\"1\">sat2string101</f1><f2 type=\"i32\" identifier=\"2\">101</f2></SAT2_struct></f1><f2 type=\"i32\" identifier=\"2\">101</f2></SandeshAsyncTest2>";

    SandeshXMLMessageTest *msg = dynamic_cast<SandeshXMLMessageTest *>(
        builder_->Create(
            reinterpret_cast<const uint8_t *>(xmlmessage.c_str()),
            xmlmessage.size()));
    msg->SetHeader(hdr);
    boost::uuids::uuid unm(rgen_());
    VizMsg vmsgp(msg, unm); 
    
    GenDb::DbDataValueVec rowkey;
    rowkey.push_back(unm);

    boost::ptr_vector<GenDb::NewCol> msg_table_expected_vector = 
        boost::assign::ptr_list_of<GenDb::NewCol>
        (GenDb::NewCol(g_viz_constants.SOURCE, hdr.get_Source()))
        (GenDb::NewCol(g_viz_constants.NAMESPACE, std::string()))
        (GenDb::NewCol(g_viz_constants.MODULE, hdr.get_Module()))
        (GenDb::NewCol(g_viz_constants.INSTANCE_ID, hdr.get_InstanceId()))
        (GenDb::NewCol(g_viz_constants.NODE_TYPE, hdr.get_NodeType()))
        (GenDb::NewCol(g_viz_constants.TIMESTAMP,
            static_cast<uint64_t>(hdr.get_Timestamp())))
        (GenDb::NewCol(g_viz_constants.CATEGORY, std::string()))
        (GenDb::NewCol(g_viz_constants.LEVEL,
            static_cast<uint32_t>(0)))
        (GenDb::NewCol(g_viz_constants.MESSAGE_TYPE, messagetype))
        (GenDb::NewCol(g_viz_constants.SEQUENCE_NUM,
            static_cast<uint32_t>(0)))
        (GenDb::NewCol(g_viz_constants.VERSION,
            static_cast<uint32_t>(0)))
        (GenDb::NewCol(g_viz_constants.SANDESH_TYPE,
            static_cast<uint8_t>(0)))
        (GenDb::NewCol(g_viz_constants.DATA, xmlmessage));

    EXPECT_CALL(*dbif_mock(),
            Db_AddColumnProxy(
                Pointee(
                    AllOf(Field(&GenDb::ColList::cfname_, g_viz_constants.COLLECTOR_GLOBAL_TABLE),
                        Field(&GenDb::ColList::rowkey_, rowkey),
                        Field(&GenDb::ColList::columns_,
                            msg_table_expected_vector)))))
        .Times(1)
        .WillOnce(Return(true));

    DbDataValueVec *colname(new DbDataValueVec(1,
        (uint32_t)(hdr.get_Timestamp() & g_viz_constants.RowTimeInMask)));
    DbDataValueVec *colvalue(new DbDataValueVec(1, unm));
    boost::ptr_vector<GenDb::NewCol> idx_expected_vector = 
        boost::assign::ptr_list_of<GenDb::NewCol>
        (GenDb::NewCol(colname, colvalue));

    GenDb::DbDataValueVec src_idx_rowkey;
    src_idx_rowkey.push_back((uint32_t)(hdr.get_Timestamp() >> g_viz_constants.RowTimeInBits));
    src_idx_rowkey.push_back(hdr.get_Source());
    EXPECT_CALL(*dbif_mock(),
            Db_AddColumnProxy(
                Pointee(
                    AllOf(Field(&GenDb::ColList::cfname_, g_viz_constants.MESSAGE_TABLE_SOURCE),
                        Field(&GenDb::ColList::rowkey_, src_idx_rowkey),
                        Field(&GenDb::ColList::columns_,
                            idx_expected_vector)))))
        .Times(1)
        .WillOnce(Return(true));

    GenDb::DbDataValueVec mod_idx_rowkey;
    mod_idx_rowkey.push_back((uint32_t)(hdr.get_Timestamp() >> g_viz_constants.RowTimeInBits));
    mod_idx_rowkey.push_back(hdr.get_Module());
    EXPECT_CALL(*dbif_mock(),
            Db_AddColumnProxy(
                Pointee(
                    AllOf(Field(&GenDb::ColList::cfname_, g_viz_constants.MESSAGE_TABLE_MODULE_ID),
                        Field(&GenDb::ColList::rowkey_, mod_idx_rowkey),
                        Field(&GenDb::ColList::columns_,
                            idx_expected_vector)))))
        .Times(1)
        .WillOnce(Return(true));

    GenDb::DbDataValueVec cat_idx_rowkey;
    cat_idx_rowkey.push_back((uint32_t)(hdr.get_Timestamp() >> g_viz_constants.RowTimeInBits));
    cat_idx_rowkey.push_back(hdr.get_Category());
    EXPECT_CALL(*dbif_mock(),
            Db_AddColumnProxy(
                Pointee(
                    AllOf(Field(&GenDb::ColList::cfname_, g_viz_constants.MESSAGE_TABLE_CATEGORY),
                        Field(&GenDb::ColList::rowkey_, cat_idx_rowkey),
                        Field(&GenDb::ColList::columns_,
                            idx_expected_vector)))))
        .Times(1)
        .WillOnce(Return(true));

    GenDb::DbDataValueVec msgtype_idx_rowkey;
    msgtype_idx_rowkey.push_back((uint32_t)(hdr.get_Timestamp() >> g_viz_constants.RowTimeInBits));
    msgtype_idx_rowkey.push_back(messagetype);
    EXPECT_CALL(*dbif_mock(),
            Db_AddColumnProxy(
                Pointee(
                    AllOf(Field(&GenDb::ColList::cfname_, g_viz_constants.MESSAGE_TABLE_MESSAGE_TYPE),
                        Field(&GenDb::ColList::rowkey_, msgtype_idx_rowkey),
                        Field(&GenDb::ColList::columns_,
                            idx_expected_vector)))))
        .Times(1)
        .WillOnce(Return(true));

    GenDb::DbDataValueVec ts_idx_rowkey;
    ts_idx_rowkey.push_back((uint32_t)(hdr.get_Timestamp() >> g_viz_constants.RowTimeInBits));
    EXPECT_CALL(*dbif_mock(),
            Db_AddColumnProxy(
                Pointee(
                    AllOf(Field(&GenDb::ColList::cfname_, g_viz_constants.MESSAGE_TABLE_TIMESTAMP),
                        Field(&GenDb::ColList::rowkey_, ts_idx_rowkey),
                        Field(&GenDb::ColList::columns_,
                            idx_expected_vector)))))
        .Times(1)
        .WillOnce(Return(true));

    EXPECT_CALL(*dbif_mock(),
            Db_AddColumnProxy(
                Pointee(
                    AllOf(Field(&GenDb::ColList::cfname_, g_viz_constants.STATS_TABLE_BY_STR_TAG),
                        _,
                        _))))
        .Times(2)
        .WillRepeatedly(Return(true));

    db_handler()->MessageTableInsert(&vmsgp);
    vmsgp.msg = NULL;
    delete msg;
}

TEST_F(DbHandlerTest, ObjectTableInsertTest) {
    SandeshHeader hdr;
    hdr.set_Timestamp(UTCTimestampUsec());
    hdr.set_Source("127.0.0.1");
    uint64_t timestamp(hdr.get_Timestamp()); 
    boost::uuids::uuid unm(rgen_());
    std::string table("ObjectTableInsertTest");
    std::string rowkey_str("ObjectTableInsertTestRowkey");

      {
        DbDataValueVec *colname(new DbDataValueVec());
        colname->reserve(2);
        colname->push_back("ObjectTableInsertTestRowkey");
        colname->push_back((uint32_t)(hdr.get_Timestamp() & g_viz_constants.RowTimeInMask));

        DbDataValueVec *colvalue(new DbDataValueVec(1, unm));
        boost::ptr_vector<GenDb::NewCol> expected_vector = 
            boost::assign::ptr_list_of<GenDb::NewCol>
            (GenDb::NewCol(colname, colvalue));

        GenDb::DbDataValueVec rowkey;
        rowkey.push_back((uint32_t)(hdr.get_Timestamp() >> g_viz_constants.RowTimeInBits));
        rowkey.push_back((uint8_t)0);
        rowkey.push_back("ObjectTableInsertTest");
        EXPECT_CALL(*dbif_mock(),
                Db_AddColumnProxy(
                    Pointee(
                        AllOf(Field(&GenDb::ColList::cfname_, g_viz_constants.OBJECT_TABLE), 
                            Field(&GenDb::ColList::rowkey_, rowkey),
                            Field(&GenDb::ColList::columns_,
                                expected_vector)))))
            .Times(1)
            .WillOnce(Return(true));
      }

      {
        DbDataValueVec *colname(new DbDataValueVec(1,
            (uint32_t)(hdr.get_Timestamp() & g_viz_constants.RowTimeInMask)));
        DbDataValueVec *colvalue(new DbDataValueVec(1,
            "ObjectTableInsertTestRowkey"));
        boost::ptr_vector<GenDb::NewCol> expected_vector =
            boost::assign::ptr_list_of<GenDb::NewCol>
            (GenDb::NewCol(colname, colvalue));

        GenDb::DbDataValueVec rowkey;
        rowkey.push_back((uint32_t)(hdr.get_Timestamp() >> g_viz_constants.RowTimeInBits));
        rowkey.push_back("ObjectTableInsertTest");
        EXPECT_CALL(*dbif_mock(),
                Db_AddColumnProxy(
                    Pointee(
                        AllOf(Field(&GenDb::ColList::cfname_, g_viz_constants.OBJECT_VALUE_TABLE),
                            Field(&GenDb::ColList::rowkey_, rowkey),
                            Field(&GenDb::ColList::columns_,
                                expected_vector)))))
            .Times(1)
            .WillOnce(Return(true));
      }

      {
        boost::uuids::string_generator gen;
        boost::uuids::uuid unm_allf = gen(std::string("ffffffffffffffffffffffffffffffff"));
        DbDataValueVec *colname(new DbDataValueVec);
        colname->reserve(4);
        colname->push_back("ObjectTableInsertTest:Objecttype");
        colname->push_back("");
        colname->push_back((uint32_t)0);
        colname->push_back(unm_allf);
        DbDataValueVec *colvalue(new DbDataValueVec(1,""));
        boost::ptr_vector<GenDb::NewCol> expected_vector =
            boost::assign::ptr_list_of<GenDb::NewCol>
            (GenDb::NewCol(colname, colvalue));

        GenDb::DbDataValueVec rowkey;
        rowkey.push_back((uint32_t)(hdr.get_Timestamp() >> g_viz_constants.RowTimeInBits));
        rowkey.push_back((uint8_t)0);
        rowkey.push_back("FieldNames");
        rowkey.push_back("fields");
        rowkey.push_back("name");
        EXPECT_CALL(*dbif_mock(),
                Db_AddColumnProxy(
                    Pointee(
                        AllOf(Field(&GenDb::ColList::cfname_, g_viz_constants.STATS_TABLE_BY_STR_TAG),
                            Field(&GenDb::ColList::rowkey_, rowkey),_))))
            .Times(1)
            .WillOnce(Return(true));
      }

      {
        boost::uuids::string_generator gen;
        boost::uuids::uuid unm_allf = gen(std::string("ffffffffffffffffffffffffffffffff"));
        DbDataValueVec *colname(new DbDataValueVec);
        colname->reserve(4);
        colname->push_back(hdr.get_Source());
        colname->push_back("");
        colname->push_back((uint32_t)0);
        colname->push_back(unm_allf);
        DbDataValueVec *colvalue(new DbDataValueVec(1,""));
        boost::ptr_vector<GenDb::NewCol> expected_vector =
            boost::assign::ptr_list_of<GenDb::NewCol> 
            (GenDb::NewCol(colname, colvalue));

        GenDb::DbDataValueVec rowkey;
        rowkey.push_back((uint32_t)(hdr.get_Timestamp() >> g_viz_constants.RowTimeInBits));
        rowkey.push_back((uint8_t)0);
        rowkey.push_back("FieldNames");
        rowkey.push_back("fields");
        rowkey.push_back("Source");
        EXPECT_CALL(*dbif_mock(),
                Db_AddColumnProxy(
                    Pointee(
                        AllOf(Field(&GenDb::ColList::cfname_, g_viz_constants.STATS_TABLE_BY_STR_TAG),
                            Field(&GenDb::ColList::rowkey_, rowkey),_))))
            .Times(1)
            .WillOnce(Return(true));
      }

    
    db_handler()->ObjectTableInsert(table, rowkey_str, timestamp, unm);
}

TEST_F(DbHandlerTest, FlowTableInsertTest) {
    init_vizd_tables();

    SandeshHeader hdr;
    hdr.set_Module("VizdTest");
    hdr.set_Source("127.0.0.1");
    std::string messagetype("");
    std::string xmlmessage = "<FlowDataIpv4Object type=\"sandesh\"><flowdata type=\"struct\" identifier=\"1\"><FlowDataIpv4><flowuuid type=\"string\" identifier=\"1\">555788e0-513c-4351-8711-3fc481cf2eb4</flowuuid><direction_ing type=\"byte\" identifier=\"2\">0</direction_ing><sourcevn type=\"string\" identifier=\"3\">default-domain:demo:vn1</sourcevn><sourceip type=\"i32\" identifier=\"4\">-1062731011</sourceip><destvn type=\"string\" identifier=\"5\">default-domain:demo:vn0</destvn><destip type=\"i32\" identifier=\"6\">-1062731267</destip><protocol type=\"byte\" identifier=\"7\">6</protocol><sport type=\"i16\" identifier=\"8\">5201</sport><dport type=\"i16\" identifier=\"9\">-24590</dport><vm type=\"string\" identifier=\"12\">04430130-664a-4b89-9287-39d71f351207</vm><reverse_uuid type=\"string\" identifier=\"16\">58745ee7-d616-4e59-b8f7-96f896487c9f</reverse_uuid><bytes type=\"i64\" identifier=\"23\">0</bytes><packets type=\"i64\" identifier=\"24\">0</packets><diff_bytes type=\"i64\" identifier=\"26\">0</diff_bytes><diff_packets type=\"i64\" identifier=\"27\">0</diff_packets></FlowDataIpv4></flowdata></FlowDataIpv4Object>";

    SandeshXMLMessageTest *msg = dynamic_cast<SandeshXMLMessageTest *>(
        builder_->Create(
            reinterpret_cast<const uint8_t *>(xmlmessage.c_str()),
            xmlmessage.size()));
    msg->SetHeader(hdr);

    std::string flowu_str = "555788e0-513c-4351-8711-3fc481cf2eb4";
    boost::uuids::uuid flowu = boost::uuids::string_generator()(flowu_str);

      {
        GenDb::DbDataValueVec rowkey;
        rowkey.push_back(flowu);

        EXPECT_CALL(*dbif_mock(),
                Db_AddColumnProxy(
                    Pointee(
                        AllOf(Field(&GenDb::ColList::cfname_, g_viz_constants.FLOW_TABLE),
                            Field(&GenDb::ColList::rowkey_, rowkey)))))
            .Times(1)
            .WillOnce(Return(true));
      }

    GenDb::DbDataValueVec ocolvalue;
    ocolvalue.push_back((uint64_t)0); //bytes
    ocolvalue.push_back((uint64_t)0); //pkts
    ocolvalue.push_back((uint8_t)0); //dir
    ocolvalue.push_back(flowu); //flowuuid
    ocolvalue.push_back(hdr.get_Source()); //vrouter
    ocolvalue.push_back("default-domain:demo:vn1"); //svn
    ocolvalue.push_back("default-domain:demo:vn0"); //dvn
    ocolvalue.push_back((uint32_t)-1062731011); //sip
    ocolvalue.push_back((uint32_t)-1062731267); //dip
    ocolvalue.push_back((uint8_t)6); //prot
    ocolvalue.push_back((uint16_t)5201); //sport
    ocolvalue.push_back((uint16_t)-24590); //dport
    ocolvalue.push_back(""); //json

      {
        GenDb::DbDataValueVec *colname(new GenDb::DbDataValueVec);
        colname->reserve(4);
        colname->push_back("default-domain:demo:vn1");
        colname->push_back((uint32_t)-1062731011);
        colname->push_back((uint32_t)(hdr.get_Timestamp() & g_viz_constants.RowTimeInMask));
        colname->push_back(flowu);
        GenDb::DbDataValueVec *colvalue(new GenDb::DbDataValueVec(ocolvalue));
        boost::ptr_vector<GenDb::NewCol> expected_vector =
            boost::assign::ptr_list_of<GenDb::NewCol> 
            (GenDb::NewCol(colname, colvalue));

        GenDb::DbDataValueVec rowkey;
        rowkey.push_back((uint32_t)(hdr.get_Timestamp() >> g_viz_constants.RowTimeInBits));
        uint8_t partition_no = 0;
        rowkey.push_back(partition_no);
        rowkey.push_back((uint8_t)0); //direction

        EXPECT_CALL(*dbif_mock(),
                Db_AddColumnProxy(
                    Pointee(
                        AllOf(Field(&GenDb::ColList::cfname_, g_viz_constants.FLOW_TABLE_SVN_SIP),
                            Field(&GenDb::ColList::rowkey_, rowkey),
                            Field(&GenDb::ColList::columns_,
                                expected_vector)))))
            .Times(1)
            .WillOnce(Return(true));
      }
      {
        GenDb::DbDataValueVec *colname(new GenDb::DbDataValueVec);
        colname->reserve(4);
        colname->push_back("default-domain:demo:vn0");
        colname->push_back((uint32_t)-1062731267);
        colname->push_back((uint32_t)(hdr.get_Timestamp() & g_viz_constants.RowTimeInMask));
        colname->push_back(flowu);
        GenDb::DbDataValueVec *colvalue(new GenDb::DbDataValueVec(ocolvalue));
        boost::ptr_vector<GenDb::NewCol> expected_vector =
            boost::assign::ptr_list_of<GenDb::NewCol>
            (GenDb::NewCol(colname, colvalue));

        GenDb::DbDataValueVec rowkey;
        rowkey.push_back((uint32_t)(hdr.get_Timestamp() >> g_viz_constants.RowTimeInBits));
        uint8_t partition_no = 0;
        rowkey.push_back(partition_no);
        rowkey.push_back((uint8_t)0); //direction

        EXPECT_CALL(*dbif_mock(),
                Db_AddColumnProxy(
                    Pointee(
                        AllOf(Field(&GenDb::ColList::cfname_, g_viz_constants.FLOW_TABLE_DVN_DIP),
                            Field(&GenDb::ColList::rowkey_, rowkey),
                            Field(&GenDb::ColList::columns_,
                                expected_vector)))))
            .Times(1)
            .WillOnce(Return(true));
      }
      {
        GenDb::DbDataValueVec *colname(new GenDb::DbDataValueVec);
        colname->reserve(4);
        colname->push_back((uint8_t)6);
        colname->push_back((uint16_t)5201);
        colname->push_back((uint32_t)(hdr.get_Timestamp() & g_viz_constants.RowTimeInMask));
        colname->push_back(flowu);
        GenDb::DbDataValueVec *colvalue(new GenDb::DbDataValueVec(ocolvalue));
        boost::ptr_vector<GenDb::NewCol> expected_vector =
            boost::assign::ptr_list_of<GenDb::NewCol>
            (GenDb::NewCol(colname, colvalue));

        GenDb::DbDataValueVec rowkey;
        rowkey.push_back((uint32_t)(hdr.get_Timestamp() >> g_viz_constants.RowTimeInBits));
        uint8_t partition_no = 0;
        rowkey.push_back(partition_no);
        rowkey.push_back((uint8_t)0); //direction

        EXPECT_CALL(*dbif_mock(),
                Db_AddColumnProxy(
                    Pointee(
                        AllOf(Field(&GenDb::ColList::cfname_, g_viz_constants.FLOW_TABLE_PROT_SP),
                            Field(&GenDb::ColList::rowkey_, rowkey),
                            Field(&GenDb::ColList::columns_,
                                expected_vector)))))
            .Times(1)
            .WillOnce(Return(true));
      }

      {
        GenDb::DbDataValueVec *colname(new GenDb::DbDataValueVec);
        colname->reserve(4);
        colname->push_back((uint8_t)6);
        colname->push_back((uint16_t)-24590);
        colname->push_back((uint32_t)(hdr.get_Timestamp() & g_viz_constants.RowTimeInMask));
        colname->push_back(flowu);
        GenDb::DbDataValueVec *colvalue(new GenDb::DbDataValueVec(ocolvalue));
        boost::ptr_vector<GenDb::NewCol> expected_vector =
            boost::assign::ptr_list_of<GenDb::NewCol>
            (GenDb::NewCol(colname, colvalue));

        GenDb::DbDataValueVec rowkey;
        rowkey.push_back((uint32_t)(hdr.get_Timestamp() >> g_viz_constants.RowTimeInBits));
        uint8_t partition_no = 0;
        rowkey.push_back(partition_no);
        rowkey.push_back((uint8_t)0); //direction

        EXPECT_CALL(*dbif_mock(),
                Db_AddColumnProxy(
                    Pointee(
                        AllOf(Field(&GenDb::ColList::cfname_, g_viz_constants.FLOW_TABLE_PROT_DP),
                            Field(&GenDb::ColList::rowkey_, rowkey),
                            Field(&GenDb::ColList::columns_,
                                expected_vector)))))
            .Times(1)
            .WillOnce(Return(true));
      }

      {
        GenDb::DbDataValueVec *colname(new GenDb::DbDataValueVec);
        colname->reserve(4);
        colname->push_back(hdr.get_Source()); //vrouter
        colname->push_back((uint32_t)(hdr.get_Timestamp() & g_viz_constants.RowTimeInMask));
        colname->push_back(flowu);
        GenDb::DbDataValueVec *colvalue(new GenDb::DbDataValueVec(ocolvalue));
        boost::ptr_vector<GenDb::NewCol> expected_vector =
            boost::assign::ptr_list_of<GenDb::NewCol>
            (GenDb::NewCol(colname, colvalue));

        GenDb::DbDataValueVec rowkey;
        rowkey.push_back((uint32_t)(hdr.get_Timestamp() >> g_viz_constants.RowTimeInBits));
        uint8_t partition_no = 0;
        rowkey.push_back(partition_no);
        rowkey.push_back((uint8_t)0); //direction

        EXPECT_CALL(*dbif_mock(),
                Db_AddColumnProxy(
                    Pointee(
                        AllOf(Field(&GenDb::ColList::cfname_, g_viz_constants.FLOW_TABLE_VROUTER),
                            Field(&GenDb::ColList::rowkey_, rowkey),
                            Field(&GenDb::ColList::columns_,
                                expected_vector)))))
            .Times(1)
            .WillOnce(Return(true));
      }

    db_handler()->FlowTableInsert(msg->GetMessageNode(),
        msg->GetHeader());
    delete msg;
}

class UUIDRandomGenTest : public ::testing::Test {
 public:
    bool PopulateUUIDMap(std::map<std::string, unsigned int>& uuid_map,
        unsigned int count, bool lock, bool rgen_on_stack) {
        for (unsigned int i = 0; i < count; i++) {
            std::string uuids = GenerateUUID(lock, rgen_on_stack);
            std::map<std::string, unsigned int>::const_iterator it =
                uuid_map.find(uuids);
            if (it == uuid_map.end()) {
                uuid_map[uuids] = i;
            } else {
                LOG(ERROR, "DUPLICATE uuid:" << uuids << " found in thread id:"
                    << pthread_self() << ", counter:" << i <<
                    ", earlier counter:" << it->second << ", id:" <<
                    it->first);
                EXPECT_EQ(0, 1);
            }
        }
        return true;
    }

 private:
    std::string GenerateUUID(bool lock, bool rgen_on_stack) {
        boost::uuids::uuid uuid;
        if (lock) {
            tbb::mutex::scoped_lock lock(rgen_mutex_);
            if (rgen_on_stack) {
                uuid = boost::uuids::random_generator()();
            } else {
                uuid = rgen_();
            }
        } else {
            if (rgen_on_stack) {
                uuid = boost::uuids::random_generator()();
            } else {
                uuid = rgen_();
            }
        }
        std::stringstream ss;
        ss << uuid;
        return ss.str();
    }

    tbb::mutex rgen_mutex_;
    boost::uuids::random_generator rgen_;
};

class WorkerThread {
 public:
    typedef boost::function<void(void)> WorkerFn;
    WorkerThread(WorkerFn fn) :
        thread_id_(pthread_self()),
        fn_(fn) {
    }
    static void *ThreadRun(void *objp) {
        WorkerThread *obj = reinterpret_cast<WorkerThread *>(objp);
        obj->fn_();
        return NULL;
    }
    void Start() {
        int res = pthread_create(&thread_id_, NULL, &ThreadRun, this);
        assert(res == 0);
    }
    void Join() {
        int res = pthread_join(thread_id_, NULL);
        assert(res == 0);
    }

 private:
    pthread_t thread_id_;
    WorkerFn fn_;
};

TEST_F(UUIDRandomGenTest, SingleThread) {
    unsigned int count = 10000;
    std::map<std::string, unsigned int> uuid_map;
    bool unique = PopulateUUIDMap(uuid_map, count, false, false);
    EXPECT_TRUE(unique);
}

TEST_F(UUIDRandomGenTest, SingleThreadOnStack) {
    unsigned int count = 10000;
    std::map<std::string, unsigned int> uuid_map;
    bool unique = PopulateUUIDMap(uuid_map, count, false, true);
    EXPECT_TRUE(unique);
}

TEST_F(UUIDRandomGenTest, MultiThreadedLocked) {
    unsigned int count = 1000000;
    std::map<std::string, unsigned int> uuid_map1;
    WorkerThread t1(boost::bind(&UUIDRandomGenTest::PopulateUUIDMap, this,
        uuid_map1, count, true, false));
    std::map<std::string, unsigned int> uuid_map2;
    WorkerThread t2(boost::bind(&UUIDRandomGenTest::PopulateUUIDMap, this,
        uuid_map2, count, true, false));
    t1.Start();
    t2.Start();
    t1.Join();
    t2.Join();
}

TEST_F(UUIDRandomGenTest, DISABLED_MultiThreadedNoLock) {
    unsigned int count = 1000000;
    std::map<std::string, unsigned int> uuid_map1;
    WorkerThread t1(boost::bind(&UUIDRandomGenTest::PopulateUUIDMap, this,
        uuid_map1, count, false, false));
    std::map<std::string, unsigned int> uuid_map2;
    WorkerThread t2(boost::bind(&UUIDRandomGenTest::PopulateUUIDMap, this,
        uuid_map2, count, false, false));
    t1.Start();
    t2.Start();
    t1.Join();
    t2.Join();
}

int main(int argc, char **argv) {
    LoggingInit();
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

