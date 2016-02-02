/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/intrusive/avl_set.hpp>
#include <boost/functional/hash.hpp>
#include <boost/bind.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <tbb/atomic.h>

#include "db/db.h"
#include "db/db_table.h"
#include "db/db_entry.h"
#include "db/db_client.h"
#include "db/db_partition.h"
#include "db/db_table_walker.h"
#include "base/time_util.h"
#include "base/task.h"
#include "base/test/task_test_util.h"

#include "base/logging.h"
#include "base/task_annotations.h"
#include "base/string_util.h"
#include "testing/gunit.h"

#define FIND_COUNT (40*1000)
class VlanTable;

static boost::uuids::uuid MakeUuid(int id) {
    char str[50];
    sprintf(str, "00000000-0000-0000-0000-00%010x", id);
    return StringToUuid(std::string(str));
}

struct VlanTableReqKey : public DBRequestKey {
    VlanTableReqKey(int id) : uuid_(MakeUuid(id)) {}
    VlanTableReqKey(const boost::uuids::uuid &u) : uuid_(u) {}
    boost::uuids::uuid uuid_;
};

struct VlanTableReqData : public DBRequestData {
    VlanTableReqData(std::string desc) : description_(desc) {};
    std::string description_;
};

class Vlan : public DBEntry {
public:
    Vlan(const boost::uuids::uuid &u) : uuid_(u) { }
    Vlan(int id) : uuid_(MakeUuid(id)) { }
    Vlan(const boost::uuids::uuid &u, std::string desc) :
        uuid_(u), description_(desc) { }

    ~Vlan() {
    }

    bool IsLess(const DBEntry &rhs) const {
        const Vlan &a = static_cast<const Vlan &>(rhs);
        return uuid_ < a.uuid_;
    }

    void SetKey(const DBRequestKey *key) {
        const VlanTableReqKey *k = static_cast<const VlanTableReqKey *>(key);
        uuid_ = k->uuid_;
    }

    std::string ToString() const {
        return "Vlan";
    }

    virtual KeyPtr GetDBRequestKey() const {
        VlanTableReqKey *key = new VlanTableReqKey(uuid_);
        return KeyPtr(key);
    }

    const boost::uuids::uuid &get_uuid() const { return uuid_; }
    void set_description(const std::string &descr) { description_ = descr; }
private:
    boost::uuids::uuid uuid_;
    std::string description_;
    DISALLOW_COPY_AND_ASSIGN(Vlan);
};

class VlanTable : public DBTable {
public:
    VlanTable(DB *db) : DBTable(db, "__vlan__.0") { }
    ~VlanTable() { }

    // Alloc a derived DBEntry
    virtual std::auto_ptr<DBEntry> AllocEntry(const DBRequestKey *key) const {
        const VlanTableReqKey *vkey = static_cast<const VlanTableReqKey *>(key);
        Vlan *vlan = new Vlan(vkey->uuid_);
        return std::auto_ptr<DBEntry>(vlan);
    };

    size_t Hash(const DBEntry *entry) const {
        return 0;
    }

    size_t Hash(const DBRequestKey *key) const {
        return 0;
    }

    virtual DBEntry *Add(const DBRequest *req) {
        const VlanTableReqKey *key = static_cast<const VlanTableReqKey *>
            (req->key.get());
        const VlanTableReqData *data = static_cast<const VlanTableReqData *>
            (req->data.get());
        Vlan *vlan = new Vlan(key->uuid_);
        vlan->set_description(data->description_);
        return vlan;
    };

    virtual bool OnChange(DBEntry *entry, const DBRequest *req) {
        const VlanTableReqData *data = static_cast<const VlanTableReqData *>
            (req->data.get());
        Vlan *vlan = static_cast<Vlan *>(entry);
        vlan->set_description(data->description_);
        return true;
    };

    virtual bool Delete(DBEntry *entry, const DBRequest *req) {
        return true;
    }

    static DBTableBase *CreateTable(DB *db, const std::string &name) {
        VlanTable *table = new VlanTable(db);
        table->Init();
        return table;
    }

    uint64_t FindScale(uint32_t id, uint32_t count, bool use_key, bool do_lock){
        boost::uuids::uuid u = MakeUuid(id);
        uint64_t start = ClockMonotonicUsec();

        if (use_key) {
            for (uint32_t i = 0; i < count; i++) {
                VlanTableReqKey key(u);
                if (do_lock) {
                    assert(Find(&key) != NULL);
                } else {
                    assert(FindNoLock(&key) != NULL);
                }
            }
        } else {
            for (uint32_t i = 0; i < count; i++) {
                Vlan entry(u);
                if (do_lock) {
                    assert(Find(&entry) != NULL);
                } else {
                    assert(FindNoLock(&entry) != NULL);
                }
            }
        }

        return ClockMonotonicUsec() - start;
    }

    DISALLOW_COPY_AND_ASSIGN(VlanTable);
};

class DBTest : public ::testing::Test {
public:
    DBTest() {
        table_ = static_cast<DBTable *>(db_.CreateTable("db.test.vlan.0"));
        vlan_table_ = static_cast<VlanTable *>(table_);
        scheduler_ = TaskScheduler::GetInstance();
    }

