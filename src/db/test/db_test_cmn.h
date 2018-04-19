/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef db_test_cmn_h
#define db_test_cmn_h

#include <boost/assign/list_of.hpp>

#include "io/event_manager.h"
#include "base/task.h"
#include "base/task_annotations.h"
#include "base/test/task_test_util.h"

struct Client : public DBClient {
private:
};

struct VlanState : public DBState {
public:
    int count;
    VlanState(int n) : count(n) {}
};

class DBTest : public ::testing::Test {
protected:
    tbb::atomic<long> adc_notification;
    tbb::atomic<long> del_notification;
    tbb::atomic<long> add_notification_client1;
    tbb::atomic<long> add_notification_client2;
    tbb::atomic<long> del_notification_client1;
    tbb::atomic<long> del_notification_client2;
    tbb::atomic<long> walk_count_;
    tbb::atomic<bool> walk_done_;
    tbb::atomic<bool> notify_yield;
public:
    DBTest() : tid_(DBTableBase::kInvalidId), tid_1_(DBTableBase::kInvalidId) {
        itbl = static_cast<VlanTable *>(db_.CreateTable("db.test.vlan.0"));
        itbl_1 = static_cast<VlanTable *>(db1_.CreateTable("db.test.vlan.1"));
    }

    virtual void SetUp() {
        TaskScheduler *scheduler = TaskScheduler::GetInstance();
        TaskPolicy walker_policy = boost::assign::list_of
            (TaskExclusion(scheduler->GetTaskId("db::DBTable")));
        scheduler->SetPolicy(scheduler->GetTaskId("db::Walker"), walker_policy);
    }

    virtual void TearDown() {
    }

    void DBTestListener(DBTablePartBase *root, DBEntryBase *entry) {
        Vlan *vlan = static_cast<Vlan *>(entry);
        bool del_notify = vlan->IsDeleted();
        if (del_notify) {
            // Access vlan entry to make sure it is not deleted
            if (vlan->getTag() == 101) {
            } else {
            }
            del_notification++;
        } else {
            adc_notification++;
        }
    }

    bool TableWalk(DBTablePartBase *root, DBEntryBase *entry) {
        walk_count_++;
        return true;
    }

    void TWalkDone(DBTable::DBTableWalkRef walker, DBTableBase *tbl) {
        walk_done_ = true;
    }


    void DBTestListener_1(DBTablePartBase *root, DBEntryBase *entry) {
        Vlan *vlan = static_cast<Vlan *>(entry);
        bool del_notify = vlan->IsDeleted();
        if (del_notify) {
            vlan->ClearState(itbl, tid_1_);
        } else {
            VlanState *state = static_cast<VlanState *>
                (vlan->GetState(root->parent(), tid_1_));
            if (state) {
                // Change Notification
            } else {
                VlanState mystate(20);
                // Add notification
                vlan->SetState(root->parent(), tid_1_, &mystate);
            }
        }
    }

    void DBTestListener_2(DBTablePartBase *root, DBEntryBase *entry) {
        Vlan *vlan = static_cast<Vlan *>(entry);

        DBTestListener(root, entry);
        if (vlan->getTag() >= 2000) {
            return;
        }

        DBRequest dbReq;
        if (entry->IsDeleted()) {
            dbReq.key.reset(new VlanTableReqKey(2000 + vlan->getTag()));
            dbReq.oper = DBRequest::DB_ENTRY_DELETE;
        } else {
            dbReq.key.reset(new VlanTableReqKey(2000 + vlan->getTag()));
            dbReq.data.reset(new VlanTableReqData("DB Test Vlan"));
            dbReq.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
        }
        itbl->Enqueue(&dbReq);
    }

