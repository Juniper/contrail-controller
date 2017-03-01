/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <pthread.h>

#include <boost/bind.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/assign/ptr_list_of.hpp>
#include <boost/uuid/uuid.hpp>

#include <testing/gunit.h>
#include <base/logging.h>
#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh.h>
#include <sandesh/sandesh_message_builder.h>
#include <sandesh/common/flow_types.h>

#include <analytics/viz_types.h>
#include <analytics/viz_constants.h>
#include <analytics/db_handler.h>
#include <analytics/db_handler_impl.h>
#include <analytics/vizd_table_desc.h>

#include <analytics/test/cql_if_mock.h>

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

TtlMap ttl_map = g_viz_constants.TtlValuesDefault;

struct DbHandlerCacheParam {
        uint32_t field_cache_index_;
        std::set<std::string> field_cache_set_;
};

class DbHandlerTest : public ::testing::Test {
public:
    DbHandlerTest() :
        builder_(SandeshXMLMessageTestBuilder::GetInstance()),
        dbif_mock_(new CqlIfMock()),
        db_handler_(new DbHandler(dbif_mock_, ttl_map)) {
    }

    ~DbHandlerTest() {
    }

    virtual void SetUp() {
    }

    virtual void TearDown() {
    }

    CqlIfMock* dbif_mock() {
        return dbif_mock_;
    }

    DbHandlerPtr db_handler() {
        return db_handler_;
    }

    struct DbHandlerCacheParam GetDbHandlerCacheParam() {
        db_handler_cache_param_.field_cache_index_ = DbHandler::field_cache_index_;
        db_handler_cache_param_.field_cache_set_ = DbHandler::field_cache_set_;
        return db_handler_cache_param_;
    }

