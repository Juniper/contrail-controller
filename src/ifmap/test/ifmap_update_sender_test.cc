/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "ifmap/ifmap_update_sender.h"

#include "base/logging.h"
#include "base/task.h"
#include "base/test/task_test_util.h"
#include "db/db.h"
#include "db/db_graph.h"
#include "db/db_table.h"
#include "io/event_manager.h"
#include "ifmap/ifmap_client.h"
#include "ifmap/ifmap_exporter.h"
#include "ifmap/ifmap_link_table.h"
#include "ifmap/ifmap_node.h"
#include "ifmap/ifmap_server.h"
#include "ifmap/ifmap_table.h"
#include "ifmap/ifmap_update.h"
#include "ifmap/ifmap_update_queue.h"
#include "schema/vnc_cfg_types.h"
#include "testing/gunit.h"

using namespace std;

class TestClient : public IFMapClient {
public:
    TestClient(const string &addr)
        : identifier_(addr), send_success_(true), send_update_cnt_(0) {
    }

    virtual const string &identifier() const {
        return identifier_;
    }

    virtual bool SendUpdate(const std::string &msg) {
        cout << "Sending " << endl << msg << endl;
        send_update_cnt_++;
        return send_success_;
    }

    int get_send_update_cnt() { return send_update_cnt_; }

    // Control if you want to block or continue sending
    void set_send_success(bool succ) { send_success_ = succ; }

private:
    string identifier_;
    bool send_success_;
    int send_update_cnt_;
};

struct IFMapUpdateDeleter {
    IFMapUpdateDeleter(IFMapUpdateQueue *queue) : queue_(queue) { }
    void operator()(IFMapUpdate *ptr) {
        queue_->Dequeue(ptr);
        delete ptr;
    }
private:
    IFMapUpdateQueue *queue_;
};

class IFMapUpdateSenderTest : public ::testing::Test {
protected:
    typedef map<string, IFMapNode *> NodeMap;

    IFMapUpdateSenderTest()
        : server_(&db_, &graph_, evm_.io_service()), tbl_(NULL),
          sender_(server_.sender()), queue_(server_.queue()) {
    }

    virtual void SetUp() {
        IFMapLinkTable_Init(&db_, &graph_);
        vnc_cfg_Server_ModuleInit(&db_, &graph_);
        tbl_ = IFMapTable::FindTable(&db_, "virtual-network");
        ASSERT_TRUE(tbl_);
        server_.Initialize();
    }

    virtual void TearDown() {
        IFMapExporter *exporter = server_.exporter();
        for (NodeMap::iterator iter = node_map_.begin();
             iter != node_map_.end(); ++iter) {
            IFMapNode *node = iter->second;
            IFMapNodeState *state = exporter->NodeStateLookup(node);
            assert(state != NULL);
            DBTable::ListenerId tsid = exporter->TableListenerId(node->table());
            node->ClearState(node->table(), tsid);
            state->ClearAndDispose(IFMapUpdateDeleter(queue_));
            delete state;
            delete node;
        }
    }

    IFMapUpdate *CreateUpdate(const char *name, bool positive) {
        IFMapTable::RequestKey key;
        key.id_type = "virtual-network";
        key.id_name = name;
        auto_ptr<DBEntry> entry(tbl_->AllocEntry(&key));
        IFMapNode *node = static_cast<IFMapNode *>(entry.release());
        node_map_.insert(make_pair(name, node));
        IFMapExporter *exporter = server_.exporter();
        IFMapNodeState *state = exporter->NodeStateLocate(node);
        state->SetValid(node);
        IFMapUpdate *update = new IFMapUpdate(node, positive);
        state->Insert(update);
        return update;
    }

    void SetSendBlocked(int client_index) {
        sender_->SetSendBlocked(client_index);
    }

    DB db_;
    DBGraph graph_;
    EventManager evm_;
    IFMapServer server_;
    DBTable *tbl_;
    NodeMap node_map_;
    IFMapUpdateSender *sender_;
    IFMapUpdateQueue *queue_;
};

TEST_F(IFMapUpdateSenderTest, BasicQTraversal) {
    TestClient c0("c0");
    server_.ClientRegister(&c0);

    IFMapUpdate *u1 = CreateUpdate("u1", true);
    IFMapUpdate *u2 = CreateUpdate("u2", false);
    IFMapUpdate *u3 = CreateUpdate("u3", true);
    IFMapUpdate *u4 = CreateUpdate("u4", false);

    // c0 to receive all updates.
    BitSet cli_bs;
    cli_bs.set(c0.index());
    u1->AdvertiseOr(cli_bs);
    u2->AdvertiseOr(cli_bs);
    u3->AdvertiseOr(cli_bs);
    u4->AdvertiseOr(cli_bs);

    queue_->Join(c0.index());

    // Add 4 update nodes
    queue_->Enqueue(u1);
    queue_->Enqueue(u2);
    queue_->Enqueue(u3);
    queue_->Enqueue(u4);
    TASK_UTIL_EXPECT_EQ(5, queue_->size()); // 4 updates and 1 tail_marker
    TASK_UTIL_EXPECT_TRUE(queue_->GetLast() != queue_->tail_marker());

    // Should print 5 elements
    queue_->PrintQueue();

    sender_->SendActive(c0.index());
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, queue_->size());
    TASK_UTIL_EXPECT_TRUE(queue_->GetLast() == queue_->tail_marker());
    TASK_UTIL_EXPECT_EQ(1, c0.get_send_update_cnt());
    TASK_UTIL_EXPECT_TRUE(queue_->GetMarker(c0.index()) ==
                          queue_->tail_marker());

    queue_->PrintQueue();
    queue_->Leave(c0.index());
}

TEST_F(IFMapUpdateSenderTest, QTraversalNoInterest) {
    TestClient c0("c0");
    TestClient c1("c1");
    server_.ClientRegister(&c0);
    server_.ClientRegister(&c1);

    IFMapUpdate *u1 = CreateUpdate("u1", true);
    IFMapUpdate *u2 = CreateUpdate("u2", false);
    IFMapUpdate *u3 = CreateUpdate("u3", true);
    IFMapUpdate *u4 = CreateUpdate("u4", false);

    // c1 to receive all updates. c0 is not interested in any of these.
    BitSet cli_bs;
    cli_bs.set(c1.index());
    u1->AdvertiseOr(cli_bs);
    u2->AdvertiseOr(cli_bs);
    u3->AdvertiseOr(cli_bs);
    u4->AdvertiseOr(cli_bs);

    queue_->Join(c0.index());
    queue_->Join(c1.index());

    // Add 4 update nodes
    queue_->Enqueue(u1);
    queue_->Enqueue(u2);
    queue_->Enqueue(u3);
    queue_->Enqueue(u4);
    TASK_UTIL_EXPECT_EQ(5, queue_->size()); // 4 updates and 1 tail_marker
    TASK_UTIL_EXPECT_TRUE(queue_->GetLast() != queue_->tail_marker());

    // Should print 5 elements
    queue_->PrintQueue();

    SetSendBlocked(c1.index());
    sender_->SendActive(c0.index());
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(6, queue_->size());
    TASK_UTIL_EXPECT_TRUE(queue_->GetLast() == queue_->tail_marker());
    TASK_UTIL_EXPECT_EQ(0, c0.get_send_update_cnt());
    TASK_UTIL_EXPECT_TRUE(queue_->GetMarker(c0.index()) ==
                          queue_->tail_marker());
    TASK_UTIL_EXPECT_TRUE(queue_->GetMarker(c1.index()) !=
                          queue_->tail_marker());

    queue_->PrintQueue();
    queue_->Leave(c0.index());
    queue_->Leave(c1.index());
}

