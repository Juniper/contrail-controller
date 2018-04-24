/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/intrusive/avl_set.hpp>
#include <boost/functional/hash.hpp>
#include <boost/bind.hpp>
#include <tbb/atomic.h>

#include "db/db.h"
#include "db/db_table.h"
#include "db/db_entry.h"
#include "db/db_client.h"
#include "db/db_partition.h"
#include "db/db_table_walker.h"
#include "base/time_util.h"

#include "base/logging.h"
#include "base/task_annotations.h"
#include "testing/gunit.h"

class VlanTable;

struct VlanTableReqKey : public DBRequestKey {
    VlanTableReqKey(unsigned short tag) : tag(tag), del(true) {}
    VlanTableReqKey(unsigned short tag, bool del) : tag(tag), del(del) {}
    unsigned short tag;
    bool del;
};

struct VlanTableReqData : public DBRequestData {
    VlanTableReqData(std::string desc) : description(desc) {};
    std::string description;
};

class Vlan : public DBEntry {
public:
    Vlan(unsigned short tag) : vlan_tag(tag) { }
    Vlan(unsigned short tag, std::string desc)
        : vlan_tag(tag), description(desc) { }

    ~Vlan() {
    }

    bool IsLess(const DBEntry &rhs) const {
        const Vlan &a = static_cast<const Vlan &>(rhs);
        return vlan_tag < a.vlan_tag;
    }

    void SetKey(const DBRequestKey *key) {
        const VlanTableReqKey *k = static_cast<const VlanTableReqKey *>(key);
        vlan_tag = k->tag;
    }

    unsigned short getTag() const {
        return vlan_tag;
    }

    std::string getDesc() const {
        return description;
    }

    std::string ToString() const {
        return "Vlan";
    }

    void updateDescription(const std::string &desc) {
        description.assign(desc);
    }

    virtual KeyPtr GetDBRequestKey() const {
        VlanTableReqKey *key = new VlanTableReqKey(vlan_tag);
        return KeyPtr(key);
    }
private:
    unsigned short vlan_tag;
    std::string description;
    DISALLOW_COPY_AND_ASSIGN(Vlan);
};

class VlanTable : public DBTable {
public:
    VlanTable(DB *db) :
        DBTable(db, "__vlan__.0"), retry_delete_count_(0), del_req_count_(0) {
    }
    ~VlanTable() { }

    uint32_t del_req_count() const { return del_req_count_; }

    // Alloc a derived DBEntry
    virtual std::auto_ptr<DBEntry> AllocEntry(const DBRequestKey *key) const {
        const VlanTableReqKey *vkey = static_cast<const VlanTableReqKey *>(key);
        Vlan *vlan = new Vlan(vkey->tag);
        return std::auto_ptr<DBEntry>(vlan);
    };

    size_t Hash(const DBEntry *entry) const {
        return (static_cast<const Vlan *>(entry))->getTag();
    }

    size_t Hash(const DBRequestKey *key) const {
        return (static_cast<const VlanTableReqKey *>(key))->tag;
    }

    virtual DBEntry *Add(const DBRequest *req) {
        const VlanTableReqKey *key = static_cast<const VlanTableReqKey *>
            (req->key.get());
        const VlanTableReqData *data = static_cast<const VlanTableReqData *>
            (req->data.get());
        Vlan *vlan = new Vlan(key->tag);
        vlan->updateDescription(data->description);
        return vlan;
    };

    virtual bool OnChange(DBEntry *entry, const DBRequest *req) {
        const VlanTableReqData *data = static_cast<const VlanTableReqData *>
            (req->data.get());
        Vlan *vlan = static_cast<Vlan *>(entry);

        vlan->updateDescription(data->description);
        return true;
    };

    virtual bool Delete(DBEntry *entry, const DBRequest *req) {
        del_req_count_++;
        const VlanTableReqKey *key = static_cast<const VlanTableReqKey *>
            (req->key.get());
        return key->del;
    }

    Vlan *Find(VlanTableReqKey *key) {
        Vlan vlan(key->tag);
        return static_cast<Vlan *>(DBTable::Find(&vlan));
    };

    static DBTableBase *CreateTable(DB *db, const std::string &name) {
        VlanTable *table = new VlanTable(db);
        table->Init();
        return table;
    }

    void RetryDelete() {
        retry_delete_count_++;
    }

    uint32_t retry_delete_count() const { return retry_delete_count_; }
    uint32_t retry_delete_count_;
    uint32_t del_req_count_;
    DISALLOW_COPY_AND_ASSIGN(VlanTable);
};

#include "db_test_cmn.h"

