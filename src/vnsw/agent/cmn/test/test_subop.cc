/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "testing/gunit.h"
#include "base/logging.h"
#include <io/event_manager.h>
#include <db/db.h>
#include "cmn/agent_cmn.h"
#include "base/task.h"
#include "test_subop.h"

using namespace std;

TaskScheduler       *scheduler;
static TableC *table_c_;
DB *db_;

void RouterIdDepInit(Agent *agent) {
}

DBEntryBase::KeyPtr EntryC::GetDBRequestKey() const {
    EntryKey *key = new EntryKey(id_);
    return DBEntryBase::KeyPtr(key);
}

void EntryC::SetKey(const DBRequestKey *k) { 
    const EntryKey *key = static_cast<const EntryKey *>(k);
    id_ = key->id_;
}

AgentDBTable *EntryC::DBToTable() const {
    return table_c_;
}

std::auto_ptr<DBEntry> TableC::AllocEntry(const DBRequestKey *k) const {
    const EntryKey *key = static_cast<const EntryKey *>(k);
    EntryC *entry = new EntryC(key->id_);
    return std::auto_ptr<DBEntry>(static_cast<DBEntry *>(entry));
}

DBEntry *TableC::Add(const DBRequest *req) {
    EntryKey *key = static_cast<EntryKey *>(req->key.get());
    EntryC *entry = new EntryC(key->id_);
    OnChange(entry, req);
    add_count_++;
    return entry;
}

bool TableC::OnChange(DBEntry *entry, const DBRequest *req) {
    EntryData *data = static_cast<EntryData *>(req->data.get());

    EntryC *a = static_cast<EntryC *>(entry);
    a->data_ = data->data_;
    change_count_++;
    return true;
}

bool TableC::Resync(DBEntry *entry, DBRequest *req) {
    resync_count_++;
    return OnChange(entry, req);
}

void TableC::Delete(DBEntry *entry, const DBRequest *req) {
    del_count_++;
}

EntryC *TableC::FindC(int id) {
    auto_ptr<EntryKey> key(new EntryKey());
    key->id_ = id;
    return static_cast<EntryC *>(table_c_->FindActiveEntry(key.get()));
}

DBTableBase *TableC::CreateTable(DB *db, const std::string &name) {
    table_c_ = new TableC(db, name);
    table_c_->Init();
    return table_c_;
};

void TableC::Register() {
    DB::RegisterFactory("db.tablea.0", &TableC::CreateTable);
};

class DBTest : public ::testing::Test {
};

class ClientC {
public:
    ClientC() { };

    void DBTestListener(DBTablePartBase *root, DBEntryBase *e) {
        bool del_notify = e->IsDeleted();
        if (del_notify) {
            del_notification++;
        } else {
            adc_notification++;
        }
    }

    void Init(DB *db) {
        table_ = static_cast<TableC *>(db->FindTable("db.tablea.0"));
        assert(table_);
        listener_id_ = table_->Register(boost::bind(&ClientC::DBTestListener, 
                                                    this, _1, _2));
    };

    static void ClearCount() {adc_notification = 0; del_notification = 0;};
    static DBTable *table_;
    static DBTableBase::ListenerId listener_id_;
    static tbb::atomic<long> adc_notification;
    static tbb::atomic<long> del_notification;
};

int EntryC::free_count_;
int TableC::add_count_;

int TableC::change_count_;

int TableC::resync_count_;
int TableC::del_count_;

DBTableBase::ListenerId ClientC::listener_id_;
DBTable *ClientC::table_;
tbb::atomic<long> ClientC::adc_notification;
tbb::atomic<long> ClientC::del_notification;

void init_db_tables() {
    TableC::Register();
}

void create_db_tables(DB *db) {
    table_c_ = static_cast<TableC *>(db->CreateTable("db.tablea.0"));
}

void init_db_clients(DB *db) {
    ClientC *client_a = new ClientC();
    client_a->Init(db);
}

