/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <iostream>
#include <fstream>

#include "db/db.h"
#include "db/db_table.h"
#include "db/db_entry.h"
#include "db/db_client.h"
#include "db/db_partition.h"

#include "base/logging.h"
#include "testing/gunit.h"

#include "ksync/ksync_index.h"
#include "ksync/ksync_entry.h"
#include "ksync/ksync_object.h"

#include "base/test/task_test_util.h"
#include "io/event_manager.h"

using namespace std;
class VlanTable;

VlanTable *vlan_table_;

KSyncObjectManager *object_manager;

class Vlan : public KSyncEntry {
public:
    enum LastOp {
        INVALID,
        TEMP,
        INIT,
        ADD,
        CHANGE,
        DEL
    };

    Vlan(uint16_t tag, uint16_t dep_tag, size_t index) :
        KSyncEntry(index), tag_(tag), dep_tag_(dep_tag), dep_vlan_(NULL),
        op_(INIT), all_delete_state_comp_(true) { };

    Vlan(uint16_t tag) :
        KSyncEntry(), tag_(tag), dep_tag_(0), dep_vlan_(NULL), op_(TEMP),
        all_delete_state_comp_(true) { };

    Vlan(uint16_t tag, uint16_t dep_tag) :
        KSyncEntry(kInvalidIndex), tag_(tag), dep_tag_(dep_tag),
        dep_vlan_(NULL), op_(TEMP),
        all_delete_state_comp_(true) { };

    virtual ~Vlan() {
        if (GetState() == KSyncEntry::FREE_WAIT) {
            free_wait_count_++;
        }

        if (op_ == TEMP)
            return;

        assert((op_ == DEL) || (op_ == INIT));
    };

    std::string ToString() const {return "VLAN";};
    virtual bool IsLess(const KSyncEntry &rhs) const {
        const Vlan &vlan = static_cast<const Vlan &>(rhs);
        return tag_ < vlan.tag_;
    };

    virtual bool Add();
    virtual bool Change();

    virtual bool Delete() {
        op_ = DEL;
        delete_count_++;
        if (tag_ >= 0xF00 && tag_ < 0xFE0)
            return true;
        return false;
    };

    bool AllowDeleteStateComp() {return all_delete_state_comp_;}
    KSyncObject *GetObject() const;
    KSyncEntry *UnresolvedReference() {
        if (dep_tag_ == 0)
            return NULL;

        if (dep_vlan_->IsResolved())
            return NULL;

        return dep_vlan_.get();
    };

    uint16_t GetTag() const { return tag_;};
    uint16_t GetDepTag() const { return dep_tag_;};
    const Vlan *GetDepVlan() const { return static_cast<Vlan *>(dep_vlan_.get());};
    LastOp GetOp() const { return op_;};

    static uint32_t add_count_;
    static uint32_t delete_count_;
    static uint32_t change_count_;
    static uint32_t free_wait_count_;

    uint16_t tag_;
    uint16_t dep_tag_;
    KSyncEntryPtr dep_vlan_;
    LastOp op_;
    bool all_delete_state_comp_;
    DISALLOW_COPY_AND_ASSIGN(Vlan);
};
uint32_t Vlan::add_count_;
uint32_t Vlan::delete_count_;
uint32_t Vlan::change_count_;
uint32_t Vlan::free_wait_count_;

class VlanTable : public KSyncObject {
public:
    VlanTable(int max_index) : KSyncObject("Vlan KSync", max_index), is_empty_count_(0) {
        evm_.reset(new EventManager());
        // set timer value to -1, and trigger timer callback explicitly
        // set 1 entry processing per iteration
        InitStaleEntryCleanup(*(evm_->io_service()), -1, -1, 1);
    };
    ~VlanTable() {
        evm_->Shutdown();
    };

    virtual KSyncEntry *Alloc(const KSyncEntry *key, uint32_t index) {
        const Vlan *vlan  = static_cast<const Vlan *>(key);
        Vlan *v = new Vlan(vlan->GetTag(), vlan->GetDepTag(), index);
        if (vlan->GetDepTag() != 0) {
            Vlan key(vlan->GetDepTag());
            v->dep_vlan_ = static_cast<Vlan *>(vlan_table_->GetReference(&key));
        }

        return v;
    }

    virtual void EmptyTable(void) { is_empty_count_++; }

    int is_empty_count_;
    auto_ptr<EventManager> evm_;
    DISALLOW_COPY_AND_ASSIGN(VlanTable);
};

void TestInit() {
    Vlan::add_count_ = 0;
    Vlan::change_count_ = 0;
    Vlan::delete_count_ = 0;
    Vlan::free_wait_count_ = 0;
}

class TestUT : public ::testing::Test {
public:
    TestUT() { cout << "Creating TestTask" << endl; };
    void TestBody() {};
    virtual void SetUp() {
        TestInit();
    }

};

KSyncObject *Vlan::GetObject() const {
    return vlan_table_;
};

bool Vlan::Add() {
    op_ = ADD;
    add_count_++;

    if (tag_ >= 0xF00)
        return true;
    return false;
}

bool Vlan::Change() {
    op_ = CHANGE;
    change_count_++;

    if (dep_tag_ != 0) {
        Vlan key(dep_tag_);
        dep_vlan_ = static_cast<Vlan *>(vlan_table_->GetReference(&key));
    }

    if (tag_ >= 0xF00)
        return true;
    return false;
};

Vlan *AddStaleVlan(uint16_t tag, uint16_t dep_tag, KSyncEntry::KSyncState state,
                   Vlan::LastOp op, uint32_t index) {
    Vlan v(tag, dep_tag);
    Vlan *vlan = static_cast<Vlan *>(vlan_table_->CreateStale(&v));

    EXPECT_EQ(vlan->GetState(), state);
    EXPECT_EQ(vlan->GetOp(), op);
    EXPECT_EQ(vlan->GetIndex(), index);
    return vlan;
}

Vlan *AddVlan(uint16_t tag, uint16_t dep_tag, KSyncEntry::KSyncState state,
              Vlan::LastOp op, uint32_t index) {
    Vlan v(tag, dep_tag);
    Vlan *vlan = static_cast<Vlan *>(vlan_table_->Create(&v));

    EXPECT_EQ(vlan->GetState(), state);
    EXPECT_EQ(vlan->GetOp(), op);
    EXPECT_EQ(vlan->GetIndex(), index);
    return vlan;
}

void ChangeVlan(Vlan *vlan, uint16_t dep_tag, KSyncEntry::KSyncState state,
                Vlan::LastOp op) {

    Vlan v(dep_tag);
    vlan->dep_tag_ = dep_tag;
    if (vlan->dep_tag_ != 0) {
        vlan->dep_vlan_ = static_cast<Vlan *>(vlan_table_->GetReference(&v));
    } else {
        vlan->dep_vlan_ = NULL;
    }

    vlan_table_->Change(vlan);
    EXPECT_EQ(vlan->GetState(), state);
    EXPECT_EQ(vlan->GetOp(), op);
}

