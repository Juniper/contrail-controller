/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh.h>
#include "test_cfg_types.h"
#include <fstream>
#include <boost/algorithm/string.hpp>
#include <boost/format.hpp>
#include <boost/uuid/string_generator.hpp>
#include <boost/function.hpp>
#include <boost/bind.hpp>
#include <pugixml/pugixml.hpp>

#include "base/logging.h"
#include "base/task.h"
#include "base/util.h"
#include "db/db.h"
#include "db/db_graph.h"
#include "db/db_table_partition.h"
#include "ifmap_agent_parser.h"
#include "ifmap_agent_table.h"
#include "ifmap/ifmap_node.h"
#include "ifmap/ifmap_link.h"
#include "testing/gunit.h"
#include "vr_types.h"

class Agent;
void RouterIdDepInit(Agent *agent) {
}

using namespace std;

class CfgTest : public ::testing::Test {
protected:
    virtual void SetUp() {
       foo_cnt = 0;
       bar_cnt = 0;
       parser_ = new IFMapAgentParser(&db_);
       IFMapAgentLinkTable_Init(&db_, &graph_);
       test_cfg_Agent_ModuleInit(&db_, &graph_);
       test_cfg_Agent_ParserInit(&db_, parser_);
    }

    virtual void TearDown() {
        foo_cnt = 0;
        bar_cnt = 0;
        IFMapLinkTable *ltable = static_cast<IFMapLinkTable *>(
            db_.FindTable(IFMAP_AGENT_LINK_DB_NAME));
        ltable->Clear();
        WaitForIdle();
        IFMapTable::ClearTables(&db_);
        WaitForIdle();
        db_.Clear();
        DB::ClearFactoryRegistry();
        parser_->NodeClear();
        delete parser_;
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

    void LinkListener(DBTablePartBase *partition, DBEntryBase *dbe) {
        IFMapLink *link = static_cast <IFMapLink *>(dbe);
        IFMapNode *node = link->LeftNode(&db_);
        EXPECT_EQ("testfoo", node->name());
        node = link->RightNode(&db_);
        EXPECT_EQ("testbar", node->name());
    }

    void Listener(DBTablePartBase *partition, DBEntryBase *dbe) {
        DBRequest req;
        IFMapNode *node = static_cast <IFMapNode *> (dbe);
        int op = 1;

        if (node->IsDeleted()) {
            op = -1;
        }

        if (strcmp(node->table()->Typename(), "foo") == 0) {
            foo_cnt += op;
        } else {
            bar_cnt += op;
        }
    }

    void EmptyListener(DBTablePartBase *partition, DBEntryBase *dbe) {
    }

    pugi::xml_document xdoc_;
    IFMapAgentParser *parser_;
    DB db_;
    DBGraph graph_;
    int foo_cnt;
    int bar_cnt;
};

TEST_F(CfgTest, NodeTest) {
    pugi::xml_parse_result result =
            xdoc_.load_file("controller/src/vnsw/agent/testdata/test_add_cfg.xml");
    EXPECT_TRUE(result);

    parser_->ConfigParse(xdoc_.first_child(), 0);
    
    WaitForIdle();
    IFMapTable *table = IFMapTable::FindTable(&db_, "foo");
    ASSERT_TRUE(table!=NULL);

    IFMapNode *TestFoo = table->FindNode("testfoo");
    ASSERT_TRUE(TestFoo!=NULL);
    EXPECT_EQ("testfoo", TestFoo->name());
    

    table = IFMapTable::FindTable(&db_, "bar");
    ASSERT_TRUE(table!=NULL);
    IFMapNode *TestBar = table->FindNode("testbar");
    ASSERT_TRUE(TestBar!=NULL);
    EXPECT_EQ("testbar", TestBar->name());

    //Do the deletion of the nodes
    result = xdoc_.load_file("controller/src/vnsw/agent/testdata/test_del_cfg.xml");
    EXPECT_TRUE(result);

    parser_->ConfigParse(xdoc_.first_child(), 0);

    WaitForIdle();
    table = IFMapTable::FindTable(&db_, "foo");
    ASSERT_TRUE(table!=NULL);
    TestFoo = table->FindNode("testfoo");
    ASSERT_TRUE(TestFoo==NULL);

    table = IFMapTable::FindTable(&db_, "bar");
    ASSERT_TRUE(table!=NULL);
    TestBar = table->FindNode("testbar");
    ASSERT_TRUE(TestBar==NULL);
}


TEST_F(CfgTest, LinkTest) {

    IFMapTable *ftable = IFMapTable::FindTable(&db_, "foo");
    ASSERT_TRUE(ftable!=NULL);

    IFMapTable *btable = IFMapTable::FindTable(&db_, "bar");
    ASSERT_TRUE(btable!=NULL);

    IFMapLinkTable *ltable = static_cast<IFMapLinkTable *>(
            db_.FindTable(IFMAP_AGENT_LINK_DB_NAME));

    DBTableBase::ListenerId fid = ftable->
            Register(boost::bind(&CfgTest_LinkTest_Test::Listener, this, _1, _2));
    DBTableBase::ListenerId bid = btable->
            Register(boost::bind(&CfgTest_LinkTest_Test::Listener, this, _1, _2));
    DBTableBase::ListenerId lid = ltable->
            Register(boost::bind(&CfgTest_LinkTest_Test::LinkListener, this, _1, _2));

    pugi::xml_parse_result result =
            xdoc_.load_file("controller/src/vnsw/agent/testdata/test_link_add_cfg.xml");
    EXPECT_TRUE(result);

    parser_->ConfigParse(xdoc_.first_child(), 0);
    
    WaitForIdle();

    EXPECT_EQ(foo_cnt, 1);
    EXPECT_EQ(bar_cnt, 1);

    //Ensure that both nodes are added fine
    IFMapNode *TestFoo = ftable->FindNode("testfoo");
    ASSERT_TRUE(TestFoo!=NULL);
    EXPECT_EQ("testfoo", TestFoo->name());

    IFMapNode *TestBar = btable->FindNode("testbar");
    ASSERT_TRUE(TestBar!=NULL);
    EXPECT_EQ("testbar", TestBar->name());


    //Ensure that from Foo there is a link to Bar
    for (DBGraphVertex::adjacency_iterator iter = TestFoo->begin(&graph_);
         iter != TestFoo->end(&graph_); ++iter) {
        TestBar = static_cast<IFMapNode *>(iter.operator->());
    	EXPECT_EQ("testbar", TestBar->name());
    }

    //Ensure that there is link from Bar to foo as well
    for (DBGraphVertex::adjacency_iterator iter = TestBar->begin(&graph_);
         iter != TestBar->end(&graph_); ++iter) {
        TestFoo = static_cast<IFMapNode *>(iter.operator->());
    	EXPECT_EQ("testfoo", TestFoo->name());
    }


    //Delete only link, but not nodes
    result = xdoc_.load_file("controller/src/vnsw/agent/testdata/test_link_del_cfg.xml");
    parser_->ConfigParse(xdoc_.first_child(), 0);
    WaitForIdle();

    //Ensure that only link is deleted and not nodes
    TestFoo = ftable->FindNode("testfoo");
    ASSERT_TRUE(TestFoo!=NULL);
    EXPECT_EQ("testfoo", TestFoo->name());

    TestBar = btable->FindNode("testbar");
    ASSERT_TRUE(TestBar!=NULL);
    EXPECT_EQ("testbar", TestBar->name());


    //Ensure that there is no link from foo to bar after deletion
    int cnt = 0;
    for (DBGraphVertex::adjacency_iterator iter = TestFoo->begin(&graph_);
         iter != TestFoo->end(&graph_); ++iter) {
           cnt++;
    }
    EXPECT_EQ(0, cnt);

    //Ensure that thre is no link from bar to foo after link deletion
    cnt = 0;
    for (DBGraphVertex::adjacency_iterator iter = TestBar->begin(&graph_);
         iter != TestBar->end(&graph_); ++iter) {
           cnt++;
    }
    EXPECT_EQ(0, cnt);

    EXPECT_EQ(foo_cnt, 1);
    EXPECT_EQ(bar_cnt, 1);

    //Delete the nodes as well
    result = xdoc_.load_file("controller/src/vnsw/agent/testdata/test_del_cfg.xml");
    parser_->ConfigParse(xdoc_.first_child(), 0);
    WaitForIdle();

    TestFoo = ftable->FindNode("testfoo");
    ASSERT_TRUE(TestFoo==NULL);

    TestBar = btable->FindNode("testbar");
    ASSERT_TRUE(TestBar==NULL);

    EXPECT_EQ(foo_cnt, 0);    
    EXPECT_EQ(bar_cnt, 0);    

    ftable->Unregister(fid);
    btable->Unregister(bid);
    ltable->Unregister(lid);
}

TEST_F(CfgTest, LinkAttrTest) {

    char buff[1500];
    sprintf(buff, 
        "<update>\n"    
        "   <node type=\"A\">\n"
        "       <name>testA</name>\n"
        "   </node>\n"
        "   <node type=\"B\">\n"
        "       <name>testB</name>\n"
        "   </node>\n"
        "   <node type=\"A-B-data\">\n"
        "       <name>testdata</name>\n"
        "       <value>\n"
        "           <val1>999</val1>\n"
        "           <val2>888</val2>\n"
        "       </value>\n"
        "   </node>\n"
        "   <link>\n"
        "       <node type=\"A\">\n"
        "           <name>testA</name>\n"
        "       </node>\n"
        "       <node type=\"A-B-data\">\n"
        "           <name>testdata</name>\n"
        "       </node>\n"
        "   </link>\n"
        "   <link>\n"
        "       <node type=\"B\">\n"
        "           <name>testB</name>\n"
        "       </node>\n"
        "       <node type=\"A-B-data\">\n"
        "           <name>testdata</name>\n"
        "       </node>\n"
        "   </link>\n"
        "</update>");

    IFMapTable *Atable = IFMapTable::FindTable(&db_, "A");
    ASSERT_TRUE(Atable!=NULL);

    IFMapTable *Btable = IFMapTable::FindTable(&db_, "B");
    ASSERT_TRUE(Btable!=NULL);

    IFMapTable *ABtable = IFMapTable::FindTable(&db_, "A_B_data");
    ASSERT_TRUE(ABtable!=NULL);

    pugi::xml_document xdoc_;
    pugi::xml_parse_result result = xdoc_.load(buff);
    EXPECT_TRUE(result);

    parser_->ConfigParse(xdoc_, 0);
    
    WaitForIdle();

    //Ensure that both nodes are added fine along with attribute
    IFMapNode *TestA = Atable->FindNode("testA");
    ASSERT_TRUE(TestA !=NULL);
    EXPECT_EQ("testA", TestA->name());

    IFMapNode *TestB = Btable->FindNode("testB");
    ASSERT_TRUE(TestB !=NULL);
    EXPECT_EQ("testB", TestB->name());

    IFMapNode *TestAB = ABtable->FindNode("testdata");
    ASSERT_TRUE(TestAB !=NULL);
    EXPECT_EQ("testdata", TestAB->name());

    autogen::ABData *abd = static_cast<autogen::ABData *> (TestAB->GetObject());
    autogen::DataType d = abd->data();
    EXPECT_EQ(d.val1, 999);
    EXPECT_EQ(d.val2, 888);

    //Ensure that there is Link from A to ABData
    for (DBGraphVertex::adjacency_iterator iter = TestA->begin(&graph_);
         iter != TestA->end(&graph_); ++iter) {
        TestAB = static_cast<IFMapNode *>(iter.operator->());
    	EXPECT_EQ("testdata", TestAB->name());
    }

    //Ensure that there is link from B to ABData
    for (DBGraphVertex::adjacency_iterator iter = TestB->begin(&graph_);
         iter != TestB->end(&graph_); ++iter) {
        TestAB = static_cast<IFMapNode *>(iter.operator->());
    	EXPECT_EQ("testdata", TestAB->name());
    }
}

TEST_F(CfgTest, NodeReaddTest) {

    char buff[1500];
    sprintf(buff, 
        "<update>\n"    
        "   <node type=\"foo\">\n"
        "       <name>testfoo</name>\n"
        "   </node>\n"
        "   <node type=\"bar\">\n"
        "       <name>testbar</name>\n"
        "   </node>\n"
        "   <link>\n"
        "       <node type=\"foo\">\n"
        "           <name>testfoo</name>\n"
        "       </node>\n"
        "       <node type=\"bar\">\n"
        "           <name>testbar</name>\n"
        "       </node>\n"
        "   </link>\n"
        "</update>");


    IFMapTable *ftable = IFMapTable::FindTable(&db_, "foo");
    ASSERT_TRUE(ftable!=NULL);

    IFMapTable *btable = IFMapTable::FindTable(&db_, "bar");
    ASSERT_TRUE(btable!=NULL);

    IFMapLinkTable *ltable = static_cast<IFMapLinkTable *>(
            db_.FindTable(IFMAP_AGENT_LINK_DB_NAME));
    ASSERT_TRUE(ltable!=NULL);

    DBTableBase::ListenerId fid = ftable->
            Register(boost::bind(&CfgTest_NodeReaddTest_Test::EmptyListener, this, _1, _2));
    DBTableBase::ListenerId bid = btable->
            Register(boost::bind(&CfgTest_NodeReaddTest_Test::EmptyListener, this, _1, _2));
    DBTableBase::ListenerId lid = ltable->
            Register(boost::bind(&CfgTest_NodeReaddTest_Test::EmptyListener, this, _1, _2));

    pugi::xml_document xdoc_;
    pugi::xml_parse_result result = xdoc_.load(buff);
    EXPECT_TRUE(result);

    parser_->ConfigParse(xdoc_, 0);
    WaitForIdle();

    //Ensure that both nodes are added fine along with attribute
    IFMapNode *TestFoo = ftable->FindNode("testfoo");
    ASSERT_TRUE(TestFoo !=NULL);
    EXPECT_EQ("testfoo", TestFoo->name());

    IFMapNode *TestBar = btable->FindNode("testbar");
    ASSERT_TRUE(TestBar !=NULL);
    EXPECT_EQ("testbar", TestBar->name());

    //Ensure that there is Link from Foo to Bar
    for (DBGraphVertex::adjacency_iterator iter = TestFoo->begin(&graph_);
         iter != TestFoo->end(&graph_); ++iter) {
        TestBar = static_cast<IFMapNode *>(iter.operator->());
    	EXPECT_EQ("testbar", TestBar->name());
    }

    //Ensure that there is link from Bar to Foo
    for (DBGraphVertex::adjacency_iterator iter = TestBar->begin(&graph_);
         iter != TestBar->end(&graph_); ++iter) {
        TestFoo = static_cast<IFMapNode *>(iter.operator->());
    	EXPECT_EQ("testfoo", TestFoo->name());
    }

    //Delete the all config and readdd the same 
    sprintf(buff, 
        "<delete>\n"    
        "   <link>\n"
        "       <node type=\"foo\">\n"
        "           <name>testfoo</name>\n"
        "       </node>\n"
        "       <node type=\"bar\">\n"
        "           <name>testbar</name>\n"
        "       </node>\n"
        "   </link>\n"
        "   <node type=\"foo\">\n"
        "       <name>testfoo</name>\n"
        "   </node>\n"
        "   <node type=\"bar\">\n"
        "       <name>testbar</name>\n"
        "   </node>\n"
        "</delete>\n"
        "<update>\n"    
        "   <node type=\"foo\">\n"
        "       <name>testfoo</name>\n"
        "   </node>\n"
        "   <node type=\"bar\">\n"
        "       <name>testbar</name>\n"
        "   </node>\n"
        "   <link>\n"
        "       <node type=\"foo\">\n"
        "           <name>testfoo</name>\n"
        "       </node>\n"
        "       <node type=\"bar\">\n"
        "           <name>testbar</name>\n"
        "       </node>\n"
        "   </link>\n"
        "</update>");

    result = xdoc_.load(buff);
    EXPECT_TRUE(result);
    parser_->ConfigParse(xdoc_, 0);
    WaitForIdle();

    //Ensure that both nodes are added fine along with attribute
    TestFoo = ftable->FindNode("testfoo");
    ASSERT_TRUE(TestFoo !=NULL);
    EXPECT_EQ("testfoo", TestFoo->name());

    TestBar = btable->FindNode("testbar");
    ASSERT_TRUE(TestBar !=NULL);
    EXPECT_EQ("testbar", TestBar->name());
    //Ensure that there is Link from Foo to Bar
    for (DBGraphVertex::adjacency_iterator iter = TestFoo->begin(&graph_);
         iter != TestFoo->end(&graph_); ++iter) {
        TestBar = static_cast<IFMapNode *>(iter.operator->());
    	EXPECT_EQ("testbar", TestBar->name());
    }

    //Ensure that there is link from Bar to Foo
    for (DBGraphVertex::adjacency_iterator iter = TestBar->begin(&graph_);
         iter != TestBar->end(&graph_); ++iter) {
        TestFoo = static_cast<IFMapNode *>(iter.operator->());
    	EXPECT_EQ("testfoo", TestFoo->name());
    }
    ftable->Unregister(fid);
    btable->Unregister(bid);
    ltable->Unregister(lid);
}

TEST_F(CfgTest, LinkReorderTest) {
    char buff[1500];
    sprintf(buff, 
        "<update>\n"    
        "   <link>\n"
        "       <node type=\"foo\">\n"
        "           <name>testfoo</name>\n"
        "       </node>\n"
        "       <node type=\"bar\">\n"
        "           <name>testbar</name>\n"
        "       </node>\n"
        "   </link>\n"
        "   <link>\n"
        "       <node type=\"foo\">\n"
        "           <name>testfoo</name>\n"
        "       </node>\n"
        "       <node type=\"test\">\n"
        "           <name>testtest</name>\n"
        "       </node>\n"
        "   </link>\n"
        "   <node type=\"foo\">\n"
        "       <name>testfoo</name>\n"
        "   </node>\n"
        "   <node type=\"bar\">\n"
        "       <name>testbar</name>\n"
        "   </node>\n"
        "   <node type=\"test\">\n"
        "       <name>testtest</name>\n"
        "   </node>\n"
        "</update>");

    IFMapTable *ftable = IFMapTable::FindTable(&db_, "foo");
    ASSERT_TRUE(ftable!=NULL);

    IFMapTable *btable = IFMapTable::FindTable(&db_, "bar");
    ASSERT_TRUE(btable!=NULL);

    IFMapTable *ttable = IFMapTable::FindTable(&db_, "test");
    ASSERT_TRUE(ttable!=NULL);

    IFMapAgentLinkTable *ltable = static_cast<IFMapAgentLinkTable *>(
            db_.FindTable(IFMAP_AGENT_LINK_DB_NAME));
    ASSERT_TRUE(ltable!=NULL);

    pugi::xml_parse_result result = xdoc_.load(buff);
    EXPECT_TRUE(result);
    parser_->ConfigParse(xdoc_, 0);
    WaitForIdle();

    //Ensure that both nodes are added fine along with attribute
    IFMapNode *TestFoo = ftable->FindNode("testfoo");
    ASSERT_TRUE(TestFoo !=NULL);
    EXPECT_EQ("testfoo", TestFoo->name());

    IFMapNode *TestBar = btable->FindNode("testbar");
    ASSERT_TRUE(TestBar !=NULL);
    EXPECT_EQ("testbar", TestBar->name());

    IFMapNode *TestTest = ttable->FindNode("testtest");
    ASSERT_TRUE(TestTest !=NULL);
    EXPECT_EQ("testtest", TestTest->name());

    //Ensure that there is Link from Foo to Bar
    int cnt = 0;
    for (DBGraphVertex::adjacency_iterator iter = TestFoo->begin(&graph_);
         iter != TestFoo->end(&graph_); ++iter) {
        cnt++;
    }
    EXPECT_EQ(cnt, 2);

    //Ensure that there is link from Bar to Foo
    for (DBGraphVertex::adjacency_iterator iter = TestBar->begin(&graph_);
         iter != TestBar->end(&graph_); ++iter) {
        TestFoo = static_cast<IFMapNode *>(iter.operator->());
    	EXPECT_EQ("testfoo", TestFoo->name());
    }

    //Ensure that there is link from Test to Foo
    for (DBGraphVertex::adjacency_iterator iter = TestTest->begin(&graph_);
         iter != TestBar->end(&graph_); ++iter) {
        TestFoo = static_cast<IFMapNode *>(iter.operator->());
    	EXPECT_EQ("testfoo", TestFoo->name());
    }

    //Delete the all config and add one link with seq 0
    sprintf(buff, 
        "<delete>\n"    
        "   <link>\n"
        "       <node type=\"foo\">\n"
        "           <name>testfoo</name>\n"
        "       </node>\n"
        "       <node type=\"bar\">\n"
        "           <name>testbar</name>\n"
        "       </node>\n"
        "   </link>\n"
        "   <link>\n"
        "       <node type=\"foo\">\n"
        "           <name>testfoo</name>\n"
        "       </node>\n"
        "       <node type=\"test\">\n"
        "           <name>testtest</name>\n"
        "       </node>\n"
        "   </link>\n"
        "   <node type=\"foo\">\n"
        "       <name>testfoo</name>\n"
        "   </node>\n"
        "   <node type=\"bar\">\n"
        "       <name>testbar</name>\n"
        "   </node>\n"
        "   <node type=\"test\">\n"
        "       <name>testtest</name>\n"
        "   </node>\n"
        "</delete>\n"
        "<update>\n"    
        "   <link>\n"
        "       <node type=\"foo\">\n"
        "           <name>testfoo</name>\n"
        "       </node>\n"
        "       <node type=\"bar\">\n"
        "           <name>testbar</name>\n"
        "       </node>\n"
        "   </link>\n"
        "</update>\n");

    result = xdoc_.load(buff);
    EXPECT_TRUE(result);
    parser_->ConfigParse(xdoc_, 0);
    WaitForIdle();

    //Add another link and nodes with seq = 1
    sprintf(buff, 
        "<update>\n"    
        "   <link>\n"
        "       <node type=\"foo\">\n"
        "           <name>testfoo</name>\n"
        "       </node>\n"
        "       <node type=\"test\">\n"
        "           <name>testtest</name>\n"
        "       </node>\n"
        "   </link>\n"
        "   <node type=\"foo\">\n"
        "       <name>testfoo</name>\n"
        "   </node>\n"
        "   <node type=\"bar\">\n"
        "       <name>testbar</name>\n"
        "   </node>\n"
        "   <node type=\"test\">\n"
        "       <name>testtest</name>\n"
        "   </node>\n"
        "</update>\n");

    result = xdoc_.load(buff);
    EXPECT_TRUE(result);
    parser_->ConfigParse(xdoc_, 1);
    WaitForIdle();

    TestFoo = ftable->FindNode("testfoo");
    ASSERT_TRUE(TestFoo !=NULL);
    EXPECT_EQ("testfoo", TestFoo->name());

    TestBar = btable->FindNode("testbar");
    ASSERT_TRUE(TestBar !=NULL);
    EXPECT_EQ("testbar", TestBar->name());

    TestTest = ttable->FindNode("testtest");
    ASSERT_TRUE(TestBar !=NULL);
    EXPECT_EQ("testtest", TestTest->name());

    //Ensure that there is link only from foo->test and not foo->bar
    for (DBGraphVertex::adjacency_iterator iter = TestFoo->begin(&graph_);
         iter != TestFoo->end(&graph_); ++iter) {
        TestTest = static_cast<IFMapNode *>(iter.operator->());
    	EXPECT_EQ("testtest", TestTest->name());
    }

    cnt = 0;
    for (DBGraphVertex::adjacency_iterator iter = TestBar->begin(&graph_);
         iter != TestBar->end(&graph_); ++iter) {
        cnt++;
    }
    EXPECT_EQ(cnt, 0);

    //Delete the old seq links
    ltable->DestroyDefLink();

    //Delete all config
    sprintf(buff, 
        "<delete>\n"    
        "   <link>\n"
        "       <node type=\"foo\">\n"
        "           <name>testfoo</name>\n"
        "       </node>\n"
        "       <node type=\"test\">\n"
        "           <name>testtest</name>\n"
        "       </node>\n"
        "   </link>\n"
        "   <node type=\"foo\">\n"
        "       <name>testfoo</name>\n"
        "   </node>\n"
        "   <node type=\"bar\">\n"
        "       <name>testbar</name>\n"
        "   </node>\n"
        "   <node type=\"test\">\n"
        "       <name>testtest</name>\n"
        "   </node>\n"
        "</delete>\n");

    result = xdoc_.load(buff);
    EXPECT_TRUE(result);
    parser_->ConfigParse(xdoc_, 1);
    WaitForIdle();

    //Add link with seq 0
    sprintf(buff, 
        "<update>\n"    
        "   <link>\n"
        "       <node type=\"foo\">\n"
        "           <name>testfoo</name>\n"
        "       </node>\n"
        "       <node type=\"test\">\n"
        "           <name>testtest</name>\n"
        "       </node>\n"
        "   </link>\n"
        "</update>\n");   

    result = xdoc_.load(buff);
    EXPECT_TRUE(result);
    parser_->ConfigParse(xdoc_, 0);
    WaitForIdle();

    //update the same with seq 1 and add nodes too
    sprintf(buff, 
        "<update>\n"    
        "   <link>\n"
        "       <node type=\"test\">\n"
        "           <name>testtest</name>\n"
        "       </node>\n"
        "       <node type=\"foo\">\n"
        "           <name>testfoo</name>\n"
        "       </node>\n"
        "   </link>\n"
        "   <node type=\"foo\">\n"
        "       <name>testfoo</name>\n"
        "   </node>\n"
        "   <node type=\"test\">\n"
        "       <name>testtest</name>\n"
        "   </node>\n"
        "</update>\n");

    result = xdoc_.load(buff);
    EXPECT_TRUE(result);
    parser_->ConfigParse(xdoc_, 1);
    WaitForIdle();

    TestFoo = ftable->FindNode("testfoo");
    ASSERT_TRUE(TestFoo !=NULL);
    EXPECT_EQ("testfoo", TestFoo->name());

    TestTest = ttable->FindNode("testtest");
    ASSERT_TRUE(TestTest !=NULL);
    EXPECT_EQ("testtest", TestTest->name());
        
    //Ensure that there is link from foo->test 
    for (DBGraphVertex::adjacency_iterator iter = TestFoo->begin(&graph_);
         iter != TestFoo->end(&graph_); ++iter) {
        TestTest = static_cast<IFMapNode *>(iter.operator->());
    	EXPECT_EQ("testtest", TestTest->name());
    }

    sprintf(buff, 
        "<delete>\n"    
        "   <link>\n"
        "       <node type=\"test\">\n"
        "           <name>testtest</name>\n"
        "       </node>\n"
        "       <node type=\"foo\">\n"
        "           <name>testfoo</name>\n"
        "       </node>\n"
        "   </link>\n"
        "   <node type=\"foo\">\n"
        "       <name>testfoo</name>\n"
        "   </node>\n"
        "   <node type=\"test\">\n"
        "       <name>testtest</name>\n"
        "   </node>\n"
        "</delete>\n");

    result = xdoc_.load(buff);
    EXPECT_TRUE(result);
    parser_->ConfigParse(xdoc_, 1);
    WaitForIdle();

    sprintf(buff, 
        "<update>\n"    
        "   <link>\n"
        "       <node type=\"test\">\n"
        "           <name>testtest</name>\n"
        "       </node>\n"
        "       <node type=\"foo\">\n"
        "           <name>testfoo</name>\n"
        "       </node>\n"
        "   </link>\n"
        "</update>\n"    
        "<delete>\n"    
        "   <link>\n"
        "       <node type=\"test\">\n"
        "           <name>testtest</name>\n"
        "       </node>\n"
        "       <node type=\"foo\">\n"
        "           <name>testfoo</name>\n"
        "       </node>\n"
        "   </link>\n"
        "</delete>\n"    
        "<update>\n"    
        "   <link>\n"
        "       <node type=\"test\">\n"
        "           <name>testtest</name>\n"
        "       </node>\n"
        "       <node type=\"foo\">\n"
        "           <name>testfoo</name>\n"
        "       </node>\n"
        "   </link>\n"
        "</update>\n"    
        "<delete>\n"    
        "   <link>\n"
        "       <node type=\"foo\">\n"
        "           <name>testfoo</name>\n"
        "       </node>\n"
        "       <node type=\"test\">\n"
        "           <name>testtest</name>\n"
        "       </node>\n"
        "   </link>\n"
        "</delete>\n"    
        "<update>\n"    
        "   <node type=\"foo\">\n"
        "       <name>testfoo</name>\n"
        "   </node>\n"
        "   <node type=\"test\">\n"
        "       <name>testtest</name>\n"
        "   </node>\n"
        "</update>\n");    

    result = xdoc_.load(buff);
    EXPECT_TRUE(result);
    parser_->ConfigParse(xdoc_, 0);
    WaitForIdle();

    TestFoo = ftable->FindNode("testfoo");
    ASSERT_TRUE(TestFoo !=NULL);
    EXPECT_EQ("testfoo", TestFoo->name());

    TestTest = ttable->FindNode("testtest");
    ASSERT_TRUE(TestTest !=NULL);
    EXPECT_EQ("testtest", TestTest->name()); 

    //Ensure that there are no links from foo->test and test->foo
    cnt = 0;
    for (DBGraphVertex::adjacency_iterator iter = TestFoo->begin(&graph_);
         iter != TestFoo->end(&graph_); ++iter) {
        cnt++;
    }
    EXPECT_EQ(cnt, 0);

    for (DBGraphVertex::adjacency_iterator iter = TestTest->begin(&graph_);
         iter != TestTest->end(&graph_); ++iter) {
        cnt++;
    }
    EXPECT_EQ(cnt, 0);

    sprintf(buff, 
        "<update>\n"    
        "   <link>\n"
        "       <node type=\"test\">\n"
        "           <name>testtest</name>\n"
        "       </node>\n"
        "       <node type=\"foo\">\n"
        "           <name>testfoo</name>\n"
        "       </node>\n"
        "   </link>\n"
        "   <link>\n"
        "       <node type=\"foo\">\n"
        "           <name>testfoo</name>\n"
        "       </node>\n"
        "       <node type=\"bar\">\n"
        "           <name>testbar</name>\n"
        "       </node>\n"
        "   </link>\n"
        "</update>\n");    
    result = xdoc_.load(buff);
    EXPECT_TRUE(result);
    parser_->ConfigParse(xdoc_, 0);
    WaitForIdle();

    //Delete unadded links
    ltable->DestroyDefLink();
}

TEST_F(CfgTest, NodeDeleteLinkPendingTest) {
    char buff[1500];
    sprintf(buff, 
        "<update>\n"    
        "   <link>\n"
        "       <node type=\"foo\">\n"
        "           <name>testfoo</name>\n"
        "       </node>\n"
        "       <node type=\"bar\">\n"
        "           <name>testbar</name>\n"
        "       </node>\n"
        "   </link>\n"
        "   <link>\n"
        "       <node type=\"foo\">\n"
        "           <name>testfoo</name>\n"
        "       </node>\n"
        "       <node type=\"test\">\n"
        "           <name>testtest</name>\n"
        "       </node>\n"
        "   </link>\n"
        "   <node type=\"foo\">\n"
        "       <name>testfoo</name>\n"
        "   </node>\n"
        "   <node type=\"bar\">\n"
        "       <name>testbar</name>\n"
        "   </node>\n"
        "   <node type=\"test\">\n"
        "       <name>testtest</name>\n"
        "   </node>\n"
        "</update>");

    IFMapTable *ftable = IFMapTable::FindTable(&db_, "foo");
    ASSERT_TRUE(ftable!=NULL);

    IFMapTable *btable = IFMapTable::FindTable(&db_, "bar");
    ASSERT_TRUE(btable!=NULL);

    IFMapTable *ttable = IFMapTable::FindTable(&db_, "test");
    ASSERT_TRUE(ttable!=NULL);

    IFMapAgentLinkTable *ltable = static_cast<IFMapAgentLinkTable *>(
            db_.FindTable(IFMAP_AGENT_LINK_DB_NAME));
    ASSERT_TRUE(ltable!=NULL);

    pugi::xml_parse_result result = xdoc_.load(buff);
    EXPECT_TRUE(result);
    parser_->ConfigParse(xdoc_, 0);
    WaitForIdle();

    //Ensure that both nodes are added fine along with attribute
    IFMapNode *TestFoo = ftable->FindNode("testfoo");
    ASSERT_TRUE(TestFoo !=NULL);
    EXPECT_EQ("testfoo", TestFoo->name());

    IFMapNode *TestBar = btable->FindNode("testbar");
    ASSERT_TRUE(TestBar !=NULL);
    EXPECT_EQ("testbar", TestBar->name());

    IFMapNode *TestTest = ttable->FindNode("testtest");
    ASSERT_TRUE(TestTest !=NULL);
    EXPECT_EQ("testtest", TestTest->name());

    //Ensure that there is Link from Foo to Bar
    int cnt = 0;
    for (DBGraphVertex::adjacency_iterator iter = TestFoo->begin(&graph_);
         iter != TestFoo->end(&graph_); ++iter) {
        cnt++;
    }
    EXPECT_EQ(cnt, 2);

    //Ensure that there is link from Bar to Foo
    for (DBGraphVertex::adjacency_iterator iter = TestBar->begin(&graph_);
         iter != TestBar->end(&graph_); ++iter) {
        TestFoo = static_cast<IFMapNode *>(iter.operator->());
    	EXPECT_EQ("testfoo", TestFoo->name());
    }

    //Ensure that there is link from Test to Foo
    for (DBGraphVertex::adjacency_iterator iter = TestTest->begin(&graph_);
         iter != TestBar->end(&graph_); ++iter) {
        TestFoo = static_cast<IFMapNode *>(iter.operator->());
    	EXPECT_EQ("testfoo", TestFoo->name());
    }

    //Now delete the node without deleting links
    sprintf(buff, 
        "<delete>\n"    
        "   <node type=\"foo\">\n"
        "       <name>testfoo</name>\n"
        "   </node>\n"
        "</delete>");

    result = xdoc_.load(buff);
    EXPECT_TRUE(result);
    parser_->ConfigParse(xdoc_, 0);
    WaitForIdle();

    //testfoo should not exist
    TestFoo = ftable->FindNode("testfoo");
    ASSERT_TRUE(TestFoo ==NULL);

    //testbar and testtest should exist
    TestTest = ttable->FindNode("testtest");
    ASSERT_TRUE(TestTest !=NULL);
    EXPECT_EQ("testtest", TestTest->name()); 

    TestBar = btable->FindNode("testbar");
    ASSERT_TRUE(TestBar !=NULL);
    EXPECT_EQ("testbar", TestBar->name()); 

    //Ensure that there are no links from test->foo and bar->foo
    cnt = 0;
    for (DBGraphVertex::adjacency_iterator iter = TestBar->begin(&graph_);
         iter != TestBar->end(&graph_); ++iter) {
        cnt++;
    }
    EXPECT_EQ(cnt, 0);

    for (DBGraphVertex::adjacency_iterator iter = TestTest->begin(&graph_);
         iter != TestTest->end(&graph_); ++iter) {
        cnt++;
    }
    EXPECT_EQ(cnt, 0);

    //Add node foo again and veriy all nodes and links
    sprintf(buff, 
        "<update>\n"    
        "   <node type=\"foo\">\n"
        "       <name>testfoo</name>\n"
        "   </node>\n"
        "</update>");

    result = xdoc_.load(buff);
    EXPECT_TRUE(result);
    parser_->ConfigParse(xdoc_, 0);
    WaitForIdle();

    TestFoo = ftable->FindNode("testfoo");
    ASSERT_TRUE(TestFoo !=NULL);
    EXPECT_EQ("testfoo", TestFoo->name());

    TestBar = btable->FindNode("testbar");
    ASSERT_TRUE(TestBar !=NULL);
    EXPECT_EQ("testbar", TestBar->name());

    TestTest = ttable->FindNode("testtest");
    ASSERT_TRUE(TestTest !=NULL);
    EXPECT_EQ("testtest", TestTest->name());

    //Verify that there is Link from Foo to Bar and Test
    cnt = 0;
    for (DBGraphVertex::adjacency_iterator iter = TestFoo->begin(&graph_);
         iter != TestFoo->end(&graph_); ++iter) {
        cnt++;
    }
    EXPECT_EQ(cnt, 2);

    //Verify that there is link from Bar to Foo
    for (DBGraphVertex::adjacency_iterator iter = TestBar->begin(&graph_);
         iter != TestBar->end(&graph_); ++iter) {
        TestFoo = static_cast<IFMapNode *>(iter.operator->());
    	EXPECT_EQ("testfoo", TestFoo->name());
    }

    //Verify that there is link from Test to Foo
    for (DBGraphVertex::adjacency_iterator iter = TestTest->begin(&graph_);
         iter != TestBar->end(&graph_); ++iter) {
        TestFoo = static_cast<IFMapNode *>(iter.operator->());
    	EXPECT_EQ("testfoo", TestFoo->name());
    }

    //Now delete the node without deleting links
    sprintf(buff, 
        "<delete>\n"    
        "   <node type=\"foo\">\n"
        "       <name>testfoo</name>\n"
        "   </node>\n"
        "</delete>");

    result = xdoc_.load(buff);
    EXPECT_TRUE(result);
    parser_->ConfigParse(xdoc_, 0);
    WaitForIdle();

    //Now delete links and add node
    sprintf(buff, 
        "<delete>\n"    
        "   <link>\n"
        "       <node type=\"foo\">\n"
        "           <name>testfoo</name>\n"
        "       </node>\n"
        "       <node type=\"bar\">\n"
        "           <name>testbar</name>\n"
        "       </node>\n"
        "   </link>\n"
        "   <link>\n"
        "       <node type=\"foo\">\n"
        "           <name>testfoo</name>\n"
        "       </node>\n"
        "       <node type=\"test\">\n"
        "           <name>testtest</name>\n"
        "       </node>\n"
        "   </link>\n"
        "</delete>"
        "<update>\n"    
        "   <node type=\"foo\">\n"
        "       <name>testfoo</name>\n"
        "   </node>\n"
        "</update>");

    result = xdoc_.load(buff);
    EXPECT_TRUE(result);
    parser_->ConfigParse(xdoc_, 0);
    WaitForIdle();

    TestFoo = ftable->FindNode("testfoo");
    ASSERT_TRUE(TestFoo !=NULL);
    EXPECT_EQ("testfoo", TestFoo->name());

    TestBar = btable->FindNode("testbar");
    ASSERT_TRUE(TestBar !=NULL);
    EXPECT_EQ("testbar", TestBar->name());

    TestTest = ttable->FindNode("testtest");
    ASSERT_TRUE(TestTest !=NULL);
    EXPECT_EQ("testtest", TestTest->name());

    //Verify that there are no links from test->foo and bar->foo
    cnt = 0;
    for (DBGraphVertex::adjacency_iterator iter = TestBar->begin(&graph_);
         iter != TestBar->end(&graph_); ++iter) {
        cnt++;
    }
    EXPECT_EQ(cnt, 0);

    for (DBGraphVertex::adjacency_iterator iter = TestTest->begin(&graph_);
         iter != TestTest->end(&graph_); ++iter) {
        cnt++;
    }
    EXPECT_EQ(cnt, 0);

    //Have all the nodes and links and delete bar
    sprintf(buff, 
        "<update>\n"    
        "   <link>\n"
        "       <node type=\"foo\">\n"
        "           <name>testfoo</name>\n"
        "       </node>\n"
        "       <node type=\"bar\">\n"
        "           <name>testbar</name>\n"
        "       </node>\n"
        "   </link>\n"
        "   <link>\n"
        "       <node type=\"foo\">\n"
        "           <name>testfoo</name>\n"
        "       </node>\n"
        "       <node type=\"test\">\n"
        "           <name>testtest</name>\n"
        "       </node>\n"
        "   </link>\n"
        "</update>"
        "<delete>\n"    
        "   <node type=\"bar\">\n"
        "       <name>testbar</name>\n"
        "   </node>\n"
        "</delete>");

    result = xdoc_.load(buff);
    EXPECT_TRUE(result);
    parser_->ConfigParse(xdoc_, 0);
    WaitForIdle();

    //Verify that bar does not exists, foo test exists and a link
    //between foo and test
    //Ensure that both nodes are added fine along with attribute
    TestFoo = ftable->FindNode("testfoo");
    ASSERT_TRUE(TestFoo !=NULL);
    EXPECT_EQ("testfoo", TestFoo->name());

    TestBar = btable->FindNode("testbar");
    ASSERT_TRUE(TestBar ==NULL);

    TestTest = ttable->FindNode("testtest");
    ASSERT_TRUE(TestTest !=NULL);
    EXPECT_EQ("testtest", TestTest->name());

    //Ensure that there is Link from Foo to test
    for (DBGraphVertex::adjacency_iterator iter = TestFoo->begin(&graph_);
         iter != TestFoo->end(&graph_); ++iter) {
        TestTest = static_cast<IFMapNode *>(iter.operator->());
    	EXPECT_EQ("testtest", TestTest->name());
    }

    // cleanup ifmap DB.
    sprintf(buff, 
        "<delete>\n"
        "   <link>\n"
        "       <node type=\"foo\">\n"
        "           <name>testfoo</name>\n"
        "       </node>\n"
        "       <node type=\"bar\">\n"
        "           <name>testbar</name>\n"
        "       </node>\n"
        "   </link>\n"
        "   <link>\n"
        "       <node type=\"foo\">\n"
        "           <name>testfoo</name>\n"
        "       </node>\n"
        "       <node type=\"test\">\n"
        "           <name>testtest</name>\n"
        "       </node>\n"
        "   </link>\n"
        "</delete>\n"
        "<delete>\n"    
        "   <node type=\"test\">\n"
        "       <name>testtest</name>\n"
        "   </node>\n"
        "</delete>\n"
        "<delete>\n"    
        "   <node type=\"foo\">\n"
        "       <name>testfoo</name>\n"
        "   </node>\n"
        "</delete>");

    result = xdoc_.load(buff);
    EXPECT_TRUE(result);
    parser_->ConfigParse(xdoc_, 0);
    WaitForIdle();

}

TEST_F(CfgTest, NodeDelLinkAddDeferTest) {

    char buff[1500];
    sprintf(buff, 
        "<update>\n"    
        "   <node type=\"foo\">\n"
        "       <name>testfoo</name>\n"
        "   </node>\n"
        "   <node type=\"bar\">\n"
        "       <name>testbar</name>\n"
        "   </node>\n"
        "   <link>\n"
        "       <node type=\"foo\">\n"
        "           <name>testfoo</name>\n"
        "       </node>\n"
        "       <node type=\"bar\">\n"
        "           <name>testbar</name>\n"
        "       </node>\n"
        "   </link>\n"
        "</update>");


    IFMapTable *ftable = IFMapTable::FindTable(&db_, "foo");
    ASSERT_TRUE(ftable!=NULL);

    IFMapTable *btable = IFMapTable::FindTable(&db_, "bar");
    ASSERT_TRUE(btable!=NULL);

    IFMapLinkTable *ltable = static_cast<IFMapLinkTable *>(
            db_.FindTable(IFMAP_AGENT_LINK_DB_NAME));
    ASSERT_TRUE(ltable!=NULL);

    DBTableBase::ListenerId fid = ftable->
            Register(boost::bind(&CfgTest_NodeDelLinkAddDeferTest_Test::EmptyListener, this, _1, _2));
    DBTableBase::ListenerId bid = btable->
            Register(boost::bind(&CfgTest_NodeDelLinkAddDeferTest_Test::EmptyListener, this, _1, _2));
    DBTableBase::ListenerId lid = ltable->
            Register(boost::bind(&CfgTest_NodeDelLinkAddDeferTest_Test::EmptyListener, this, _1, _2));

    pugi::xml_document xdoc_;
    pugi::xml_parse_result result = xdoc_.load(buff);
    EXPECT_TRUE(result);

    parser_->ConfigParse(xdoc_, 0);
    WaitForIdle();

    //Ensure that both nodes are added fine along with attribute
    IFMapNode *TestFoo = ftable->FindNode("testfoo");
    ASSERT_TRUE(TestFoo !=NULL);
    EXPECT_EQ("testfoo", TestFoo->name());

    IFMapNode *TestBar = btable->FindNode("testbar");
    ASSERT_TRUE(TestBar !=NULL);
    EXPECT_EQ("testbar", TestBar->name());

    //Ensure that there is Link from Foo to Bar
    for (DBGraphVertex::adjacency_iterator iter = TestFoo->begin(&graph_);
         iter != TestFoo->end(&graph_); ++iter) {
        TestBar = static_cast<IFMapNode *>(iter.operator->());
    	EXPECT_EQ("testbar", TestBar->name());
    }

    //Ensure that there is link from Bar to Foo
    for (DBGraphVertex::adjacency_iterator iter = TestBar->begin(&graph_);
         iter != TestBar->end(&graph_); ++iter) {
        TestFoo = static_cast<IFMapNode *>(iter.operator->());
    	EXPECT_EQ("testfoo", TestFoo->name());
    }

    //Delete the all config and readdd the same 
    sprintf(buff, 
        "<delete>\n"    
        "   <link>\n"
        "       <node type=\"foo\">\n"
        "           <name>testfoo</name>\n"
        "       </node>\n"
        "       <node type=\"bar\">\n"
        "           <name>testbar</name>\n"
        "       </node>\n"
        "   </link>\n"
        "   <node type=\"foo\">\n"
        "       <name>testfoo</name>\n"
        "   </node>\n"
        "   <node type=\"bar\">\n"
        "       <name>testbar</name>\n"
        "   </node>\n"
        "</delete>\n"
        "<update>\n"    
        "   <node type=\"bar\">\n"
        "       <name>testbar</name>\n"
        "   </node>\n"
        "   <link>\n"
        "       <node type=\"foo\">\n"
        "           <name>testfoo</name>\n"
        "       </node>\n"
        "       <node type=\"bar\">\n"
        "           <name>testbar</name>\n"
        "       </node>\n"
        "   </link>\n"
        "   <node type=\"foo\">\n"
        "       <name>testfoo</name>\n"
        "   </node>\n"
        "</update>");

    result = xdoc_.load(buff);
    EXPECT_TRUE(result);
    parser_->ConfigParse(xdoc_, 0);
    WaitForIdle();

    //Ensure that both nodes are added fine along with attribute
    TestFoo = ftable->FindNode("testfoo");
    ASSERT_TRUE(TestFoo !=NULL);
    EXPECT_EQ("testfoo", TestFoo->name());

    TestBar = btable->FindNode("testbar");
    ASSERT_TRUE(TestBar !=NULL);
    EXPECT_EQ("testbar", TestBar->name());
    //Ensure that there is Link from Foo to Bar
    for (DBGraphVertex::adjacency_iterator iter = TestFoo->begin(&graph_);
         iter != TestFoo->end(&graph_); ++iter) {
        TestBar = static_cast<IFMapNode *>(iter.operator->());
    	EXPECT_EQ("testbar", TestBar->name());
    }

    //Ensure that there is link from Bar to Foo
    for (DBGraphVertex::adjacency_iterator iter = TestBar->begin(&graph_);
         iter != TestBar->end(&graph_); ++iter) {
        TestFoo = static_cast<IFMapNode *>(iter.operator->());
    	EXPECT_EQ("testfoo", TestFoo->name());
    }
    ftable->Unregister(fid);
    btable->Unregister(bid);
    ltable->Unregister(lid);
}

TEST_F(CfgTest, LinkJumbleTest) {
    char buff[1500];
    sprintf(buff, 
        "<update>\n"    
        "   <link>\n"
        "       <node type=\"foo\">\n"
        "           <name>testfoo</name>\n"
        "       </node>\n"
        "       <node type=\"bar\">\n"
        "           <name>testbar</name>\n"
        "       </node>\n"
        "   </link>\n"
        "   <link>\n"
        "       <node type=\"foo\">\n"
        "           <name>testfoo</name>\n"
        "       </node>\n"
        "       <node type=\"test\">\n"
        "           <name>testtest</name>\n"
        "       </node>\n"
        "   </link>\n"
        "   <node type=\"foo\">\n"
        "       <name>testfoo</name>\n"
        "   </node>\n"
        "   <node type=\"test\">\n"
        "       <name>testtest</name>\n"
        "   </node>\n"
        "</update>");

    IFMapTable *ftable = IFMapTable::FindTable(&db_, "foo");
    ASSERT_TRUE(ftable!=NULL);

    IFMapTable *btable = IFMapTable::FindTable(&db_, "bar");
    ASSERT_TRUE(btable!=NULL);

    IFMapTable *ttable = IFMapTable::FindTable(&db_, "test");
    ASSERT_TRUE(ttable!=NULL);

    IFMapAgentLinkTable *ltable = static_cast<IFMapAgentLinkTable *>(
            db_.FindTable(IFMAP_AGENT_LINK_DB_NAME));
    ASSERT_TRUE(ltable!=NULL);

    pugi::xml_parse_result result = xdoc_.load(buff);
    EXPECT_TRUE(result);
    parser_->ConfigParse(xdoc_, 0);
    WaitForIdle();

    //Ensure that both nodes are added fine along with attribute
    IFMapNode *TestFoo = ftable->FindNode("testfoo");
    ASSERT_TRUE(TestFoo !=NULL);
    EXPECT_EQ("testfoo", TestFoo->name());


    IFMapNode *TestTest = ttable->FindNode("testtest");
    ASSERT_TRUE(TestTest !=NULL);
    EXPECT_EQ("testtest", TestTest->name());

    //Bar is not added so it should not erxist
    IFMapNode *TestBar = btable->FindNode("testbar");
    ASSERT_TRUE(TestBar == NULL);


    //Ensure that there is link from Test to Foo
    for (DBGraphVertex::adjacency_iterator iter = TestTest->begin(&graph_);
         iter != TestBar->end(&graph_); ++iter) {
        TestFoo = static_cast<IFMapNode *>(iter.operator->());
    	EXPECT_EQ("testfoo", TestFoo->name());
    }

    //Add Foo again
    sprintf(buff, 
        "<update>\n"    
        "    <node type=\"foo\">\n"
        "        <name>testfoo</name>\n"
        "    </node>\n"
        "</update>\n");

    result = xdoc_.load(buff);
    EXPECT_TRUE(result);
    parser_->ConfigParse(xdoc_, 0);
    WaitForIdle();

    //Bar is still not added
    TestBar = btable->FindNode("testbar");
    ASSERT_TRUE(TestBar == NULL);

    //Add bar now
    sprintf(buff, 
        "<update>\n"    
        "    <node type=\"bar\">\n"
        "        <name>testbar</name>\n"
        "    </node>\n"
        "</update>\n");

    result = xdoc_.load(buff);
    EXPECT_TRUE(result);
    parser_->ConfigParse(xdoc_, 0);
    WaitForIdle();

    TestFoo = ftable->FindNode("testfoo");
    ASSERT_TRUE(TestFoo !=NULL);
    EXPECT_EQ("testfoo", TestFoo->name());


    TestTest = ttable->FindNode("testtest");
    ASSERT_TRUE(TestTest !=NULL);
    EXPECT_EQ("testtest", TestTest->name());

    TestBar = btable->FindNode("testbar");
    ASSERT_TRUE(TestBar != NULL);
    EXPECT_EQ("testbar", TestBar->name());
    //Delete unadded links
    ltable->DestroyDefLink();
}

TEST_F(CfgTest, LinkSeqTest) {
    char buff[1500];
    sprintf(buff,
        "<update>\n"
        "   <node type=\"foo\">\n"
        "       <name>testfoo</name>\n"
        "   </node>\n"
        "   <node type=\"test\">\n"
        "       <name>testtest</name>\n"
        "   </node>\n"
        "</update>");

    IFMapTable *ftable = IFMapTable::FindTable(&db_, "foo");
    ASSERT_TRUE(ftable!=NULL);

    IFMapTable *ttable = IFMapTable::FindTable(&db_, "test");
    ASSERT_TRUE(ttable!=NULL);

    pugi::xml_parse_result result = xdoc_.load(buff);
    EXPECT_TRUE(result);
    parser_->ConfigParse(xdoc_, 0);
    WaitForIdle();

    //Ensure that both nodes are added fine along with attribute
    IFMapNode *TestFoo = ftable->FindNode("testfoo");
    ASSERT_TRUE(TestFoo !=NULL);
    EXPECT_EQ("testfoo", TestFoo->name());


    IFMapNode *TestTest = ttable->FindNode("testtest");
    ASSERT_TRUE(TestTest !=NULL);
    EXPECT_EQ("testtest", TestTest->name());

    //Add the link between the nodes with an advanced sequence number
    sprintf(buff,
        "<update>\n"
        "   <link>\n"
        "       <node type=\"foo\">\n"
        "           <name>testfoo</name>\n"
        "       </node>\n"
        "       <node type=\"test\">\n"
        "           <name>testtest</name>\n"
        "       </node>\n"
        "   </link>\n"
        "</update>\n");

    result = xdoc_.load(buff);
    EXPECT_TRUE(result);
    parser_->ConfigParse(xdoc_, 1);
    WaitForIdle();

    //Ensure that there is no link from Test to Foo
    int link_count = 0;
    for (DBGraphVertex::adjacency_iterator iter = TestTest->begin(&graph_);
         iter != TestTest->end(&graph_); ++iter) {
        link_count++;
    }
    EXPECT_EQ(link_count, 0);

    for (DBGraphVertex::adjacency_iterator iter = TestFoo->begin(&graph_);
         iter != TestFoo->end(&graph_); ++iter) {
        link_count++;
    }
    EXPECT_EQ(link_count, 0);


    //Add the Test and Foo with required seq number and verify that link
    //exists
    sprintf(buff,
        "<update>\n"
        "   <node type=\"foo\">\n"
        "       <name>testfoo</name>\n"
        "   </node>\n"
        "   <node type=\"test\">\n"
        "       <name>testtest</name>\n"
        "   </node>\n"
        "</update>");

    result = xdoc_.load(buff);
    EXPECT_TRUE(result);
    parser_->ConfigParse(xdoc_, 1);
    WaitForIdle();

    for (DBGraphVertex::adjacency_iterator iter = TestTest->begin(&graph_);
         iter != TestTest->end(&graph_); ++iter) {
        TestFoo = static_cast<IFMapNode *>(iter.operator->());
        EXPECT_EQ("testfoo", TestFoo->name());
        link_count++;
    }
    EXPECT_EQ(link_count, 1);
}

int main(int argc, char **argv) {
    LoggingInit();
    ::testing::InitGoogleTest(&argc, argv);

    int ret = RUN_ALL_TESTS();
    TaskScheduler::GetInstance()->Terminate();
    return ret;
}

