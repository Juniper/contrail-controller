/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <iostream>
#include <fstream>

#include <boost/intrusive/avl_set.hpp>
#include <boost/functional/hash.hpp>
#include <boost/bind.hpp>
#include <tbb/atomic.h>

#include "base/logging.h"
#include "testing/gunit.h"

#include "db/db.h"
#include "db/db_table.h"
#include "db/db_entry.h"
#include "db/db_client.h"
#include "db/db_partition.h"

#include "ksync/ksync_index.h"
#include "ksync/ksync_entry.h"
#include "ksync/ksync_netlink.h"
#include "ksync/ksync_object.h"
#include "io/event_manager.h"
#include "base/test/task_test_util.h"

using namespace std;
class VlanTable;

VlanTable *vlan_table_;

KSyncObjectManager *object_manager;

class TestUT : public ::testing::Test {
public:
    TestUT() { cout << "Creating TestTask" << endl; };
    void TestBody() {};
};

class Vlan : public DBEntry {
public:
    struct VlanKey : public DBRequestKey {
        VlanKey(string name, uint16_t tag) : DBRequestKey(), name_(name),
        tag_(tag) { };
        virtual ~VlanKey() { };

        string name_;
        uint16_t tag_;
    };

    Vlan(string name, uint16_t tag) : DBEntry(), name_(name), tag_(tag) { };
    virtual ~Vlan() { };

    bool IsLess(const DBEntry &rhs) const {
        const Vlan &vlan = static_cast<const Vlan &>(rhs);
        return name_ < vlan.name_;
    };
    virtual string ToString() const { return "Vlan"; };
    virtual void SetKey(const DBRequestKey *k) {
        const VlanKey *key = static_cast<const VlanKey *>(k);
        name_ = key->name_;
        tag_ = key->tag_;
    };

    virtual KeyPtr GetDBRequestKey() const {
        VlanKey *key = new VlanKey(name_, tag_);
        return DBEntryBase::KeyPtr(key);
    };

    uint16_t GetTag() const {return tag_;};
    string name() const {return name_;}
private:
    string name_;
    uint16_t tag_;
    friend class VlanTable;
    DISALLOW_COPY_AND_ASSIGN(Vlan);
};

class VlanTable : public DBTable {
public:
    VlanTable(DB *db, const std::string &name) : DBTable(db, name) { };
    virtual ~VlanTable() { };

    virtual std::auto_ptr<DBEntry> AllocEntry(const DBRequestKey *k) const {
        const Vlan::VlanKey *key = static_cast<const Vlan::VlanKey *>(k);
        Vlan *vlan = new Vlan(key->name_, key->tag_);
        return std::auto_ptr<DBEntry>(static_cast<DBEntry *>(vlan));
    }

    virtual DBEntry *Add(const DBRequest *req) {
        Vlan::VlanKey *key = static_cast<Vlan::VlanKey *>(req->key.get());
        Vlan *vlan = new Vlan(key->name_, key->tag_);
        return vlan;
    }

    virtual bool OnChange(DBEntry *entry, const DBRequest *req) {
        Vlan::VlanKey *key = static_cast<Vlan::VlanKey *>(req->key.get());
        Vlan *vlan = static_cast<Vlan *>(entry);
        vlan->tag_ = key->tag_;
        return true;
    }

    virtual bool Delete(DBEntry *entry, const DBRequest *req) {
        return true;
    }

    static VlanTable *CreateTable(DB *db, const string &name) {
        VlanTable *table = new VlanTable(db, name);
        table->Init();
        return table;
    }

private:
    DISALLOW_COPY_AND_ASSIGN(VlanTable);
};

class VlanKSyncEntry : public KSyncDBEntry {
public:
    VlanKSyncEntry(const VlanKSyncEntry *entry) : 
        KSyncDBEntry(), tag_(entry->tag_), no_ack_trigger_(true),
        sync_pending_(false) { };

    VlanKSyncEntry(const Vlan *vlan) : 
        KSyncDBEntry(), tag_(vlan->GetTag()), no_ack_trigger_(true),
        sync_pending_(false) { };
    VlanKSyncEntry(const uint16_t tag) :
        KSyncDBEntry(), tag_(tag), no_ack_trigger_(true),
        sync_pending_(false) { };
    virtual ~VlanKSyncEntry() {};

    virtual bool IsLess(const KSyncEntry &rhs) const {
        const VlanKSyncEntry &entry = static_cast<const VlanKSyncEntry &>(rhs);
        return tag_ < entry.tag_;
    }
    virtual std::string ToString() const {return "VLAN";};;
    virtual KSyncEntry *UnresolvedReference() {return NULL;};
    virtual bool Sync(DBEntry *e) {
        Vlan *entry = static_cast<Vlan *>(e);
        if (name_ != entry->name()) {
            name_ = entry->name();
            return true;
        }
        return sync_pending_;
    }
    void set_no_ack_trigger(bool val) {no_ack_trigger_ = val;}
    void set_sync_pending(bool val) {sync_pending_ = val;}
    virtual bool Add() {
        add_count_++;
        return no_ack_trigger_;
    };
    virtual bool Change() {
        change_count_++;
        return no_ack_trigger_;
    };
    virtual bool Delete() {
        del_count_++;
        return no_ack_trigger_;
    };
    KSyncDBObject *GetObject();