TEST_F(IFMapUpdateSenderTest, ClientBlockedAndGoTest) {
    TestClient c0("c0");
    TestClient c1("c1");
    server_.ClientRegister(&c0);
    server_.ClientRegister(&c1);

    IFMapUpdate *u1 = CreateUpdate("u1", true);
    IFMapUpdate *u2 = CreateUpdate("u2", true);
    IFMapUpdate *u3 = CreateUpdate("u3", true);
    IFMapUpdate *u4 = CreateUpdate("u4", true);

    // Set up both clients to receive all updates
    BitSet cli_bs;
    cli_bs.set(c0.index());
    cli_bs.set(c1.index());
    u1->AdvertiseOr(cli_bs);
    u2->AdvertiseOr(cli_bs);
    u3->AdvertiseOr(cli_bs);
    u4->AdvertiseOr(cli_bs);

    queue_->Join(c0.index());
    queue_->Join(c1.index());

    queue_->Enqueue(u1);
    queue_->Enqueue(u2);
    queue_->Enqueue(u3);
    queue_->Enqueue(u4);
    TASK_UTIL_EXPECT_EQ(5, queue_->size()); // TM followed by 4 updates
    TASK_UTIL_EXPECT_TRUE(queue_->GetLast() != queue_->tail_marker());

    // Block clients after sending one node
    c0.set_send_success(false);
    c1.set_send_success(false);
    sender_->SetObjectsPerMessage(1);

    queue_->PrintQueue();

    // Activate the clients. They should block after processing one node.
    sender_->QueueActive();
    task_util::WaitForIdle();
    queue_->PrintQueue();
    TASK_UTIL_EXPECT_EQ(1, c0.get_send_update_cnt());
    TASK_UTIL_EXPECT_EQ(1, c1.get_send_update_cnt());
    TASK_UTIL_EXPECT_EQ(4, queue_->size()); // u1 is gone
    TASK_UTIL_EXPECT_TRUE(sender_->IsClientBlocked(c0.index()));
    TASK_UTIL_EXPECT_TRUE(sender_->IsClientBlocked(c1.index()));
    TASK_UTIL_EXPECT_TRUE(queue_->GetMarker(c0.index()) ==
                          queue_->tail_marker());
    TASK_UTIL_EXPECT_TRUE(queue_->GetMarker(c1.index()) ==
                          queue_->tail_marker());
    TASK_UTIL_EXPECT_TRUE(queue_->GetLast() != queue_->tail_marker());

    // Unblock both the clients. They should block after processing one node.
    sender_->SendActive(c0.index());
    sender_->SendActive(c1.index());
    task_util::WaitForIdle();
    queue_->PrintQueue();
    TASK_UTIL_EXPECT_EQ(2, c0.get_send_update_cnt());
    TASK_UTIL_EXPECT_EQ(2, c1.get_send_update_cnt());
    TASK_UTIL_EXPECT_EQ(3, queue_->size()); // u2 is gone
    TASK_UTIL_EXPECT_TRUE(sender_->IsClientBlocked(c0.index()));
    TASK_UTIL_EXPECT_TRUE(sender_->IsClientBlocked(c1.index()));
    TASK_UTIL_EXPECT_TRUE(queue_->GetMarker(c0.index()) ==
                          queue_->tail_marker());
    TASK_UTIL_EXPECT_TRUE(queue_->GetMarker(c1.index()) ==
                          queue_->tail_marker());
    TASK_UTIL_EXPECT_TRUE(queue_->GetLast() != queue_->tail_marker());

    // Unblock both the clients.
    sender_->SendActive(c0.index());
    sender_->SendActive(c1.index());
    task_util::WaitForIdle();
    queue_->PrintQueue();
    TASK_UTIL_EXPECT_EQ(3, c0.get_send_update_cnt());
    TASK_UTIL_EXPECT_EQ(3, c1.get_send_update_cnt());
    TASK_UTIL_EXPECT_EQ(2, queue_->size()); // u3 is gone
    TASK_UTIL_EXPECT_TRUE(sender_->IsClientBlocked(c0.index()));
    TASK_UTIL_EXPECT_TRUE(sender_->IsClientBlocked(c1.index()));
    TASK_UTIL_EXPECT_TRUE(queue_->GetMarker(c0.index()) ==
                          queue_->tail_marker());
    TASK_UTIL_EXPECT_TRUE(queue_->GetMarker(c1.index()) ==
                          queue_->tail_marker());
    TASK_UTIL_EXPECT_TRUE(queue_->GetLast() != queue_->tail_marker());

    // Unblock both the clients.
    sender_->SendActive(c0.index());
    sender_->SendActive(c1.index());
    task_util::WaitForIdle();
    queue_->PrintQueue();
    TASK_UTIL_EXPECT_EQ(4, c0.get_send_update_cnt());
    TASK_UTIL_EXPECT_EQ(4, c1.get_send_update_cnt());
    TASK_UTIL_EXPECT_EQ(1, queue_->size()); // 1 tail_marker
    TASK_UTIL_EXPECT_TRUE(queue_->GetMarker(c0.index()) ==
                          queue_->tail_marker());
    TASK_UTIL_EXPECT_TRUE(queue_->GetMarker(c1.index()) ==
                          queue_->tail_marker());

    // No more nodes in the queue. Unblock clients but nothing should happen.
    sender_->SendActive(c0.index());
    sender_->SendActive(c1.index());
    task_util::WaitForIdle();
    queue_->PrintQueue();
    TASK_UTIL_EXPECT_EQ(1, queue_->size()); // 1 tail_marker
    TASK_UTIL_EXPECT_TRUE(queue_->GetMarker(c0.index()) ==
                          queue_->tail_marker());
    TASK_UTIL_EXPECT_TRUE(queue_->GetMarker(c1.index()) ==
                          queue_->tail_marker());

    // All done
    queue_->Leave(c0.index());
    queue_->Leave(c1.index());
}

