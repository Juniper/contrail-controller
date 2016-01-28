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

#include <cmn/agent_cmn.h>
#include <cmn/index_vector.h>
#include <oper_db.h>
#include "test/test_cmn_util.h"

#define FIND_COUNT (40 * 1000)

struct VlanTableReqKey : public AgentOperDBKey {
    VlanTableReqKey(int id) : uuid_(MakeUuid(id)) {}
    VlanTableReqKey(const boost::uuids::uuid &u) : uuid_(u) {}
    boost::uuids::uuid uuid_;
};

struct VlanTableReqData : public AgentOperDBData {
    VlanTableReqData(std::string desc) :
        AgentOperDBData(Agent::GetInstance(), NULL), description_(desc) {}
    std::string description_;
};

class Vlan : public AgentRefCount<Vlan>, public AgentOperDBEntry {
public:
    Vlan(const boost::uuids::uuid &u) : type_(0), uuid_(u) { }
    Vlan(int id) : type_(0), uuid_(MakeUuid(id)) { }
    Vlan(const boost::uuids::uuid &u, std::string desc) :
        type_(0), uuid_(u), description_(desc) { }

    ~Vlan() {
    }

    bool DBEntrySandesh(Sandesh *sresp, std::string &name) const {
        return true;
    }
    uint32_t GetRefCount() const {
        return AgentRefCount<Vlan>::GetRefCount();
    }
    bool IsLess(const DBEntry &rhs) const {
        const Vlan &intf = static_cast<const Vlan &>(rhs);
        if (type_ != intf.type_) {
            return type_ < intf.type_;
        }

        return Cmp(rhs);
    }

    virtual bool Cmp(const DBEntry &rhs) const {
        const Vlan &intf=static_cast<const Vlan &>(rhs);
        return uuid_ < intf.uuid_;
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
    int type_;
    boost::uuids::uuid uuid_;
    std::string description_;
    DISALLOW_COPY_AND_ASSIGN(Vlan);
};

class VlanTable : public AgentOperDBTable {
public:
    VlanTable(DB *db) : AgentOperDBTable(db, "__vlan__.0") { }
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

    DBEntry *OperDBAdd(const DBRequest *req) {
        const VlanTableReqKey *key = static_cast<const VlanTableReqKey *>
            (req->key.get());
        const VlanTableReqData *data = static_cast<const VlanTableReqData *>
            (req->data.get());
        Vlan *vlan = new Vlan(key->uuid_);
        vlan->set_description(data->description_);
        return vlan;
    };

    bool OperDBOnChange(DBEntry *entry, const DBRequest *req) {
        return true;
    };

    bool OperDBDelete(DBEntry *entry, const DBRequest *req) {
        return true;
    }

    static DBTableBase *CreateTable(DB *db, const std::string &name) {
        VlanTable *table = new VlanTable(db);
        table->Init();
        return table;
    }

    uint64_t FindScale(int id, uint32_t count, bool use_key, bool do_lock){
        boost::uuids::uuid u = MakeUuid(id);
        uint64_t start = ClockMonotonicUsec();
        if (use_key) {
            for (uint32_t i = 0; i < count; i++) {
                VlanTableReqKey key(u);
                if (do_lock) {
                    assert(DBTable::Find(&key) != NULL);
                } else {
                    assert(DBTable::FindNoLock(&key) != NULL);
                }
            }
        } else {
            for (uint32_t i = 0; i < count; i++) {
                Vlan entry(u);
                if (do_lock) {
                    assert(DBTable::Find(&entry) != NULL);
                } else {
                    assert(DBTable::FindNoLock(&entry) != NULL);
                }
            }
        }

        return ClockMonotonicUsec() - start;
    }