    uint32_t GetTag() const {return tag_;};
    const string &name() const {return name_;}
    static void Reset() {
        add_count_ = 0;
        change_count_ = 0;
        del_count_ = 0;
    }

    static int GetAddCount() {return add_count_;};
    static int GetChangeCount() {return change_count_;};
    static int GetDelCount() {return del_count_;};

private:
    uint16_t tag_;
    string name_;
    bool     no_ack_trigger_;
    bool     sync_pending_;
    static int add_count_;
    static int change_count_;
    static int del_count_;
    DISALLOW_COPY_AND_ASSIGN(VlanKSyncEntry);
};
int VlanKSyncEntry::add_count_;
int VlanKSyncEntry::change_count_;
int VlanKSyncEntry::del_count_;

class VlanKSyncObject : public KSyncDBObject {
public:
    VlanKSyncObject(DBTableBase *table) : KSyncDBObject("Vlan KSync", table),
                                          is_empty_count_(0) {
        evm_.reset(new EventManager());
        // set timer value to -1, and trigger timer callback explicitly
        // set 1 entry processing per iteration
        InitStaleEntryCleanup(*(evm_->io_service()), -1, -1, 1);
    }

    virtual ~VlanKSyncObject() {
        evm_->Shutdown();
    }

    virtual KSyncEntry *Alloc(const KSyncEntry *entry, uint32_t index) {
        const VlanKSyncEntry *vlan = static_cast<const VlanKSyncEntry *>(entry);
        VlanKSyncEntry *ksync = new VlanKSyncEntry(vlan);
        return static_cast<KSyncEntry *>(ksync);
    };

    virtual KSyncEntry *DBToKSyncEntry(const DBEntry *e) {
        const Vlan *vlan = static_cast<const Vlan *>(e);
        VlanKSyncEntry *key = new VlanKSyncEntry(vlan);
        return static_cast<KSyncEntry *>(key);
    }

    DBFilterResp DBEntryFilter(const DBEntry *entry,
                               const KSyncDBEntry *ksync) {
        const Vlan *vlan = static_cast<const Vlan *>(entry);
        if (vlan->GetTag() == 0) {
            return DBFilterDelete;
        }
        if (ksync != NULL) {
            const VlanKSyncEntry *kvlan =
                static_cast<const VlanKSyncEntry *>(ksync);
            if (vlan->GetTag() != kvlan->GetTag()) {
                return DBFilterDelAdd;
            }
        }
        return DBFilterAccept;
    }

    static void Init(VlanTable *table) {
        assert(singleton_ == NULL);
        singleton_ = new VlanKSyncObject(table);
    };

    static void Shutdown() {
        delete singleton_;
        singleton_ = NULL;
    }

    static VlanKSyncObject *GetKSyncObject() { return singleton_; };

    virtual void EmptyTable(void) { is_empty_count_++; }
    int is_empty_count_;

private:
    static VlanKSyncObject *singleton_;
    auto_ptr<EventManager> evm_;
    DISALLOW_COPY_AND_ASSIGN(VlanKSyncObject);

};
VlanKSyncObject *VlanKSyncObject::singleton_;

class DBKSyncTest : public ::testing::Test {
protected:
    tbb::atomic<long> adc_notification;
    tbb::atomic<long> del_notification;

public:
    DBKSyncTest() {
        adc_notification = 0;
        del_notification = 0;
    }

    virtual void SetUp() {
        VlanKSyncEntry::Reset();
        itbl = static_cast<VlanTable *>(db_.CreateTable("db.test.vlan.0"));
        tid_ = itbl->Register
            (boost::bind(&DBKSyncTest::DBTestListener, this, _1, _2));
        VlanKSyncObject::Init(itbl);
    }

    virtual void TearDown() {
        itbl->Unregister(tid_);
        db_.RemoveTable(itbl);
        VlanKSyncObject::Shutdown();
        delete itbl;
    }

    void DBTestListener(DBTablePartBase *root, DBEntryBase *entry) {
        Vlan *vlan = static_cast<Vlan *>(entry);
        bool del_notify = vlan->IsDeleted();
        if (del_notify) {
            del_notification++;
        } else {
            adc_notification++;
        }
    }

    DB db_;
    VlanTable *itbl;
    DBTableBase::ListenerId tid_;
    DBTableBase::ListenerId tid_1_;
};