// SYNC Client without dependency
TEST_F(TestUT, sync_no_dep) {
    // Vlan entry with index 0
    Vlan *vlan1 = AddVlan(0xF01, 0, KSyncEntry::IN_SYNC, Vlan::ADD, 0);
    ChangeVlan(vlan1, 0, KSyncEntry::IN_SYNC, Vlan::CHANGE);

    // Vlan entry with index 1
    Vlan *vlan2 = AddVlan(0xF02, 0, KSyncEntry::IN_SYNC, Vlan::ADD, 1);
    ChangeVlan(vlan2, 0, KSyncEntry::IN_SYNC, Vlan::CHANGE);

    // Delete entry with index 0
    vlan_table_->Delete(vlan1);

    // New VLAN must re-use index 0
    Vlan *vlan3 = AddVlan(0xF03, 0, KSyncEntry::IN_SYNC, Vlan::ADD, 0);
    ChangeVlan(vlan3, 0, KSyncEntry::IN_SYNC, Vlan::CHANGE);

    // Delete an entry without change
    Vlan *vlan4 = AddVlan(0xF04, 0, KSyncEntry::IN_SYNC, Vlan::ADD, 2);
    vlan_table_->Delete(vlan4);

    vlan_table_->Delete(vlan2);
    vlan_table_->Delete(vlan3);

    EXPECT_EQ(Vlan::add_count_, 4U);
    EXPECT_EQ(Vlan::change_count_, 3U);
    EXPECT_EQ(Vlan::delete_count_, 4U);
}

// SYNC Client with dependency that is met
TEST_F(TestUT, sync_dep_1) {
    // Vlan entry with index 0
    Vlan *vlan1 = AddVlan(0xF01, 0, KSyncEntry::IN_SYNC, Vlan::ADD, 0);
    Vlan *vlan2 = AddVlan(0xF02, 0xF01, KSyncEntry::IN_SYNC, Vlan::ADD, 1);
    EXPECT_EQ(0xF01, vlan2->GetDepVlan()->GetTag());

    Vlan *vlan3 = AddVlan(0xF03, 0, KSyncEntry::IN_SYNC, Vlan::ADD, 2);
    ChangeVlan(vlan2, 0xF03, KSyncEntry::IN_SYNC, Vlan::CHANGE);
    EXPECT_EQ(0xF03, vlan2->GetDepVlan()->GetTag());

    vlan_table_->Delete(vlan1);
    vlan_table_->Delete(vlan2);
    vlan_table_->Delete(vlan3);

    EXPECT_EQ(Vlan::add_count_, 3U);
    EXPECT_EQ(Vlan::change_count_, 1U);
    EXPECT_EQ(Vlan::delete_count_, 3U);
}

// SYNC Client with dependency that is not met
TEST_F(TestUT, sync_dep_2) {
    Vlan *vlan1 = AddVlan(0xF01, 0xF02, KSyncEntry::ADD_DEFER, Vlan::INIT, 0);
    EXPECT_EQ(Vlan::add_count_, 0U);
    vlan_table_->Delete(vlan1);
    EXPECT_EQ(Vlan::delete_count_, 0U);

    TestInit();
    vlan1 = AddVlan(0xF01, 0xF02, KSyncEntry::ADD_DEFER, Vlan::INIT, 0);
    EXPECT_EQ(Vlan::add_count_, 0U);
    Vlan *vlan2 = AddVlan(0xF02, 0, KSyncEntry::IN_SYNC, Vlan::ADD, 1);
    EXPECT_EQ(Vlan::add_count_, 2U);
    EXPECT_EQ(vlan1->GetState(), KSyncEntry::IN_SYNC);
    EXPECT_EQ(vlan1->GetOp(), Vlan::ADD);

    vlan_table_->Delete(vlan1);
    vlan_table_->Delete(vlan2);
}

// SYNC Client with dependency. vlan2 refers to vlan1.
// vlan1 cannot be deleted till vlan2 is deleted
TEST_F(TestUT, sync_dep_del_order_1) {
    Vlan *vlan1 = AddVlan(0xF01, 0, KSyncEntry::IN_SYNC, Vlan::ADD, 0);
    Vlan *vlan2 = AddVlan(0xF02, 0xF01, KSyncEntry::IN_SYNC, Vlan::ADD, 1);
    EXPECT_EQ(0xF01, vlan2->GetDepVlan()->GetTag());

    vlan_table_->Delete(vlan1);
    // Vlan1 not deleted since vlan1 is still referring to it
    EXPECT_EQ(Vlan::delete_count_, 0U);
    EXPECT_EQ(vlan1->GetState(), KSyncEntry::DEL_DEFER_REF);
    vlan_table_->Delete(vlan2);
    // No wboth vlan1 and vlan2 are deleted
    EXPECT_EQ(Vlan::delete_count_, 2U);

    vlan1 = AddVlan(0xF01, 0, KSyncEntry::IN_SYNC, Vlan::ADD, 0);
    vlan2 = AddVlan(0xF02, 0xF01, KSyncEntry::IN_SYNC, Vlan::ADD, 1);
    EXPECT_EQ(0xF01, vlan2->GetDepVlan()->GetTag());

    vlan_table_->Delete(vlan2);
    EXPECT_EQ(Vlan::delete_count_, 3U);
    vlan_table_->Delete(vlan1);

    EXPECT_EQ(Vlan::add_count_, 4U);
    EXPECT_EQ(Vlan::change_count_, 0U);
    EXPECT_EQ(Vlan::delete_count_, 4U);
}

// SYNC Client with dependency.
// vlan4 refers to vlan1
// vlan3 refers to vlan2
// vlan2 refers to vlan1
// vlan1 does not refer to any vlan
TEST_F(TestUT, sync_dep_del_order_2) {
    Vlan *vlan1 = AddVlan(0xF01, 0, KSyncEntry::IN_SYNC, Vlan::ADD, 0);
    Vlan *vlan2 = AddVlan(0xF02, 0xF01, KSyncEntry::IN_SYNC, Vlan::ADD, 1);
    Vlan *vlan3 = AddVlan(0xF03, 0xF02, KSyncEntry::IN_SYNC, Vlan::ADD, 2);
    Vlan *vlan4 = AddVlan(0xF04, 0xF01, KSyncEntry::IN_SYNC, Vlan::ADD, 3);
    EXPECT_EQ(0xF01, vlan2->GetDepVlan()->GetTag());

    vlan_table_->Delete(vlan1);
    vlan_table_->Delete(vlan2);
    // Both vlan2 and vlan1 not deleted
    EXPECT_EQ(Vlan::delete_count_, 0U);
    EXPECT_EQ(vlan1->GetState(), KSyncEntry::DEL_DEFER_REF);
    EXPECT_EQ(vlan2->GetState(), KSyncEntry::DEL_DEFER_REF);

    vlan_table_->Delete(vlan3);
    EXPECT_EQ(Vlan::delete_count_, 2U);
    EXPECT_EQ(vlan1->GetState(), KSyncEntry::DEL_DEFER_REF);
    vlan_table_->Delete(vlan4);
    EXPECT_EQ(Vlan::delete_count_, 4U);

    EXPECT_EQ(Vlan::add_count_, 4U);
    EXPECT_EQ(Vlan::change_count_, 0U);
    EXPECT_EQ(Vlan::delete_count_, 4U);
}

// Basic ASYNC test
TEST_F(TestUT, async_basic) {
    Vlan *vlan1 = AddVlan(0x1, 0, KSyncEntry::SYNC_WAIT, Vlan::ADD, 0);
    vlan_table_->NotifyEvent(vlan1, KSyncEntry::ADD_ACK);
    EXPECT_EQ(vlan1->GetState(), KSyncEntry::IN_SYNC);

    vlan_table_->Change(vlan1);
    EXPECT_EQ(vlan1->GetState(), KSyncEntry::SYNC_WAIT);
    EXPECT_EQ(vlan1->GetOp(), Vlan::CHANGE);
    vlan_table_->NotifyEvent(vlan1, KSyncEntry::CHANGE_ACK);
    EXPECT_EQ(vlan1->GetOp(), Vlan::CHANGE);
    EXPECT_EQ(vlan1->GetState(), KSyncEntry::IN_SYNC);

    vlan_table_->Delete(vlan1);
    EXPECT_EQ(vlan1->GetState(), KSyncEntry::DEL_ACK_WAIT);
    EXPECT_EQ(vlan1->GetOp(), Vlan::DEL);
    vlan_table_->NetlinkAck(vlan1, KSyncEntry::DEL_ACK);
    EXPECT_EQ(Vlan::delete_count_, 1U);
}

