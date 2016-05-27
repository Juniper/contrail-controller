/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <vector>
#include <tbb/atomic.h>
#include <tbb/spin_rw_mutex.h>

#include <boost/bind.hpp>
#include <boost/foreach.hpp>
#include <boost/dynamic_bitset.hpp>
#include <boost/type_traits.hpp>

#include "base/compiler.h"
#include "base/logging.h"
#include "base/task_annotations.h"
#include "base/time_util.h"
#include "db/db.h"
#include "db/db_partition.h"
#include "db/db_table.h"
#include "db/db_table_partition.h"
#include "db/db_table_walk_mgr.h"
#include "db/db_types.h"

class DBEntry;
class DBEntryBase;

using namespace std;

DBRequest::DBRequest() : oper(static_cast<DBOperation>(0)) {
}

DBRequest::~DBRequest() {
#if defined(__GNUC__) && (__GNUC_PREREQ(4, 2) > 0)
    boost::has_virtual_destructor<DBRequestKey>::type key_has_destructor;
    boost::has_virtual_destructor<DBRequestData>::type data_has_destructor;
    assert(key_has_destructor && data_has_destructor);
#endif
}

void DBRequest::Swap(DBRequest *rhs) {
    swap(oper, rhs->oper);
    swap(key, rhs->key);
    swap(data, rhs->data);
}

class DBTableBase::ListenerInfo {
public:
    typedef vector<ChangeCallback> CallbackList;
    typedef vector<string> NameList;
    typedef vector<tbb::atomic<uint64_t> > StateCountList;

    explicit ListenerInfo(const string &table_name) :
        db_state_accounting_(true) {
        if (table_name.find("__ifmap_") != string::npos) {
            // TODO need to have unconditional DB state accounting
            // for now skipp DB State accounting for ifmap tables
            db_state_accounting_ = false;
        }
    }

    DBTableBase::ListenerId Register(ChangeCallback callback,
        const string &name) {
        tbb::spin_rw_mutex::scoped_lock write_lock(rw_mutex_, true);
        size_t i = bmap_.find_first();
        if (i == bmap_.npos) {
            i = callbacks_.size();
            callbacks_.push_back(callback);
            names_.push_back(name);
            state_count_.resize(i + 1);
            state_count_[i] = 0;
        } else {
            bmap_.reset(i);
            if (bmap_.none()) {
                bmap_.clear();
            }
            callbacks_[i] = callback;
            names_[i] = name;
            state_count_[i] = 0;
        }
        return i;
    }

    void Unregister(ListenerId listener) {
        tbb::spin_rw_mutex::scoped_lock write_lock(rw_mutex_, true);
        callbacks_[listener] = NULL;
        names_[listener] = "";
        // During Unregister Listener should have cleaned up,
        // DB states from all the entries in this table.
        assert(state_count_[listener] == 0);
        if ((size_t) listener == callbacks_.size() - 1) {
            while (!callbacks_.empty() && callbacks_.back() == NULL) {
                callbacks_.pop_back();
                names_.pop_back();
                state_count_.pop_back();
            }
            if (bmap_.size() > callbacks_.size()) {
                bmap_.resize(callbacks_.size());
            }
        } else {
            if ((size_t) listener >= bmap_.size()) {
                bmap_.resize(listener + 1);
            }
            bmap_.set(listener);
        }
    }

    // concurrency: called from DBPartition task.
    void RunNotify(DBTablePartBase *tpart, DBEntryBase *entry) {
        tbb::spin_rw_mutex::scoped_lock read_lock(rw_mutex_, false);
        for (CallbackList::iterator iter = callbacks_.begin();
             iter != callbacks_.end(); ++iter) {
            if (*iter != NULL) {
                ChangeCallback cb = *iter;
                (cb)(tpart, entry);
            }
        }
    }

    void AddToDBStateCount(ListenerId listener, int count) {
        if (db_state_accounting_ && listener != DBTableBase::kInvalidId) {
            state_count_[listener] += count;
        }
    }

    uint64_t GetDBStateCount(ListenerId listener) {
        assert(db_state_accounting_ && listener != DBTableBase::kInvalidId);
        return state_count_[listener];
    }

    void FillListeners(vector<ShowTableListener> *listeners) const {
        tbb::spin_rw_mutex::scoped_lock read_lock(rw_mutex_, false);
        ListenerId id = 0;
        for (CallbackList::const_iterator iter = callbacks_.begin();
             iter != callbacks_.end(); ++iter, ++id) {
            if (*iter != NULL) {
                ShowTableListener item;
                item.id = id;
                item.name = names_[id];
                item.state_count = state_count_[id];
                listeners->push_back(item);
            }
        }
    }