TEST_F(IFMapUpdateSenderTest, ClientBlockedTestWithDiffRecvSets) {
    TestClient c0("c0");
    TestClient c1("c1");
    server_.ClientRegister(&c0);
    server_.ClientRegister(&c1);

    IFMapUpdate *u0 = CreateUpdate("u0", true);
    IFMapUpdate *u1 = CreateUpdate("u1", true);
    IFMapUpdate *u2 = CreateUpdate("u2", true);
    IFMapUpdate *u3 = CreateUpdate("u3", true);
    IFMapUpdate *u4 = CreateUpdate("u4", true);

    // Client c0 to receive u0/u1/u3 and c1 to receive u0/u2/u4
    BitSet cli_bs;
    cli_bs.set(c0.index());
    u1->AdvertiseOr(cli_bs);
    u3->AdvertiseOr(cli_bs);
    cli_bs.set(c1.index()); // Add c1 for u0/u2/u4
    u0->AdvertiseOr(cli_bs);
    cli_bs.reset(c0.index()); // Remove c0 for u2/u4
    u2->AdvertiseOr(cli_bs);
    u4->AdvertiseOr(cli_bs);

    queue_->Join(c0.index());
    queue_->Join(c1.index());

    queue_->Enqueue(u0);
    queue_->Enqueue(u1);
    queue_->Enqueue(u2);
    queue_->Enqueue(u3);
    queue_->Enqueue(u4);
    TASK_UTIL_EXPECT_EQ(6, queue_->size()); // TM followed by 5 updates

    // Block client after sending one node
    c0.set_send_success(false);
    c1.set_send_success(false);
    sender_->SetObjectsPerMessage(1);

    queue_->PrintQueue();

    // Activate the clients. They should block after processing u0.
    sender_->QueueActive();
    task_util::WaitForIdle();
    queue_->PrintQueue();
    TASK_UTIL_EXPECT_EQ(1, c0.get_send_update_cnt());
    TASK_UTIL_EXPECT_EQ(1, c1.get_send_update_cnt());
    TASK_UTIL_EXPECT_EQ(5, queue_->size()); // u0 is gone
    TASK_UTIL_EXPECT_TRUE(sender_->IsClientBlocked(c0.index()));
    TASK_UTIL_EXPECT_TRUE(sender_->IsClientBlocked(c1.index()));
    TASK_UTIL_EXPECT_TRUE(queue_->GetMarker(c0.index()) ==
                          queue_->tail_marker());
    TASK_UTIL_EXPECT_TRUE(queue_->GetMarker(c1.index()) ==
                          queue_->tail_marker());

    // Processing u1. Unblock c0. c1 stays blocked.
    sender_->SendActive(c0.index());
    task_util::WaitForIdle();
    queue_->PrintQueue();
    TASK_UTIL_EXPECT_EQ(2, c0.get_send_update_cnt());
    TASK_UTIL_EXPECT_EQ(1, c1.get_send_update_cnt());
    TASK_UTIL_EXPECT_EQ(5, queue_->size()); // u1 is gone, c0 marker added
    TASK_UTIL_EXPECT_TRUE(sender_->IsClientBlocked(c0.index()));
    TASK_UTIL_EXPECT_TRUE(sender_->IsClientBlocked(c1.index()));
    TASK_UTIL_EXPECT_TRUE(queue_->GetMarker(c0.index()) ==
                          queue_->tail_marker());
    TASK_UTIL_EXPECT_TRUE(queue_->GetMarker(c1.index()) !=
                          queue_->tail_marker());

    // Processing u2. Unblock c1. c0 stays blocked.
    sender_->SendActive(c1.index());
    task_util::WaitForIdle();
    queue_->PrintQueue();
    TASK_UTIL_EXPECT_EQ(2, c0.get_send_update_cnt());
    TASK_UTIL_EXPECT_EQ(2, c1.get_send_update_cnt());
    // c0 is blocked, c1 will block after sending u2. So, both should merge
    // into the tail_marker
    TASK_UTIL_EXPECT_EQ(3, queue_->size()); // u2 is gone
    TASK_UTIL_EXPECT_TRUE(sender_->IsClientBlocked(c0.index()));
    TASK_UTIL_EXPECT_TRUE(sender_->IsClientBlocked(c1.index()));
    TASK_UTIL_EXPECT_TRUE(queue_->GetMarker(c0.index()) ==
                          queue_->tail_marker());
    TASK_UTIL_EXPECT_TRUE(queue_->GetMarker(c1.index()) ==
                          queue_->tail_marker());

    // Processing u3. Unblock c0. c1 stays blocked.
    sender_->SendActive(c0.index());
    task_util::WaitForIdle();
    queue_->PrintQueue();
    TASK_UTIL_EXPECT_EQ(3, c0.get_send_update_cnt());
    TASK_UTIL_EXPECT_EQ(2, c1.get_send_update_cnt());
    // c1 is blocked, so markers should split, TM should have c0
    TASK_UTIL_EXPECT_EQ(3, queue_->size()); // u3 is gone, 2 markers
    TASK_UTIL_EXPECT_TRUE(sender_->IsClientBlocked(c0.index()));
    TASK_UTIL_EXPECT_TRUE(sender_->IsClientBlocked(c1.index()));
    TASK_UTIL_EXPECT_TRUE(queue_->GetMarker(c0.index()) ==
                          queue_->tail_marker());
    TASK_UTIL_EXPECT_TRUE(queue_->GetMarker(c1.index()) !=
                          queue_->tail_marker());

    // Processing u4. Unblock c1. c0 stays blocked.
    sender_->SendActive(c1.index());
    task_util::WaitForIdle();
    queue_->PrintQueue();
    TASK_UTIL_EXPECT_EQ(3, c0.get_send_update_cnt());
    TASK_UTIL_EXPECT_EQ(3, c1.get_send_update_cnt());
    // c0 is blocked, c1 will consume u4 and then we will flush when c1-marker
    // encounters TM and c1 will block. So, both should merge into the tm.
    TASK_UTIL_EXPECT_EQ(1, queue_->size());
    TASK_UTIL_EXPECT_TRUE(sender_->IsClientBlocked(c0.index()));
    TASK_UTIL_EXPECT_TRUE(sender_->IsClientBlocked(c1.index()));
    TASK_UTIL_EXPECT_TRUE(queue_->GetMarker(c0.index()) ==
                          queue_->tail_marker());
    TASK_UTIL_EXPECT_TRUE(queue_->GetMarker(c1.index()) ==
                          queue_->tail_marker());

    // All done
    queue_->Leave(c0.index());
    queue_->Leave(c1.index());
}