// ASYNC Change when ADD and CHANGE ACK is pending
TEST_F(TestUT, async_change) {
    Vlan *vlan1 = AddVlan(0x1, 0, KSyncEntry::SYNC_WAIT, Vlan::ADD, 0);

    vlan_table_->Change(vlan1);
    EXPECT_EQ(vlan1->GetState(), KSyncEntry::NEED_SYNC);
    EXPECT_EQ(Vlan::add_count_, 1U);
    EXPECT_EQ(Vlan::change_count_, 0U);

    vlan_table_->NotifyEvent(vlan1, KSyncEntry::ADD_ACK);
    EXPECT_EQ(vlan1->GetState(), KSyncEntry::SYNC_WAIT);
    EXPECT_EQ(Vlan::add_count_, 1U);
    EXPECT_EQ(Vlan::change_count_, 1U);
    vlan_table_->NotifyEvent(vlan1, KSyncEntry::CHANGE_ACK);
    EXPECT_EQ(vlan1->GetState(), KSyncEntry::IN_SYNC);

    vlan_table_->Change(vlan1);
    EXPECT_EQ(vlan1->GetState(), KSyncEntry::SYNC_WAIT);
    EXPECT_EQ(Vlan::change_count_, 2U);
    vlan_table_->Change(vlan1);
    EXPECT_EQ(vlan1->GetState(), KSyncEntry::NEED_SYNC);
    vlan_table_->NotifyEvent(vlan1, KSyncEntry::CHANGE_ACK);
    EXPECT_EQ(Vlan::change_count_, 3U);
    EXPECT_EQ(vlan1->GetState(), KSyncEntry::SYNC_WAIT);
    vlan_table_->NotifyEvent(vlan1, KSyncEntry::CHANGE_ACK);
    EXPECT_EQ(vlan1->GetState(), KSyncEntry::IN_SYNC);
    EXPECT_EQ(Vlan::change_count_, 3U);

    vlan_table_->Delete(vlan1);
    EXPECT_EQ(vlan1->GetState(), KSyncEntry::DEL_ACK_WAIT);
    EXPECT_EQ(vlan1->GetOp(), Vlan::DEL);
    EXPECT_EQ(Vlan::delete_count_, 1U);
    vlan_table_->NetlinkAck(vlan1, KSyncEntry::DEL_ACK);
    EXPECT_EQ(Vlan::free_wait_count_, 1U);
}

//IN_SYNC->DEL_DEFER->FREE_WAIT
TEST_F(TestUT, del_defer) {
    Vlan *vlan1;
    Vlan *vlan2;
    Vlan *vlan3;
    Vlan *vlan4;

    vlan1 = AddVlan(0xF01, 0xF02, KSyncEntry::ADD_DEFER, Vlan::INIT, 0);
    vlan3 = AddVlan(0xF03, 0xF02, KSyncEntry::ADD_DEFER, Vlan::INIT, 2);
    vlan4 = AddVlan(0xF04, 0xF02, KSyncEntry::ADD_DEFER, Vlan::INIT, 3);
    EXPECT_EQ(Vlan::add_count_, 0U);
    EXPECT_EQ(vlan1->GetRefCount(), 2U);
    vlan2 = AddVlan(0xF02, 0, KSyncEntry::IN_SYNC, Vlan::ADD, 1);
    EXPECT_EQ(vlan1->GetRefCount(), 1U);
    EXPECT_EQ(vlan2->GetRefCount(), 4U);
    EXPECT_EQ(Vlan::add_count_, 4U);
    EXPECT_EQ(vlan1->GetState(), KSyncEntry::IN_SYNC);
    EXPECT_EQ(vlan3->GetState(), KSyncEntry::IN_SYNC);
    EXPECT_EQ(vlan4->GetState(), KSyncEntry::IN_SYNC);
    vlan_table_->Delete(vlan2);
    EXPECT_EQ(Vlan::delete_count_, 0U);
    EXPECT_EQ(vlan2->GetState(), KSyncEntry::DEL_DEFER_REF);
    vlan_table_->Delete(vlan1);
    EXPECT_EQ(Vlan::free_wait_count_, 1U);
    vlan_table_->Delete(vlan3);
    EXPECT_EQ(Vlan::free_wait_count_, 2U);
    EXPECT_EQ(Vlan::delete_count_, 2U);
    vlan_table_->Delete(vlan4);
    EXPECT_EQ(Vlan::free_wait_count_, 4U);
    EXPECT_EQ(Vlan::delete_count_, 4U);
}

// When change is invoked on an object in ADD_DEFER state,
// its dependency constraints should be recalculated
// ADD_DEFER->ADD_DEFER->IN_SYNC
TEST_F(TestUT, double_add_defer) {
    Vlan *vlan1;
    Vlan *vlan2;
    Vlan *vlan3;

    vlan1 = AddVlan(0xF01, 0xF02, KSyncEntry::ADD_DEFER, Vlan::INIT, 0);
    EXPECT_EQ(Vlan::add_count_, 0U);
    EXPECT_EQ(vlan1->GetRefCount(), 2U);
    ChangeVlan(vlan1, 0xF03, KSyncEntry::ADD_DEFER, Vlan::INIT);
    EXPECT_EQ(0xF03, vlan1->GetDepVlan()->GetTag());

    vlan2 = AddVlan(0xF02, 0, KSyncEntry::IN_SYNC, Vlan::ADD, 1);
    EXPECT_EQ(vlan1->GetState(), KSyncEntry::ADD_DEFER);
    EXPECT_EQ(vlan2->GetRefCount(), 1U);
    vlan3 = AddVlan(0xF03, 0, KSyncEntry::IN_SYNC, Vlan::ADD, 2);
    EXPECT_EQ(vlan3->GetRefCount(), 2U);
    EXPECT_EQ(vlan1->GetState(), KSyncEntry::IN_SYNC);
    vlan_table_->Delete(vlan1);
    vlan_table_->Delete(vlan2);
    vlan_table_->Delete(vlan3);
}

// When change is invoked on an object in ADD_DEFER state,
// its dependency constraints should be recalculated
// ADD_DEFER->IN_SYNC
TEST_F(TestUT, add_defer_dep_reeval) {
    Vlan *vlan1;
    Vlan *vlan3;

    vlan1 = AddVlan(0xF01, 0xF02, KSyncEntry::ADD_DEFER, Vlan::INIT, 0);
    EXPECT_EQ(Vlan::add_count_, 0U);
    EXPECT_EQ(vlan1->GetRefCount(), 2U);

    vlan3 = AddVlan(0xF03, 0, KSyncEntry::IN_SYNC, Vlan::ADD, 2);
    Vlan::free_wait_count_ = 0;
    ChangeVlan(vlan1, 0xF03, KSyncEntry::IN_SYNC, Vlan::ADD);
    EXPECT_EQ(vlan3->GetRefCount(), 2U);
    EXPECT_EQ(vlan1->GetDepTag(), 0xF03);
    //Temp Object corresponding to 0XF02 should be destroyed
    //Verify this by using free_wait_count_
    EXPECT_EQ(Vlan::delete_count_, 0U);
    EXPECT_EQ(Vlan::free_wait_count_, 1U);
    vlan_table_->Delete(vlan1);
    vlan_table_->Delete(vlan3);
}