    bool empty() const {
        tbb::spin_rw_mutex::scoped_lock read_lock(rw_mutex_, false);
        return callbacks_.empty(); 
    }

    size_t size() const {
        tbb::spin_rw_mutex::scoped_lock read_lock(rw_mutex_, false);
        return (callbacks_.size() - bmap_.count());
    }

private:
    bool db_state_accounting_;
    CallbackList callbacks_;
    NameList names_;
    StateCountList state_count_;
    mutable tbb::spin_rw_mutex rw_mutex_;
    boost::dynamic_bitset<> bmap_;      // free list.
};

DBTableBase::DBTableBase(DB *db, const string &name)
        : db_(db), name_(name), info_(new ListenerInfo(name)),
          enqueue_count_(0), input_count_(0), notify_count_(0) {
    walker_count_ = 0;
    walk_request_count_ = 0;
    walk_complete_count_ = 0;
    walk_cancel_count_ = 0;
}

DBTableBase::~DBTableBase() {
}

DBTableBase::ListenerId DBTableBase::Register(ChangeCallback callback,
    const string &name) {
    return info_->Register(callback, name);
}

void DBTableBase::Unregister(ListenerId listener) {
    info_->Unregister(listener);
    // If a table is marked for deletion, then we may trigger the deletion
    // process when the last client is removed
    if (info_->empty())
        RetryDelete();
}

bool DBTableBase::Enqueue(DBRequest *req) {
    DBTablePartBase *tpart = GetTablePartition(req->key.get());
    DBPartition *partition = db_->GetPartition(tpart->index());
    enqueue_count_++;
    return partition->EnqueueRequest(tpart, NULL, req);
}

void DBTableBase::EnqueueRemove(DBEntryBase *db_entry) {
    DBTablePartBase *tpart = GetTablePartition(db_entry);
    DBPartition *partition = db_->GetPartition(tpart->index());
    partition->EnqueueRemove(tpart, db_entry);
}

void DBTableBase::RunNotify(DBTablePartBase *tpart, DBEntryBase *entry) {
    notify_count_++;
    info_->RunNotify(tpart, entry);
}

void DBTableBase::AddToDBStateCount(ListenerId listener, int count) {
    info_->AddToDBStateCount(listener, count);
}

uint64_t DBTableBase::GetDBStateCount(ListenerId listener) {
    return info_->GetDBStateCount(listener);
}

bool DBTableBase::MayDelete() const {
    if (HasListeners()) {
        return false;
    }
    if (HasWalkers()) {
        return false;
    }
    if (!empty()) {
        return false;
    }

    return true;
}

bool DBTableBase::HasListeners() const {
    return !info_->empty();
}

size_t DBTableBase::GetListenerCount() const {
    return info_->size();
}

void DBTableBase::FillListeners(vector<ShowTableListener> *listeners) const {
    info_->FillListeners(listeners);
}

int DBTable::walker_task_id_ = -1;
int DBTable::max_iteration_to_yield_ = kIterationToYield;

class DBTable::WalkWorker : public Task {
public:
    WalkWorker(TableWalker *walker, int db_partition_id);

    virtual bool Run();

    std::string Description() const { return "DBTable::WalkWorker"; }

private:
    // Store the last visited node to continue walk
    std::auto_ptr<DBRequestKey> walk_ctx_;

    // Table partition for which this worker was created
    DBTablePartition *tbl_partition_;

    TableWalker *walker_;
};

class DBTable::TableWalker {
public:
    TableWalker(DBTable *table) : table_(table) {
    }

    void WalkTable(DBTableWalkMgr::WalkReqList &list);

    bool InvokeWalkCb(DBTablePartBase *part, DBEntryBase *entry);

    void WalkDone() {
        DBTableWalkMgr *walker = table_->database()->GetWalkMgr();
        req_list_.clear();
        walker->WalkDone();
    }

    DBTable *table() {
        return table_;
    }

    DBTable *table_;
    DBTableWalkMgr::WalkReqList req_list_;
    // check whether iteraton is completed on all Table Partition
    tbb::atomic<long> status_;
};