    DISALLOW_COPY_AND_ASSIGN(VlanTable);
};

struct PortInfo input[] = {
    {"vnet1", 1, "1.1.1.1", "00:00:01:01:01:01", 1, 1},
};

class DBTest : public ::testing::Test {
public:
    DBTest() {
        table_ = static_cast<DBTable *>(db_.CreateTable("db.test.vlan.0"));
        vlan_table_ = static_cast<VlanTable *>(table_);
        scheduler_ = TaskScheduler::GetInstance();
    }

    virtual void SetUp() {
        agent_ = Agent::GetInstance();

        DBRequest addReq;
        addReq.key.reset(new VlanTableReqKey(101));
        addReq.data.reset(new VlanTableReqData("DB Test Vlan"));
        addReq.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
        table_->Enqueue(&addReq);
        task_util::WaitForIdle();

        CreateVmportEnv(input, 1);
        task_util::WaitForIdle();
    }

    virtual void TearDown() {
        DBRequest delReq;
        delReq.key.reset(new VlanTableReqKey(101));
        delReq.oper = DBRequest::DB_ENTRY_DELETE;
        table_->Enqueue(&delReq);
        task_util::WaitForIdle();

        DeleteVmportEnv(input, 1, true);
        task_util::WaitForIdle();
    }

protected:
    DB db_;
    DBTable *table_;
    VlanTable *vlan_table_;
    Agent *agent_;
    TaskScheduler *scheduler_;
};

tbb::atomic<uint32_t> scale_count_;
class ScaleTask : public Task {
public:
    ScaleTask(VlanTable *table, bool use_key, bool do_lock, int thread_count,
              uint32_t find_count) :
        Task(TaskScheduler::GetInstance()->GetTaskId(kTaskFlowEvent), -1),
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
    ConcurrencyScope scope(kTaskFlowEvent);
    VlanTableReqKey key(101);
    EXPECT_TRUE(table_->Find(&key) != NULL);
    EXPECT_TRUE(table_->FindNoLock(&key) != NULL);