KSyncDBObject *VlanKSyncEntry::GetObject() {
    return VlanKSyncObject::GetKSyncObject();
}

TEST_F(DBKSyncTest, Basic) {
    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    req.key.reset(new Vlan::VlanKey("vlan10", 10));
    req.data.reset(NULL);
    itbl->Enqueue(&req);

    task_util::WaitForIdle();
    EXPECT_EQ(adc_notification, 1);
    EXPECT_EQ(del_notification, 0);

    EXPECT_EQ(VlanKSyncEntry::GetAddCount(), 1);
    EXPECT_EQ(VlanKSyncEntry::GetDelCount(), 0);

    req.oper = DBRequest::DB_ENTRY_DELETE;
    req.key.reset(new Vlan::VlanKey("vlan10", 10));
    req.data.reset(NULL);
    itbl->Enqueue(&req);

    task_util::WaitForIdle();
    EXPECT_EQ(adc_notification, 1);
    EXPECT_EQ(del_notification, 1);

    EXPECT_EQ(VlanKSyncEntry::GetAddCount(), 1);
    EXPECT_EQ(VlanKSyncEntry::GetDelCount(), 1);
}

TEST_F(DBKSyncTest, AddDelCompress) {
    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    req.key.reset(new Vlan::VlanKey("vlan10", 10));
    req.data.reset(NULL);
    itbl->Enqueue(&req);

    req.oper = DBRequest::DB_ENTRY_DELETE;
    req.key.reset(new Vlan::VlanKey("vlan10", 10));
    req.data.reset(NULL);
    itbl->Enqueue(&req);

    task_util::WaitForIdle();
    EXPECT_EQ(adc_notification, 0);
    EXPECT_EQ(del_notification, 1);

    EXPECT_EQ(VlanKSyncEntry::GetAddCount(), 0);
    EXPECT_EQ(VlanKSyncEntry::GetDelCount(), 0);
    task_util::WaitForIdle();
}

TEST_F(DBKSyncTest, DuplicateDelete) {
    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    req.key.reset(new Vlan::VlanKey("vlan10", 10));
    req.data.reset(NULL);
    itbl->Enqueue(&req);

    //Get a reference to vlan entry, to avoid deletion
    //of ksync entry upon first request
    task_util::WaitForIdle();
    VlanKSyncEntry v(10);
    KSyncEntry::KSyncEntryPtr ksync_vlan;
    ksync_vlan = VlanKSyncObject::GetKSyncObject()->Find(&v); 

    req.oper = DBRequest::DB_ENTRY_DELETE;
    req.key.reset(new Vlan::VlanKey("vlan10", 10));
    req.data.reset(NULL);
    itbl->Enqueue(&req);
    task_util::WaitForIdle();

    req.oper = DBRequest::DB_ENTRY_DELETE;
    req.key.reset(new Vlan::VlanKey("vlan10", 10));
    req.data.reset(NULL);
    itbl->Enqueue(&req);
    task_util::WaitForIdle();

    ksync_vlan = NULL;
    task_util::WaitForIdle();
    EXPECT_EQ(adc_notification, 1);
    EXPECT_EQ(del_notification, 2);

    EXPECT_EQ(VlanKSyncEntry::GetAddCount(), 1);
    EXPECT_EQ(VlanKSyncEntry::GetDelCount(), 1);
}

TEST_F(DBKSyncTest, Del_Ack_Wait_to_Temp) {
    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    req.key.reset(new Vlan::VlanKey("vlan10", 10));
    req.data.reset(NULL);
    itbl->Enqueue(&req);

    //Get a reference to vlan entry, to avoid deletion
    //of ksync entry upon first request
    task_util::WaitForIdle();
    VlanKSyncEntry v(10);
    KSyncEntry::KSyncEntryPtr ksync_vlan;

    ksync_vlan = VlanKSyncObject::GetKSyncObject()->Find(&v);
    VlanKSyncEntry *k_vlan = (VlanKSyncEntry *)ksync_vlan.get();
    k_vlan->set_no_ack_trigger(false);
    ksync_vlan = NULL;
    req.oper = DBRequest::DB_ENTRY_DELETE;
    req.key.reset(new Vlan::VlanKey("vlan10", 10));
    req.data.reset(NULL);
    itbl->Enqueue(&req);
    task_util::WaitForIdle();
    ksync_vlan = VlanKSyncObject::GetKSyncObject()->Find(&v);

    EXPECT_EQ(ksync_vlan->GetState(), KSyncEntry::DEL_ACK_WAIT);
    VlanKSyncObject::GetKSyncObject()->NetlinkAck(ksync_vlan.get(),
                                                  KSyncEntry::DEL_ACK);
    EXPECT_EQ(ksync_vlan->GetState(), KSyncEntry::TEMP);

    k_vlan->set_no_ack_trigger(true);

    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    req.key.reset(new Vlan::VlanKey("vlan10", 10));
    req.data.reset(NULL);
    itbl->Enqueue(&req);
    task_util::WaitForIdle();
    EXPECT_EQ(ksync_vlan->GetState(), KSyncEntry::IN_SYNC);

    req.oper = DBRequest::DB_ENTRY_DELETE;
    req.key.reset(new Vlan::VlanKey("vlan10", 10));
    req.data.reset(NULL);
    itbl->Enqueue(&req);
    task_util::WaitForIdle();

    ksync_vlan = NULL;
    task_util::WaitForIdle();
    EXPECT_EQ(adc_notification, 2);
    EXPECT_EQ(del_notification, 2);

    EXPECT_EQ(VlanKSyncEntry::GetAddCount(), 2);
    EXPECT_EQ(VlanKSyncEntry::GetDelCount(), 2);
}