// ADD_DEFER->del_req->DEL_DEFER
TEST_F(TestUT, add_defer_to_del_defer) {
    Vlan *vlan1;
    Vlan *vlan2;

    vlan2 = AddVlan(0xF02, 0xF03, KSyncEntry::ADD_DEFER, Vlan::INIT, 0);
    vlan1 = AddVlan(0xF01, 0xF02, KSyncEntry::ADD_DEFER, Vlan::INIT, 2);
    EXPECT_EQ(vlan2->GetRefCount(), 4U);
    vlan_table_->Delete(vlan2);
    EXPECT_EQ(vlan2->GetState(), KSyncEntry::TEMP);
    EXPECT_EQ(Vlan::delete_count_, 0U);
    EXPECT_EQ(Vlan::add_count_, 0U);
    EXPECT_EQ(vlan1->GetRefCount(), 2U);
    vlan_table_->Delete(vlan1);
    EXPECT_EQ(Vlan::delete_count_, 0U);
    EXPECT_EQ(Vlan::free_wait_count_, 3U);
}

//IN_SYNC->CHANGE_DEFER->IN_SYNC
TEST_F(TestUT, change_defer1) {
    Vlan *vlan1;
    Vlan *vlan2;

    vlan1 = AddVlan(0xF01, 0, KSyncEntry::IN_SYNC, Vlan::ADD, 0);
    ChangeVlan(vlan1, 0xF02, KSyncEntry::CHANGE_DEFER, Vlan::ADD);
    EXPECT_EQ(Vlan::add_count_, 1U);
    vlan2 = AddVlan(0xF02, 0, KSyncEntry::IN_SYNC, Vlan::ADD, 1);
    EXPECT_EQ(vlan1->GetState(), KSyncEntry::IN_SYNC);
    EXPECT_EQ(Vlan::add_count_, 2U);
    EXPECT_EQ(vlan2->GetRefCount(), 2U);
    vlan_table_->Delete(vlan1);
    vlan_table_->Delete(vlan2);
    EXPECT_EQ(Vlan::delete_count_, 2U);
}

//IN_SYNC->CHANGE_DEFER->FREE_WAIT
TEST_F(TestUT, change_defer2) {
    Vlan *vlan1;

    vlan1 = AddVlan(0xF01, 0, KSyncEntry::IN_SYNC, Vlan::ADD, 0);
    ChangeVlan(vlan1, 0xF02, KSyncEntry::CHANGE_DEFER, Vlan::ADD);
    EXPECT_EQ(vlan1->GetRefCount(), 2U);
    vlan_table_->Delete(vlan1);
    EXPECT_EQ(Vlan::delete_count_, 1U);
    EXPECT_EQ(Vlan::free_wait_count_, 2U);
}

//IN_SYNC->CHANGE_DEFER->DEL_ACK_WAIT->FREE_WAIT
TEST_F(TestUT, async_change_defer) {
    Vlan *vlan1;

    vlan1 = AddVlan(0xFE1, 0, KSyncEntry::IN_SYNC, Vlan::ADD, 0);
    ChangeVlan(vlan1, 0xFE2, KSyncEntry::CHANGE_DEFER, Vlan::ADD);
    EXPECT_EQ(vlan1->GetRefCount(), 2U);
    Vlan *vlan2 = static_cast<Vlan *>(vlan1->dep_vlan_.get());
    vlan2->GetState();
    vlan_table_->Delete(vlan1);
    EXPECT_EQ(vlan1->GetState(), KSyncEntry::DEL_ACK_WAIT);
    EXPECT_EQ(Vlan::delete_count_, 1U);
    vlan_table_->NetlinkAck(vlan1, KSyncEntry::DEL_ACK);
    EXPECT_EQ(Vlan::free_wait_count_, 2U);
}

//IN_SYNC->CHANGE_DEFER->CHANGE_DEFER->IN_SYNC
TEST_F(TestUT, double_change_defer) {
    Vlan *vlan1;
    Vlan *vlan3;

    vlan1 = AddVlan(0xFE1, 0, KSyncEntry::IN_SYNC, Vlan::ADD, 0);
    EXPECT_EQ(vlan1->GetRefCount(), 1U);
    ChangeVlan(vlan1, 0xFE2, KSyncEntry::CHANGE_DEFER, Vlan::ADD);
    EXPECT_EQ(vlan1->GetRefCount(), 2U);
    EXPECT_EQ(Vlan::add_count_, 1U);
    ChangeVlan(vlan1, 0xF05, KSyncEntry::CHANGE_DEFER, Vlan::ADD);
    EXPECT_EQ(Vlan::add_count_, 1U);
    EXPECT_EQ(Vlan::free_wait_count_, 1U);
    vlan3 = AddVlan(0xF05, 0, KSyncEntry::IN_SYNC, Vlan::ADD, 2);
    EXPECT_EQ(vlan1->GetState(), KSyncEntry::IN_SYNC);
    EXPECT_EQ(vlan1->GetRefCount(), 1U);
    EXPECT_EQ(vlan3->GetRefCount(), 2U);
    EXPECT_EQ(vlan1->Seen(), true);
    EXPECT_EQ(vlan3->Seen(), true);
    vlan_table_->Delete(vlan1);
    EXPECT_EQ(vlan1->GetState(), KSyncEntry::DEL_ACK_WAIT);
    EXPECT_EQ(Vlan::delete_count_, 1U);
    vlan_table_->Delete(vlan3);
    EXPECT_EQ(vlan3->GetState(), KSyncEntry::DEL_DEFER_REF);
    vlan_table_->NetlinkAck(vlan1, KSyncEntry::DEL_ACK);
    EXPECT_EQ(Vlan::delete_count_, 2U);
    EXPECT_EQ(Vlan::free_wait_count_, 3U);
}

// ADD_DEFER->del_req->TEMP->IN_SYNC
TEST_F(TestUT, add_defer_to_temp) {
    Vlan *vlan1;
    Vlan *vlan2;
    Vlan *vlan4;

    vlan2 = AddVlan(0xF02, 0xF03, KSyncEntry::ADD_DEFER, Vlan::INIT, 0);
    vlan1 = AddVlan(0xF01, 0xF02, KSyncEntry::ADD_DEFER, Vlan::INIT, 2);
    EXPECT_EQ(vlan2->GetRefCount(), 4U);
    EXPECT_EQ(vlan1->GetRefCount(), 2U);
    vlan_table_->Delete(vlan2);
    EXPECT_EQ(vlan2->GetState(), KSyncEntry::TEMP);
    // The object pointed to by 0xF03 (in TEMP state) will not be
    // destroyed/removed even though delete is invoked on vlan2
    // because vlan2 is just moved to TEMP state and it still holds
    // pointer to 0xF03
    EXPECT_EQ(Vlan::free_wait_count_, 0U);
    EXPECT_EQ(Vlan::delete_count_, 0U);
    EXPECT_EQ(Vlan::add_count_, 0U);
    vlan4 = AddVlan(0xF04, 0, KSyncEntry::IN_SYNC, Vlan::ADD, 3);
    EXPECT_EQ(Vlan::add_count_, 1U);
    ChangeVlan(vlan2, 0xF04, KSyncEntry::IN_SYNC, Vlan::ADD);
    EXPECT_EQ(Vlan::add_count_, 3U);
    EXPECT_EQ(Vlan::free_wait_count_, 1U);
    vlan_table_->Delete(vlan1);
    vlan_table_->Delete(vlan2);
    vlan_table_->Delete(vlan4);
    EXPECT_EQ(Vlan::delete_count_, 3U);
    EXPECT_EQ(Vlan::free_wait_count_, 4U);
}