    bool WriteToCache(uint32_t temp_t2, std::string fc_entry) {
        return db_handler()->CanRecordDataForT2(temp_t2, fc_entry);
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

    void DbAddColumnCbFn(bool success) {
        assert(success);
    }

    void MessageTableOnlyInsert(const VizMsg *vmsgp) {
        db_handler()->MessageTableOnlyInsert(vmsgp,
            boost::bind(&DbHandlerTest::DbAddColumnCbFn, this, _1));
    }

    void MessageTableInsert(const VizMsg *vmsgp) {
        db_handler()->MessageTableInsert(vmsgp,
            boost::bind(&DbHandlerTest::DbAddColumnCbFn, this, _1));
    }

    void MessageIndexTableInsert(const std::string& cfname,
        const SandeshHeader& header, const std::string& message_type,
        const boost::uuids::uuid& unm, const std::string keyword) {
        db_handler()->MessageIndexTableInsert(cfname, header, message_type,
            unm, keyword,
            boost::bind(&DbHandlerTest::DbAddColumnCbFn, this, _1));
    }

    void ObjectTableInsert(const std::string &table,
        const std::string &rowkey_str, uint64_t timestamp,
        const boost::uuids::uuid& unm, const VizMsg *vmsgp) {
        db_handler()->ObjectTableInsert(table, rowkey_str, timestamp,
            unm, vmsgp,
            boost::bind(&DbHandlerTest::DbAddColumnCbFn, this, _1));
    }

    void FlowTableInsert(const pugi::xml_node& parent,
        const SandeshHeader &header) {
        db_handler()->FlowTableInsert(parent, header,
            boost::bind(&DbHandlerTest::DbAddColumnCbFn, this, _1));
    }

private:
    EventManager evm_;
    CqlIfMock *dbif_mock_;
    DbHandlerPtr db_handler_;
    DbHandlerCacheParam db_handler_cache_param_;
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

    int ttl = ttl_map.find(TtlType::GLOBAL_TTL)->second*3600;
    boost::ptr_vector<GenDb::NewCol> msg_table_expected_vector =
        boost::assign::ptr_list_of<GenDb::NewCol>
        (GenDb::NewCol(g_viz_constants.SOURCE, hdr.get_Source(), ttl))
        (GenDb::NewCol(g_viz_constants.NAMESPACE, std::string(), ttl))
        (GenDb::NewCol(g_viz_constants.MODULE, hdr.get_Module(), ttl))
        (GenDb::NewCol(g_viz_constants.INSTANCE_ID, hdr.get_InstanceId(), ttl))
        (GenDb::NewCol(g_viz_constants.NODE_TYPE, hdr.get_NodeType(), ttl))
        (GenDb::NewCol(g_viz_constants.TIMESTAMP,
            static_cast<uint64_t>(hdr.get_Timestamp()), ttl))
        (GenDb::NewCol(g_viz_constants.CATEGORY, std::string(), ttl))
        (GenDb::NewCol(g_viz_constants.LEVEL,
            static_cast<uint32_t>(0), ttl))
        (GenDb::NewCol(g_viz_constants.MESSAGE_TYPE, messagetype, ttl))
        (GenDb::NewCol(g_viz_constants.SEQUENCE_NUM,
            static_cast<uint32_t>(0), ttl))
        (GenDb::NewCol(g_viz_constants.VERSION,
            static_cast<uint32_t>(0), ttl))
        (GenDb::NewCol(g_viz_constants.SANDESH_TYPE,
            static_cast<uint8_t>(0), ttl))
        (GenDb::NewCol(g_viz_constants.DATA, xmlmessage, ttl));

    EXPECT_CALL(*dbif_mock(),
            Db_AddColumnProxy(
                Pointee(
                    AllOf(Field(&GenDb::ColList::cfname_, g_viz_constants.COLLECTOR_GLOBAL_TABLE),
                        Field(&GenDb::ColList::rowkey_, rowkey),
                        Field(&GenDb::ColList::columns_, msg_table_expected_vector)))))
        .Times(1)
        .WillOnce(Return(true));

    MessageTableOnlyInsert(&vmsgp);
    vmsgp.msg = NULL;
    delete msg;
}

TEST_F(DbHandlerTest, MessageTableOnlyInsertConfigAuditTest) {
    SandeshHeader hdr;

    hdr.set_Source("127.0.0.1");
    hdr.set_Module("VizdTest");
    hdr.set_InstanceId("Test");
    hdr.set_NodeType("Test");
    hdr.set_Timestamp(UTCTimestampUsec());
    std::string messagetype("VncApiConfigLog");
    std::string xmlmessage = "<VncApiConfigLog type=\"sandesh\"><file type=\"string\" identifier=\"-32768\">src/analytics/test/viz_collector_test.cc</file><line type=\"i32\" identifier=\"-32767\">80</line><f1 type=\"struct\" identifier=\"1\"><SAT2_struct><f1 type=\"string\" identifier=\"1\">sat2string101</f1><f2 type=\"i32\" identifier=\"2\">101</f2></SAT2_struct></f1><f2 type=\"i32\" identifier=\"2\">101</f2></VncApiConfigLog>";

    SandeshXMLMessageTest *msg = dynamic_cast<SandeshXMLMessageTest *>(
        builder_->Create(
            reinterpret_cast<const uint8_t *>(xmlmessage.c_str()),
            xmlmessage.size()));
    msg->SetHeader(hdr);
    boost::uuids::uuid unm(rgen_());
    VizMsg vmsgp(msg, unm);

    GenDb::DbDataValueVec rowkey;
    rowkey.push_back(unm);

    int ttl = ttl_map.find(TtlType::CONFIGAUDIT_TTL)->second*3600;
    boost::ptr_vector<GenDb::NewCol> msg_table_expected_vector =
        boost::assign::ptr_list_of<GenDb::NewCol>
        (GenDb::NewCol(g_viz_constants.SOURCE, hdr.get_Source(), ttl))
        (GenDb::NewCol(g_viz_constants.NAMESPACE, std::string(), ttl))
        (GenDb::NewCol(g_viz_constants.MODULE, hdr.get_Module(), ttl))
        (GenDb::NewCol(g_viz_constants.INSTANCE_ID, hdr.get_InstanceId(), ttl))
        (GenDb::NewCol(g_viz_constants.NODE_TYPE, hdr.get_NodeType(), ttl))
        (GenDb::NewCol(g_viz_constants.TIMESTAMP,
            static_cast<uint64_t>(hdr.get_Timestamp()), ttl))
        (GenDb::NewCol(g_viz_constants.CATEGORY, std::string(), ttl))
        (GenDb::NewCol(g_viz_constants.LEVEL,
            static_cast<uint32_t>(0), ttl))
        (GenDb::NewCol(g_viz_constants.MESSAGE_TYPE, messagetype, ttl))
        (GenDb::NewCol(g_viz_constants.SEQUENCE_NUM,
            static_cast<uint32_t>(0), ttl))
        (GenDb::NewCol(g_viz_constants.VERSION,
            static_cast<uint32_t>(0), ttl))
        (GenDb::NewCol(g_viz_constants.SANDESH_TYPE,
            static_cast<uint8_t>(0), ttl))
        (GenDb::NewCol(g_viz_constants.DATA, xmlmessage, ttl));

    EXPECT_CALL(*dbif_mock(),
            Db_AddColumnProxy(
                Pointee(
                    AllOf(Field(&GenDb::ColList::cfname_, g_viz_constants.COLLECTOR_GLOBAL_TABLE),
                        Field(&GenDb::ColList::rowkey_, rowkey),
                        Field(&GenDb::ColList::columns_, msg_table_expected_vector)))))
        .Times(1)
        .WillOnce(Return(true));

    MessageTableOnlyInsert(&vmsgp);
    vmsgp.msg = NULL;
    delete msg;
}

TEST_F(DbHandlerTest, MessageIndexTableInsertTest) {
    SandeshHeader hdr;

    hdr.set_Source("127.0.0.1");
    hdr.set_Timestamp(UTCTimestampUsec());
    boost::uuids::uuid unm(rgen_());

    int ttl = ttl_map.find(TtlType::GLOBAL_TTL)->second*3600;

    DbDataValueVec *colname(new DbDataValueVec());
    colname->reserve(3);
    colname->push_back(hdr.get_Source());
    colname->push_back((uint32_t)(hdr.get_Timestamp() & g_viz_constants.RowTimeInMask));
    colname->push_back(unm);

    DbDataValueVec *colvalue(new DbDataValueVec());
    boost::ptr_vector<GenDb::NewCol> idx_expected_vector =
        boost::assign::ptr_list_of<GenDb::NewCol>
        (GenDb::NewCol(colname, colvalue, ttl));
    GenDb::DbDataValueVec src_idx_rowkey;
    src_idx_rowkey.push_back((uint32_t)(hdr.get_Timestamp() >> g_viz_constants.RowTimeInBits));
    uint8_t partition_no = 1;
    src_idx_rowkey.push_back(partition_no);
    EXPECT_CALL(*dbif_mock(),
            Db_AddColumnProxy(
                Pointee(
                    AllOf(Field(&GenDb::ColList::cfname_, g_viz_constants.MESSAGE_TABLE_SOURCE),
                        _,
                        Field(&GenDb::ColList::columns_,
                            idx_expected_vector)))))
        .Times(1)
        .WillOnce(Return(true));

    MessageIndexTableInsert(g_viz_constants.MESSAGE_TABLE_SOURCE,
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
    hdr.set_Type(SandeshType::SYSTEM);
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

    int ttl = ttl_map.find(TtlType::GLOBAL_TTL)->second*3600;
    boost::ptr_vector<GenDb::NewCol> msg_table_expected_vector =
        boost::assign::ptr_list_of<GenDb::NewCol>
        (GenDb::NewCol(g_viz_constants.SOURCE, hdr.get_Source(), ttl))
        (GenDb::NewCol(g_viz_constants.NAMESPACE, std::string(), ttl))
        (GenDb::NewCol(g_viz_constants.MODULE, hdr.get_Module(), ttl))
        (GenDb::NewCol(g_viz_constants.INSTANCE_ID, hdr.get_InstanceId(), ttl))
        (GenDb::NewCol(g_viz_constants.NODE_TYPE, hdr.get_NodeType(), ttl))
        (GenDb::NewCol(g_viz_constants.TIMESTAMP,
            static_cast<uint64_t>(hdr.get_Timestamp()), ttl))
        (GenDb::NewCol(g_viz_constants.CATEGORY, std::string(), ttl))
        (GenDb::NewCol(g_viz_constants.LEVEL,
            static_cast<uint32_t>(0), ttl))
        (GenDb::NewCol(g_viz_constants.MESSAGE_TYPE, messagetype, ttl))
        (GenDb::NewCol(g_viz_constants.SEQUENCE_NUM,
            static_cast<uint32_t>(0), ttl))
        (GenDb::NewCol(g_viz_constants.VERSION,
            static_cast<uint32_t>(0), ttl))
        (GenDb::NewCol(g_viz_constants.SANDESH_TYPE,
            static_cast<uint8_t>(SandeshType::SYSTEM), ttl))
        (GenDb::NewCol(g_viz_constants.DATA, xmlmessage, ttl));

    EXPECT_CALL(*dbif_mock(),
            Db_AddColumnProxy(
                Pointee(
                    AllOf(Field(&GenDb::ColList::cfname_,
                        g_viz_constants.COLLECTOR_GLOBAL_TABLE),
                        Field(&GenDb::ColList::rowkey_, rowkey),
                        Field(&GenDb::ColList::columns_,
                            msg_table_expected_vector)))))
        .Times(1)
        .WillOnce(Return(true));

    DbDataValueVec *colname(new DbDataValueVec());
    colname->reserve(3);
    colname->push_back(hdr.get_Source());
    colname->push_back((uint32_t)
        (hdr.get_Timestamp() & g_viz_constants.RowTimeInMask));
    colname->push_back(unm);

    DbDataValueVec *colvalue(new DbDataValueVec());
    boost::ptr_vector<GenDb::NewCol> idx_expected_vector =
        boost::assign::ptr_list_of<GenDb::NewCol>
        (GenDb::NewCol(colname, colvalue, ttl));

    EXPECT_CALL(*dbif_mock(),
            Db_AddColumnProxy(
                Pointee(
                    AllOf(Field(&GenDb::ColList::cfname_,
                              g_viz_constants.MESSAGE_TABLE_SOURCE),
                        _,
                        Field(&GenDb::ColList::columns_,
                            idx_expected_vector)))))
        .Times(1)
        .WillOnce(Return(true));


    DbDataValueVec *modcolname(new DbDataValueVec());
    modcolname->reserve(3);
    modcolname->push_back(hdr.get_Module());
    modcolname->push_back((uint32_t)
        (hdr.get_Timestamp() & g_viz_constants.RowTimeInMask));
    modcolname->push_back(unm);
    colvalue =new DbDataValueVec();
    boost::ptr_vector<GenDb::NewCol> mod_idx_expected_vector =
        boost::assign::ptr_list_of<GenDb::NewCol>
        (GenDb::NewCol(modcolname, colvalue, ttl));

    EXPECT_CALL(*dbif_mock(),
            Db_AddColumnProxy(
                Pointee(
                    AllOf(Field(&GenDb::ColList::cfname_,
                              g_viz_constants.MESSAGE_TABLE_MODULE_ID),
                        _,
                        Field(&GenDb::ColList::columns_,
                            mod_idx_expected_vector)))))
        .Times(1)
        .WillOnce(Return(true));

    DbDataValueVec *catcolname(new DbDataValueVec());
    catcolname->reserve(3);
    catcolname->push_back(hdr.get_Category());
    catcolname->push_back((uint32_t)
        (hdr.get_Timestamp() & g_viz_constants.RowTimeInMask));
    catcolname->push_back(unm);
    colvalue =new DbDataValueVec();
    boost::ptr_vector<GenDb::NewCol> cat_idx_expected_vector =
        boost::assign::ptr_list_of<GenDb::NewCol>
        (GenDb::NewCol(catcolname, colvalue, ttl));

    EXPECT_CALL(*dbif_mock(),
            Db_AddColumnProxy(
                Pointee(
                    AllOf(Field(&GenDb::ColList::cfname_,
                              g_viz_constants.MESSAGE_TABLE_CATEGORY),
                        _,
                        Field(&GenDb::ColList::columns_,
                            cat_idx_expected_vector)))))
        .Times(1)
        .WillOnce(Return(true));

    DbDataValueVec *msgtypecolname(new DbDataValueVec());
    msgtypecolname->reserve(3);
    msgtypecolname->push_back(messagetype);
    msgtypecolname->push_back((uint32_t)
        (hdr.get_Timestamp() & g_viz_constants.RowTimeInMask));
    msgtypecolname->push_back(unm);
    colvalue =new DbDataValueVec();
    boost::ptr_vector<GenDb::NewCol> msg_type_idx_expected_vector =
        boost::assign::ptr_list_of<GenDb::NewCol>
        (GenDb::NewCol(msgtypecolname, colvalue, ttl));

    EXPECT_CALL(*dbif_mock(),
            Db_AddColumnProxy(
                Pointee(
                    AllOf(Field(&GenDb::ColList::cfname_,
                              g_viz_constants.MESSAGE_TABLE_MESSAGE_TYPE),
                        _,
                        Field(&GenDb::ColList::columns_,
                            msg_type_idx_expected_vector)))))
        .Times(1)
        .WillOnce(Return(true));

    DbDataValueVec *ts_colname(new DbDataValueVec());
    ts_colname->reserve(2);
    ts_colname->push_back((uint32_t)
        (hdr.get_Timestamp() & g_viz_constants.RowTimeInMask));
    ts_colname->push_back(unm);
    colvalue =new DbDataValueVec();
    boost::ptr_vector<GenDb::NewCol> ts_idx_expected_vector =
        boost::assign::ptr_list_of<GenDb::NewCol>
        (GenDb::NewCol(ts_colname, colvalue, ttl));

    EXPECT_CALL(*dbif_mock(),
            Db_AddColumnProxy(
                Pointee(
                    AllOf(Field(&GenDb::ColList::cfname_,
                              g_viz_constants.MESSAGE_TABLE_TIMESTAMP),
                        _,
                        Field(&GenDb::ColList::columns_,
                            ts_idx_expected_vector)))))
        .Times(1)
        .WillOnce(Return(true));

    EXPECT_CALL(*dbif_mock(),
            Db_AddColumnProxy(
                Pointee(
                    AllOf(Field(&GenDb::ColList::cfname_,
                              g_viz_constants.MESSAGE_TABLE_KEYWORD),
                        _,
                        _))))
        .Times(3)
        .WillRepeatedly(Return(true));

    EXPECT_CALL(*dbif_mock(),
            Db_AddColumnProxy(
                Pointee(
                    AllOf(Field(&GenDb::ColList::cfname_,
                              g_viz_constants.STATS_TABLE_BY_STR_TAG),
                        _,
                        _))))
        .Times(6)
        .WillRepeatedly(Return(true));

    MessageTableInsert(&vmsgp);
    vmsgp.msg = NULL;
    delete msg;
}
//#endif

TEST_F(DbHandlerTest, ObjectTableInsertTest) {
    SandeshHeader hdr;
    hdr.set_Timestamp(UTCTimestampUsec());
    hdr.set_Source("127.0.0.1");
    uint64_t timestamp(hdr.get_Timestamp()); 
    boost::uuids::uuid unm(rgen_());
    std::string table("ObjectTableInsertTest");
    std::string rowkey_str("ObjectTableInsertTestRowkey");
    std::string xmlmessage = "<VNSwitchErrorMsgObject type=\"sandesh\"><length type=\"i32\">0000000020</length><field1 type=\"string\">field1_value</field1><field2 type=\"struct\"><field21 type=\"i16\">21</field21><field22 type=\"string\">string22</field22></field2><field3 type=\"i32\">3</field3></VNSwitchErrorMsgObject>";
    SandeshXMLMessageTest *msg = dynamic_cast<SandeshXMLMessageTest *>(
        builder_->Create(
            reinterpret_cast<const uint8_t *>(xmlmessage.c_str()),
            xmlmessage.size()));
    msg->SetHeader(hdr);
    VizMsg vmsgp(msg, unm);

    int ttl = ttl_map.find(TtlType::GLOBAL_TTL)->second*3600;
      {
        DbDataValueVec *colname(new DbDataValueVec());
        colname->reserve(2);
        colname->push_back("ObjectTableInsertTestRowkey");
        colname->push_back((uint32_t)(hdr.get_Timestamp() & g_viz_constants.RowTimeInMask));

        DbDataValueVec *colvalue(new DbDataValueVec(1, unm));
        boost::ptr_vector<GenDb::NewCol> expected_vector = 
            boost::assign::ptr_list_of<GenDb::NewCol>
            (GenDb::NewCol(colname, colvalue, ttl));

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
            (GenDb::NewCol(colname, colvalue, ttl));

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
            .Times(5)
            .WillRepeatedly(Return(true));
      }

      {
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
            .Times(5)
            .WillRepeatedly(Return(true));
      }

    ObjectTableInsert(table, rowkey_str, timestamp, unm, &vmsgp);
    vmsgp.msg = NULL;
    delete msg;
}

MATCHER_P(RowKeyEq, rowkey, "") {
    bool match(arg.size() == rowkey.size());
    if (!match) {
        *result_listener << "Row key size: actual: " << arg.size() <<
            ", expected: " << rowkey.size();
        return match;
    }
    for (size_t i = 0; i < arg.size(); i++) {
       // Don't compare the partition
       if (i == 1) {
           continue;
       }
       // boost::variant does not provide operator!=
       if (!(arg[i] == rowkey[i])) {
           *result_listener << "Row key element [" << i << "] actual:"
               << arg[i] << ", expected: " << rowkey[i];
           match = false;
       }
    }
    return match;
}

TEST_F(DbHandlerTest, FlowTableInsertTest) {
    init_vizd_tables();

    SandeshHeader hdr;
    hdr.set_Timestamp(UTCTimestampUsec());
    hdr.set_Module("VizdTest");
    hdr.set_Source("127.0.0.1");
    std::string messagetype("");
    std::vector<std::pair<std::string, std::vector<FlowLogData> > > flow_msgs;
    // Flow sandesh with single flow sample
    {
        std::string xmlmessage = "<FlowDataIpv4Object type=\"sandesh\"><flowdata type=\"struct\" identifier=\"1\"><FlowDataIpv4><flowuuid type=\"string\" identifier=\"1\">555788e0-513c-4351-8711-3fc481cf2eb4</flowuuid><direction_ing type=\"byte\" identifier=\"2\">0</direction_ing><sourcevn type=\"string\" identifier=\"3\">default-domain:demo:vn1</sourcevn><sourceip type=\"i32\" identifier=\"4\">-1062731011</sourceip><destvn type=\"string\" identifier=\"5\">default-domain:demo:vn0</destvn><destip type=\"i32\" identifier=\"6\">-1062731267</destip><protocol type=\"byte\" identifier=\"7\">6</protocol><sport type=\"i16\" identifier=\"8\">5201</sport><dport type=\"i16\" identifier=\"9\">-24590</dport><vm type=\"string\" identifier=\"12\">04430130-664a-4b89-9287-39d71f351207</vm><reverse_uuid type=\"string\" identifier=\"16\">58745ee7-d616-4e59-b8f7-96f896487c9f</reverse_uuid><bytes type=\"i64\" identifier=\"23\">0</bytes><packets type=\"i64\" identifier=\"24\">0</packets><diff_bytes type=\"i64\" identifier=\"26\">0</diff_bytes><diff_packets type=\"i64\" identifier=\"27\">0</diff_packets></FlowDataIpv4></flowdata></FlowDataIpv4Object>";

        std::vector<FlowLogData> flowdata_list;
        FlowLogData flow_data1;
        flow_data1.set_flowuuid("555788e0-513c-4351-8711-3fc481cf2eb4");
        flow_data1.set_direction_ing(0);
        flow_data1.set_sourcevn("default-domain:demo:vn1");
        flow_data1.set_sourceip(Ip4Address((uint32_t)-1062731011));
        flow_data1.set_destvn("default-domain:demo:vn0");
        flow_data1.set_destip(Ip4Address((uint32_t)-1062731267));
        flow_data1.set_protocol(6);
        flow_data1.set_sport(5201);
        flow_data1.set_dport(-24590);
        flow_data1.set_diff_bytes(0);
        flow_data1.set_diff_packets(0);
        flowdata_list.push_back(flow_data1);
        flow_msgs.push_back(std::make_pair(xmlmessage, flowdata_list));
    }

    // Flow sandesh with list of flow samples
    {
        std::string xmlmessage = "<FlowDataIpv4Object type=\"sandesh\"><flowdata type=\"list\" identifier=\"1\"><list type=\"struct\" size=\"2\"><FlowDataIpv4><flowuuid type=\"string\" identifier=\"1\">511788e0-513f-4351-8711-3fc481cf2efa</flowuuid><direction_ing type=\"byte\" identifier=\"2\">1</direction_ing><sourcevn type=\"string\" identifier=\"3\">default-domain:contrail:vn0</sourcevn><sourceip type=\"i32\" identifier=\"4\">168430081</sourceip><destvn type=\"string\" identifier=\"5\">default-domain:contrail:vn1</destvn><destip type=\"i32\" identifier=\"6\">-1062731263</destip><protocol type=\"byte\" identifier=\"7\">17</protocol><sport type=\"i16\" identifier=\"8\">12345</sport><dport type=\"i16\" identifier=\"9\">8087</dport><vm type=\"string\" identifier=\"12\">04430130-6641-1b89-9287-39d71f351206</vm><reverse_uuid type=\"string\" identifier=\"16\">58745ee6-d616-4e59-b8f7-96e896587c9f</reverse_uuid><bytes type=\"i64\" identifier=\"23\">1024</bytes><packets type=\"i64\" identifier=\"24\">4</packets><diff_bytes type=\"i64\" identifier=\"26\">256</diff_bytes><diff_packets type=\"i64\" identifier=\"27\">1</diff_packets></FlowDataIpv4><FlowDataIpv4><flowuuid type=\"string\" identifier=\"1\">525538ef-513f-435f-871f-3fc482cf2ebf</flowuuid><direction_ing type=\"byte\" identifier=\"2\">0</direction_ing><sourcevn type=\"string\" identifier=\"3\">default-domain:demo:vn0</sourcevn><sourceip type=\"i32\" identifier=\"4\">-1062731011</sourceip><destvn type=\"string\" identifier=\"5\">default-domain:contrail:vn0</destvn><destip type=\"i32\" identifier=\"6\">168430082</destip><protocol type=\"byte\" identifier=\"7\">6</protocol><sport type=\"i16\" identifier=\"8\">11221</sport><dport type=\"i16\" identifier=\"9\">8086</dport><vm type=\"string\" identifier=\"12\">04430130-664a-4b89-3456-39d71f351207</vm><reverse_uuid type=\"string\" identifier=\"16\">58745ee3-d613-4e53-b8f3-96f896487c93</reverse_uuid><bytes type=\"i64\" identifier=\"23\">512</bytes><packets type=\"i64\" identifier=\"24\">2</packets><diff_bytes type=\"i64\" identifier=\"26\">512</diff_bytes><diff_packets type=\"i64\" identifier=\"27\">2</diff_packets></FlowDataIpv4></list></flowdata></FlowDataIpv4Object>";

        std::vector<FlowLogData> flowdata_list;
        FlowLogData flow_data1;
        flow_data1.set_flowuuid("511788e0-513f-4351-8711-3fc481cf2efa");
        flow_data1.set_direction_ing(1);
        flow_data1.set_sourcevn("default-domain:contrail:vn0");
        flow_data1.set_sourceip(Ip4Address(168430081));
        flow_data1.set_destvn("default-domain:contrail:vn1");
        flow_data1.set_destip(Ip4Address((uint32_t)-1062731263));
        flow_data1.set_protocol(17);
        flow_data1.set_sport(12345);
        flow_data1.set_dport(8087);
        flow_data1.set_diff_bytes(256);
        flow_data1.set_diff_packets(1);
        flowdata_list.push_back(flow_data1);

        FlowLogData flow_data2;
        flow_data2.set_flowuuid("525538ef-513f-435f-871f-3fc482cf2ebf");
        flow_data2.set_direction_ing(0);
        flow_data2.set_sourcevn("default-domain:demo:vn0");
        flow_data2.set_sourceip(Ip4Address((uint32_t)-1062731011));
        flow_data2.set_destvn("default-domain:contrail:vn0");
        flow_data2.set_destip(Ip4Address(168430082));
        flow_data2.set_protocol(6);
        flow_data2.set_sport(11221);
        flow_data2.set_dport(8086);
        flow_data2.set_diff_bytes(512);
        flow_data2.set_diff_packets(2);
        flowdata_list.push_back(flow_data2);
        flow_msgs.push_back(std::make_pair(xmlmessage, flowdata_list));
    }

    // Flow sandesh with single flow sample -
    // Renamed "FlowDataIpv4Object" to "FlowLogDataObject"
    {
        std::string xmlmessage = "<FlowLogDataObject type=\"sandesh\"><flowdata type=\"struct\" identifier=\"1\"><FlowLogData><flowuuid type=\"string\" identifier=\"1\">555788e0-513c-4351-8711-3fc481cf2eb4</flowuuid><direction_ing type=\"byte\" identifier=\"2\">0</direction_ing><sourcevn type=\"string\" identifier=\"3\">default-domain:demo:vn1</sourcevn><sourceip type=\"i32\" identifier=\"4\">-1062731011</sourceip><destvn type=\"string\" identifier=\"5\">default-domain:demo:vn0</destvn><destip type=\"i32\" identifier=\"6\">-1062731267</destip><protocol type=\"byte\" identifier=\"7\">6</protocol><sport type=\"i16\" identifier=\"8\">5201</sport><dport type=\"i16\" identifier=\"9\">-24590</dport><vm type=\"string\" identifier=\"12\">04430130-664a-4b89-9287-39d71f351207</vm><reverse_uuid type=\"string\" identifier=\"16\">58745ee7-d616-4e59-b8f7-96f896487c9f</reverse_uuid><bytes type=\"i64\" identifier=\"23\">0</bytes><packets type=\"i64\" identifier=\"24\">0</packets><diff_bytes type=\"i64\" identifier=\"26\">0</diff_bytes><diff_packets type=\"i64\" identifier=\"27\">0</diff_packets></FlowLogData></flowdata></FlowLogDataObject>";

        std::vector<FlowLogData> flowdata_list;
        FlowLogData flow_data1;
        flow_data1.set_flowuuid("555788e0-513c-4351-8711-3fc481cf2eb4");
        flow_data1.set_direction_ing(0);
        flow_data1.set_sourcevn("default-domain:demo:vn1");
        flow_data1.set_sourceip(Ip4Address((uint32_t)-1062731011));
        flow_data1.set_destvn("default-domain:demo:vn0");
        flow_data1.set_destip(Ip4Address((uint32_t)-1062731267));
        flow_data1.set_protocol(6);
        flow_data1.set_sport(5201);
        flow_data1.set_dport(-24590);
        flow_data1.set_diff_bytes(0);
        flow_data1.set_diff_packets(0);
        flowdata_list.push_back(flow_data1);
        flow_msgs.push_back(std::make_pair(xmlmessage, flowdata_list));
    }

    // Flow sandesh with list of flow samples -
    // Renamed "FlowDataIpv4Object" to "FlowLogDataObject"
    {
        std::string xmlmessage = "<FlowLogDataObject type=\"sandesh\"><flowdata type=\"list\" identifier=\"1\"><list type=\"struct\" size=\"2\"><FlowLogData><flowuuid type=\"string\" identifier=\"1\">511788e0-513f-4351-8711-3fc481cf2efa</flowuuid><direction_ing type=\"byte\" identifier=\"2\">1</direction_ing><sourcevn type=\"string\" identifier=\"3\">default-domain:contrail:vn0</sourcevn><sourceip type=\"i32\" identifier=\"4\">168430081</sourceip><destvn type=\"string\" identifier=\"5\">default-domain:contrail:vn1</destvn><destip type=\"i32\" identifier=\"6\">-1062731263</destip><protocol type=\"byte\" identifier=\"7\">17</protocol><sport type=\"i16\" identifier=\"8\">12345</sport><dport type=\"i16\" identifier=\"9\">8087</dport><vm type=\"string\" identifier=\"12\">04430130-6641-1b89-9287-39d71f351206</vm><reverse_uuid type=\"string\" identifier=\"16\">58745ee6-d616-4e59-b8f7-96e896587c9f</reverse_uuid><bytes type=\"i64\" identifier=\"23\">1024</bytes><packets type=\"i64\" identifier=\"24\">4</packets><diff_bytes type=\"i64\" identifier=\"26\">256</diff_bytes><diff_packets type=\"i64\" identifier=\"27\">1</diff_packets></FlowLogData><FlowLogData><flowuuid type=\"string\" identifier=\"1\">525538ef-513f-435f-871f-3fc482cf2ebf</flowuuid><direction_ing type=\"byte\" identifier=\"2\">0</direction_ing><sourcevn type=\"string\" identifier=\"3\">default-domain:demo:vn0</sourcevn><sourceip type=\"i32\" identifier=\"4\">-1062731011</sourceip><destvn type=\"string\" identifier=\"5\">default-domain:contrail:vn0</destvn><destip type=\"i32\" identifier=\"6\">168430082</destip><protocol type=\"byte\" identifier=\"7\">6</protocol><sport type=\"i16\" identifier=\"8\">11221</sport><dport type=\"i16\" identifier=\"9\">8086</dport><vm type=\"string\" identifier=\"12\">04430130-664a-4b89-3456-39d71f351207</vm><reverse_uuid type=\"string\" identifier=\"16\">58745ee3-d613-4e53-b8f3-96f896487c93</reverse_uuid><bytes type=\"i64\" identifier=\"23\">512</bytes><packets type=\"i64\" identifier=\"24\">2</packets><diff_bytes type=\"i64\" identifier=\"26\">512</diff_bytes><diff_packets type=\"i64\" identifier=\"27\">2</diff_packets></FlowLogData></list></flowdata></FlowLogDataObject>";

        std::vector<FlowLogData> flowdata_list;
        FlowLogData flow_data1;
        flow_data1.set_flowuuid("511788e0-513f-4351-8711-3fc481cf2efa");
        flow_data1.set_direction_ing(1);
        flow_data1.set_sourcevn("default-domain:contrail:vn0");
        flow_data1.set_sourceip(Ip4Address(168430081));
        flow_data1.set_destvn("default-domain:contrail:vn1");
        flow_data1.set_destip(Ip4Address((uint32_t)-1062731263));
        flow_data1.set_protocol(17);
        flow_data1.set_sport(12345);
        flow_data1.set_dport(8087);
        flow_data1.set_diff_bytes(256);
        flow_data1.set_diff_packets(1);
        flowdata_list.push_back(flow_data1);

        FlowLogData flow_data2;
        flow_data2.set_flowuuid("525538ef-513f-435f-871f-3fc482cf2ebf");
        flow_data2.set_direction_ing(0);
        flow_data2.set_sourcevn("default-domain:demo:vn0");
        flow_data2.set_sourceip(Ip4Address((uint32_t)-1062731011));
        flow_data2.set_destvn("default-domain:contrail:vn0");
        flow_data2.set_destip(Ip4Address(168430082));
        flow_data2.set_protocol(6);
        flow_data2.set_sport(11221);
        flow_data2.set_dport(8086);
        flow_data2.set_diff_bytes(512);
        flow_data2.set_diff_packets(2);
        flowdata_list.push_back(flow_data2);
        flow_msgs.push_back(std::make_pair(xmlmessage, flowdata_list));
    }

    // FieldNames will be call 7 times each for FlowTable and FlowSeriesTable
    // This dataset has 3 unique svn, 3 unique dvn, and a single vrouter
    {
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
			    Field(&GenDb::ColList::rowkey_, RowKeyEq(rowkey)),_))))
	    .Times(14)
	    .WillRepeatedly(Return(true));
    }
    {
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
			    Field(&GenDb::ColList::rowkey_, RowKeyEq(rowkey)),_))))
	    .Times(14)
	    .WillRepeatedly(Return(true));
    }

    std::vector<std::pair<std::string, std::vector<FlowLogData> > >::
        const_iterator fit;
    for (fit = flow_msgs.begin(); fit != flow_msgs.end(); fit++) {
        std::auto_ptr<SandeshXMLMessageTest> msg(
            dynamic_cast<SandeshXMLMessageTest *>(
                builder_->Create(reinterpret_cast<const uint8_t *>(
                    fit->first.c_str()), fit->first.size())));
        msg->SetHeader(hdr);
        std::vector<FlowLogData>::const_iterator dit;
        for (dit = fit->second.begin(); dit != fit->second.end(); dit++) {
            boost::uuids::uuid flowu = StringToUuid(dit->get_flowuuid());
            // set expectations for FLOW_TABLE
            {
                GenDb::DbDataValueVec rowkey;
                rowkey.push_back(flowu);

                EXPECT_CALL(*dbif_mock(),
                    Db_AddColumnProxy(
                        Pointee(
                            AllOf(Field(&GenDb::ColList::cfname_,
                                        g_viz_constants.FLOW_TABLE),
                                Field(&GenDb::ColList::rowkey_, RowKeyEq(rowkey))))))
                    .Times(1)
                    .WillOnce(Return(true));
            }

            int ttl = ttl_map.find(TtlType::FLOWDATA_TTL)->second*3600;
            GenDb::DbDataValueVec ocolvalue;
            std::ostringstream cv_ss;
            const std::vector<std::string> &frnames(
                g_viz_constants.FlowRecordNames);
            cv_ss << "{";
            cv_ss << "\"" << frnames[FlowRecordFields::FLOWREC_DIFF_BYTES]
                << "\":" << (uint64_t)dit->get_diff_bytes() << ",";
            cv_ss << "\"" << frnames[FlowRecordFields::FLOWREC_DIFF_PACKETS]
                << "\":" << (uint64_t)dit->get_diff_packets() << ",";
            cv_ss << "\"" << frnames[FlowRecordFields::FLOWREC_SHORT_FLOW]
                << "\":" << 0 << ",";
            cv_ss << "\"" << frnames[FlowRecordFields::FLOWREC_FLOWUUID]
                << "\":\"" << to_string(flowu) << "\",";
            cv_ss << "\"" << frnames[FlowRecordFields::FLOWREC_VROUTER]
                << "\":\"" << hdr.get_Source() << "\",";
            cv_ss << "\"" << frnames[FlowRecordFields::FLOWREC_SOURCEVN]
                << "\":\"" << dit->get_sourcevn() << "\",";
            cv_ss << "\"" << frnames[FlowRecordFields::FLOWREC_DESTVN]
                << "\":\"" << dit->get_destvn() << "\",";
            cv_ss << "\"" << frnames[FlowRecordFields::FLOWREC_SOURCEIP]
                << "\":\"" << dit->get_sourceip() << "\",";
            cv_ss << "\"" << frnames[FlowRecordFields::FLOWREC_DESTIP]
                << "\":\"" << dit->get_destip() << "\",";
            cv_ss << "\"" << frnames[FlowRecordFields::FLOWREC_PROTOCOL]
                << "\":" << (uint16_t)dit->get_protocol() << ",";
            cv_ss << "\"" << frnames[FlowRecordFields::FLOWREC_SPORT]
                << "\":" << (uint16_t)dit->get_sport() << ",";
            cv_ss << "\"" << frnames[FlowRecordFields::FLOWREC_DPORT]
                << "\":" << (uint16_t)dit->get_dport() << ",";
            cv_ss << "\"" << frnames[FlowRecordFields::FLOWREC_JSON]
                << "\":" << "\"\"";
            cv_ss << "}";
            std::string ocolvalue_s(cv_ss.str());
            ocolvalue.push_back(ocolvalue_s);
            // set expectations for FLOW_TABLE_SVN_SIP
            {
                GenDb::DbDataValueVec *colname(new GenDb::DbDataValueVec);
                colname->reserve(4);
                colname->push_back(dit->get_sourcevn());
                colname->push_back(dit->get_sourceip());
                colname->push_back((uint32_t)(hdr.get_Timestamp() &
                                              g_viz_constants.RowTimeInMask));
                colname->push_back(flowu);
                GenDb::DbDataValueVec *colvalue(
                    new GenDb::DbDataValueVec(ocolvalue));
                boost::ptr_vector<GenDb::NewCol> expected_vector =
                    boost::assign::ptr_list_of<GenDb::NewCol>
                    (GenDb::NewCol(colname, colvalue, ttl));

                GenDb::DbDataValueVec rowkey;
                rowkey.push_back((uint32_t)(hdr.get_Timestamp() >>
                                            g_viz_constants.RowTimeInBits));
                uint8_t partition_no = 0;
                rowkey.push_back(partition_no);
                rowkey.push_back((uint8_t)dit->get_direction_ing());

                EXPECT_CALL(*dbif_mock(),
                    Db_AddColumnProxy(
                        Pointee(
                            AllOf(Field(&GenDb::ColList::cfname_,
                                        g_viz_constants.FLOW_TABLE_SVN_SIP),
                                Field(&GenDb::ColList::rowkey_, RowKeyEq(rowkey)),
                                Field(&GenDb::ColList::columns_,
                                      expected_vector)))))
                    .Times(1)
                    .WillOnce(Return(true));
            }

            // set expectations for FLOW_TABLE_DVN_DIP
            {
                GenDb::DbDataValueVec *colname(new GenDb::DbDataValueVec);
                colname->reserve(4);
                colname->push_back(dit->get_destvn());
                colname->push_back(dit->get_destip());
                colname->push_back((uint32_t)(hdr.get_Timestamp() &
                                              g_viz_constants.RowTimeInMask));
                colname->push_back(flowu);
                GenDb::DbDataValueVec *colvalue(
                    new GenDb::DbDataValueVec(ocolvalue));
                boost::ptr_vector<GenDb::NewCol> expected_vector =
                    boost::assign::ptr_list_of<GenDb::NewCol>
                    (GenDb::NewCol(colname, colvalue, ttl));

                GenDb::DbDataValueVec rowkey;
                rowkey.push_back((uint32_t)(hdr.get_Timestamp() >>
                                            g_viz_constants.RowTimeInBits));
                uint8_t partition_no = 0;
                rowkey.push_back(partition_no);
                rowkey.push_back((uint8_t)dit->get_direction_ing());

                EXPECT_CALL(*dbif_mock(),
                    Db_AddColumnProxy(
                        Pointee(
                            AllOf(Field(&GenDb::ColList::cfname_,
                                        g_viz_constants.FLOW_TABLE_DVN_DIP),
                                Field(&GenDb::ColList::rowkey_, RowKeyEq(rowkey)),
                                Field(&GenDb::ColList::columns_,
                                      expected_vector)))))
                    .Times(1)
                    .WillOnce(Return(true));
            }

            // set expectations for FLOW_TABLE_PROT_SP
            {
                GenDb::DbDataValueVec *colname(new GenDb::DbDataValueVec);
                colname->reserve(4);
                colname->push_back((uint8_t)dit->get_protocol());
                colname->push_back((uint16_t)dit->get_sport());
                colname->push_back((uint32_t)(hdr.get_Timestamp() &
                                              g_viz_constants.RowTimeInMask));
                colname->push_back(flowu);
                GenDb::DbDataValueVec *colvalue(
                    new GenDb::DbDataValueVec(ocolvalue));
                boost::ptr_vector<GenDb::NewCol> expected_vector =
                    boost::assign::ptr_list_of<GenDb::NewCol>
                    (GenDb::NewCol(colname, colvalue, ttl));

                GenDb::DbDataValueVec rowkey;
                rowkey.push_back((uint32_t)(hdr.get_Timestamp() >>
                                            g_viz_constants.RowTimeInBits));
                uint8_t partition_no = 0;
                rowkey.push_back(partition_no);
                rowkey.push_back((uint8_t)dit->get_direction_ing());

                EXPECT_CALL(*dbif_mock(),
                    Db_AddColumnProxy(
                        Pointee(
                            AllOf(Field(&GenDb::ColList::cfname_,
                                        g_viz_constants.FLOW_TABLE_PROT_SP),
                                Field(&GenDb::ColList::rowkey_, RowKeyEq(rowkey)),
                                Field(&GenDb::ColList::columns_,
                                      expected_vector)))))
                    .Times(1)
                    .WillOnce(Return(true));
            }

            // set expectations for FLOW_TABLE_PROT_DP
            {
                GenDb::DbDataValueVec *colname(new GenDb::DbDataValueVec);
                colname->reserve(4);
                colname->push_back((uint8_t)dit->get_protocol());
                colname->push_back((uint16_t)dit->get_dport());
                colname->push_back((uint32_t)(hdr.get_Timestamp() &
                                              g_viz_constants.RowTimeInMask));
                colname->push_back(flowu);
                GenDb::DbDataValueVec *colvalue(
                    new GenDb::DbDataValueVec(ocolvalue));
                boost::ptr_vector<GenDb::NewCol> expected_vector =
                    boost::assign::ptr_list_of<GenDb::NewCol>
                    (GenDb::NewCol(colname, colvalue, ttl));

                GenDb::DbDataValueVec rowkey;
                rowkey.push_back((uint32_t)(hdr.get_Timestamp() >>
                                            g_viz_constants.RowTimeInBits));
                uint8_t partition_no = 0;
                rowkey.push_back(partition_no);
                rowkey.push_back((uint8_t)dit->get_direction_ing());

                EXPECT_CALL(*dbif_mock(),
                    Db_AddColumnProxy(
                        Pointee(
                            AllOf(Field(&GenDb::ColList::cfname_,
                                        g_viz_constants.FLOW_TABLE_PROT_DP),
                                Field(&GenDb::ColList::rowkey_, RowKeyEq(rowkey)),
                                Field(&GenDb::ColList::columns_,
                                      expected_vector)))))
                    .Times(1)
                    .WillOnce(Return(true));
            }

            // set expectations for FLOW_TABLE_VROUTER
            {
                GenDb::DbDataValueVec *colname(new GenDb::DbDataValueVec);
                colname->reserve(4);
                colname->push_back(hdr.get_Source()); //vrouter
                colname->push_back((uint32_t)(hdr.get_Timestamp() &
                                              g_viz_constants.RowTimeInMask));
                colname->push_back(flowu);
                GenDb::DbDataValueVec *colvalue(
                    new GenDb::DbDataValueVec(ocolvalue));
                boost::ptr_vector<GenDb::NewCol> expected_vector =
                    boost::assign::ptr_list_of<GenDb::NewCol>
                    (GenDb::NewCol(colname, colvalue, ttl));

                GenDb::DbDataValueVec rowkey;
                rowkey.push_back((uint32_t)(hdr.get_Timestamp() >>
                                            g_viz_constants.RowTimeInBits));
                uint8_t partition_no = 0;
                rowkey.push_back(partition_no);
                rowkey.push_back((uint8_t)dit->get_direction_ing());

                EXPECT_CALL(*dbif_mock(),
                    Db_AddColumnProxy(
                        Pointee(
                            AllOf(Field(&GenDb::ColList::cfname_,
                                        g_viz_constants.FLOW_TABLE_VROUTER),
                                Field(&GenDb::ColList::rowkey_, RowKeyEq(rowkey)),
                                Field(&GenDb::ColList::columns_,
                                      expected_vector)))))
                    .Times(1)
                    .WillOnce(Return(true));
            }
        }

        FlowTableInsert(msg->GetMessageNode(), msg->GetHeader());
    }
}