TEST_F(DBKSyncTest, KSyncEntryRenewWithNewDBEntry) {
    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    req.key.reset(new Vlan::VlanKey("vlan10", 10));
    req.data.reset(NULL);
    itbl->Enqueue(&req);

    //Get a reference to vlan entry, to avoid deletion
    task_util::WaitForIdle();
    VlanKSyncEntry v(10);
    KSyncEntry::KSyncEntryPtr ksync_vlan;
    ksync_vlan = VlanKSyncObject::GetKSyncObject()->Find(&v);

    req.oper = DBRequest::DB_ENTRY_DELETE;
    req.key.reset(new Vlan::VlanKey("vlan10", 10));
    req.data.reset(NULL);
    itbl->Enqueue(&req);
    task_util::WaitForIdle();

    // Delete pending due to reference
    EXPECT_EQ(ksync_vlan->GetState(), KSyncEntry::DEL_DEFER_REF);

    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    req.key.reset(new Vlan::VlanKey("new_vlan10", 10));
    req.data.reset(NULL);
    itbl->Enqueue(&req);
    task_util::WaitForIdle();

    // Ksync should move to in sync and new db entry.
    EXPECT_EQ(ksync_vlan->GetState(), KSyncEntry::IN_SYNC);
    ksync_vlan = NULL;

    req.oper = DBRequest::DB_ENTRY_DELETE;
    req.key.reset(new Vlan::VlanKey("new_vlan10", 10));
    req.data.reset(NULL);
    itbl->Enqueue(&req);
    task_util::WaitForIdle();

    EXPECT_EQ(adc_notification, 2);
    EXPECT_EQ(del_notification, 2);

    EXPECT_EQ(VlanKSyncEntry::GetAddCount(), 1);
    EXPECT_EQ(VlanKSyncEntry::GetDelCount(), 1);
}

TEST_F(DBKSyncTest, OneKSyncEntryForTwoOperDBEntry) {
    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    req.key.reset(new Vlan::VlanKey("vlan10", 10));
    req.data.reset(NULL);
    itbl->Enqueue(&req);

    task_util::WaitForIdle();
    VlanKSyncEntry v(10);
    VlanKSyncEntry *ksync_vlan =
        static_cast<VlanKSyncEntry *>(VlanKSyncObject::GetKSyncObject()->Find(&v));

    // check ksync entry in sync and db entry vlan 10 being in use
    EXPECT_EQ(ksync_vlan->GetState(), KSyncEntry::IN_SYNC);
    EXPECT_TRUE(ksync_vlan->name().compare("vlan10") == 0);

    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    req.key.reset(new Vlan::VlanKey("new_vlan10", 10));
    req.data.reset(NULL);
    itbl->Enqueue(&req);
    task_util::WaitForIdle();

    // check ksync entry in sync and db entry vlan 10 being in use
    EXPECT_EQ(ksync_vlan->GetState(), KSyncEntry::IN_SYNC);
    EXPECT_TRUE(ksync_vlan->name().compare("vlan10") == 0);

    // trigger a change on un associated entry
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    req.key.reset(new Vlan::VlanKey("new_vlan10", 10));
    req.data.reset(NULL);
    itbl->Enqueue(&req);
    task_util::WaitForIdle();

    // check ksync entry in sync and db entry vlan 10 being in use
    EXPECT_EQ(ksync_vlan->GetState(), KSyncEntry::IN_SYNC);
    EXPECT_TRUE(ksync_vlan->name().compare("vlan10") == 0);

    // trigger delete on un associated entry
    req.oper = DBRequest::DB_ENTRY_DELETE;
    req.key.reset(new Vlan::VlanKey("new_vlan10", 10));
    req.data.reset(NULL);
    itbl->Enqueue(&req);
    task_util::WaitForIdle();

    // check ksync entry in sync and db entry vlan 10 being in use
    EXPECT_EQ(ksync_vlan->GetState(), KSyncEntry::IN_SYNC);
    EXPECT_TRUE(ksync_vlan->name().compare("vlan10") == 0);

    // Add duplicate entry again and then trigger delete on associated entry.
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    req.key.reset(new Vlan::VlanKey("new_vlan10", 10));
    req.data.reset(NULL);
    itbl->Enqueue(&req);
    task_util::WaitForIdle();

    req.oper = DBRequest::DB_ENTRY_DELETE;
    req.key.reset(new Vlan::VlanKey("vlan10", 10));
    req.data.reset(NULL);
    itbl->Enqueue(&req);
    task_util::WaitForIdle();

    // check ksync entry in sync and db entry new vlan 10 being in use
    EXPECT_EQ(ksync_vlan->GetState(), KSyncEntry::IN_SYNC);
    EXPECT_TRUE(ksync_vlan->name().compare("new_vlan10") == 0);

    req.oper = DBRequest::DB_ENTRY_DELETE;
    req.key.reset(new Vlan::VlanKey("new_vlan10", 10));
    req.data.reset(NULL);
    itbl->Enqueue(&req);
    task_util::WaitForIdle();

    EXPECT_EQ(adc_notification, 4);
    EXPECT_EQ(del_notification, 3);

    EXPECT_EQ(VlanKSyncEntry::GetAddCount(), 1);
    EXPECT_EQ(VlanKSyncEntry::GetDelCount(), 1);
}

