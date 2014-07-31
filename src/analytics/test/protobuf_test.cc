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

#include <base/logging.h>

#include "analytics/db_handler.h"
#include "analytics/self_describing_message.pb.h"
#include "analytics/protobuf_server_impl.h"
#include "test_message.pb.h"

using namespace ::google::protobuf;
using boost::assign::map_list_of;

static std::string d_desc_file_ = "build/debug/analytics/test/test_message.desc";

class ProtobufReaderTest : public ::testing::Test {
};

bool VerifyTestMessageInner(const Message &inner_msg, int expected_status) {
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
    // Reflection
    const Reflection *inner_reflection = inner_msg.GetReflection();
    std::string tm_inner_name("TestMessageInner");
    std::stringstream ss;
    ss << tm_inner_name << expected_status;
    EXPECT_TRUE(inner_reflection->GetString(
        inner_msg, tm_inner_name_field) == ss.str());
    EXPECT_TRUE(inner_reflection->GetInt32(
        inner_msg, tm_inner_status_field) == expected_status);
    EXPECT_TRUE(inner_reflection->GetInt32(
        inner_msg, tm_inner_counter_field) == expected_status);
    return true;
}

// Create TestMessage and serialize it
void CreateAndSerializeTestMessage(uint8_t *output, size_t size,
    int *serialized_size) {
    TestMessage test_message;
    test_message.set_tm_name("TestMessage");
    test_message.set_tm_status("Test");
    test_message.set_tm_counter(3);
    TestMessageInner *test_message_inner = test_message.add_tm_inner();
    ASSERT_TRUE(test_message_inner != NULL);
    test_message_inner->set_tm_inner_name("TestMessageInner1");
    test_message_inner->set_tm_inner_status(1);
    test_message_inner->set_tm_inner_counter(1);
    test_message_inner = test_message.add_tm_inner();
    test_message_inner->set_tm_inner_name("TestMessageInner2");
    test_message_inner->set_tm_inner_status(2);
    test_message_inner->set_tm_inner_counter(2);

    int test_message_size = test_message.ByteSize();
    ASSERT_GE(size, test_message_size);
    *serialized_size = test_message_size;
    bool success = test_message.SerializeToArray(output, test_message_size);
    ASSERT_TRUE(success);
}

// Create SelfDescribingMessage for TestMessage and serialize it
void CreateAndSerializeSelfDescribingMessage(uint8_t *output, size_t size,
    int *serialized_size, const char *desc_file, uint8_t *message_data,
    size_t message_data_size) {
    std::ifstream desc(desc_file,
        std::ios_base::in | std::ios_base::binary);
    ASSERT_TRUE(desc.is_open());
    FileDescriptorSet fds;
    bool success = fds.ParseFromIstream(&desc);
    ASSERT_TRUE(success);
    desc.close();
    SelfDescribingMessage sdm_message;
    sdm_message.set_timestamp(123456789);
    sdm_message.mutable_proto_files()->CopyFrom(fds);
    sdm_message.set_type_name("TestMessage");
    sdm_message.set_message_data(message_data, message_data_size);
    int sdm_message_size = sdm_message.ByteSize();
    ASSERT_GE(size, sdm_message_size);
    *serialized_size = sdm_message_size;
    success = sdm_message.SerializeToArray(output, sdm_message_size);
    ASSERT_TRUE(success);
}

TEST_F(ProtobufReaderTest, Parse) {
    // Create TestMessage and serialize it
    uint8_t data[512];
    int serialized_data_size(0);
    CreateAndSerializeTestMessage(data, sizeof(data),
        &serialized_data_size);
    // Create SelfDescribingMessageTest for TestMessage and serialize it
    uint8_t sdm_data[512];
    int serialized_sdm_data_size(0);
    CreateAndSerializeSelfDescribingMessage(sdm_data,
        sizeof(sdm_data), &serialized_sdm_data_size, d_desc_file_.c_str(),
        data, serialized_data_size);
    // Parse the SelfDescribingMessage from sdm_data to get TestMessage
    protobuf::impl::ProtobufReader reader;
    Message *msg = NULL;
    uint64_t timestamp;
    bool success = reader.ParseSelfDescribingMessage(sdm_data,
        serialized_sdm_data_size, &timestamp, &msg);
    ASSERT_TRUE(success);
    ASSERT_TRUE(msg != NULL);
    EXPECT_EQ(123456789, timestamp);
    const Descriptor *mdesc = msg->GetDescriptor();
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
    const FieldDescriptor* tm_counter_field =
        mdesc->FindFieldByName("tm_counter");
    ASSERT_TRUE(tm_counter_field != NULL);
    EXPECT_TRUE(tm_counter_field->type() == FieldDescriptor::TYPE_INT32);
    EXPECT_TRUE(tm_counter_field->label() == FieldDescriptor::LABEL_OPTIONAL);
    EXPECT_TRUE(tm_counter_field->number() == 3);
    // Inner Message
    const FieldDescriptor* tm_inner_field = mdesc->FindFieldByName("tm_inner");
    ASSERT_TRUE(tm_inner_field != NULL);
    EXPECT_TRUE(tm_inner_field->type() == FieldDescriptor::TYPE_MESSAGE);
    EXPECT_TRUE(tm_inner_field->label() == FieldDescriptor::LABEL_REPEATED);
    EXPECT_TRUE(tm_inner_field->number() == 4);
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
    const FieldDescriptor* tm_inner_counter_field =
        imdesc->FindFieldByName("tm_inner_counter");
    ASSERT_TRUE(tm_inner_counter_field != NULL);
    EXPECT_TRUE(tm_inner_counter_field->type() == FieldDescriptor::TYPE_INT32);
    EXPECT_TRUE(tm_inner_counter_field->label() ==
        FieldDescriptor::LABEL_OPTIONAL);
    EXPECT_TRUE(tm_inner_counter_field->number() == 3);
    // Use the reflection interface to examine the contents.
    const Reflection* reflection = msg->GetReflection();
    EXPECT_TRUE(reflection->GetString(*msg, tm_name_field) == "TestMessage");
    EXPECT_TRUE(reflection->GetString(*msg, tm_status_field) == "Test");
    EXPECT_TRUE(reflection->GetInt32(*msg, tm_counter_field) == 3);
    EXPECT_TRUE(reflection->FieldSize(*msg, tm_inner_field) == 2);
    const Message &inner_msg1(
        reflection->GetRepeatedMessage(*msg, tm_inner_field, 0));
    EXPECT_TRUE(VerifyTestMessageInner(inner_msg1, 1));
    const Message &inner_msg2(
        reflection->GetRepeatedMessage(*msg, tm_inner_field, 1));
    EXPECT_TRUE(VerifyTestMessageInner(inner_msg2, 2));
    delete msg;
}