    void DBTestYieldListener1(DBTablePartBase *root, DBEntryBase *entry) {
        int diff = 0;
        if (entry->IsDeleted()) {
            del_notification_client1++;
            diff =
                std::abs(del_notification_client1 - del_notification_client2);
        } else {
            add_notification_client1++;
            diff =
                std::abs(add_notification_client1 - add_notification_client2);
        }
        if (diff > DBTablePartBase::kMaxIterations) {
            notify_yield = false;
        }
    }
    void DBTestYieldListener2(DBTablePartBase *root, DBEntryBase *entry) {
        int diff = 0;
        if (entry->IsDeleted()) {
            del_notification_client2++;
            diff =
                std::abs(del_notification_client1 - del_notification_client2);
        } else {
            add_notification_client2++;
            diff =
                std::abs(add_notification_client1 - add_notification_client2);
        }
        if (diff > DBTablePartBase::kMaxIterations) {
            notify_yield = false;
        }
    }

    DB db_;
    DB db1_;
    VlanTable *itbl;
    VlanTable *itbl_1;
    DBTableBase::ListenerId tid_;
    DBTableBase::ListenerId tid_1_;
};

// To Test:
// Check whether table Create and lookup is find
// Check the registration and De-registration of the listener
// Verify the hole-mgmt for Listener

TEST_F(DBTest, Basic) {
    EXPECT_EQ(itbl, db_.FindTable("db.test.vlan.0"));
    uint32_t retry_delete_count = itbl->retry_delete_count();

    tid_ =
        itbl->Register(boost::bind(&DBTest::DBTestListener, this, _1, _2));
    EXPECT_EQ(tid_, 0);

    itbl->Unregister(tid_);
    EXPECT_EQ(itbl->retry_delete_count(), retry_delete_count+1);
    retry_delete_count = itbl->retry_delete_count();

    for (int i = 0; i < 15; i++) {
        tid_ =
            itbl->Register(boost::bind(&DBTest::DBTestListener,
                        this, _1, _2));
        EXPECT_EQ(tid_, i);
    }

    itbl->Unregister(7);
    // There are pending registers. retry_delete_count is not incremented
    EXPECT_EQ(itbl->retry_delete_count(), retry_delete_count);

    tid_ =
        itbl->Register(boost::bind(&DBTest::DBTestListener, this, _1, _2));
    EXPECT_EQ(tid_, 7);


    for (int i = 14; i >=0; i--) {
        EXPECT_EQ(itbl->retry_delete_count(), retry_delete_count);
        itbl->Unregister(i);
    }

    // All clients are registered. retry_delete_count should be incremented
    EXPECT_EQ(itbl->retry_delete_count(), retry_delete_count+1);
    retry_delete_count = itbl->retry_delete_count();
}

// To Test:
// Basic Create and DELETE of DB entries
// Notification of add/delete entries
// Find API

TEST_F(DBTest, DBEntryBase) {
    // Register a client for notification
    tid_ =
        itbl->Register(boost::bind(&DBTest::DBTestListener, this, _1, _2));
    EXPECT_EQ(tid_, 0);

    // Clear stats in begin
    adc_notification = 0;
    del_notification = 0;

    // Create a VLAN
    DBRequest addReq;
    addReq.key.reset(new VlanTableReqKey(101));
    addReq.data.reset(new VlanTableReqData("DB Test Vlan"));
    addReq.oper = DBRequest::DB_ENTRY_ADD_CHANGE;

    itbl->Enqueue(&addReq);

    task_util::WaitForIdle();
    EXPECT_EQ(adc_notification, 1);

    VlanTableReqKey key(101);
    Vlan *vlan = itbl->Find(&key);
    EXPECT_TRUE(vlan != NULL);


    // Delete a VLAN
    DBRequest delReq;
    delReq.key.reset(new VlanTableReqKey(101));
    delReq.oper = DBRequest::DB_ENTRY_DELETE;

    itbl->Enqueue(&delReq);

    task_util::WaitForIdle();
    EXPECT_EQ(del_notification, 1);

    vlan = itbl->Find(&key);
    EXPECT_TRUE(vlan == NULL);

    itbl->Unregister(tid_);

    // Clear in the End
    adc_notification = 0;
    del_notification = 0;
}

// To Test:
// Verify the ADD suppression logic
//     IF an entry is deleted before add notification to client, send only delete

