/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "ifmap/ifmap_server.h"

#include <fstream>
#include "base/logging.h"
#include "base/test/task_test_util.h"
#include "control-node/control_node.h"
#include "db/db.h"
#include "db/db_graph.h"
#include "io/event_manager.h"
#include "ifmap/ifmap_client.h"
#include "ifmap/ifmap_link_table.h"
#include "ifmap/ifmap_table.h"
#include "ifmap/ifmap_update_queue.h"
#include "ifmap/test/ifmap_client_mock.h"
#include "ifmap/test/ifmap_test_util.h"
#include "ifmap/ifmap_xmpp.h"
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

class IFMapServerTest : public ::testing::Test {
  protected:
    IFMapServerTest()
            : db_(TaskScheduler::GetInstance()->GetTaskId("db::IFMapTable")),
              server_(&db_, &db_graph_, evm_.io_service()) {
    }

    virtual void SetUp() {
        xmpp_server_ = new XmppServer(&evm_, "bgp.contrail.com");
        IFMapLinkTable_Init(&db_, &db_graph_);
        vnc_cfg_Server_ModuleInit(&db_, &db_graph_);
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

    DB db_;
    DBGraph db_graph_;
    EventManager evm_;
    IFMapServer server_;
    XmppServer *xmpp_server_;
    auto_ptr<IFMapChannelManagerMock> ifmap_channel_mgr_;
};

static void IFMapVRouterLink(DB *db, const string &vrouter,
                             const string &network) {
    static int vm_index = 0;
    vm_index++;
    ostringstream oss;
    oss << "aa" << setfill('0') << setw(2) << vm_index;
    string vm_id(oss.str());
    oss << ":eth0";
    string vmi_id(oss.str());
    ifmap_test_util::IFMapMsgLink(db, "config-root", "root",
                                  "virtual-router", vrouter,
                                  "config-root-virtual-router");
    ifmap_test_util::IFMapMsgLink(db, "virtual-router", vrouter,
                                  "virtual-machine", vm_id,
                                  "virtual-router-virtual-machine");
    ifmap_test_util::IFMapMsgLink(db, "virtual-machine", vm_id,
                                  "virtual-machine-interface", vmi_id,
                                  "virtual-machine-interface-virtual-machine");
    ifmap_test_util::IFMapMsgLink(db, "virtual-machine-interface", vmi_id,
                                  "virtual-network", network,
                                  "virtual-machine-interface-virtual-network");
}

TEST_F(IFMapServerTest, ClientUnregister) {
    IFMapClientMock *c1 = new IFMapClientMock("orange");
    IFMapClientMock *c2 = new IFMapClientMock("blue");
    server_.AddClient(c1);
    server_.AddClient(c2);

    IFMapVRouterLink(&db_, "orange", "net0");
    IFMapVRouterLink(&db_, "blue", "net0");

    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(server_.queue()->empty());
    TASK_UTIL_EXPECT_EQ(7, c1->count());
    TASK_UTIL_EXPECT_EQ(7, c2->count());

    server_.DeleteClient(c1);

    ifmap_test_util::IFMapMsgUnlink(&db_, "virtual-router", "orange",
                                    "virtual-machine", "aa01",
                                    "virtual-router-virtual-machine");

    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(server_.queue()->empty());

    TASK_UTIL_EXPECT_EQ(7, c2->count());

    IFMapClientMock *c3 = new IFMapClientMock("orange");
    server_.AddClient(c3);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(server_.queue()->empty());
    TASK_UTIL_EXPECT_EQ(0, c3->count());

    ifmap_test_util::IFMapMsgLink(&db_, "virtual-router", "orange",
                                  "virtual-machine", "aa01",
                                  "virtual-router-virtual-machine");
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(server_.queue()->empty());
    TASK_UTIL_EXPECT_EQ(7, c3->count());

    server_.DeleteClient(c3);
    server_.DeleteClient(c2);
    task_util::WaitForIdle();
    usleep(1000);
    delete c3;
    delete c2;
    delete c1;
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    LoggingInit();
    ControlNode::SetDefaultSchedulingPolicy();
    int status = RUN_ALL_TESTS();
    TaskScheduler::GetInstance()->Terminate();
    return status;
}
