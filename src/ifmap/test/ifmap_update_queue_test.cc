/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "ifmap/ifmap_update_queue.h"

#include "base/logging.h"
#include "base/task.h"
#include "db/db.h"
#include "db/db_graph.h"
#include "db/db_table.h"
#include "io/event_manager.h"
#include "ifmap/ifmap_node.h"
#include "ifmap/ifmap_server.h"
#include "ifmap/ifmap_table.h"
#include "ifmap/ifmap_update.h"
#include "schema/vnc_cfg_types.h"
#include "testing/gunit.h"

using namespace std;

class IFMapUpdateQueueTest : public ::testing::Test {
protected:
    IFMapUpdateQueueTest()
        : db_(TaskScheduler::GetInstance()->GetTaskId("db::IFMapTable")),
          server_(&db_, &graph_, evm_.io_service()), tbl_(NULL),
          queue_(new IFMapUpdateQueue(&server_)) {
    }

    virtual void SetUp() {
        vnc_cfg_Server_ModuleInit(&db_, &graph_);
        tbl_ = IFMapTable::FindTable(&db_, "virtual-network");
        ASSERT_TRUE(tbl_);
    }
    virtual void TearDown() {
        evm_.Shutdown();
    }

    DB db_;
    DBGraph graph_;
    EventManager evm_;
    IFMapServer server_;
    DBTable *tbl_;
    auto_ptr<IFMapUpdateQueue> queue_;
};

static IFMapUpdate *CreateUpdate(DBEntry *entry) {
    IFMapUpdate *update = new IFMapUpdate(static_cast<IFMapNode *>(entry),
                                          true);
    BitSet sadv;
    sadv.set(1);
    update->SetAdvertise(sadv);
    return update;
}