    virtual void SetUp() {
        DBRequest addReq;
        addReq.key.reset(new VlanTableReqKey(101));
        addReq.data.reset(new VlanTableReqData("DB Test Vlan"));
        addReq.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
        table_->Enqueue(&addReq);
        task_util::WaitForIdle();
    }

    virtual void TearDown() {
        DBRequest delReq;
        delReq.key.reset(new VlanTableReqKey(101));
        delReq.oper = DBRequest::DB_ENTRY_DELETE;
        table_->Enqueue(&delReq);
        task_util::WaitForIdle();
    }

protected:
    DB db_;
    DBTable *table_;
    VlanTable *vlan_table_;
    TaskScheduler *scheduler_;
};

tbb::atomic<uint32_t> scale_count_;
class ScaleTask : public Task {
public:
    ScaleTask(VlanTable *table, bool use_key, bool do_lock, int thread_count,
              uint32_t find_count) :
        Task(TaskScheduler::GetInstance()->GetTaskId("db::DBTable"), -1),
        table_(table), do_lock_(do_lock), use_key_(use_key),
        thread_count_(thread_count), find_count_(find_count) {
    }
    ~ScaleTask() { }

    bool Run() {
        scale_count_++;
        while (scale_count_ != thread_count_);

        uint64_t delay = table_->FindScale(101, find_count_, use_key_, do_lock_);
        std::cout << "Use-Key " << use_key_ << " Do-Lock " << do_lock_ <<
            " Time is " << delay << " usec" << std::endl;
        return true;
    }
    std::string Description() const { return "ScaleTask"; }
private:
    VlanTable *table_;
    bool do_lock_;
    bool use_key_;
    uint32_t thread_count_;
    uint32_t find_count_;
};

// Find routine tests
TEST_F(DBTest, Find) {
    ConcurrencyScope scope("db::DBTable");
    VlanTableReqKey key(101);
    EXPECT_TRUE(table_->Find(&key) != NULL);
    EXPECT_TRUE(table_->FindNoLock(&key) != NULL);

    Vlan entry(101);
    EXPECT_TRUE(table_->Find(&entry) != NULL);
    EXPECT_TRUE(table_->FindNoLock(&entry) != NULL);
}

// Find routine tests
TEST_F(DBTest, FindScaleTask) {
    ConcurrencyScope scope("db::DBTable");
    uint32_t count = FIND_COUNT;

    uint64_t key_with_lock = vlan_table_->FindScale(101, count,true, true);
    uint64_t key_no_lock = vlan_table_->FindScale(101, count, true, false);
    uint64_t entry_with_lock = vlan_table_->FindScale(101, count, false, true);
    uint64_t entry_no_lock = vlan_table_->FindScale(101, count, false, false);

    std::cout << "Lookup with Key with lock    : " << key_with_lock << " usec"
        << std::endl;
    std::cout << "Lookup with Key no lock      : " << key_no_lock << " usec"
        << std::endl;
    std::cout << "Lookup with Entry with lock  : " << entry_with_lock
        << " usec" << std::endl;
    std::cout << "Lookup with Entry no lock    : " << entry_no_lock
        << " usec" << std::endl;
}

TEST_F(DBTest, VlanScaleTask) {
    ConcurrencyScope scope("db::DBTable");
    scale_count_ = 0;
    uint32_t count = FIND_COUNT/4;

    scheduler_->Enqueue(new ScaleTask(vlan_table_, true, true, 4, count));
    scheduler_->Enqueue(new ScaleTask(vlan_table_, true, true, 4, count));
    scheduler_->Enqueue(new ScaleTask(vlan_table_, true, true, 4, count));
    scheduler_->Enqueue(new ScaleTask(vlan_table_, true, true, 4, count));
    task_util::WaitForIdle();

    scale_count_ = 0;
    scheduler_->Enqueue(new ScaleTask(vlan_table_, true, false, 4, count));
    scheduler_->Enqueue(new ScaleTask(vlan_table_, true, false, 4, count));
    scheduler_->Enqueue(new ScaleTask(vlan_table_, true, false, 4, count));
    scheduler_->Enqueue(new ScaleTask(vlan_table_, true, false, 4, count));
    task_util::WaitForIdle();

    scale_count_ = 0;
    scheduler_->Enqueue(new ScaleTask(vlan_table_, false, true, 4, count));
    scheduler_->Enqueue(new ScaleTask(vlan_table_, false, true, 4, count));
    scheduler_->Enqueue(new ScaleTask(vlan_table_, false, true, 4, count));
    scheduler_->Enqueue(new ScaleTask(vlan_table_, false, true, 4, count));
    task_util::WaitForIdle();

    scale_count_ = 0;
    scheduler_->Enqueue(new ScaleTask(vlan_table_, false, false, 4, count));
    scheduler_->Enqueue(new ScaleTask(vlan_table_, false, false, 4, count));
    scheduler_->Enqueue(new ScaleTask(vlan_table_, false, false, 4, count));
    scheduler_->Enqueue(new ScaleTask(vlan_table_, false, false, 4, count));
    task_util::WaitForIdle();
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
