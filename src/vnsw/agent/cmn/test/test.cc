/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "testing/gunit.h"
#include "base/logging.h"
#include <io/event_manager.h>
#include <db/db.h>
#include "cmn/agent_cmn.h"
#include "base/task.h"
#include "test.h"

using namespace std;

TaskScheduler       *scheduler;
static TableA *table_a_;
static TableB *table_b_;
static TableC *table_c_;
DB *db_;

void RouterIdDepInit(Agent *agent) {
}

DBEntryBase::KeyPtr EntryA::GetDBRequestKey() const {
    EntryKey *key = new EntryKey(id_);
    return DBEntryBase::KeyPtr(key);
}

void EntryA::SetKey(const DBRequestKey *k) { 
    const EntryKey *key = static_cast<const EntryKey *>(k);
    id_ = key->id_;
}

AgentDBTable *EntryA::DBToTable() const {
    return table_a_;
}

DBEntryBase::KeyPtr EntryB::GetDBRequestKey() const {
    EntryKey *key = new EntryKey(id_);
    return DBEntryBase::KeyPtr(key);
}

void EntryB::SetKey(const DBRequestKey *k) { 
    const EntryKey *key = static_cast<const EntryKey *>(k);
    id_ = key->id_;
}

AgentDBTable *EntryB::DBToTable() const {
    return table_b_;
}

std::auto_ptr<DBEntry> TableA::AllocEntry(const DBRequestKey *k) const {
    const EntryKey *key = static_cast<const EntryKey *>(k);
    EntryA *entry = new EntryA(key->id_);
    return std::auto_ptr<DBEntry>(static_cast<DBEntry *>(entry));
}

DBEntry *TableA::Add(const DBRequest *req) {
    EntryKey *key = static_cast<EntryKey *>(req->key.get());
    EntryA *entry = new EntryA(key->id_);
    OnChange(entry, req);
    return entry;
}

bool TableA::OnChange(DBEntry *entry, const DBRequest *req) {
    bool ret = false;
    EntryData *data = static_cast<EntryData *>(req->data.get());

    EntryA *a = static_cast<EntryA *>(entry);
    if (a->data_ != data->data_) {
        a->data_ = data->data_;
        ret = true;
    }

    EntryKey k(data->ref_id_);
    EntryB *b = static_cast<EntryB *>(table_b_->FindActiveEntry(&k));
    if (a->ref_.get() != b) {
        a->ref_ = b;
        ret = true;
    }

    return ret;
}

void TableA::Delete(DBEntry *entry, const DBRequest *req) {
    del_count_++;
}

DBTableBase *TableA::CreateTable(DB *db, const std::string &name) {
    table_a_ = new TableA(db, name);
    table_a_->Init();
    return table_a_;
};

void TableA::Register() {
    DB::RegisterFactory("db.tablea.0", &TableA::CreateTable);
};

EntryB *TableA::FindBRef(const EntryKey &k) const {
    auto_ptr<EntryKey> key(new EntryKey());
    key->id_ = k.id_;
    return static_cast<EntryB *>(table_b_->FindActiveEntry(key.get()));
}

std::auto_ptr<DBEntry> TableB::AllocEntry(const DBRequestKey *k) const {
    const EntryKey *key = static_cast<const EntryKey *>(k);
    EntryB *entry = new EntryB(key->id_);
    return std::auto_ptr<DBEntry>(static_cast<DBEntry *>(entry));
}

DBEntry *TableB::Add(const DBRequest *req) {
    EntryKey *key = static_cast<EntryKey *>(req->key.get());
    EntryB *entry = new EntryB(key->id_);
    OnChange(entry, req);
    return entry;
}

bool TableB::OnChange(DBEntry *entry, const DBRequest *req) {
    bool ret = false;
    EntryData *data = static_cast<EntryData *>(req->data.get());
    EntryB *b = static_cast<EntryB *>(entry);

    if (b->data_ != data->data_) {
        b->data_ = data->data_;
        ret = true;
    }

    return ret;
}

void TableB::Delete(DBEntry *entry, const DBRequest *req) {
    del_count_++;
}