TEST_F(IFMapUpdateQueueTest, Basic) {
    IFMapTable::RequestKey key;
    IFMapMarker *marker;

    key.id_name = "a";
    auto_ptr<DBEntry> n1(tbl_->AllocEntry(&key));
    key.id_name = "b";
    auto_ptr<DBEntry> n2(tbl_->AllocEntry(&key));
    key.id_name = "c";
    auto_ptr<DBEntry> n3(tbl_->AllocEntry(&key));
    key.id_name = "d";
    auto_ptr<DBEntry> n4(tbl_->AllocEntry(&key));

    IFMapUpdate *u1 = CreateUpdate(n1.get());
    IFMapUpdate *u2 = CreateUpdate(n2.get());
    IFMapUpdate *u3 = CreateUpdate(n3.get());
    IFMapUpdate *u4 = CreateUpdate(n4.get());

    queue_->Join(1);     // client 1
    queue_->Join(2);     // client 2
    queue_->Join(3);     // client 3
    queue_->Join(5);     // client 5

    // Add 4 update nodes
    queue_->Enqueue(u1);
    queue_->Enqueue(u2);
    queue_->Enqueue(u3);
    queue_->Enqueue(u4);
    EXPECT_EQ(queue_->size(), 5); // 4 updates and 1 tail_marker
    EXPECT_EQ(queue_->tail_marker()->get_sequence(), 1U);
    EXPECT_EQ(u1->get_sequence(), 2U);
    EXPECT_EQ(u2->get_sequence(), 3U);
    EXPECT_EQ(u3->get_sequence(), 4U);
    EXPECT_EQ(u4->get_sequence(), 5U);

    // Insert marker for client 1 after u1
    marker = queue_->GetMarker(1);
    ASSERT_TRUE(marker != NULL);
    BitSet rem_bs;
    rem_bs.set(1);
    queue_->MarkerSplitAfter(marker, u1, rem_bs);
    EXPECT_EQ(queue_->size(), 6); // 5 from before + 1 marker
    // Marker for client 1 is before u2 whose sequence is 3
    marker = queue_->GetMarker(1);
    EXPECT_EQ(marker->get_sequence(), 3U);
    EXPECT_EQ(queue_->tail_marker()->get_sequence(), 1U);
    rem_bs.Reset(rem_bs);
    ASSERT_TRUE(rem_bs.empty());

    // Insert marker for client 2 before u3 (after u2)
    marker = queue_->GetMarker(2);
    ASSERT_TRUE(marker != NULL);
    rem_bs.set(2);
    queue_->MarkerSplitBefore(marker, u3, rem_bs);
    EXPECT_EQ(queue_->size(), 7); // 6 from before + 1 marker
    // Marker for client 2 is before u3 whose sequence is 4
    marker = queue_->GetMarker(2);
    EXPECT_EQ(marker->get_sequence(), 4U);
    EXPECT_EQ(queue_->tail_marker()->get_sequence(), 1U);
    rem_bs.Reset(rem_bs);
    ASSERT_TRUE(rem_bs.empty());

    // Insert marker for client 3 after u3
    marker = queue_->GetMarker(3);
    ASSERT_TRUE(marker != NULL);
    rem_bs.set(3);
    queue_->MarkerSplitAfter(marker, u3, rem_bs);
    EXPECT_EQ(queue_->size(), 8); // 7 from before + 1 marker
    // Marker for client 3 is before u4 whose sequence is 5
    marker = queue_->GetMarker(3);
    EXPECT_EQ(marker->get_sequence(), 5U);
    rem_bs.Reset(rem_bs);
    ASSERT_TRUE(rem_bs.empty());

    // Move tail_marker_ beyond u4. It should get the next higher number, 6
    queue_->MoveMarkerAfter(queue_->tail_marker(), u4);
    EXPECT_EQ(queue_->size(), 8); // no change from before
    EXPECT_EQ(queue_->tail_marker()->get_sequence(), 6U);

    // Should print 8 elements
    queue_->PrintQueue();

    // Merge markers for 3 and 1 with both ending with sequence 5
    marker = queue_->GetMarker(1);
    EXPECT_EQ(marker->get_sequence(), 3U);
    rem_bs.set(1);
    queue_->MarkerMerge(queue_->GetMarker(3), queue_->GetMarker(1), rem_bs);
    EXPECT_EQ(queue_->size(), 7); // 8 from before, deleting 1
    rem_bs.Reset(rem_bs);
    ASSERT_TRUE(rem_bs.empty());
    marker = queue_->GetMarker(1);
    EXPECT_EQ(marker->get_sequence(), 5U);
    marker = queue_->GetMarker(3);
    EXPECT_EQ(marker->get_sequence(), 5U);

    // Merge client 2's marker to the marker for clients 3 and 1
    marker = queue_->GetMarker(2);
    EXPECT_EQ(marker->get_sequence(), 4U);
    rem_bs.set(2);
    queue_->MarkerMerge(queue_->GetMarker(3), queue_->GetMarker(2), rem_bs);
    EXPECT_EQ(queue_->size(), 6); // 7 from before, deleting 1
    rem_bs.Reset(rem_bs);
    ASSERT_TRUE(rem_bs.empty());
    marker = queue_->GetMarker(2);
    EXPECT_EQ(marker->get_sequence(), 5U);
    marker = queue_->GetMarker(3);
    EXPECT_EQ(marker->get_sequence(), 5U);

    // Merge markers for clients 1,2,3 and tail_marker_
    rem_bs.set(1);
    rem_bs.set(2);
    rem_bs.set(3);
    queue_->MarkerMerge(queue_->tail_marker(), queue_->GetMarker(3), rem_bs);
    EXPECT_EQ(queue_->size(), 5); // 6 from before, deleting 1
    rem_bs.Reset(rem_bs);
    ASSERT_TRUE(rem_bs.empty());
    marker = queue_->GetMarker(1);
    EXPECT_EQ(marker->get_sequence(), 6U);
    marker = queue_->GetMarker(2);
    EXPECT_EQ(marker->get_sequence(), 6U);
    marker = queue_->GetMarker(3);
    EXPECT_EQ(marker->get_sequence(), 6U);
    EXPECT_EQ(queue_->tail_marker()->get_sequence(), 6U);

    queue_->Dequeue(u1);
    queue_->Dequeue(u2);
    queue_->Dequeue(u3);
    queue_->Dequeue(u4);
    EXPECT_EQ(queue_->size(), 1); // 4 updates and 1 tail_marker

    // Should print only the tail_marker_
    queue_->PrintQueue();

    queue_->Leave(1);     // client 1
    queue_->Leave(2);     // client 2
    queue_->Leave(3);     // client 3
    queue_->Leave(5);     // client 5

    delete(u1);
    delete(u2);
    delete(u3);
    delete(u4);
}

