/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "../viz_message.h"
#include "testing/gunit.h"
#include "base/logging.h"
#include "boost/lexical_cast.hpp"
#include <sandesh/sandesh_message_builder.h>

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
};

VizMessageTest::SandeshXMLMessageTestBuilder
    VizMessageTest::SandeshXMLMessageTestBuilder::instance_;

TEST_F(VizMessageTest, Test1) {
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

int main(int argc, char **argv) {
    LoggingInit();
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

