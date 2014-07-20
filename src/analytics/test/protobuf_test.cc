/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#include <fstream>

#include <testing/gunit.h>

#include <google/protobuf/descriptor.h>
#include <google/protobuf/descriptor.pb.h>
#include <google/protobuf/message.h>
#include <google/protobuf/dynamic_message.h>

#include <base/logging.h>

#include "test_message.pb.h"
#include "self_describing_message_test.pb.h"

using namespace ::google::protobuf;

static std::string d_desc_file_ = "build/debug/analytics/test/test_message.desc";

class ProtobufTest : public ::testing::Test {
protected:
    bool VerifyTestMessageInner(const Message &inner_msg, int expected_status) {
        const Descriptor *inner_msg_desc = inner_msg.GetDescriptor();
        const Reflection *inner_reflection = inner_msg.GetReflection();
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
        EXPECT_TRUE(inner_reflection->GetString(
            inner_msg, tm_inner_name_field) == "TestMessageInner");
        EXPECT_TRUE(inner_reflection->GetInt32(
            inner_msg, tm_inner_status_field) == expected_status);
        return true;
    }
};

TEST_F(ProtobufTest, SelfDescribingMessageTest) {
    uint8_t data[512];
    // Create TestMessage and serialize it
    TestMessage test_message;
    test_message.set_tm_name("TestMessage");
    test_message.set_tm_status("Test");
    TestMessageInner *test_message_inner = test_message.add_tm_inner();
    ASSERT_TRUE(test_message_inner != NULL);
    test_message_inner->set_tm_inner_name("TestMessageInner");
    test_message_inner->set_tm_inner_status(1);
    test_message_inner = test_message.add_tm_inner();
    test_message_inner->set_tm_inner_name("TestMessageInner");
    test_message_inner->set_tm_inner_status(2);
    
    int test_message_size = test_message.ByteSize();
    bool success = test_message.SerializeToArray(data, test_message_size);
    ASSERT_TRUE(success);
    // Create SelfDescribingMessageTest for TestMessage and serialize it
    std::ifstream desc(d_desc_file_.c_str(), 
        std::ios_base::in | std::ios_base::binary); 
    ASSERT_TRUE(desc.is_open());
    FileDescriptorSet fds;
    success = fds.ParseFromIstream(&desc);
    ASSERT_TRUE(success);
    desc.close();
    SelfDescribingMessageTest sdmt_message;
    sdmt_message.mutable_proto_files()->CopyFrom(fds);
    sdmt_message.set_type_name("TestMessage");
    sdmt_message.set_message_data(data, test_message_size);
    uint8_t sdm_data[512];
    int sdmt_message_size = sdmt_message.ByteSize();
    success = sdmt_message.SerializeToArray(sdm_data, sdmt_message_size);
    ASSERT_TRUE(success);
    // Parse the SelfDescribingMessageTest from sdm_data
    SelfDescribingMessageTest parsed_sdmt_message;
    success = parsed_sdmt_message.ParseFromArray(sdm_data, sdmt_message_size);
    ASSERT_TRUE(success);
    const FileDescriptorSet &parsed_fds(parsed_sdmt_message.proto_files());
    EXPECT_EQ(1, parsed_fds.file_size());     
    const FileDescriptorProto &parsed_fdp(parsed_fds.file(0));
    DescriptorPool dpool;
    const FileDescriptor *fd(dpool.BuildFile(parsed_fdp));
    ASSERT_TRUE(fd != NULL);
    const Descriptor *mdesc = dpool.FindMessageTypeByName(
        parsed_sdmt_message.type_name());
    ASSERT_TRUE(mdesc != NULL);
    // Get the descriptors for the fields we're interested in and verify
    // their types.
    const FieldDescriptor* tm_name_field = mdesc->FindFieldByName("tm_name");
    ASSERT_TRUE(tm_name_field != NULL);
    EXPECT_TRUE(tm_name_field->type() == FieldDescriptor::TYPE_STRING);
    EXPECT_TRUE(tm_name_field->label() == FieldDescriptor::LABEL_REQUIRED);
    EXPECT_TRUE(tm_name_field->number() == 1);
    const FieldDescriptor* tm_status_field = 
        mdesc->FindFieldByName("tm_status");
    ASSERT_TRUE(tm_status_field != NULL);
    EXPECT_TRUE(tm_status_field->type() == FieldDescriptor::TYPE_STRING);
    EXPECT_TRUE(tm_status_field->label() == FieldDescriptor::LABEL_OPTIONAL);
    EXPECT_TRUE(tm_status_field->number() == 2);
    // Inner Message
    const FieldDescriptor* tm_inner_field = mdesc->FindFieldByName("tm_inner");
    ASSERT_TRUE(tm_inner_field != NULL);
    EXPECT_TRUE(tm_inner_field->type() == FieldDescriptor::TYPE_MESSAGE);
    EXPECT_TRUE(tm_inner_field->label() == FieldDescriptor::LABEL_REPEATED);
    EXPECT_TRUE(tm_inner_field->number() == 3);
    const Descriptor *imdesc = tm_inner_field->message_type();
    ASSERT_TRUE(imdesc != NULL);
    const FieldDescriptor* tm_inner_name_field =
        imdesc->FindFieldByName("tm_inner_name");
    ASSERT_TRUE(tm_inner_name_field != NULL);
    EXPECT_TRUE(tm_inner_name_field->type() == FieldDescriptor::TYPE_STRING);
    EXPECT_TRUE(tm_inner_name_field->label() ==
        FieldDescriptor::LABEL_REQUIRED);
    EXPECT_TRUE(tm_inner_name_field->number() == 1);
    const FieldDescriptor* tm_inner_status_field = 
        imdesc->FindFieldByName("tm_inner_status");
    ASSERT_TRUE(tm_inner_status_field != NULL);
    EXPECT_TRUE(tm_inner_status_field->type() == FieldDescriptor::TYPE_INT32);
    EXPECT_TRUE(tm_inner_status_field->label() ==
        FieldDescriptor::LABEL_OPTIONAL);
    EXPECT_TRUE(tm_inner_status_field->number() == 2);
    // Parse the message.
    DynamicMessageFactory dmf(&dpool);
    const Message* msg_proto = dmf.GetPrototype(mdesc);
    Message* msg = msg_proto->New();
    msg->ParseFromString(parsed_sdmt_message.message_data());
    // Use the reflection interface to examine the contents.
    const Reflection* reflection = msg->GetReflection();
    EXPECT_TRUE(reflection->GetString(*msg, tm_name_field) == "TestMessage");
    EXPECT_TRUE(reflection->GetString(*msg, tm_status_field) == "Test");
    EXPECT_TRUE(reflection->FieldSize(*msg, tm_inner_field) == 2);
    const Message &inner_msg1(
        reflection->GetRepeatedMessage(*msg, tm_inner_field, 0));
    EXPECT_TRUE(VerifyTestMessageInner(inner_msg1, 1));
    const Message &inner_msg2(
        reflection->GetRepeatedMessage(*msg, tm_inner_field, 1));
    EXPECT_TRUE(VerifyTestMessageInner(inner_msg2, 2));
    delete msg;
}

int main(int argc, char **argv) {
    char *top_obj_dir = getenv("TOP_OBJECT_PATH");
    if (top_obj_dir) {
        d_desc_file_ = std::string(top_obj_dir) + "/analytics/test/test_message.desc";
    }
    ::testing::InitGoogleTest(&argc, argv);
    LoggingInit();
    int result = RUN_ALL_TESTS();
    return result;
}