TEST_F(DbHandlerTest, CanRecordDataForT2Test) {
    /* start w/ some random number*/
    uint32_t t2 = UTCTimestampUsec() >> g_viz_constants.RowTimeInBits;
    uint32_t t2_index = t2 >> g_viz_constants.CacheTimeInAdditionalBits;

    std::string fc_entry("tabname:vn1");
    bool ret = WriteToCache(t2, fc_entry);
    struct DbHandlerCacheParam cache_param = GetDbHandlerCacheParam();
    // All the field_cache_t2 should be updated
    EXPECT_EQ(t2_index, cache_param.field_cache_index_);
    std::set<std::string>  new_test_cache;
    new_test_cache.insert(fc_entry);
    EXPECT_THAT(cache_param.field_cache_set_, ::testing::Contains(fc_entry));
    EXPECT_EQ(true, ret);

    fc_entry = "tabname:vn2";
    ret = WriteToCache(t2, fc_entry);
    cache_param = GetDbHandlerCacheParam();
    EXPECT_EQ(t2_index, cache_param.field_cache_index_);
    new_test_cache.insert(fc_entry);
    EXPECT_THAT(cache_param.field_cache_set_, ::testing::Contains(fc_entry));
    EXPECT_EQ(true, ret);

    // add same entry for same t2, expect ret false
    fc_entry = "tabname:vn2";
    ret = WriteToCache(t2, fc_entry);
    cache_param = GetDbHandlerCacheParam();
    EXPECT_EQ(t2_index, cache_param.field_cache_index_);
    EXPECT_THAT(cache_param.field_cache_set_, ::testing::Contains(fc_entry));
    EXPECT_EQ(false, ret);

    // New entry with t2+1 should return true
    fc_entry="tabname:vn3";
    ret = WriteToCache(t2+1, fc_entry);
    new_test_cache.insert(fc_entry);
    cache_param = GetDbHandlerCacheParam();
    EXPECT_EQ(t2_index, cache_param.field_cache_index_);
    EXPECT_THAT(cache_param.field_cache_set_, ::testing::Contains(fc_entry));
    EXPECT_EQ(true, ret);

    // same entry with t2+2 should return false
    fc_entry="tabname:vn3";
    ret = WriteToCache(t2+2, fc_entry);
    new_test_cache.insert(fc_entry);
    cache_param = GetDbHandlerCacheParam();
    EXPECT_THAT(cache_param.field_cache_set_, ::testing::Contains(fc_entry));
    EXPECT_EQ(false, ret);

    // New entry with t2>field_cache_t2_ with existing value should return true
    LOG(ERROR, "t2 " << t2 << "  t2_index " << t2_index);
    t2 += (1<<g_viz_constants.CacheTimeInAdditionalBits) + 1;
    t2_index = (t2>>g_viz_constants.CacheTimeInAdditionalBits);
    LOG(ERROR, "t2 " << t2 << "  t2_index " << t2_index);
    fc_entry = "tabname:vn3";
    ret = WriteToCache(t2, fc_entry);
    cache_param = GetDbHandlerCacheParam();
    EXPECT_EQ(t2_index, cache_param.field_cache_index_);
    EXPECT_THAT(cache_param.field_cache_set_, ::testing::Contains(fc_entry));
    EXPECT_EQ(true, ret);
}