TEST_F(DBTest, AddSuppression) {
    // Clear in begin
    adc_notification = 0;
    del_notification = 0;

    // Register a client for notification
    tid_ =
        itbl->Register(boost::bind(&DBTest::DBTestListener, this, _1, _2));
    EXPECT_EQ(tid_, 0);

    // Stop the scheduler
    TaskScheduler::GetInstance()->Stop();

    // Create a VLAN
    DBRequest addReq;
    addReq.key.reset(new VlanTableReqKey(101));
    addReq.data.reset(new VlanTableReqData("DB Test Vlan"));
    addReq.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    itbl->Enqueue(&addReq);

    // Delete a VLAN
    DBRequest delReq;
    delReq.key.reset(new VlanTableReqKey(101));
    delReq.oper = DBRequest::DB_ENTRY_DELETE;
    itbl->Enqueue(&delReq);

    // Start the scheduler and wait for it be idle
    TaskScheduler::GetInstance()->Start();
    task_util::WaitForIdle();

    EXPECT_EQ(adc_notification, 0);
    EXPECT_EQ(del_notification, 1);
    VlanTableReqKey key(101);
    Vlan *vlan = itbl->Find(&key);
    EXPECT_TRUE(vlan == NULL);

    itbl->Unregister(tid_);

    // Clear stats in End
    adc_notification = 0;
    del_notification = 0;
}


// To Test:
// Verify that DBState is stored as per client
// Delete entry stays in DB till all DBState is removed
// Verify the DBentry is removed from the tree on last DBState removal
// Verify that DBEntry is not removed in notification path on removal of last DBState
TEST_F(DBTest, TestState) {
    // Clear stats in begin
    adc_notification = 0;
    del_notification = 0;

    // Register a client for notification
    tid_ =
        itbl->Register(boost::bind(&DBTest::DBTestListener, this, _1, _2));
    EXPECT_EQ(tid_, 0);

    // Create a VLAN
    DBRequest addReq;
    addReq.key.reset(new VlanTableReqKey(101));
    addReq.data.reset(new VlanTableReqData("DB Test Vlan"));
    addReq.oper = DBRequest::DB_ENTRY_ADD_CHANGE;

    itbl->Enqueue(&addReq);
    task_util::WaitForIdle();

    Vlan *vlan = NULL;
    VlanTableReqKey key(101);
    vlan = itbl->Find(&key);
    EXPECT_TRUE(vlan != NULL);

    VlanState mystate(5);
    vlan->SetState(itbl, tid_, &mystate);

    // Delete a VLAN
    DBRequest delReq;
    delReq.key.reset(new VlanTableReqKey(101));
    delReq.oper = DBRequest::DB_ENTRY_DELETE;

    itbl->Enqueue(&delReq);
    task_util::WaitForIdle();

    vlan = itbl->Find(&key);
    EXPECT_TRUE(vlan != NULL);
    EXPECT_TRUE(vlan->IsDeleted());

    VlanState *tmpState = static_cast<VlanState *>(vlan->GetState(itbl, tid_));
    EXPECT_EQ(tmpState, &mystate);

    vlan->ClearState(itbl, tid_);
    task_util::WaitForIdle();

    vlan = itbl->Find(&key);
    EXPECT_TRUE(vlan == NULL);

    itbl->Unregister(tid_);

    // Test set/delete of mydata in Notification
    tid_1_ =
        itbl->Register(boost::bind(&DBTest::DBTestListener_1, this, _1, _2));
    EXPECT_EQ(tid_1_, 0);

    tid_ =
        itbl->Register(boost::bind(&DBTest::DBTestListener, this, _1, _2));
    EXPECT_EQ(tid_, 1);

    addReq.key.reset(new VlanTableReqKey(202));
    addReq.data.reset(new VlanTableReqData("DB Test Vlan"));
    addReq.oper = DBRequest::DB_ENTRY_ADD_CHANGE;

    itbl->Enqueue(&addReq);
    task_util::WaitForIdle();

    VlanTableReqKey key_1(202);
    vlan = itbl->Find(&key_1);
    EXPECT_TRUE(vlan != NULL);

    DBRequest updReq;
    updReq.key.reset(new VlanTableReqKey(202));
    updReq.data.reset(new VlanTableReqData("Updated Description"));
    updReq.oper = DBRequest::DB_ENTRY_ADD_CHANGE;

    itbl->Enqueue(&updReq);
    task_util::WaitForIdle();

    vlan = itbl->Find(&key_1);
    EXPECT_TRUE(vlan != NULL);
    EXPECT_EQ(vlan->getDesc(), "Updated Description");

    // Delete a VLAN
    delReq.key.reset(new VlanTableReqKey(202));
    delReq.oper = DBRequest::DB_ENTRY_DELETE;

    itbl->Enqueue(&delReq);
    task_util::WaitForIdle();

    vlan = itbl->Find(&key_1);
    EXPECT_TRUE(vlan == NULL);

    // Clear stats in end
    adc_notification = 0;
    del_notification = 0;
}


