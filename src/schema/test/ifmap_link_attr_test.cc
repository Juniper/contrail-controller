/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "schema/test/ifmap_link_attr_types.h"
#include <fstream>
#include <boost/algorithm/string.hpp>
#include <boost/format.hpp>
#include <pugixml/pugixml.hpp>

#include "base/logging.h"
#include "base/task.h"
#include "base/util.h"
#include "db/db.h"
#include "db/db_graph.h"
#include "db/db_table_partition.h"
#include "ifmap/ifmap_link_table.h"
#include "ifmap/ifmap_node.h"
#include "ifmap/ifmap_server_parser.h"
#include "ifmap/ifmap_server_table.h"
#include "ifmap/ifmap_table.h"
#include "testing/gunit.h"

using namespace autogen;
using namespace std;

string CreateTmpFilename(const char *name) {
    ostringstream oss;
    const testing::UnitTest *test = testing::UnitTest::GetInstance();
    const testing::TestInfo *info = test->current_test_info();
    oss << "/tmp/" << info->name() << "_" << getpid() << "_" << name;
    return oss.str();
}

class LinkAttrTest : public ::testing::Test {
  protected:
    LinkAttrTest() : xparser_(NULL) {
    }

    virtual void SetUp()  {
        xparser_ = IFMapServerParser::GetInstance("ifmap_link_attr");
        ifmap_link_attr_ParserInit(xparser_);
        ifmap_link_attr_Server_ModuleInit(&db_, &graph_);
        IFMapLinkTable_Init(&db_, &graph_);
    }

    virtual void TearDown() {
        IFMapLinkTable *ltable = static_cast<IFMapLinkTable *>(
             db_.FindTable("__ifmap_metadata__.0"));
        ltable->Clear();

        IFMapTable::ClearTables(&db_);
        WaitForIdle();
        db_.Clear();
        DB::ClearFactoryRegistry();
        xparser_->MetadataClear("ifmap_link_attr");
    }

