/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <tbb/mutex.h>

#include "base/logging.h"
#include "db/db.h"
#include "db/db_entry.h"
#include "db/db_partition.h"
#include "db/db_table.h"
#include "db/db_table_partition.h"

using namespace std;

// concurrency: called from DBPartition task.
void DBTablePartBase::Notify(DBEntryBase *entry) {
    if (entry->is_onlist()) {
        return;
    }
    entry->set_onlist();
    bool was_empty = change_list_.empty();
    change_list_.push_back(*entry);
    if (was_empty) {
        DB *db = parent()->database();
        DBPartition *partition = db->GetPartition(index_);
        partition->OnTableChange(this);
    }
}

// concurrency: called from DBPartition task.
bool DBTablePartBase::RunNotify() {
    for (int i = 0; ((i < kMaxIterations) && !change_list_.empty()); ++i) {
        DBEntryBase *entry = &change_list_.front();
        change_list_.pop_front();

        parent()->RunNotify(this, entry);

        // If the entry is marked deleted and all DBStates are removed
        // and it's not already on the remove queue, it can be removed
        // from the tree right away.
        if (entry->IsDeleted() && entry->is_state_empty(this) &&
            !entry->IsOnRemoveQ()) {
            Remove(entry);
        } else {
            entry->clear_onlist();
        }
    }
    if (!change_list_.empty()) {
        DB *db = parent()->database();
        DBPartition *partition = db->GetPartition(index_);
        partition->OnTableChange(this);
        return false;
    }
    return true;
}

void DBTablePartBase::Delete(DBEntryBase *entry) {
    if (parent_->HasListeners()) {
        entry->MarkDelete();
        Notify(entry);
    } else {
        // Remove from change_list
        if (entry->is_onlist()) {
            change_list_.erase(change_list_.iterator_to(*entry));
        }
        Remove(entry);
    }
}

DBTablePartition::DBTablePartition(DBTable *table, int index)
    : DBTablePartBase(table, index) {
}

void DBTablePartition::Process(DBClient *client, DBRequest *req) {
    DBTable *table = static_cast<DBTable *>(parent());
    table->Input(this, client, req);
}

void DBTablePartition::Add(DBEntry *entry) {
    tbb::mutex::scoped_lock lock(mutex_);
    std::pair<Tree::iterator, bool> ret = tree_.insert(*entry);
    assert(ret.second);
    entry->set_table_partition(static_cast<DBTablePartBase *>(this));
    Notify(entry);
}

void DBTablePartition::Change(DBEntry *entry) {
    tbb::mutex::scoped_lock lock(mutex_);
    Notify(entry);
}

void DBTablePartition::Remove(DBEntryBase *db_entry) {
    tbb::mutex::scoped_lock lock(mutex_);
    DBEntry *entry = static_cast<DBEntry *>(db_entry);

    assert(tree_.erase(*entry));
    delete entry;

    // If a table is marked for deletion, then we may trigger the deletion
    // process when the last prefix is deleted
    if (tree_.empty())
        table()->RetryDelete();
}

DBEntry *DBTablePartition::Find(const DBEntry *entry) {
    tbb::mutex::scoped_lock lock(mutex_);
    Tree::iterator loc = tree_.find(*entry);
    if (loc != tree_.end()) {
        return loc.operator->();
    }
    return NULL;
}

DBEntry *DBTablePartition::Find(const DBRequestKey *key) {
    tbb::mutex::scoped_lock lock(mutex_);
    DBTable *table = static_cast<DBTable *>(parent());
    std::auto_ptr<DBEntry> entry_ptr = table->AllocEntry(key);

    Tree::iterator loc = tree_.find(*(entry_ptr.get()));
    if (loc != tree_.end()) {
        return loc.operator->();
    }
    return NULL;
}

// Returns the matching entry or next in lex order
DBEntry *DBTablePartition::lower_bound(const DBEntryBase *key) {
    const DBEntry *entry = static_cast<const DBEntry *>(key);
    tbb::mutex::scoped_lock lock(mutex_);

    Tree::iterator it = tree_.lower_bound(*entry);
    if (it != tree_.end()) {
        return (it.operator->());
    }
    return NULL;
}

DBEntry *DBTablePartition::GetFirst() {
    tbb::mutex::scoped_lock lock(mutex_);
    Tree::iterator it = tree_.begin();
    if (it == tree_.end()) {
        return NULL;
    }
    return it.operator->();        
}

// Returns the next entry (Doesn't search). Threaded walk
DBEntry *DBTablePartition::GetNext(const DBEntryBase *key) {
    const DBEntry *entry = static_cast<const DBEntry *>(key);
    tbb::mutex::scoped_lock lock(mutex_);

    Tree::const_iterator it = tree_.iterator_to(*entry);
    it++;
    if (it != tree_.end()) {
        return const_cast<DBEntry *>(it.operator->());
    }
    return NULL;
}

DBTable *DBTablePartition::table() {
    return static_cast<DBTable *>(parent());
}