// To Test:
// Delete entry stays in DB till all DBState is removed
// and on re-Add with same key it becomes active again.
// Verify that DBEntry is reused on Add after delete.
TEST_F(DBTest, DBentryReUse) {
    // Clear stats in begin
    adc_notification = 0;
    del_notification = 0;

    // Register a client for notification
    tid_ =
        itbl->Register(boost::bind(&DBTest::DBTestListener, this, _1, _2));
    EXPECT_EQ(tid_, 0);

    // Create a VLAN
    DBRequest addReq;
    addReq.key.reset(new VlanTableReqKey(101));
    addReq.data.reset(new VlanTableReqData("DB Test Vlan"));
    addReq.oper = DBRequest::DB_ENTRY_ADD_CHANGE;

    itbl->Enqueue(&addReq);
    task_util::WaitForIdle();

    Vlan *vlan = NULL;
    VlanTableReqKey key(101);
    vlan = itbl->Find(&key);
    EXPECT_TRUE(vlan != NULL);

    EXPECT_EQ(adc_notification, 1);

    VlanState mystate(5);
    vlan->SetState(itbl, tid_, &mystate);

    // Delete a VLAN
    DBRequest delReq;
    delReq.key.reset(new VlanTableReqKey(101));
    delReq.oper = DBRequest::DB_ENTRY_DELETE;

    itbl->Enqueue(&delReq);
    task_util::WaitForIdle();

    vlan = itbl->Find(&key);
    EXPECT_TRUE(vlan != NULL);
    EXPECT_TRUE(vlan->IsDeleted());

    EXPECT_EQ(del_notification, 1);

    Vlan *last_vlan = vlan;

    // Re-ADD the same entry again.
    addReq.key.reset(new VlanTableReqKey(101));
    addReq.data.reset(new VlanTableReqData("DB Test Vlan"));
    addReq.oper = DBRequest::DB_ENTRY_ADD_CHANGE;

    itbl->Enqueue(&addReq);
    task_util::WaitForIdle();
    task_util::WaitForIdle();

    vlan = itbl->Find(&key);
    EXPECT_TRUE(!vlan->IsDeleted());

    EXPECT_EQ(adc_notification, 2);

    VlanState *tmpState = static_cast<VlanState *>(vlan->GetState(itbl, tid_));
    EXPECT_EQ(tmpState, &mystate);
    EXPECT_EQ(last_vlan, vlan);

    delReq.key.reset(new VlanTableReqKey(101));
    delReq.oper = DBRequest::DB_ENTRY_DELETE;

    itbl->Enqueue(&delReq);
    task_util::WaitForIdle();

    vlan->ClearState(itbl, tid_);
    task_util::WaitForIdle();

    vlan = itbl->Find(&key);
    EXPECT_TRUE(vlan == NULL);

    EXPECT_EQ(del_notification, 2);

    itbl->Unregister(tid_);

    // Clear stats in end
    adc_notification = 0;
    del_notification = 0;
}


