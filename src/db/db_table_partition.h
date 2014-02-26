/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_db_table_partition_h
#define ctrlplane_db_table_partition_h

#include <boost/intrusive/list.hpp>
#include <tbb/mutex.h>

#include "db/db_entry.h"

class DBTableBase;
class DBTable;

// Table shard contained within a DBPartition.
class DBTablePartBase {
public:
    static const int kMaxIterations = 256;
    typedef boost::intrusive::member_hook<DBEntryBase, 
            boost::intrusive::list_member_hook<>, 
            &DBEntryBase::chg_list_> ChangeListMember; 

    typedef boost::intrusive::list<DBEntryBase, ChangeListMember> ChangeList;


    DBTablePartBase(DBTableBase *tbl_base, int index)
        : parent_(tbl_base), index_(index) {
    }

    // Input processing stage for DBRequests. Called from per-partition thread.
    virtual void Process(DBClient *client, DBRequest *req) = 0;

    // Enqueue a change notification. Deferred until the RunNotify stage.
    void Notify(DBEntryBase *entry);

    // Run the notification queue.
    bool RunNotify();

    DBTableBase *parent() { return parent_; }
    int index() const { return index_; }

    virtual void Remove(DBEntryBase *) = 0;

    void Delete(DBEntryBase *);

    // Walk functions
    virtual DBEntryBase *lower_bound(const DBEntryBase *key) = 0;
    virtual DBEntryBase *GetFirst() = 0;
    virtual DBEntryBase *GetNext(const DBEntryBase *) = 0;

    tbb::mutex &dbstate_mutex() {
        return dbstate_mutex_;
    }

    virtual ~DBTablePartBase() {};
private:
    tbb::mutex dbstate_mutex_;
    DBTableBase *parent_;
    int index_;
    ChangeList change_list_;
    DISALLOW_COPY_AND_ASSIGN(DBTablePartBase);
};

class DBTablePartition : public DBTablePartBase {
public:
    typedef boost::intrusive::member_hook<DBEntry,
        boost::intrusive::set_member_hook<>,
        &DBEntry::node_> SetMember;
    typedef boost::intrusive::set<DBEntry, SetMember> Tree;
    
    DBTablePartition(DBTable *parent, int index);

    ///////////////////////////////////////////////////////////////
    // Virtual functions from DBTableBase implemented by DBTable
    ///////////////////////////////////////////////////////////////
    void Process(DBClient *client, DBRequest *req);
    // Returns the matching route or next in lex order
    virtual DBEntry *lower_bound(const DBEntryBase *entry);
    // Returns the next route (Doesn't search). Threaded walk
    virtual DBEntry *GetNext(const DBEntryBase *entry);

    virtual DBEntry *GetFirst();
    
    ///////////////////////////////////////////////////////////
    // Methods used in implementing DBTablePartition
    ///////////////////////////////////////////////////////////

    // Add a DB Entry
    virtual void Add(DBEntry *entry);

    // Generate Change notification for an entry
    virtual void Change(DBEntry *entry);

    // Remove an entry from DB Table. Entry will not be accessible from 
    // DB anymore
    virtual void Remove(DBEntryBase *entry);

    // Find DB Entry. Get key from from argument
    DBEntry *Find(const DBEntry *entry);

    // Find DB Entry. Get key from from argument
    DBEntry *Find(const DBRequestKey *key);

    DBTable *table();
    size_t size() const { return tree_.size(); }

private:
    tbb::mutex mutex_;
    Tree tree_;
    DISALLOW_COPY_AND_ASSIGN(DBTablePartition);
};

#endif
