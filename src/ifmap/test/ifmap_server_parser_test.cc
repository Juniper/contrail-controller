/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "ifmap/ifmap_server_parser.h"

#include <fstream>
#include "base/logging.h"
#include "base/test/task_test_util.h"
#include "control-node/control_node.h"
#include "db/db.h"
#include "db/db_graph.h"
#include "io/event_manager.h"
#include "ifmap/ifmap_client.h"
#include "ifmap/ifmap_link_table.h"
#include "ifmap/ifmap_server.h"
#include "ifmap/ifmap_table.h"
#include "ifmap/ifmap_update_queue.h"
#include "ifmap/ifmap_xmpp.h"
#include "ifmap/test/ifmap_client_mock.h"
#include "ifmap/test/ifmap_test_util.h"
#include "xmpp/xmpp_server.h"

#include "schema/vnc_cfg_types.h"
#include "testing/gunit.h"

using namespace std;

class IFMapChannelManagerMock : public IFMapChannelManager {
public:
    IFMapChannelManagerMock(XmppServer *xmpp_server, IFMapServer *ifmap_server)
            : IFMapChannelManager(xmpp_server, ifmap_server) {
    }

};

class IFMapServerParserTest : public ::testing::Test {
  protected:
    IFMapServerParserTest()
            : server_(&db_, &graph_, evm_.io_service()), parser_(NULL) {
    }

    virtual void SetUp() {
        xmpp_server_ = new XmppServer(&evm_, "bgp.contrail.com");
        IFMapLinkTable_Init(&db_, &graph_);
        parser_ = IFMapServerParser::GetInstance("vnc_cfg");
        vnc_cfg_ParserInit(parser_);
        vnc_cfg_Server_ModuleInit(&db_, &graph_);
        server_.Initialize();
        ifmap_channel_mgr_.reset(new IFMapChannelManagerMock(xmpp_server_,
                                                             &server_));
        server_.set_ifmap_channel_manager(ifmap_channel_mgr_.get());
    }

    virtual void TearDown() {
        server_.Shutdown();
        task_util::WaitForIdle();
        IFMapLinkTable_Clear(&db_);
        IFMapTable::ClearTables(&db_);
        task_util::WaitForIdle();
        db_.Clear();
        parser_->MetadataClear("vnc_cfg");
        task_util::WaitForIdle();
        TcpServerManager::DeleteServer(xmpp_server_);
        xmpp_server_ = NULL;
        task_util::WaitForIdle();
        evm_.Shutdown();
    }

    string FileRead(const string &filename) {
        ifstream file(filename.c_str());
        string content((istreambuf_iterator<char>(file)),
                       istreambuf_iterator<char>());
        return content;
    }

    IFMapNode *NodeLookup(const string &type, const string &name) {
        return ifmap_test_util::IFMapNodeLookup(&db_, type, name);
    }

    IFMapLink *LinkLookup(const IFMapNode *left, const IFMapNode *right) {
        return static_cast<IFMapLink *>(graph_.GetEdge(left, right));
    }

    DB db_;
    DBGraph graph_;
    EventManager evm_;
    IFMapServer server_;
    IFMapServerParser *parser_;
    XmppServer *xmpp_server_;
    auto_ptr<IFMapChannelManagerMock> ifmap_channel_mgr_;
};