// To Test:
// Walker

TEST_F(DBTest, JWalker) {
    DBTable *table = dynamic_cast<DBTable *>(itbl);
    if (table == NULL) {
        return;
    }

    int walk_count = 2;
    // Clear stats in begin
    adc_notification = 0;
    del_notification = 0;

    // Register a client for notification
    tid_ =
        itbl->Register(boost::bind(&DBTest::DBTestListener, this, _1, _2));
    EXPECT_EQ(tid_, 0);


    for (int i = 0; i < walk_count; i++) {
        DBRequest addReq;
        addReq.key.reset(new VlanTableReqKey(i));
        addReq.data.reset(new VlanTableReqData("DB Test Vlan"));
        addReq.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
        itbl->Enqueue(&addReq);
    }

    LOG(DEBUG, "Verify Add notification");
    task_util::WaitForIdle();
    EXPECT_TRUE(adc_notification == walk_count);


    for (int i = 0; i<walk_count; i++) {
        VlanTableReqKey lookupKey(i);
        Vlan *vlan = itbl->Find(&lookupKey);
        EXPECT_TRUE(vlan != NULL);
    }

    adc_notification = 0;

    {
        ConcurrencyScope scope("bgp::Config");
        table->NotifyAllEntries();
    }
    task_util::WaitForIdle();

    EXPECT_TRUE(adc_notification == walk_count);

    walk_done_ = false;
    walk_count_ = 0;

    DBTable::DBTableWalkRef walk_ref = table->AllocWalker(
                              boost::bind(&DBTest::TableWalk, this, _1, _2),
                              boost::bind(&DBTest::TWalkDone, this, _1, _2));

    DBTable::DBTableWalkRef walk_ref_1 = table->AllocWalker(
                              boost::bind(&DBTest::TableWalk, this, _1, _2),
                              boost::bind(&DBTest::TWalkDone, this, _1, _2));

    DBTable::DBTableWalkRef walk_ref_2 = table->AllocWalker(
                              boost::bind(&DBTest::TableWalk, this, _1, _2),
                              boost::bind(&DBTest::TWalkDone, this, _1, _2));

    TaskScheduler::GetInstance()->Stop();
    table->WalkTable(walk_ref);
    table->WalkTable(walk_ref_1);
    table->WalkTable(walk_ref_2);

    LOG(DEBUG, "Verify Walk count and done for full table search");
    TaskScheduler::GetInstance()->Start();
    task_util::WaitForIdle();

    EXPECT_TRUE(walk_done_);

    EXPECT_EQ(walk_count_, 3*walk_count);

    walk_done_ = false;
    walk_count_ = 0;

    table->WalkTable(walk_ref);
    LOG(DEBUG, "Verify Walk count and done for walk on non existing entry");

    task_util::WaitForIdle();
    EXPECT_TRUE(walk_done_);
    EXPECT_EQ(walk_count_, walk_count);


    del_notification = 0;
    adc_notification = 0;
    // Delete Bulk VLANs
    for (int i = 0; i<walk_count; i++) {
        DBRequest delReq;
        delReq.key.reset(new VlanTableReqKey(i));
        delReq.oper = DBRequest::DB_ENTRY_DELETE;
        itbl->Enqueue(&delReq);
    }

    LOG(DEBUG, "Verify Delete notification");
    task_util::WaitForIdle();
    EXPECT_TRUE(del_notification == walk_count);
}

