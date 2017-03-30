/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "schema/test/ifmap_identity_property_types.h"
#include <fstream>
#include <boost/foreach.hpp>
#include <pugixml/pugixml.hpp>

#include "base/logging.h"
#include "base/task.h"
#include "base/util.h"
#include "db/db.h"
#include "db/db_graph.h"
#include "ifmap/ifmap_link_table.h"
#include "ifmap/ifmap_node.h"
#include "ifmap/ifmap_server_parser.h"
#include "ifmap/ifmap_server_table.h"
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

class IdentityPropertyTest : public ::testing::Test {
  protected:
    IdentityPropertyTest() : xparser_(NULL) {
    }

    virtual void SetUp()  {
        xparser_ = IFMapServerParser::GetInstance("ifmap_identity_property");
        ifmap_identity_property_ParserInit(xparser_);
        ifmap_identity_property_Server_ModuleInit(&db_, &graph_);
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

        xparser_->MetadataClear("ifmap_identity_property");
    }

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

TEST_F(IdentityPropertyTest, Parse) {
    pugi::xml_parse_result result =
            xdoc_.load_file("controller/src/schema/testdata/ifmap_identity_property_1.xml");
    EXPECT_TRUE(result);

    IFMapServerParser::RequestList requests;
    xparser_->ParseResults(xdoc_, &requests);
    EXPECT_EQ(7, requests.size());
    DBRequest *req = requests.front();
    IFMapServerTable::RequestData *data =
            static_cast<IFMapServerTable::RequestData *>(req->data.get());
    ASSERT_TRUE(data);
    autogen::AttributeType *attr =
            static_cast<autogen::AttributeType *>(data->content.get());
    ASSERT_TRUE(attr);
    EXPECT_EQ(10, attr->attr1);
    EXPECT_EQ("baz", attr->attr2);
    EXPECT_EQ(true, attr->attr3);
    STLDeleteValues(&requests);
}

TEST_F(IdentityPropertyTest, EncodeDecode) {
    pugi::xml_parse_result result =
    xdoc_.load_file(
        "controller/src/schema/testdata/ifmap_identity_property_1.xml");
    EXPECT_TRUE(result);

    IFMapServerParser::RequestList requests;
    xparser_->ParseResults(xdoc_, &requests);
    EXPECT_EQ(7, requests.size());
    
    IFMapServerTable *table = static_cast<IFMapServerTable *>(
        IFMapTable::FindTable(&db_, "foo"));
    ASSERT_TRUE(table != NULL);

    BOOST_FOREACH(DBRequest *req, requests) {
        table->Enqueue(req);
    }
    STLDeleteValues(&requests);
    WaitForIdle();

    pugi::xml_document xmsg;
    pugi::xml_node config = xmsg.append_child("config");
    pugi::xml_node update = config.append_child("update");

    const char *objs[] = {"a", "b", "c", "d", "e"};
    BOOST_FOREACH(const char *name, objs) {
        IFMapNode *node = table->FindNode(name);
        Foo *foo = static_cast<Foo *>(node->GetObject());
        ASSERT_TRUE(foo != NULL);
        if (node->name() == "b") {
            EXPECT_EQ(2, foo->simple_list().size());
        } else if (node->name() == "c") {
            EXPECT_EQ(2, foo->complex_list().size());
        } else if (node->name() == "d") {
            EXPECT_EQ(3, foo->value().size());
        } else if (node->name() == "e") {
            EXPECT_EQ(5, foo->complex_list().size());
            // Check default value for bool is false.
            autogen::AttributeType attr = foo->complex_list().at(0);
            EXPECT_FALSE(attr.attr3);
            // Check explicitly setting true works correctly.
            attr = foo->complex_list().at(1);
            EXPECT_TRUE(attr.attr3);
            // Check explicitly setting false works correctly.
            attr = foo->complex_list().at(4);
            EXPECT_FALSE(attr.attr3);
            EXPECT_EQ(true, foo->bool_value());
        }
        node->EncodeNodeDetail(&update);
    }

    string filename = CreateTmpFilename("1.xml");
    xmsg.save_file(filename.c_str());

    pugi::xml_node node = update.first_child();
    EXPECT_TRUE(node);
    BOOST_FOREACH(const char *name, objs) {
        IFMapTable::RequestKey key;
        auto_ptr<Foo> obj(static_cast<Foo *>(
            table->AllocObject()));
        string id_name;
        EXPECT_TRUE(Foo::Decode(node, &id_name, obj.get()));
        EXPECT_EQ(name, id_name);
        if (id_name == "b") {
            EXPECT_EQ(2, obj->simple_list().size());
        } else if (id_name == "c") {
            EXPECT_EQ(2, obj->complex_list().size());
        } else if (id_name == "d") {
            EXPECT_EQ(1, obj->complex_list().size());
            EXPECT_EQ(3, obj->value().size());
        } else if (id_name == "e") {
            EXPECT_EQ(5, obj->complex_list().size());
            // Check default value for bool is false.
            autogen::AttributeType attr = obj->complex_list().at(0);
            EXPECT_FALSE(attr.attr3);
            // Check explicitly setting true works correctly.
            attr = obj->complex_list().at(1);
            EXPECT_TRUE(attr.attr3);
            // Check explicitly setting false works correctly.
            attr = obj->complex_list().at(4);
            EXPECT_FALSE(attr.attr3);
            EXPECT_EQ(true, obj->bool_value());
        }
        node = node.next_sibling();
    }
}

TEST_F(IdentityPropertyTest, UnsignedLong) {
    IFMapServerTable *table = static_cast<IFMapServerTable *>(
        IFMapTable::FindTable(&db_, "foo"));
    IFMapTable::RequestKey key;
    key.id_name = "a";
    auto_ptr<IFMapNode> node_ptr(static_cast<IFMapNode *>(
        table->AllocEntry(&key).release()));
    Foo *obj = static_cast<Foo *>(table->AllocObject());
    node_ptr->Insert(obj);
    Foo::Int64_tProperty prop;
    prop.data = 0x8000000100020003UL;
    obj->SetProperty("long-value", &prop);
    EXPECT_EQ(prop.data, obj->long_value());

    pugi::xml_document xmsg;
    pugi::xml_node config = xmsg.append_child("config");
    pugi::xml_node update = config.append_child("update");
    node_ptr->EncodeNodeDetail(&update);

    string filename = CreateTmpFilename("ulong.xml");
    xmsg.save_file(filename.c_str());
    pugi::xml_node node = update.first_child();
    EXPECT_TRUE(node);

    auto_ptr<Foo> result(static_cast<Foo *>(table->AllocObject()));
    string id_name;
    EXPECT_TRUE(Foo::Decode(node, &id_name, result.get()));
    EXPECT_EQ(prop.data, result->long_value());
}

TEST_F(IdentityPropertyTest, Bool) {
    IFMapServerTable *table = static_cast<IFMapServerTable *>(
        IFMapTable::FindTable(&db_, "foo"));
    IFMapTable::RequestKey key;
    key.id_name = "a";
    auto_ptr<IFMapNode> node_ptr(static_cast<IFMapNode *>(
        table->AllocEntry(&key).release()));
    Foo *obj = static_cast<Foo *>(table->AllocObject());
    node_ptr->Insert(obj);
    Foo::OolProperty bool_prop;
    bool_prop.data = true;
    obj->SetProperty("bool-value", &bool_prop);
    EXPECT_EQ(bool_prop.data, obj->bool_value());

    pugi::xml_document xmsg;
    pugi::xml_node config = xmsg.append_child("config");
    pugi::xml_node update = config.append_child("update");
    node_ptr->EncodeNodeDetail(&update);

    string filename = CreateTmpFilename("bool.xml");
    xmsg.save_file(filename.c_str());
    pugi::xml_node node = update.first_child();
    EXPECT_TRUE(node);

    auto_ptr<Foo> result(static_cast<Foo *>(table->AllocObject()));
    string id_name;
    EXPECT_TRUE(Foo::Decode(node, &id_name, result.get()));
    EXPECT_EQ(bool_prop.data, result->bool_value());
}

int main(int argc, char **argv) {
    LoggingInit();
    ::testing::InitGoogleTest(&argc, argv);

    return RUN_ALL_TESTS();
}
