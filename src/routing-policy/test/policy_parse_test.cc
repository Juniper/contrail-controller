/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <fstream>
#include "routing-policy/policy_config_parser.h"
#include "base/logging.h"
#include "testing/gunit.h"

using namespace std;
class PolicyParserTest : public ::testing::Test {
protected:
    PolicyParserTest() {
    }

    string FileRead(const string &filename) {
        ifstream file(filename.c_str());
        string content((istreambuf_iterator<char>(file)),
                       istreambuf_iterator<char>());
        return content;
    }  
             
    virtual void SetUp() {
    }

    virtual void TearDown() {
    }

    PolicyConfigParser parser_;
};

TEST_F(PolicyParserTest, Basic) {
    string content = FileRead("src/routing-policy/testdata/policy_1.xml");
    parser_.Parse(content);
}

int main(int argc, char **argv) {
    LoggingInit();
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
