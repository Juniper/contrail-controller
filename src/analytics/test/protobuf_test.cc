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
#include "analytics/self_describing_message.pb.h"
#include "analytics/protobuf_server_impl.h"
#include "analytics/protobuf_server.h"
#include "test_message.pb.h"

using namespace ::google::protobuf;
using boost::assign::map_list_of;

static std::string d_desc_file_ = "build/debug/analytics/test/test_message.desc";

namespace {

class ProtobufReaderTest : public ::testing::Test {
};

bool VerifyTestMessageInner(const Message &inner_msg, int expected_status,
    TestMessage::TestMessageEnum expected_enum,
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
    std::string tm_inner_name("TestMessageInner");
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
    EXPECT_EQ(TestMessage::TestMessageEnum(edesc->number()), expected_enum);
    return true;
}

// Create TestMessage and serialize it
void CreateAndSerializeTestMessage(uint8_t *output, size_t size,
    int *serialized_size) {
    TestMessage test_message;
    test_message.set_tm_name("TestMessage");
    test_message.set_tm_status("Test");
    test_message.set_tm_counter(3);
    test_message.set_tm_enum(TestMessage::GOOD);
    TestMessageInner *test_message_inner = test_message.add_tm_inner();
    ASSERT_TRUE(test_message_inner != NULL);
    test_message_inner->set_tm_inner_name("TestMessageInner1");
    test_message_inner->set_tm_inner_status(1);
    test_message_inner->set_tm_inner_counter(1);
    test_message_inner->set_tm_inner_enum(TestMessage::GOOD);
    test_message_inner = test_message.add_tm_inner();
    test_message_inner->set_tm_inner_name("TestMessageInner2");
    test_message_inner->set_tm_inner_status(2);
    test_message_inner->set_tm_inner_counter(2);
    test_message_inner->set_tm_inner_enum(TestMessage::BAD);

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
    const FieldDescriptor* tm_enum_field =
        mdesc->FindFieldByName("tm_enum");
    ASSERT_TRUE(tm_enum_field != NULL);
    EXPECT_TRUE(tm_enum_field->type() == FieldDescriptor::TYPE_ENUM);
    EXPECT_TRUE(tm_enum_field->label() == FieldDescriptor::LABEL_OPTIONAL);
    EXPECT_TRUE(tm_enum_field->number() == 5);
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
    const FieldDescriptor* tm_inner_enum_field =
        imdesc->FindFieldByName("tm_inner_enum");
    ASSERT_TRUE(tm_inner_enum_field != NULL);
    EXPECT_TRUE(tm_inner_enum_field->type() == FieldDescriptor::TYPE_ENUM);
    EXPECT_TRUE(tm_inner_enum_field->label() ==
        FieldDescriptor::LABEL_OPTIONAL);
    EXPECT_TRUE(tm_inner_enum_field->number() == 4);
    // Use the reflection interface to examine the contents.
    const Reflection* reflection = msg->GetReflection();
    EXPECT_TRUE(reflection->GetString(*msg, tm_name_field) == "TestMessage");
    EXPECT_TRUE(reflection->GetString(*msg, tm_status_field) == "Test");
    EXPECT_TRUE(reflection->GetInt32(*msg, tm_counter_field) == 3);
    const EnumValueDescriptor *edesc(reflection->GetEnum(*msg, tm_enum_field));
    EXPECT_TRUE(edesc != NULL);
    EXPECT_STREQ(edesc->name().c_str(), "GOOD");
    EXPECT_EQ(TestMessage::TestMessageEnum(edesc->number()),
        TestMessage::GOOD);
    EXPECT_TRUE(reflection->FieldSize(*msg, tm_inner_field) == 2);
    const Message &inner_msg1(
        reflection->GetRepeatedMessage(*msg, tm_inner_field, 0));
    EXPECT_TRUE(VerifyTestMessageInner(inner_msg1, 1,
        TestMessage::GOOD, "GOOD"));
    const Message &inner_msg2(
        reflection->GetRepeatedMessage(*msg, tm_inner_field, 1));
    EXPECT_TRUE(VerifyTestMessageInner(inner_msg2, 2,
        TestMessage::BAD, "BAD"));

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

class ProtobufStatWalkerTest : public ::testing::Test {
};

TEST_F(ProtobufStatWalkerTest, Basic) {
    StatCbTester ct(PopulateTestMessageStatsInfo());

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

class ProtobufServerTest : public ::testing::Test {
 protected:
    ProtobufServerTest() :
        stats_tester_(PopulateTestMessageStatsInfo()) {
    }

    virtual void SetUp() {
        for (int i = 0; i < stats_tester_.exp_.size(); i++) {
            match_.push_back(true);
        }
        evm_.reset(new EventManager());
        server_.reset(new protobuf::ProtobufServer(evm_.get(), 0,
            boost::bind(&StatCbTester::Cb, &stats_tester_, _1, _2, _3,
            _4, _5)));
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

    std::vector<bool> match_;
    StatCbTester stats_tester_;
    std::auto_ptr<ServerThread> thread_;
    std::auto_ptr<protobuf::ProtobufServer> server_;
    ProtobufMockClient *client_;
    std::auto_ptr<EventManager> evm_;
};

TEST_F(ProtobufServerTest, Basic) {
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
    std::string snd(reinterpret_cast<const char *>(sdm_data),
        serialized_sdm_data_size);
    client_->Send(snd, server_endpoint);
    TASK_UTIL_EXPECT_EQ(client_->GetTxPackets(), 1);
    TASK_UTIL_EXPECT_VECTOR_EQ(stats_tester_.match_, match_);
    // Compare statistics
    std::vector<SocketIOStats> va_rx_stats, va_tx_stats;
    std::vector<SocketEndpointMessageStats> va_rx_msg_stats;
    server_->GetStatistics(&va_tx_stats, &va_rx_stats, &va_rx_msg_stats);
    EXPECT_EQ(1, va_tx_stats.size());
    EXPECT_EQ(1, va_rx_stats.size());
    EXPECT_EQ(1, va_rx_msg_stats.size());
    const SocketIOStats &a_rx_io_stats(va_rx_stats[0]);
    EXPECT_EQ(1, a_rx_io_stats.get_calls());
    EXPECT_EQ(serialized_sdm_data_size, a_rx_io_stats.get_bytes());
    EXPECT_EQ(serialized_sdm_data_size, a_rx_io_stats.get_average_bytes());
    const SocketIOStats &a_tx_io_stats(va_tx_stats[0]);
    EXPECT_EQ(0, a_tx_io_stats.get_calls());
    EXPECT_EQ(0, a_tx_io_stats.get_bytes());
    EXPECT_EQ(0, a_tx_io_stats.get_average_bytes());
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
    EXPECT_EQ(serialized_sdm_data_size, a_rx_msg_stats.get_bytes());
}

}  // namespace

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