// Verfies RefCount for an object in TEMP state
TEST_F(TestUT, refcount_verify_for_temp) {
    Vlan *vlan1;
    Vlan *vlan2;
    Vlan *vlan3;

    vlan2 = AddVlan(0xF02, 0xF03, KSyncEntry::ADD_DEFER, Vlan::INIT, 0);
    vlan1 = AddVlan(0xF01, 0xF02, KSyncEntry::ADD_DEFER, Vlan::INIT, 2);
    EXPECT_EQ(vlan2->GetRefCount(), 4U);
    EXPECT_EQ(vlan1->GetRefCount(), 2U);
    vlan_table_->Delete(vlan2);
    EXPECT_EQ(vlan2->GetState(), KSyncEntry::TEMP);
    EXPECT_EQ(vlan2->GetRefCount(), 3U);
    EXPECT_EQ(Vlan::free_wait_count_, 0U);
    vlan3 = AddVlan(0xF03, 0, KSyncEntry::IN_SYNC, Vlan::ADD, 1);
    EXPECT_EQ(vlan3->GetRefCount(), 2U);
    vlan_table_->Delete(vlan1);
    EXPECT_EQ(vlan3->GetRefCount(), 1U);
    EXPECT_EQ(Vlan::free_wait_count_, 2U);
    vlan_table_->Delete(vlan3);
}

// (1)SYNC_WAIT->NEED_SYNC->NEED_SYNC->SYNC_WAIT
// (2)SYNC_WAIT->NEED_SYNC->CHANGE_DEFER->SYNC_WAIT
// (3)SYNC_WAIT->NEED_SYNC->CHANGE_DEFER->SYNC_WAIT
TEST_F(TestUT, need_sync_to_change_defer) {
    Vlan *vlan1 = AddVlan(0x1, 0, KSyncEntry::SYNC_WAIT, Vlan::ADD, 0);

    vlan_table_->Change(vlan1);
    EXPECT_EQ(vlan1->GetState(), KSyncEntry::NEED_SYNC);
    EXPECT_EQ(Vlan::add_count_, 1U);
    EXPECT_EQ(Vlan::change_count_, 0U);
    ChangeVlan(vlan1, 0x2, KSyncEntry::NEED_SYNC, Vlan::ADD);
    Vlan *vlan2 = AddVlan(0x2, 0, KSyncEntry::SYNC_WAIT, Vlan::ADD, 1);
    EXPECT_EQ(vlan1->GetState(), KSyncEntry::NEED_SYNC);
    EXPECT_EQ(vlan2->GetRefCount(), 2U);
    vlan_table_->NotifyEvent(vlan1, KSyncEntry::ADD_ACK);
    EXPECT_EQ(vlan1->GetState(), KSyncEntry::SYNC_WAIT);
    EXPECT_EQ(vlan1->GetDepTag(), 0x2);
    ChangeVlan(vlan1, 0x3, KSyncEntry::NEED_SYNC, Vlan::CHANGE);
    vlan_table_->NotifyEvent(vlan1, KSyncEntry::CHANGE_ACK);
    EXPECT_EQ(vlan1->GetState(), KSyncEntry::CHANGE_DEFER);
    EXPECT_EQ(vlan1->GetDepTag(), 0x3);
    Vlan *vlan3 = AddVlan(0x3, 0, KSyncEntry::SYNC_WAIT, Vlan::ADD, 2);
    EXPECT_EQ(vlan1->GetState(), KSyncEntry::SYNC_WAIT);
    EXPECT_EQ(vlan3->GetRefCount(), 2U);
    ChangeVlan(vlan1, 0x4, KSyncEntry::NEED_SYNC, Vlan::CHANGE);
    vlan_table_->Delete(vlan1);
    EXPECT_EQ(vlan1->GetState(), KSyncEntry::DEL_DEFER_SYNC);
    EXPECT_EQ(Vlan::delete_count_, 0U);
    vlan_table_->NotifyEvent(vlan1, KSyncEntry::CHANGE_ACK);
    EXPECT_EQ(Vlan::delete_count_, 1U);
    EXPECT_EQ(Vlan::free_wait_count_, 0U);
    EXPECT_EQ(vlan1->GetState(), KSyncEntry::DEL_ACK_WAIT);
    vlan_table_->NetlinkAck(vlan1, KSyncEntry::DEL_ACK);
    EXPECT_EQ(Vlan::free_wait_count_, 2U);
    vlan_table_->Delete(vlan2);
    vlan_table_->NotifyEvent(vlan2, KSyncEntry::ADD_ACK);
    vlan_table_->Delete(vlan3);
    vlan_table_->NotifyEvent(vlan3, KSyncEntry::ADD_ACK);
    vlan_table_->NetlinkAck(vlan2, KSyncEntry::DEL_ACK);
    vlan_table_->NetlinkAck(vlan3, KSyncEntry::DEL_ACK);
}

//sync_wait->(del_req)->del_defer->(add_ack)->del_ack_wait->(del_ack)->(verify delete count)
TEST_F(TestUT, async_delete) {
    Vlan *vlan1 = AddVlan(0x1, 0, KSyncEntry::SYNC_WAIT, Vlan::ADD, 0);
    vlan_table_->Delete(vlan1);
    EXPECT_EQ(vlan1->GetState(), KSyncEntry::DEL_DEFER_SYNC);
    vlan_table_->NotifyEvent(vlan1, KSyncEntry::ADD_ACK);
    EXPECT_EQ(vlan1->GetState(), KSyncEntry::DEL_ACK_WAIT);
    vlan_table_->NetlinkAck(vlan1, KSyncEntry::DEL_ACK);
    EXPECT_EQ(Vlan::delete_count_, 1U);
    EXPECT_EQ(Vlan::free_wait_count_, 1U);
}

//add_defer->(add dep obj)->sync_wait->(del dep obj)->dep_obj_del_defer
TEST_F(TestUT, async_add_defer) {
    Vlan *vlan1 = AddVlan(0x1, 0x2, KSyncEntry::ADD_DEFER, Vlan::INIT, 0);
    Vlan *vlan2 = AddVlan(0x2, 0, KSyncEntry::SYNC_WAIT, Vlan::ADD, 1);
    EXPECT_EQ(vlan2->GetRefCount(), 2U);
    EXPECT_EQ(vlan1->GetState(), KSyncEntry::SYNC_WAIT);
    vlan_table_->Delete(vlan2);
    EXPECT_EQ(vlan2->GetState(), KSyncEntry::DEL_DEFER_SYNC);
    EXPECT_EQ(vlan1->GetState(), KSyncEntry::SYNC_WAIT);
    vlan_table_->NotifyEvent(vlan2, KSyncEntry::ADD_ACK);
    EXPECT_EQ(vlan2->GetState(), KSyncEntry::DEL_DEFER_REF);
    vlan_table_->NotifyEvent(vlan1, KSyncEntry::ADD_ACK);
    EXPECT_EQ(vlan1->GetState(), KSyncEntry::IN_SYNC);
    vlan_table_->Delete(vlan1);
    EXPECT_EQ(vlan1->GetState(), KSyncEntry::DEL_ACK_WAIT);
    vlan_table_->NetlinkAck(vlan1, KSyncEntry::DEL_ACK);
    vlan_table_->NetlinkAck(vlan2, KSyncEntry::DEL_ACK);

    EXPECT_EQ(Vlan::delete_count_, 2U);
    EXPECT_EQ(Vlan::free_wait_count_, 2U);
}