// To Test:
// Walker Stats
TEST_F(DBTest, WalkerStats) {
    DBTable *table = dynamic_cast<DBTable *>(itbl);
    if (table == NULL) {
        return;
    }

    walk_done_ = false;
    TaskScheduler::GetInstance()->Stop();

    DBTable::DBTableWalkRef walk_ref = table->AllocWalker(
                              boost::bind(&DBTest::TableWalk, this, _1, _2),
                              boost::bind(&DBTest::TWalkDone, this, _1, _2));
    table->WalkTable(walk_ref);

    TaskScheduler::GetInstance()->Start();
    task_util::WaitForIdle();

    LOG(DEBUG, "Verify Walk request and complete count");
    TASK_UTIL_EXPECT_TRUE(walk_done_);
    TASK_UTIL_EXPECT_EQ(1, table->walk_request_count());
    TASK_UTIL_EXPECT_EQ(1, table->walk_complete_count());
    TASK_UTIL_EXPECT_EQ(0, table->walk_cancel_count());

    walk_done_ = false;
    TaskScheduler::GetInstance()->Stop();
    table->WalkTable(walk_ref);
    table->ReleaseWalker(walk_ref);
    TaskScheduler::GetInstance()->Start();
    task_util::WaitForIdle();

    LOG(DEBUG, "Verify Walk request and cancel count");
    TASK_UTIL_EXPECT_FALSE(walk_done_);
    TASK_UTIL_EXPECT_EQ(2, table->walk_request_count());
    TASK_UTIL_EXPECT_EQ(1, table->walk_complete_count());
    TASK_UTIL_EXPECT_EQ(1, table->walk_cancel_count());
}

// To Test:
// Verify Bulk ADD DELETE of objects to DBTable
TEST_F(DBTest, Bulk) {
    int bulk_count = 10;
    // Register a client for notification
    tid_ =
        itbl->Register(boost::bind(&DBTest::DBTestListener, this, _1, _2));
    EXPECT_EQ(tid_, 0);

    // Clear stats in begin
    adc_notification = 0;
    del_notification = 0;
    // Create Bulk VLANs
    for (int i = 0; i<bulk_count; i++) {
        DBRequest addReq;
        addReq.key.reset(new VlanTableReqKey(i));
        addReq.data.reset(new VlanTableReqData("DB Test Vlan"));
        addReq.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
        itbl->Enqueue(&addReq);
    }

    LOG(DEBUG, "Verify Add notification");
    task_util::WaitForIdle();
    EXPECT_TRUE(adc_notification == bulk_count);

    LOG(DEBUG, "Verify Added entry from tree");
    for (int i = 0; i<bulk_count; i++) {
        VlanTableReqKey lookupKey(i);
        Vlan *vlan = itbl->Find(&lookupKey);
        EXPECT_TRUE(vlan != NULL);
    }
    del_notification = 0;
    adc_notification = 0;
    // Delete Bulk VLANs
    for (int i = 0; i<bulk_count; i++) {
        DBRequest delReq;
        delReq.key.reset(new VlanTableReqKey(i));
        delReq.oper = DBRequest::DB_ENTRY_DELETE;
        itbl->Enqueue(&delReq);
    }

    LOG(DEBUG, "Verify Delete notification");
    task_util::WaitForIdle();
    EXPECT_TRUE(del_notification == bulk_count);

    LOG(DEBUG, "Verify Whether all entries are gone");
    int failed = 0;
    for (int i = 0; i<bulk_count; i++) {
        VlanTableReqKey lookupKey(i);
        Vlan *vlan = itbl->Find(&lookupKey);
        EXPECT_TRUE(vlan == NULL);
        if (vlan != NULL) {
            LOG(DEBUG, "Not yet deleted = " << vlan->getTag());
            LOG(DEBUG, "Delete marked = " << vlan->IsDeleted());
            failed++;
        }
    }
    EXPECT_EQ(adc_notification, 0);
    LOG(DEBUG, "Total Failed cases = " << failed);
    itbl->Unregister(tid_);

    // Clear stats in end
    adc_notification = 0;
    del_notification = 0;
}