DBTableBase *TableB::CreateTable(DB *db, const std::string &name) {
    table_b_ = new TableB(db, name);
    table_b_->Init();
    return table_b_;
}

void TableB::Register() {
    DB::RegisterFactory("db.tableb.0", &TableB::CreateTable);
}

EntryA *TableB::FindA(const EntryKey &k) const {
    auto_ptr<EntryKey> key(new EntryKey());
    key->id_ = k.id_;
    return static_cast<EntryA *>(table_b_->FindActiveEntry(key.get()));
}

class DBTest : public ::testing::Test {
};

class ClientA {
public:
    ClientA() { };

    void DBTestListener(DBTablePartBase *root, DBEntryBase *e) {
        bool del_notify = e->IsDeleted();
        if (del_notify) {
            del_notification++;
        } else {
            adc_notification++;
        }
    }

    void Init(DB *db) {
        table_ = static_cast<TableA *>(db->FindTable("db.tablea.0"));
        assert(table_);
        listener_id_ = table_->Register(boost::bind(&ClientA::DBTestListener, 
                                                    this, _1, _2));
    };

    static void ClearCount() {adc_notification = 0; del_notification = 0;};
    static DBTable *table_;
    static DBTableBase::ListenerId listener_id_;
    static tbb::atomic<long> adc_notification;
    static tbb::atomic<long> del_notification;
};

class ClientB {
public:
    ClientB() { };

    void DBTestListener(DBTablePartBase *root, DBEntryBase *e) {
        bool del_notify = e->IsDeleted();
        if (del_notify) {
            del_notification++;
        } else {
            adc_notification++;
        }
    }

    void Init(DB *db) {
        table_ = static_cast<TableA *>(db->FindTable("db.tableb.0"));
        assert(table_);
        listener_id_ = table_->Register(boost::bind(&ClientB::DBTestListener, 
                                                    this, _1, _2));
    };

    static void ClearCount() {adc_notification = 0; del_notification = 0;};
    static DBTable *table_;
    static DBTableBase::ListenerId listener_id_;
    static tbb::atomic<long> adc_notification;
    static tbb::atomic<long> del_notification;
};

int EntryA::free_count_;
int EntryB::free_count_;
int TableA::del_count_;
int TableB::del_count_;

DBTableBase::ListenerId ClientA::listener_id_;
DBTableBase::ListenerId ClientB::listener_id_;
DBTable *ClientA::table_;
DBTable *ClientB::table_;
tbb::atomic<long> ClientA::adc_notification;
tbb::atomic<long> ClientA::del_notification;
tbb::atomic<long> ClientB::adc_notification;
tbb::atomic<long> ClientB::del_notification;

std::auto_ptr<ClientA> client_a;
std::auto_ptr<ClientB> client_b;

void init_db_tables() {
    TableA::Register();
    TableB::Register();
    TableC::Register();
}

void create_db_tables(DB *db) {
    table_a_ = static_cast<TableA *>(db->CreateTable("db.tablea.0"));
    table_b_ = static_cast<TableB *>(db->CreateTable("db.tableb.0"));
    table_c_ = static_cast<TableC *>(db->CreateTable("db.tablec.0"));
}

void init_db_clients(DB *db) {
    client_a.reset(new ClientA());
    client_b.reset(new ClientB());

    client_a->Init(db);
    client_b->Init(db);
}

void AddEnqueue(DBTable *table, int id, int data, int ref_id) {
    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    req.key.reset(new EntryKey(id));
    req.data.reset(new EntryData(data, ref_id));
    table->Enqueue(&req);
}

void DelEnqueue(DBTable *table, int id, int data, int ref_id) {
    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_DELETE;
    req.key.reset(new EntryKey(id));
    req.data.reset(new EntryData(data, ref_id));
    table->Enqueue(&req);
}

EntryA *FindA(int id) {
    EntryKey key(id);
    return static_cast<EntryA *>(table_a_->FindActiveEntry(&key));
}

EntryA *FindA(int id, bool del) {
    EntryKey key(id);
    return static_cast<EntryA *>(table_a_->Find(&key, del));
}

EntryB *FindB(int id) {
    EntryKey key(id);
    return static_cast<EntryB *>(table_b_->FindActiveEntry(&key));
}

