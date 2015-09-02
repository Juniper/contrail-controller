/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "testing/gunit.h"
#include <boost/lexical_cast.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/random_generator.hpp>

#include <base/logging.h>
#include <sandesh/sandesh_message_builder.h>

#include "../viz_message.h"
#include "collector_uve_types.h"

using namespace pugi;

class VizMessageTest : public ::testing::Test {
public:
    VizMessageTest() :
        builder_(SandeshXMLMessageTestBuilder::GetInstance()) {
    }

    virtual void SetUp() {
    }

    virtual void TearDown() {
    }

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

protected:
    SandeshMessageBuilder *builder_;
    boost::uuids::random_generator rgen_;
    VizMsgStatistics stats_;
};

VizMessageTest::SandeshXMLMessageTestBuilder
    VizMessageTest::SandeshXMLMessageTestBuilder::instance_;

TEST_F(VizMessageTest, RuleMsg) {
    SandeshHeader hdr;
    std::string xmlmessage = "<Sandesh><VNSwitchErrorMsg type=\"sandesh\"><length type=\"i32\">0000000020</length><field1 type=\"string\">field1_value</field1><field2 type=\"struct\"><field21 type=\"i16\">21</field21><field22 type=\"string\">string22</field22></field2><field3 type=\"i32\">3</field3></VNSwitchErrorMsg></Sandesh>";
    boost::uuids::uuid unm(rgen_());
    SandeshXMLMessageTest *msg = dynamic_cast<SandeshXMLMessageTest *>(
        builder_->Create(
        reinterpret_cast<const uint8_t *>(xmlmessage.c_str()),
        xmlmessage.size()));
    msg->SetHeader(hdr);
    VizMsg vmsgp(msg, unm);
    RuleMsg rmsg(&vmsgp);
    int ret;
    std::string type, value;

    ret = rmsg.field_value("field2.field21", type, value);
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(type, "i16");
    EXPECT_EQ(boost::lexical_cast<int>(value), boost::lexical_cast<int>("21"));

    ret = rmsg.field_value("field2.field22", type, value);
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(type, "string");
    EXPECT_EQ(value, "string22");

    ret = rmsg.field_value("field3", type, value);
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(type, "i32");
    EXPECT_EQ(boost::lexical_cast<int>(value), boost::lexical_cast<int>("003"));

    RuleMsg::RuleMsgPredicate p1("First");
    p1 = std::string("Second");
    EXPECT_EQ(p1.tmp_, "Second");

    vmsgp.msg = NULL;
    delete msg;
}