//add->(del_req)->del->(Reference add)->(del_ack)->temp->(del_req)
TEST_F(TestUT, temp_del_req) {
    Vlan *vlan1 = AddVlan(0x1, 0x0, KSyncEntry::SYNC_WAIT, Vlan::ADD, 0);
    vlan_table_->NotifyEvent(vlan1, KSyncEntry::ADD_ACK);
    EXPECT_EQ(vlan1->GetState(), KSyncEntry::IN_SYNC);

    vlan_table_->Delete(vlan1);
    EXPECT_EQ(vlan1->GetState(), KSyncEntry::DEL_ACK_WAIT);
    EXPECT_EQ(vlan1->GetOp(), Vlan::DEL);

    //Add a reference
    Vlan *vlan2 = AddVlan(0x2, 0x1, KSyncEntry::ADD_DEFER, Vlan::INIT, 1);

    vlan_table_->NetlinkAck(vlan1, KSyncEntry::DEL_ACK);
    EXPECT_EQ(vlan1->GetState(), KSyncEntry::TEMP);

    vlan_table_->Delete(vlan1);
    EXPECT_EQ(vlan1->GetState(), KSyncEntry::TEMP);

    vlan_table_->Delete(vlan2);

    //Vlan 2 will be in add defer state, hence delete callback would not be called
    EXPECT_EQ(Vlan::delete_count_, 1U);
    EXPECT_EQ(Vlan::free_wait_count_, 2U);
}

//add->(del_req)->del->(Reference add)->(add_req)->(del_ack)->renew
TEST_F(TestUT, renew_dependency_reval) {
    Vlan *vlan1 = AddVlan(0x1, 0x0, KSyncEntry::SYNC_WAIT, Vlan::ADD, 0);
    vlan_table_->NotifyEvent(vlan1, KSyncEntry::ADD_ACK);
    EXPECT_EQ(vlan1->GetState(), KSyncEntry::IN_SYNC);

    vlan_table_->Delete(vlan1);
    EXPECT_EQ(vlan1->GetState(), KSyncEntry::DEL_ACK_WAIT);
    EXPECT_EQ(vlan1->GetOp(), Vlan::DEL);

    //Add a reference  to vlan1
    Vlan *vlan2 = AddVlan(0x2, 0x1, KSyncEntry::ADD_DEFER, Vlan::INIT, 1);

    //Request to add vlan1 again
    ChangeVlan(vlan1, 0x0, KSyncEntry::RENEW_WAIT, Vlan::DEL);
    vlan_table_->NetlinkAck(vlan1, KSyncEntry::DEL_ACK);
    EXPECT_EQ(vlan1->GetState(), KSyncEntry::SYNC_WAIT);

    //vlan2 should have been re-evaluated
    EXPECT_EQ(vlan2->GetState(), KSyncEntry::SYNC_WAIT);
    vlan_table_->NotifyEvent(vlan1, KSyncEntry::ADD_ACK);
    vlan_table_->NotifyEvent(vlan2, KSyncEntry::ADD_ACK);

    EXPECT_EQ(vlan1->GetState(), KSyncEntry::IN_SYNC);
    EXPECT_EQ(vlan2->GetState(), KSyncEntry::IN_SYNC);

    vlan_table_->Delete(vlan2);
    vlan_table_->Delete(vlan1);
    vlan_table_->NetlinkAck(vlan2, KSyncEntry::DEL_ACK);
    vlan_table_->NetlinkAck(vlan1, KSyncEntry::DEL_ACK);

    EXPECT_EQ(Vlan::add_count_, 3U);
    EXPECT_EQ(Vlan::delete_count_, 3U);
    EXPECT_EQ(Vlan::free_wait_count_, 2U);
}

TEST_F(TestUT, del_ack_to_renew_to_del_defer) {
    Vlan *vlan1 = AddVlan(0x1, 0x0, KSyncEntry::SYNC_WAIT, Vlan::ADD, 0);
    vlan_table_->NotifyEvent(vlan1, KSyncEntry::ADD_ACK);
    EXPECT_EQ(vlan1->GetState(), KSyncEntry::IN_SYNC);

    vlan_table_->Delete(vlan1);
    EXPECT_EQ(vlan1->GetState(), KSyncEntry::DEL_ACK_WAIT);
    EXPECT_EQ(vlan1->GetOp(), Vlan::DEL);

    //Request to add vlan1 again
    ChangeVlan(vlan1, 0x0, KSyncEntry::RENEW_WAIT, Vlan::DEL);

    vlan1->all_delete_state_comp_ = false;
    vlan_table_->Delete(vlan1);
    EXPECT_EQ(vlan1->GetState(), KSyncEntry::DEL_DEFER_DEL_ACK);
    EXPECT_EQ(vlan1->GetOp(), Vlan::DEL);

    vlan_table_->NetlinkAck(vlan1, KSyncEntry::DEL_ACK);

    if (Vlan::free_wait_count_ == 0) {
        EXPECT_EQ(vlan1->GetState(), KSyncEntry::DEL_ACK_WAIT);
        vlan_table_->NotifyEvent(vlan1, KSyncEntry::DEL_ACK);
    }

    EXPECT_EQ(Vlan::delete_count_, 2U);
    EXPECT_EQ(Vlan::free_wait_count_, 1U);
}

TEST_F(TestUT, del_ack_to_renew_to_del_ack) {
    Vlan *vlan1 = AddVlan(0x1, 0x0, KSyncEntry::SYNC_WAIT, Vlan::ADD, 0);
    vlan_table_->NotifyEvent(vlan1, KSyncEntry::ADD_ACK);
    EXPECT_EQ(vlan1->GetState(), KSyncEntry::IN_SYNC);

    vlan_table_->Delete(vlan1);
    EXPECT_EQ(vlan1->GetState(), KSyncEntry::DEL_ACK_WAIT);
    EXPECT_EQ(vlan1->GetOp(), Vlan::DEL);

    //Request to add vlan1 again
    ChangeVlan(vlan1, 0x0, KSyncEntry::RENEW_WAIT, Vlan::DEL);

    vlan_table_->Delete(vlan1);
    EXPECT_EQ(vlan1->GetState(), KSyncEntry::DEL_ACK_WAIT);
    EXPECT_EQ(vlan1->GetOp(), Vlan::DEL);

    vlan_table_->NetlinkAck(vlan1, KSyncEntry::DEL_ACK);

    EXPECT_EQ(Vlan::delete_count_, 1U);
    EXPECT_EQ(Vlan::free_wait_count_, 1U);
}

TEST_F(TestUT, add_defer_to_delete_ack) {
    Vlan *vlan1 = AddVlan(0x1, 0x2, KSyncEntry::ADD_DEFER, Vlan::INIT, 0);
    EXPECT_EQ(Vlan::add_count_, 0U);
    vlan1->all_delete_state_comp_ = false;

    vlan_table_->Delete(vlan1);
    if (Vlan::free_wait_count_ == 0) {
        EXPECT_EQ(vlan1->GetState(), KSyncEntry::DEL_ACK_WAIT);
        EXPECT_EQ(vlan1->GetOp(), Vlan::DEL);

        vlan_table_->NetlinkAck(vlan1, KSyncEntry::DEL_ACK);
    }

    EXPECT_EQ(Vlan::delete_count_, 1U);
}

