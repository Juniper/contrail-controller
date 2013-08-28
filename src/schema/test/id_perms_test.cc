/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "schema/vnc_cfg_types.h"
#include <pugixml/pugixml.hpp>

#include "base/logging.h"
#include "ifmap/ifmap_server_parser.h"
#include "ifmap/ifmap_server_table.h"
#include "testing/gunit.h"

using namespace std;

class IdPermsTest : public ::testing::Test {
  protected:
    virtual void SetUp()  {
        xparser_ = IFMapServerParser::GetInstance("vnc_cfg");
        vnc_cfg_ParserInit(xparser_);
    }

    pugi::xml_document xdoc_;
    IFMapServerParser *xparser_;
};

TEST_F(IdPermsTest, Load) {
    pugi::xml_parse_result result =
            xdoc_.load_file("controller/src/schema/testdata/id_perms.xml");
    EXPECT_TRUE(result);

    IFMapServerParser::RequestList requests;
    xparser_->ParseResults(xdoc_, &requests);
    EXPECT_EQ(1, requests.size());
    DBRequest *request = requests.front();

    IFMapServerTable::RequestData *data =
            static_cast<IFMapServerTable::RequestData *>(request->data.get());
    
    ASSERT_TRUE(data);
    autogen::IdPermsType *perms =
            static_cast<autogen::IdPermsType *>(data->content.get());
    EXPECT_EQ(3223684968904540175, perms->uuid.uuid_mslong);
}

int main(int argc, char **argv) {
    LoggingInit();
    ::testing::InitGoogleTest(&argc, argv);

    return RUN_ALL_TESTS();
}
