/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#include <fstream>

#include <boost/assign/list_of.hpp>

#include <testing/gunit.h>

#include <google/protobuf/descriptor.h>
#include <google/protobuf/descriptor.pb.h>
#include <google/protobuf/message.h>
#include <google/protobuf/dynamic_message.h>

#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh.h>

#include <base/logging.h>
#include <base/test/task_test_util.h>
#include <io/test/event_manager_test.h>
#include <io/io_types.h>
#include <io/udp_server.h>

#include "analytics/db_handler.h"
#include "analytics/jti-protos/telemetry_top.pb.h"
#include "analytics/protobuf_server_impl.h"
#include "analytics/protobuf_server.h"
#include "test_message.pb.h"

using namespace ::google::protobuf;
using boost::assign::map_list_of;

static const int kTestMessageBufferSize = 1024;
static const int kTelemetryStreamBufferSize = 8 * 1024;
static const int kTestMessageSizeBufferSize = 5 * 1024;
static const int kTelemetryStreamSizeBufferSize = 16 * 1024;

namespace {

class MockProtobufReader : public protobuf::impl::ProtobufReader {
 protected:
    virtual const Message* GetPrototype(const Descriptor *mdesc) {
        return NULL;
    }
};

class ProtobufReaderTest : public ::testing::Test {
};

// Verify the content and structure of Test Message
template<typename MessageT, typename InnerMessageT>
bool VerifyTestMessage(const Message *msg, const Descriptor *mdesc) {
    // Get the descriptors for the fields we're interested in and verify
    // their types.
    const FieldDescriptor* tm_name_field = mdesc->FindFieldByName("tm_name");
    EXPECT_TRUE(tm_name_field != NULL);
    EXPECT_TRUE(tm_name_field->type() == FieldDescriptor::TYPE_STRING);
    EXPECT_TRUE(tm_name_field->number() == 1);
    const FieldDescriptor* tm_status_field =
        mdesc->FindFieldByName("tm_status");
    EXPECT_TRUE(tm_status_field != NULL);
    EXPECT_TRUE(tm_status_field->type() == FieldDescriptor::TYPE_STRING);
    EXPECT_TRUE(tm_status_field->number() == 2);
    const FieldDescriptor* tm_counter_field =
        mdesc->FindFieldByName("tm_counter");
    EXPECT_TRUE(tm_counter_field != NULL);
    EXPECT_TRUE(tm_counter_field->type() == FieldDescriptor::TYPE_INT32);
    EXPECT_TRUE(tm_counter_field->number() == 3);
    const FieldDescriptor* tm_enum_field =
        mdesc->FindFieldByName("tm_enum");
    EXPECT_TRUE(tm_enum_field != NULL);
    EXPECT_TRUE(tm_enum_field->type() == FieldDescriptor::TYPE_ENUM);
    EXPECT_TRUE(tm_enum_field->number() == 5);
    
    // Inner Message
    const FieldDescriptor* tm_inner_field = mdesc->FindFieldByName("tm_inner");
    EXPECT_TRUE(tm_inner_field != NULL);
    EXPECT_TRUE(tm_inner_field->type() == FieldDescriptor::TYPE_MESSAGE);
    EXPECT_TRUE(tm_inner_field->label() == FieldDescriptor::LABEL_REPEATED);
    EXPECT_TRUE(tm_inner_field->number() == 4);
    const Descriptor *imdesc = tm_inner_field->message_type();
    EXPECT_TRUE(imdesc != NULL);
    const FieldDescriptor* tm_inner_name_field =
        imdesc->FindFieldByName("tm_inner_name");
    EXPECT_TRUE(tm_inner_name_field != NULL);
    EXPECT_TRUE(tm_inner_name_field->type() == FieldDescriptor::TYPE_STRING);
    EXPECT_TRUE(tm_inner_name_field->number() == 1);
    const FieldDescriptor* tm_inner_status_field =
        imdesc->FindFieldByName("tm_inner_status");
    EXPECT_TRUE(tm_inner_status_field != NULL);
    EXPECT_TRUE(tm_inner_status_field->type() == FieldDescriptor::TYPE_INT32);
    EXPECT_TRUE(tm_inner_status_field->number() == 2);
    const FieldDescriptor* tm_inner_counter_field =
        imdesc->FindFieldByName("tm_inner_counter");
    EXPECT_TRUE(tm_inner_counter_field != NULL);
    EXPECT_TRUE(tm_inner_counter_field->type() == FieldDescriptor::TYPE_INT32);
    EXPECT_TRUE(tm_inner_counter_field->number() == 3);
    const FieldDescriptor* tm_inner_enum_field =
        imdesc->FindFieldByName("tm_inner_enum");
    EXPECT_TRUE(tm_inner_enum_field != NULL);
    EXPECT_TRUE(tm_inner_enum_field->type() == FieldDescriptor::TYPE_ENUM);
    EXPECT_TRUE(tm_inner_enum_field->number() == 4);
    
    // Use the reflection interface to examine the contents.
    // Ensure that value is same as the set in PopulateTestMessage()
    const Reflection* reflection = msg->GetReflection();
    EXPECT_TRUE(reflection->GetString(*msg, tm_name_field) ==
        TYPE_NAME(MessageT));
    EXPECT_TRUE(reflection->GetString(*msg, tm_status_field) == "Test");
    EXPECT_TRUE(reflection->GetInt32(*msg, tm_counter_field) == 3);
    const EnumValueDescriptor *edesc(reflection->GetEnum(*msg, tm_enum_field));
    EXPECT_TRUE(edesc != NULL);
    EXPECT_STREQ(edesc->name().c_str(), "GOOD");
    EXPECT_EQ(typename MessageT::TestMessageEnum(edesc->number()),
        MessageT::GOOD);
    EXPECT_TRUE(reflection->FieldSize(*msg, tm_inner_field) == 2);
    
    return true;
}

// Populate the test message
template<typename MessageT, typename InnerMessageT>
void PopulateTestMessage(MessageT *test_message) {
    test_message->set_tm_name(TYPE_NAME(MessageT));
    test_message->set_tm_status("Test");
    test_message->set_tm_counter(3);
    test_message->set_tm_enum(MessageT::GOOD);
    
    // Add element to tm_inner list
    InnerMessageT *test_message_inner = test_message->add_tm_inner();
    ASSERT_TRUE(test_message_inner != NULL);
    test_message_inner->set_tm_inner_name(TYPE_NAME(InnerMessageT) + "1");
    test_message_inner->set_tm_inner_status(1);
    test_message_inner->set_tm_inner_counter(1);
    test_message_inner->set_tm_inner_enum(MessageT::GOOD);
    
    // Add element to tm_inner list
    test_message_inner = test_message->add_tm_inner();
    test_message_inner->set_tm_inner_name(TYPE_NAME(InnerMessageT) + "2");
    test_message_inner->set_tm_inner_status(2);
    test_message_inner->set_tm_inner_counter(2);
    test_message_inner->set_tm_inner_enum(MessageT::BAD);
}

// Create TestMessage and serialize it
void CreateAndSerializeTestMessage(uint8_t *output, size_t size,
    int *serialized_size) {
    TestMessage test_message;
   
    // Populate the members in TestMessage Instance
    PopulateTestMessage<TestMessage, TestMessageInner>(&test_message);
    int test_message_size = test_message.ByteSize();
    ASSERT_GE(size, test_message_size);
    *serialized_size = test_message_size;
    
    bool success = test_message.SerializeToArray(output, test_message_size);
    ASSERT_TRUE(success);
}

void PopulateTestMessageAllTypesInner(TestMessageAllTypesInner *inner_msg) {
    inner_msg->set_tmp_inner_string("TestMessageAllTypesInner");
    inner_msg->set_tmp_inner_int32(std::numeric_limits<int32_t>::min());
    inner_msg->set_tmp_inner_int64(std::numeric_limits<int64_t>::min());
    inner_msg->set_tmp_inner_uint32(std::numeric_limits<uint32_t>::min());
    inner_msg->set_tmp_inner_uint64(std::numeric_limits<uint64_t>::min());
    inner_msg->set_tmp_inner_float(std::numeric_limits<float>::min());
    inner_msg->set_tmp_inner_double(std::numeric_limits<double>::min());
    inner_msg->set_tmp_inner_bool(false);
    inner_msg->set_tmp_inner_enum(TestMessageAllTypes::BAD);
}

// Create TestMessageAllTypes and serialize it
void CreateAndSerializeTestMessageAllTypes(uint8_t *output, size_t size,
    int *serialized_size) {
    TestMessageAllTypes test_message;
    test_message.set_tmp_string("TestMessageAllTypes");
    test_message.set_tmp_int32(std::numeric_limits<int32_t>::max());
    test_message.set_tmp_int64(std::numeric_limits<int64_t>::max());
    test_message.set_tmp_uint32(std::numeric_limits<uint32_t>::max());
    test_message.set_tmp_uint64(std::numeric_limits<uint64_t>::max());
    test_message.set_tmp_float(std::numeric_limits<float>::max());
    test_message.set_tmp_double(std::numeric_limits<double>::max());
    test_message.set_tmp_bool(true);
    test_message.set_tmp_enum(TestMessageAllTypes::GOOD);
    TestMessageAllTypesInner *test_message_inner =
        test_message.add_tmp_inner();
    ASSERT_TRUE(test_message_inner != NULL);
    PopulateTestMessageAllTypesInner(test_message_inner);
    TestMessageAllTypesInner *test_message_inner1 =
        test_message.mutable_tmp_message();
    ASSERT_TRUE(test_message_inner1 != NULL);
    PopulateTestMessageAllTypesInner(test_message_inner1);

    int test_message_size = test_message.ByteSize();
    ASSERT_GE(size, test_message_size);
    *serialized_size = test_message_size;
    bool success = test_message.SerializeToArray(output, test_message_size);
    ASSERT_TRUE(success);
}

// Create TestMessageSize and serialize it
void CreateAndSerializeTestMessageSize(uint8_t *output, size_t size,
    int *serialized_size, int test_message_data_size) {
    TestMessageSize test_message;
    boost::scoped_array<uint8_t> tms_data(new uint8_t[test_message_data_size]);
    test_message.set_tms_data(tms_data.get(), test_message_data_size);

    int test_message_size = test_message.ByteSize();
    ASSERT_GE(size, test_message_size);
    *serialized_size = test_message_size;
    bool success = test_message.SerializeToArray(output, test_message_size);
    ASSERT_TRUE(success);
}

void CreateAndSerializeTelemetryStreamMessageInternal(
    const std::string &message_name, uint8_t *output, size_t size,
    int *serialized_size, uint8_t *message_data, size_t message_data_size) {
    TelemetryStream tstream;
    tstream.set_timestamp(123456789);
    tstream.set_system_id("protobuf_test");
    if (message_data[0] == 0) {
    } else if (message_name.compare("TestMessage") == 0) {
	ASSERT_TRUE(tstream.mutable_enterprise()
		    ->MutableExtension(juniperNetworks)->MutableExtension(tm_simple)
		    ->ParseFromArray(message_data, message_data_size));
    } else if (message_name.compare("TestMessageBase") == 0) {
	ASSERT_TRUE(tstream.mutable_enterprise()
		    ->MutableExtension(juniperNetworks)->MutableExtension(tm_base)
		    ->ParseFromArray(message_data, message_data_size));
    } else if (message_name.compare("TestMessageBaseStream") == 0) {
	ASSERT_TRUE(tstream.mutable_enterprise()
		    ->MutableExtension(juniperNetworks)
		    ->MutableExtension(tm_base_stream)
		    ->ParseFromArray(message_data, message_data_size));
    } else if (message_name.compare("TestMessageAllTypes") == 0) {
	ASSERT_TRUE(tstream.mutable_enterprise()
		    ->MutableExtension(juniperNetworks)->MutableExtension(tm_all_types)
		    ->ParseFromArray(message_data, message_data_size));
    } else if (message_name.compare("TestMessageSize") == 0) { 
	ASSERT_TRUE(tstream.mutable_enterprise()
		    ->MutableExtension(juniperNetworks)->MutableExtension(tm_size)
		    ->ParseFromArray(message_data, message_data_size));
    }
    int tstream_size = tstream.ByteSize();
    ASSERT_GE(size, tstream_size);
    *serialized_size = tstream_size;
    bool success = tstream.SerializeToArray(output, tstream_size);
    ASSERT_TRUE(success);
}

// Create TelemetryStream for message_name and serialize it
void CreateAndSerializeTelemetryStreamMessage(const std::string &message_name,
    uint8_t *output, size_t size, int *serialized_size, 
    uint8_t *message_data, size_t message_data_size) {
    CreateAndSerializeTelemetryStreamMessageInternal(message_name, output,
						     size, serialized_size,
						     message_data, 
						     message_data_size);
}

static void ProtobufReaderTestParseFailure(const std::string &message) {
}

TEST_F(ProtobufReaderTest, ParseFail) {
    google::protobuf::SetLogHandler(&protobuf::impl::ProtobufLibraryLog);
    
    // Create TestMessage and serialize it
    boost::scoped_array<uint8_t> data(new uint8_t[kTestMessageBufferSize]);
    int serialized_data_size(0);
    CreateAndSerializeTestMessage(data.get(), kTestMessageBufferSize,
        &serialized_data_size);
    
    // Failure 1
    // Create TelemetryStream for TestMessage and serialize it
    boost::scoped_array<uint8_t> sdm_data(
				new uint8_t[kTelemetryStreamBufferSize]);
    int serialized_sdm_data_size(0);
    // Change message name to fail parsing
    CreateAndSerializeTelemetryStreamMessage("TestMessageFail", sdm_data.get(),
	kTelemetryStreamBufferSize, &serialized_sdm_data_size,
        data.get(), (size_t) serialized_data_size);
    // Parse the TelemetryStream from sdm_data to get TestMessage
    protobuf::impl::ProtobufReader reader;
    std::string message_name;
    Message *msg = NULL;
    uint64_t timestamp;
    TelemetryStream tstream;
    bool success = reader.ParseTelemetryStreamMessage(&tstream, sdm_data.get(),
	serialized_sdm_data_size, &timestamp, &message_name, &msg,
        ProtobufReaderTestParseFailure);
    ASSERT_FALSE(success);
    ASSERT_TRUE(msg == NULL);
    
    // Failure 2 
    // Create TelemetryStream for TestMessage and serialize it
    sdm_data.reset(new uint8_t[kTelemetryStreamBufferSize]);
    serialized_sdm_data_size = 0;
    CreateAndSerializeTelemetryStreamMessage("TestMessage", sdm_data.get(),
	kTelemetryStreamBufferSize, &serialized_sdm_data_size,
        data.get(), (size_t) serialized_data_size);
    // Change one byte in the serialized TelemetryStream to fail parsing
    uint8_t *sdm_data_start(sdm_data.get());
    *sdm_data_start = 0;
    success = reader.ParseTelemetryStreamMessage(&tstream, sdm_data.get(),
        serialized_sdm_data_size, &timestamp, &message_name, &msg,
        ProtobufReaderTestParseFailure);
    ASSERT_FALSE(success);
    ASSERT_TRUE(msg == NULL);
    
    // Failure 3
    // Create TelemetryStream for TestMessage and serialize it
    sdm_data.reset(new uint8_t[kTelemetryStreamBufferSize]);
    serialized_sdm_data_size = 0;
    // Change one byte in the serialized TestMessage to fail parsing
    uint8_t *data_start(data.get());
    *data_start = 0;
    CreateAndSerializeTelemetryStreamMessage("TestMessage", sdm_data.get(),
	kTelemetryStreamBufferSize, &serialized_sdm_data_size,
        data.get(), (size_t) serialized_data_size);
    success = reader.ParseTelemetryStreamMessage(&tstream, sdm_data.get(),
        serialized_sdm_data_size, &timestamp, &message_name, &msg,
        ProtobufReaderTestParseFailure);
    ASSERT_FALSE(success);
    ASSERT_TRUE(msg == NULL);
    
    // Failure 4
    // Create TestMessage and serialize it
    data.reset(new uint8_t[kTestMessageBufferSize]);
    serialized_data_size = 0;
    CreateAndSerializeTestMessage(data.get(), kTestMessageBufferSize,
        &serialized_data_size);
    // Create TelemetryStream for TestMessage and serialize it
    sdm_data.reset(new uint8_t[kTelemetryStreamBufferSize]);
    serialized_sdm_data_size = 0;
    CreateAndSerializeTelemetryStreamMessage("TestMessage", sdm_data.get(),
	kTelemetryStreamBufferSize, &serialized_sdm_data_size,
        data.get(), (size_t) serialized_data_size);
    
    // Override GetPrototype to return NULL prototype message
    MockProtobufReader mreader;
    success = mreader.ParseTelemetryStreamMessage(&tstream, sdm_data.get(),
        serialized_sdm_data_size, &timestamp, &message_name, &msg,
        ProtobufReaderTestParseFailure);
    ASSERT_FALSE(success);
    ASSERT_TRUE(msg == NULL);
}

TEST_F(ProtobufReaderTest, ProtobufLogLevelTest) {
    log4cplus::LogLevel level;
    level = protobuf::impl::Protobuf2log4Level(LOGLEVEL_INFO);
    EXPECT_EQ(log4cplus::INFO_LOG_LEVEL, level);
    level = protobuf::impl::Protobuf2log4Level(LOGLEVEL_WARNING);
    EXPECT_EQ(log4cplus::WARN_LOG_LEVEL, level);
    level = protobuf::impl::Protobuf2log4Level(LOGLEVEL_ERROR);
    EXPECT_EQ(log4cplus::ERROR_LOG_LEVEL, level);
    level = protobuf::impl::Protobuf2log4Level(LOGLEVEL_FATAL);
    EXPECT_EQ(log4cplus::FATAL_LOG_LEVEL, level);
}

struct ArgSet {
    std::string statAttr;
    DbHandler::TagMap attribs_tag;
    DbHandler::AttribMap attribs;
};

class StatCbTester {
public:
    StatCbTester(const vector<ArgSet>& exp, bool log_exp = false) : exp_(exp) {
        for (vector<ArgSet>::const_iterator it = exp_.begin();
             it != exp_.end(); it++ ) {
            if (log_exp) {
                const ArgSet &exp_argSet(*it);
                LOG(ERROR, "Expected: " << it - exp_.begin());
                const std::string &statAttr(exp_argSet.statAttr);
                LOG(ERROR, "StatAttr: " << statAttr);
                const DbHandler::TagMap &attribs_tag(exp_argSet.attribs_tag);
                for (DbHandler::TagMap::const_iterator ct = attribs_tag.begin();
                     ct != attribs_tag.end(); ct++) {
                  LOG(ERROR, "tag " << ct->first);
                }
                const DbHandler::AttribMap &attribs(exp_argSet.attribs);
                for (DbHandler::AttribMap::const_iterator ct = attribs.begin();
                     ct != attribs.end(); ct++) {
                   LOG(ERROR, "attrib (" << ct->first << ", " << ct->second << ")");
                }
                LOG(ERROR, "\n");
            }
            match_.push_back(false);
        }
    }
    void Cb(const uint64_t &timestamp,
            const std::string& statName,
            const std::string& statAttr,
            const DbHandler::TagMap & attribs_tag,
            const DbHandler::AttribMap & attribs) {
        bool is_match = false;
        LOG(ERROR, "StatName: " << statName << " StatAttr: " << statAttr);
        for (DbHandler::TagMap::const_iterator ct = attribs_tag.begin();
             ct != attribs_tag.end(); ct++) {
            LOG(ERROR, "tag " << ct->first);
        }
        for (DbHandler::AttribMap::const_iterator ct = attribs.begin();
             ct != attribs.end(); ct++) {
            LOG(ERROR, "attrib (" << ct->first << ", " << ct->second << ")");
        }
        for (size_t idx = 0 ; idx < exp_.size() ; idx ++) {
            if ((exp_[idx].statAttr == statAttr) &&
                (exp_[idx].attribs_tag == attribs_tag) &&
                (exp_[idx].attribs == attribs)) {
                EXPECT_EQ(match_[idx] , false);
                match_[idx] = true;
                is_match = true;
                LOG(ERROR, "MATCHED");
                break;
            }
        }
        LOG(ERROR, "\n");
        EXPECT_EQ(true, is_match);
    }
    virtual ~StatCbTester() {
        for (vector<bool>::const_iterator it = match_.begin();
             it != match_.end(); it++ ) {
            EXPECT_EQ(true, *it);
        }
    }
    const vector<ArgSet> exp_;
    vector<bool> match_;
};

vector<ArgSet> PopulateTestMessageStatsInfo() {
    vector<ArgSet> av;
    ArgSet a1;
    a1.statAttr = string("enterprise.juniperNetworks.tm_simple.tm_inner");
    a1.attribs = map_list_of
        ("Source", DbHandler::Var("127.0.0.1"))
	("component_id", DbHandler::Var((uint64_t)0))
	("sensor_name", DbHandler::Var(""))
	("sequence_number", DbHandler::Var((uint64_t)0))
	("sub_component_id", DbHandler::Var((uint64_t)0))
	("system_id", DbHandler::Var("protobuf_test"))
	("version_major", DbHandler::Var((uint64_t)0))
	("version_minor", DbHandler::Var((uint64_t)0))
	
	("tm_name", DbHandler::Var("TestMessage"))
        ("tm_status", DbHandler::Var("Test"))
        ("tm_counter", DbHandler::Var((uint64_t)3))
        ("tm_enum", DbHandler::Var("GOOD"))

        ("enterprise.juniperNetworks.tm_simple.tm_name", DbHandler::Var("TestMessage"))
        ("enterprise.juniperNetworks.tm_simple.tm_status", DbHandler::Var("Test"))
        ("enterprise.juniperNetworks.tm_simple.tm_counter", DbHandler::Var((uint64_t)3))
        ("enterprise.juniperNetworks.tm_simple.tm_enum", DbHandler::Var("GOOD"))
        ("enterprise.juniperNetworks.tm_simple.tm_inner.tm_inner_name", DbHandler::Var("TestMessageInner1"))
        ("enterprise.juniperNetworks.tm_simple.tm_inner.tm_inner_status", DbHandler::Var((uint64_t)1))
        ("enterprise.juniperNetworks.tm_simple.tm_inner.tm_inner_counter", DbHandler::Var((uint64_t)1))
        ("enterprise.juniperNetworks.tm_simple.tm_inner.tm_inner_enum", DbHandler::Var("GOOD"));

    DbHandler::AttribMap sm;
    a1.attribs_tag.insert(make_pair("Source", make_pair(
        DbHandler::Var("127.0.0.1"), sm)));
    a1.attribs_tag.insert(make_pair("component_id", make_pair(
        DbHandler::Var((uint64_t)0), sm)));
    a1.attribs_tag.insert(make_pair("sensor_name", make_pair(
        DbHandler::Var(""), sm)));
    a1.attribs_tag.insert(make_pair("sequence_number", make_pair(
        DbHandler::Var((uint64_t)0), sm)));
    a1.attribs_tag.insert(make_pair("sub_component_id", make_pair(
        DbHandler::Var((uint64_t)0), sm)));
    a1.attribs_tag.insert(make_pair("system_id", make_pair(
        DbHandler::Var("protobuf_test"), sm)));
    a1.attribs_tag.insert(make_pair("version_major", make_pair(
        DbHandler::Var((uint64_t)0), sm)));
    a1.attribs_tag.insert(make_pair("version_minor", make_pair(
        DbHandler::Var((uint64_t)0), sm)));

    a1.attribs_tag.insert(make_pair("tm_name", make_pair(
        DbHandler::Var("TestMessage"), sm)));
    a1.attribs_tag.insert(make_pair("tm_status", make_pair(
        DbHandler::Var("Test"), sm)));
    a1.attribs_tag.insert(make_pair("tm_counter", make_pair(
        DbHandler::Var((uint64_t)3), sm)));
    a1.attribs_tag.insert(make_pair("tm_enum", make_pair(
        DbHandler::Var("GOOD"), sm)));

    a1.attribs_tag.insert(make_pair("enterprise.juniperNetworks.tm_simple.tm_name", make_pair(
        DbHandler::Var("TestMessage"), sm)));
    a1.attribs_tag.insert(make_pair("enterprise.juniperNetworks.tm_simple.tm_status", make_pair(
        DbHandler::Var("Test"), sm)));
    a1.attribs_tag.insert(make_pair("enterprise.juniperNetworks.tm_simple.tm_counter", make_pair(
        DbHandler::Var((uint64_t)3), sm)));
    a1.attribs_tag.insert(make_pair("enterprise.juniperNetworks.tm_simple.tm_enum", make_pair(
        DbHandler::Var("GOOD"), sm)));

    a1.attribs_tag.insert(make_pair("enterprise.juniperNetworks.tm_simple.tm_inner.tm_inner_name",
        make_pair(DbHandler::Var("TestMessageInner1"), sm)));
    a1.attribs_tag.insert(make_pair("enterprise.juniperNetworks.tm_simple.tm_inner.tm_inner_enum",
        make_pair(DbHandler::Var("GOOD"), sm)));
    av.push_back(a1);

    ArgSet a2;
    a2.statAttr = string("enterprise.juniperNetworks.tm_simple.tm_inner");
    a2.attribs = map_list_of
	("Source", DbHandler::Var("127.0.0.1"))
	("component_id", DbHandler::Var((uint64_t)0))
	("sensor_name", DbHandler::Var(""))
	("sequence_number", DbHandler::Var((uint64_t)0))
	("sub_component_id", DbHandler::Var((uint64_t)0))
	("system_id", DbHandler::Var("protobuf_test"))
	("version_major", DbHandler::Var((uint64_t)0))
	("version_minor", DbHandler::Var((uint64_t)0))
	
	("tm_name", DbHandler::Var("TestMessage"))
        ("tm_status", DbHandler::Var("Test"))
        ("tm_counter", DbHandler::Var((uint64_t)3))
        ("tm_enum", DbHandler::Var("GOOD"))

        ("enterprise.juniperNetworks.tm_simple.tm_name", DbHandler::Var("TestMessage"))
        ("enterprise.juniperNetworks.tm_simple.tm_status", DbHandler::Var("Test"))
        ("enterprise.juniperNetworks.tm_simple.tm_counter", DbHandler::Var((uint64_t)3))
        ("enterprise.juniperNetworks.tm_simple.tm_enum", DbHandler::Var("GOOD"))
        ("enterprise.juniperNetworks.tm_simple.tm_inner.tm_inner_name", DbHandler::Var("TestMessageInner2"))
        ("enterprise.juniperNetworks.tm_simple.tm_inner.tm_inner_status", DbHandler::Var((uint64_t)2))
        ("enterprise.juniperNetworks.tm_simple.tm_inner.tm_inner_counter", DbHandler::Var((uint64_t)2))
        ("enterprise.juniperNetworks.tm_simple.tm_inner.tm_inner_enum", DbHandler::Var("BAD"));

    a2.attribs_tag.insert(make_pair("Source", make_pair(
        DbHandler::Var("127.0.0.1"), sm)));
    a2.attribs_tag.insert(make_pair("component_id", make_pair(
        DbHandler::Var((uint64_t)0), sm)));
    a2.attribs_tag.insert(make_pair("sensor_name", make_pair(
        DbHandler::Var(""), sm)));
    a2.attribs_tag.insert(make_pair("sequence_number", make_pair(
        DbHandler::Var((uint64_t)0), sm)));
    a2.attribs_tag.insert(make_pair("sub_component_id", make_pair(
        DbHandler::Var((uint64_t)0), sm)));
    a2.attribs_tag.insert(make_pair("system_id", make_pair(
        DbHandler::Var("protobuf_test"), sm)));
    a2.attribs_tag.insert(make_pair("version_major", make_pair(
        DbHandler::Var((uint64_t)0), sm)));
    a2.attribs_tag.insert(make_pair("version_minor", make_pair(
        DbHandler::Var((uint64_t)0), sm)));
    
    a2.attribs_tag.insert(make_pair("tm_name", make_pair(
        DbHandler::Var("TestMessage"), sm)));
    a2.attribs_tag.insert(make_pair("tm_status", make_pair(
        DbHandler::Var("Test"), sm)));
    a2.attribs_tag.insert(make_pair("tm_counter", make_pair(
        DbHandler::Var((uint64_t)3), sm)));
    a2.attribs_tag.insert(make_pair("tm_enum", make_pair(
        DbHandler::Var("GOOD"), sm)));
    
    a2.attribs_tag.insert(make_pair("enterprise.juniperNetworks.tm_simple.tm_name", make_pair(
        DbHandler::Var("TestMessage"), sm)));
    a2.attribs_tag.insert(make_pair("enterprise.juniperNetworks.tm_simple.tm_status", make_pair(
        DbHandler::Var("Test"), sm)));
    a2.attribs_tag.insert(make_pair("enterprise.juniperNetworks.tm_simple.tm_counter", make_pair(
        DbHandler::Var((uint64_t)3), sm)));
    a2.attribs_tag.insert(make_pair("enterprise.juniperNetworks.tm_simple.tm_enum", make_pair(
        DbHandler::Var("GOOD"), sm)));

    a2.attribs_tag.insert(make_pair("enterprise.juniperNetworks.tm_simple.tm_inner.tm_inner_name",
        make_pair(DbHandler::Var("TestMessageInner2"), sm)));
    a2.attribs_tag.insert(make_pair("enterprise.juniperNetworks.tm_simple.tm_inner.tm_inner_enum",
        make_pair(DbHandler::Var("BAD"), sm)));
    av.push_back(a2);

    ArgSet a3;
    a3.statAttr = string("enterprise.juniperNetworks.tm_simple");
    a3.attribs = map_list_of
	("Source", DbHandler::Var("127.0.0.1"))
	("component_id", DbHandler::Var((uint64_t)0))
	("sensor_name", DbHandler::Var(""))
	("sequence_number", DbHandler::Var((uint64_t)0))
	("sub_component_id", DbHandler::Var((uint64_t)0))
	("system_id", DbHandler::Var("protobuf_test"))
	("version_major", DbHandler::Var((uint64_t)0))
	("version_minor", DbHandler::Var((uint64_t)0))
	
	("tm_name", DbHandler::Var("TestMessage"))
        ("tm_status", DbHandler::Var("Test"))
        ("tm_counter", DbHandler::Var((uint64_t)3))
        ("tm_enum", DbHandler::Var("GOOD"))

        ("enterprise.juniperNetworks.tm_simple.tm_name", DbHandler::Var("TestMessage"))
        ("enterprise.juniperNetworks.tm_simple.tm_status", DbHandler::Var("Test"))
        ("enterprise.juniperNetworks.tm_simple.tm_counter", DbHandler::Var((uint64_t)3))
	("enterprise.juniperNetworks.tm_simple.tm_enum", DbHandler::Var("GOOD"));

    a3.attribs_tag.insert(make_pair("Source", make_pair(
        DbHandler::Var("127.0.0.1"), sm)));
    a3.attribs_tag.insert(make_pair("component_id", make_pair(
        DbHandler::Var((uint64_t)0), sm)));
    a3.attribs_tag.insert(make_pair("sensor_name", make_pair(
        DbHandler::Var(""), sm)));
    a3.attribs_tag.insert(make_pair("sequence_number", make_pair(
        DbHandler::Var((uint64_t)0), sm)));
    a3.attribs_tag.insert(make_pair("sub_component_id", make_pair(
        DbHandler::Var((uint64_t)0), sm)));
    a3.attribs_tag.insert(make_pair("system_id", make_pair(
        DbHandler::Var("protobuf_test"), sm)));
    a3.attribs_tag.insert(make_pair("version_major", make_pair(
        DbHandler::Var((uint64_t)0), sm)));
    a3.attribs_tag.insert(make_pair("version_minor", make_pair(
        DbHandler::Var((uint64_t)0), sm)));
    
    a3.attribs_tag.insert(make_pair("tm_name", make_pair(
        DbHandler::Var("TestMessage"), sm)));
    a3.attribs_tag.insert(make_pair("tm_status", make_pair(
        DbHandler::Var("Test"), sm)));
    a3.attribs_tag.insert(make_pair("tm_counter", make_pair(
        DbHandler::Var((uint64_t)3), sm)));
    a3.attribs_tag.insert(make_pair("tm_enum", make_pair(
        DbHandler::Var("GOOD"), sm)));
    
    a3.attribs_tag.insert(make_pair("enterprise.juniperNetworks.tm_simple.tm_name", make_pair(
        DbHandler::Var("TestMessage"), sm)));
    a3.attribs_tag.insert(make_pair("enterprise.juniperNetworks.tm_simple.tm_status", make_pair(
        DbHandler::Var("Test"), sm)));
    a3.attribs_tag.insert(make_pair("enterprise.juniperNetworks.tm_simple.tm_counter", make_pair(
        DbHandler::Var((uint64_t)3), sm)));
    a3.attribs_tag.insert(make_pair("enterprise.juniperNetworks.tm_simple.tm_enum", make_pair(
        DbHandler::Var("GOOD"), sm)));

    av.push_back(a3);
    return av;
}

template<typename MessageT>
void PopulateTestMessageBaseStatsAttribs(DbHandler::AttribMap *attribs,
    int extension) {
    attribs->insert(make_pair("Source", DbHandler::Var("127.0.0.1")));
    attribs->insert(make_pair("tmb_name", DbHandler::Var("TestMessageBase")));
    attribs->insert(make_pair("tmb_status", DbHandler::Var("Test")));
    attribs->insert(make_pair("tmb_counter", DbHandler::Var((uint64_t)3)));
    attribs->insert(make_pair("tmb_enum", DbHandler::Var("GOOD")));
    std::string extension_name_prefix("tmb_extension");
    std::stringstream ss;
    ss << "tmb_extension" << extension << ".";
    std::string extension_name(ss.str());
    attribs->insert(make_pair(extension_name + "tm_name",
        DbHandler::Var(TYPE_NAME(MessageT))));
    attribs->insert(make_pair(extension_name + "tm_status",
        DbHandler::Var("Test")));
    attribs->insert(make_pair(extension_name + "tm_counter",
        DbHandler::Var((uint64_t)3)));
    attribs->insert(make_pair(extension_name + "tm_enum",
        DbHandler::Var("GOOD")));
}

template<typename MessageT>
void PopulateTestMessageBaseStatsAttribsTag(DbHandler::TagMap *attribs_tag,
    int extension) {
    DbHandler::AttribMap sm;
    attribs_tag->insert(make_pair("Source", make_pair(
        DbHandler::Var("127.0.0.1"), sm)));
    attribs_tag->insert(make_pair("tmb_name", make_pair(
        DbHandler::Var("TestMessageBase"), sm)));
    attribs_tag->insert(make_pair("tmb_status", make_pair(
        DbHandler::Var("Test"), sm)));
    attribs_tag->insert(make_pair("tmb_counter", make_pair(
        DbHandler::Var((uint64_t)3), sm)));
    attribs_tag->insert(make_pair("tmb_enum", make_pair(
        DbHandler::Var("GOOD"), sm)));
    std::string extension_name_prefix("tmb_extension");
    std::stringstream ss;
    ss << "tmb_extension" << extension << ".";
    std::string extension_name(ss.str());
    attribs_tag->insert(make_pair(extension_name + "tm_name", make_pair(
        DbHandler::Var(TYPE_NAME(MessageT)), sm)));
    attribs_tag->insert(make_pair(extension_name + "tm_status", make_pair(
        DbHandler::Var("Test"), sm)));
    attribs_tag->insert(make_pair(extension_name + "tm_counter", make_pair(
        DbHandler::Var((uint64_t)3), sm)));
    attribs_tag->insert(make_pair(extension_name + "tm_enum", make_pair(
        DbHandler::Var("GOOD"), sm)));
}

template<typename MessageT, typename InnerMessageT>
void PopulateTestMessageExtensionStatsAttribs(DbHandler::AttribMap *attribs,
    int extension, int inner_status,
    int inner_counter, const std::string &inner_enum_name) {
    attribs->insert(make_pair("Source", DbHandler::Var("127.0.0.1")));
    attribs->insert(make_pair("tmb_name", DbHandler::Var("TestMessageBase")));
    attribs->insert(make_pair("tmb_status", DbHandler::Var("Test")));
    attribs->insert(make_pair("tmb_counter", DbHandler::Var((uint64_t)3)));
    attribs->insert(make_pair("tmb_enum", DbHandler::Var("GOOD")));
    std::string extension_name_prefix("tmb_extension");
    std::stringstream ss;
    ss << "tmb_extension" << extension << ".";
    std::string extension_name(ss.str());
    attribs->insert(make_pair(extension_name + "tm_name",
        DbHandler::Var(TYPE_NAME(MessageT))));
    attribs->insert(make_pair(extension_name + "tm_status",
        DbHandler::Var("Test")));
    attribs->insert(make_pair(extension_name + "tm_counter",
        DbHandler::Var((uint64_t)3)));
    attribs->insert(make_pair(extension_name + "tm_enum",
        DbHandler::Var("GOOD")));
    std::stringstream ss1;
    ss1 << TYPE_NAME(InnerMessageT) << inner_status;
    std::string inner_name(ss1.str());
    attribs->insert(make_pair(extension_name + "tm_inner.tm_inner_name",
        DbHandler::Var(inner_name)));
    attribs->insert(make_pair(extension_name + "tm_inner.tm_inner_status",
        DbHandler::Var((uint64_t)inner_status)));
    attribs->insert(make_pair(extension_name + "tm_inner.tm_inner_counter",
        DbHandler::Var((uint64_t)inner_counter)));
    attribs->insert(make_pair(extension_name + "tm_inner.tm_inner_enum",
        DbHandler::Var(inner_enum_name)));
}

template<typename MessageT, typename InnerMessageT>
void PopulateTestMessageExtensionStatsAttribsTag(DbHandler::TagMap *attribs_tag,
    int extension, int inner_status, const std::string &inner_enum_name) {
    PopulateTestMessageBaseStatsAttribsTag<MessageT>(attribs_tag, extension);
    DbHandler::AttribMap sm;
    std::string extension_name_prefix("tmb_extension");
    std::stringstream ss;
    ss << extension_name_prefix << extension << ".";
    std::string extension_name(ss.str());
    std::stringstream ss1;
    ss1 << TYPE_NAME(InnerMessageT) << inner_status;
    std::string inner_name(ss1.str());
    attribs_tag->insert(make_pair(extension_name + "tm_inner.tm_inner_name",
        make_pair(DbHandler::Var(inner_name), sm)));
    attribs_tag->insert(make_pair(extension_name + "tm_inner.tm_inner_enum",
        make_pair(DbHandler::Var(inner_enum_name), sm)));
}

void PopulateTestMessageAllTypes(DbHandler::AttribMap *attribs) {
    attribs->insert(make_pair("Source", DbHandler::Var("127.0.0.1")));
    attribs->insert(make_pair("component_id", DbHandler::Var(static_cast<uint64_t>(0))));
    attribs->insert(make_pair("sensor_name", DbHandler::Var("")));
    attribs->insert(make_pair("sequence_number", DbHandler::Var(static_cast<uint64_t>(0))));
    attribs->insert(make_pair("sub_component_id", DbHandler::Var(static_cast<uint64_t>(0))));
    attribs->insert(make_pair("system_id", DbHandler::Var("protobuf_test")));
    attribs->insert(make_pair("version_major", DbHandler::Var(static_cast<uint64_t>(0))));
    attribs->insert(make_pair("version_minor", DbHandler::Var(static_cast<uint64_t>(0))));

    attribs->insert(make_pair("tmp_string", DbHandler::Var(
        "TestMessageAllTypes")));
    attribs->insert(make_pair("tmp_int32", DbHandler::Var(
        static_cast<uint64_t>(std::numeric_limits<int32_t>::max()))));
    attribs->insert(make_pair("tmp_int64", DbHandler::Var(
        static_cast<uint64_t>(std::numeric_limits<int64_t>::max()))));
    attribs->insert(make_pair("tmp_uint32", DbHandler::Var(
        static_cast<uint64_t>(std::numeric_limits<uint32_t>::max()))));
    attribs->insert(make_pair("tmp_uint64", DbHandler::Var(
        std::numeric_limits<uint64_t>::max())));
    attribs->insert(make_pair("tmp_float", DbHandler::Var(
        static_cast<double>(std::numeric_limits<float>::max()))));
    attribs->insert(make_pair("tmp_double", DbHandler::Var(
        std::numeric_limits<double>::max())));
    attribs->insert(make_pair("tmp_bool", DbHandler::Var(
        static_cast<uint64_t>(true))));
    attribs->insert(make_pair("tmp_enum", DbHandler::Var("GOOD")));

}
void PopulateTestMessageAllTypesAttribs(DbHandler::AttribMap *attribs,
    const std::string &statAttr) {
    attribs->insert(make_pair("Source", DbHandler::Var("127.0.0.1")));
    attribs->insert(make_pair("component_id", DbHandler::Var(static_cast<uint64_t>(0))));
    attribs->insert(make_pair("sensor_name", DbHandler::Var("")));
    attribs->insert(make_pair("sequence_number", DbHandler::Var(static_cast<uint64_t>(0))));
    attribs->insert(make_pair("sub_component_id", DbHandler::Var(static_cast<uint64_t>(0))));
    attribs->insert(make_pair("system_id", DbHandler::Var("protobuf_test")));
    attribs->insert(make_pair("version_major", DbHandler::Var(static_cast<uint64_t>(0))));
    attribs->insert(make_pair("version_minor", DbHandler::Var(static_cast<uint64_t>(0))));

    attribs->insert(make_pair("tmp_string", DbHandler::Var(
        "TestMessageAllTypes")));
    attribs->insert(make_pair("tmp_int32", DbHandler::Var(
        static_cast<uint64_t>(std::numeric_limits<int32_t>::max()))));
    attribs->insert(make_pair("tmp_int64", DbHandler::Var(
        static_cast<uint64_t>(std::numeric_limits<int64_t>::max()))));
    attribs->insert(make_pair("tmp_uint32", DbHandler::Var(
        static_cast<uint64_t>(std::numeric_limits<uint32_t>::max()))));
    attribs->insert(make_pair("tmp_uint64", DbHandler::Var(
        std::numeric_limits<uint64_t>::max())));
    attribs->insert(make_pair("tmp_float", DbHandler::Var(
        static_cast<double>(std::numeric_limits<float>::max()))));
    attribs->insert(make_pair("tmp_double", DbHandler::Var(
        std::numeric_limits<double>::max())));
    attribs->insert(make_pair("tmp_bool", DbHandler::Var(
        static_cast<uint64_t>(true))));
    attribs->insert(make_pair("tmp_enum", DbHandler::Var("GOOD")));

    attribs->insert(make_pair("enterprise.juniperNetworks.tm_all_types.tmp_string", DbHandler::Var(
        "TestMessageAllTypes")));
    attribs->insert(make_pair("enterprise.juniperNetworks.tm_all_types.tmp_int32", DbHandler::Var(
        static_cast<uint64_t>(std::numeric_limits<int32_t>::max()))));
    attribs->insert(make_pair("enterprise.juniperNetworks.tm_all_types.tmp_int64", DbHandler::Var(
        static_cast<uint64_t>(std::numeric_limits<int64_t>::max()))));
    attribs->insert(make_pair("enterprise.juniperNetworks.tm_all_types.tmp_uint32", DbHandler::Var(
        static_cast<uint64_t>(std::numeric_limits<uint32_t>::max()))));
    attribs->insert(make_pair("enterprise.juniperNetworks.tm_all_types.tmp_uint64", DbHandler::Var(
        std::numeric_limits<uint64_t>::max())));
    attribs->insert(make_pair("enterprise.juniperNetworks.tm_all_types.tmp_float", DbHandler::Var(
        static_cast<double>(std::numeric_limits<float>::max()))));
    attribs->insert(make_pair("enterprise.juniperNetworks.tm_all_types.tmp_double", DbHandler::Var(
        std::numeric_limits<double>::max())));
    attribs->insert(make_pair("enterprise.juniperNetworks.tm_all_types.tmp_bool", DbHandler::Var(
        static_cast<uint64_t>(true))));
    attribs->insert(make_pair("enterprise.juniperNetworks.tm_all_types.tmp_enum", DbHandler::Var("GOOD")));


    attribs->insert(make_pair(statAttr + ".tmp_inner_string", DbHandler::Var(
        "TestMessageAllTypesInner")));
    attribs->insert(make_pair(statAttr + ".tmp_inner_int32", DbHandler::Var(
        static_cast<uint64_t>(std::numeric_limits<int32_t>::min()))));
    attribs->insert(make_pair(statAttr + ".tmp_inner_int64", DbHandler::Var(
        static_cast<uint64_t>(std::numeric_limits<int64_t>::min()))));
    attribs->insert(make_pair(statAttr + ".tmp_inner_uint32", DbHandler::Var(
        static_cast<uint64_t>(std::numeric_limits<uint32_t>::min()))));
    attribs->insert(make_pair(statAttr + ".tmp_inner_uint64", DbHandler::Var(
        std::numeric_limits<uint64_t>::min())));
    attribs->insert(make_pair(statAttr + ".tmp_inner_float", DbHandler::Var(
        static_cast<double>(std::numeric_limits<float>::min()))));
    attribs->insert(make_pair(statAttr + ".tmp_inner_double", DbHandler::Var(
        std::numeric_limits<double>::min())));
    attribs->insert(make_pair(statAttr + ".tmp_inner_bool", DbHandler::Var(
        static_cast<uint64_t>(false))));
    attribs->insert(make_pair(statAttr + ".tmp_inner_enum", DbHandler::Var(
        "BAD")));
}

void PopulateTestMessageAllTypesTags(DbHandler::TagMap *attribs_tag) {
    DbHandler::AttribMap sm;
    attribs_tag->insert(make_pair("Source", make_pair(
        DbHandler::Var("127.0.0.1"), sm)));
    attribs_tag->insert(make_pair("component_id", make_pair(DbHandler::Var((uint64_t)0), sm)));
    attribs_tag->insert(make_pair("sensor_name", make_pair(
				DbHandler::Var(""), sm)));
    attribs_tag->insert(make_pair("sequence_number", make_pair(
				DbHandler::Var((uint64_t)0), sm)));
    attribs_tag->insert(make_pair("sub_component_id", make_pair(
				DbHandler::Var((uint64_t)0), sm)));
    attribs_tag->insert(make_pair("system_id", make_pair(
				DbHandler::Var("protobuf_test"), sm)));
    attribs_tag->insert(make_pair("version_major", make_pair(
				DbHandler::Var((uint64_t)0), sm)));
    attribs_tag->insert(make_pair("version_minor", make_pair(
				DbHandler::Var((uint64_t)0), sm)));


    attribs_tag->insert(make_pair("tmp_string", make_pair(
        DbHandler::Var("TestMessageAllTypes"), sm)));
    attribs_tag->insert(make_pair("tmp_int32", make_pair(
        DbHandler::Var((uint64_t) std::numeric_limits<int32_t>::max()), sm)));
    attribs_tag->insert(make_pair("tmp_int64", make_pair(
        DbHandler::Var((uint64_t) std::numeric_limits<int64_t>::max()), sm)));
    attribs_tag->insert(make_pair("tmp_uint32", make_pair(
        DbHandler::Var((uint64_t) std::numeric_limits<uint32_t>::max()), sm)));
    attribs_tag->insert(make_pair("tmp_uint64", make_pair(
        DbHandler::Var(std::numeric_limits<uint64_t>::max()), sm)));
    attribs_tag->insert(make_pair("tmp_float", make_pair(
        DbHandler::Var((double) std::numeric_limits<float>::max()), sm)));
    attribs_tag->insert(make_pair("tmp_double", make_pair(
        DbHandler::Var(std::numeric_limits<double>::max()), sm)));
    attribs_tag->insert(make_pair("tmp_bool", make_pair(
        DbHandler::Var((uint64_t) true), sm)));
    attribs_tag->insert(make_pair("tmp_enum", make_pair(
        DbHandler::Var("GOOD"), sm)));
    
    attribs_tag->insert(make_pair("enterprise.juniperNetworks.tm_all_types.tmp_string", make_pair(
        DbHandler::Var("TestMessageAllTypes"), sm)));
    attribs_tag->insert(make_pair("enterprise.juniperNetworks.tm_all_types.tmp_int32", make_pair(
        DbHandler::Var((uint64_t) std::numeric_limits<int32_t>::max()), sm)));
    attribs_tag->insert(make_pair("enterprise.juniperNetworks.tm_all_types.tmp_int64", make_pair(
        DbHandler::Var((uint64_t) std::numeric_limits<int64_t>::max()), sm)));
    attribs_tag->insert(make_pair("enterprise.juniperNetworks.tm_all_types.tmp_uint32", make_pair(
        DbHandler::Var((uint64_t) std::numeric_limits<uint32_t>::max()), sm)));
    attribs_tag->insert(make_pair("enterprise.juniperNetworks.tm_all_types.tmp_uint64", make_pair(
        DbHandler::Var(std::numeric_limits<uint64_t>::max()), sm)));
    attribs_tag->insert(make_pair("enterprise.juniperNetworks.tm_all_types.tmp_float", make_pair(
        DbHandler::Var((double) std::numeric_limits<float>::max()), sm)));
    attribs_tag->insert(make_pair("enterprise.juniperNetworks.tm_all_types.tmp_double", make_pair(
        DbHandler::Var(std::numeric_limits<double>::max()), sm)));
    attribs_tag->insert(make_pair("enterprise.juniperNetworks.tm_all_types.tmp_bool", make_pair(
        DbHandler::Var((uint64_t) true), sm)));
    attribs_tag->insert(make_pair("enterprise.juniperNetworks.tm_all_types.tmp_enum", make_pair(
        DbHandler::Var("GOOD"), sm)));
}
void PopulateTestMessageAllTypesAttribTags(DbHandler::TagMap *attribs_tag,
    const std::string &statAttr) {
    DbHandler::AttribMap sm;
    attribs_tag->insert(make_pair("Source", make_pair(
        DbHandler::Var("127.0.0.1"), sm)));
    attribs_tag->insert(make_pair("component_id", make_pair(DbHandler::Var((uint64_t)0), sm)));
    attribs_tag->insert(make_pair("sensor_name", make_pair(
				DbHandler::Var(""), sm)));
    attribs_tag->insert(make_pair("sequence_number", make_pair(
				DbHandler::Var((uint64_t)0), sm)));
    attribs_tag->insert(make_pair("sub_component_id", make_pair(
				DbHandler::Var((uint64_t)0), sm)));
    attribs_tag->insert(make_pair("system_id", make_pair(
				DbHandler::Var("protobuf_test"), sm)));
    attribs_tag->insert(make_pair("version_major", make_pair(
				DbHandler::Var((uint64_t)0), sm)));
    attribs_tag->insert(make_pair("version_minor", make_pair(
				DbHandler::Var((uint64_t)0), sm)));


    attribs_tag->insert(make_pair("tmp_string", make_pair(
        DbHandler::Var("TestMessageAllTypes"), sm)));
    attribs_tag->insert(make_pair("tmp_int32", make_pair(
        DbHandler::Var((uint64_t) std::numeric_limits<int32_t>::max()), sm)));
    attribs_tag->insert(make_pair("tmp_int64", make_pair(
        DbHandler::Var((uint64_t) std::numeric_limits<int64_t>::max()), sm)));
    attribs_tag->insert(make_pair("tmp_uint32", make_pair(
        DbHandler::Var((uint64_t) std::numeric_limits<uint32_t>::max()), sm)));
    attribs_tag->insert(make_pair("tmp_uint64", make_pair(
        DbHandler::Var(std::numeric_limits<uint64_t>::max()), sm)));
    attribs_tag->insert(make_pair("tmp_float", make_pair(
        DbHandler::Var((double) std::numeric_limits<float>::max()), sm)));
    attribs_tag->insert(make_pair("tmp_double", make_pair(
        DbHandler::Var(std::numeric_limits<double>::max()), sm)));
    attribs_tag->insert(make_pair("tmp_bool", make_pair(
        DbHandler::Var((uint64_t) true), sm)));
    attribs_tag->insert(make_pair("tmp_enum", make_pair(
        DbHandler::Var("GOOD"), sm)));
   
    attribs_tag->insert(make_pair("enterprise.juniperNetworks.tm_all_types.tmp_string", make_pair(
        DbHandler::Var("TestMessageAllTypes"), sm)));
    attribs_tag->insert(make_pair("enterprise.juniperNetworks.tm_all_types.tmp_int32", make_pair(
        DbHandler::Var((uint64_t) std::numeric_limits<int32_t>::max()), sm)));
    attribs_tag->insert(make_pair("enterprise.juniperNetworks.tm_all_types.tmp_int64", make_pair(
        DbHandler::Var((uint64_t) std::numeric_limits<int64_t>::max()), sm)));
    attribs_tag->insert(make_pair("enterprise.juniperNetworks.tm_all_types.tmp_uint32", make_pair(
        DbHandler::Var((uint64_t) std::numeric_limits<uint32_t>::max()), sm)));
    attribs_tag->insert(make_pair("enterprise.juniperNetworks.tm_all_types.tmp_uint64", make_pair(
        DbHandler::Var(std::numeric_limits<uint64_t>::max()), sm)));
    attribs_tag->insert(make_pair("enterprise.juniperNetworks.tm_all_types.tmp_float", make_pair(
        DbHandler::Var((double) std::numeric_limits<float>::max()), sm)));
    attribs_tag->insert(make_pair("enterprise.juniperNetworks.tm_all_types.tmp_double", make_pair(
        DbHandler::Var(std::numeric_limits<double>::max()), sm)));
    attribs_tag->insert(make_pair("enterprise.juniperNetworks.tm_all_types.tmp_bool", make_pair(
        DbHandler::Var((uint64_t) true), sm)));
    attribs_tag->insert(make_pair("enterprise.juniperNetworks.tm_all_types.tmp_enum", make_pair(
        DbHandler::Var("GOOD"), sm)));
    
    attribs_tag->insert(make_pair(statAttr + ".tmp_inner_string",
        make_pair(DbHandler::Var("TestMessageAllTypesInner"), sm)));
    attribs_tag->insert(make_pair(statAttr + ".tmp_inner_enum",
        make_pair(DbHandler::Var("BAD"), sm)));
}

vector<ArgSet> PopulateTestMessageAllTypesStatsInfo() {
    vector<ArgSet> av;
    ArgSet a1;
    a1.statAttr = string("enterprise.juniperNetworks.tm_all_types.tmp_inner");
    PopulateTestMessageAllTypesAttribs(&a1.attribs, a1.statAttr);
    PopulateTestMessageAllTypesAttribTags(&a1.attribs_tag, a1.statAttr);
    av.push_back(a1);

    ArgSet a2;
    a2.statAttr = string("enterprise.juniperNetworks.tm_all_types.tmp_message");
    PopulateTestMessageAllTypesAttribs(&a2.attribs, a2.statAttr);
    PopulateTestMessageAllTypesAttribTags(&a2.attribs_tag, a2.statAttr);
    av.push_back(a2);

    ArgSet a3;
    a3.statAttr = string("enterprise.juniperNetworks.tm_all_types");
    PopulateTestMessageAllTypes(&a3.attribs);
    a3.attribs.insert(make_pair("enterprise.juniperNetworks.tm_all_types.tmp_string", DbHandler::Var(
        "TestMessageAllTypes")));
    a3.attribs.insert(make_pair("enterprise.juniperNetworks.tm_all_types.tmp_int32", DbHandler::Var(
        static_cast<uint64_t>(std::numeric_limits<int32_t>::max()))));
    a3.attribs.insert(make_pair("enterprise.juniperNetworks.tm_all_types.tmp_int64", DbHandler::Var(
        static_cast<uint64_t>(std::numeric_limits<int64_t>::max()))));
    a3.attribs.insert(make_pair("enterprise.juniperNetworks.tm_all_types.tmp_uint32", DbHandler::Var(
        static_cast<uint64_t>(std::numeric_limits<uint32_t>::max()))));
    a3.attribs.insert(make_pair("enterprise.juniperNetworks.tm_all_types.tmp_uint64", DbHandler::Var(
        std::numeric_limits<uint64_t>::max())));
    a3.attribs.insert(make_pair("enterprise.juniperNetworks.tm_all_types.tmp_float", DbHandler::Var(
        static_cast<double>(std::numeric_limits<float>::max()))));
    a3.attribs.insert(make_pair("enterprise.juniperNetworks.tm_all_types.tmp_double", DbHandler::Var(
        std::numeric_limits<double>::max())));
    a3.attribs.insert(make_pair("enterprise.juniperNetworks.tm_all_types.tmp_bool", DbHandler::Var(
        static_cast<uint64_t>(true))));
    a3.attribs.insert(make_pair("enterprise.juniperNetworks.tm_all_types.tmp_enum", DbHandler::Var("GOOD")));
    PopulateTestMessageAllTypesTags(&a3.attribs_tag);
    
    av.push_back(a3);

    return av;
}

class ProtobufStatWalkerTest : public ::testing::Test {
};

TEST_F(ProtobufStatWalkerTest, Basic) {
    StatCbTester ct(PopulateTestMessageStatsInfo());

    // Create TestMessage and serialize it
    boost::scoped_array<uint8_t> data(new uint8_t[kTestMessageBufferSize]);
    int serialized_data_size(0);
    CreateAndSerializeTestMessage(data.get(), kTestMessageBufferSize,
        &serialized_data_size);
    // Create TelemetryStream for TestMessage and serialize it
    boost::scoped_array<uint8_t> sdm_data(
	new uint8_t[kTelemetryStreamBufferSize]);
    int serialized_sdm_data_size(0);
    CreateAndSerializeTelemetryStreamMessage("TestMessage", sdm_data.get(),
	kTelemetryStreamBufferSize, &serialized_sdm_data_size,
        data.get(), (size_t) serialized_data_size);
    // Parse the TelemetryStream from sdm_data to get TestMessage
    protobuf::impl::ProtobufReader reader;
    std::string message_name;
    Message *msg = NULL;
    uint64_t timestamp;
    TelemetryStream tstream;
    //ASSERT_TRUE(reader.ParseSchemaFiles(tm_proto_dir_));
    bool success = reader.ParseTelemetryStreamMessage(&tstream, sdm_data.get(),
        serialized_sdm_data_size, &timestamp, &message_name, &msg, NULL);
    ASSERT_TRUE(success);
    ASSERT_TRUE(msg != NULL);

    boost::system::error_code ec;
    boost::asio::ip::address raddr(
        boost::asio::ip::address::from_string("127.0.0.1", ec));
    boost::asio::ip::udp::endpoint rep(raddr, 0);
    protobuf::impl::ProcessProtobufMessage(tstream, message_name, 
					   *msg, timestamp, rep, 
		    boost::bind(&StatCbTester::Cb, &ct, _1, _2, _3, _4, _5));
    delete msg;
}

TEST_F(ProtobufStatWalkerTest, AllTypes) {
    StatCbTester ct(PopulateTestMessageAllTypesStatsInfo(), true);

    // Create TestMessageAllTyes and serialize it
    boost::scoped_array<uint8_t> data(new uint8_t[kTestMessageBufferSize]);
    int serialized_data_size(0);
    CreateAndSerializeTestMessageAllTypes(data.get(), kTestMessageBufferSize,
        &serialized_data_size);
    // Create TelemetryStream for TestMessageAllTyes and serialize it
    boost::scoped_array<uint8_t> sdm_data(
	new uint8_t[kTelemetryStreamBufferSize]);
    int serialized_sdm_data_size(0);
    CreateAndSerializeTelemetryStreamMessage("TestMessageAllTypes",
	sdm_data.get(), kTelemetryStreamBufferSize,
        &serialized_sdm_data_size,
        data.get(), (size_t) serialized_data_size);
    // Parse the TelemetryStream from sdm_data to get TestMessageAllTypes
    protobuf::impl::ProtobufReader reader;
    std::string message_name;
    Message *msg = NULL;
    uint64_t timestamp;
    TelemetryStream tstream;
    //ASSERT_TRUE(reader.ParseSchemaFiles(tm_proto_dir_));
    bool success = reader.ParseTelemetryStreamMessage(&tstream, sdm_data.get(),
        serialized_sdm_data_size, &timestamp, &message_name, &msg, NULL);
    ASSERT_TRUE(success);
    ASSERT_TRUE(msg != NULL);

    boost::system::error_code ec;
    boost::asio::ip::address raddr(
        boost::asio::ip::address::from_string("127.0.0.1", ec));
    boost::asio::ip::udp::endpoint rep(raddr, 0);
    protobuf::impl::ProcessProtobufMessage(tstream, message_name,
					   *msg, timestamp, rep,
        boost::bind(&StatCbTester::Cb, &ct, _1, _2, _3, _4, _5));
    delete msg;
}

class ProtobufMockClient : public UdpServer {
 public:
    explicit ProtobufMockClient(EventManager *evm) :
        UdpServer(evm),
        tx_count_(0) {
    }