bool DBTable::WalkWorker::Run() {
    int count = 0;
    DBRequestKey *key_resume;

    // Check where we left in last iteration
    if ((key_resume = walk_ctx_.get()) == NULL) {
        // First time invoke of worker thread, start from beginning
        key_resume = NULL;
    }

    DBEntry *entry;
    if (key_resume != NULL) {
        DBTable *table = walker_->table();
        std::auto_ptr<const DBEntryBase> start;
        start = table->AllocEntry(key_resume);
        // Find matching or next in sort order
        entry = tbl_partition_->lower_bound(start.get());
    } else {
        entry = tbl_partition_->GetFirst();
    }
    if (entry == NULL) {
        goto walk_done;
    }

    for (DBEntry *next = NULL; entry; entry = next) {
        next = tbl_partition_->GetNext(entry);
        if (count == GetIterationToYield()) {
            // store the context
            walk_ctx_ = entry->GetDBRequestKey();
            return false;
        }

        // Invoke walker function
        bool more = walker_->InvokeWalkCb(tbl_partition_, entry);
        if (!more) {
            break;
        }

        db_walker_wait();
        count++;
    }

walk_done:
    // Check whether all other walks on the table is completed
    long num_walkers_on_tpart = walker_->status_.fetch_and_decrement();
    if (num_walkers_on_tpart == 1) {
        walker_->WalkDone();
    }
    return true;
}

DBTable::WalkWorker::WalkWorker(TableWalker *walker, int db_partition_id)
    : Task(walker_task_id_, db_partition_id), walker_(walker) {
    tbl_partition_ = static_cast<DBTablePartition *>
        (walker_->table()->GetTablePartition(db_partition_id));
}

void DBTable::TableWalker::WalkTable(DBTableWalkMgr::WalkReqList &list) {
    int num_worker = table_->PartitionCount();
    status_ = num_worker;
    req_list_ = list;
    for (int i = 0; i < num_worker; i++) {
        WalkWorker *task = new WalkWorker(this, i);
        TaskScheduler *scheduler = TaskScheduler::GetInstance();
        scheduler->Enqueue(task);
    }
}

bool DBTable::TableWalker::InvokeWalkCb(DBTablePartBase *part,
                                            DBEntryBase *entry) {
    uint32_t count = 0;
    BOOST_FOREACH(DBTableWalkMgr::DBTableWalkRef walker, req_list_) {
        if (walker->done() || walker->stopped()) continue;
        bool more = walker->walk_fn()(part, entry);
        if (!more) {
            count++;
            walker->set_walk_done();
        }
    }
    return (count < req_list_.size());
}

///////////////////////////////////////////////////////////
// Implementation of DBTable methods
///////////////////////////////////////////////////////////
DBTable::DBTable(DB *db, const string &name)
    : DBTableBase(db, name),
      walker_(new TableWalker(this)) {
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    walker_task_id_ = scheduler->GetTaskId("db::DBTable");
    DBTableWalkMgr *walker = database()->GetWalkMgr();
    walk_ref_= walker->AllocWalker(this,
        boost::bind(&DBTable::WalkCallback, this, _1, _2),
        boost::bind(&DBTable::WalkCompleteCallback, this, _1));
}

DBTable::~DBTable() {
    STLDeleteValues(&partitions_);
}

void DBTable::Init() {
    for (int i = 0; i < PartitionCount(); i++) {
        partitions_.push_back(AllocPartition(i));
    }
}

DBTablePartition *DBTable::AllocPartition(int index) {
    return new DBTablePartition(this, index);
}

void DBTable::WalkTable(DBTableWalkMgr::WalkReqList &list) {
    walker_->WalkTable(list);
}

DBEntry *DBTable::Add(const DBRequest *req) {
    return AllocEntry(req->key.get()).release();
}

void DBTable::Change(DBEntryBase *entry) {
    DBTablePartBase *tpart = GetTablePartition(entry);
    tpart->Notify(entry);
}

bool DBTable::OnChange(DBEntry *entry, const DBRequest *req) {
    return true;
}

bool DBTable::Delete(DBEntry *entry, const DBRequest *req) {
    return true;
}

int DBTable::PartitionCount() const {
    return DB::PartitionCount();
}

static size_t HashToPartition(size_t hash) {
    return hash % DB::PartitionCount();
}

DBTablePartBase *DBTable::GetTablePartition(const int index) {
    return partitions_[index];
}

DBTablePartBase *DBTable::GetTablePartition(const DBRequestKey *key) {
    int id = HashToPartition(Hash(key));
    return GetTablePartition(id);
}

DBTablePartBase *DBTable::GetTablePartition(const DBEntryBase *entry) {
    const DBEntry *gentry = static_cast<const DBEntry *>(entry);
    size_t id = HashToPartition(Hash(gentry));
    return GetTablePartition(id);
}

