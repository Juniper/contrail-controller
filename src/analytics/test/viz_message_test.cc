/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "../viz_message.h"
#include "testing/gunit.h"
#include "base/logging.h"
#include "boost/lexical_cast.hpp"

class VizMessageTest : public ::testing::Test {
public:
    virtual void SetUp() {
    }

    virtual void TearDown() {
    }
};

TEST_F(VizMessageTest, Test1) {
    SandeshHeader hdr;
    std::string messagetype;
    std::string xmlmessage = "<Sandesh><VNSwitchErrorMsg type=\"sandesh\"><length type=\"i32\">0000000020</length><field1 type=\"string\">field1_value</field1><field2 type=\"struct\"><field21 type=\"i16\">21</field21><field22 type=\"string\">string22</field22></field2><field3 type=\"i32\">3</field3></VNSwitchErrorMsg></Sandesh>";
    boost::uuids::uuid unm = boost::uuids::random_generator()();
    boost::shared_ptr<VizMsg> vmsgp(new VizMsg(hdr, messagetype, xmlmessage, unm)); 
    RuleMsg rmsg(vmsgp);
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
}

int main(int argc, char **argv) {
    LoggingInit();
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