TEST_F(IFMapUpdateQueueTest, MarkerMerge) {
    IFMapTable::RequestKey key;
    IFMapMarker *marker;

    key.id_name = "a";
    auto_ptr<DBEntry> n1(tbl_->AllocEntry(&key));
    key.id_name = "b";
    auto_ptr<DBEntry> n2(tbl_->AllocEntry(&key));

    IFMapUpdate *u1 = CreateUpdate(n1.get());
    IFMapUpdate *u2 = CreateUpdate(n2.get());

    queue_->Join(1);     // client 1
    queue_->Join(2);     // client 2

    queue_->Enqueue(u1);
    queue_->Enqueue(u2);
    EXPECT_EQ(queue_->size(), 3); // 2 updates and 1 tail_marker
    EXPECT_EQ(queue_->tail_marker()->get_sequence(), 1U);
    EXPECT_EQ(u1->get_sequence(), 2U);
    EXPECT_EQ(u2->get_sequence(), 3U);

    // Insert marker for client 1 after u1
    marker = queue_->GetMarker(1);
    ASSERT_TRUE(marker != NULL);
    BitSet rem_bs;
    rem_bs.set(1);
    queue_->MarkerSplitAfter(marker, u1, rem_bs);
    EXPECT_EQ(queue_->size(), 4); // 3 from before + 1 marker
    marker = queue_->GetMarker(1);
    EXPECT_EQ(marker->get_sequence(), u2->get_sequence()); // should be 3
    EXPECT_EQ(marker->get_sequence(), 3U);
    rem_bs.Reset(rem_bs);
    ASSERT_TRUE(rem_bs.empty());

    // Insert marker for client 2 after u2
    marker = queue_->GetMarker(2);
    ASSERT_TRUE(marker != NULL);
    rem_bs.set(2);
    queue_->MoveMarkerAfter(marker, u2);
    EXPECT_EQ(queue_->size(), 4); // 3 from before + 1 marker
    marker = queue_->GetMarker(1);
    EXPECT_EQ(marker->get_sequence(), u2->get_sequence()); // should be 3
    EXPECT_EQ(marker->get_sequence(), 3U);
    marker = queue_->GetMarker(2);
    EXPECT_EQ(marker->get_sequence(), 4U);  // should get the next higher value
    rem_bs.Reset(rem_bs);
    ASSERT_TRUE(rem_bs.empty());

    // Merge client 1's marker into client 2's marker
    rem_bs.set(1);
    queue_->MarkerMerge(queue_->GetMarker(2), queue_->GetMarker(1), rem_bs);
    marker = queue_->GetMarker(1);
    EXPECT_EQ(marker->get_sequence(), queue_->GetMarker(2)->get_sequence());
    EXPECT_EQ(marker->get_sequence(), 4U);
    EXPECT_EQ(queue_->GetMarker(2)->get_sequence(), 4U);

    queue_->Dequeue(u1);
    queue_->Dequeue(u2);

    queue_->Leave(1);     // client 1
    queue_->Leave(2);     // client 2

    delete(u1);
    delete(u2);
}