struct ArgSet {
    std::string statAttr;
    DbHandler::TagMap attribs_tag;
    DbHandler::AttribMap attribs;
};

class StatCbTester {
public:
    StatCbTester(const vector<ArgSet>& exp) : exp_(exp) {
        for (vector<ArgSet>::const_iterator it = exp_.begin();
             it != exp_.end(); it++ ) {
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

class ProtobufStatWalkerTest : public ::testing::Test {
};

TEST_F(ProtobufStatWalkerTest, Basic) {
    vector<ArgSet> av;
    ArgSet a1;
    a1.statAttr = string("TestMessage");
    a1.attribs = map_list_of
        ("TestMessage.tm_name", DbHandler::Var("TestMessage"))
        ("TestMessage.tm_status", DbHandler::Var("Test"))
        ("TestMessage.tm_counter", DbHandler::Var((uint64_t)3));

    DbHandler::AttribMap sm;
    a1.attribs_tag.insert(make_pair("TestMessage.tm_name", make_pair(
        DbHandler::Var("TestMessage"), sm)));
    a1.attribs_tag.insert(make_pair("TestMessage.tm_status", make_pair(
        DbHandler::Var("Test"), sm)));
    av.push_back(a1);

    ArgSet a2;
    a2.statAttr = string("TestMessage.tm_inner");
    a2.attribs = map_list_of
        ("TestMessage.tm_name", DbHandler::Var("TestMessage"))
        ("TestMessage.tm_status", DbHandler::Var("Test"))
        ("TestMessage.tm_inner.tm_inner_name",
         DbHandler::Var("TestMessageInner1"))
        ("TestMessage.tm_inner.tm_inner_status",
         DbHandler::Var((uint64_t)1))
        ("TestMessage.tm_inner.tm_inner_counter",
         DbHandler::Var((uint64_t)1));

    a2.attribs_tag.insert(make_pair("TestMessage.tm_name", make_pair(
        DbHandler::Var("TestMessage"), sm)));
    a2.attribs_tag.insert(make_pair("TestMessage.tm_status", make_pair(
        DbHandler::Var("Test"), sm)));
    a2.attribs_tag.insert(make_pair("TestMessage.tm_inner.tm_inner_name",
        make_pair(
            DbHandler::Var("TestMessageInner1"), sm)));
    av.push_back(a2);

    ArgSet a3;
    a3.statAttr = string("TestMessage.tm_inner");
    a3.attribs = map_list_of
        ("TestMessage.tm_name", DbHandler::Var("TestMessage"))
        ("TestMessage.tm_status", DbHandler::Var("Test"))
        ("TestMessage.tm_inner.tm_inner_name",
         DbHandler::Var("TestMessageInner2"))
        ("TestMessage.tm_inner.tm_inner_status",
         DbHandler::Var((uint64_t)2))
        ("TestMessage.tm_inner.tm_inner_counter",
         DbHandler::Var((uint64_t)2));

    a3.attribs_tag.insert(make_pair("TestMessage.tm_name", make_pair(
        DbHandler::Var("TestMessage"), sm)));
    a3.attribs_tag.insert(make_pair("TestMessage.tm_status", make_pair(
        DbHandler::Var("Test"), sm)));
    a3.attribs_tag.insert(make_pair("TestMessage.tm_inner.tm_inner_name",
        make_pair(
            DbHandler::Var("TestMessageInner2"), sm)));
    av.push_back(a3);

    StatCbTester ct(av);

    // Create TestMessage and serialize it
    uint8_t data[512];
    int serialized_data_size(0);
    CreateAndSerializeTestMessage(data, sizeof(data),
        &serialized_data_size);
    // Create SelfDescribingMessageTest for TestMessage and serialize it
    uint8_t sdm_data[512];
    int serialized_sdm_data_size(0);
    CreateAndSerializeSelfDescribingMessage(sdm_data,
        sizeof(sdm_data), &serialized_sdm_data_size, d_desc_file_.c_str(),
        data, serialized_data_size);
    // Parse the SelfDescribingMessage from sdm_data to get TestMessage
    protobuf::impl::ProtobufReader reader;
    Message *msg = NULL;
    uint64_t timestamp;
    bool success = reader.ParseSelfDescribingMessage(sdm_data,
        serialized_sdm_data_size, &timestamp, &msg);
    ASSERT_TRUE(success);
    ASSERT_TRUE(msg != NULL);

    protobuf::impl::ProcessProtobufMessage(*msg, timestamp,
        boost::bind(&StatCbTester::Cb, &ct, _1, _2, _3, _4, _5));
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