void WaitForIdle() {
    static const int kTimeout = 15;
    for (int i = 0; i < (kTimeout * 1000); i++) {
        if (scheduler->IsEmpty()) {
            usleep(1000);
            if (scheduler->IsEmpty()) {
                break;
            }
        }
        usleep(1000);
    }
    EXPECT_TRUE(scheduler->IsEmpty());
}

bool ValidateA(int id, int refcount, int del_count, int free_count,
               int adc_notify_count, int del_notify_count) {
    EntryA *entrya;
    bool ret = true;

    WaitForIdle();
    if (adc_notify_count > 0) {
        int i = 0;
        while(i++ < 10) {
            if (adc_notify_count == ClientA::adc_notification)
                break;
            usleep(1000);
        }
        EXPECT_EQ(adc_notify_count,ClientA::adc_notification);
        if (adc_notify_count != ClientA::adc_notification)
            ret = false;
    }

    if (del_notify_count > 0) {
        int i = 0;
        while(i++ < 10) {
            if (del_notify_count == ClientA::del_notification)
                break;
            usleep(1000);
        }
        EXPECT_EQ(del_notify_count, ClientA::del_notification);
        if (del_notify_count != ClientA::del_notification)
            ret = false;
    }

    entrya = FindA(id);
    if (entrya) {
        EXPECT_EQ(refcount, (int)entrya->GetRefCount());
        if (refcount != (int)entrya->GetRefCount())
            ret = false;
    }

    if (del_count >= 0) {
        EXPECT_EQ(del_count, TableA::del_count_);
        if (del_count != TableA::del_count_)
            ret = false;
    }

    if (free_count >= 0) {
        EXPECT_EQ(free_count, EntryA::free_count_);
        if (free_count != EntryA::free_count_)
            ret = false;
    }

    return ret;
}

bool ValidateB(int id, int refcount, int del_count, int free_count, 
               int adc_notify_count, int del_notify_count) {
    EntryB *entryb;
    bool ret = true;

    WaitForIdle();
    if (adc_notify_count < 0) {
        int i = 0;
        while(i++ < 10) {
            if (adc_notify_count == ClientB::adc_notification)
                break;
            usleep(1000);
        }
        EXPECT_EQ(adc_notify_count, ClientB::adc_notification);
        if (adc_notify_count != ClientB::adc_notification)
            ret = false;
    }

    if (del_notify_count < 0) {
        int i = 0;
        while(i++ < 10) {
            if (del_notify_count == ClientB::del_notification)
                break;
            usleep(1000);
        }
        EXPECT_EQ(del_notify_count, ClientB::del_notification);
        if(del_notify_count != ClientB::del_notification)
            ret = false;
    }

    entryb = FindB(id);
    if (entryb) {
        EXPECT_EQ(refcount, (int)entryb->GetRefCount());
        if (refcount != (int)entryb->GetRefCount())
            ret = false;
    }

    if (del_count >= 0) {
        EXPECT_EQ(del_count, TableB::del_count_);
        if (del_count != TableB::del_count_)
            ret = false;
    }

    if (free_count >= 0) {
        EXPECT_EQ(free_count, EntryB::free_count_);
        if (free_count != EntryB::free_count_)
            ret = false;
    }

    return ret;
}

void TestInit() {
    ClientA::ClearCount();
    ClientB::ClearCount();
    EntryA::ClearCount();
    EntryB::ClearCount();
    TableA::ClearCount();
    TableB::ClearCount();
}

// Add Entry-B, Add Entry-A, Del Entry-B, Del Entry-A
TEST_F(DBTest, TestRef_RefWait_0) {
    TestInit();

    AddEnqueue(table_b_, 1, 1, 1);
    EXPECT_TRUE(ValidateB(1, 0, 0, 0, 1, 0));
    EXPECT_TRUE(ValidateA(0, 0, 0, 0, 0, 0));

    AddEnqueue(table_a_, 1, 1, 1);
    EXPECT_TRUE(ValidateA(1, 0, 0, 0, 1, 0));
    EXPECT_TRUE(ValidateB(1, 1, 0, 0, 1, 0));

    DelEnqueue(table_b_, 1, 1, 1);
    EXPECT_TRUE(ValidateA(1, 0, 0, 0, 1, 0));
    // B not freed since A still referring to it
    EXPECT_TRUE(ValidateB(1, 1, 1, 0, 1, 1));

    DelEnqueue(table_a_, 1, 1, 1);
    EXPECT_TRUE(ValidateA(1, 0, 1, 1, 1, 1));
    // B freed since A not referring anymore
    EXPECT_TRUE(ValidateB(1, 1, 1, 1, 1, 1));
}