TEST_F(IFMapUpdateSenderTest, MarkerMergeTest) {
    TestClient c0("c0");
    TestClient c1("c1");
    TestClient c2("c2");
    server_.ClientRegister(&c0);
    server_.ClientRegister(&c1);
    server_.ClientRegister(&c2);

    IFMapUpdate *u1 = CreateUpdate("u1", true);
    IFMapUpdate *u2 = CreateUpdate("u2", true);
    IFMapUpdate *u3 = CreateUpdate("u3", true);
    IFMapUpdate *u4 = CreateUpdate("u4", true);

    BitSet cli_bs;
    cli_bs.set(c0.index()); // c0 to receive u1/u2/u3/u4
    u1->AdvertiseOr(cli_bs);
    cli_bs.set(c1.index()); // c1 to receive u2/u3/u4
    u2->AdvertiseOr(cli_bs);
    u3->AdvertiseOr(cli_bs);
    cli_bs.set(c2.index()); // c2 to receive only u4
    u4->AdvertiseOr(cli_bs);

    queue_->Join(c0.index());
    queue_->Join(c1.index());
    queue_->Join(c2.index());

    queue_->Enqueue(u1);
    queue_->Enqueue(u2);
    queue_->Enqueue(u3);
    queue_->Enqueue(u4);
    TASK_UTIL_EXPECT_EQ(5, queue_->size()); // TM followed by 4 updates

    // Block c0 after sending one node
    c0.set_send_success(false);
    sender_->SetObjectsPerMessage(1);

    // Insert marker for c0 before tail_marker
    IFMapMarker *marker = queue_->GetMarker(c0.index());
    ASSERT_TRUE(marker != NULL);
    BitSet rem_bs;
    rem_bs.set(c0.index());
    queue_->MarkerSplitBefore(marker, marker, rem_bs);
    TASK_UTIL_EXPECT_EQ(6, queue_->size()); // 5 from before + c0 marker
    rem_bs.Reset(rem_bs);
    ASSERT_TRUE(rem_bs.empty());
    TASK_UTIL_EXPECT_TRUE(queue_->GetMarker(c0.index()) !=
                          queue_->tail_marker());

    // Insert marker for c1 after u1
    marker = queue_->GetMarker(c1.index());
    ASSERT_TRUE(marker != NULL);
    rem_bs.set(c1.index());
    queue_->MarkerSplitAfter(marker, u1, rem_bs);
    TASK_UTIL_EXPECT_EQ(7, queue_->size()); // 6 from before + c1 marker
    rem_bs.Reset(rem_bs);
    ASSERT_TRUE(rem_bs.empty());
    TASK_UTIL_EXPECT_TRUE(queue_->GetMarker(c1.index()) !=
                          queue_->tail_marker());
    TASK_UTIL_EXPECT_TRUE(queue_->GetMarker(c2.index()) ==
                          queue_->tail_marker());
 
    // Move marker for c2 (tail_marker) after u3
    marker = queue_->GetMarker(c2.index());
    ASSERT_TRUE(marker != NULL);
    queue_->MoveMarkerAfter(marker, u3);
    TASK_UTIL_EXPECT_EQ(7, queue_->size()); // just moving TM
    TASK_UTIL_EXPECT_TRUE(queue_->GetMarker(c2.index()) ==
                          queue_->tail_marker());

    // Should print 7 elements
    queue_->PrintQueue();

    // Send u1 to c0. Since nobody else wants it, it should be free'ed.
    sender_->SendActive(c0.index());
    SetSendBlocked(c1.index());
    SetSendBlocked(c2.index());
    task_util::WaitForIdle();
    queue_->PrintQueue();
    TASK_UTIL_EXPECT_EQ(1, c0.get_send_update_cnt());
    TASK_UTIL_EXPECT_EQ(0, c1.get_send_update_cnt());
    TASK_UTIL_EXPECT_EQ(0, c2.get_send_update_cnt());
    // c0 blocks after sending one node, c1 is blocked, the c0/c1 markers will
    // merge since both are blocked
    TASK_UTIL_EXPECT_EQ(5, queue_->size()); // c0c1Marker/u2/u3/u4/TM
    TASK_UTIL_EXPECT_TRUE(queue_->GetMarker(c0.index()) ==
                          queue_->GetMarker(c1.index()));
    TASK_UTIL_EXPECT_TRUE(queue_->GetMarker(c0.index()) !=
                          queue_->GetMarker(c2.index()));
    TASK_UTIL_EXPECT_TRUE(queue_->GetMarker(c2.index()) ==
                          queue_->tail_marker());

    // Send u2 to c0. c1/c2 are blocked but they still want u2.
    sender_->SendActive(c0.index());
    task_util::WaitForIdle();
    queue_->PrintQueue();
    TASK_UTIL_EXPECT_EQ(2, c0.get_send_update_cnt());
    TASK_UTIL_EXPECT_EQ(0, c1.get_send_update_cnt());
    TASK_UTIL_EXPECT_EQ(0, c2.get_send_update_cnt());
    TASK_UTIL_EXPECT_TRUE(sender_->IsClientBlocked(c0.index()));
    TASK_UTIL_EXPECT_TRUE(sender_->IsClientBlocked(c1.index()));
    TASK_UTIL_EXPECT_TRUE(sender_->IsClientBlocked(c2.index()));
    // c0 blocks after sending one node, c0c1 marker has to split into 2
    TASK_UTIL_EXPECT_EQ(6, queue_->size()); // u2/u3/u4/3markers
    TASK_UTIL_EXPECT_TRUE(queue_->GetMarker(c0.index()) !=
                          queue_->GetMarker(c1.index()));
    TASK_UTIL_EXPECT_TRUE(queue_->GetMarker(c0.index()) !=
                          queue_->GetMarker(c2.index()));
    TASK_UTIL_EXPECT_TRUE(queue_->GetMarker(c1.index()) !=
                          queue_->GetMarker(c2.index()));
    TASK_UTIL_EXPECT_TRUE(queue_->GetMarker(c2.index()) ==
                          queue_->tail_marker());

    // Send u3 to c0. c1/c2 are blocked but they still want u3.
    sender_->SendActive(c0.index());
    task_util::WaitForIdle();
    queue_->PrintQueue();
    TASK_UTIL_EXPECT_EQ(3, c0.get_send_update_cnt());
    TASK_UTIL_EXPECT_EQ(0, c1.get_send_update_cnt());
    TASK_UTIL_EXPECT_EQ(0, c2.get_send_update_cnt());
    TASK_UTIL_EXPECT_TRUE(sender_->IsClientBlocked(c0.index()));
    TASK_UTIL_EXPECT_TRUE(sender_->IsClientBlocked(c1.index()));
    TASK_UTIL_EXPECT_TRUE(sender_->IsClientBlocked(c2.index()));
    // c0 blocks after sending one node, c2 is blocked, the c0/TM markers will
    // merge since both are blocked
    TASK_UTIL_EXPECT_EQ(5, queue_->size()); // u2/u3/u4/2markers
    TASK_UTIL_EXPECT_TRUE(queue_->GetMarker(c0.index()) !=
                          queue_->GetMarker(c1.index()));
    TASK_UTIL_EXPECT_TRUE(queue_->GetMarker(c0.index()) ==
                          queue_->GetMarker(c2.index()));
    TASK_UTIL_EXPECT_TRUE(queue_->GetMarker(c2.index()) ==
                          queue_->tail_marker());

    // Send u4 to c0. c1/c2 are blocked but they still want u4.
    sender_->SendActive(c0.index());
    task_util::WaitForIdle();
    queue_->PrintQueue();
    TASK_UTIL_EXPECT_EQ(4, c0.get_send_update_cnt());
    TASK_UTIL_EXPECT_EQ(0, c1.get_send_update_cnt());
    TASK_UTIL_EXPECT_EQ(0, c2.get_send_update_cnt());
    TASK_UTIL_EXPECT_TRUE(sender_->IsClientBlocked(c0.index()));
    TASK_UTIL_EXPECT_TRUE(sender_->IsClientBlocked(c1.index()));
    TASK_UTIL_EXPECT_TRUE(sender_->IsClientBlocked(c2.index()));
    // c0 blocks after sending one node, c0c2 marker (TM) has to split into 2
    TASK_UTIL_EXPECT_EQ(6, queue_->size()); // u2/u3/u4/3markers
    TASK_UTIL_EXPECT_TRUE(queue_->GetMarker(c0.index()) !=
                          queue_->GetMarker(c1.index()));
    TASK_UTIL_EXPECT_TRUE(queue_->GetMarker(c0.index()) !=
                          queue_->GetMarker(c2.index()));
    TASK_UTIL_EXPECT_TRUE(queue_->GetMarker(c1.index()) !=
                          queue_->GetMarker(c2.index()));
    TASK_UTIL_EXPECT_TRUE(queue_->GetMarker(c0.index()) ==
                          queue_->tail_marker());

    // Unblock c1. c0 has already received everything.
    sender_->SendActive(c0.index());
    sender_->SendActive(c1.index());
    task_util::WaitForIdle();
    queue_->PrintQueue();
    TASK_UTIL_EXPECT_EQ(4, c0.get_send_update_cnt()); // u1/u2/u3/u4
    TASK_UTIL_EXPECT_EQ(3, c1.get_send_update_cnt()); // u2/u3/u4
    TASK_UTIL_EXPECT_EQ(0, c2.get_send_update_cnt());
    // c1 marker will merge into TM
    TASK_UTIL_EXPECT_EQ(3, queue_->size()); // u4/2markers
    TASK_UTIL_EXPECT_TRUE(queue_->GetMarker(c0.index()) ==
                          queue_->GetMarker(c1.index()));
    TASK_UTIL_EXPECT_TRUE(queue_->GetMarker(c0.index()) !=
                          queue_->GetMarker(c2.index()));
    TASK_UTIL_EXPECT_TRUE(queue_->GetMarker(c0.index()) ==
                          queue_->tail_marker());

    // Unblock all clients. The markers should merge.
    sender_->SendActive(c0.index());
    sender_->SendActive(c1.index());
    sender_->SendActive(c2.index());
    task_util::WaitForIdle();
    queue_->PrintQueue();
    TASK_UTIL_EXPECT_EQ(4, c0.get_send_update_cnt()); // u1/u2/u3/u4
    TASK_UTIL_EXPECT_EQ(3, c1.get_send_update_cnt()); // u2/u3/u4
    TASK_UTIL_EXPECT_EQ(1, c2.get_send_update_cnt()); // u4
    TASK_UTIL_EXPECT_EQ(1, queue_->size()); // only TM
    TASK_UTIL_EXPECT_TRUE(queue_->GetMarker(c0.index()) ==
                          queue_->tail_marker());
    TASK_UTIL_EXPECT_TRUE(queue_->GetMarker(c1.index()) ==
                          queue_->tail_marker());
    TASK_UTIL_EXPECT_TRUE(queue_->GetMarker(c2.index()) ==
                          queue_->tail_marker());

    // All done
    queue_->Leave(c0.index());
    queue_->Leave(c1.index());
    queue_->Leave(c2.index());
}

