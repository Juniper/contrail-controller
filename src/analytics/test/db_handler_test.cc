/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <pthread.h>

#include <boost/bind.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/assign/ptr_list_of.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/foreach.hpp>

#include <testing/gunit.h>
#include <base/logging.h>
#include <sandesh/sandesh_message_builder.h>
#include <sandesh/common/flow_types.h>

#include <analytics/viz_types.h>
#include <analytics/viz_constants.h>
#include <analytics/db_handler.h>
#include <analytics/db_handler_impl.h>
#include <analytics/vizd_table_desc.h>
#include <analytics/usrdef_counters.h>

#include <analytics/test/cql_if_mock.h>
#include <analytics/test/usrdef_counters_mock.h>

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
    SandeshMessageBuilder *builder_;
    boost::uuids::random_generator rgen_;
    DbHandler::ObjectNamesVec object_names_;

    void DbAddColumnCbFn(bool success) {
        assert(success);
    }

    void MessageTableOnlyInsert(const VizMsg *vmsgp) {
        db_handler()->MessageTableOnlyInsert(vmsgp, object_names_,
            boost::bind(&DbHandlerTest::DbAddColumnCbFn, this, _1));
    }

    void MessageTableInsert(const VizMsg *vmsgp) {
        db_handler()->MessageTableInsert(vmsgp, object_names_,
            boost::bind(&DbHandlerTest::DbAddColumnCbFn, this, _1));
    }

    void ObjectTableInsert(const std::string &table,
        const std::string &rowkey_str, uint64_t timestamp,
        const boost::uuids::uuid& unm, const VizMsg *vmsgp) {
        db_handler()->ObjectTableInsert(table, rowkey_str, timestamp,
            unm, vmsgp,
            boost::bind(&DbHandlerTest::DbAddColumnCbFn, this, _1));
    }

    void SessionTableInsert(const pugi::xml_node& parent,
        const SandeshHeader &header) {
        db_handler()->SessionTableInsert(parent, header,
            boost::bind(&DbHandlerTest::DbAddColumnCbFn, this, _1));
    }

    boost::shared_ptr<GenDb::ColList> MessageTablePrepareMsg(
                                            SandeshXMLMessageTest *msg,
                                            SandeshHeader &hdr,
                                            boost::uuids::uuid &unm,
                                            std::string messagetype,
                                            int ttl);

private:
    CqlIfMock *dbif_mock_;
    DbHandlerPtr db_handler_;
    DbHandlerCacheParam db_handler_cache_param_;
};