class FlowTableTest: public ::testing::Test {
};

static const std::vector<FlowRecordFields::type> FlowIndexTableColumnValues =
    boost::assign::list_of
    (FlowRecordFields::FLOWREC_DIFF_BYTES)
    (FlowRecordFields::FLOWREC_DIFF_PACKETS)
    (FlowRecordFields::FLOWREC_SHORT_FLOW)
    (FlowRecordFields::FLOWREC_FLOWUUID)
    (FlowRecordFields::FLOWREC_VROUTER)
    (FlowRecordFields::FLOWREC_SOURCEVN)
    (FlowRecordFields::FLOWREC_DESTVN)
    (FlowRecordFields::FLOWREC_SOURCEIP)
    (FlowRecordFields::FLOWREC_DESTIP)
    (FlowRecordFields::FLOWREC_PROTOCOL)
    (FlowRecordFields::FLOWREC_SPORT)
    (FlowRecordFields::FLOWREC_DPORT)
    (FlowRecordFields::FLOWREC_JSON);

static const uint64_t diff_bytes_(123456789);
static const uint64_t diff_packets_(512);
static const uint8_t short_flow_(1);
static const boost::uuids::uuid flow_uuid_ = boost::uuids::random_generator()();
static const std::string flow_uuid_s_(to_string(flow_uuid_));
static const std::string vrouter_("VRouter");
static const std::string source_vn_("SourceVN");
static const std::string dest_vn_("DestVN");
static const std::string source_ip_("1.1.1.1");
static const std::string dest_ip_("2.2.2.2");
static const uint8_t protocol_(6);
static const uint16_t sport_(65535);
static const uint16_t dport_(80);
static const std::string json_("");