TEST_F(VizMessageTest, Stats) {
    // Send SYSTEM sandesh at SYS_DEBUG
    SandeshHeader hdr;
    hdr.set_Level(static_cast<int32_t>(SandeshLevel::SYS_DEBUG));
    hdr.set_Type(SandeshType::SYSTEM);
    std::string xmlmessage = "<VNSwitchErrorMsg type=\"sandesh\"><length type=\"i32\">0000000020</length><field1 type=\"string\">field1_value</field1><field2 type=\"struct\"><field21 type=\"i16\">21</field21><field22 type=\"string\">string22</field22></field2><field3 type=\"i32\">3</field3></VNSwitchErrorMsg>";
    boost::uuids::uuid unm(rgen_());
    SandeshXMLMessageTest *msg = dynamic_cast<SandeshXMLMessageTest *>(
        builder_->Create(
        reinterpret_cast<const uint8_t *>(xmlmessage.c_str()),
        xmlmessage.size()));
    msg->SetHeader(hdr);
    VizMsg vmsgp(msg, unm);
    // Update stats
    stats_.Update(&vmsgp);
    // Verify Gets - SandeshStats, SandeshLogLevelStats, SandeshMessageInfo 
    std::vector<SandeshStats> vsstats;
    stats_.Get(vsstats);
    ASSERT_EQ(1, vsstats.size());
    EXPECT_STREQ("VNSwitchErrorMsg", vsstats[0].get_message_type().c_str());
    EXPECT_EQ(1, vsstats[0].get_messages());
    EXPECT_EQ(xmlmessage.size(), vsstats[0].get_bytes());
    vsstats.clear();
    std::vector<SandeshLogLevelStats> vsllstats;
    stats_.Get(vsllstats);
    ASSERT_EQ(1, vsllstats.size());
    EXPECT_STREQ("SYS_DEBUG", vsllstats[0].get_level().c_str());
    EXPECT_EQ(1, vsllstats[0].get_messages());
    EXPECT_EQ(xmlmessage.size(), vsllstats[0].get_bytes());
    vsllstats.clear();
    std::vector<SandeshMessageInfo> vsmi;
    stats_.Get(vsmi);
    ASSERT_EQ(1, vsmi.size());
    EXPECT_STREQ("VNSwitchErrorMsg", vsmi[0].get_type().c_str());
    EXPECT_STREQ("SYS_DEBUG", vsmi[0].get_level().c_str());
    EXPECT_EQ(1, vsmi[0].get_messages());
    EXPECT_EQ(xmlmessage.size(), vsmi[0].get_bytes());
    vsmi.clear();
    // Send same message, update stats
    stats_.Update(&vmsgp);
    // Verify updates
    stats_.Get(vsstats);
    ASSERT_EQ(1, vsstats.size());
    EXPECT_STREQ("VNSwitchErrorMsg", vsstats[0].get_message_type().c_str());
    EXPECT_EQ(2, vsstats[0].get_messages());
    EXPECT_EQ(xmlmessage.size() * 2, vsstats[0].get_bytes());
    vsstats.clear();
    stats_.Get(vsllstats);
    ASSERT_EQ(1, vsllstats.size());
    EXPECT_STREQ("SYS_DEBUG", vsllstats[0].get_level().c_str());
    EXPECT_EQ(2, vsllstats[0].get_messages());
    EXPECT_EQ(xmlmessage.size() * 2, vsllstats[0].get_bytes());
    vsllstats.clear();
    stats_.Get(vsmi);
    ASSERT_EQ(1, vsmi.size());
    EXPECT_STREQ("VNSwitchErrorMsg", vsmi[0].get_type().c_str());
    EXPECT_STREQ("SYS_DEBUG", vsmi[0].get_level().c_str());
    // Diffs
    EXPECT_EQ(1, vsmi[0].get_messages());
    EXPECT_EQ(xmlmessage.size(), vsmi[0].get_bytes());
    vsmi.clear();
    // Delete message
    vmsgp.msg = NULL;
    delete msg;
    msg = NULL;
    // Send OBJECT sandesh at SYS_INVALID
    hdr.set_Level(static_cast<int32_t>(SandeshLevel::INVALID));
    hdr.set_Type(SandeshType::OBJECT);
    std::string xmlmessage_object = "<VNSwitchErrorMsgObject type=\"sandesh\"><length type=\"i32\">0000000020</length><field1 type=\"string\">field1_value</field1><field2 type=\"struct\"><field21 type=\"i16\">21</field21><field22 type=\"string\">string22</field22></field2><field3 type=\"i32\">3</field3></VNSwitchErrorMsgObject>";
    unm = rgen_();
    msg = dynamic_cast<SandeshXMLMessageTest *>(
        builder_->Create(
        reinterpret_cast<const uint8_t *>(xmlmessage_object.c_str()),
        xmlmessage_object.size()));
    msg->SetHeader(hdr);
    VizMsg vmsgp_object(msg, unm);
    // Update stats
    stats_.Update(&vmsgp_object);
    // Verify Gets - SandeshStats, SandeshLogLevelStats, SandeshMessageInfo 
    stats_.Get(vsstats);
    ASSERT_EQ(2, vsstats.size());
    EXPECT_STREQ("VNSwitchErrorMsg", vsstats[0].get_message_type().c_str());
    EXPECT_STREQ("VNSwitchErrorMsgObject", vsstats[1].get_message_type().c_str());
    EXPECT_EQ(2, vsstats[0].get_messages());
    EXPECT_EQ(1, vsstats[1].get_messages());
    EXPECT_EQ(xmlmessage.size() * 2, vsstats[0].get_bytes());
    EXPECT_EQ(xmlmessage_object.size(), vsstats[1].get_bytes());
    vsstats.clear();
    // Only SYSTEM and SYSLOG have level
    stats_.Get(vsllstats);
    ASSERT_EQ(1, vsllstats.size());
    EXPECT_STREQ("SYS_DEBUG", vsllstats[0].get_level().c_str());
    EXPECT_EQ(2, vsllstats[0].get_messages());
    EXPECT_EQ(xmlmessage.size() * 2, vsllstats[0].get_bytes());
    vsllstats.clear();
    stats_.Get(vsmi);
    ASSERT_EQ(2, vsmi.size());
    EXPECT_STREQ("VNSwitchErrorMsg", vsmi[0].get_type().c_str());
    EXPECT_STREQ("SYS_DEBUG", vsmi[0].get_level().c_str());
    // Diffs
    EXPECT_EQ(0, vsmi[0].get_messages());
    EXPECT_EQ(0, vsmi[0].get_bytes());
    EXPECT_STREQ("VNSwitchErrorMsgObject", vsmi[1].get_type().c_str());
    EXPECT_STREQ("INVALID", vsmi[1].get_level().c_str());
    EXPECT_EQ(1, vsmi[1].get_messages());
    EXPECT_EQ(xmlmessage_object.size(), vsmi[1].get_bytes());
    vsmi.clear();
    // Delete message
    vmsgp_object.msg = NULL;
    delete msg;
    msg = NULL;
}

int main(int argc, char **argv) {
    LoggingInit();
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