boost::shared_ptr<GenDb::ColList> DbHandlerTest::MessageTablePrepareMsg(
                                                        SandeshXMLMessageTest *msg,
                                                        SandeshHeader &hdr,
                                                        boost::uuids::uuid &unm,
                                                        std::string messagetype,
                                                        int ttl) {

    boost::shared_ptr<GenDb::ColList> col_list(new GenDb::ColList);
    col_list->cfname_ = g_viz_constants.COLLECTOR_GLOBAL_TABLE;
    uint64_t timestamp = hdr.get_Timestamp();
    uint32_t T2(timestamp >> g_viz_constants.RowTimeInBits);
    uint32_t T1(timestamp & g_viz_constants.RowTimeInMask);
    boost::system::error_code ec;

    // Rowkey
    GenDb::DbDataValueVec& rowkey = col_list->rowkey_;
    rowkey.reserve(2);
    rowkey.push_back(T2);
    rowkey.push_back(static_cast<uint32_t>(0));

    // Columns
    GenDb::DbDataValueVec *col_name(new GenDb::DbDataValueVec());
    col_name->reserve(20);
    col_name->push_back(T1);
    col_name->push_back(unm);

    // Prepend T2: to secondary index columns
    col_name->push_back(PrependT2(T2, hdr.get_Source()));
    col_name->push_back(PrependT2(T2, messagetype));
    col_name->push_back(PrependT2(T2, hdr.get_Module()));
    object_names_.clear();
    object_names_.push_back("ObjectCollectorInfo:host01");
    object_names_.push_back("ObjectCollectorInfo:host02");
    BOOST_FOREACH(const std::string &object_name, object_names_) {
        col_name->push_back(PrependT2(T2, object_name));
    }

    // Set the value as BLANK for remaining entries if
    // object_names_.size() < MSG_TABLE_MAX_OBJECTS_PER_MSG
    for (int i = object_names_.size();
         i < g_viz_constants.MSG_TABLE_MAX_OBJECTS_PER_MSG; i++) {
        col_name->push_back(GenDb::DbDataValue());
    }
    if (hdr.__isset.IPAddress) {
        boost::system::error_code ec;
        IpAddress ipaddr(IpAddress::from_string(hdr.get_IPAddress(), ec));
        col_name->push_back(ipaddr);
    } else {
        col_name->push_back(GenDb::DbDataValue());
    }
    if (hdr.__isset.Pid) {
        col_name->push_back((uint32_t)hdr.get_Pid());
    } else {
        col_name->push_back(GenDb::DbDataValue());
    }
    col_name->push_back(hdr.get_Category());
    col_name->push_back((uint32_t)hdr.get_Level());
    col_name->push_back(hdr.get_NodeType());
    col_name->push_back(hdr.get_InstanceId());
    col_name->push_back((uint32_t)hdr.get_SequenceNum());
    col_name->push_back((uint8_t)hdr.get_Type());
    GenDb::DbDataValueVec *col_value(new GenDb::DbDataValueVec(1,
        msg->ExtractMessage()));
    GenDb::NewCol *col(new GenDb::NewCol(col_name, col_value, ttl));
    GenDb::NewColVec& columns = col_list->columns_;
    columns.reserve(1);
    columns.push_back(col);
    return col_list;
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

SandeshXMLMessageTestBuilder
    SandeshXMLMessageTestBuilder::instance_;


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

    int ttl = ttl_map.find(TtlType::GLOBAL_TTL)->second*3600;
    boost::shared_ptr<GenDb::ColList> col_list
            = MessageTablePrepareMsg(msg, hdr, unm, messagetype, ttl);
    EXPECT_CALL(*dbif_mock(),
            Db_AddColumnProxy(
                Pointee(
                    AllOf(Field(&GenDb::ColList::cfname_, col_list->cfname_),
                        Field(&GenDb::ColList::rowkey_, RowKeyEq(col_list->rowkey_)),
                        Field(&GenDb::ColList::columns_, col_list->columns_)))))
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

    int ttl = ttl_map.find(TtlType::CONFIGAUDIT_TTL)->second*3600;
    boost::shared_ptr<GenDb::ColList> col_list
            = MessageTablePrepareMsg(msg, hdr, unm, messagetype, ttl);
    EXPECT_CALL(*dbif_mock(),
            Db_AddColumnProxy(
                Pointee(
                    AllOf(Field(&GenDb::ColList::cfname_, col_list->cfname_),
                        Field(&GenDb::ColList::rowkey_, RowKeyEq(col_list->rowkey_)),
                        Field(&GenDb::ColList::columns_, col_list->columns_)))))
        .Times(1)
        .WillOnce(Return(true));

    MessageTableOnlyInsert(&vmsgp);
    vmsgp.msg = NULL;
    delete msg;
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

    int ttl = ttl_map.find(TtlType::GLOBAL_TTL)->second*3600;
    boost::shared_ptr<GenDb::ColList> col_list
            = MessageTablePrepareMsg(msg, hdr, unm, messagetype, ttl);
    EXPECT_CALL(*dbif_mock(),
            Db_AddColumnProxy(
                Pointee(
                    AllOf(Field(&GenDb::ColList::cfname_, col_list->cfname_),
                        Field(&GenDb::ColList::rowkey_, RowKeyEq(col_list->rowkey_)),
                        Field(&GenDb::ColList::columns_, col_list->columns_)))))
        .Times(1)
        .WillOnce(Return(true));

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

MATCHER_P(ColumnMatcherEq, ecolumns, "") {
    bool match(arg.size() == ecolumns.size());
    if (!match) {
        *result_listener << "Column size: actual: " << arg.size() <<
            ", expected: " << ecolumns.size();
        return match;
    }

    GenDb::DbDataValueVec* acol = arg[0].name.get();
    GenDb::DbDataValueVec* ecol = ecolumns[0].name.get();

    match = acol->size() == ecol->size();

    if (!match) {
        *result_listener << "colname size: actual: " << acol->size() <<
            ", expected: " << ecol->size();
        return match;
    }
    for (size_t i = 0; i < acol->size(); i++) {
        // skip uuid
        if (i == 3) {
            continue;
        }
        // boost::variant does not provide operator!=
        if (!(acol->at(i) == ecol->at(i))) {
            *result_listener << "colname element [" << i << "] actual:"
                << acol->at(i) << ", expected: " << ecol->at(i);
            return false;
        }
    }

    GenDb::DbDataValueVec* aval = arg[0].value.get();
    GenDb::DbDataValueVec* eval = ecolumns[0].value.get();

    match = aval->size() == eval->size();
    return match;
}


TEST_F(DbHandlerTest, SessionTableInsertTest) {
    init_vizd_tables();
    SandeshHeader hdr;

    hdr.set_Timestamp(UTCTimestampUsec());
    hdr.set_Module("VizdTest");
    hdr.set_Source("127.0.0.1");
    std::string messagetype("");
    std::vector<std::pair<std::string, std::vector<SessionEndpoint> > >
        session_msgs;

    {
        std::string xmlstring = "<SessionEndpointObject type=\"sandesh\"><session_data type=\"list\"><list type=\"struct\" size=\"1\"><SessionEndpoint><vmi type=\"string\" identifier=\"1\">4618cc76-789f-446c-897a-88803a5ed1c7</vmi><vn type=\"string\" identifier=\"2\">default-domain:mock-gen-test:vn0</vn><deployment type=\"string\">Dep1</deployment><tier type=\"string\">Tier3</tier><application type=\"string\">App1</application><site type=\"string\">Site1</site><labels type=\"set\"><set><element>Blue</element><element>Red</element><element>Yellow</element></set></labels><remote_deployment type=\"string\">Dep3</remote_deployment><remote_tier type=\"string\">Tier4</remote_tier><remote_application type=\"string\">App3</remote_application><remote_site type=\"string\">Site4</remote_site><remote_vn type=\"string\">default-domain:mock-gen-test:vn50</remote_vn><is_client_session type=\"bool\">false</is_client_session><is_si type=\"byte\">1</is_si><vrouter_ip type=\"string\">1.0.0.81</vrouter_ip><sess_agg_info type=\"map\"><map key=\"struct\" value=\"struct\" size=\"2\"><SessionIpPortProtocol><ip type=\"ipaddr\">1.0.0.1</ip><port type=\"u16\">443</port><protocol type=\"u16\">6</protocol></SessionIpPortProtocol><SessionAggInfo><sampled_forward_bytes type=\"i64\">33327</sampled_forward_bytes><sampled_forward_pkts type=\"i64\">69</sampled_forward_pkts><sampled_reverse_bytes type=\"i64\">34646</sampled_reverse_bytes><sampled_reverse_pkts type=\"i64\">98</sampled_reverse_pkts><sessionMap type=\"map\"><map key=\"struct\" value=\"struct\" size=\"1\"><SessionIpPort><ip type=\"ipaddr\">1.0.0.51</ip><port type=\"u16\">45085</port></SessionIpPort><SessionInfo><forward_flow_info type=\"struct\" identifier=\"1\"><SessionFlowInfo><sampled_bytes type=\"i64\">33327</sampled_bytes><sampled_pkts type=\"i64\">69</sampled_pkts></SessionFlowInfo></forward_flow_info><reverse_flow_info type=\"struct\" identifier=\"2\"><SessionFlowInfo><sampled_bytes type=\"i64\">34646</sampled_bytes><sampled_pkts type=\"i64\">34</sampled_pkts></SessionFlowInfo></reverse_flow_info></SessionInfo></map></sessionMap></SessionAggInfo><SessionIpPortProtocol><ip type=\"ipaddr\">1.0.0.1</ip><port type=\"u16\">8080</port><protocol type=\"u16\">17</protocol></SessionIpPortProtocol><SessionAggInfo><sampled_forward_bytes type=\"i64\">53994</sampled_forward_bytes><sampled_forward_pkts type=\"i64\">145</sampled_forward_pkts><sampled_reverse_bytes type=\"i64\">34465</sampled_reverse_bytes><sampled_reverse_pkts type=\"i64\">173</sampled_reverse_pkts><sessionMap type=\"map\"><map key=\"struct\" value=\"struct\" size=\"2\"><SessionIpPort><ip type=\"ipaddr\">1.0.0.52</ip><port type=\"u16\">29552</port></SessionIpPort><SessionInfo><forward_flow_info type=\"struct\" identifier=\"1\"><SessionFlowInfo><sampled_bytes type=\"i64\">33375</sampled_bytes><sampled_pkts type=\"i64\">75</sampled_pkts></SessionFlowInfo></forward_flow_info><reverse_flow_info type=\"struct\" identifier=\"2\"><SessionFlowInfo><sampled_bytes type=\"i64\">20619</sampled_bytes><sampled_pkts type=\"i64\">87</sampled_pkts></SessionFlowInfo></reverse_flow_info></SessionInfo><SessionIpPort><ip type=\"ipaddr\">1.0.0.51</ip><port type=\"u16\">40132</port></SessionIpPort><SessionInfo><forward_flow_info type=\"struct\" identifer=\"1\"><SessionFlowInfo><sampled_bytes type=\"i64\">21210</sampled_bytes><sampled_pkts type=\"i64\">70</sampled_pkts></SessionFlowInfo></forward_flow_info><reverse_flow_info type=\"struct\" identifier=\"2\"><SessionFlowInfo><sampled_bytes type=\"i64\">13846</sampled_bytes><sampled_pkts type=\"i64\">86</sampled_pkts></SessionFlowInfo></reverse_flow_info></SessionInfo></map></sessionMap></SessionAggInfo></map></sess_agg_info></SessionEndpoint></list></session_data></SessionEndpointObject>";

        std::vector<SessionEndpoint> session_list;
        SessionEndpoint end_point;
        end_point.set_vmi(
            "4618cc76-789f-446c-897a-88803a5ed1c7");
        end_point.set_vn("default-domain:mock-gen-test:vn0");
        end_point.set_remote_vn("default-domain:mock-gen-test:vn50");
        end_point.set_deployment("Dep1");
        end_point.set_tier("Tier3");
        end_point.set_application("App1");
        end_point.set_site("Site1");
        std::vector<std::string> labels;
        labels.push_back("Blue");
        labels.push_back("Red");
        labels.push_back("Yellow");
        end_point.set_remote_deployment("Dep3");
        end_point.set_remote_tier("Tier4");
        end_point.set_remote_application("App3");
        end_point.set_remote_site("Site4");
        end_point.set_is_client_session(false);
        end_point.set_is_si(1);
        end_point.set_vrouter_ip(Ip4Address::from_string("1.0.0.81"));
        std::map<SessionIpPortProtocol, SessionAggInfo> sess_agg_map;
        {
            SessionAggInfo session_agg_info;
            {
                session_agg_info.set_sampled_forward_bytes(33327);
                session_agg_info.set_sampled_forward_pkts(69);
                session_agg_info.set_sampled_reverse_bytes(34646);
                session_agg_info.set_sampled_reverse_pkts(98);
                std::map<SessionIpPort, SessionInfo> session_map;
                {
                    SessionInfo session_val;
                    SessionFlowInfo forward_flow_info;
                    SessionFlowInfo reverse_flow_info;
                    forward_flow_info.set_sampled_bytes(33327);
                    forward_flow_info.set_sampled_pkts(69);
                    reverse_flow_info.set_sampled_bytes(34646);
                    reverse_flow_info.set_sampled_pkts(98);
                    session_val.set_forward_flow_info(forward_flow_info);
                    session_val.set_reverse_flow_info(reverse_flow_info);
                    SessionIpPort sess_ip_port;
                    sess_ip_port.set_ip(Ip4Address::from_string("1.0.0.51"));
                    sess_ip_port.set_port(45085);
                    session_map.insert(std::make_pair(sess_ip_port, session_val));
                }
                session_agg_info.set_sessionMap(session_map);
            }
            SessionIpPortProtocol sess_ip_port_proto;
            sess_ip_port_proto.set_ip(Ip4Address::from_string("1.0.0.1"));
            sess_ip_port_proto.set_port(443);
            sess_ip_port_proto.set_protocol(6);
            sess_agg_map.insert(std::make_pair(sess_ip_port_proto, session_agg_info));
        }
        {
            SessionAggInfo session_agg_info;
            {
                session_agg_info.set_sampled_forward_bytes(53994);
                session_agg_info.set_sampled_forward_pkts(145);
                session_agg_info.set_sampled_reverse_bytes(34465);
                session_agg_info.set_sampled_reverse_pkts(173);
                std::map<SessionIpPort, SessionInfo> session_map;
                {
                    SessionInfo session_val;
                    SessionFlowInfo forward_flow_info;
                    SessionFlowInfo reverse_flow_info;
                    forward_flow_info.set_sampled_bytes(33375);
                    forward_flow_info.set_sampled_pkts(75);
                    reverse_flow_info.set_sampled_bytes(20619);
                    reverse_flow_info.set_sampled_pkts(87);
                    session_val.set_forward_flow_info(forward_flow_info);
                    session_val.set_reverse_flow_info(reverse_flow_info);
                    SessionIpPort sess_ip_port;
                    sess_ip_port.set_ip(Ip4Address::from_string("1.0.0.52"));
                    sess_ip_port.set_port(29552);
                    session_map.insert(std::make_pair(sess_ip_port, session_val));
                }
                {
                    SessionInfo session_val;
                    SessionFlowInfo forward_flow_info;
                    SessionFlowInfo reverse_flow_info;
                    forward_flow_info.set_sampled_bytes(21210);
                    forward_flow_info.set_sampled_pkts(70);
                    reverse_flow_info.set_sampled_bytes(13846);
                    reverse_flow_info.set_sampled_pkts(86);
                    session_val.set_forward_flow_info(forward_flow_info);
                    session_val.set_reverse_flow_info(reverse_flow_info);
                    SessionIpPort sess_ip_port;
                    sess_ip_port.set_ip(Ip4Address::from_string("1.0.0.51"));
                    sess_ip_port.set_port(40132);
                    session_map.insert(std::make_pair(sess_ip_port, session_val));

                }
                session_agg_info.set_sessionMap(session_map);
            }
            SessionIpPortProtocol sess_ip_port_proto;
            sess_ip_port_proto.set_ip(Ip4Address::from_string("1.0.0.1"));
            sess_ip_port_proto.set_port(8080);
            sess_ip_port_proto.set_protocol(17);
            sess_agg_map.insert(std::make_pair(sess_ip_port_proto, session_agg_info));
        }
        end_point.set_sess_agg_info(sess_agg_map);
        session_list.push_back(end_point);
        session_msgs.push_back(std::make_pair(xmlstring, session_list));
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
                Field(&GenDb::ColList::rowkey_, RowKeyEq(rowkey)),_))))
        .Times(3)
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
        .Times(3)
        .WillRepeatedly(Return(true));
    }

    std::vector<std::pair<std::string, std::vector<SessionEndpoint> > >::
        const_iterator sit;
    for (sit = session_msgs.begin(); sit != session_msgs.end(); sit++) {
        std::auto_ptr<SandeshXMLMessageTest> msg(
            dynamic_cast<SandeshXMLMessageTest *>(
                builder_->Create(reinterpret_cast<const uint8_t *>(
                    sit->first.c_str()), sit->first.size())));
        msg->SetHeader(hdr);
        std::vector<SessionEndpoint>::const_iterator dit;
        for (dit = sit->second.begin(); dit != sit->second.end(); dit++) {

           int ttl = ttl_map.find(TtlType::FLOWDATA_TTL)->second*3600;

           {
                GenDb::DbDataValueVec rowkey;
                std::string t2 = integerToString(
                    (hdr.get_Timestamp() >> g_viz_constants.RowTimeInBits));
                rowkey.push_back((uint32_t)(hdr.get_Timestamp() >>
                    g_viz_constants.RowTimeInBits));
                rowkey.push_back((uint32_t)0);
                rowkey.push_back((uint8_t)dit->get_is_si());
                rowkey.push_back((uint8_t)dit->get_is_client_session());

                GenDb::DbDataValueVec* colname(new GenDb::DbDataValueVec());
                colname->reserve(21);
                colname->push_back((uint16_t)(6));//protocol
                colname->push_back((uint16_t)(443));  // server-port
                colname->push_back((uint32_t)(hdr.get_Timestamp()
                    & g_viz_constants.RowTimeInMask));
                colname->push_back(boost::uuids::random_generator()()); // uuid
                colname->push_back(t2 + ":Dep1");
                colname->push_back(t2 + ":Tier3");
                colname->push_back(t2 + ":App1");
                colname->push_back(t2 + ":Site1");
                colname->push_back("Blue;Red;Yellow");
                colname->push_back(t2 + ":Dep3");
                colname->push_back(t2 + ":Tier4");
                colname->push_back(t2 + ":App3");
                colname->push_back(t2 + ":Site4");
                colname->push_back("");
                colname->push_back(t2 + ":" + g_viz_constants.UNKNOWN);
                colname->push_back(t2 + ":" + g_viz_constants.UNKNOWN);
                colname->push_back(t2 + ":" + dit->get_vmi());
                colname->push_back(t2 + ":1.0.0.1"); //local-ip
                colname->push_back(t2 + ":" + dit->get_vn());
                colname->push_back(t2 + ":" + dit->get_remote_vn());
                colname->push_back("127.0.0.1");
                colname->push_back(dit->get_vrouter_ip());
                colname->push_back((uint64_t)33327);
                colname->push_back((uint64_t)69);
                colname->push_back((uint64_t)34646);
                colname->push_back((uint64_t)98);
                colname->push_back((uint64_t)0);
                colname->push_back((uint64_t)0);
                colname->push_back((uint64_t)0);
                colname->push_back((uint64_t)0);

                GenDb::DbDataValueVec* colvalue(new GenDb::DbDataValueVec());
                colvalue->reserve(1);
                colvalue->push_back("");

                boost::ptr_vector<GenDb::NewCol> ecolumns =
                        boost::assign::ptr_list_of<GenDb::NewCol>
                        (GenDb::NewCol(colname, colvalue, ttl));

                std::string cfname(g_viz_constants.SESSION_TABLE);

                EXPECT_CALL(*dbif_mock(),
                        Db_AddColumnProxy(
                            Pointee(
                                AllOf(Field(&GenDb::ColList::cfname_,
                                            cfname),
                                    Field(&GenDb::ColList::rowkey_,
                                        RowKeyEq(rowkey)),
                                    Field(&GenDb::ColList::columns_,
                                        ColumnMatcherEq(ecolumns))))))
                        .Times(1)
                        .WillOnce(Return(true));
            }
            {

                GenDb::DbDataValueVec rowkey;
                std::string t2 = integerToString(
                    (hdr.get_Timestamp() >> g_viz_constants.RowTimeInBits));
                rowkey.push_back((uint32_t)(hdr.get_Timestamp() >>
                    g_viz_constants.RowTimeInBits));
                rowkey.push_back((uint32_t)0);
                rowkey.push_back((uint8_t)dit->get_is_si());
                rowkey.push_back((uint8_t)dit->get_is_client_session());

                GenDb::DbDataValueVec* colname(new GenDb::DbDataValueVec());
                colname->reserve(21);
                colname->push_back((uint16_t)(17));//protocol
                colname->push_back((uint16_t)(8080));  // server-port
                colname->push_back((uint32_t)(hdr.get_Timestamp()
                    & g_viz_constants.RowTimeInMask));
                colname->push_back(boost::uuids::random_generator()()); // uuid
                colname->push_back(t2 + ":Dep1");
                colname->push_back(t2 + ":Tier3");
                colname->push_back(t2 + ":App1");
                colname->push_back(t2 + ":Site1");
                colname->push_back("Blue;Red;Yellow");
                colname->push_back(t2 + ":Dep3");
                colname->push_back(t2 + ":Tier4");
                colname->push_back(t2 + ":App3");
                colname->push_back(t2 + ":Site4");
                colname->push_back("");
                colname->push_back(t2 + ":" + g_viz_constants.UNKNOWN);
                colname->push_back(t2 + ":" + g_viz_constants.UNKNOWN);
                colname->push_back(t2 + ":" + dit->get_vmi());
                colname->push_back(t2 + ":1.0.0.1"); //local-ip
                colname->push_back(t2 + ":" + dit->get_vn());
                colname->push_back(t2 + ":" + dit->get_remote_vn());
                colname->push_back("127.0.0.1");
                colname->push_back(dit->get_vrouter_ip());
                colname->push_back((uint64_t)53994);
                colname->push_back((uint64_t)145);
                colname->push_back((uint64_t)34465);
                colname->push_back((uint64_t)173);
                colname->push_back((uint64_t)0);
                colname->push_back((uint64_t)0);
                colname->push_back((uint64_t)0);
                colname->push_back((uint64_t)0);

                GenDb::DbDataValueVec* colvalue(new GenDb::DbDataValueVec());
                colvalue->reserve(1);
                colvalue->push_back("");

                boost::ptr_vector<GenDb::NewCol> expected_columns =
                        boost::assign::ptr_list_of<GenDb::NewCol>
                        (GenDb::NewCol(colname, colvalue, ttl));

                std::string cfname(g_viz_constants.SESSION_TABLE);

                EXPECT_CALL(*dbif_mock(),
                        Db_AddColumnProxy(
                            Pointee(
                                AllOf(Field(&GenDb::ColList::cfname_,
                                            cfname),
                                    Field(&GenDb::ColList::rowkey_,
                                        RowKeyEq(rowkey)),
                                    Field(&GenDb::ColList::columns_,
                                        ColumnMatcherEq(expected_columns))))))
                        .Times(1)
                        .WillOnce(Return(true));
            }

        }
        SessionTableInsert(msg->GetMessageNode(),msg->GetHeader());
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
    EXPECT_LE(t2_index, cache_param.field_cache_index_);
    EXPECT_THAT(cache_param.field_cache_set_, ::testing::Contains(fc_entry));
    EXPECT_EQ(true, ret);

    // same entry with t2+1 should return false
    fc_entry="tabname:vn3";
    ret = WriteToCache(t2+1, fc_entry);
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