    void Send(const std::string &snd, boost::asio::ip::udp::endpoint to) {
        boost::asio::mutable_buffer send = AllocateBuffer(snd.length());
        char *p = boost::asio::buffer_cast<char *>(send);
        std::copy(snd.begin(), snd.end(), p);
        LOG(ERROR, "ProtobufMockClient sending to " << to);
        StartSend(to, snd.length(), send);
        snd_buf_ = snd;
    }

    void HandleSend(boost::asio::const_buffer send_buffer,
                    boost::asio::ip::udp::endpoint remote_endpoint,
                    std::size_t bytes_transferred,
                    const boost::system::error_code& error) {
        tx_count_ += 1;
        LOG(ERROR, "ProtobufMockClient sent " << bytes_transferred
            << " bytes, error(" << error << ")");
    }

    int GetTxPackets() { return tx_count_; }

 private:
    int tx_count_;
    std::string snd_buf_;
};

class ProtobufServerStatsTest : public ::testing::Test {
 protected:
    ProtobufServerStatsTest() :
        stats_tester_(PopulateTestMessageStatsInfo()) {
    }

    virtual void SetUp() {
        for (size_t i = 0; i < stats_tester_.exp_.size(); i++) {
            match_.push_back(true);
        }
        evm_.reset(new EventManager());
        server_.reset(new protobuf::ProtobufServer(evm_.get(), 0,
            boost::bind(&StatCbTester::Cb, &stats_tester_, _1, _2, _3,
            _4, _5)));
        // Set libprotobuf shutdown on delete to false since that can happen
        // only once in the lifetime of a binary
        server_->SetShutdownLibProtobufOnDelete(false);
        client_ = new ProtobufMockClient(evm_.get());
        thread_.reset(new ServerThread(evm_.get()));
    }

