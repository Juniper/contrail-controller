/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <vector>
#include <tbb/spin_rw_mutex.h>

#include <boost/bind.hpp>
#include <boost/dynamic_bitset.hpp>
#include <boost/type_traits.hpp>

#include "base/compiler.h"
#include "base/logging.h"
#include "base/task_annotations.h"
#include "db/db.h"
#include "db/db_partition.h"
#include "db/db_table.h"
#include "db/db_table_partition.h"
#include "db/db_table_walker.h"

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

    DBTableBase::ListenerId Register(ChangeCallback callback) {
        tbb::spin_rw_mutex::scoped_lock write_lock(rw_mutex_, true);
        size_t i = bmap_.find_first();
        if (i == bmap_.npos) {
            i = callbacks_.size();
            callbacks_.push_back(callback);
        } else {
            bmap_.reset(i);
            if (bmap_.none()) {
                bmap_.clear();
            }
            callbacks_[i] = callback;
        }
        return i;
    }

    void Unregister(ListenerId listener) {
        tbb::spin_rw_mutex::scoped_lock write_lock(rw_mutex_, true);
        callbacks_[listener] = NULL;
        if ((size_t) listener == callbacks_.size() - 1) {
            while (!callbacks_.empty() && callbacks_.back() == NULL) {
                callbacks_.pop_back();
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

    bool empty() { 
        tbb::spin_rw_mutex::scoped_lock read_lock(rw_mutex_, false);
        return callbacks_.empty(); 
    }

private:
    CallbackList callbacks_;
    tbb::spin_rw_mutex rw_mutex_;
    boost::dynamic_bitset<> bmap_;      // free list.
};

DBTableBase::DBTableBase(DB *db, const string &name)
        : db_(db), name_(name), info_(new ListenerInfo()) {
}

DBTableBase::~DBTableBase() {
}

DBTableBase::ListenerId DBTableBase::Register(ChangeCallback callback) {
    return info_->Register(callback);
}

void DBTableBase::Unregister(ListenerId listener) {
    info_->Unregister(listener);
}

bool DBTableBase::Enqueue(DBRequest *req) {
    DBTablePartBase *tpart = GetTablePartition(req->key.get());
    DBPartition *partition = db_->GetPartition(tpart->index());
    return partition->EnqueueRequest(tpart, NULL, req);
}

void DBTableBase::EnqueueRemove(DBEntryBase *db_entry) {
    DBTablePartBase *tpart = GetTablePartition(db_entry);
    DBPartition *partition = db_->GetPartition(tpart->index());
    partition->EnqueueRemove(tpart, db_entry);
}

void DBTableBase::RunNotify(DBTablePartBase *tpart, DBEntryBase *entry) {
    info_->RunNotify(tpart, entry);
}

bool DBTableBase::HasListeners() const {
    return !info_->empty();
}

///////////////////////////////////////////////////////////
// Implementation of DBTable methods
///////////////////////////////////////////////////////////
DBTable::DBTable(DB *db, const string &name)
    : DBTableBase(db, name),
      walk_id_(DBTableWalker::kInvalidWalkerId) {
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

void DBTable::Delete(DBEntry *entry, const DBRequest *req) {
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

DBEntry *DBTable::Find(const DBEntry *entry) {
    size_t id = HashToPartition(Hash(entry));
    DBTablePartition *tbl_partition =
        static_cast<DBTablePartition *>(GetTablePartition(id));
    return tbl_partition->Find(entry);
}

DBEntry *DBTable::Find(const DBRequestKey *key) {
    int id = HashToPartition(Hash(key));
    DBTablePartition *tbl_partition =
    static_cast<DBTablePartition *>(GetTablePartition(id));
    return tbl_partition->Find(key);
}

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
    } else {
        if (entry) {
            Delete(entry, req);
            tbl_partition->Delete(entry);
        }
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
    walk_id_ = DBTableWalker::kInvalidWalkerId;
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
    CHECK_CONCURRENCY("bgp::Config");

    DBTableWalker *walker = database()->GetWalker();
    if (walk_id_ != DBTableWalker::kInvalidWalkerId)
        walker->WalkCancel(walk_id_);
    walk_id_= walker->WalkTable(this, NULL, DBTable::WalkCallback,
        boost::bind(&DBTable::WalkCompleteCallback, this, _1));
}