    string FileRead(const string &filename) {
        ifstream file(filename.c_str());
        string content((istreambuf_iterator<char>(file)),
                       istreambuf_iterator<char>());
        return content;
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

    // Wait for the scheduler to become idle. Timeout in 1 second.
    void WaitForIdle() {
        static const int kTimeout = 1;
        TaskScheduler *scheduler = TaskScheduler::GetInstance();
        for (int i = 0; i < (kTimeout * 1000); i++) {
            if (scheduler->IsEmpty()) {
                break;
            }
            usleep(1000);
        }
        EXPECT_TRUE(scheduler->IsEmpty());
    }

    pugi::xml_document xdoc_;
    IFMapServerParser *xparser_;
    DB db_;
    DBGraph graph_;
};

TEST_F(LinkAttrTest, Decode) {
    pugi::xml_parse_result result =
            xdoc_.load_file("controller/src/schema/testdata/ifmap_link_attr_1.xml");
    EXPECT_TRUE(result);

    IFMapServerParser::RequestList requests;
    xparser_->ParseResults(xdoc_, &requests);
    EXPECT_EQ(1, requests.size());
    DBRequest *req = requests.front();

    IFMapTable *table = NULL;
    IFMapTable *attr_table = NULL;
    {
        IFMapTable::RequestKey *key =
                static_cast<IFMapTable::RequestKey *>(req->key.get());
        IFMapServerTable::RequestData *data =
                static_cast<IFMapServerTable::RequestData *>(req->data.get());
        ASSERT_TRUE(data);
        autogen::AttributeType *attr =
                static_cast<autogen::AttributeType *>(data->content.get());
        ASSERT_TRUE(attr);
        EXPECT_EQ(10, attr->attr1);
        EXPECT_EQ("baz", attr->attr2);

        // Remove the xml namespace qualifier.
        key->id_type = IdTypename(key->id_type);
        data->id_type = IdTypename(data->id_type);
        data->metadata = IdTypename(data->metadata);

        table = static_cast<IFMapTable *>(
            db_.FindTable(TableName(key->id_type)));
        ASSERT_TRUE(table != NULL);

        attr_table = static_cast<IFMapTable *>(
            db_.FindTable(TableName(data->metadata)));
        ASSERT_TRUE(attr_table);
    }

    table->Enqueue(req);
    STLDeleteValues(&requests);

    // Wait for operation to be performed.
    WaitForIdle();

    DBTablePartition *partition =
            static_cast<DBTablePartition *>(attr_table->GetTablePartition(0));

    IFMapNode *node = static_cast<IFMapNode *>(partition->GetFirst());
    FooBarLink *fbl = static_cast<FooBarLink *>(node->GetObject());
    ASSERT_TRUE(fbl);
    ASSERT_EQ(10, fbl->data().attr1);
    ASSERT_EQ("baz", fbl->data().attr2);
}

TEST_F(LinkAttrTest, AgentEncodeDecode) {
    pugi::xml_parse_result result =
            xdoc_.load_file("controller/src/schema/testdata/ifmap_link_attr_1.xml");
    EXPECT_TRUE(result);

    IFMapServerParser::RequestList requests;
    xparser_->ParseResults(xdoc_, &requests);
    EXPECT_EQ(1, requests.size());

    IFMapTable *table = IFMapTable::FindTable(&db_, "foo");
    ASSERT_TRUE(table != NULL);

    table->Enqueue(requests.front());
    STLDeleteValues(&requests);
    WaitForIdle();

    IFMapNode *a = table->FindNode("a");
    ASSERT_TRUE(a != NULL);
    EXPECT_EQ("a", a->name());

    pugi::xml_document xmsg;
    pugi::xml_node config = xmsg.append_child("config");
    pugi::xml_node update = config.append_child("update");
    a->EncodeNodeDetail(&update);

    table = IFMapTable::FindTable(&db_, "bar");
    ASSERT_TRUE(table != NULL);
    IFMapNode *b = table->FindNode("b");
    ASSERT_TRUE(b != NULL);
    b->EncodeNodeDetail(&update);

    table = IFMapTable::FindTable(&db_, "foo-bar-link");
    ASSERT_TRUE(table != NULL);
    IFMapNode *lnode = table->FindNode(
        IFMapServerTable::LinkAttrKey(a, b));
    ASSERT_TRUE(lnode != NULL);
    FooBarLink *attr = static_cast<FooBarLink *>(lnode->GetObject());
    ASSERT_TRUE(attr != NULL);
    lnode->EncodeNodeDetail(&update);

    string filename = CreateTmpFilename("1.xml");
    xmsg.save_file(filename.c_str());

    pugi::xml_node node = update.first_child();
    EXPECT_TRUE(node);

    EXPECT_STREQ("node", node.name());
    EXPECT_STREQ("foo", node.attribute("type").value());
    table = IFMapTable::FindTable(&db_, "bar");
    IFMapTable::RequestKey key;
    IFMapServerTable *bar_table = static_cast<IFMapServerTable *>(table);
    auto_ptr<Foo> aprime(static_cast<Foo *>(bar_table->AllocObject()));
    string id_name;
    EXPECT_TRUE(Foo::Decode(node, &id_name, aprime.get()));
    EXPECT_EQ("a", id_name);

    node = node.next_sibling();
    ASSERT_TRUE(node);
    node = node.next_sibling();

    table = IFMapTable::FindTable(&db_, "foo-bar-link");
    IFMapServerTable *attr_table = static_cast<IFMapServerTable *>(table);
    auto_ptr<FooBarLink> n_attr(
        static_cast<FooBarLink *>(attr_table->AllocObject()));
    EXPECT_TRUE(FooBarLink::Decode(node, &id_name, n_attr.get()));
    EXPECT_EQ(lnode->name(), id_name);
    EXPECT_EQ(attr->data().attr1, n_attr->data().attr1);
    EXPECT_EQ(attr->data().attr2, n_attr->data().attr2);
}

int main(int argc, char **argv) {
    LoggingInit();
    ::testing::InitGoogleTest(&argc, argv);

    return RUN_ALL_TESTS();
}