// Find DB Entry without taking lock. Calling routine must ensure its
// running in exclusion with DB task
DBEntry *DBTable::FindNoLock(const DBEntry *entry) {
    size_t id = HashToPartition(Hash(entry));
    DBTablePartition *tbl_partition =
        static_cast<DBTablePartition *>(GetTablePartition(id));
    return tbl_partition->FindNoLock(entry);
}

DBEntry *DBTable::Find(const DBEntry *entry) {
    size_t id = HashToPartition(Hash(entry));
    DBTablePartition *tbl_partition =
        static_cast<DBTablePartition *>(GetTablePartition(id));
    return tbl_partition->Find(entry);
}

// Find DB Entry without taking lock. Calling routine must ensure its
// running in exclusion with DB task
DBEntry *DBTable::FindNoLock(const DBRequestKey *key) {
    int id = HashToPartition(Hash(key));
    DBTablePartition *tbl_partition =
    static_cast<DBTablePartition *>(GetTablePartition(id));
    return tbl_partition->FindNoLock(key);
}

DBEntry *DBTable::Find(const DBRequestKey *key) {
    int id = HashToPartition(Hash(key));
    DBTablePartition *tbl_partition =
    static_cast<DBTablePartition *>(GetTablePartition(id));
    return tbl_partition->Find(key);
}

//
// Concurrency: called from task that's mutually exclusive with db::DBTable.
//
// Calculate the size across all partitions.
//
size_t DBTable::Size() const {
    size_t total = 0;
    for (vector<DBTablePartition *>::const_iterator iter = partitions_.begin();
         iter != partitions_.end(); iter++) {
        total += (*iter)->size();
    }
    return total;
}

void DBTable::Input(DBTablePartition *tbl_partition, DBClient *client,
                    DBRequest *req) {
    DBRequestKey *key = 
        static_cast<DBRequestKey *>(req->key.get());
    DBEntry *entry = NULL;

    entry = tbl_partition->Find(key);
    if (req->oper == DBRequest::DB_ENTRY_ADD_CHANGE) {
        if (entry) {
            if (OnChange(entry, req) || entry->IsDeleted()) {
                // The entry may currently be marked as deleted.
                entry->ClearDelete();
                tbl_partition->Change(entry);
            }
        } else {
            if ((entry = Add(req)) != NULL) {
                tbl_partition->Add(entry);
            }
        }
    } else if (req->oper == DBRequest::DB_ENTRY_DELETE) {
        if (entry) {
            if (Delete(entry, req)) {
                tbl_partition->Delete(entry);
            }
        }
    } else if (req->oper == DBRequest::DB_ENTRY_NOTIFY) {
        if (entry) {
            tbl_partition->Notify(entry);
        }
    } else {
        assert(0);
    }
}

void DBTable::DBStateClear(DBTable *table, ListenerId id) {
    DBEntryBase *next = NULL;

    for (int i = 0; i < table->PartitionCount(); ++i) {
        DBTablePartition *partition = static_cast<DBTablePartition *>(
            table->GetTablePartition(i));

        for (DBEntryBase *entry = partition->GetFirst(); entry; entry = next) {
            next = partition->GetNext(entry);
            DBState *state = entry->GetState(table, id);
            if (state) {
                entry->ClearState(table, id);
                delete state;
            }
        }
    }
}

//
// Callback for table walk triggered by NotifyAllEntries.
//
bool DBTable::WalkCallback(DBTablePartBase *tpart, DBEntryBase *entry) {
    tpart->Notify(entry);
    return true;
}

//
// Callback for completion of table walk triggered by NotifyAllEntries.
//
void DBTable::WalkCompleteCallback(DBTableBase *tbl_base) {
}

//
// Concurrency: called from task that's mutually exclusive with db::DBTable.
//
// Trigger notification of all entries to all listeners.
// Should be used sparingly e.g. to handle significant configuration change.
//
// Cancel any outstanding walk and start a new one.  The walk callback just
// turns around and puts the DBentryBase on the change list.
//
void DBTable::NotifyAllEntries() {
    CHECK_CONCURRENCY("bgp::Config", "bgp::RTFilter");

    DBTableWalkMgr *walker = database()->GetWalkMgr();
    walker->WalkTable(walk_ref_);
}

void DBTable::TableShutdown() {
    DBTableWalkMgr *walker = database()->GetWalkMgr();
    walker->ReleaseWalker(walk_ref_);
    return DBTableBase::TableShutdown();
}