TEST_F(DBKSyncTest, Vlan_object_delete_with_dup_entries) {
    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    req.key.reset(new Vlan::VlanKey("vlan10", 10));
    req.data.reset(NULL);
    itbl->Enqueue(&req);

    task_util::WaitForIdle();
    VlanKSyncEntry v(10);
    VlanKSyncEntry *ksync_vlan =
        static_cast<VlanKSyncEntry *>(VlanKSyncObject::GetKSyncObject()->Find(&v));

    // check ksync entry in sync and db entry vlan 10 being in use
    EXPECT_EQ(ksync_vlan->GetState(), KSyncEntry::IN_SYNC);
    EXPECT_TRUE(ksync_vlan->name().compare("vlan10") == 0);

    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    req.key.reset(new Vlan::VlanKey("new_vlan10", 10));
    req.data.reset(NULL);
    itbl->Enqueue(&req);
    task_util::WaitForIdle();

    // check ksync entry in sync and db entry vlan 10 being in use
    EXPECT_EQ(ksync_vlan->GetState(), KSyncEntry::IN_SYNC);
    EXPECT_TRUE(ksync_vlan->name().compare("vlan10") == 0);

    // fetch previous pointer and create a new vlan object.
    VlanKSyncObject *vlan_obj = VlanKSyncObject::GetKSyncObject();

    // trigger ksync object delete
    vlan_obj->is_empty_count_ = 0;
    object_manager->Delete(vlan_obj);
    task_util::WaitForIdle();

    EXPECT_EQ(vlan_obj->is_empty_count_, 1);

    EXPECT_EQ(VlanKSyncEntry::GetAddCount(), 1);
    EXPECT_EQ(VlanKSyncEntry::GetDelCount(), 1);

    VlanKSyncObject::Shutdown();
    VlanKSyncObject::Init(itbl);

    // delete both the db entries.
    req.oper = DBRequest::DB_ENTRY_DELETE;
    req.key.reset(new Vlan::VlanKey("vlan10", 10));
    req.data.reset(NULL);
    itbl->Enqueue(&req);
    task_util::WaitForIdle();

    req.oper = DBRequest::DB_ENTRY_DELETE;
    req.key.reset(new Vlan::VlanKey("new_vlan10", 10));
    req.data.reset(NULL);
    itbl->Enqueue(&req);
    task_util::WaitForIdle();

    EXPECT_EQ(adc_notification, 2);
    EXPECT_EQ(del_notification, 2);
}

TEST_F(DBKSyncTest, create_stale_vlan_entry_to_non_stale) {
    // Create a stale entry for vlan 10
    VlanKSyncEntry vlan_key(10);
    VlanKSyncObject::GetKSyncObject()->CreateStale(&vlan_key);

    EXPECT_EQ(VlanKSyncEntry::GetAddCount(), 1);
    EXPECT_EQ(VlanKSyncEntry::GetDelCount(), 0);

    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    req.key.reset(new Vlan::VlanKey("vlan10", 10));
    req.data.reset(NULL);
    itbl->Enqueue(&req);

    task_util::WaitForIdle();
    EXPECT_EQ(adc_notification, 1);
    EXPECT_EQ(del_notification, 0);

    // Triggers delete of stale entry
    TestTriggerStaleEntryCleanupCb(VlanKSyncObject::GetKSyncObject());

    EXPECT_EQ(VlanKSyncEntry::GetAddCount(), 1);
    EXPECT_EQ(VlanKSyncEntry::GetChangeCount(), 1);
    EXPECT_EQ(VlanKSyncEntry::GetDelCount(), 0);

    req.oper = DBRequest::DB_ENTRY_DELETE;
    req.key.reset(new Vlan::VlanKey("vlan10", 10));
    req.data.reset(NULL);
    itbl->Enqueue(&req);

    task_util::WaitForIdle();
    EXPECT_EQ(adc_notification, 1);
    EXPECT_EQ(del_notification, 1);

    EXPECT_EQ(VlanKSyncEntry::GetAddCount(), 1);
    EXPECT_EQ(VlanKSyncEntry::GetDelCount(), 1);
}