    Vlan entry(101);
    EXPECT_TRUE(table_->Find(&entry) != NULL);
    EXPECT_TRUE(table_->FindNoLock(&entry) != NULL);
}

// Find routine tests
TEST_F(DBTest, VlanScaleNoTask) {
    ConcurrencyScope scope(kTaskFlowEvent);
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
    ConcurrencyScope scope(kTaskFlowEvent);
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

static uint64_t FindInterfaceScale(DBTable *table, uint32_t id,
                                   uint32_t count, bool use_key, bool do_lock){
    uint64_t start = ClockMonotonicUsec();
    boost::uuids::uuid u = MakeUuid(id);

    if (use_key) {
        for (uint32_t i = 0; i < count; i++) {
            VmInterfaceKey key(VmInterfaceKey(AgentKey::RESYNC, u, ""));
            if (do_lock) {
                assert(table->Find(&key) != NULL);
            } else {
                assert(table->FindNoLock(&key) != NULL);
            }
        }
    } else {
        for (uint32_t i = 0; i < count; i++) {
            VmInterface entry(u);
            if (do_lock) {
                assert(table->Find(&entry) != NULL);
            } else {
                assert(table->FindNoLock(&entry) != NULL);
            }
        }
    }

    return ClockMonotonicUsec() - start;
}

class ScaleInterfaceTask : public Task {
public:
    ScaleInterfaceTask(DBTable *table, bool use_key, bool do_lock,
                       int thread_count, uint32_t find_count) :
        Task(TaskScheduler::GetInstance()->GetTaskId(kTaskFlowEvent), -1),
        table_(table), do_lock_(do_lock), use_key_(use_key),
        thread_count_(thread_count), find_count_(find_count) {
    }
    ~ScaleInterfaceTask() { }

    bool Run() {
        scale_count_++;
        while (scale_count_ != thread_count_);
        uint64_t delay = FindInterfaceScale(table_, 1, find_count_, use_key_, do_lock_);
        std::cout << "Use-Key " << use_key_ << " Do-Lock " << do_lock_ <<
            " Time is " << delay << " usec" << std::endl;
        return true;
    }
    std::string Description() const { return "ScaleInterfaceTask"; }
private:
    DBTable *table_;
    bool do_lock_;
    bool use_key_;
    uint32_t thread_count_;
    uint32_t find_count_;
};

TEST_F(DBTest, ScaleVmInterface) {
    ConcurrencyScope scope(kTaskFlowUpdate);
    DBTable *table =
        static_cast<DBTable *>(Agent::GetInstance()->interface_table());
    uint32_t count = FIND_COUNT;

    uint64_t key_with_lock = FindInterfaceScale(table, 1, count, true, true);
    uint64_t key_no_lock = FindInterfaceScale(table, 1, count, true, false);
    uint64_t entry_with_lock = FindInterfaceScale(table, 1, count, false, true);
    uint64_t entry_no_lock = FindInterfaceScale(table, 1, count, false, false);

    std::cout << "Lookup with Key with lock    : " << key_with_lock << " usec"
        << std::endl;
    std::cout << "Lookup with Key no lock      : " << key_no_lock << " usec"
        << std::endl;
    std::cout << "Lookup with Entry with lock  : " << entry_with_lock
        << " usec" << std::endl;
    std::cout << "Lookup with Entry no lock    : " << entry_no_lock
        << " usec" << std::endl;
}

TEST_F(DBTest, ScaleTaskVmInterface) {
    ConcurrencyScope scope(kTaskFlowUpdate);
    scale_count_ = 0;
    uint32_t count = FIND_COUNT/4;
    DBTable *table =
        static_cast<DBTable *>(Agent::GetInstance()->interface_table());

    scheduler_->Enqueue(new ScaleInterfaceTask(table, true, true, 4, count));
    scheduler_->Enqueue(new ScaleInterfaceTask(table, true, true, 4, count));
    scheduler_->Enqueue(new ScaleInterfaceTask(table, true, true, 4, count));
    scheduler_->Enqueue(new ScaleInterfaceTask(table, true, true, 4, count));
    task_util::WaitForIdle();

    scale_count_ = 0;
    scheduler_->Enqueue(new ScaleInterfaceTask(table, true, false, 4, count));
    scheduler_->Enqueue(new ScaleInterfaceTask(table, true, false, 4, count));
    scheduler_->Enqueue(new ScaleInterfaceTask(table, true, false, 4, count));
    scheduler_->Enqueue(new ScaleInterfaceTask(table, true, false, 4, count));
    task_util::WaitForIdle();

    scale_count_ = 0;
    scheduler_->Enqueue(new ScaleInterfaceTask(table, false, true, 4, count));
    scheduler_->Enqueue(new ScaleInterfaceTask(table, false, true, 4, count));
    scheduler_->Enqueue(new ScaleInterfaceTask(table, false, true, 4, count));
    scheduler_->Enqueue(new ScaleInterfaceTask(table, false, true, 4, count));
    task_util::WaitForIdle();

    scale_count_ = 0;
    scheduler_->Enqueue(new ScaleInterfaceTask(table, false, false, 4, count));
    scheduler_->Enqueue(new ScaleInterfaceTask(table, false, false, 4, count));
    scheduler_->Enqueue(new ScaleInterfaceTask(table, false, false, 4, count));
    scheduler_->Enqueue(new ScaleInterfaceTask(table, false, false, 4, count));
    task_util::WaitForIdle();
}

void RegisterFactory() {
    DB::RegisterFactory("db.test.vlan.0", &VlanTable::CreateTable);
}

int main(int argc, char **argv) {
    GETUSERARGS();
    ::testing::InitGoogleTest(&argc, argv);
    RegisterFactory();
    client = TestInit(init_file, ksync_init, false, false, false);
    bool ret = RUN_ALL_TESTS();
    usleep(10000);
    TestShutdown();
    delete client;
    return ret;
}