TEST_F(IFMapUpdateSenderTest, RegularMarkerMergeIntoTM) {
    TestClient c0("c0");
    TestClient c1("c1");
    TestClient c2("c2");
    server_.ClientRegister(&c0);
    server_.ClientRegister(&c1);
    server_.ClientRegister(&c2);

    IFMapUpdate *u1 = CreateUpdate("u1", true);
    IFMapUpdate *u2 = CreateUpdate("u2", true);
    IFMapUpdate *u3 = CreateUpdate("u3", true);
    IFMapUpdate *u4 = CreateUpdate("u4", true);

    // c0 to receive u1/u2/u3/u4. c1/c2 to receive nothing.
    BitSet cli_bs;
    cli_bs.set(c0.index());
    u1->AdvertiseOr(cli_bs);
    u2->AdvertiseOr(cli_bs);
    u3->AdvertiseOr(cli_bs);
    u4->AdvertiseOr(cli_bs);

    queue_->Join(c0.index());
    queue_->Join(c1.index());
    queue_->Join(c2.index());

    queue_->Enqueue(u1);
    queue_->Enqueue(u2);
    queue_->Enqueue(u3);
    queue_->Enqueue(u4);
    TASK_UTIL_EXPECT_EQ(5, queue_->size()); // TM followed by 4 updates
    TASK_UTIL_EXPECT_TRUE(queue_->GetLast() != queue_->tail_marker());
    queue_->PrintQueue();

    // Block c0. Activate c1/c2.
    SetSendBlocked(c0.index());
    sender_->SendActive(c1.index());
    sender_->SendActive(c2.index());
    task_util::WaitForIdle();
    queue_->PrintQueue();
    TASK_UTIL_EXPECT_EQ(0, c0.get_send_update_cnt());
    TASK_UTIL_EXPECT_EQ(0, c1.get_send_update_cnt());
    TASK_UTIL_EXPECT_EQ(0, c2.get_send_update_cnt());
    TASK_UTIL_EXPECT_EQ(6, queue_->size()); // c0-marker, 4 updates, TM(c1/c2)

    TASK_UTIL_EXPECT_TRUE(queue_->GetLast() == queue_->tail_marker());
    TASK_UTIL_EXPECT_TRUE(queue_->GetMarker(c0.index()) !=
                          queue_->tail_marker());
    TASK_UTIL_EXPECT_TRUE(queue_->GetMarker(c1.index()) ==
                          queue_->tail_marker());
    TASK_UTIL_EXPECT_TRUE(queue_->GetMarker(c2.index()) ==
                          queue_->tail_marker());

    // Activate c0.
    sender_->SendActive(c0.index());
    task_util::WaitForIdle();
    queue_->PrintQueue();
    // all updates in one msg
    TASK_UTIL_EXPECT_EQ(1, c0.get_send_update_cnt());
    TASK_UTIL_EXPECT_EQ(0, c1.get_send_update_cnt());
    TASK_UTIL_EXPECT_EQ(0, c2.get_send_update_cnt());
    TASK_UTIL_EXPECT_EQ(1, queue_->size()); // only TM
    TASK_UTIL_EXPECT_TRUE(queue_->GetMarker(c0.index()) ==
                          queue_->tail_marker());
    TASK_UTIL_EXPECT_TRUE(queue_->GetMarker(c1.index()) ==
                          queue_->tail_marker());
    TASK_UTIL_EXPECT_TRUE(queue_->GetMarker(c2.index()) ==
                          queue_->tail_marker());

    // All done
    queue_->Leave(c0.index());
    queue_->Leave(c1.index());
    queue_->Leave(c2.index());
}