TEST_F(IFMapUpdateQueueTest, MarkerSplitBefore) {
    IFMapTable::RequestKey key;
    IFMapMarker *marker;

    key.id_name = "a";
    auto_ptr<DBEntry> n1(tbl_->AllocEntry(&key));
    key.id_name = "b";
    auto_ptr<DBEntry> n2(tbl_->AllocEntry(&key));

    IFMapUpdate *u1 = CreateUpdate(n1.get());
    IFMapUpdate *u2 = CreateUpdate(n2.get());

    queue_->Join(1);     // client 1
    queue_->Join(2);     // client 2

    queue_->Enqueue(u1);
    queue_->Enqueue(u2);
    EXPECT_EQ(queue_->size(), 3);
    EXPECT_EQ(queue_->tail_marker()->get_sequence(), 1U);
    EXPECT_EQ(u1->get_sequence(), 2U);
    EXPECT_EQ(u2->get_sequence(), 3U);

    // Split client 1's marker before u2
    marker = queue_->GetMarker(1);
    ASSERT_TRUE(marker != NULL);
    BitSet rem_bs;
    rem_bs.set(1);
    queue_->MarkerSplitBefore(marker, u2, rem_bs);
    EXPECT_EQ(queue_->size(), 4);
    marker = queue_->GetMarker(1);
    EXPECT_EQ(marker->get_sequence(), u2->get_sequence()); // should be 3
    EXPECT_EQ(marker->get_sequence(), 3U);
    EXPECT_EQ(queue_->tail_marker()->get_sequence(), 1U);
    rem_bs.Reset(rem_bs);
    ASSERT_TRUE(rem_bs.empty());

    queue_->Dequeue(u1);
    queue_->Dequeue(u2);

    queue_->Leave(1);     // client 1
    queue_->Leave(2);     // client 2

    delete(u1);
    delete(u2);
}

TEST_F(IFMapUpdateQueueTest, MarkerSplitAfter) {
    IFMapTable::RequestKey key;
    IFMapMarker *marker;

    key.id_name = "a";
    auto_ptr<DBEntry> n1(tbl_->AllocEntry(&key));
    key.id_name = "b";
    auto_ptr<DBEntry> n2(tbl_->AllocEntry(&key));

    IFMapUpdate *u1 = CreateUpdate(n1.get());
    IFMapUpdate *u2 = CreateUpdate(n2.get());

    queue_->Join(1);     // client 1
    queue_->Join(2);     // client 2
    queue_->Join(3);     // client 3

    queue_->Enqueue(u1);
    queue_->Enqueue(u2);
    EXPECT_EQ(queue_->size(), 3); // 2 updates and 1 tail_marker
    EXPECT_EQ(queue_->tail_marker()->get_sequence(), 1U);
    EXPECT_EQ(u1->get_sequence(), 2U);
    EXPECT_EQ(u2->get_sequence(), 3U);

    // Split client 1's marker after u1
    marker = queue_->GetMarker(1);
    ASSERT_TRUE(marker != NULL);
    BitSet rem_bs;
    rem_bs.set(1);
    queue_->MarkerSplitAfter(marker, u1, rem_bs);
    EXPECT_EQ(queue_->size(), 4);
    marker = queue_->GetMarker(1);
    EXPECT_EQ(marker->get_sequence(), u2->get_sequence()); // same as u2's
    EXPECT_EQ(marker->get_sequence(), 3U);
    rem_bs.Reset(rem_bs);
    ASSERT_TRUE(rem_bs.empty());

    // Split client 2's marker after u2
    marker = queue_->GetMarker(2);
    ASSERT_TRUE(marker != NULL);
    rem_bs.set(2);
    queue_->MarkerSplitAfter(marker, u2, rem_bs);
    EXPECT_EQ(queue_->size(), 5);
    marker = queue_->GetMarker(2);
    EXPECT_EQ(marker->get_sequence(), 4U);  // should get the next higher value
    rem_bs.Reset(rem_bs);
    ASSERT_TRUE(rem_bs.empty());

    queue_->Dequeue(u1);
    queue_->Dequeue(u2);

    queue_->Leave(1);     // client 1
    queue_->Leave(2);     // client 2
    queue_->Leave(3);     // client 3

    delete(u1);
    delete(u2);
}