TEST_F(DBKSyncTest, DeleteAck_after_create_stale) {
    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    req.key.reset(new Vlan::VlanKey("vlan10", 10));
    req.data.reset(NULL);
    itbl->Enqueue(&req);

    // set entry to wait for ack trigger, to control KSync state
    // events order
    task_util::WaitForIdle();
    VlanKSyncEntry v(10);
    VlanKSyncEntry *ksync_vlan;
    ksync_vlan =
        static_cast<VlanKSyncEntry*>(VlanKSyncObject::GetKSyncObject()->Find(&v));
    ksync_vlan->set_no_ack_trigger(false);

    req.oper = DBRequest::DB_ENTRY_DELETE;
    req.key.reset(new Vlan::VlanKey("vlan10", 10));
    req.data.reset(NULL);
    itbl->Enqueue(&req);
    task_util::WaitForIdle();
    EXPECT_EQ(adc_notification, 1);
    EXPECT_EQ(del_notification, 1);
    EXPECT_EQ(VlanKSyncEntry::GetAddCount(), 1);
    EXPECT_EQ(VlanKSyncEntry::GetDelCount(), 1);

    // set to true so that we don't need to generate add ack
    ksync_vlan->set_no_ack_trigger(true);
    // Create a stale entry for vlan 10
    VlanKSyncEntry vlan_key(10);
    VlanKSyncObject::GetKSyncObject()->CreateStale(&vlan_key);
    EXPECT_EQ(adc_notification, 1);
    EXPECT_EQ(del_notification, 1);

    // simulate ACK to move to active state
    VlanKSyncObject::GetKSyncObject()->NetlinkAck(ksync_vlan,
                                                  KSyncEntry::DEL_ACK);

    task_util::WaitForIdle();
    EXPECT_EQ(VlanKSyncEntry::GetAddCount(), 2);
    EXPECT_EQ(VlanKSyncEntry::GetDelCount(), 1);

    // verify DB entry clean up
    Vlan::VlanKey v_key("vlan10", 10);
    EXPECT_TRUE(NULL == itbl->Find(&v_key));

    // Triggers delete of stale entry
    TestTriggerStaleEntryCleanupCb(VlanKSyncObject::GetKSyncObject());

    EXPECT_EQ(VlanKSyncEntry::GetAddCount(), 2);
    EXPECT_EQ(VlanKSyncEntry::GetDelCount(), 2);
}

TEST_F(DBKSyncTest, DBFilterDelete) {
    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    req.key.reset(new Vlan::VlanKey("vlan10", 10));
    req.data.reset(NULL);
    itbl->Enqueue(&req);

    // set entry to wait for ack trigger, to control KSync state
    // events order
    task_util::WaitForIdle();
    VlanKSyncEntry v(10);
    VlanKSyncEntry *ksync_vlan;
    ksync_vlan =
        static_cast<VlanKSyncEntry*>(VlanKSyncObject::GetKSyncObject()->Find(&v));
    EXPECT_TRUE(ksync_vlan != NULL);

    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    req.key.reset(new Vlan::VlanKey("vlan10", 0));
    req.data.reset(NULL);
    itbl->Enqueue(&req);
    task_util::WaitForIdle();

    // Above should trigger a delete filter
    ksync_vlan =
        static_cast<VlanKSyncEntry*>(VlanKSyncObject::GetKSyncObject()->Find(&v));
    EXPECT_TRUE(ksync_vlan == NULL);

    req.oper = DBRequest::DB_ENTRY_DELETE;
    req.key.reset(new Vlan::VlanKey("vlan10", 0));
    req.data.reset(NULL);
    itbl->Enqueue(&req);
    task_util::WaitForIdle();
    EXPECT_EQ(adc_notification, 2);
    EXPECT_EQ(del_notification, 1);
    EXPECT_EQ(VlanKSyncEntry::GetAddCount(), 1);
    EXPECT_EQ(VlanKSyncEntry::GetDelCount(), 1);
}