TEST_F(IFMapUpdateSenderTest, MultipleBlockAndGo3Clients) {
    TestClient c0("c0");
    TestClient c1("c1");
    TestClient c2("c2");
    server_.ClientRegister(&c0);
    server_.ClientRegister(&c1);
    server_.ClientRegister(&c2);

    IFMapUpdate *u0 = CreateUpdate("u0", true);
    IFMapUpdate *u1 = CreateUpdate("u1", false);
    IFMapUpdate *u2 = CreateUpdate("u2", true);
    IFMapUpdate *u3 = CreateUpdate("u3", false);
    IFMapUpdate *u4 = CreateUpdate("u4", true);

    // Set up all clients to receive all updates
    BitSet cli_bs;
    cli_bs.set(c0.index());
    cli_bs.set(c1.index());
    cli_bs.set(c2.index());
    u0->AdvertiseOr(cli_bs);
    u1->AdvertiseOr(cli_bs);
    u2->AdvertiseOr(cli_bs);
    u3->AdvertiseOr(cli_bs);
    u4->AdvertiseOr(cli_bs);

    queue_->Join(c0.index());
    queue_->Join(c1.index());
    queue_->Join(c2.index());

    queue_->Enqueue(u0);
    queue_->Enqueue(u1);
    queue_->Enqueue(u2);
    queue_->Enqueue(u3);
    queue_->Enqueue(u4);
    TASK_UTIL_EXPECT_EQ(6, queue_->size()); // TM followed by 5 updates
    TASK_UTIL_EXPECT_TRUE(queue_->GetLast() != queue_->tail_marker());

    // Block c0 after sending 2 nodes. c1 wont block. c2 wont even start. Each
    // message sent to the clients will have 2 nodes. So, each client should
    // receive 3 messages - 2 msgs with 2 nodes and 1 msg with 1 node.
    SetSendBlocked(c2.index());
    c0.set_send_success(false);
    sender_->SetObjectsPerMessage(2);

    queue_->PrintQueue();

    // Activate c0/c1
    sender_->SendActive(c0.index());
    sender_->SendActive(c1.index());
    task_util::WaitForIdle();
    queue_->PrintQueue();
    TASK_UTIL_EXPECT_EQ(1, c0.get_send_update_cnt()); // received u0/u1
    TASK_UTIL_EXPECT_EQ(3, c1.get_send_update_cnt()); // received all
    TASK_UTIL_EXPECT_EQ(0, c2.get_send_update_cnt()); // received none
    TASK_UTIL_EXPECT_EQ(8, queue_->size()); // 3 TMs, 5 nodes
    TASK_UTIL_EXPECT_TRUE(sender_->IsClientBlocked(c0.index()));
    TASK_UTIL_EXPECT_TRUE(sender_->IsClientBlocked(c2.index()));
    TASK_UTIL_EXPECT_TRUE(queue_->GetLast() == queue_->tail_marker());
    TASK_UTIL_EXPECT_TRUE(queue_->GetMarker(c0.index()) !=
                          queue_->tail_marker());
    TASK_UTIL_EXPECT_TRUE(queue_->GetMarker(c1.index()) ==
                          queue_->tail_marker());
    TASK_UTIL_EXPECT_TRUE(queue_->GetMarker(c0.index()) !=
                          queue_->GetMarker(c2.index()));

    // Block c2 after sending 2 nodes. c0 to remain blocked.
    c2.set_send_success(false);
    sender_->SetObjectsPerMessage(2);
    sender_->SendActive(c2.index());
    task_util::WaitForIdle();
    queue_->PrintQueue();
    TASK_UTIL_EXPECT_EQ(1, c0.get_send_update_cnt());
    TASK_UTIL_EXPECT_EQ(3, c1.get_send_update_cnt());
    TASK_UTIL_EXPECT_EQ(1, c2.get_send_update_cnt()); // received u0/u1
    // c2 would have consumed u0/u1 when it hits c0's marker. A flush will
    // cause c2 to block. Then c0/c2 will merge since both are blocked.
    TASK_UTIL_EXPECT_TRUE(queue_->GetMarker(c0.index()) ==
                          queue_->GetMarker(c2.index()));
    TASK_UTIL_EXPECT_TRUE(queue_->GetMarker(c1.index()) ==
                          queue_->tail_marker());
    TASK_UTIL_EXPECT_TRUE(queue_->GetMarker(c0.index()) !=
                          queue_->tail_marker());
    // u0/u1 should have been free'ed.
    TASK_UTIL_EXPECT_EQ(5, queue_->size()); // 2 markers, 3 nodes
    TASK_UTIL_EXPECT_TRUE(sender_->IsClientBlocked(c0.index()));
    TASK_UTIL_EXPECT_TRUE(sender_->IsClientBlocked(c2.index()));

    // Activate c0/c2. They should block after consuming 2 more nodes.
    c0.set_send_success(false);
    c2.set_send_success(false);
    sender_->SetObjectsPerMessage(2);
    sender_->SendActive(c0.index());
    sender_->SendActive(c2.index());
    task_util::WaitForIdle();
    queue_->PrintQueue();
    TASK_UTIL_EXPECT_EQ(2, c0.get_send_update_cnt()); // received u2/u3
    TASK_UTIL_EXPECT_EQ(3, c1.get_send_update_cnt());
    TASK_UTIL_EXPECT_EQ(2, c2.get_send_update_cnt()); // received u2/u3
    TASK_UTIL_EXPECT_TRUE(sender_->IsClientBlocked(c0.index()));
    TASK_UTIL_EXPECT_TRUE(sender_->IsClientBlocked(c2.index()));
    // u2/u3 should have been free'ed.
    TASK_UTIL_EXPECT_EQ(3, queue_->size()); // u4 and 2 markers
    // The markers for c0 and c2 should stay the same as they consume u2/u3
    TASK_UTIL_EXPECT_TRUE(queue_->GetMarker(c1.index()) ==
                          queue_->tail_marker());
    TASK_UTIL_EXPECT_TRUE(queue_->GetMarker(c0.index()) !=
                          queue_->tail_marker());
    TASK_UTIL_EXPECT_TRUE(queue_->GetMarker(c0.index()) ==
                          queue_->GetMarker(c2.index()));

    // Unblock all clients
    c0.set_send_success(true);
    c2.set_send_success(true);
    sender_->SendActive(c0.index());
    sender_->SendActive(c2.index());
    task_util::WaitForIdle();
    queue_->PrintQueue();
    TASK_UTIL_EXPECT_EQ(3, c0.get_send_update_cnt());
    TASK_UTIL_EXPECT_EQ(3, c1.get_send_update_cnt());
    TASK_UTIL_EXPECT_EQ(3, c2.get_send_update_cnt());
    TASK_UTIL_EXPECT_EQ(1, queue_->size()); // only TM
    TASK_UTIL_EXPECT_TRUE(queue_->GetMarker(c0.index()) ==
                          queue_->tail_marker());
    TASK_UTIL_EXPECT_TRUE(queue_->GetMarker(c1.index()) ==
                          queue_->tail_marker());
    TASK_UTIL_EXPECT_TRUE(queue_->GetMarker(c2.index()) ==
                          queue_->tail_marker());
    EXPECT_FALSE(sender_->IsClientBlocked(c0.index()));
    EXPECT_FALSE(sender_->IsClientBlocked(c1.index()));
    EXPECT_FALSE(sender_->IsClientBlocked(c2.index()));

    // All done
    queue_->Leave(c0.index());
    queue_->Leave(c1.index());
    queue_->Leave(c2.index());
}

TEST_F(IFMapUpdateSenderTest, SingleBlockAndGo3Clients) {
    TestClient c0("c0");
    TestClient c1("c1");
    TestClient c2("c2");
    server_.ClientRegister(&c0);
    server_.ClientRegister(&c1);
    server_.ClientRegister(&c2);

    IFMapUpdate *u0 = CreateUpdate("u0", true);
    IFMapUpdate *u1 = CreateUpdate("u1", false);
    IFMapUpdate *u2 = CreateUpdate("u2", true);
    IFMapUpdate *u3 = CreateUpdate("u3", false);
    IFMapUpdate *u4 = CreateUpdate("u4", true);

    BitSet cli_bs;
    cli_bs.set(c0.index());
    cli_bs.set(c1.index());
    cli_bs.set(c2.index());
    u0->AdvertiseOr(cli_bs);
    u1->AdvertiseOr(cli_bs);
    u2->AdvertiseOr(cli_bs);
    u3->AdvertiseOr(cli_bs);
    u4->AdvertiseOr(cli_bs);

    queue_->Join(c0.index());
    queue_->Join(c1.index());
    queue_->Join(c2.index());

    // Add 5 update nodes
    queue_->Enqueue(u0);
    queue_->Enqueue(u1);
    queue_->Enqueue(u2);
    queue_->Enqueue(u3);
    queue_->Enqueue(u4);
    TASK_UTIL_EXPECT_EQ(6, queue_->size()); // TM followed by 5 updates
    TASK_UTIL_EXPECT_TRUE(queue_->GetLast() != queue_->tail_marker());

    SetSendBlocked(c0.index());
    SetSendBlocked(c1.index());

    queue_->PrintQueue();

    sender_->SendActive(c2.index());
    task_util::WaitForIdle();
    queue_->PrintQueue();
    TASK_UTIL_EXPECT_EQ(0, c0.get_send_update_cnt());
    TASK_UTIL_EXPECT_EQ(0, c1.get_send_update_cnt());
    TASK_UTIL_EXPECT_EQ(1, c2.get_send_update_cnt());
    TASK_UTIL_EXPECT_EQ(7, queue_->size());
    TASK_UTIL_EXPECT_TRUE(sender_->IsClientBlocked(c0.index()));
    TASK_UTIL_EXPECT_TRUE(sender_->IsClientBlocked(c1.index()));
    EXPECT_FALSE(sender_->IsClientBlocked(c2.index()));
    TASK_UTIL_EXPECT_TRUE(queue_->GetLast() == queue_->tail_marker());
    TASK_UTIL_EXPECT_TRUE(queue_->GetMarker(c2.index()) ==
                          queue_->tail_marker());

    sender_->SendActive(c1.index());
    task_util::WaitForIdle();
    queue_->PrintQueue();
    TASK_UTIL_EXPECT_EQ(0, c0.get_send_update_cnt());
    TASK_UTIL_EXPECT_EQ(1, c1.get_send_update_cnt());
    TASK_UTIL_EXPECT_EQ(1, c2.get_send_update_cnt());
    TASK_UTIL_EXPECT_EQ(7, queue_->size());
    TASK_UTIL_EXPECT_TRUE(sender_->IsClientBlocked(c0.index()));
    EXPECT_FALSE(sender_->IsClientBlocked(c1.index()));
    EXPECT_FALSE(sender_->IsClientBlocked(c2.index()));
    TASK_UTIL_EXPECT_TRUE(queue_->GetLast() == queue_->tail_marker());
    TASK_UTIL_EXPECT_TRUE(queue_->GetMarker(c1.index()) ==
                          queue_->tail_marker());
    TASK_UTIL_EXPECT_TRUE(queue_->GetMarker(c2.index()) ==
                          queue_->tail_marker());

    sender_->SendActive(c0.index());
    task_util::WaitForIdle();
    queue_->PrintQueue();
    TASK_UTIL_EXPECT_EQ(1, c0.get_send_update_cnt());
    TASK_UTIL_EXPECT_EQ(1, c1.get_send_update_cnt());
    TASK_UTIL_EXPECT_EQ(1, c2.get_send_update_cnt());
    TASK_UTIL_EXPECT_EQ(1, queue_->size());
    EXPECT_FALSE(sender_->IsClientBlocked(c0.index()));
    EXPECT_FALSE(sender_->IsClientBlocked(c1.index()));
    EXPECT_FALSE(sender_->IsClientBlocked(c2.index()));
    TASK_UTIL_EXPECT_TRUE(queue_->GetLast() == queue_->tail_marker());
    TASK_UTIL_EXPECT_TRUE(queue_->GetMarker(c0.index()) ==
                          queue_->tail_marker());
    TASK_UTIL_EXPECT_TRUE(queue_->GetMarker(c1.index()) ==
                          queue_->tail_marker());
    TASK_UTIL_EXPECT_TRUE(queue_->GetMarker(c2.index()) ==
                          queue_->tail_marker());

    // All done
    queue_->Leave(c0.index());
    queue_->Leave(c1.index());
    queue_->Leave(c2.index());
}