static const GenDb::DbDataValueVec FlowIndexTableColumnDbValues =
    boost::assign::list_of
    (GenDb::DbDataValue(diff_bytes_))
    (GenDb::DbDataValue(diff_packets_))
    (GenDb::DbDataValue(short_flow_))
    (GenDb::DbDataValue(flow_uuid_))
    (GenDb::DbDataValue(vrouter_))
    (GenDb::DbDataValue(source_vn_))
    (GenDb::DbDataValue(dest_vn_))
    (GenDb::DbDataValue(source_ip_))
    (GenDb::DbDataValue(dest_ip_))
    (GenDb::DbDataValue(protocol_))
    (GenDb::DbDataValue(sport_))
    (GenDb::DbDataValue(dport_))
    (GenDb::DbDataValue(json_));

static const std::vector<std::string> FlowIndexTableColumnDbValuesJson =
    boost::assign::list_of
    (integerToString(diff_bytes_))
    (integerToString(diff_packets_))
    (integerToString(short_flow_))
    ("\"" + flow_uuid_s_ + "\"")
    ("\"" + vrouter_ + "\"")
    ("\"" + source_vn_ + "\"")
    ("\"" + dest_vn_ + "\"")
    ("\"" + source_ip_ + "\"")
    ("\"" + dest_ip_ + "\"")
    (integerToString(protocol_))
    (integerToString(sport_))
    (integerToString(dport_))
    ("\"" + json_ + "\"");

