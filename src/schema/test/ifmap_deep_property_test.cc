/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "schema/test/ifmap_deep_property_types.h"
#include <fstream>
#include <pugixml/pugixml.hpp>

#include "base/logging.h"
#include "base/util.h"
#include "ifmap/ifmap_server_table.h"
#include "ifmap/ifmap_server_parser.h"
#include "testing/gunit.h"

using namespace std;

class DeepPropertyTest : public ::testing::Test {
  protected:
    DeepPropertyTest() : xparser_(NULL) {
    }

    virtual void SetUp()  {
        xparser_.reset(IFMapServerParser::GetInstance("ifmap_deep_property"));
        ifmap_deep_property_ParserInit(xparser_.get());
    }


    pugi::xml_document xdoc_;
    boost::scoped_ptr<IFMapServerParser> xparser_;
};

TEST_F(DeepPropertyTest, Decode) {
    pugi::xml_parse_result result =
            xdoc_.load_file("controller/src/schema/testdata/ifmap_deep_property_1.xml");
    EXPECT_TRUE(result);

    IFMapServerParser::RequestList requests;
    xparser_->ParseResults(xdoc_, &requests);
    EXPECT_EQ(1, requests.size());
    DBRequest *req = requests.front();
    IFMapServerTable::RequestData *data =
            static_cast<IFMapServerTable::RequestData *>(req->data.get());
    ASSERT_TRUE(data);
    autogen::AttributeType *attr =
            static_cast<autogen::AttributeType *>(data->content.get());
    ASSERT_TRUE(attr);
    EXPECT_EQ(11, attr->attr1.d11);
    EXPECT_EQ("abcd", attr->attr1.d12);
    EXPECT_EQ(21, attr->attr2.d21);
    EXPECT_EQ("efgh", attr->attr2.d22);
    STLDeleteValues(&requests);
}

int main(int argc, char **argv) {
    LoggingInit();
    ::testing::InitGoogleTest(&argc, argv);

    return RUN_ALL_TESTS();
}
