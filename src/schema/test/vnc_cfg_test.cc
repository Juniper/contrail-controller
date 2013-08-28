/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "schema/vnc_cfg_types.h"
#include <boost/bind.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/format.hpp>
#include <tbb/concurrent_queue.h>

#include <fstream>
#include <pugixml/pugixml.hpp>

#include "base/logging.h"
#include "base/util.h"
#include "db/db.h"
#include "db/db_table.h"
#include "db/db_graph.h"
#include "ifmap/ifmap_parser.h"
#include "ifmap/ifmap_table.h"
#include "testing/gunit.h"
#include "ifmap/client/ifmap_mgr.h"

using namespace std;

class VncCfgTest : public ::testing::Test {
public:
    void IfmapReceive(const char *data, size_t length) {
        std::string doc(data, length);
        cout << "Pushing data:" << endl << doc << endl;
        queue_.push(doc);
    }

protected:
    VncCfgTest() : xparser_(NULL) {
    }

    virtual void SetUp()  {
        xparser_ = IFMapParser::GetInstance("vnc_cfg_parser");
        vnc_cfg_ParserInit(xparser_);
        vnc_cfg_Server_ModuleInit(&db_, &graph_);
    }
    virtual void TearDown() {
      // ifmapmgr_->Shutdown();
      // ifmapmgr_->Join();
    }

    string IdTypename(const string &id_typename) {
        size_t loc = id_typename.find(':');
        if (loc != string::npos) {
            size_t pos = loc + 1;
            return string(id_typename, pos, id_typename.size() - pos);
        }
        return id_typename;
    }

    string TableName(const string &id_type) {
        string name = id_type;
        boost::replace_all(name, "-", "_");
        return str(boost::format("__ifmap__.%s.0") % name);
    }

    pugi::xml_document xdoc_;
    IFMapParser *xparser_;
    DB db_;
    DBGraph graph_;
    tbb::concurrent_queue<std::string> queue_;
};

TEST_F(VncCfgTest, Decode) {
    std::string msg;
    IfmapMgr *ifmapmgr = IfmapMgr::CreateIfmapMgr("https://10.1.2.237:8443",
                                        "test2", "test2", "",
                                        boost::bind(&VncCfgTest::IfmapReceive,
                                                    this, _1, _2));
    ifmapmgr->Start();

    while (true) {
        if (!queue_.try_pop(msg)) {
            usleep(1000);
            continue;
        }
        cout << "Queue has element. Breaking." << endl;
        break;
    }
    pugi::xml_parse_result result = xdoc_.load(msg.c_str());
    EXPECT_TRUE(result);

    IFMapParser::RequestList requests;
    xparser_->ParseResults(xdoc_, &requests);
    EXPECT_EQ(3, requests.size());
    while (!requests.empty()) {
        DBRequest *req = requests.front();

        IFMapTable::RequestKey *key =
                static_cast<IFMapTable::RequestKey *>(req->key.get());
        key->id_type = IdTypename(key->id_type);
        LOG(DEBUG, "key: " << key->id_type);

        IFMapTable *table = static_cast<IFMapTable *>
                                       (db_.FindTable(TableName(key->id_type)));
        ASSERT_TRUE(table != NULL);
        table->Enqueue(req);
    }

    usleep(1000);
    // verify the datastructure on the table.

    sleep(10);
    ifmapmgr->Cancel();
    sleep(10);
    ifmapmgr->Join();
}

int main(int argc, char **argv) {
    LoggingInit();
    ::testing::InitGoogleTest(&argc, argv);

    return RUN_ALL_TESTS();
}