TEST_F(IFMapUpdateSenderTest, MarkerMergeTest2) {
    TestClient c0("c0");
    TestClient c1("c1");
    TestClient c2("c2");
    TestClient c3("c3");
    server_.ClientRegister(&c0);
    server_.ClientRegister(&c1);
    server_.ClientRegister(&c2);
    server_.ClientRegister(&c3);

    IFMapUpdate *u1 = CreateUpdate("u1", false);
    IFMapUpdate *u2 = CreateUpdate("u2", true);
    IFMapUpdate *u3 = CreateUpdate("u3", false);
    IFMapUpdate *u4 = CreateUpdate("u4", true);

    // c0 gets u1/u2/u3/u4, c1 gets u2/u3/u4, c2 gets u3/u4, c3 gets u4
    BitSet cli_bs;
    cli_bs.set(c0.index());
    u1->AdvertiseOr(cli_bs);
    cli_bs.set(c1.index());
    u2->AdvertiseOr(cli_bs);
    cli_bs.set(c2.index());
    u3->AdvertiseOr(cli_bs);
    cli_bs.set(c3.index());
    u4->AdvertiseOr(cli_bs);

    queue_->Join(c0.index());
    queue_->Join(c1.index());
    queue_->Join(c2.index());
    queue_->Join(c3.index());

    queue_->Enqueue(u1);
    queue_->Enqueue(u2);
    queue_->Enqueue(u3);
    queue_->Enqueue(u4);
    TASK_UTIL_EXPECT_EQ(5, queue_->size()); // TM followed by 4 updates

    // Insert marker for c0 before tail_marker
    IFMapMarker *marker = queue_->GetMarker(c0.index());
    ASSERT_TRUE(marker != NULL);
    BitSet rem_bs;
    rem_bs.set(c0.index());
    queue_->MarkerSplitBefore(marker, marker, rem_bs);
    TASK_UTIL_EXPECT_EQ(6, queue_->size()); // 5 from before + c0 marker
    rem_bs.Reset(rem_bs);
    ASSERT_TRUE(rem_bs.empty());
    TASK_UTIL_EXPECT_TRUE(queue_->GetMarker(c0.index()) !=
                          queue_->tail_marker());

    // Insert marker for c1 after u1
    marker = queue_->GetMarker(c1.index());
    ASSERT_TRUE(marker != NULL);
    rem_bs.set(c1.index());
    queue_->MarkerSplitAfter(marker, u1, rem_bs);
    TASK_UTIL_EXPECT_EQ(7, queue_->size()); // 6 from before + c1 marker
    rem_bs.Reset(rem_bs);
    ASSERT_TRUE(rem_bs.empty());
    TASK_UTIL_EXPECT_TRUE(queue_->GetMarker(c1.index()) !=
                          queue_->tail_marker());
    TASK_UTIL_EXPECT_TRUE(queue_->GetMarker(c2.index()) ==
                          queue_->tail_marker());
    TASK_UTIL_EXPECT_TRUE(queue_->GetMarker(c3.index()) ==
                          queue_->tail_marker());

    // Insert marker for c2 after u2
    marker = queue_->GetMarker(c2.index());
    ASSERT_TRUE(marker != NULL);
    rem_bs.set(c2.index());
    queue_->MarkerSplitAfter(marker, u2, rem_bs);
    TASK_UTIL_EXPECT_EQ(8, queue_->size()); // 7 from before + c2 marker
    rem_bs.Reset(rem_bs);
    ASSERT_TRUE(rem_bs.empty());
    TASK_UTIL_EXPECT_TRUE(queue_->GetMarker(c2.index()) !=
                          queue_->tail_marker());
    TASK_UTIL_EXPECT_TRUE(queue_->GetMarker(c3.index()) ==
                          queue_->tail_marker());

    // Move marker for c3 (tail_marker) after u3
    marker = queue_->GetMarker(c3.index());
    ASSERT_TRUE(marker != NULL);
    queue_->MoveMarkerAfter(marker, u3);
    TASK_UTIL_EXPECT_EQ(8, queue_->size()); // same as before, just moving TM
    TASK_UTIL_EXPECT_TRUE(queue_->GetMarker(c3.index()) ==
                          queue_->tail_marker());
 
    queue_->PrintQueue();

    // c0/c2 will block after sending one node. c1/c3 will not block
    // All the clients are ready and waiting for a trigger.
    c0.set_send_success(false);
    c1.set_send_success(true);
    c2.set_send_success(false);
    c3.set_send_success(true);
    sender_->SendActive(c0.index());
    sender_->SetObjectsPerMessage(1);
    task_util::WaitForIdle();
    queue_->PrintQueue();
    // c0 will send u1 and block. While processing c1-marker, it will be
    // left behind since its blocked. c1 will continue, consume u2 and then
    // encounter c2-marker and merge with it since both are ready. Then c1/c2
    // will consume u3, c2 will block and c1 will continue. c1-marker will then
    // encounter c3-marker (tm), merge with it and both c1/c3 will continue and
    // consume u4. TM will have c1/c3.
    TASK_UTIL_EXPECT_EQ(1, c0.get_send_update_cnt());
    TASK_UTIL_EXPECT_EQ(3, c1.get_send_update_cnt());
    TASK_UTIL_EXPECT_EQ(1, c2.get_send_update_cnt());
    TASK_UTIL_EXPECT_EQ(1, c3.get_send_update_cnt());
    TASK_UTIL_EXPECT_EQ(6, queue_->size());
    TASK_UTIL_EXPECT_TRUE(sender_->IsClientBlocked(c0.index()));
    EXPECT_FALSE(sender_->IsClientBlocked(c1.index()));
    TASK_UTIL_EXPECT_TRUE(sender_->IsClientBlocked(c2.index()));
    EXPECT_FALSE(sender_->IsClientBlocked(c3.index()));
    TASK_UTIL_EXPECT_TRUE(queue_->GetMarker(c0.index()) !=
                          queue_->tail_marker());
    TASK_UTIL_EXPECT_TRUE(queue_->GetMarker(c1.index()) ==
                          queue_->tail_marker());
    TASK_UTIL_EXPECT_TRUE(queue_->GetMarker(c2.index()) !=
                          queue_->tail_marker());
    TASK_UTIL_EXPECT_TRUE(queue_->GetMarker(c3.index()) ==
                          queue_->tail_marker());

    // c0 will not blocked after sending. Both c0/c2 are blocked right now.
    c0.set_send_success(true);
    c2.set_send_success(false);
    // activate c0.
    sender_->SendActive(c0.index());
    task_util::WaitForIdle();
    queue_->PrintQueue();
    // c0 will consume u2/u3 and encounter c2-marker. It will leave c2 marker
    // behind since its blocked and continue to consume u4. It will then merge
    // with the TM.
    TASK_UTIL_EXPECT_EQ(4, c0.get_send_update_cnt());
    TASK_UTIL_EXPECT_EQ(3, c1.get_send_update_cnt());
    TASK_UTIL_EXPECT_EQ(1, c2.get_send_update_cnt());
    TASK_UTIL_EXPECT_EQ(1, c3.get_send_update_cnt());
    TASK_UTIL_EXPECT_EQ(3, queue_->size());
    EXPECT_FALSE(sender_->IsClientBlocked(c0.index()));
    EXPECT_FALSE(sender_->IsClientBlocked(c1.index()));
    TASK_UTIL_EXPECT_TRUE(sender_->IsClientBlocked(c2.index()));
    EXPECT_FALSE(sender_->IsClientBlocked(c3.index()));
    TASK_UTIL_EXPECT_TRUE(queue_->GetMarker(c0.index()) ==
                          queue_->tail_marker());
    TASK_UTIL_EXPECT_TRUE(queue_->GetMarker(c1.index()) ==
                          queue_->tail_marker());
    TASK_UTIL_EXPECT_TRUE(queue_->GetMarker(c2.index()) !=
                          queue_->tail_marker());
    TASK_UTIL_EXPECT_TRUE(queue_->GetMarker(c3.index()) ==
                          queue_->tail_marker());

    // c2 will block after sending. Note, that if we had set this to true, c2
    // would merge into the TM.
    c2.set_send_success(false);
    sender_->SendActive(c2.index());
    task_util::WaitForIdle();
    queue_->PrintQueue();
    // c2 consumes u4 and encounters TM. While merging with TM, since its
    // blocked, c2-marker stays as a separate marker.
    TASK_UTIL_EXPECT_EQ(4, c0.get_send_update_cnt());
    TASK_UTIL_EXPECT_EQ(3, c1.get_send_update_cnt());
    TASK_UTIL_EXPECT_EQ(2, c2.get_send_update_cnt());
    TASK_UTIL_EXPECT_EQ(1, c3.get_send_update_cnt());
    TASK_UTIL_EXPECT_EQ(2, queue_->size()); // c2-marker and TM
    EXPECT_FALSE(sender_->IsClientBlocked(c0.index()));
    EXPECT_FALSE(sender_->IsClientBlocked(c1.index()));
    TASK_UTIL_EXPECT_TRUE(sender_->IsClientBlocked(c2.index())); // blocked
    EXPECT_FALSE(sender_->IsClientBlocked(c3.index()));
    TASK_UTIL_EXPECT_TRUE(queue_->GetMarker(c0.index()) ==
                          queue_->tail_marker());
    TASK_UTIL_EXPECT_TRUE(queue_->GetMarker(c1.index()) ==
                          queue_->tail_marker());
    TASK_UTIL_EXPECT_TRUE(queue_->GetMarker(c2.index()) !=
                          queue_->tail_marker());
    TASK_UTIL_EXPECT_TRUE(queue_->GetMarker(c3.index()) ==
                          queue_->tail_marker());

    // All done
    queue_->Leave(c0.index());
    queue_->Leave(c1.index());
    queue_->Leave(c2.index());
    queue_->Leave(c3.index());
}