    virtual void TearDown() {
        match_.clear();
        task_util::WaitForIdle();
        evm_->Shutdown();
        task_util::WaitForIdle();
        client_->Shutdown();
        task_util::WaitForIdle();
        server_->Shutdown();
        task_util::WaitForIdle();
        UdpServerManager::DeleteServer(client_);
        task_util::WaitForIdle();
        if (thread_.get() != NULL) {
            thread_->Join();
        }
        task_util::WaitForIdle();
    }

    size_t GetServerReceivedMessageStatisticsSize() {
        std::vector<SocketEndpointMessageStats> va_rx_msg_stats;
        server_->GetReceivedMessageStatistics(&va_rx_msg_stats);
        return va_rx_msg_stats.size();
    }

    std::string GetClientEndpoint() const {
        boost::system::error_code ec;
        boost::asio::ip::udp::endpoint client_endpoint =
            client_->GetLocalEndpoint(&ec);
        EXPECT_TRUE(ec == 0);
        boost::asio::ip::address local_client_addr =
            boost::asio::ip::address::from_string("127.0.0.1", ec);
        EXPECT_TRUE(ec == 0);
        client_endpoint.address(local_client_addr);
        std::stringstream ss;
        ss << client_endpoint;
        return ss.str();
    }

    std::vector<bool> match_;
    StatCbTester stats_tester_;
    std::auto_ptr<ServerThread> thread_;
    std::auto_ptr<protobuf::ProtobufServer> server_;
    ProtobufMockClient *client_;
    std::auto_ptr<EventManager> evm_;
};

static bool VerifySocketEndpointMessageStats(
    const SocketEndpointMessageStats &stats, const std::string &endpoint_name,
    const std::string &message_name, uint64_t messages, uint64_t bytes,
    uint64_t errors) {
    EXPECT_EQ(endpoint_name, stats.get_endpoint_name());
    EXPECT_EQ(message_name, stats.get_message_name());
    EXPECT_EQ(messages, stats.get_messages());
    EXPECT_EQ(bytes, stats.get_bytes());
    EXPECT_EQ(errors, stats.get_errors());
    return true;
}

TEST_F(ProtobufServerStatsTest, Basic) {
    EXPECT_TRUE(server_->Initialize());
    task_util::WaitForIdle();
    boost::system::error_code ec;
    boost::asio::ip::udp::endpoint server_endpoint =
        server_->GetLocalEndpoint(&ec);
    EXPECT_TRUE(ec == 0);
    LOG(ERROR, "ProtobufServer: " << server_endpoint);
    thread_->Start();
    client_->Initialize(0);
    // Create TestMessage and serialize it
    boost::scoped_array<uint8_t> data(new uint8_t[kTestMessageBufferSize]);
    int serialized_data_size(0);
    CreateAndSerializeTestMessage(data.get(), kTestMessageBufferSize,
        &serialized_data_size);
    // Create TelemetryStream for TestMessage and serialize it
    boost::scoped_array<uint8_t> sdm_data(
	new uint8_t[kTelemetryStreamBufferSize]);
    int serialized_sdm_data_size(0);
    CreateAndSerializeTelemetryStreamMessage("TestMessage", sdm_data.get(),
	kTelemetryStreamBufferSize, &serialized_sdm_data_size,
        data.get(), (size_t) serialized_data_size);
    std::string snd(reinterpret_cast<const char *>(sdm_data.get()),
        serialized_sdm_data_size);
    client_->Send(snd, server_endpoint);
    TASK_UTIL_EXPECT_EQ(client_->GetTxPackets(), 1);
    TASK_UTIL_EXPECT_VECTOR_EQ(stats_tester_.match_, match_);
    // Compare statistics
    TASK_UTIL_EXPECT_EQ(1, GetServerReceivedMessageStatisticsSize());
    std::vector<SocketEndpointMessageStats> va_rx_msg_stats;
    server_->GetReceivedMessageStatistics(&va_rx_msg_stats);
    const SocketEndpointMessageStats &a_rx_msg_stats(va_rx_msg_stats[0]);
    std::string client_endpoint(GetClientEndpoint());
    EXPECT_TRUE(VerifySocketEndpointMessageStats(a_rx_msg_stats,
        client_endpoint, "enterprise.juniperNetworks.tm_simple", 1, serialized_sdm_data_size, 0));

    std::vector<SocketIOStats> va_rx_stats, va_tx_stats;
    std::vector<SocketEndpointMessageStats> va_rx_diff_msg_stats;
    server_->GetStatistics(&va_tx_stats, &va_rx_stats, &va_rx_diff_msg_stats);
    EXPECT_EQ(1, va_tx_stats.size());
    EXPECT_EQ(1, va_rx_stats.size());
    const SocketIOStats &a_rx_io_stats(va_rx_stats[0]);
    EXPECT_EQ(1, a_rx_io_stats.get_calls());
    EXPECT_EQ(serialized_sdm_data_size, a_rx_io_stats.get_bytes());
    EXPECT_EQ(serialized_sdm_data_size, a_rx_io_stats.get_average_bytes());
    const SocketIOStats &a_tx_io_stats(va_tx_stats[0]);
    EXPECT_EQ(0, a_tx_io_stats.get_calls());
    EXPECT_EQ(0, a_tx_io_stats.get_bytes());
    EXPECT_EQ(0, a_tx_io_stats.get_average_bytes());
    EXPECT_EQ(1, va_rx_diff_msg_stats.size());
    const SocketEndpointMessageStats &a_rx_diff_msg_stats(
        va_rx_diff_msg_stats[0]);
    EXPECT_TRUE(VerifySocketEndpointMessageStats(a_rx_diff_msg_stats,
        client_endpoint, "enterprise.juniperNetworks.tm_simple", 1, serialized_sdm_data_size, 0));
}

class ProtobufServerTest : public ::testing::Test {
 protected:
    virtual void SetUp() {
        evm_.reset(new EventManager());
        server_.reset(new protobuf::ProtobufServer(evm_.get(), 0, NULL));
        // Set libprotobuf shutdown on delete to false since that can happen
        // only once in the lifetime of a binary
        server_->SetShutdownLibProtobufOnDelete(false);
        client_ = new ProtobufMockClient(evm_.get());
        thread_.reset(new ServerThread(evm_.get()));
    }