TEST_F(TestUT, add_defer_to_del_ref_pending_to_add) {
    Vlan *vlan1 = AddVlan(0x1, 0x2, KSyncEntry::ADD_DEFER, Vlan::INIT, 0);
    EXPECT_EQ(Vlan::add_count_, 0U);
    vlan1->all_delete_state_comp_ = false;

    // Add Reference.
    KSyncEntry::KSyncEntryPtr vlan_ref = vlan1;
    vlan_table_->Delete(vlan1);

    EXPECT_EQ(vlan1->GetState(), KSyncEntry::DEL_DEFER_REF);
    // Request to add vlan1 again
    ChangeVlan(vlan1, 0x2, KSyncEntry::ADD_DEFER, Vlan::INIT);

    // Remove Reference.
    vlan_ref = NULL;
    vlan_table_->Delete(vlan1);
    if (Vlan::free_wait_count_ == 0) {
        EXPECT_EQ(vlan1->GetState(), KSyncEntry::DEL_ACK_WAIT);
        EXPECT_EQ(vlan1->GetOp(), Vlan::DEL);

        vlan_table_->NetlinkAck(vlan1, KSyncEntry::DEL_ACK);
    }
    EXPECT_EQ(Vlan::add_count_, 0U);
    EXPECT_EQ(Vlan::delete_count_, 1U);
}

TEST_F(TestUT, trigger_object_delete_on_empty_object) {
    vlan_table_->is_empty_count_ = 0;
    object_manager->Delete(vlan_table_);
    task_util::WaitForIdle();

    EXPECT_EQ(Vlan::add_count_, 0U);
    EXPECT_EQ(Vlan::delete_count_, 0U);

    EXPECT_TRUE(vlan_table_->delete_scheduled());
    EXPECT_EQ(vlan_table_->is_empty_count_, 1);

    // Delete previous object and create a new one.
    delete vlan_table_;
    vlan_table_ = new VlanTable(200);
}

TEST_F(TestUT, trigger_object_delete) {
    size_t count = 110;
    int first_tag = 0xF00;
    for (size_t i = 0; i < count; i++) {
        AddVlan((i+first_tag), 0, KSyncEntry::IN_SYNC, Vlan::ADD, i);
    }

    vlan_table_->is_empty_count_ = 0;
    object_manager->Delete(vlan_table_);
    task_util::WaitForIdle();

    EXPECT_EQ(Vlan::add_count_, count);
    EXPECT_EQ(Vlan::delete_count_, count);

    EXPECT_TRUE(vlan_table_->delete_scheduled());
    EXPECT_EQ(vlan_table_->is_empty_count_, 1);

    // Delete previous object and create a new one.
    delete vlan_table_;
    vlan_table_ = new VlanTable(200);
}

TEST_F(TestUT, trigger_object_delete_with_pending_ack) {
    size_t count = 110;
    int first_tag = 0xF00;
    for (size_t i = 0; i < count; i++) {
        AddVlan((i+first_tag), 0, KSyncEntry::IN_SYNC, Vlan::ADD, i);
    }

    Vlan *vlan1 = AddVlan(0x1, 0, KSyncEntry::SYNC_WAIT, Vlan::ADD, count);
    vlan_table_->NotifyEvent(vlan1, KSyncEntry::ADD_ACK);
    vlan_table_->Delete(vlan1);

    vlan_table_->is_empty_count_ = 0;
    object_manager->Delete(vlan_table_);
    task_util::WaitForIdle();

    EXPECT_EQ(Vlan::add_count_, count + 1);
    EXPECT_EQ(Vlan::delete_count_, count + 1);

    EXPECT_TRUE(vlan_table_->delete_scheduled());
    EXPECT_EQ(vlan_table_->is_empty_count_, 0);

    vlan_table_->NetlinkAck(vlan1, KSyncEntry::DEL_ACK);
    EXPECT_EQ(vlan_table_->is_empty_count_, 1);

    // Delete previous object and create a new one.
    delete vlan_table_;
    vlan_table_ = new VlanTable(200);
}

// create stale entry and clean up on timer trigger
TEST_F(TestUT, stale_entry_create_cleanup) {
    // Stale Vlan entry with index 0
    AddStaleVlan(0xF01, 0, KSyncEntry::IN_SYNC, Vlan::ADD, 0);

    // Stale Vlan entry with index 0
    AddStaleVlan(0xF02, 0, KSyncEntry::IN_SYNC, Vlan::ADD, 1);

    // Triggers delete of stale entry
    TestTriggerStaleEntryCleanupCb(vlan_table_);

    EXPECT_EQ(Vlan::add_count_, 2U);
    EXPECT_EQ(Vlan::change_count_, 0U);
    EXPECT_EQ(Vlan::delete_count_, 1U);

    // Triggers delete of stale entry
    TestTriggerStaleEntryCleanupCb(vlan_table_);

    EXPECT_EQ(Vlan::add_count_, 2U);
    EXPECT_EQ(Vlan::change_count_, 0U);
    EXPECT_EQ(Vlan::delete_count_, 2U);
}

// create stale entry and convert it to non-stale entry
// no clean up happens on timer trigger
TEST_F(TestUT, stale_entry_convert_to_non_stale) {
    // Stale Vlan entry with index 0
    AddStaleVlan(0xF01, 0, KSyncEntry::IN_SYNC, Vlan::ADD, 0);

    // convert to non stale entry
    Vlan *vlan1 = AddVlan(0xF01, 0, KSyncEntry::IN_SYNC, Vlan::CHANGE, 0);

    // Triggers delete of stale entry
    TestTriggerStaleEntryCleanupCb(vlan_table_);

    EXPECT_EQ(Vlan::add_count_, 1U);
    EXPECT_EQ(Vlan::change_count_, 1U);
    EXPECT_EQ(Vlan::delete_count_, 0U);

    // Delete entry with index 0
    vlan_table_->Delete(vlan1);

    EXPECT_EQ(Vlan::add_count_, 1U);
    EXPECT_EQ(Vlan::change_count_, 1U);
    EXPECT_EQ(Vlan::delete_count_, 1U);
}

TEST_F(TestUT, trigger_object_delete_with_stale_entry) {
    size_t count = 110;
    int first_tag = 0xF00;
    for (size_t i = 0; i < count; i++) {
        AddStaleVlan((i+first_tag), 0, KSyncEntry::IN_SYNC, Vlan::ADD, i);
    }

    vlan_table_->is_empty_count_ = 0;
    object_manager->Delete(vlan_table_);
    task_util::WaitForIdle();

    EXPECT_EQ(Vlan::add_count_, count);
    EXPECT_EQ(Vlan::delete_count_, count);

    EXPECT_TRUE(vlan_table_->delete_scheduled());
    EXPECT_EQ(vlan_table_->is_empty_count_, 1);

    // Delete previous object and create a new one.
    delete vlan_table_;
    vlan_table_ = new VlanTable(200);
}

TEST_F(TestUT, CreateStaleFailure) {
    // Vlan entry with index 0
    Vlan *vlan1 = AddVlan(0xF01, 0, KSyncEntry::IN_SYNC, Vlan::ADD, 0);

    // Since vlan is already created, stale creation should fail.
    Vlan stale_key(0xF01, 0);
    EXPECT_TRUE(vlan_table_->CreateStale(&stale_key) == NULL);

    // Delete entry with index 0
    vlan_table_->Delete(vlan1);
}