TEST_F(DBKSyncTest, DBFilterDelAdd) {
    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    req.key.reset(new Vlan::VlanKey("vlan10", 10));
    req.data.reset(NULL);
    itbl->Enqueue(&req);

    // set entry to wait for ack trigger, to control KSync state
    // events order
    task_util::WaitForIdle();
    VlanKSyncEntry v(10);
    VlanKSyncEntry *ksync_vlan;
    ksync_vlan =
        static_cast<VlanKSyncEntry*>(VlanKSyncObject::GetKSyncObject()->Find(&v));
    EXPECT_TRUE(ksync_vlan != NULL);

    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    req.key.reset(new Vlan::VlanKey("vlan10", 11));
    req.data.reset(NULL);
    itbl->Enqueue(&req);
    task_util::WaitForIdle();

    // Above should trigger a delete-add filter, which will trigger delete
    // for ksync entry with 10 and create of ksync entry with 11
    ksync_vlan =
        static_cast<VlanKSyncEntry*>(VlanKSyncObject::GetKSyncObject()->Find(&v));
    EXPECT_TRUE(ksync_vlan == NULL);
    VlanKSyncEntry v1(11);
    ksync_vlan =
        static_cast<VlanKSyncEntry*>(VlanKSyncObject::GetKSyncObject()->Find(&v1));
    EXPECT_TRUE(ksync_vlan != NULL);

    req.oper = DBRequest::DB_ENTRY_DELETE;
    req.key.reset(new Vlan::VlanKey("vlan10", 11));
    req.data.reset(NULL);
    itbl->Enqueue(&req);
    task_util::WaitForIdle();
    EXPECT_EQ(adc_notification, 2);
    EXPECT_EQ(del_notification, 1);
    EXPECT_EQ(VlanKSyncEntry::GetAddCount(), 2);
    EXPECT_EQ(VlanKSyncEntry::GetDelCount(), 2);
}

TEST_F(DBKSyncTest, DBFilterDelAddwithTwoOperDBEntry) {
    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    req.key.reset(new Vlan::VlanKey("vlan10", 10));
    req.data.reset(NULL);
    itbl->Enqueue(&req);

    // set entry to wait for ack trigger, to control KSync state
    // events order
    task_util::WaitForIdle();
    VlanKSyncEntry v(10);
    VlanKSyncEntry *ksync_vlan;
    ksync_vlan =
        static_cast<VlanKSyncEntry*>(VlanKSyncObject::GetKSyncObject()->Find(&v));
    EXPECT_TRUE(ksync_vlan != NULL);
    if (ksync_vlan != NULL) {
        // check ksync entry in sync and db entry vlan 10 being in use
        EXPECT_EQ(ksync_vlan->GetState(), KSyncEntry::IN_SYNC);
        EXPECT_TRUE(ksync_vlan->name().compare("vlan10") == 0);
    }

    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    req.key.reset(new Vlan::VlanKey("new_vlan10", 10));
    req.data.reset(NULL);
    itbl->Enqueue(&req);
    task_util::WaitForIdle();

    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    req.key.reset(new Vlan::VlanKey("vlan10", 11));
    req.data.reset(NULL);
    itbl->Enqueue(&req);
    task_util::WaitForIdle();

    // Above should trigger a delete-add filter, which will trigger delete
    // for ksync entry with 10  to start using new_vlan10 and create of
    // ksync entry with 11
    ksync_vlan =
        static_cast<VlanKSyncEntry*>(VlanKSyncObject::GetKSyncObject()->Find(&v));
    EXPECT_TRUE(ksync_vlan != NULL);
    if (ksync_vlan != NULL) {
        // check ksync entry in sync and db entry vlan 10 being in use
        EXPECT_EQ(ksync_vlan->GetState(), KSyncEntry::IN_SYNC);
        EXPECT_TRUE(ksync_vlan->name().compare("new_vlan10") == 0);
    }

    VlanKSyncEntry v1(11);
    ksync_vlan =
        static_cast<VlanKSyncEntry*>(VlanKSyncObject::GetKSyncObject()->Find(&v1));
    EXPECT_TRUE(ksync_vlan != NULL);

    req.oper = DBRequest::DB_ENTRY_DELETE;
    req.key.reset(new Vlan::VlanKey("vlan10", 11));
    req.data.reset(NULL);
    itbl->Enqueue(&req);
    task_util::WaitForIdle();

    req.oper = DBRequest::DB_ENTRY_DELETE;
    req.key.reset(new Vlan::VlanKey("new_vlan10", 10));
    req.data.reset(NULL);
    itbl->Enqueue(&req);
    task_util::WaitForIdle();

    EXPECT_EQ(adc_notification, 3);
    EXPECT_EQ(del_notification, 2);
    EXPECT_EQ(VlanKSyncEntry::GetAddCount(), 2);
    EXPECT_EQ(VlanKSyncEntry::GetChangeCount(), 1);
    EXPECT_EQ(VlanKSyncEntry::GetDelCount(), 2);
}