TEST_F(IFMapUpdateSenderTest, Bug228) {
    TestClient c0("c0");
    TestClient c1("c1");
    server_.ClientRegister(&c0);
    server_.ClientRegister(&c1);

    IFMapUpdate *u1 = CreateUpdate("u1", true);
    IFMapUpdate *u2 = CreateUpdate("u2", false);
    IFMapUpdate *u3 = CreateUpdate("u3", true);
    IFMapUpdate *u4 = CreateUpdate("u4", false);

    // Both to receive all updates.
    BitSet cli_bs;
    cli_bs.set(c0.index());
    cli_bs.set(c1.index());
    u1->AdvertiseOr(cli_bs);
    u2->AdvertiseOr(cli_bs);
    u3->AdvertiseOr(cli_bs);
    u4->AdvertiseOr(cli_bs);

    queue_->Join(c0.index());
    queue_->Join(c1.index());

    // Add 4 update nodes
    queue_->Enqueue(u1);
    queue_->Enqueue(u2);
    queue_->Enqueue(u3);
    queue_->Enqueue(u4);
    TASK_UTIL_EXPECT_EQ(5, queue_->size()); // 4 updates and 1 tail_marker
    TASK_UTIL_EXPECT_TRUE(queue_->GetLast() != queue_->tail_marker());

    queue_->PrintQueue(); // Should print 5 elements

    // Set up so that clients block after sending one node
    c0.set_send_success(false);
    c1.set_send_success(false);
    sender_->SetObjectsPerMessage(1);

    // Block c0 and activate c1. The markers will split and c0 will get its own
    // marker.
    SetSendBlocked(c0.index());
    sender_->SendActive(c1.index());
    task_util::WaitForIdle();
    queue_->PrintQueue();
    TASK_UTIL_EXPECT_EQ(0, c0.get_send_update_cnt());
    TASK_UTIL_EXPECT_EQ(1, c1.get_send_update_cnt());
    TASK_UTIL_EXPECT_EQ(6, queue_->size()); // 4 nodes + 2 markers
    TASK_UTIL_EXPECT_TRUE(sender_->IsClientBlocked(c0.index()));
    TASK_UTIL_EXPECT_TRUE(queue_->GetMarker(c0.index()) !=
                          queue_->tail_marker());
    TASK_UTIL_EXPECT_TRUE(queue_->GetMarker(c1.index()) ==
                          queue_->tail_marker());

    // Activate c0 and immediately unregister him so that c0 is cleaned up
    task_util::TaskSchedulerStop();
    sender_->SendActive(c0.index());
    server_.ClientUnregister(&c0);
    task_util::TaskSchedulerStart();

    TASK_UTIL_EXPECT_EQ(4, queue_->size()); // u1, c0-marker gone
    TASK_UTIL_EXPECT_TRUE(queue_->GetMarker(c0.index()) == NULL);
    TASK_UTIL_EXPECT_TRUE(queue_->GetMarker(c1.index()) ==
                          queue_->tail_marker());
    TASK_UTIL_EXPECT_EQ(0, c0.get_send_update_cnt());
    queue_->PrintQueue();

    // Activate c1 and let him consume everything
    c1.set_send_success(true);
    sender_->SendActive(c1.index());
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(4, c1.get_send_update_cnt());
    TASK_UTIL_EXPECT_EQ(1, queue_->size());
    TASK_UTIL_EXPECT_TRUE(queue_->GetMarker(c1.index()) ==
                          queue_->tail_marker());
    queue_->PrintQueue();
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    bool success = RUN_ALL_TESTS();
    TaskScheduler::GetInstance()->Terminate();
    return success;
}