// In a single message, adds vn1, vn2, vn3, then deletes, vn3, then adds vn4,
// vn5, then deletes vn5, vn4 and vn2. Only vn1 should remain.
TEST_F(IFMapServerParserTest, ServerParser) {
    IFMapTable *table = IFMapTable::FindTable(&db_, "virtual-network");

    string message(FileRead("src/ifmap/testdata/server_parser_test.xml"));
    assert(message.size() != 0);
    parser_->Receive(&db_, message.data(), message.size(), 0);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, table->Size());

    IFMapNode *vn1 = NodeLookup("virtual-network", "vn1");
    EXPECT_TRUE(vn1 != NULL);
    IFMapObject *obj = vn1->Find(IFMapOrigin(IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(obj != NULL);

    IFMapNode *vn = NodeLookup("virtual-network", "vn2");
    EXPECT_TRUE(vn == NULL);
    vn = NodeLookup("virtual-network", "vn3");
    EXPECT_TRUE(vn == NULL);
    vn = NodeLookup("virtual-network", "vn4");
    EXPECT_TRUE(vn == NULL);
    vn = NodeLookup("virtual-network", "vn5");
    EXPECT_TRUE(vn == NULL);
}

// In 4 separate messages: 1) adds vn1, vn2, vn3, 2) deletes vn3, 3) adds vn4,
// vn5, 4) deletes vn5, vn4 and vn2. Only vn1 should remain.
// Same as ServerParser except that the various operations are happening in
// separate messages.
TEST_F(IFMapServerParserTest, ServerParserInParts) {
    IFMapTable *table = IFMapTable::FindTable(&db_, "virtual-network");

    string message(FileRead("src/ifmap/testdata/server_parser_test_p1.xml"));
    assert(message.size() != 0);
    parser_->Receive(&db_, message.data(), message.size(), 0);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(3, table->Size());

    IFMapNode *vn1 = NodeLookup("virtual-network", "vn1");
    EXPECT_TRUE(vn1 != NULL);
    IFMapObject *obj = vn1->Find(IFMapOrigin(IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(obj != NULL);

    IFMapNode *vn2 = NodeLookup("virtual-network", "vn2");
    EXPECT_TRUE(vn2 != NULL);
    obj = vn2->Find(IFMapOrigin(IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(obj != NULL);

    IFMapNode *vn3 = NodeLookup("virtual-network", "vn3");
    EXPECT_TRUE(vn3 != NULL);
    obj = vn3->Find(IFMapOrigin(IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(obj != NULL);

    message = FileRead("src/ifmap/testdata/server_parser_test_p2.xml");
    assert(message.size() != 0);
    parser_->Receive(&db_, message.data(), message.size(), 0);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(2, table->Size());
    IFMapNode *node = NodeLookup("virtual-network", "vn3");
    EXPECT_TRUE(node == NULL);

    message = FileRead("src/ifmap/testdata/server_parser_test_p3.xml");
    assert(message.size() != 0);
    parser_->Receive(&db_, message.data(), message.size(), 0);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(4, table->Size());

    IFMapNode *vn4 = NodeLookup("virtual-network", "vn4");
    EXPECT_TRUE(vn4 != NULL);
    obj = vn4->Find(IFMapOrigin(IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(obj != NULL);

    IFMapNode *vn5 = NodeLookup("virtual-network", "vn5");
    EXPECT_TRUE(vn5 != NULL);
    obj = vn5->Find(IFMapOrigin(IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(obj != NULL);

    message = FileRead("src/ifmap/testdata/server_parser_test_p4.xml");
    assert(message.size() != 0);
    parser_->Receive(&db_, message.data(), message.size(), 0);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, table->Size());

    // Only vn1 should exist
    IFMapNode *vn = NodeLookup("virtual-network", "vn1");
    EXPECT_TRUE(vn != NULL);
    vn = NodeLookup("virtual-network", "vn2");
    EXPECT_TRUE(vn == NULL);
    vn = NodeLookup("virtual-network", "vn3");
    EXPECT_TRUE(vn == NULL);
    vn = NodeLookup("virtual-network", "vn4");
    EXPECT_TRUE(vn == NULL);
    vn = NodeLookup("virtual-network", "vn5");
    EXPECT_TRUE(vn == NULL);
}

// In a single message, adds vn1, vn2, vn3 and then deletes all of them.
TEST_F(IFMapServerParserTest, ServerParser1) {
    IFMapTable *table = IFMapTable::FindTable(&db_, "virtual-network");

    string message(FileRead("src/ifmap/testdata/server_parser_test1.xml"));
    assert(message.size() != 0);
    parser_->Receive(&db_, message.data(), message.size(), 0);
    task_util::WaitForIdle();
    EXPECT_EQ(0, table->Size());

    IFMapNode *vn = NodeLookup("virtual-network", "vn1");
    EXPECT_TRUE(vn == NULL);
    vn = NodeLookup("virtual-network", "vn2");
    EXPECT_TRUE(vn == NULL);
    vn = NodeLookup("virtual-network", "vn3");
    EXPECT_TRUE(vn == NULL);
}

// In 2 separate messages, adds vn1, vn2, vn3 and then deletes all of them.
// Same as ServerParser1 except that the various operations are happening in
// separate messages.
TEST_F(IFMapServerParserTest, ServerParser1InParts) {
    IFMapTable *table = IFMapTable::FindTable(&db_, "virtual-network");

    string message(FileRead("src/ifmap/testdata/server_parser_test1_p1.xml"));
    assert(message.size() != 0);
    parser_->Receive(&db_, message.data(), message.size(), 0);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(3, table->Size());

    IFMapNode *vn = NodeLookup("virtual-network", "vn1");
    EXPECT_TRUE(vn != NULL);
    IFMapObject *obj = vn->Find(IFMapOrigin(IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(obj != NULL);
    vn = NodeLookup("virtual-network", "vn2");
    EXPECT_TRUE(vn != NULL);
    obj = vn->Find(IFMapOrigin(IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(obj != NULL);
    vn = NodeLookup("virtual-network", "vn3");
    EXPECT_TRUE(vn != NULL);
    obj = vn->Find(IFMapOrigin(IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(obj != NULL);

    message = FileRead("src/ifmap/testdata/server_parser_test1_p2.xml");
    assert(message.size() != 0);
    parser_->Receive(&db_, message.data(), message.size(), 0);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(0, table->Size());

    vn = NodeLookup("virtual-network", "vn1");
    EXPECT_TRUE(vn == NULL);
    vn = NodeLookup("virtual-network", "vn2");
    EXPECT_TRUE(vn == NULL);
    vn = NodeLookup("virtual-network", "vn3");
    EXPECT_TRUE(vn == NULL);
}

// In a single message, adds vn1, vn2, vn3 in separate updateResult stanza's
// and then adds them again in a single stanza 
TEST_F(IFMapServerParserTest, ServerParser2) {
    IFMapTable *table = IFMapTable::FindTable(&db_, "virtual-network");

    string message(FileRead("src/ifmap/testdata/server_parser_test2.xml"));
    assert(message.size() != 0);
    parser_->Receive(&db_, message.data(), message.size(), 0);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(3, table->Size());

    IFMapNode *vn = NodeLookup("virtual-network", "vn1");
    EXPECT_TRUE(vn != NULL);
    IFMapObject *obj = vn->Find(IFMapOrigin(IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(obj != NULL);

    vn = NodeLookup("virtual-network", "vn2");
    EXPECT_TRUE(vn != NULL);
    obj = vn->Find(IFMapOrigin(IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(obj != NULL);

    vn = NodeLookup("virtual-network", "vn3");
    EXPECT_TRUE(vn != NULL);
    obj = vn->Find(IFMapOrigin(IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(obj != NULL);
}

// In 4 separate messages: 1) adds vn1, 2) adds vn2, 3) adds vn3 4) adds all of
// them again in a single stanza 
// Same as ServerParser2 except that the various operations are happening in
// separate messages.
TEST_F(IFMapServerParserTest, ServerParser2InParts) {
    IFMapTable *table = IFMapTable::FindTable(&db_, "virtual-network");

    string message(FileRead("src/ifmap/testdata/server_parser_test2_p1.xml"));
    assert(message.size() != 0);
    parser_->Receive(&db_, message.data(), message.size(), 0);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, table->Size());

    IFMapNode *vn = NodeLookup("virtual-network", "vn1");
    EXPECT_TRUE(vn != NULL);
    IFMapObject *obj = vn->Find(IFMapOrigin(IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(obj != NULL);

    message = FileRead("src/ifmap/testdata/server_parser_test2_p2.xml");
    assert(message.size() != 0);
    parser_->Receive(&db_, message.data(), message.size(), 0);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(2, table->Size());

    vn = NodeLookup("virtual-network", "vn2");
    EXPECT_TRUE(vn != NULL);
    obj = vn->Find(IFMapOrigin(IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(obj != NULL);

    message = FileRead("src/ifmap/testdata/server_parser_test2_p3.xml");
    assert(message.size() != 0);
    parser_->Receive(&db_, message.data(), message.size(), 0);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(3, table->Size());

    vn = NodeLookup("virtual-network", "vn3");
    EXPECT_TRUE(vn != NULL);
    obj = vn->Find(IFMapOrigin(IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(obj != NULL);

    message = FileRead("src/ifmap/testdata/server_parser_test2_p4.xml");
    assert(message.size() != 0);
    parser_->Receive(&db_, message.data(), message.size(), 0);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(3, table->Size());

    vn = NodeLookup("virtual-network", "vn1");
    EXPECT_TRUE(vn != NULL);
    vn = NodeLookup("virtual-network", "vn2");
    EXPECT_TRUE(vn != NULL);
    vn = NodeLookup("virtual-network", "vn3");
    EXPECT_TRUE(vn != NULL);
}

// In a single message, deletes vn1, vn2, vn3 in a deleteResult stanza and then
// deletes them again in a single stanza 
TEST_F(IFMapServerParserTest, ServerParser3) {
    IFMapTable *table = IFMapTable::FindTable(&db_, "virtual-network");
    EXPECT_EQ(0, table->Size());

    string message(FileRead("src/ifmap/testdata/server_parser_test3.xml"));
    assert(message.size() != 0);
    parser_->Receive(&db_, message.data(), message.size(), 0);
    task_util::WaitForIdle();
    EXPECT_EQ(0, table->Size());

    IFMapNode *vn = NodeLookup("virtual-network", "vn1");
    EXPECT_TRUE(vn == NULL);
    vn = NodeLookup("virtual-network", "vn2");
    EXPECT_TRUE(vn == NULL);
    vn = NodeLookup("virtual-network", "vn3");
    EXPECT_TRUE(vn == NULL);
}

// In 2 separate messages, 1) deletes vn1, vn2, vn3 2) deletes them again 
// Same as ServerParser3 except that the various operations are happening in
// separate messages.
TEST_F(IFMapServerParserTest, ServerParser3InParts) {
    IFMapTable *table = IFMapTable::FindTable(&db_, "virtual-network");
    EXPECT_EQ(0, table->Size());

    string message(FileRead("src/ifmap/testdata/server_parser_test3_p1.xml"));
    assert(message.size() != 0);
    parser_->Receive(&db_, message.data(), message.size(), 0);
    task_util::WaitForIdle();
    EXPECT_EQ(0, table->Size());

    IFMapNode *vn = NodeLookup("virtual-network", "vn1");
    EXPECT_TRUE(vn == NULL);
    vn = NodeLookup("virtual-network", "vn2");
    EXPECT_TRUE(vn == NULL);
    vn = NodeLookup("virtual-network", "vn3");
    EXPECT_TRUE(vn == NULL);

    message = FileRead("src/ifmap/testdata/server_parser_test3_p1.xml");
    assert(message.size() != 0);
    parser_->Receive(&db_, message.data(), message.size(), 0);
    task_util::WaitForIdle();
    EXPECT_EQ(0, table->Size());

    vn = NodeLookup("virtual-network", "vn1");
    EXPECT_TRUE(vn == NULL);
    vn = NodeLookup("virtual-network", "vn2");
    EXPECT_TRUE(vn == NULL);
    vn = NodeLookup("virtual-network", "vn3");
    EXPECT_TRUE(vn == NULL);
}

// In a single message: 
// 1) create link(vr,vm), then vr-with-properties, then vm-with-properties
// 2) delete link(vr,vm)
// Both vr and vm nodes should continue to live
TEST_F(IFMapServerParserTest, ServerParser4) {
    IFMapTable *vrtable = IFMapTable::FindTable(&db_, "virtual-router");
    EXPECT_EQ(0, vrtable->Size());
    IFMapTable *vmtable = IFMapTable::FindTable(&db_, "virtual-machine");
    EXPECT_EQ(0, vmtable->Size());

    string message(FileRead("src/ifmap/testdata/server_parser_test4.xml"));
    assert(message.size() != 0);
    parser_->Receive(&db_, message.data(), message.size(), 0);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1, vmtable->Size());

    IFMapNode *vr1 = NodeLookup("virtual-router", "vr1");
    EXPECT_TRUE(vr1 != NULL);
    IFMapObject *obj = vr1->Find(IFMapOrigin(IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(obj != NULL);

    IFMapNode *vm1 = NodeLookup("virtual-machine", "vm1");
    EXPECT_TRUE(vm1 != NULL);
    obj = vm1->Find(IFMapOrigin(IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(obj != NULL);

    IFMapLink *link = LinkLookup(vr1, vm1);
    EXPECT_TRUE(link == NULL);
}

// In 2 separate messages: 
// 1) create link(vr,vm), then vr-with-properties, then vm-with-properties
// 2) delete link(vr,vm)
// Same as ServerParser4 except that the various operations are happening in
// separate messages.
TEST_F(IFMapServerParserTest, ServerParser4InParts) {
    IFMapTable *vrtable = IFMapTable::FindTable(&db_, "virtual-router");
    EXPECT_EQ(0, vrtable->Size());
    IFMapTable *vmtable = IFMapTable::FindTable(&db_, "virtual-machine");
    EXPECT_EQ(0, vmtable->Size());

    string message(FileRead("src/ifmap/testdata/server_parser_test4_p1.xml"));
    assert(message.size() != 0);
    parser_->Receive(&db_, message.data(), message.size(), 0);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1, vmtable->Size());

    IFMapNode *vr1 = NodeLookup("virtual-router", "vr1");
    EXPECT_TRUE(vr1 != NULL);
    IFMapObject *obj = vr1->Find(IFMapOrigin(IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(obj != NULL);

    IFMapNode *vm1 = NodeLookup("virtual-machine", "vm1");
    EXPECT_TRUE(vm1 != NULL);
    obj = vm1->Find(IFMapOrigin(IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(obj != NULL);

    IFMapLink *link = LinkLookup(vr1, vm1);
    EXPECT_TRUE(link != NULL);

    message = FileRead("src/ifmap/testdata/server_parser_test4_p2.xml");
    assert(message.size() != 0);
    parser_->Receive(&db_, message.data(), message.size(), 0);
    task_util::WaitForIdle();
    EXPECT_EQ(1, vrtable->Size());
    EXPECT_EQ(1, vmtable->Size());
    TASK_UTIL_EXPECT_TRUE(LinkLookup(vr1, vm1) == NULL);

    vr1 = NodeLookup("virtual-router", "vr1");
    EXPECT_TRUE(vr1 != NULL);
    obj = vr1->Find(IFMapOrigin(IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(obj != NULL);

    vm1 = NodeLookup("virtual-machine", "vm1");
    EXPECT_TRUE(vm1 != NULL);
    obj = vm1->Find(IFMapOrigin(IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(obj != NULL);
}

// In a single message: 
// 1) create link(vr,vm)         2) delete link(vr,vm)
// Both vr and vm nodes should get deleted since they dont have any properties
TEST_F(IFMapServerParserTest, ServerParser5) {
    IFMapTable *vrtable = IFMapTable::FindTable(&db_, "virtual-router");
    EXPECT_EQ(0, vrtable->Size());
    IFMapTable *vmtable = IFMapTable::FindTable(&db_, "virtual-machine");
    EXPECT_EQ(0, vmtable->Size());

    string message(FileRead("src/ifmap/testdata/server_parser_test5.xml"));
    assert(message.size() != 0);
    parser_->Receive(&db_, message.data(), message.size(), 0);
    task_util::WaitForIdle();
    EXPECT_EQ(0, vrtable->Size());
    EXPECT_EQ(0, vmtable->Size());

    IFMapNode *vr1 = NodeLookup("virtual-router", "vr1");
    EXPECT_TRUE(vr1 == NULL);

    IFMapNode *vm1 = NodeLookup("virtual-machine", "vm1");
    EXPECT_TRUE(vm1 == NULL);
}

// In 2 separate messages: 
// 1) create link(vr,vm)         2) delete link(vr,vm)
// Both vr and vm nodes should get deleted since they dont have any properties
// Same as ServerParser5 except that the various operations are happening in
// separate messages.
TEST_F(IFMapServerParserTest, ServerParser5InParts) {
    IFMapTable *vrtable = IFMapTable::FindTable(&db_, "virtual-router");
    EXPECT_EQ(0, vrtable->Size());
    IFMapTable *vmtable = IFMapTable::FindTable(&db_, "virtual-machine");
    EXPECT_EQ(0, vmtable->Size());

    string message(FileRead("src/ifmap/testdata/server_parser_test5_p1.xml"));
    assert(message.size() != 0);
    parser_->Receive(&db_, message.data(), message.size(), 0);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1, vmtable->Size());

    IFMapNode *vr1 = NodeLookup("virtual-router", "vr1");
    EXPECT_TRUE(vr1 != NULL);
    IFMapObject *obj = vr1->Find(IFMapOrigin(IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(obj == NULL);
    IFMapNode *vm1 = NodeLookup("virtual-machine", "vm1");
    EXPECT_TRUE(vm1 != NULL);
    obj = vm1->Find(IFMapOrigin(IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(obj == NULL);
    IFMapLink *link = LinkLookup(vr1, vm1);
    EXPECT_TRUE(link != NULL);

    message = FileRead("src/ifmap/testdata/server_parser_test5_p2.xml");
    assert(message.size() != 0);
    parser_->Receive(&db_, message.data(), message.size(), 0);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(0, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(0, vmtable->Size());

    vr1 = NodeLookup("virtual-router", "vr1");
    EXPECT_TRUE(vr1 == NULL);
    vm1 = NodeLookup("virtual-machine", "vm1");
    EXPECT_TRUE(vm1 == NULL);
}

// In a single message: 
// 1) create link(vr,vm), then vr-with-properties, then vm-with-properties
// 2) delete vr, then link(vr,vm)
// The vr should disappear and vm should continue to live
TEST_F(IFMapServerParserTest, ServerParser6) {
    IFMapTable *vrtable = IFMapTable::FindTable(&db_, "virtual-router");
    EXPECT_EQ(0, vrtable->Size());
    IFMapTable *vmtable = IFMapTable::FindTable(&db_, "virtual-machine");
    EXPECT_EQ(0, vmtable->Size());

    string message(FileRead("src/ifmap/testdata/server_parser_test6.xml"));
    assert(message.size() != 0);
    parser_->Receive(&db_, message.data(), message.size(), 0);
    task_util::WaitForIdle();
    EXPECT_EQ(0, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1, vmtable->Size());

    IFMapNode *vr1 = NodeLookup("virtual-router", "vr1");
    EXPECT_TRUE(vr1 == NULL);

    IFMapNode *vm1 = NodeLookup("virtual-machine", "vm1");
    EXPECT_TRUE(vm1 != NULL);
    IFMapObject *obj = vm1->Find(IFMapOrigin(IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(obj != NULL);
}

// In 2 separate messages: 
// 1) create link(vr,vm), then vr-with-properties, then vm-with-properties
// 2) delete vr, then link(vr,vm)
// The vr should disappear and vm should continue to live
// Same as ServerParser6 except that the various operations are happening in
// separate messages.
TEST_F(IFMapServerParserTest, ServerParser6InParts) {
    IFMapTable *vrtable = IFMapTable::FindTable(&db_, "virtual-router");
    EXPECT_EQ(0, vrtable->Size());
    IFMapTable *vmtable = IFMapTable::FindTable(&db_, "virtual-machine");
    EXPECT_EQ(0, vmtable->Size());

    string message(FileRead("src/ifmap/testdata/server_parser_test6_p1.xml"));
    assert(message.size() != 0);
    parser_->Receive(&db_, message.data(), message.size(), 0);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1, vmtable->Size());

    IFMapNode *vr1 = NodeLookup("virtual-router", "vr1");
    EXPECT_TRUE(vr1 != NULL);
    IFMapObject *obj = vr1->Find(IFMapOrigin(IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(obj != NULL);

    IFMapNode *vm1 = NodeLookup("virtual-machine", "vm1");
    EXPECT_TRUE(vm1 != NULL);
    obj = vm1->Find(IFMapOrigin(IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(obj != NULL);

    IFMapLink *link = LinkLookup(vr1, vm1);
    EXPECT_TRUE(link != NULL);

    message = FileRead("src/ifmap/testdata/server_parser_test6_p2.xml");
    assert(message.size() != 0);
    parser_->Receive(&db_, message.data(), message.size(), 0);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(0, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1, vmtable->Size());

    vr1 = NodeLookup("virtual-router", "vr1");
    EXPECT_TRUE(vr1 == NULL);

    vm1 = NodeLookup("virtual-machine", "vm1");
    EXPECT_TRUE(vm1 != NULL);
    obj = vm1->Find(IFMapOrigin(IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(obj != NULL);
}

// In a single message: 
// 1) create link(vr,vm), then vr-with-properties, then vm-with-properties
// 2) delete vr, then link(vr,vm)
// 3) add vr-with-properties
// Both vr and vm nodes should continue to live
TEST_F(IFMapServerParserTest, ServerParser7) {
    IFMapTable *vrtable = IFMapTable::FindTable(&db_, "virtual-router");
    EXPECT_EQ(0, vrtable->Size());
    IFMapTable *vmtable = IFMapTable::FindTable(&db_, "virtual-machine");
    EXPECT_EQ(0, vmtable->Size());

    string message(FileRead("src/ifmap/testdata/server_parser_test7.xml"));
    assert(message.size() != 0);
    parser_->Receive(&db_, message.data(), message.size(), 0);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1, vmtable->Size());

    IFMapNode *vr1 = NodeLookup("virtual-router", "vr1");
    EXPECT_TRUE(vr1 != NULL);
    IFMapObject *obj = vr1->Find(IFMapOrigin(IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(obj != NULL);

    IFMapNode *vm1 = NodeLookup("virtual-machine", "vm1");
    EXPECT_TRUE(vm1 != NULL);
    obj = vm1->Find(IFMapOrigin(IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(obj != NULL);

    IFMapLink *link = LinkLookup(vr1, vm1);
    EXPECT_TRUE(link == NULL);
}

// In 3 separate messages: 
// 1) create link(vr,vm), then vr-with-properties, then vm-with-properties
// 2) delete vr and link(vr,vm)
// 3) add vr-with-properties
// Both vr and vm nodes should continue to live
// Same as ServerParser7 except that the various operations are happening in
// separate messages.
TEST_F(IFMapServerParserTest, ServerParser7InParts) {
    IFMapTable *vrtable = IFMapTable::FindTable(&db_, "virtual-router");
    EXPECT_EQ(0, vrtable->Size());
    IFMapTable *vmtable = IFMapTable::FindTable(&db_, "virtual-machine");
    EXPECT_EQ(0, vmtable->Size());

    string message(FileRead("src/ifmap/testdata/server_parser_test7_p1.xml"));
    assert(message.size() != 0);
    parser_->Receive(&db_, message.data(), message.size(), 0);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1, vmtable->Size());

    IFMapNode *vr1 = NodeLookup("virtual-router", "vr1");
    EXPECT_TRUE(vr1 != NULL);
    IFMapObject *obj = vr1->Find(IFMapOrigin(IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(obj != NULL);

    IFMapNode *vm1 = NodeLookup("virtual-machine", "vm1");
    EXPECT_TRUE(vm1 != NULL);
    obj = vm1->Find(IFMapOrigin(IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(obj != NULL);

    IFMapLink *link = LinkLookup(vr1, vm1);
    EXPECT_TRUE(link != NULL);

    message = FileRead("src/ifmap/testdata/server_parser_test7_p2.xml");
    assert(message.size() != 0);
    parser_->Receive(&db_, message.data(), message.size(), 0);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(0, vrtable->Size());
    EXPECT_EQ(1, vmtable->Size());

    vr1 = NodeLookup("virtual-router", "vr1");
    EXPECT_TRUE(vr1 == NULL);

    vm1 = NodeLookup("virtual-machine", "vm1");
    EXPECT_TRUE(vm1 != NULL);
    obj = vm1->Find(IFMapOrigin(IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(obj != NULL);

    message = FileRead("src/ifmap/testdata/server_parser_test7_p3.xml");
    assert(message.size() != 0);
    parser_->Receive(&db_, message.data(), message.size(), 0);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, vrtable->Size());
    EXPECT_EQ(1, vmtable->Size());

    vr1 = NodeLookup("virtual-router", "vr1");
    EXPECT_TRUE(vr1 != NULL);
    obj = vr1->Find(IFMapOrigin(IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(obj != NULL);

    vm1 = NodeLookup("virtual-machine", "vm1");
    EXPECT_TRUE(vm1 != NULL);
    obj = vm1->Find(IFMapOrigin(IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(obj != NULL);

    link = LinkLookup(vr1, vm1);
    EXPECT_TRUE(link == NULL);
}

// In a single message:
// 1) create link(vr,vm)   2) delete link(vr,vm)    3) create link(vr,vm)
// Both vr and vm should exist but should not have any objects since we dont
// get any node-config with properties for either of them
TEST_F(IFMapServerParserTest, ServerParser8) {
    IFMapTable *vrtable = IFMapTable::FindTable(&db_, "virtual-router");
    EXPECT_EQ(0, vrtable->Size());
    IFMapTable *vmtable = IFMapTable::FindTable(&db_, "virtual-machine");
    EXPECT_EQ(0, vmtable->Size());

    string message(FileRead("src/ifmap/testdata/server_parser_test8.xml"));
    assert(message.size() != 0);
    parser_->Receive(&db_, message.data(), message.size(), 0);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1, vmtable->Size());

    IFMapNode *vr1 = NodeLookup("virtual-router", "vr1");
    EXPECT_TRUE(vr1 != NULL);
    IFMapObject *obj = vr1->Find(IFMapOrigin(IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(obj == NULL);

    IFMapNode *vm1 = NodeLookup("virtual-machine", "vm1");
    EXPECT_TRUE(vm1 != NULL);
    obj = vm1->Find(IFMapOrigin(IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(obj == NULL);

    IFMapLink *link = LinkLookup(vr1, vm1);
    EXPECT_TRUE(link != NULL);
}

// In 3 separte messages:
// 1) create link(vr,vm)   2) delete link(vr,vm)    3) create link(vr,vm)
// Both vr and vm should exist but should not have any objects since we dont
// get any node-config with properties for either of them
// Same as ServerParser8 except that the various operations are happening in
// separate messages.
TEST_F(IFMapServerParserTest, ServerParser8InParts) {
    IFMapTable *vrtable = IFMapTable::FindTable(&db_, "virtual-router");
    EXPECT_EQ(0, vrtable->Size());
    IFMapTable *vmtable = IFMapTable::FindTable(&db_, "virtual-machine");
    EXPECT_EQ(0, vmtable->Size());

    string message(FileRead("src/ifmap/testdata/server_parser_test8_p1.xml"));
    assert(message.size() != 0);
    parser_->Receive(&db_, message.data(), message.size(), 0);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1, vmtable->Size());

    IFMapNode *vr1 = NodeLookup("virtual-router", "vr1");
    EXPECT_TRUE(vr1 != NULL);
    IFMapObject *obj = vr1->Find(IFMapOrigin(IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(obj == NULL);

    IFMapNode *vm1 = NodeLookup("virtual-machine", "vm1");
    EXPECT_TRUE(vm1 != NULL);
    obj = vm1->Find(IFMapOrigin(IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(obj == NULL);

    IFMapLink *link = LinkLookup(vr1, vm1);
    EXPECT_TRUE(link != NULL);

    message = FileRead("src/ifmap/testdata/server_parser_test8_p2.xml");
    assert(message.size() != 0);
    parser_->Receive(&db_, message.data(), message.size(), 0);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(0, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(0, vmtable->Size());

    vr1 = NodeLookup("virtual-router", "vr1");
    EXPECT_TRUE(vr1 == NULL);
    vm1 = NodeLookup("virtual-machine", "vm1");
    EXPECT_TRUE(vm1 == NULL);

    message = FileRead("src/ifmap/testdata/server_parser_test8_p3.xml");
    assert(message.size() != 0);
    parser_->Receive(&db_, message.data(), message.size(), 0);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1, vmtable->Size());

    vr1 = NodeLookup("virtual-router", "vr1");
    EXPECT_TRUE(vr1 != NULL);
    obj = vr1->Find(IFMapOrigin(IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(obj == NULL);

    vm1 = NodeLookup("virtual-machine", "vm1");
    EXPECT_TRUE(vm1 != NULL);
    obj = vm1->Find(IFMapOrigin(IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(obj == NULL);

    link = LinkLookup(vr1, vm1);
    EXPECT_TRUE(link != NULL);
}

// In a single message: 
// 1) create vr-with-properties, then vm-with-properties, then link(vr,vm)
// 2) delete link(vr,vm)
// 3) add link(vr,vm)
// Both vr and vm nodes should continue to live
TEST_F(IFMapServerParserTest, ServerParser9) {
    IFMapTable *vrtable = IFMapTable::FindTable(&db_, "virtual-router");
    EXPECT_EQ(0, vrtable->Size());
    IFMapTable *vmtable = IFMapTable::FindTable(&db_, "virtual-machine");
    EXPECT_EQ(0, vmtable->Size());

    string message(FileRead("src/ifmap/testdata/server_parser_test9.xml"));
    assert(message.size() != 0);
    parser_->Receive(&db_, message.data(), message.size(), 0);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1, vmtable->Size());

    IFMapNode *vr1 = NodeLookup("virtual-router", "vr1");
    EXPECT_TRUE(vr1 != NULL);
    IFMapObject *obj = vr1->Find(IFMapOrigin(IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(obj != NULL);

    IFMapNode *vm1 = NodeLookup("virtual-machine", "vm1");
    EXPECT_TRUE(vm1 != NULL);
    obj = vm1->Find(IFMapOrigin(IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(obj != NULL);

    IFMapLink *link = LinkLookup(vr1, vm1);
    EXPECT_TRUE(link != NULL);
}

// In 3 separate messages:
// 1) create vr-with-properties, then vm-with-properties, then link(vr,vm)
// 2) delete link(vr,vm)
// 3) add link(vr,vm)
// Both vr and vm nodes should continue to live
// Same as ServerParser9 except that the various operations are happening in
// separate messages.
TEST_F(IFMapServerParserTest, ServerParser9InParts) {
    IFMapTable *vrtable = IFMapTable::FindTable(&db_, "virtual-router");
    EXPECT_EQ(0, vrtable->Size());
    IFMapTable *vmtable = IFMapTable::FindTable(&db_, "virtual-machine");
    EXPECT_EQ(0, vmtable->Size());

    string message(FileRead("src/ifmap/testdata/server_parser_test9_p1.xml"));
    assert(message.size() != 0);
    parser_->Receive(&db_, message.data(), message.size(), 0);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1, vmtable->Size());

    IFMapNode *vr1 = NodeLookup("virtual-router", "vr1");
    EXPECT_TRUE(vr1 != NULL);
    IFMapObject *obj = vr1->Find(IFMapOrigin(IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(obj != NULL);

    IFMapNode *vm1 = NodeLookup("virtual-machine", "vm1");
    EXPECT_TRUE(vm1 != NULL);
    obj = vm1->Find(IFMapOrigin(IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(obj != NULL);

    IFMapLink *link = LinkLookup(vr1, vm1);
    EXPECT_TRUE(link != NULL);

    message = FileRead("src/ifmap/testdata/server_parser_test9_p2.xml");
    assert(message.size() != 0);
    parser_->Receive(&db_, message.data(), message.size(), 0);
    task_util::WaitForIdle();
    EXPECT_EQ(1, vrtable->Size());
    EXPECT_EQ(1, vmtable->Size());
    TASK_UTIL_EXPECT_TRUE(LinkLookup(vr1, vm1) == NULL);

    vr1 = NodeLookup("virtual-router", "vr1");
    EXPECT_TRUE(vr1 != NULL);
    obj = vr1->Find(IFMapOrigin(IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(obj != NULL);

    vm1 = NodeLookup("virtual-machine", "vm1");
    EXPECT_TRUE(vm1 != NULL);
    obj = vm1->Find(IFMapOrigin(IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(obj != NULL);

    message = FileRead("src/ifmap/testdata/server_parser_test9_p3.xml");
    assert(message.size() != 0);
    parser_->Receive(&db_, message.data(), message.size(), 0);
    task_util::WaitForIdle();
    EXPECT_EQ(1, vrtable->Size());
    EXPECT_EQ(1, vmtable->Size());
    TASK_UTIL_EXPECT_TRUE(LinkLookup(vr1, vm1) != NULL);

    vr1 = NodeLookup("virtual-router", "vr1");
    EXPECT_TRUE(vr1 != NULL);
    obj = vr1->Find(IFMapOrigin(IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(obj != NULL);

    vm1 = NodeLookup("virtual-machine", "vm1");
    EXPECT_TRUE(vm1 != NULL);
    obj = vm1->Find(IFMapOrigin(IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(obj != NULL);
}

// In a single message: 
// 1) create vr-with-properties, then vm-with-properties, then link(vr,vm)
// 2) delete link(vr,vm), then delete vr
// The vr should disappear and vm should continue to live
// Similar to ServerParser6, except that in step2, we delete link and then vr
TEST_F(IFMapServerParserTest, ServerParser10) {
    IFMapTable *vrtable = IFMapTable::FindTable(&db_, "virtual-router");
    EXPECT_EQ(0, vrtable->Size());
    IFMapTable *vmtable = IFMapTable::FindTable(&db_, "virtual-machine");
    EXPECT_EQ(0, vmtable->Size());

    string message(FileRead("src/ifmap/testdata/server_parser_test10.xml"));
    assert(message.size() != 0);
    parser_->Receive(&db_, message.data(), message.size(), 0);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(0, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1, vmtable->Size());

    IFMapNode *vr1 = NodeLookup("virtual-router", "vr1");
    EXPECT_TRUE(vr1 == NULL);

    IFMapNode *vm1 = NodeLookup("virtual-machine", "vm1");
    EXPECT_TRUE(vm1 != NULL);
    IFMapObject *obj = vm1->Find(IFMapOrigin(IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(obj != NULL);
}

// In 2 separate messages: 
// 1) create vr-with-properties, then vm-with-properties, then link(vr,vm)
// 2) delete link(vr,vm), then delete vr
// The vr should disappear and vm should continue to live
// Similar to ServerParser6, except that in step2, we delete link and then vr
// Same as ServerParser10 except that the various operations are happening in
// separate messages.
TEST_F(IFMapServerParserTest, ServerParser10InParts) {
    IFMapTable *vrtable = IFMapTable::FindTable(&db_, "virtual-router");
    EXPECT_EQ(0, vrtable->Size());
    IFMapTable *vmtable = IFMapTable::FindTable(&db_, "virtual-machine");
    EXPECT_EQ(0, vmtable->Size());

    string message(FileRead("src/ifmap/testdata/server_parser_test10_p1.xml"));
    assert(message.size() != 0);
    parser_->Receive(&db_, message.data(), message.size(), 0);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1, vmtable->Size());

    IFMapNode *vr1 = NodeLookup("virtual-router", "vr1");
    EXPECT_TRUE(vr1 != NULL);
    IFMapObject *obj = vr1->Find(IFMapOrigin(IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(obj != NULL);

    IFMapNode *vm1 = NodeLookup("virtual-machine", "vm1");
    EXPECT_TRUE(vm1 != NULL);
    obj = vm1->Find(IFMapOrigin(IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(obj != NULL);

    IFMapLink *link = LinkLookup(vr1, vm1);
    EXPECT_TRUE(link != NULL);

    message = FileRead("src/ifmap/testdata/server_parser_test10_p2.xml");
    assert(message.size() != 0);
    parser_->Receive(&db_, message.data(), message.size(), 0);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(0, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1, vmtable->Size());
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-router", "vr1") == NULL);

    vm1 = NodeLookup("virtual-machine", "vm1");
    EXPECT_TRUE(vm1 != NULL);
    obj = vm1->Find(IFMapOrigin(IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(obj != NULL);
}

// In a single message: 
// 1) create vr-with-properties, then vm-with-properties, then link(vr,vm)
// 2) delete link(vr,vm), then delete vr
// 3) add link(vr,vm)
// Both vr and vm nodes should continue to live but vr should have no object
TEST_F(IFMapServerParserTest, ServerParser11) {
    IFMapTable *vrtable = IFMapTable::FindTable(&db_, "virtual-router");
    EXPECT_EQ(0, vrtable->Size());
    IFMapTable *vmtable = IFMapTable::FindTable(&db_, "virtual-machine");
    EXPECT_EQ(0, vmtable->Size());

    string message(FileRead("src/ifmap/testdata/server_parser_test11.xml"));
    assert(message.size() != 0);
    parser_->Receive(&db_, message.data(), message.size(), 0);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1, vmtable->Size());

    IFMapNode *vr1 = NodeLookup("virtual-router", "vr1");
    EXPECT_TRUE(vr1 != NULL);
    IFMapObject *obj = vr1->Find(IFMapOrigin(IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(obj == NULL);

    IFMapNode *vm1 = NodeLookup("virtual-machine", "vm1");
    EXPECT_TRUE(vm1 != NULL);
    obj = vm1->Find(IFMapOrigin(IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(obj != NULL);

    IFMapLink *link = LinkLookup(vr1, vm1);
    EXPECT_TRUE(link != NULL);
}

// In 3 separate messages: 
// 1) create vr-with-properties, then vm-with-properties, then link(vr,vm)
// 2) delete link(vr,vm), then delete vr
// 3) add link(vr,vm)
// Both vr and vm nodes should continue to live but vr should have no object
TEST_F(IFMapServerParserTest, ServerParser11InParts) {
    IFMapTable *vrtable = IFMapTable::FindTable(&db_, "virtual-router");
    EXPECT_EQ(0, vrtable->Size());
    IFMapTable *vmtable = IFMapTable::FindTable(&db_, "virtual-machine");
    EXPECT_EQ(0, vmtable->Size());

    string message(FileRead("src/ifmap/testdata/server_parser_test11_p1.xml"));
    assert(message.size() != 0);
    parser_->Receive(&db_, message.data(), message.size(), 0);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1, vmtable->Size());

    IFMapNode *vr1 = NodeLookup("virtual-router", "vr1");
    EXPECT_TRUE(vr1 != NULL);
    IFMapObject *obj = vr1->Find(IFMapOrigin(IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(obj != NULL);

    IFMapNode *vm1 = NodeLookup("virtual-machine", "vm1");
    EXPECT_TRUE(vm1 != NULL);
    obj = vm1->Find(IFMapOrigin(IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(obj != NULL);

    IFMapLink *link = LinkLookup(vr1, vm1);
    EXPECT_TRUE(link != NULL);

    message = FileRead("src/ifmap/testdata/server_parser_test11_p2.xml");
    assert(message.size() != 0);
    parser_->Receive(&db_, message.data(), message.size(), 0);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(0, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1, vmtable->Size());

    vr1 = NodeLookup("virtual-router", "vr1");
    EXPECT_TRUE(vr1 == NULL);

    vm1 = NodeLookup("virtual-machine", "vm1");
    EXPECT_TRUE(vm1 != NULL);
    obj = vm1->Find(IFMapOrigin(IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(obj != NULL);

    message = FileRead("src/ifmap/testdata/server_parser_test11_p3.xml");
    assert(message.size() != 0);
    parser_->Receive(&db_, message.data(), message.size(), 0);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1, vmtable->Size());

    // vr1 should not have any object
    vr1 = NodeLookup("virtual-router", "vr1");
    EXPECT_TRUE(vr1 != NULL);
    obj = vr1->Find(IFMapOrigin(IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(obj == NULL);

    vm1 = NodeLookup("virtual-machine", "vm1");
    EXPECT_TRUE(vm1 != NULL);
    obj = vm1->Find(IFMapOrigin(IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(obj != NULL);

    link = LinkLookup(vr1, vm1);
    EXPECT_TRUE(link != NULL);
}

// In a single message: 
// 1) create link(vr,vm), then link(vr,gsc)
// 2) delete link(vr,vm), then link(vr,gsc)
// No nodes should exist.
TEST_F(IFMapServerParserTest, ServerParser12) {
    IFMapTable *vrtable = IFMapTable::FindTable(&db_, "virtual-router");
    EXPECT_EQ(0, vrtable->Size());
    IFMapTable *vmtable = IFMapTable::FindTable(&db_, "virtual-machine");
    EXPECT_EQ(0, vmtable->Size());
    IFMapTable *gsctable = IFMapTable::FindTable(&db_, "global-system-config");
    EXPECT_EQ(0, gsctable->Size());

    string message(FileRead("src/ifmap/testdata/server_parser_test12.xml"));
    assert(message.size() != 0);
    parser_->Receive(&db_, message.data(), message.size(), 0);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(0, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(0, vmtable->Size());

    IFMapNode *vr1 = NodeLookup("virtual-router", "vr1");
    EXPECT_TRUE(vr1 == NULL);

    IFMapNode *vm1 = NodeLookup("virtual-machine", "vm1");
    EXPECT_TRUE(vm1 == NULL);

    IFMapNode *gsc = NodeLookup("global-system-config", "gsc");
    EXPECT_TRUE(gsc == NULL);
}

// In 2 separate messages: 
// 1) create link(vr,vm), then link(vr,gsc)
// 2) delete link(vr,vm), then link(vr,gsc)
TEST_F(IFMapServerParserTest, ServerParser12InParts) {
    IFMapTable *vrtable = IFMapTable::FindTable(&db_, "virtual-router");
    EXPECT_EQ(0, vrtable->Size());
    IFMapTable *vmtable = IFMapTable::FindTable(&db_, "virtual-machine");
    EXPECT_EQ(0, vmtable->Size());
    IFMapTable *gsctable = IFMapTable::FindTable(&db_, "global-system-config");
    EXPECT_EQ(0, gsctable->Size());

    string message(FileRead("src/ifmap/testdata/server_parser_test12_p1.xml"));
    assert(message.size() != 0);
    parser_->Receive(&db_, message.data(), message.size(), 0);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1, vmtable->Size());
    TASK_UTIL_EXPECT_EQ(1, gsctable->Size());

    IFMapNode *vr1 = NodeLookup("virtual-router", "vr1");
    EXPECT_TRUE(vr1 != NULL);
    IFMapObject *obj = vr1->Find(IFMapOrigin(IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(obj == NULL);

    IFMapNode *vm1 = NodeLookup("virtual-machine", "vm1");
    EXPECT_TRUE(vm1 != NULL);
    obj = vm1->Find(IFMapOrigin(IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(obj == NULL);

    IFMapNode *gsc = NodeLookup("global-system-config", "gsc");
    EXPECT_TRUE(gsc != NULL);
    obj = gsc->Find(IFMapOrigin(IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(obj == NULL);

    IFMapLink *link = LinkLookup(vr1, vm1);
    EXPECT_TRUE(link != NULL);
    link = LinkLookup(vr1, gsc);
    EXPECT_TRUE(link != NULL);

    message = FileRead("src/ifmap/testdata/server_parser_test12_p2.xml");
    assert(message.size() != 0);
    parser_->Receive(&db_, message.data(), message.size(), 0);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(0, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(0, vmtable->Size());
    TASK_UTIL_EXPECT_EQ(0, gsctable->Size());

    vr1 = NodeLookup("virtual-router", "vr1");
    EXPECT_TRUE(vr1 == NULL);
    vm1 = NodeLookup("virtual-machine", "vm1");
    EXPECT_TRUE(vm1 == NULL);
    gsc = NodeLookup("global-system-config", "gsc");
    EXPECT_TRUE(gsc == NULL);
}

// In a single message: 
// 1) create link(vr,vm), then vr-with-properties, then vm-with-properties,
// 2) create link(vr,gsc), then gsc-with-properties
// 3) delete link(vr,vm), then link(vr,gsc)
TEST_F(IFMapServerParserTest, ServerParser13) {
    IFMapTable *vrtable = IFMapTable::FindTable(&db_, "virtual-router");
    EXPECT_EQ(0, vrtable->Size());
    IFMapTable *vmtable = IFMapTable::FindTable(&db_, "virtual-machine");
    EXPECT_EQ(0, vmtable->Size());
    IFMapTable *gsctable = IFMapTable::FindTable(&db_, "global-system-config");
    EXPECT_EQ(0, gsctable->Size());

    string message(FileRead("src/ifmap/testdata/server_parser_test13.xml"));
    assert(message.size() != 0);
    parser_->Receive(&db_, message.data(), message.size(), 0);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1, vmtable->Size());
    TASK_UTIL_EXPECT_EQ(1, gsctable->Size());

    usleep(1000);

    IFMapNode *vr1 = NodeLookup("virtual-router", "vr1");
    EXPECT_TRUE(vr1 != NULL);
    IFMapObject *obj = vr1->Find(IFMapOrigin(IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(obj != NULL);

    IFMapNode *vm1 = NodeLookup("virtual-machine", "vm1");
    EXPECT_TRUE(vm1 != NULL);
    obj = vm1->Find(IFMapOrigin(IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(obj != NULL);

    IFMapNode *gsc = NodeLookup("global-system-config", "gsc");
    EXPECT_TRUE(gsc != NULL);
    obj = gsc->Find(IFMapOrigin(IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(obj != NULL);

    EXPECT_TRUE(LinkLookup(vr1, vm1) == NULL);
    EXPECT_TRUE(LinkLookup(vr1, gsc) == NULL);
}

// In 3 separate messages: 
// 1) create link(vr,vm), then vr-with-properties, then vm-with-properties,
// 2) create link(vr,gsc), then gsc-with-properties
// 3) delete link(vr,vm), then link(vm, gsc)
TEST_F(IFMapServerParserTest, ServerParser13InParts) {
    IFMapTable *vrtable = IFMapTable::FindTable(&db_, "virtual-router");
    EXPECT_EQ(0, vrtable->Size());
    IFMapTable *vmtable = IFMapTable::FindTable(&db_, "virtual-machine");
    EXPECT_EQ(0, vmtable->Size());
    IFMapTable *gsctable = IFMapTable::FindTable(&db_, "global-system-config");
    EXPECT_EQ(0, gsctable->Size());

    string message(FileRead("src/ifmap/testdata/server_parser_test13_p1.xml"));
    assert(message.size() != 0);
    parser_->Receive(&db_, message.data(), message.size(), 0);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1, vmtable->Size());
    TASK_UTIL_EXPECT_EQ(0, gsctable->Size());

    IFMapNode *vr1 = NodeLookup("virtual-router", "vr1");
    EXPECT_TRUE(vr1 != NULL);
    IFMapObject *obj = vr1->Find(IFMapOrigin(IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(obj != NULL);

    IFMapNode *vm1 = NodeLookup("virtual-machine", "vm1");
    EXPECT_TRUE(vm1 != NULL);
    obj = vm1->Find(IFMapOrigin(IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(obj != NULL);

    IFMapNode *gsc = NodeLookup("global-system-config", "gsc");
    EXPECT_TRUE(gsc == NULL);

    EXPECT_TRUE(LinkLookup(vr1, vm1) != NULL);

    message = FileRead("src/ifmap/testdata/server_parser_test13_p2.xml");
    assert(message.size() != 0);
    parser_->Receive(&db_, message.data(), message.size(), 0);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, gsctable->Size());
    EXPECT_EQ(1, vrtable->Size());
    EXPECT_EQ(1, vmtable->Size());

    gsc = NodeLookup("global-system-config", "gsc");
    EXPECT_TRUE(gsc != NULL);
    obj = gsc->Find(IFMapOrigin(IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(obj != NULL);

    EXPECT_TRUE(NodeLookup("virtual-router", "vr1") != NULL);
    EXPECT_TRUE(NodeLookup("virtual-machine", "vm1") != NULL);
    EXPECT_TRUE(LinkLookup(vr1, vm1) != NULL);
    EXPECT_TRUE(LinkLookup(vr1, gsc) != NULL);

    message = FileRead("src/ifmap/testdata/server_parser_test13_p3.xml");
    assert(message.size() != 0);
    parser_->Receive(&db_, message.data(), message.size(), 0);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1, vmtable->Size());
    TASK_UTIL_EXPECT_EQ(1, gsctable->Size());

    EXPECT_TRUE(NodeLookup("virtual-router", "vr1") != NULL);
    EXPECT_TRUE(NodeLookup("virtual-machine", "vm1") != NULL);
    EXPECT_TRUE(NodeLookup("global-system-config", "gsc") != NULL);
    TASK_UTIL_EXPECT_TRUE(LinkLookup(vr1, vm1) == NULL);
    TASK_UTIL_EXPECT_TRUE(LinkLookup(vr1, gsc) == NULL);
}

// In a single message: 
// 1) create link(vr,vm), then vr-with-properties, then vm-with-properties,
// 2) create link(vr,gsc), then gsc-with-properties
// 3) delete gsc, then link(vr,gsc)
TEST_F(IFMapServerParserTest, ServerParser14) {
    IFMapTable *vrtable = IFMapTable::FindTable(&db_, "virtual-router");
    EXPECT_EQ(0, vrtable->Size());
    IFMapTable *vmtable = IFMapTable::FindTable(&db_, "virtual-machine");
    EXPECT_EQ(0, vmtable->Size());
    IFMapTable *gsctable = IFMapTable::FindTable(&db_, "global-system-config");
    EXPECT_EQ(0, gsctable->Size());

    string message(FileRead("src/ifmap/testdata/server_parser_test14.xml"));
    assert(message.size() != 0);
    parser_->Receive(&db_, message.data(), message.size(), 0);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1, vmtable->Size());
    TASK_UTIL_EXPECT_EQ(0, gsctable->Size());

    usleep(1000);

    IFMapNode *vr1 = NodeLookup("virtual-router", "vr1");
    EXPECT_TRUE(vr1 != NULL);
    IFMapObject *obj = vr1->Find(IFMapOrigin(IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(obj != NULL);

    IFMapNode *vm1 = NodeLookup("virtual-machine", "vm1");
    EXPECT_TRUE(vm1 != NULL);
    obj = vm1->Find(IFMapOrigin(IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(obj != NULL);

    IFMapNode *gsc = NodeLookup("global-system-config", "gsc");
    EXPECT_TRUE(gsc == NULL);

    EXPECT_TRUE(LinkLookup(vr1, vm1) != NULL);
}

// In 3 separate messages: 
// 1) create link(vr,vm), then vr-with-properties, then vm-with-properties,
// 2) create link(vr,gsc), then gsc-with-properties
// 3) delete gsc, then link(vr,gsc)
TEST_F(IFMapServerParserTest, ServerParser14InParts) {
    IFMapTable *vrtable = IFMapTable::FindTable(&db_, "virtual-router");
    EXPECT_EQ(0, vrtable->Size());
    IFMapTable *vmtable = IFMapTable::FindTable(&db_, "virtual-machine");
    EXPECT_EQ(0, vmtable->Size());
    IFMapTable *gsctable = IFMapTable::FindTable(&db_, "global-system-config");
    EXPECT_EQ(0, gsctable->Size());

    // Using datafile from test13_p1
    string message(FileRead("src/ifmap/testdata/server_parser_test13_p1.xml"));
    assert(message.size() != 0);
    parser_->Receive(&db_, message.data(), message.size(), 0);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1, vmtable->Size());
    TASK_UTIL_EXPECT_EQ(0, gsctable->Size());

    IFMapNode *vr1 = NodeLookup("virtual-router", "vr1");
    EXPECT_TRUE(vr1 != NULL);
    IFMapObject *obj = vr1->Find(IFMapOrigin(IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(obj != NULL);

    IFMapNode *vm1 = NodeLookup("virtual-machine", "vm1");
    EXPECT_TRUE(vm1 != NULL);
    obj = vm1->Find(IFMapOrigin(IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(obj != NULL);

    IFMapNode *gsc = NodeLookup("global-system-config", "gsc");
    EXPECT_TRUE(gsc == NULL);

    EXPECT_TRUE(LinkLookup(vr1, vm1) != NULL);

    // Using datafile from test13_p2
    message = FileRead("src/ifmap/testdata/server_parser_test13_p2.xml");
    assert(message.size() != 0);
    parser_->Receive(&db_, message.data(), message.size(), 0);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, gsctable->Size());
    EXPECT_EQ(1, vrtable->Size());
    EXPECT_EQ(1, vmtable->Size());

    gsc = NodeLookup("global-system-config", "gsc");
    EXPECT_TRUE(gsc != NULL);
    obj = gsc->Find(IFMapOrigin(IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(obj != NULL);

    EXPECT_TRUE(NodeLookup("virtual-router", "vr1") != NULL);
    EXPECT_TRUE(NodeLookup("virtual-machine", "vm1") != NULL);
    EXPECT_TRUE(LinkLookup(vr1, vm1) != NULL);
    EXPECT_TRUE(LinkLookup(vr1, gsc) != NULL);

    // Need new datafile for step 3
    message = FileRead("src/ifmap/testdata/server_parser_test14_p3.xml");
    assert(message.size() != 0);
    parser_->Receive(&db_, message.data(), message.size(), 0);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1, vmtable->Size());
    TASK_UTIL_EXPECT_EQ(0, gsctable->Size());

    vr1 = NodeLookup("virtual-router", "vr1");
    EXPECT_TRUE(vr1 != NULL);
    obj = vr1->Find(IFMapOrigin(IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(obj != NULL);

    vm1 = NodeLookup("virtual-machine", "vm1");
    EXPECT_TRUE(vm1 != NULL);
    obj = vm1->Find(IFMapOrigin(IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(obj != NULL);

    EXPECT_TRUE(NodeLookup("global-system-config", "gsc") == NULL);
    EXPECT_TRUE(LinkLookup(vr1, vm1) != NULL);
}

// In a single message: 
// 1) create link(vr,vm), then vr-with-properties, then vm-with-properties,
// 2) create link(vr,gsc), then gsc-with-properties
// 3) delete vr
TEST_F(IFMapServerParserTest, ServerParser15) {
    IFMapTable *vrtable = IFMapTable::FindTable(&db_, "virtual-router");
    EXPECT_EQ(0, vrtable->Size());
    IFMapTable *vmtable = IFMapTable::FindTable(&db_, "virtual-machine");
    EXPECT_EQ(0, vmtable->Size());
    IFMapTable *gsctable = IFMapTable::FindTable(&db_, "global-system-config");
    EXPECT_EQ(0, gsctable->Size());

    string message(FileRead("src/ifmap/testdata/server_parser_test15.xml"));
    assert(message.size() != 0);
    parser_->Receive(&db_, message.data(), message.size(), 0);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1, vmtable->Size());
    TASK_UTIL_EXPECT_EQ(1, gsctable->Size());

    usleep(1000);

    // Object should not exist
    IFMapNode *vr1 = NodeLookup("virtual-router", "vr1");
    EXPECT_TRUE(vr1 != NULL);
    IFMapObject *obj = vr1->Find(IFMapOrigin(IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(obj == NULL);
    EXPECT_TRUE(vr1->HasAdjacencies(&graph_));

    IFMapNode *vm1 = NodeLookup("virtual-machine", "vm1");
    EXPECT_TRUE(vm1 != NULL);
    obj = vm1->Find(IFMapOrigin(IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(obj != NULL);

    IFMapNode *gsc = NodeLookup("global-system-config", "gsc");
    EXPECT_TRUE(gsc != NULL);
    obj = gsc->Find(IFMapOrigin(IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(obj != NULL);

    EXPECT_TRUE(LinkLookup(vr1, vm1) != NULL);
    EXPECT_TRUE(LinkLookup(vr1, gsc) != NULL);
}

// In 3 separate messages: 
// 1) create link(vr,vm), then vr-with-properties, then vm-with-properties,
// 2) create link(vr,gsc), then gsc-with-properties
// 3) delete vr
TEST_F(IFMapServerParserTest, ServerParser15InParts) {
    IFMapTable *vrtable = IFMapTable::FindTable(&db_, "virtual-router");
    EXPECT_EQ(0, vrtable->Size());
    IFMapTable *vmtable = IFMapTable::FindTable(&db_, "virtual-machine");
    EXPECT_EQ(0, vmtable->Size());
    IFMapTable *gsctable = IFMapTable::FindTable(&db_, "global-system-config");
    EXPECT_EQ(0, gsctable->Size());

    // Using datafile from test13_p1
    string message(FileRead("src/ifmap/testdata/server_parser_test13_p1.xml"));
    assert(message.size() != 0);
    parser_->Receive(&db_, message.data(), message.size(), 0);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1, vmtable->Size());
    TASK_UTIL_EXPECT_EQ(0, gsctable->Size());

    IFMapNode *vr1 = NodeLookup("virtual-router", "vr1");
    EXPECT_TRUE(vr1 != NULL);
    IFMapObject *obj = vr1->Find(IFMapOrigin(IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(obj != NULL);

    IFMapNode *vm1 = NodeLookup("virtual-machine", "vm1");
    EXPECT_TRUE(vm1 != NULL);
    obj = vm1->Find(IFMapOrigin(IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(obj != NULL);

    IFMapNode *gsc = NodeLookup("global-system-config", "gsc");
    EXPECT_TRUE(gsc == NULL);

    EXPECT_TRUE(LinkLookup(vr1, vm1) != NULL);

    // Using datafile from test13_p2
    message = FileRead("src/ifmap/testdata/server_parser_test13_p2.xml");
    assert(message.size() != 0);
    parser_->Receive(&db_, message.data(), message.size(), 0);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, gsctable->Size());
    EXPECT_EQ(1, vrtable->Size());
    EXPECT_EQ(1, vmtable->Size());

    gsc = NodeLookup("global-system-config", "gsc");
    EXPECT_TRUE(gsc != NULL);
    obj = gsc->Find(IFMapOrigin(IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(obj != NULL);

    EXPECT_TRUE(NodeLookup("virtual-router", "vr1") != NULL);
    EXPECT_TRUE(NodeLookup("virtual-machine", "vm1") != NULL);
    EXPECT_TRUE(LinkLookup(vr1, vm1) != NULL);
    EXPECT_TRUE(LinkLookup(vr1, gsc) != NULL);

    // Need new datafile for step 3
    message = FileRead("src/ifmap/testdata/server_parser_test15_p3.xml");
    assert(message.size() != 0);
    parser_->Receive(&db_, message.data(), message.size(), 0);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1, vmtable->Size());
    TASK_UTIL_EXPECT_EQ(1, gsctable->Size());

    // Object should not exist
    vr1 = NodeLookup("virtual-router", "vr1");
    EXPECT_TRUE(vr1 != NULL);
    obj = vr1->Find(IFMapOrigin(IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(obj == NULL);
    EXPECT_TRUE(vr1->HasAdjacencies(&graph_));

    vm1 = NodeLookup("virtual-machine", "vm1");
    EXPECT_TRUE(vm1 != NULL);
    obj = vm1->Find(IFMapOrigin(IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(obj != NULL);

    gsc = NodeLookup("global-system-config", "gsc");
    EXPECT_TRUE(gsc != NULL);
    obj = gsc->Find(IFMapOrigin(IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(obj != NULL);

    EXPECT_TRUE(LinkLookup(vr1, vm1) != NULL);
    EXPECT_TRUE(LinkLookup(vr1, gsc) != NULL);
}

// In a single message: 
// 1) create link(vr,vm), then vr-with-properties, then vm-with-properties,
// 2) create link(vr,gsc), then gsc-with-properties
// 3) delete link(vr,gsc), then delete gsc, then delete vr
TEST_F(IFMapServerParserTest, ServerParser16) {
    IFMapTable *vrtable = IFMapTable::FindTable(&db_, "virtual-router");
    EXPECT_EQ(0, vrtable->Size());
    IFMapTable *vmtable = IFMapTable::FindTable(&db_, "virtual-machine");
    EXPECT_EQ(0, vmtable->Size());
    IFMapTable *gsctable = IFMapTable::FindTable(&db_, "global-system-config");
    EXPECT_EQ(0, gsctable->Size());

    string message(FileRead("src/ifmap/testdata/server_parser_test16.xml"));
    assert(message.size() != 0);
    parser_->Receive(&db_, message.data(), message.size(), 0);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1, vmtable->Size());
    TASK_UTIL_EXPECT_EQ(0, gsctable->Size());

    usleep(1000);

    // Object should not exist
    IFMapNode *vr1 = NodeLookup("virtual-router", "vr1");
    EXPECT_TRUE(vr1 != NULL);
    IFMapObject *obj = vr1->Find(IFMapOrigin(IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(obj == NULL);
    EXPECT_TRUE(vr1->HasAdjacencies(&graph_));

    IFMapNode *vm1 = NodeLookup("virtual-machine", "vm1");
    EXPECT_TRUE(vm1 != NULL);
    obj = vm1->Find(IFMapOrigin(IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(obj != NULL);

    IFMapNode *gsc = NodeLookup("global-system-config", "gsc");
    EXPECT_TRUE(gsc == NULL);

    EXPECT_TRUE(LinkLookup(vr1, vm1) != NULL);
}

// In 3 separate messages: 
// 1) create link(vr,vm), then vr-with-properties, then vm-with-properties,
// 2) create link(vr,gsc), then gsc-with-properties
// 3) delete link(vr,gsc), then delete gsc, then delete vr
TEST_F(IFMapServerParserTest, ServerParser16InParts) {
    IFMapTable *vrtable = IFMapTable::FindTable(&db_, "virtual-router");
    EXPECT_EQ(0, vrtable->Size());
    IFMapTable *vmtable = IFMapTable::FindTable(&db_, "virtual-machine");
    EXPECT_EQ(0, vmtable->Size());
    IFMapTable *gsctable = IFMapTable::FindTable(&db_, "global-system-config");
    EXPECT_EQ(0, gsctable->Size());

    // Using datafile from test13_p1
    string message(FileRead("src/ifmap/testdata/server_parser_test13_p1.xml"));
    assert(message.size() != 0);
    parser_->Receive(&db_, message.data(), message.size(), 0);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1, vmtable->Size());
    TASK_UTIL_EXPECT_EQ(0, gsctable->Size());

    IFMapNode *vr1 = NodeLookup("virtual-router", "vr1");
    EXPECT_TRUE(vr1 != NULL);
    IFMapObject *obj = vr1->Find(IFMapOrigin(IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(obj != NULL);

    IFMapNode *vm1 = NodeLookup("virtual-machine", "vm1");
    EXPECT_TRUE(vm1 != NULL);
    obj = vm1->Find(IFMapOrigin(IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(obj != NULL);

    IFMapNode *gsc = NodeLookup("global-system-config", "gsc");
    EXPECT_TRUE(gsc == NULL);

    EXPECT_TRUE(LinkLookup(vr1, vm1) != NULL);

    // Using datafile from test13_p2
    message = FileRead("src/ifmap/testdata/server_parser_test13_p2.xml");
    assert(message.size() != 0);
    parser_->Receive(&db_, message.data(), message.size(), 0);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, gsctable->Size());
    EXPECT_EQ(1, vrtable->Size());
    EXPECT_EQ(1, vmtable->Size());

    gsc = NodeLookup("global-system-config", "gsc");
    EXPECT_TRUE(gsc != NULL);
    obj = gsc->Find(IFMapOrigin(IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(obj != NULL);

    EXPECT_TRUE(NodeLookup("virtual-router", "vr1") != NULL);
    EXPECT_TRUE(NodeLookup("virtual-machine", "vm1") != NULL);
    EXPECT_TRUE(LinkLookup(vr1, vm1) != NULL);
    EXPECT_TRUE(LinkLookup(vr1, gsc) != NULL);

    // Need new datafile for step 3
    message = FileRead("src/ifmap/testdata/server_parser_test16_p3.xml");
    assert(message.size() != 0);
    parser_->Receive(&db_, message.data(), message.size(), 0);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1, vmtable->Size());
    TASK_UTIL_EXPECT_EQ(0, gsctable->Size());

    // Object should not exist
    vr1 = NodeLookup("virtual-router", "vr1");
    EXPECT_TRUE(vr1 != NULL);
    obj = vr1->Find(IFMapOrigin(IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(obj == NULL);
    EXPECT_TRUE(vr1->HasAdjacencies(&graph_));

    vm1 = NodeLookup("virtual-machine", "vm1");
    EXPECT_TRUE(vm1 != NULL);
    obj = vm1->Find(IFMapOrigin(IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(obj != NULL);

    gsc = NodeLookup("global-system-config", "gsc");
    EXPECT_TRUE(gsc == NULL);

    EXPECT_TRUE(LinkLookup(vr1, vm1) != NULL);
}


int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    LoggingInit();
    ControlNode::SetDefaultSchedulingPolicy();
    int status = RUN_ALL_TESTS();
    TaskScheduler::GetInstance()->Terminate();
    return status;
}