TEST_F(DBKSyncTest, DBFilterDelAddDupToDup) {
    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    req.key.reset(new Vlan::VlanKey("vlan10", 10));
    req.data.reset(NULL);
    itbl->Enqueue(&req);

    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    req.key.reset(new Vlan::VlanKey("vlan11", 11));
    req.data.reset(NULL);
    itbl->Enqueue(&req);

    // set entry to wait for ack trigger, to control KSync state
    // events order
    task_util::WaitForIdle();
    VlanKSyncEntry v(10);
    VlanKSyncEntry v1(11);
    VlanKSyncEntry *ksync_vlan;
    ksync_vlan =
        static_cast<VlanKSyncEntry*>(VlanKSyncObject::GetKSyncObject()->Find(&v));
    EXPECT_TRUE(ksync_vlan != NULL);
    if (ksync_vlan != NULL) {
        // check ksync entry in sync and db entry vlan 10 being in use
        EXPECT_EQ(ksync_vlan->GetState(), KSyncEntry::IN_SYNC);
        EXPECT_TRUE(ksync_vlan->name().compare("vlan10") == 0);
    }

    ksync_vlan =
        static_cast<VlanKSyncEntry*>(VlanKSyncObject::GetKSyncObject()->Find(&v1));
    EXPECT_TRUE(ksync_vlan != NULL);
    if (ksync_vlan != NULL) {
        // check ksync entry in sync and db entry vlan 11 being in use
        EXPECT_EQ(ksync_vlan->GetState(), KSyncEntry::IN_SYNC);
        EXPECT_TRUE(ksync_vlan->name().compare("vlan11") == 0);
    }

    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    req.key.reset(new Vlan::VlanKey("new_vlan10", 10));
    req.data.reset(NULL);
    itbl->Enqueue(&req);
    task_util::WaitForIdle();

    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    req.key.reset(new Vlan::VlanKey("vlan10", 11));
    req.data.reset(NULL);
    itbl->Enqueue(&req);
    task_util::WaitForIdle();

    // Above should trigger a delete-add filter, which will trigger delete
    // for ksync entry with 10  to start using new_vlan10 and put vlan10 as
    // duplicate db entry of vlan11
    ksync_vlan =
        static_cast<VlanKSyncEntry*>(VlanKSyncObject::GetKSyncObject()->Find(&v));
    EXPECT_TRUE(ksync_vlan != NULL);
    if (ksync_vlan != NULL) {
        // check ksync entry in sync and db entry vlan 10 being in use
        EXPECT_EQ(ksync_vlan->GetState(), KSyncEntry::IN_SYNC);
        EXPECT_TRUE(ksync_vlan->name().compare("new_vlan10") == 0);
    }

    ksync_vlan =
        static_cast<VlanKSyncEntry*>(VlanKSyncObject::GetKSyncObject()->Find(&v1));
    EXPECT_TRUE(ksync_vlan != NULL);
    if (ksync_vlan != NULL) {
        // check ksync entry in sync and db entry vlan 11 being in use
        EXPECT_EQ(ksync_vlan->GetState(), KSyncEntry::IN_SYNC);
        EXPECT_TRUE(ksync_vlan->name().compare("vlan11") == 0);
    }

    req.oper = DBRequest::DB_ENTRY_DELETE;
    req.key.reset(new Vlan::VlanKey("vlan11", 11));
    req.data.reset(NULL);
    itbl->Enqueue(&req);
    task_util::WaitForIdle();

    ksync_vlan =
        static_cast<VlanKSyncEntry*>(VlanKSyncObject::GetKSyncObject()->Find(&v1));
    EXPECT_TRUE(ksync_vlan != NULL);
    if (ksync_vlan != NULL) {
        // check ksync entry in sync and db entry vlan 11 being in use
        EXPECT_EQ(ksync_vlan->GetState(), KSyncEntry::IN_SYNC);
        EXPECT_TRUE(ksync_vlan->name().compare("vlan10") == 0);
    }

    req.oper = DBRequest::DB_ENTRY_DELETE;
    req.key.reset(new Vlan::VlanKey("vlan10", 11));
    req.data.reset(NULL);
    itbl->Enqueue(&req);
    task_util::WaitForIdle();

    req.oper = DBRequest::DB_ENTRY_DELETE;
    req.key.reset(new Vlan::VlanKey("new_vlan10", 10));
    req.data.reset(NULL);
    itbl->Enqueue(&req);
    task_util::WaitForIdle();

    EXPECT_EQ(adc_notification, 4);
    EXPECT_EQ(del_notification, 3);
    EXPECT_EQ(VlanKSyncEntry::GetAddCount(), 2);
    EXPECT_EQ(VlanKSyncEntry::GetChangeCount(), 2);
    EXPECT_EQ(VlanKSyncEntry::GetDelCount(), 2);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    LoggingInit();

    object_manager = KSyncObjectManager::Init();
    DB::RegisterFactory("db.test.vlan.0", &VlanTable::CreateTable);
    return RUN_ALL_TESTS();
}