// To Test:
// DBTable::NotifyAllEntries API
TEST_F(DBTest, NotifyAllEntries) {
    const int num_entries = 128;

    // Register client for notification
    tid_ = itbl->Register(boost::bind(&DBTest::DBTestListener, this, _1, _2));
    TASK_UTIL_EXPECT_EQ(tid_, 0);
    adc_notification = 0;
    del_notification = 0;

    // Add a bunch of entries
    for (int idx = 0; idx < num_entries; ++idx) {
        DBRequest addReq;
        addReq.key.reset(new VlanTableReqKey(idx));
        addReq.data.reset(new VlanTableReqData("DB Test Vlan"));
        addReq.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
        itbl->Enqueue(&addReq);
    }
    TASK_UTIL_EXPECT_EQ(num_entries, adc_notification);
    adc_notification = 0;

    // Invoke method in right concurrency scope
    {
        ConcurrencyScope scope("bgp::Config");
        itbl->NotifyAllEntries();
    }

    // Verify notification count
    TASK_UTIL_EXPECT_EQ(num_entries, adc_notification);
    adc_notification = 0;

    // Invoke method before the previous walk has finished
    TaskScheduler::GetInstance()->Stop();
    {
        ConcurrencyScope scope("bgp::Config");
        itbl->NotifyAllEntries();
        itbl->NotifyAllEntries();
    }
    TaskScheduler::GetInstance()->Start();
    TASK_UTIL_EXPECT_EQ(num_entries, adc_notification);
    adc_notification = 0;

    // Delete all entries
    for (int idx = 0; idx < num_entries; ++idx) {
        DBRequest delReq;
        delReq.key.reset(new VlanTableReqKey(idx));
        delReq.oper = DBRequest::DB_ENTRY_DELETE;
        itbl->Enqueue(&delReq);
    }
    TASK_UTIL_EXPECT_EQ(num_entries, del_notification);

    // Unregister client
    itbl->Unregister(tid_);
}

TEST_F(DBTest, SkipDelete) {
    // Register client for notification
    tid_ = itbl->Register(boost::bind(&DBTest::DBTestListener, this, _1, _2));
    TASK_UTIL_EXPECT_EQ(tid_, 0);
    EXPECT_EQ(0, itbl->Size());

    DBRequest addReq;
    DBRequest delReq;

    // Add one entry
    addReq.key.reset(new VlanTableReqKey(1));
    addReq.data.reset(new VlanTableReqData("DB Test Vlan"));
    addReq.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    itbl->Enqueue(&addReq);
    TASK_UTIL_EXPECT_EQ(1, itbl->Size());

    // Enqueue request to delete. The Delete() API returns false
    // Entry should not be deleted
    delReq.key.reset(new VlanTableReqKey(1, false));
    delReq.oper = DBRequest::DB_ENTRY_DELETE;
    uint32_t count = itbl->del_req_count();
    itbl->Enqueue(&delReq);
    // Wait till Delete() is invoked
    TASK_UTIL_EXPECT_EQ(count + 1, itbl->del_req_count());
    // DBEntry should not be deleted
    TASK_UTIL_EXPECT_EQ(1, itbl->Size());

    // Delete all entries
    delReq.key.reset(new VlanTableReqKey(1, true));
    delReq.oper = DBRequest::DB_ENTRY_DELETE;
    itbl->Enqueue(&delReq);
    TASK_UTIL_EXPECT_EQ(0, itbl->Size());

    // Unregister client
    itbl->Unregister(tid_);
}

// Find routine tests
TEST_F(DBTest, Find) {
    // Create a VLAN
    DBRequest addReq;
    addReq.key.reset(new VlanTableReqKey(101));
    addReq.data.reset(new VlanTableReqData("DB Test Vlan"));
    addReq.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    itbl->Enqueue(&addReq);
    task_util::WaitForIdle();

    ConcurrencyScope scope("db::DBTable");
    VlanTableReqKey key(101);
    EXPECT_TRUE((static_cast<DBTable *>(itbl))->Find(&key) != NULL);
    EXPECT_TRUE((static_cast<DBTable *>(itbl))->FindNoLock(&key) != NULL);

    Vlan entry(101);
    EXPECT_TRUE((static_cast<DBTable *>(itbl))->Find(&entry) != NULL);
    EXPECT_TRUE((static_cast<DBTable *>(itbl))->FindNoLock(&entry) != NULL);

    // Delete a VLAN
    DBRequest delReq;
    delReq.key.reset(new VlanTableReqKey(101));
    delReq.oper = DBRequest::DB_ENTRY_DELETE;
    itbl->Enqueue(&delReq);
    task_util::WaitForIdle();

    // Clear stats in End
    adc_notification = 0;
    del_notification = 0;
}

void RegisterFactory() {
    DB::RegisterFactory("db.test.vlan.0", &VlanTable::CreateTable);
    DB::RegisterFactory("db.test.vlan.1", &VlanTable::CreateTable);
}

int main(int argc, char **argv) {
    LoggingInit();
    ::testing::InitGoogleTest(&argc, argv);

    RegisterFactory();

    return RUN_ALL_TESTS();
}