void AddEnqueue(int id, int data) {
    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    req.key.reset(new EntryKey(id));
    req.data.reset(new EntryData(data));
    table_c_->Enqueue(&req);
}

void DelEnqueue(int id) {
    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_DELETE;
    req.key.reset(new EntryKey(id));
    req.data.reset(NULL);
    table_c_->Enqueue(&req);
}

void ResyncEnqueue(int id, int data) {
    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    req.key.reset(new EntryKey(AgentKey::RESYNC, id));
    req.data.reset(new EntryData(data));
    table_c_->Enqueue(&req);
}

void WaitForIdle() {
    static const int kTimeout = 15;
    int i;
    for (i = 0; i < (kTimeout * 1000); i++) {
        if (scheduler->IsEmpty()) {
            break;
        }
        usleep(1000);
    }
    EXPECT_TRUE(i < (kTimeout * 1000));
    EXPECT_TRUE(scheduler->IsEmpty());
}

bool ValidateAdd(int add_count) {
    WaitForIdle();
    bool ret = true;

    EXPECT_EQ(add_count, TableC::add_count_);
    if (add_count != TableC::add_count_)
        ret = false;

    return ret;
}

bool ValidateChange(int chg_count) {
    WaitForIdle();
    bool ret = true;

    EXPECT_EQ(chg_count, TableC::change_count_);
    if (chg_count != TableC::change_count_)
        ret = false;

    return ret;
}

bool ValidateResync(int count) {
    WaitForIdle();
    bool ret = true;

    EXPECT_EQ(count, TableC::resync_count_);
    if (count != TableC::resync_count_)
        ret = false;

    return ret;
}

bool ValidateDelete(int count) {
    WaitForIdle();
    bool ret = true;

    EXPECT_EQ(count, TableC::del_count_);
    if (count != TableC::del_count_)
        ret = false;

    return ret;
}

bool ValidateEntry(int id, int data) {
    WaitForIdle();
    EntryC *entry = table_c_->FindC(id);
    EXPECT_TRUE(entry != NULL);
    if (entry == NULL) {
        return false;
    }

    EXPECT_EQ(entry->data_, data);
    if (entry->data_ != data)
        return false;

    return true;
}

EntryC *FindEntry(int id) {
    WaitForIdle();
    return table_c_->FindC(id);
}

void TestInit() {
    ClientC::ClearCount();
    EntryC::ClearCount();
    TableC::ClearCount();
}

TEST_F(DBTest, Add_0) {
    TestInit();

    AddEnqueue(1, 1);
    EXPECT_TRUE(ValidateAdd(1));
    EXPECT_TRUE(ValidateEntry(1, 1));

    AddEnqueue(1, 2);
    EXPECT_TRUE(ValidateAdd(1));
    EXPECT_TRUE(ValidateEntry(1, 2));

    DelEnqueue(1);
    EXPECT_TRUE(FindEntry(1) == NULL);
    EXPECT_TRUE(ValidateDelete(1));
}

TEST_F(DBTest, Sync_0) {
    TestInit();

    // Dont add entry only on SYNC operation
    ResyncEnqueue(1, 2);
    EXPECT_TRUE(FindEntry(1) == NULL);

    AddEnqueue(1, 1);
    EXPECT_TRUE(ValidateAdd(1));
    EXPECT_TRUE(ValidateEntry(1, 1));

    // SYNC notification expected once entry is added
    TestInit();
    ResyncEnqueue(1, 2);
    EXPECT_TRUE(ValidateAdd(0));
    EXPECT_TRUE(ValidateResync(1));
    EXPECT_TRUE(ValidateChange(1));
    EXPECT_TRUE(ValidateEntry(1, 2));

    // No SYNC for deleted entry
    TestInit();
    DelEnqueue(1);
    ResyncEnqueue(1, 2);
    EXPECT_TRUE(ValidateResync(0));
    EXPECT_TRUE(FindEntry(1) == NULL);
    EXPECT_TRUE(ValidateDelete(1));
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
    scheduler->Terminate();
    return ret;
}