TEST_F(IFMapUpdateQueueTest, MoveMarkerBefore) {
    IFMapTable::RequestKey key;
    IFMapMarker *marker;

    key.id_name = "a";
    auto_ptr<DBEntry> n1(tbl_->AllocEntry(&key));
    key.id_name = "b";
    auto_ptr<DBEntry> n2(tbl_->AllocEntry(&key));

    IFMapUpdate *u1 = CreateUpdate(n1.get());
    IFMapUpdate *u2 = CreateUpdate(n2.get());

    queue_->Enqueue(u1);
    queue_->Enqueue(u2);
    EXPECT_EQ(queue_->size(), 3); // 2 updates and 1 tail_marker
    EXPECT_EQ(queue_->tail_marker()->get_sequence(), 1U);
    EXPECT_EQ(u1->get_sequence(), 2U);
    EXPECT_EQ(u2->get_sequence(), 3U);

    queue_->Join(1);     // client 1

    // Move client 1's marker before u2. Its sequence should become 3.
    marker = queue_->GetMarker(1);
    EXPECT_EQ(marker->get_sequence(), 1U);
    queue_->MoveMarkerBefore(marker, u2);
    EXPECT_EQ(marker->get_sequence(), u2->get_sequence());
    EXPECT_EQ(marker->get_sequence(), 3U);

    queue_->Dequeue(u1);
    queue_->Dequeue(u2);

    queue_->Leave(1);     // client 1

    delete(u1);
    delete(u2);
}

TEST_F(IFMapUpdateQueueTest, MoveMarkerAfter) {
    IFMapTable::RequestKey key;
    IFMapMarker *marker;

    key.id_name = "a";
    auto_ptr<DBEntry> n1(tbl_->AllocEntry(&key));
    key.id_name = "b";
    auto_ptr<DBEntry> n2(tbl_->AllocEntry(&key));

    IFMapUpdate *u1 = CreateUpdate(n1.get());
    IFMapUpdate *u2 = CreateUpdate(n2.get());

    queue_->Enqueue(u1);
    queue_->Enqueue(u2);
    EXPECT_EQ(queue_->size(), 3); // 2 updates and 1 tail_marker
    EXPECT_EQ(queue_->tail_marker()->get_sequence(), 1U);
    EXPECT_EQ(u1->get_sequence(), 2U);
    EXPECT_EQ(u2->get_sequence(), 3U);

    queue_->Join(1);     // client 1

    marker = queue_->GetMarker(1);
    queue_->MoveMarkerAfter(marker, u1);
    EXPECT_EQ(marker->get_sequence(), u2->get_sequence());
    EXPECT_EQ(marker->get_sequence(), 3U);

    queue_->Dequeue(u1);
    queue_->Dequeue(u2);

    queue_->Leave(1);     // client 1

    delete(u1);
    delete(u2);
}

TEST_F(IFMapUpdateQueueTest, Next) {
    IFMapTable::RequestKey key;

    key.id_name = "a";
    auto_ptr<DBEntry> n1(tbl_->AllocEntry(&key));
    key.id_name = "b";
    auto_ptr<DBEntry> n2(tbl_->AllocEntry(&key));

    IFMapUpdate *u1 = CreateUpdate(n1.get());
    IFMapUpdate *u2 = CreateUpdate(n2.get());

    queue_->Enqueue(u1);
    queue_->Enqueue(u2);
    EXPECT_EQ(queue_->size(), 3); // 2 updates and 1 tail_marker
    EXPECT_EQ(queue_->tail_marker()->get_sequence(), 1U);
    EXPECT_EQ(u1->get_sequence(), 2U);
    EXPECT_EQ(u2->get_sequence(), 3U);

    // u1 is after the tail-marker
    IFMapListEntry *next = queue_->Next(queue_->tail_marker());
    EXPECT_EQ(next->get_sequence(), u1->get_sequence());

    // u2 is after u1
    next = queue_->Next(u1);
    EXPECT_EQ(next->get_sequence(), u2->get_sequence());

    queue_->Dequeue(u1);
    queue_->Dequeue(u2);

    delete(u1);
    delete(u2);
}

