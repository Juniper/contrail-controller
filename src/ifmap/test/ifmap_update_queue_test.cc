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
        : server_(&db_, &graph_, evm_.io_service()), tbl_(NULL),
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
    queue_->Join(3);     // client 3
    queue_->Join(5);     // client 5

    // Add 4 update nodes
    queue_->Enqueue(u1);
    queue_->Enqueue(u2);
    queue_->Enqueue(u3);
    queue_->Enqueue(u4);
    EXPECT_TRUE(queue_->size() == 5); // 4 updates and 1 tail_marker

    // Insert marker for client 1 after u1
    marker = queue_->GetMarker(1);
    ASSERT_TRUE(marker != NULL);
    BitSet rem_bs;
    rem_bs.set(1);
    queue_->MarkerSplitAfter(marker, u1, rem_bs);
    EXPECT_TRUE(queue_->size() == 6); // 5 from before + 1 marker
    rem_bs.Reset(rem_bs);
    ASSERT_TRUE(rem_bs.empty());
 
    // Insert marker for client 3 after u3
    marker = queue_->GetMarker(3);
    ASSERT_TRUE(marker != NULL);
    rem_bs.set(3);
    queue_->MarkerSplitAfter(marker, u3, rem_bs);
    EXPECT_TRUE(queue_->size() == 7); // 6 from before + 1 marker
    rem_bs.Reset(rem_bs);
    ASSERT_TRUE(rem_bs.empty());

    // Move tail_marker_ beyond u4
    queue_->MoveMarkerAfter(queue_->tail_marker(), u4);
    EXPECT_TRUE(queue_->size() == 7); // no change from before

    // Should print 7 elements
    queue_->PrintQueue();

    // Merge markers for clients 3 and 1
    rem_bs.set(1);
    queue_->MarkerMerge(queue_->GetMarker(3), queue_->GetMarker(1), rem_bs);
    EXPECT_TRUE(queue_->size() == 6); // 7 from before, deleting 1
    rem_bs.Reset(rem_bs);
    ASSERT_TRUE(rem_bs.empty());

    // Merge markers for clients 1,3 and tail_marker_
    rem_bs.set(1);
    rem_bs.set(3);
    queue_->MarkerMerge(queue_->tail_marker(), queue_->GetMarker(3), rem_bs);
    EXPECT_TRUE(queue_->size() == 5); // 6 from before, deleting 1
    rem_bs.Reset(rem_bs);
    ASSERT_TRUE(rem_bs.empty());

    queue_->Dequeue(u1);
    queue_->Dequeue(u2);
    queue_->Dequeue(u3);
    queue_->Dequeue(u4);
    EXPECT_TRUE(queue_->size() == 1); // 4 updates and 1 tail_marker

    // Should print only the tail_marker_
    queue_->PrintQueue();

    queue_->Leave(1);     // client 1   
    queue_->Leave(3);     // client 3
    queue_->Leave(5);     // client 5

    delete(u1);
    delete(u2);
    delete(u3);
    delete(u4);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    bool success = RUN_ALL_TESTS();
    TaskScheduler::GetInstance()->Terminate();
    return success;
}