TEST_F(DBTest, TestRef_NoRef_0) {
    TestInit();

    AddEnqueue(table_b_, 1, 1, 1);
    EXPECT_TRUE(ValidateB(1, 0, 0, 0, 1, 0));
    EXPECT_TRUE(ValidateA(0, 0, 0, 0, 0, 0));

    AddEnqueue(table_a_, 1, 1, 1);
    EXPECT_TRUE(ValidateA(1, 0, 0, 0, 1, 0));
    EXPECT_TRUE(ValidateB(1, 1, 0, 0, 1, 0));

    DelEnqueue(table_a_, 1, 1, 1);
    EXPECT_TRUE(ValidateA(1, 0, 1, 1, 1, 1));
    EXPECT_TRUE(ValidateB(1, 0, 0, 0, 1, 0));

    DelEnqueue(table_b_, 1, 1, 1);
    EXPECT_TRUE(ValidateA(1, 0, 1, 1, 1, 1));
    EXPECT_TRUE(ValidateB(1, 0, 1, 1, 1, 1));
}

void AddEnqueueTableC(DBTable *table, int id, int id_a) {
    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    req.key.reset(new EntryCKey(id, id_a));
    req.data.reset(NULL);
    table->Enqueue(&req);
}

TEST_F(DBTest, TestRef_RemoveQ_1) {
    TestInit();

    // Add entry
    AddEnqueue(table_a_, 1, 1, 1);
    WaitForIdle();

    EXPECT_TRUE(FindA(1) != NULL);

    // Add reference to entry
    AddEnqueueTableC(table_c_, 1, 1);
    WaitForIdle();

    // Delete entry. Its not freed since it has reference
    DelEnqueue(table_a_, 1, 1, 1);
    WaitForIdle();
    EXPECT_TRUE(FindA(1, true) != NULL);

    // Remove reference. entry goes to RemoveQ
    AddEnqueueTableC(table_c_, 1, 0);

    // Renew entry
    AddEnqueue(table_a_, 1, 1, 1);
    WaitForIdle();
    FindA(1);

    // Add reference to entry again
    AddEnqueueTableC(table_c_, 1, 1);

    // Delete the entry. Entry will still be in removeq
    DelEnqueue(table_a_, 1, 1, 1);
    WaitForIdle();

    table_c_->ref = NULL;
    WaitForIdle();
}

TEST_F(DBTest, TestRef_RemoveQ_2) {
    TestInit();

    AddEnqueue(table_a_, 1, 1, 1);
    WaitForIdle();

    FindA(1);
    EXPECT_TRUE(FindA(1) != NULL);

    AddEnqueueTableC(table_c_, 1, 1);
    WaitForIdle();

    DelEnqueue(table_a_, 1, 1, 1);
    WaitForIdle();
    EXPECT_TRUE(FindA(1, true) != NULL);

    // Enqueue request to table-b. It will remove 
    AddEnqueueTableC(table_c_, 1, 0);
    for (int i = 0; i < 40; i++) {
        DelEnqueue(table_a_, 1, 1, 1);
    }
    WaitForIdle();

    DelEnqueue(table_c_, 1, 1, 1);
    WaitForIdle();
}

int main(int argc, char *argv[]) {
    LoggingInit();
    ::testing::InitGoogleTest(&argc, argv);
    scheduler = TaskScheduler::GetInstance();
    db_ = new DB();
    init_db_tables();
    create_db_tables(db_);
    init_db_clients(db_);

    int ret = RUN_ALL_TESTS();
    Sandesh::Uninit();
    scheduler->Terminate();
    delete db_;
    return ret;
}