TEST_F(TestUT, DeleteAddEvent) {
    Vlan *vlan2 = AddVlan(0xF02, 0xF03, KSyncEntry::ADD_DEFER, Vlan::INIT, 0);
    Vlan *vlan1 = AddVlan(0xF01, 0xF02, KSyncEntry::ADD_DEFER, Vlan::INIT, 2);
    EXPECT_EQ(Vlan::add_count_, 0U);
    vlan_table_->Delete(vlan2);
    EXPECT_EQ(vlan2->GetState(), KSyncEntry::TEMP);

    // trigger DeleteAdd on Temp state
    vlan2->all_delete_state_comp_ = false;
    vlan_table_->NotifyEvent(vlan2, KSyncEntry::DEL_ADD_REQ);
    EXPECT_EQ(vlan2->GetState(), KSyncEntry::ADD_DEFER);
    EXPECT_EQ(Vlan::delete_count_, 0U);
    EXPECT_EQ(Vlan::add_count_, 0U);

    TestInit();
    // trigger DeleteAdd on AddDefer state
    vlan1->all_delete_state_comp_ = false;
    vlan_table_->NotifyEvent(vlan1, KSyncEntry::DEL_ADD_REQ);
    EXPECT_EQ(vlan1->GetState(), KSyncEntry::ADD_DEFER);
    EXPECT_EQ(Vlan::delete_count_, 1U);
    EXPECT_EQ(Vlan::add_count_, 0U);

    TestInit();
    // trigger DeleteAdd on SyncWait state
    Vlan *vlan3 = AddVlan(0x4, 0, KSyncEntry::SYNC_WAIT, Vlan::ADD, 3);
    vlan3->all_delete_state_comp_ = false;
    EXPECT_EQ(Vlan::add_count_, 1U);
    vlan_table_->NotifyEvent(vlan3, KSyncEntry::DEL_ADD_REQ);
    EXPECT_TRUE(vlan3->del_add_pending());
    vlan_table_->NotifyEvent(vlan3, KSyncEntry::ADD_ACK);
    EXPECT_FALSE(vlan3->del_add_pending());
    EXPECT_EQ(Vlan::delete_count_, 1U);
    EXPECT_EQ(vlan3->GetState(), KSyncEntry::RENEW_WAIT);
    vlan_table_->NotifyEvent(vlan3, KSyncEntry::DEL_ACK);
    EXPECT_EQ(Vlan::add_count_, 2U);
    vlan_table_->NotifyEvent(vlan3, KSyncEntry::ADD_ACK);
    EXPECT_EQ(vlan3->GetState(), KSyncEntry::IN_SYNC);

    TestInit();
    // trigger DeleteAdd on InSync state
    vlan_table_->NotifyEvent(vlan3, KSyncEntry::DEL_ADD_REQ);
    EXPECT_EQ(Vlan::delete_count_, 1U);
    EXPECT_EQ(vlan3->GetState(), KSyncEntry::RENEW_WAIT);
    vlan_table_->NotifyEvent(vlan3, KSyncEntry::DEL_ACK);
    EXPECT_EQ(Vlan::add_count_, 1U);
    EXPECT_EQ(vlan3->GetState(), KSyncEntry::SYNC_WAIT);

    TestInit();
    // trigger DeleteAdd on DelDeferSync state
    vlan_table_->Delete(vlan3);
    EXPECT_EQ(vlan3->GetState(), KSyncEntry::DEL_DEFER_SYNC);
    vlan_table_->NotifyEvent(vlan3, KSyncEntry::DEL_ADD_REQ);
    EXPECT_TRUE(vlan3->del_add_pending());
    vlan_table_->NotifyEvent(vlan3, KSyncEntry::ADD_ACK);
    EXPECT_FALSE(vlan3->del_add_pending());
    EXPECT_EQ(Vlan::delete_count_, 1U);
    EXPECT_EQ(vlan3->GetState(), KSyncEntry::RENEW_WAIT);

    TestInit();
    // trigger DeleteAdd on RenewWait state
    vlan_table_->NotifyEvent(vlan3, KSyncEntry::DEL_ADD_REQ);
    EXPECT_TRUE(vlan3->del_add_pending());
    vlan_table_->NotifyEvent(vlan3, KSyncEntry::DEL_ACK);
    EXPECT_FALSE(vlan3->del_add_pending());
    EXPECT_EQ(Vlan::delete_count_, 1U);
    EXPECT_EQ(vlan3->GetState(), KSyncEntry::RENEW_WAIT);

    TestInit();
    // trigger DeleteAdd on DelDeferDelAck state
    vlan_table_->Delete(vlan3);
    EXPECT_EQ(vlan3->GetState(), KSyncEntry::DEL_DEFER_DEL_ACK);
    vlan_table_->NotifyEvent(vlan3, KSyncEntry::DEL_ADD_REQ);
    EXPECT_TRUE(vlan3->del_add_pending());
    vlan_table_->NotifyEvent(vlan3, KSyncEntry::DEL_ACK);
    EXPECT_FALSE(vlan3->del_add_pending());
    EXPECT_EQ(Vlan::delete_count_, 1U);
    EXPECT_EQ(vlan3->GetState(), KSyncEntry::RENEW_WAIT);
    vlan3->all_delete_state_comp_ = true;

    vlan_table_->NotifyEvent(vlan3, KSyncEntry::DEL_ACK);
    EXPECT_EQ(Vlan::add_count_, 1U);
    vlan_table_->NotifyEvent(vlan3, KSyncEntry::ADD_ACK);
    EXPECT_EQ(vlan3->GetState(), KSyncEntry::IN_SYNC);

    TestInit();
    // trigger DeleteAdd on DelDeferRef state
    KSyncEntry::KSyncEntryPtr vlan3_ref = vlan3;
    vlan_table_->Delete(vlan3);
    EXPECT_EQ(vlan3->GetState(), KSyncEntry::DEL_DEFER_REF);
    vlan_table_->NotifyEvent(vlan3, KSyncEntry::DEL_ADD_REQ);
    EXPECT_EQ(Vlan::delete_count_, 1U);
    EXPECT_EQ(vlan3->GetState(), KSyncEntry::RENEW_WAIT);
    vlan_table_->NotifyEvent(vlan3, KSyncEntry::DEL_ACK);
    EXPECT_EQ(Vlan::add_count_, 1U);
    vlan_table_->NotifyEvent(vlan3, KSyncEntry::ADD_ACK);
    vlan3_ref = NULL;

    vlan_table_->Delete(vlan3);
    vlan_table_->NotifyEvent(vlan3, KSyncEntry::DEL_ACK);

    TestInit();
    vlan_table_->Delete(vlan1);
    vlan_table_->Delete(vlan2);
}

TEST_F(TestUT, CreateExistingEntryWithoutFind) {
    // Vlan entry with index 0
    Vlan *vlan1 = AddVlan(0xF01, 0, KSyncEntry::IN_SYNC, Vlan::ADD, 0);

    Vlan v(0xF01, 0);

    // try create again without lookup, should give back the existing entry
    Vlan *vlan2 = static_cast<Vlan *>(vlan_table_->Create(&v, true));
    EXPECT_EQ(vlan1, vlan2);
    EXPECT_EQ(Vlan::change_count_, 1U);

    vlan_table_->Delete(vlan1);
    EXPECT_EQ(Vlan::add_count_, 1U);
    EXPECT_EQ(Vlan::change_count_, 1U);
    EXPECT_EQ(Vlan::delete_count_, 1U);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    LoggingInit();

    vlan_table_ = new VlanTable(200);
    object_manager = KSyncObjectManager::Init();
    int ret = RUN_ALL_TESTS();
    delete vlan_table_;
    KSyncObjectManager::Shutdown();
    return ret;
}