// To Test:
// Verify that requests enqueued when a notification running is serviced
TEST_F(DBTest, ReqInNotifyPath) {
    int req_count = 5;
    // Register a client for notification
    tid_ =
        itbl->Register(boost::bind(&DBTest::DBTestListener_2, this, _1, _2));
    EXPECT_EQ(tid_, 0);

    // Clear stats in begin
    adc_notification = 0;
    del_notification = 0;

    // Enqueue a request
    for (int i = 0; i<req_count; i++) {
        DBRequest addReq;
        addReq.key.reset(new VlanTableReqKey(i));
        addReq.data.reset(new VlanTableReqData("DB Test Vlan"));
        addReq.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
        itbl->Enqueue(&addReq);
    }

    LOG(DEBUG, "Verify Add notification");
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(adc_notification == req_count * 2);

    // Delete a VLAN
    // Enqueue a request
    for (int i = 0; i<req_count; i++) {
        DBRequest delReq;
        delReq.key.reset(new VlanTableReqKey(i));
        delReq.oper = DBRequest::DB_ENTRY_DELETE;

        itbl->Enqueue(&delReq);
    }
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(del_notification == req_count * 2);

    itbl->Unregister(tid_);

    // Clear stats in end
    adc_notification = 0;
    del_notification = 0;
}

TEST_F(DBTest, DB_RunNotify_Yield) {

    // Clear stats in begin
    add_notification_client1 = 0;
    add_notification_client2 = 0;
    del_notification_client1 = 0;
    del_notification_client2 = 0;
    notify_yield = true;

    // Register client1 for notification
    tid_ =
        itbl->Register(boost::bind(&DBTest::DBTestYieldListener1, this, _1, _2));
    EXPECT_EQ(tid_, 0);

    // Register client2 for notification
    tid_1_ =
        itbl_1->Register(boost::bind(&DBTest::DBTestYieldListener2, this, _1, _2));
    EXPECT_EQ(tid_1_, 0);

    int req_count = DBTablePartBase::kMaxIterations * 3;

    // Enqueue add requests to db.test.vlan.0
    TaskScheduler::GetInstance()->Stop();
    for (int i = 0; i<req_count; i++) {
        DBRequest addReq;
        // Ensure entries are in the same DB partition
        addReq.key.reset(new VlanTableReqKey(DB::PartitionCount() * i));
        addReq.data.reset(new VlanTableReqData("DB Test Vlan 0"));
        addReq.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
        itbl->Enqueue(&addReq);
    }
    // Enqueue add requests to db.test.vlan.1
    for (int i = 0; i<req_count; i++) {
        DBRequest addReq;
        // Ensure entries are in the same DB partition
        addReq.key.reset(new VlanTableReqKey(DB::PartitionCount() * i));
        addReq.data.reset(new VlanTableReqData("DB Test Vlan 1"));
        addReq.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
        itbl_1->Enqueue(&addReq);
    }
    TaskScheduler::GetInstance()->Start();

    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(req_count, add_notification_client1);
    TASK_UTIL_EXPECT_EQ(req_count, add_notification_client2);
    EXPECT_TRUE(notify_yield);

    // Enqueue delete requests to db.test.vlan.0
    TaskScheduler::GetInstance()->Stop();
    for (int i = 0; i<req_count; i++) {
        DBRequest addReq;
        // Ensure entries are in the same DB partition
        addReq.key.reset(new VlanTableReqKey(DB::PartitionCount() * i));
        addReq.oper = DBRequest::DB_ENTRY_DELETE;
        itbl->Enqueue(&addReq);
    }
    // Enqueue delete requests to db.test.vlan.1
    for (int i = 0; i<req_count; i++) {
        DBRequest addReq;
        // Ensure entries are in the same DB partition
        addReq.key.reset(new VlanTableReqKey(DB::PartitionCount() * i));
        addReq.oper = DBRequest::DB_ENTRY_DELETE;
        itbl_1->Enqueue(&addReq);
    }
    TaskScheduler::GetInstance()->Start();

    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(req_count, del_notification_client1);
    TASK_UTIL_EXPECT_EQ(req_count, del_notification_client2);
    EXPECT_TRUE(notify_yield);

    itbl->Unregister(tid_);
    itbl_1->Unregister(tid_1_);

    // Clear stats at end
    add_notification_client1 = 0;
    add_notification_client2 = 0;
    del_notification_client1 = 0;
    del_notification_client2 = 0;
}

#endif // db_test_cmn_h