    virtual void TearDown() {
        task_util::WaitForIdle();
        evm_->Shutdown();
        task_util::WaitForIdle();
        client_->Shutdown();
        task_util::WaitForIdle();
        server_->Shutdown();
        task_util::WaitForIdle();
        UdpServerManager::DeleteServer(client_);
        task_util::WaitForIdle();
        if (thread_.get() != NULL) {
            thread_->Join();
        }
        task_util::WaitForIdle();
    }
    uint64_t GetServerRxBytes() {
        std::vector<SocketIOStats> va_rx_stats;
        server_->GetStatistics(NULL, &va_rx_stats, NULL);
        if (va_rx_stats.size()) {
            const SocketIOStats &a_rx_stats(va_rx_stats[0]);
            return a_rx_stats.get_bytes();
        } else {
            return 0;
        }
    }

    size_t GetServerReceivedMessageStatisticsSize() {
        std::vector<SocketEndpointMessageStats> va_rx_msg_stats;
        server_->GetReceivedMessageStatistics(&va_rx_msg_stats);
        return va_rx_msg_stats.size();
    }

    std::auto_ptr<ServerThread> thread_;
    std::auto_ptr<protobuf::ProtobufServer> server_;
    ProtobufMockClient *client_;
    std::auto_ptr<EventManager> evm_;
};

TEST_F(ProtobufServerTest, MessageSize) {
    EXPECT_TRUE(server_->Initialize());
    task_util::WaitForIdle();
    boost::system::error_code ec;
    boost::asio::ip::udp::endpoint server_endpoint =
        server_->GetLocalEndpoint(&ec);
    EXPECT_TRUE(ec == 0);
    LOG(ERROR, "ProtobufServer: " << server_endpoint);
    thread_->Start();
    client_->Initialize(0);
    // Create TestMessageSize and serialize it
    boost::scoped_array<uint8_t> data(new uint8_t[kTestMessageSizeBufferSize]);
    int serialized_data_size(0);
    CreateAndSerializeTestMessageSize(data.get(), kTestMessageSizeBufferSize,
        &serialized_data_size, 4096);
    // Create TelemetryStream for TestMessageSize and serialize it
    boost::scoped_array<uint8_t> sdm_data(
         new uint8_t[kTelemetryStreamSizeBufferSize]);
    int serialized_sdm_data_size(0);
    CreateAndSerializeTelemetryStreamMessage("TestMessageSize", sdm_data.get(),
	kTelemetryStreamSizeBufferSize, &serialized_sdm_data_size,
        data.get(), (size_t) serialized_data_size);
    std::string snd(reinterpret_cast<const char *>(sdm_data.get()),
        serialized_sdm_data_size);
    client_->Send(snd, server_endpoint);
    TASK_UTIL_EXPECT_EQ(client_->GetTxPackets(), 1);
    TASK_UTIL_EXPECT_EQ(GetServerRxBytes(), (size_t) serialized_sdm_data_size);
    // Compare statistics
    TASK_UTIL_EXPECT_EQ(1, GetServerReceivedMessageStatisticsSize());
    std::vector<SocketEndpointMessageStats> va_rx_msg_stats;
    server_->GetReceivedMessageStatistics(&va_rx_msg_stats);
    const SocketEndpointMessageStats &a_rx_msg_stats(va_rx_msg_stats[0]);
    boost::asio::ip::udp::endpoint client_endpoint =
        client_->GetLocalEndpoint(&ec);
    EXPECT_TRUE(ec == 0);
    boost::asio::ip::address local_client_addr =
        boost::asio::ip::address::from_string("127.0.0.1", ec);
    EXPECT_TRUE(ec == 0);
    client_endpoint.address(local_client_addr);
    std::stringstream ss;
    ss << client_endpoint;
    EXPECT_EQ(ss.str(), a_rx_msg_stats.get_endpoint_name());
    EXPECT_EQ(1, a_rx_msg_stats.get_messages());
    EXPECT_EQ(0, a_rx_msg_stats.get_errors());
    EXPECT_EQ(serialized_sdm_data_size, a_rx_msg_stats.get_bytes());
    EXPECT_EQ("TestMessageSize", a_rx_msg_stats.get_message_name());
}

TEST_F(ProtobufServerTest, Drop) {
    EXPECT_TRUE(server_->Initialize());
    task_util::WaitForIdle();
    boost::system::error_code ec;
    boost::asio::ip::udp::endpoint server_endpoint =
        server_->GetLocalEndpoint(&ec);
    EXPECT_TRUE(ec == 0);
    LOG(ERROR, "ProtobufServer: " << server_endpoint);
    thread_->Start();
    client_->Initialize(0);
    // Create TestMessageSize and serialize it
    boost::scoped_array<uint8_t> data(new uint8_t[kTestMessageBufferSize]);
    int serialized_data_size(0);
    CreateAndSerializeTestMessageSize(data.get(), kTestMessageBufferSize,
        &serialized_data_size, 8);
    // Create TelemetryStream for TestMessageSize with type name
    // wrongly set to TestMessageSizeWrong and serialize it, so that the
    // server drops it when parsing
    boost::scoped_array<uint8_t> sdm_data(
	new uint8_t[kTelemetryStreamBufferSize]);
    int serialized_sdm_data_size(0);
    CreateAndSerializeTelemetryStreamMessage("TestMessageSizeWrong",
	sdm_data.get(), kTelemetryStreamBufferSize,
        &serialized_sdm_data_size, data.get(),
        (size_t) serialized_data_size);
    //std::string snd(reinterpret_cast<const char *>(sdm_data.get()),
    //    serialized_sdm_data_size);
    std::string snd(reinterpret_cast<const char *>(data.get()), 
		    serialized_data_size);
    client_->Send(snd, server_endpoint);
    TASK_UTIL_EXPECT_EQ(client_->GetTxPackets(), 1);
    TASK_UTIL_EXPECT_EQ(GetServerRxBytes(), (size_t) serialized_sdm_data_size);
    // Compare statistics
    TASK_UTIL_EXPECT_EQ(1, GetServerReceivedMessageStatisticsSize());
    std::vector<SocketEndpointMessageStats> va_rx_msg_stats;
    server_->GetReceivedMessageStatistics(&va_rx_msg_stats);
    const SocketEndpointMessageStats &a_rx_msg_stats(va_rx_msg_stats[0]);
    boost::asio::ip::udp::endpoint client_endpoint =
        client_->GetLocalEndpoint(&ec);
    EXPECT_TRUE(ec == 0);
    boost::asio::ip::address local_client_addr =
        boost::asio::ip::address::from_string("127.0.0.1", ec);
    EXPECT_TRUE(ec == 0);
    client_endpoint.address(local_client_addr);
    std::stringstream ss;
    ss << client_endpoint;
    EXPECT_EQ(ss.str(), a_rx_msg_stats.get_endpoint_name());
    EXPECT_EQ(0, a_rx_msg_stats.get_messages());
    EXPECT_EQ(1, a_rx_msg_stats.get_errors());
    EXPECT_EQ(0, a_rx_msg_stats.get_bytes());
    EXPECT_EQ("JuniperNetworksSensors", a_rx_msg_stats.get_message_name());
}
}  // namespace

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    LoggingInit();
    int result = RUN_ALL_TESTS();
    return result;
}