TEST_F(IFMapUpdateQueueTest, Previous) {
    IFMapTable::RequestKey key;

    key.id_name = "a";
    auto_ptr<DBEntry> n1(tbl_->AllocEntry(&key));
    key.id_name = "b";
    auto_ptr<DBEntry> n2(tbl_->AllocEntry(&key));

    IFMapUpdate *u1 = CreateUpdate(n1.get());
    IFMapUpdate *u2 = CreateUpdate(n2.get());

    queue_->Enqueue(u1);
    queue_->Enqueue(u2);
    EXPECT_EQ(queue_->size(), 3); // 2 updates and 1 tail_marker
    EXPECT_EQ(queue_->tail_marker()->get_sequence(), 1U);
    EXPECT_EQ(u1->get_sequence(), 2U);
    EXPECT_EQ(u2->get_sequence(), 3U);

    // u1 is before u2
    IFMapListEntry *previous = queue_->Previous(u2);
    EXPECT_EQ(previous->get_sequence(), u1->get_sequence());

    // tail-marker is before u1
    previous = queue_->Previous(u1);
    EXPECT_EQ(previous->get_sequence(), queue_->tail_marker()->get_sequence());

    queue_->Dequeue(u1);
    queue_->Dequeue(u2);

    delete(u1);
    delete(u2);
}

TEST_F(IFMapUpdateQueueTest, IsEmpty) {
    EXPECT_EQ(queue_->size(), 1); // only tail-marker
    EXPECT_TRUE(queue_->empty());
}

TEST_F(IFMapUpdateQueueTest, GetLast) {
    IFMapTable::RequestKey key;

    key.id_name = "a";
    auto_ptr<DBEntry> n1(tbl_->AllocEntry(&key));
    key.id_name = "b";
    auto_ptr<DBEntry> n2(tbl_->AllocEntry(&key));
    key.id_name = "c";
    auto_ptr<DBEntry> n3(tbl_->AllocEntry(&key));

    IFMapUpdate *u1 = CreateUpdate(n1.get());
    IFMapUpdate *u2 = CreateUpdate(n2.get());
    IFMapUpdate *u3 = CreateUpdate(n3.get());

    // Enqueue u1 and check if its the last item
    queue_->Enqueue(u1);
    EXPECT_EQ(queue_->size(), 2); // 1 update and 1 tail_marker
    EXPECT_EQ(queue_->tail_marker()->get_sequence(), 1U);
    EXPECT_EQ(u1->get_sequence(), 2U);
    IFMapListEntry *last = queue_->GetLast();
    EXPECT_EQ(last->get_sequence(), u1->get_sequence());

    // Enqueue u2 and check if it became the last item
    queue_->Enqueue(u2);
    EXPECT_EQ(queue_->size(), 3);
    last = queue_->GetLast();
    EXPECT_EQ(last->get_sequence(), u2->get_sequence());

    // Enqueue u3 and check if it became the last item
    queue_->Enqueue(u3);
    EXPECT_EQ(queue_->size(), 4);
    last = queue_->GetLast();
    EXPECT_EQ(last->get_sequence(), u3->get_sequence());

    queue_->Dequeue(u1);
    queue_->Dequeue(u2);
    queue_->Dequeue(u3);

    delete(u1);
    delete(u2);
    delete(u3);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    bool success = RUN_ALL_TESTS();
    TaskScheduler::GetInstance()->Terminate();
    return success;
}
