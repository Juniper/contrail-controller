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
#include "analytics/protobuf_schema.pb.h"
#include "analytics/protobuf_server_impl.h"
#include "analytics/protobuf_server.h"
#include "test_message.pb.h"
#include "test_message_extensions.pb.h"

using namespace ::google::protobuf;
using boost::assign::map_list_of;

static std::string tm_desc_file_(
    "build/debug/analytics/test/test_message.desc");
static std::string tme_desc_file_(
    "build/debug/analytics/test/test_message_extensions.desc");
static const int kTestMessageBufferSize = 1024;
static const int kSelfDescribingMessageBufferSize = 8 * 1024;
static const int kTestMessageSizeBufferSize = 5 * 1024;
static const int kSelfDescribingMessageSizeBufferSize = 16 * 1024;

namespace {

class MockProtobufReader : public protobuf::impl::ProtobufReader {
 protected:
    virtual const Message* GetPrototype(const Descriptor *mdesc) {
        return NULL;
    }
};

class ProtobufReaderTest : public ::testing::Test {
};

template<typename MessageT, typename InnerMessageT>
bool VerifyTestMessageInner(const Message &inner_msg, int expected_status,
    typename MessageT::TestMessageEnum expected_enum,
    const char *expected_enum_name) {
    // Descriptor
    const Descriptor *inner_msg_desc = inner_msg.GetDescriptor();
    const FieldDescriptor* tm_inner_name_field =
        inner_msg_desc->FindFieldByName("tm_inner_name");
    EXPECT_TRUE(tm_inner_name_field != NULL);
    EXPECT_TRUE(tm_inner_name_field->type() == FieldDescriptor::TYPE_STRING);
    EXPECT_TRUE(tm_inner_name_field->label() ==
        FieldDescriptor::LABEL_REQUIRED);
    EXPECT_TRUE(tm_inner_name_field->number() == 1);
    const FieldDescriptor* tm_inner_status_field =
        inner_msg_desc->FindFieldByName("tm_inner_status");
    EXPECT_TRUE(tm_inner_status_field != NULL);
    EXPECT_TRUE(tm_inner_status_field->type() == FieldDescriptor::TYPE_INT32);
    EXPECT_TRUE(tm_inner_status_field->label() ==
        FieldDescriptor::LABEL_OPTIONAL);
    EXPECT_TRUE(tm_inner_status_field->number() == 2);
    const FieldDescriptor* tm_inner_counter_field =
        inner_msg_desc->FindFieldByName("tm_inner_counter");
    EXPECT_TRUE(tm_inner_counter_field != NULL);
    EXPECT_TRUE(tm_inner_counter_field->type() == FieldDescriptor::TYPE_INT32);
    EXPECT_TRUE(tm_inner_counter_field->label() ==
        FieldDescriptor::LABEL_OPTIONAL);
    EXPECT_TRUE(tm_inner_counter_field->number() == 3);
    const FieldDescriptor* tm_inner_enum_field =
        inner_msg_desc->FindFieldByName("tm_inner_enum");
    EXPECT_TRUE(tm_inner_enum_field != NULL);
    EXPECT_TRUE(tm_inner_enum_field->type() == FieldDescriptor::TYPE_ENUM);
    EXPECT_TRUE(tm_inner_enum_field->label() ==
        FieldDescriptor::LABEL_OPTIONAL);
    EXPECT_TRUE(tm_inner_enum_field->number() == 4);
    // Reflection
    const Reflection *inner_reflection = inner_msg.GetReflection();
    std::string tm_inner_name(TYPE_NAME(InnerMessageT));
    std::stringstream ss;
    ss << tm_inner_name << expected_status;
    EXPECT_TRUE(inner_reflection->GetString(
        inner_msg, tm_inner_name_field) == ss.str());
    EXPECT_TRUE(inner_reflection->GetInt32(
        inner_msg, tm_inner_status_field) == expected_status);
    EXPECT_TRUE(inner_reflection->GetInt32(
        inner_msg, tm_inner_counter_field) == expected_status);
    const EnumValueDescriptor *edesc(inner_reflection->GetEnum(
        inner_msg, tm_inner_enum_field));
    EXPECT_TRUE(edesc != NULL);
    EXPECT_STREQ(edesc->name().c_str(), expected_enum_name);
    EXPECT_EQ(typename MessageT::TestMessageEnum(edesc->number()),
        expected_enum);
    return true;
}

template<typename MessageT, typename InnerMessageT>
bool VerifyTestMessage(const Message *msg, const Descriptor *mdesc) {
    // Get the descriptors for the fields we're interested in and verify
    // their types.
    const FieldDescriptor* tm_name_field = mdesc->FindFieldByName("tm_name");
    EXPECT_TRUE(tm_name_field != NULL);
    EXPECT_TRUE(tm_name_field->type() == FieldDescriptor::TYPE_STRING);
    EXPECT_TRUE(tm_name_field->label() == FieldDescriptor::LABEL_REQUIRED);
    EXPECT_TRUE(tm_name_field->number() == 1);
    const FieldDescriptor* tm_status_field =
        mdesc->FindFieldByName("tm_status");
    EXPECT_TRUE(tm_status_field != NULL);
    EXPECT_TRUE(tm_status_field->type() == FieldDescriptor::TYPE_STRING);
    EXPECT_TRUE(tm_status_field->label() == FieldDescriptor::LABEL_OPTIONAL);
    EXPECT_TRUE(tm_status_field->number() == 2);
    const FieldDescriptor* tm_counter_field =
        mdesc->FindFieldByName("tm_counter");
    EXPECT_TRUE(tm_counter_field != NULL);
    EXPECT_TRUE(tm_counter_field->type() == FieldDescriptor::TYPE_INT32);
    EXPECT_TRUE(tm_counter_field->label() == FieldDescriptor::LABEL_OPTIONAL);
    EXPECT_TRUE(tm_counter_field->number() == 3);
    const FieldDescriptor* tm_enum_field =
        mdesc->FindFieldByName("tm_enum");
    EXPECT_TRUE(tm_enum_field != NULL);
    EXPECT_TRUE(tm_enum_field->type() == FieldDescriptor::TYPE_ENUM);
    EXPECT_TRUE(tm_enum_field->label() == FieldDescriptor::LABEL_OPTIONAL);
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
    EXPECT_TRUE(tm_inner_name_field->label() ==
        FieldDescriptor::LABEL_REQUIRED);
    EXPECT_TRUE(tm_inner_name_field->number() == 1);
    const FieldDescriptor* tm_inner_status_field =
        imdesc->FindFieldByName("tm_inner_status");
    EXPECT_TRUE(tm_inner_status_field != NULL);
    EXPECT_TRUE(tm_inner_status_field->type() == FieldDescriptor::TYPE_INT32);
    EXPECT_TRUE(tm_inner_status_field->label() ==
        FieldDescriptor::LABEL_OPTIONAL);
    EXPECT_TRUE(tm_inner_status_field->number() == 2);
    const FieldDescriptor* tm_inner_counter_field =
        imdesc->FindFieldByName("tm_inner_counter");
    EXPECT_TRUE(tm_inner_counter_field != NULL);
    EXPECT_TRUE(tm_inner_counter_field->type() == FieldDescriptor::TYPE_INT32);
    EXPECT_TRUE(tm_inner_counter_field->label() ==
        FieldDescriptor::LABEL_OPTIONAL);
    EXPECT_TRUE(tm_inner_counter_field->number() == 3);
    const FieldDescriptor* tm_inner_enum_field =
        imdesc->FindFieldByName("tm_inner_enum");
    EXPECT_TRUE(tm_inner_enum_field != NULL);
    EXPECT_TRUE(tm_inner_enum_field->type() == FieldDescriptor::TYPE_ENUM);
    EXPECT_TRUE(tm_inner_enum_field->label() ==
        FieldDescriptor::LABEL_OPTIONAL);
    EXPECT_TRUE(tm_inner_enum_field->number() == 4);
    // Use the reflection interface to examine the contents.
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
    const Message &inner_msg1(
        reflection->GetRepeatedMessage(*msg, tm_inner_field, 0));
    bool success(VerifyTestMessageInner<MessageT, InnerMessageT>(inner_msg1,
        1, MessageT::GOOD, "GOOD"));
    EXPECT_TRUE(success);
    const Message &inner_msg2(
        reflection->GetRepeatedMessage(*msg, tm_inner_field, 1));
    success = VerifyTestMessageInner<MessageT, InnerMessageT>(inner_msg2, 2,
        MessageT::BAD, "BAD");
    EXPECT_TRUE(success);
    return true;
}

// Populate the test message
template<typename MessageT, typename InnerMessageT>
void PopulateTestMessage(MessageT *test_message) {
    test_message->set_tm_name(TYPE_NAME(MessageT));
    test_message->set_tm_status("Test");
    test_message->set_tm_counter(3);
    test_message->set_tm_enum(MessageT::GOOD);
    InnerMessageT *test_message_inner = test_message->add_tm_inner();
    ASSERT_TRUE(test_message_inner != NULL);
    test_message_inner->set_tm_inner_name(TYPE_NAME(InnerMessageT) + "1");
    test_message_inner->set_tm_inner_status(1);
    test_message_inner->set_tm_inner_counter(1);
    test_message_inner->set_tm_inner_enum(MessageT::GOOD);
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

// Populate FileDescriptorSet
void PopulateFileDescriptorSet(FileDescriptorSet *fds, const char *desc_file) {
    std::ifstream desc(desc_file,
        std::ios_base::in | std::ios_base::binary);
    ASSERT_TRUE(desc.is_open());
    bool success = fds->ParseFromIstream(&desc);
    ASSERT_TRUE(success);
    desc.close();
}

// Populate empty FileDescriptorSet
void PopulateEmptyFileDescriptorSet(FileDescriptorSet *fds) {
    fds->add_file();
}

void CreateAndSerializeSelfDescribingMessageInternal(
    const FileDescriptorSet &fds,
    const std::string &message_name, uint8_t *output, size_t size,
    int *serialized_size, uint8_t *message_data, size_t message_data_size) {
    SelfDescribingMessage sdm_message;
    sdm_message.set_timestamp(123456789);
    sdm_message.mutable_proto_files()->CopyFrom(fds);
    sdm_message.set_type_name(message_name);
    sdm_message.set_message_data(message_data, message_data_size);
    int sdm_message_size = sdm_message.ByteSize();
    ASSERT_GE(size, sdm_message_size);
    *serialized_size = sdm_message_size;
    bool success = sdm_message.SerializeToArray(output, sdm_message_size);
    ASSERT_TRUE(success);
}

// Create SelfDescribingMessage for message_name and serialize it
void CreateAndSerializeSelfDescribingMessage(const std::string &message_name,
    uint8_t *output, size_t size, int *serialized_size, const char *desc_file,
    uint8_t *message_data, size_t message_data_size) {
    FileDescriptorSet fds;
    PopulateFileDescriptorSet(&fds, desc_file);
    CreateAndSerializeSelfDescribingMessageInternal(fds,
        message_name, output, size, serialized_size, message_data,
        message_data_size);
}

TEST_F(ProtobufReaderTest, Parse) {
    // Create TestMessage and serialize it
    boost::scoped_array<uint8_t> data(new uint8_t[kTestMessageBufferSize]);
    int serialized_data_size(0);
    CreateAndSerializeTestMessage(data.get(), kTestMessageBufferSize,
        &serialized_data_size);
    // Create SelfDescribingMessage for TestMessage and serialize it
    boost::scoped_array<uint8_t> sdm_data(
        new uint8_t[kSelfDescribingMessageBufferSize]);
    int serialized_sdm_data_size(0);
    CreateAndSerializeSelfDescribingMessage("TestMessage", sdm_data.get(),
        kSelfDescribingMessageBufferSize, &serialized_sdm_data_size,
        tm_desc_file_.c_str(), data.get(), (size_t) serialized_data_size);
    // Parse the SelfDescribingMessage from sdm_data to get TestMessage
    protobuf::impl::ProtobufReader reader;
    Message *msg = NULL;
    uint64_t timestamp;
    bool success = reader.ParseSelfDescribingMessage(sdm_data.get(),
        serialized_sdm_data_size, &timestamp, &msg, NULL);
    ASSERT_TRUE(success);
    ASSERT_TRUE(msg != NULL);
    EXPECT_EQ(123456789, timestamp);
    const Descriptor *mdesc = msg->GetDescriptor();
    ASSERT_TRUE(mdesc != NULL);
    success = VerifyTestMessage<TestMessage, TestMessageInner>(msg, mdesc);
    EXPECT_TRUE(success);
    delete msg;
}

void PopulateTestMessageBaseExtension(int extension,
    TestMessageBase *base_message) {
    TestMessageExtension1 *test_message_ext10;
    TestMessageExtension2 *test_message_ext20;
    switch (extension) {
    case 10:
        test_message_ext10 =
            base_message->MutableExtension(tmb_extension10);
        ASSERT_TRUE(test_message_ext10 != NULL);
        PopulateTestMessage<TestMessageExtension1,
            TestMessageExtension1Inner>(test_message_ext10);
        break;
    case 20:
        test_message_ext20 =
            base_message->MutableExtension(tmb_extension20);
        ASSERT_TRUE(test_message_ext20 != NULL);
        PopulateTestMessage<TestMessageExtension2,
            TestMessageExtension2Inner>(test_message_ext20);
        break;
    default:
        ASSERT_TRUE(0);
        break;
    }
}

// Create TestMessageBase and serialize it
void CreateAndSerializeTestMessageBase(uint8_t *output,
    size_t size, int *serialized_size) {
    TestMessageBase base_message;
    base_message.set_tmb_name("TestMessageBase");
    base_message.set_tmb_status("Test");
    base_message.set_tmb_counter(3);
    base_message.set_tmb_enum(TestMessageBase::GOOD);
    PopulateTestMessageBaseExtension(10, &base_message);
    PopulateTestMessageBaseExtension(20, &base_message);
    int base_message_size = base_message.ByteSize();
    ASSERT_GE(size, base_message_size);
    *serialized_size += base_message_size;
    bool success = base_message.SerializeToArray(output, base_message_size);
    ASSERT_TRUE(success);
}

template<typename MessageT, typename InnerMessageT>
bool VerifyTestMessageBaseExtension(int extension, const Message *msg,
    const Descriptor *mdesc) {
    std::string extension_name_prefix("tmb_extension");
    std::stringstream ss;
    ss << extension_name_prefix << extension;
    std::string extension_name(ss.str());
    // Use the reflection interface to examine the contents.
    const Reflection* reflection = msg->GetReflection();
    const FieldDescriptor* reflect_tmb_extension_field =
        reflection->FindKnownExtensionByName(extension_name);
    EXPECT_TRUE(reflect_tmb_extension_field != NULL);
    // Get the descriptors for the fields we're interested in and verify
    // their types.
    const DescriptorPool *pool(mdesc->file()->pool());
    EXPECT_TRUE(pool != NULL);
    const FieldDescriptor* tmb_extension_field =
        pool->FindExtensionByName(extension_name);
    EXPECT_TRUE(tmb_extension_field != NULL);
    EXPECT_TRUE(tmb_extension_field->type() == FieldDescriptor::TYPE_MESSAGE);
    EXPECT_TRUE(tmb_extension_field->label() ==
        FieldDescriptor::LABEL_OPTIONAL);
    EXPECT_TRUE(tmb_extension_field->number() == extension);
    EXPECT_TRUE(tmb_extension_field == reflect_tmb_extension_field);
    const Message &extension_msg(
        reflection->GetMessage(*msg, tmb_extension_field));
    const Descriptor *extension_mdesc = extension_msg.GetDescriptor();
    EXPECT_TRUE(extension_mdesc != NULL);
    bool success = VerifyTestMessage<MessageT, InnerMessageT>(
        &extension_msg, extension_mdesc);
    EXPECT_TRUE(success);
    return success;
}

bool VerifyTestMessageBase(const Message *msg, const Descriptor *mdesc) {
    // Get the descriptors for the fields we're interested in and verify
    // their types.
    const FieldDescriptor* tmb_name_field = mdesc->FindFieldByName("tmb_name");
    EXPECT_TRUE(tmb_name_field != NULL);
    EXPECT_TRUE(tmb_name_field->type() == FieldDescriptor::TYPE_STRING);
    EXPECT_TRUE(tmb_name_field->label() == FieldDescriptor::LABEL_REQUIRED);
    EXPECT_TRUE(tmb_name_field->number() == 1);
    const FieldDescriptor* tmb_status_field =
        mdesc->FindFieldByName("tmb_status");
    EXPECT_TRUE(tmb_status_field != NULL);
    EXPECT_TRUE(tmb_status_field->type() == FieldDescriptor::TYPE_STRING);
    EXPECT_TRUE(tmb_status_field->label() == FieldDescriptor::LABEL_OPTIONAL);
    EXPECT_TRUE(tmb_status_field->number() == 2);
    const FieldDescriptor* tmb_counter_field =
        mdesc->FindFieldByName("tmb_counter");
    EXPECT_TRUE(tmb_counter_field != NULL);
    EXPECT_TRUE(tmb_counter_field->type() == FieldDescriptor::TYPE_INT32);
    EXPECT_TRUE(tmb_counter_field->label() == FieldDescriptor::LABEL_OPTIONAL);
    EXPECT_TRUE(tmb_counter_field->number() == 3);
    const FieldDescriptor* tmb_enum_field =
        mdesc->FindFieldByName("tmb_enum");
    EXPECT_TRUE(tmb_enum_field != NULL);
    EXPECT_TRUE(tmb_enum_field->type() == FieldDescriptor::TYPE_ENUM);
    EXPECT_TRUE(tmb_enum_field->label() == FieldDescriptor::LABEL_OPTIONAL);
    EXPECT_TRUE(tmb_enum_field->number() == 4);
    // Use the reflection interface to examine the contents.
    const Reflection* reflection = msg->GetReflection();
    EXPECT_TRUE(reflection->GetString(*msg, tmb_name_field) ==
        "TestMessageBase");
    EXPECT_TRUE(reflection->GetString(*msg, tmb_status_field) == "Test");
    EXPECT_TRUE(reflection->GetInt32(*msg, tmb_counter_field) == 3);
    const EnumValueDescriptor *edesc(reflection->GetEnum(*msg,
        tmb_enum_field));
    EXPECT_TRUE(edesc != NULL);
    EXPECT_STREQ(edesc->name().c_str(), "GOOD");
    EXPECT_EQ(TestMessageBase::TestMessageEnum(edesc->number()),
        TestMessageBase::GOOD);
    bool success(VerifyTestMessageBaseExtension<TestMessageExtension1,
        TestMessageExtension1Inner>(10, msg, mdesc));
    EXPECT_TRUE(success);
    success = VerifyTestMessageBaseExtension<TestMessageExtension2,
        TestMessageExtension2Inner>(20, msg, mdesc);
    EXPECT_TRUE(success);
    return success;
}

TEST_F(ProtobufReaderTest, ParseMessageExtensions) {
    // Create TestMessageBase with extensions and serialize it
    boost::scoped_array<uint8_t> data(new uint8_t[kTestMessageBufferSize]);
    int serialized_data_size(0);
    CreateAndSerializeTestMessageBase(data.get(), kTestMessageBufferSize,
        &serialized_data_size);
    // Create SelfDescribingMessage for TestMessageBase and serialize it
    boost::scoped_array<uint8_t> sdm_data(
        new uint8_t[kSelfDescribingMessageBufferSize]);
    int serialized_sdm_data_size(0);
    CreateAndSerializeSelfDescribingMessage("TestMessageBase", sdm_data.get(),
        kSelfDescribingMessageBufferSize, &serialized_sdm_data_size,
        tme_desc_file_.c_str(), data.get(), (size_t) serialized_data_size);
    // Parse the SelfDescribingMessage from sdm_data to get TestMessageBase
    protobuf::impl::ProtobufReader reader;
    Message *msg = NULL;
    uint64_t timestamp;
    bool success = reader.ParseSelfDescribingMessage(sdm_data.get(),
        serialized_sdm_data_size, &timestamp, &msg, NULL);
    ASSERT_TRUE(success);
    ASSERT_TRUE(msg != NULL);
    EXPECT_EQ(123456789, timestamp);
    const Descriptor *mdesc = msg->GetDescriptor();
    ASSERT_TRUE(mdesc != NULL);
    EXPECT_TRUE(VerifyTestMessageBase(msg, mdesc));
    delete msg;
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
    // Create SelfDescribingMessage for TestMessage and serialize it
    boost::scoped_array<uint8_t> sdm_data(
        new uint8_t[kSelfDescribingMessageBufferSize]);
    int serialized_sdm_data_size(0);
    // FAILURE 1
    // Change message name to fail parsing
    CreateAndSerializeSelfDescribingMessage("TestMessageFail", sdm_data.get(),
        kSelfDescribingMessageBufferSize, &serialized_sdm_data_size,
        tm_desc_file_.c_str(), data.get(), (size_t) serialized_data_size);
    // Parse the SelfDescribingMessage from sdm_data to get TestMessage
    protobuf::impl::ProtobufReader reader;
    Message *msg = NULL;
    uint64_t timestamp;
    bool success = reader.ParseSelfDescribingMessage(sdm_data.get(),
        serialized_sdm_data_size, &timestamp, &msg,
        ProtobufReaderTestParseFailure);
    ASSERT_FALSE(success);
    ASSERT_TRUE(msg == NULL);
    // Create SelfDescribingMessage for TestMessage and serialize it
    sdm_data.reset(new uint8_t[kSelfDescribingMessageBufferSize]);
    serialized_sdm_data_size = 0;
    CreateAndSerializeSelfDescribingMessage("TestMessage", sdm_data.get(),
        kSelfDescribingMessageBufferSize, &serialized_sdm_data_size,
        tm_desc_file_.c_str(), data.get(), (size_t) serialized_data_size);
    // FAILURE 2
    // Change one byte in the serialized SelfDescribingMessage to fail parsing
    uint8_t *sdm_data_start(sdm_data.get());
    *sdm_data_start = 0;
    success = reader.ParseSelfDescribingMessage(sdm_data.get(),
        serialized_sdm_data_size, &timestamp, &msg,
        ProtobufReaderTestParseFailure);
    ASSERT_FALSE(success);
    ASSERT_TRUE(msg == NULL);
    // Create SelfDescribingMessage for TestMessage and serialize it
    sdm_data.reset(new uint8_t[kSelfDescribingMessageBufferSize]);
    serialized_sdm_data_size = 0;
    // FAILURE 3
    // Change one byte in the serialized TestMessage to fail parsing
    uint8_t *data_start(data.get());
    *data_start = 0;
    CreateAndSerializeSelfDescribingMessage("TestMessage", sdm_data.get(),
        kSelfDescribingMessageBufferSize, &serialized_sdm_data_size,
        tm_desc_file_.c_str(), data.get(), (size_t) serialized_data_size);
    success = reader.ParseSelfDescribingMessage(sdm_data.get(),
        serialized_sdm_data_size, &timestamp, &msg,
        ProtobufReaderTestParseFailure);
    ASSERT_FALSE(success);
    ASSERT_TRUE(msg == NULL);
    // Create TestMessage and serialize it
    data.reset(new uint8_t[kTestMessageBufferSize]);
    serialized_data_size = 0;
    CreateAndSerializeTestMessage(data.get(), kTestMessageBufferSize,
        &serialized_data_size);
    // Create SelfDescribingMessage for TestMessage and serialize it
    sdm_data.reset(new uint8_t[kSelfDescribingMessageBufferSize]);
    serialized_sdm_data_size = 0;
    FileDescriptorSet fds;
    // FAILURE 4
    // Populate empty FileDescriptorSet/Proto
    PopulateEmptyFileDescriptorSet(&fds);
    CreateAndSerializeSelfDescribingMessageInternal(fds,
        "TestMessage", sdm_data.get(), kSelfDescribingMessageBufferSize,
        &serialized_sdm_data_size, data.get(), (size_t) serialized_data_size);
    success = reader.ParseSelfDescribingMessage(sdm_data.get(),
        serialized_sdm_data_size, &timestamp, &msg,
        ProtobufReaderTestParseFailure);
    ASSERT_FALSE(success);
    ASSERT_TRUE(msg == NULL);
    // Create TestMessage and serialize it
    data.reset(new uint8_t[kTestMessageBufferSize]);
    serialized_data_size = 0;
    CreateAndSerializeTestMessage(data.get(), kTestMessageBufferSize,
        &serialized_data_size);
    // Create SelfDescribingMessage for TestMessage and serialize it
    sdm_data.reset(new uint8_t[kSelfDescribingMessageBufferSize]);
    serialized_sdm_data_size = 0;
    CreateAndSerializeSelfDescribingMessage("TestMessage", sdm_data.get(),
        kSelfDescribingMessageBufferSize, &serialized_sdm_data_size,
        tm_desc_file_.c_str(), data.get(), (size_t) serialized_data_size);
    // FAILURE 5
    // Override GetPrototype to return NULL prototype message
    MockProtobufReader mreader;
    success = mreader.ParseSelfDescribingMessage(sdm_data.get(),
        serialized_sdm_data_size, &timestamp, &msg,
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
    a1.statAttr = string("tm_inner");
    a1.attribs = map_list_of
        ("Source", DbHandler::Var("127.0.0.1"))
        ("tm_name", DbHandler::Var("TestMessage"))
        ("tm_status", DbHandler::Var("Test"))
        ("tm_counter", DbHandler::Var((uint64_t)3))
        ("tm_enum", DbHandler::Var("GOOD"))
        ("tm_inner.tm_inner_name", DbHandler::Var("TestMessageInner1"))
        ("tm_inner.tm_inner_status", DbHandler::Var((uint64_t)1))
        ("tm_inner.tm_inner_counter", DbHandler::Var((uint64_t)1))
        ("tm_inner.tm_inner_enum", DbHandler::Var("GOOD"));

    DbHandler::AttribMap sm;
    a1.attribs_tag.insert(make_pair("Source", make_pair(
        DbHandler::Var("127.0.0.1"), sm)));
    a1.attribs_tag.insert(make_pair("tm_name", make_pair(
        DbHandler::Var("TestMessage"), sm)));
    a1.attribs_tag.insert(make_pair("tm_status", make_pair(
        DbHandler::Var("Test"), sm)));
    a1.attribs_tag.insert(make_pair("tm_counter", make_pair(
        DbHandler::Var((uint64_t)3), sm)));
    a1.attribs_tag.insert(make_pair("tm_enum", make_pair(
        DbHandler::Var("GOOD"), sm)));
    a1.attribs_tag.insert(make_pair("tm_inner.tm_inner_name",
        make_pair(DbHandler::Var("TestMessageInner1"), sm)));
    a1.attribs_tag.insert(make_pair("tm_inner.tm_inner_enum",
        make_pair(DbHandler::Var("GOOD"), sm)));
    av.push_back(a1);

    ArgSet a2;
    a2.statAttr = string("tm_inner");
    a2.attribs = map_list_of
        ("Source", DbHandler::Var("127.0.0.1"))
        ("tm_name", DbHandler::Var("TestMessage"))
        ("tm_status", DbHandler::Var("Test"))
        ("tm_counter", DbHandler::Var((uint64_t)3))
        ("tm_enum", DbHandler::Var("GOOD"))
        ("tm_inner.tm_inner_name", DbHandler::Var("TestMessageInner2"))
        ("tm_inner.tm_inner_status", DbHandler::Var((uint64_t)2))
        ("tm_inner.tm_inner_counter", DbHandler::Var((uint64_t)2))
        ("tm_inner.tm_inner_enum", DbHandler::Var("BAD"));

    a2.attribs_tag.insert(make_pair("Source", make_pair(
        DbHandler::Var("127.0.0.1"), sm)));
    a2.attribs_tag.insert(make_pair("tm_name", make_pair(
        DbHandler::Var("TestMessage"), sm)));
    a2.attribs_tag.insert(make_pair("tm_status", make_pair(
        DbHandler::Var("Test"), sm)));
    a2.attribs_tag.insert(make_pair("tm_counter", make_pair(
        DbHandler::Var((uint64_t)3), sm)));
    a2.attribs_tag.insert(make_pair("tm_enum", make_pair(
        DbHandler::Var("GOOD"), sm)));
    a2.attribs_tag.insert(make_pair("tm_inner.tm_inner_name",
        make_pair(DbHandler::Var("TestMessageInner2"), sm)));
    a2.attribs_tag.insert(make_pair("tm_inner.tm_inner_enum",
        make_pair(DbHandler::Var("BAD"), sm)));
    av.push_back(a2);
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

vector<ArgSet> PopulateTestMessageBaseStatsInfo() {
    vector<ArgSet> av;

    ArgSet a1;
    a1.statAttr = string("tmb_extension10");
    PopulateTestMessageBaseStatsAttribs<TestMessageExtension1>(&a1.attribs,
        10);
    PopulateTestMessageBaseStatsAttribsTag<TestMessageExtension1>(
        &a1.attribs_tag, 10);
    av.push_back(a1);

    ArgSet a2;
    a2.statAttr = string("tmb_extension10.tm_inner");
    PopulateTestMessageExtensionStatsAttribs<TestMessageExtension1,
        TestMessageExtension1Inner>(&a2.attribs, 10, 1, 1, "GOOD");
    PopulateTestMessageExtensionStatsAttribsTag<TestMessageExtension1,
        TestMessageExtension1Inner>(&a2.attribs_tag, 10, 1, "GOOD");
    av.push_back(a2);

    ArgSet a3;
    a3.statAttr = string("tmb_extension10.tm_inner");
    PopulateTestMessageExtensionStatsAttribs<TestMessageExtension1,
        TestMessageExtension1Inner>(&a3.attribs, 10, 2, 2, "BAD");
    PopulateTestMessageExtensionStatsAttribsTag<TestMessageExtension1,
        TestMessageExtension1Inner>(&a3.attribs_tag, 10, 2, "BAD");
    av.push_back(a3);

    ArgSet a4;
    a4.statAttr = string("tmb_extension20");
    PopulateTestMessageBaseStatsAttribs<TestMessageExtension2>(&a4.attribs,
        20);
    PopulateTestMessageBaseStatsAttribsTag<TestMessageExtension2>(
        &a4.attribs_tag, 20);
    av.push_back(a4);

    ArgSet a5;
    a5.statAttr = string("tmb_extension20.tm_inner");
    PopulateTestMessageExtensionStatsAttribs<TestMessageExtension2,
        TestMessageExtension2Inner>(&a5.attribs, 20, 1, 1, "GOOD");
    PopulateTestMessageExtensionStatsAttribsTag<TestMessageExtension2,
        TestMessageExtension2Inner>(&a5.attribs_tag, 20, 1, "GOOD");
    av.push_back(a5);

    ArgSet a6;
    a6.statAttr = string("tmb_extension20.tm_inner");
    PopulateTestMessageExtensionStatsAttribs<TestMessageExtension2,
        TestMessageExtension2Inner>(&a6.attribs, 20, 2, 2, "BAD");
    PopulateTestMessageExtensionStatsAttribsTag<TestMessageExtension2,
        TestMessageExtension2Inner>(&a6.attribs_tag, 20, 2, "BAD");
    av.push_back(a6);

    return av;
}

void PopulateTestMessageAllTypesAttribs(DbHandler::AttribMap *attribs,
    const std::string &statAttr) {
    attribs->insert(make_pair("Source", DbHandler::Var("127.0.0.1")));
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

void PopulateTestMessageAllTypesAttribTags(DbHandler::TagMap *attribs_tag,
    const std::string &statAttr) {
    DbHandler::AttribMap sm;
    attribs_tag->insert(make_pair("Source", make_pair(
        DbHandler::Var("127.0.0.1"), sm)));
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
    attribs_tag->insert(make_pair(statAttr + ".tmp_inner_string",
        make_pair(DbHandler::Var("TestMessageAllTypesInner"), sm)));
    attribs_tag->insert(make_pair(statAttr + ".tmp_inner_enum",
        make_pair(DbHandler::Var("BAD"), sm)));
}

vector<ArgSet> PopulateTestMessageAllTypesStatsInfo() {
    vector<ArgSet> av;
    ArgSet a1;
    a1.statAttr = string("tmp_inner");
    PopulateTestMessageAllTypesAttribs(&a1.attribs, a1.statAttr);
    PopulateTestMessageAllTypesAttribTags(&a1.attribs_tag, a1.statAttr);
    av.push_back(a1);

    ArgSet a2;
    a2.statAttr = string("tmp_message");
    PopulateTestMessageAllTypesAttribs(&a2.attribs, a2.statAttr);
    PopulateTestMessageAllTypesAttribTags(&a2.attribs_tag, a2.statAttr);
    av.push_back(a2);

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
    // Create SelfDescribingMessage for TestMessage and serialize it
    boost::scoped_array<uint8_t> sdm_data(
        new uint8_t[kSelfDescribingMessageBufferSize]);
    int serialized_sdm_data_size(0);
    CreateAndSerializeSelfDescribingMessage("TestMessage", sdm_data.get(),
        kSelfDescribingMessageBufferSize, &serialized_sdm_data_size,
        tm_desc_file_.c_str(), data.get(), (size_t) serialized_data_size);
    // Parse the SelfDescribingMessage from sdm_data to get TestMessage
    protobuf::impl::ProtobufReader reader;
    Message *msg = NULL;
    uint64_t timestamp;
    bool success = reader.ParseSelfDescribingMessage(sdm_data.get(),
        serialized_sdm_data_size, &timestamp, &msg, NULL);
    ASSERT_TRUE(success);
    ASSERT_TRUE(msg != NULL);

    boost::system::error_code ec;
    boost::asio::ip::address raddr(
        boost::asio::ip::address::from_string("127.0.0.1", ec));
    boost::asio::ip::udp::endpoint rep(raddr, 0);
    protobuf::impl::ProcessProtobufMessage(*msg, timestamp, rep,
        boost::bind(&StatCbTester::Cb, &ct, _1, _2, _3, _4, _5));
    delete msg;
}

TEST_F(ProtobufStatWalkerTest, Extensions) {
    StatCbTester ct(PopulateTestMessageBaseStatsInfo());
    // Create TestMessageBase with extensions and serialize it
    boost::scoped_array<uint8_t> data(new uint8_t[kTestMessageBufferSize]);
    int serialized_data_size(0);
    CreateAndSerializeTestMessageBase(data.get(), kTestMessageBufferSize,
        &serialized_data_size);
    // Create SelfDescribingMessage for TestMessageBase and serialize it
    boost::scoped_array<uint8_t> sdm_data(
        new uint8_t[kSelfDescribingMessageBufferSize]);
    int serialized_sdm_data_size(0);
    CreateAndSerializeSelfDescribingMessage("TestMessageBase", sdm_data.get(),
        kSelfDescribingMessageBufferSize, &serialized_sdm_data_size,
        tme_desc_file_.c_str(), data.get(), (size_t) serialized_data_size);
    // Parse the SelfDescribingMessage from sdm_data to get TestMessageBase
    protobuf::impl::ProtobufReader reader;
    Message *msg = NULL;
    uint64_t timestamp;
    bool success = reader.ParseSelfDescribingMessage(sdm_data.get(),
        serialized_sdm_data_size, &timestamp, &msg, NULL);
    ASSERT_TRUE(success);
    ASSERT_TRUE(msg != NULL);

    boost::system::error_code ec;
    boost::asio::ip::address raddr(
        boost::asio::ip::address::from_string("127.0.0.1", ec));
    boost::asio::ip::udp::endpoint rep(raddr, 0);
    protobuf::impl::ProcessProtobufMessage(*msg, timestamp, rep,
        boost::bind(&StatCbTester::Cb, &ct, _1, _2, _3, _4, _5));
    delete msg;
}

// Create TestMessageBaseStream and serialize it
void CreateAndSerializeTestMessageBaseStream(uint8_t *output, size_t size,
    int *serialized_size) {
    TestMessageBaseStream base_message;
    base_message.set_tmbs_name("TestMessageBaseStream");
    TestMessageBaseSensors *tmbs_message(base_message.mutable_tmbs_sensor());
    ASSERT_TRUE(tmbs_message != NULL);
    TestMessageContrailSensors *tmcs_message(
        tmbs_message->mutable_tmbs_sensor());
    ASSERT_TRUE(tmcs_message != NULL);
    TestMessageContrailSensorsExtension1 *tmcse1_message(
        tmcs_message->MutableExtension(tmcs_extension1));
    ASSERT_TRUE(tmcse1_message != NULL);
    tmcse1_message->set_tmcse1_name("TestMessageContrailSensorsExtension1");
    tmcse1_message->set_tmcse1_counter(1);
    TestMessageContrailSensorsExtension1Inner *tmcse1_inner_message(
        tmcse1_message->add_tmcse1_inner());
    ASSERT_TRUE(tmcse1_inner_message != NULL);
    tmcse1_inner_message->set_tmcse1_inner_name(
        "TestMessageContrailSensorsExtension1Inner");
    tmcse1_inner_message->set_tmcse1_inner_counter(2);
    int base_message_size = base_message.ByteSize();
    ASSERT_GE(size, base_message_size);
    *serialized_size = base_message_size;
    bool success = base_message.SerializeToArray(output, base_message_size);
    ASSERT_TRUE(success);
}

vector<ArgSet> PopulateTestMessageBaseStreamStatsInfo() {
    vector<ArgSet> av;

    ArgSet a1;
    a1.statAttr = string("tmbs_sensor");
    a1.attribs = map_list_of
        ("Source", DbHandler::Var("127.0.0.1"))
        ("tmbs_name", DbHandler::Var("TestMessageBaseStream"));

    DbHandler::AttribMap sm;
    a1.attribs_tag.insert(make_pair("Source", make_pair(
        DbHandler::Var("127.0.0.1"), sm)));
    a1.attribs_tag.insert(make_pair("tmbs_name", make_pair(
        DbHandler::Var("TestMessageBaseStream"), sm)));
    av.push_back(a1);

    ArgSet a2;
    a2.statAttr = string("tmbs_sensor.tmbs_sensor");
    a2.attribs = map_list_of
        ("Source", DbHandler::Var("127.0.0.1"))
        ("tmbs_name", DbHandler::Var("TestMessageBaseStream"));

    a2.attribs_tag.insert(make_pair("Source", make_pair(
        DbHandler::Var("127.0.0.1"), sm)));
    a2.attribs_tag.insert(make_pair("tmbs_name", make_pair(
        DbHandler::Var("TestMessageBaseStream"), sm)));
    av.push_back(a2);

    ArgSet a3;
    a3.statAttr = string("tmbs_sensor.tmbs_sensor.tmcs_extension1");
    a3.attribs = map_list_of
        ("Source", DbHandler::Var("127.0.0.1"))
        ("tmbs_name", DbHandler::Var("TestMessageBaseStream"))
        ("tmbs_sensor.tmbs_sensor.tmcs_extension1.tmcse1_name",
            DbHandler::Var("TestMessageContrailSensorsExtension1"))
        ("tmbs_sensor.tmbs_sensor.tmcs_extension1.tmcse1_counter",
            DbHandler::Var((uint64_t)1));

    a3.attribs_tag.insert(make_pair("Source", make_pair(
        DbHandler::Var("127.0.0.1"), sm)));
    a3.attribs_tag.insert(make_pair("tmbs_name", make_pair(
        DbHandler::Var("TestMessageBaseStream"), sm)));
    a3.attribs_tag.insert(make_pair(
        "tmbs_sensor.tmbs_sensor.tmcs_extension1.tmcse1_name", make_pair(
        DbHandler::Var("TestMessageContrailSensorsExtension1"), sm)));
    av.push_back(a3);

    ArgSet a4;
    a4.statAttr =
        string("tmbs_sensor.tmbs_sensor.tmcs_extension1.tmcse1_inner");
    a4.attribs = map_list_of
    ("Source", DbHandler::Var("127.0.0.1"))
    ("tmbs_name", DbHandler::Var("TestMessageBaseStream"))
    ("tmbs_sensor.tmbs_sensor.tmcs_extension1.tmcse1_name",
        DbHandler::Var("TestMessageContrailSensorsExtension1"))
    ("tmbs_sensor.tmbs_sensor.tmcs_extension1.tmcse1_inner.tmcse1_inner_name",
        DbHandler::Var("TestMessageContrailSensorsExtension1Inner"))
    ("tmbs_sensor.tmbs_sensor.tmcs_extension1.tmcse1_inner.tmcse1_inner_counter",
        DbHandler::Var((uint64_t)2));

    a4.attribs_tag.insert(make_pair("Source", make_pair(
        DbHandler::Var("127.0.0.1"), sm)));
    a4.attribs_tag.insert(make_pair("tmbs_name", make_pair(
        DbHandler::Var("TestMessageBaseStream"), sm)));
    a4.attribs_tag.insert(make_pair(
        "tmbs_sensor.tmbs_sensor.tmcs_extension1.tmcse1_name", make_pair(
        DbHandler::Var("TestMessageContrailSensorsExtension1"), sm)));
    a4.attribs_tag.insert(make_pair(
        "tmbs_sensor.tmbs_sensor.tmcs_extension1.tmcse1_inner.tmcse1_inner_name",
        make_pair(DbHandler::Var("TestMessageContrailSensorsExtension1Inner"),
        sm)));
    av.push_back(a4);

    return av;
}

TEST_F(ProtobufStatWalkerTest, ExtensionsInnerMessage) {
    StatCbTester ct(PopulateTestMessageBaseStreamStatsInfo());
    // Create TestMessageBaseStream with extension and serialize it
    boost::scoped_array<uint8_t> data(new uint8_t[kTestMessageBufferSize]);
    int serialized_data_size(0);
    CreateAndSerializeTestMessageBaseStream(data.get(), kTestMessageBufferSize,
        &serialized_data_size);
    // Create SelfDescribingMessage for TestMessageBaseStream and serialize it
    boost::scoped_array<uint8_t> sdm_data(
        new uint8_t[kSelfDescribingMessageBufferSize]);
    int serialized_sdm_data_size(0);
    CreateAndSerializeSelfDescribingMessage("TestMessageBaseStream",
        sdm_data.get(), kSelfDescribingMessageBufferSize,
        &serialized_sdm_data_size, tme_desc_file_.c_str(),
        data.get(), (size_t) serialized_data_size);
    // Parse the SelfDescribingMessage from sdm_data to get
    // TestMessageBaseStream
    protobuf::impl::ProtobufReader reader;
    Message *msg = NULL;
    uint64_t timestamp;
    bool success = reader.ParseSelfDescribingMessage(sdm_data.get(),
        serialized_sdm_data_size, &timestamp, &msg, NULL);
    ASSERT_TRUE(success);
    ASSERT_TRUE(msg != NULL);

    boost::system::error_code ec;
    boost::asio::ip::address raddr(
        boost::asio::ip::address::from_string("127.0.0.1", ec));
    boost::asio::ip::udp::endpoint rep(raddr, 0);
    protobuf::impl::ProcessProtobufMessage(*msg, timestamp, rep,
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
    // Create SelfDescribingMessage for TestMessageAllTyes and serialize it
    boost::scoped_array<uint8_t> sdm_data(
        new uint8_t[kSelfDescribingMessageBufferSize]);
    int serialized_sdm_data_size(0);
    CreateAndSerializeSelfDescribingMessage("TestMessageAllTypes",
        sdm_data.get(), kSelfDescribingMessageBufferSize,
        &serialized_sdm_data_size, tm_desc_file_.c_str(),
        data.get(), (size_t) serialized_data_size);
    // Parse the SelfDescribingMessage from sdm_data to get TestMessageAllTypes
    protobuf::impl::ProtobufReader reader;
    Message *msg = NULL;
    uint64_t timestamp;
    bool success = reader.ParseSelfDescribingMessage(sdm_data.get(),
        serialized_sdm_data_size, &timestamp, &msg, NULL);
    ASSERT_TRUE(success);
    ASSERT_TRUE(msg != NULL);

    boost::system::error_code ec;
    boost::asio::ip::address raddr(
        boost::asio::ip::address::from_string("127.0.0.1", ec));
    boost::asio::ip::udp::endpoint rep(raddr, 0);
    protobuf::impl::ProcessProtobufMessage(*msg, timestamp, rep,
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
    std::unique_ptr<ServerThread> thread_;
    std::unique_ptr<protobuf::ProtobufServer> server_;
    ProtobufMockClient *client_;
    std::unique_ptr<EventManager> evm_;
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
    // Create SelfDescribingMessage for TestMessage and serialize it
    boost::scoped_array<uint8_t> sdm_data(
        new uint8_t[kSelfDescribingMessageBufferSize]);
    int serialized_sdm_data_size(0);
    CreateAndSerializeSelfDescribingMessage("TestMessage", sdm_data.get(),
        kSelfDescribingMessageBufferSize, &serialized_sdm_data_size,
        tm_desc_file_.c_str(), data.get(), (size_t) serialized_data_size);
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
        client_endpoint, "TestMessage", 1, serialized_sdm_data_size, 0));

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
        client_endpoint, "TestMessage", 1, serialized_sdm_data_size, 0));
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

    std::unique_ptr<ServerThread> thread_;
    std::unique_ptr<protobuf::ProtobufServer> server_;
    ProtobufMockClient *client_;
    std::unique_ptr<EventManager> evm_;
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
    // Create SelfDescribingMessage for TestMessageSize and serialize it
    boost::scoped_array<uint8_t> sdm_data(
        new uint8_t[kSelfDescribingMessageSizeBufferSize]);
    int serialized_sdm_data_size(0);
    CreateAndSerializeSelfDescribingMessage("TestMessageSize", sdm_data.get(),
        kSelfDescribingMessageSizeBufferSize, &serialized_sdm_data_size,
        tm_desc_file_.c_str(), data.get(), (size_t) serialized_data_size);
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
    // Create SelfDescribingMessage for TestMessageSize with type name
    // wrongly set to TestMessageSizeWrong and serialize it, so that the
    // server drops it when parsing
    boost::scoped_array<uint8_t> sdm_data(
        new uint8_t[kSelfDescribingMessageBufferSize]);
    int serialized_sdm_data_size(0);
    CreateAndSerializeSelfDescribingMessage("TestMessageSizeWrong",
        sdm_data.get(), kSelfDescribingMessageBufferSize,
        &serialized_sdm_data_size, tm_desc_file_.c_str(), data.get(),
        (size_t) serialized_data_size);
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
    EXPECT_EQ(0, a_rx_msg_stats.get_messages());
    EXPECT_EQ(1, a_rx_msg_stats.get_errors());
    EXPECT_EQ(0, a_rx_msg_stats.get_bytes());
    EXPECT_EQ("TestMessageSizeWrong", a_rx_msg_stats.get_message_name());
}
}  // namespace

int main(int argc, char **argv) {
    char *top_obj_dir = getenv("TOP_OBJECT_PATH");
    if (top_obj_dir) {
        tm_desc_file_ = std::string(top_obj_dir) +
            "/analytics/test/test_message.desc";
        tme_desc_file_ = std::string(top_obj_dir) +
            "/analytics/test/test_message_extensions.desc";
    }
    ::testing::InitGoogleTest(&argc, argv);
    LoggingInit();
    int result = RUN_ALL_TESTS();
    return result;
}