TEST_F(FlowTableTest, ColumnValues) {
    FlowValueArray fvalues;
    EXPECT_EQ(FlowIndexTableColumnValues.size(),
        FlowIndexTableColumnDbValues.size());
    EXPECT_EQ(FlowIndexTableColumnDbValues.size(),
        FlowIndexTableColumnDbValuesJson.size());
    for (int i = 0; i < (int)FlowIndexTableColumnValues.size(); i++) {
        fvalues[FlowIndexTableColumnValues[i]] =
            FlowIndexTableColumnDbValues[i];
    }
    GenDb::DbDataValueVec actual_db_values;
    PopulateFlowIndexTableColumnValues(FlowIndexTableColumnValues, fvalues,
        &actual_db_values, -1, NULL);
    EXPECT_EQ(1, actual_db_values.size());
    std::ostringstream expected_ss;
    expected_ss << "{";
    for (int i = 0; i < (int)FlowIndexTableColumnDbValuesJson.size(); i++) {
        if (i) {
            expected_ss << ",";
        }
        expected_ss << "\"" <<
            g_viz_constants.FlowRecordNames[FlowIndexTableColumnValues[i]] <<
            "\":" << FlowIndexTableColumnDbValuesJson[i];
    }
    expected_ss << "}";
    EXPECT_EQ(GenDb::DB_VALUE_STRING, actual_db_values[0].which());
    std::ostringstream actual_ss;
    actual_ss << actual_db_values[0];
    EXPECT_EQ(expected_ss.str(), actual_ss.str());
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

//
// Disable the UUID random generator tests. The tests were used to
// verify the need for thread safety for boost::uuids::random_generator
// and take lot of execution time.
//
TEST_F(UUIDRandomGenTest, DISABLED_SingleThread) {
    unsigned int count = 10000;
    std::map<std::string, unsigned int> uuid_map;
    bool unique = PopulateUUIDMap(uuid_map, count, false, false);
    EXPECT_TRUE(unique);
}

TEST_F(UUIDRandomGenTest, DISABLED_SingleThreadOnStack) {
    unsigned int count = 10000;
    std::map<std::string, unsigned int> uuid_map;
    bool unique = PopulateUUIDMap(uuid_map, count, false, true);
    EXPECT_TRUE(unique);
}

TEST_F(UUIDRandomGenTest, DISABLED_MultiThreadedLocked) {
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

