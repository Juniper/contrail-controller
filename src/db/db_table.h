/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_db_table_h
#define ctrlplane_db_table_h

#include <memory>
#include <vector>
#include <boost/function.hpp>
#include <boost/intrusive_ptr.hpp>
#include <tbb/atomic.h>

#include "base/util.h"

class DB;
class DBClient;
class DBEntryBase;
class DBEntry;
class DBTablePartBase;
class DBTablePartition;
class DBTableWalk;
class ShowTableListener;

class DBRequestKey {
public:
    virtual ~DBRequestKey() { }
};
class DBRequestData {
public:
    virtual ~DBRequestData() { }
};

struct DBRequest {
    typedef enum {
        DB_ENTRY_ADD_CHANGE = 1,
        DB_ENTRY_DELETE = 2,
        DB_ENTRY_NOTIFY = 3,
    } DBOperation;
    DBOperation oper;

    DBRequest();
    DBRequest(DBOperation op) : oper(op) { }
    ~DBRequest();

    std::auto_ptr<DBRequestKey> key;
    std::auto_ptr<DBRequestData> data;

    // Swap contents between two DBRequest entries.
    void Swap(DBRequest *rhs);

private:
    DISALLOW_COPY_AND_ASSIGN(DBRequest);
};

// Database table interface.
class DBTableBase {
public:
    typedef boost::function<void(DBTablePartBase *, DBEntryBase *)> ChangeCallback;
    typedef int ListenerId;

    static const int kInvalidId = -1;

    DBTableBase(DB *db, const std::string &name);
    virtual ~DBTableBase();

    // Enqueue a request to the table. Takes ownership of the data.
    bool Enqueue(DBRequest *req);
    void EnqueueRemove(DBEntryBase *db_entry);

    // Determine the table partition depending on the record key.
    virtual DBTablePartBase *GetTablePartition(const DBRequestKey *key) = 0;
    // Determine the table partition depending on the Entry 
    virtual DBTablePartBase *GetTablePartition(const DBEntryBase *entry) = 0;
    // Determine the table partition for given index
    virtual DBTablePartBase *GetTablePartition(const int index) = 0;

    // Record has been modified.
    virtual void Change(DBEntryBase *) = 0;

    // Callback from table partition for entry add/remove.
    virtual void AddRemoveCallback(const DBEntryBase *entry, bool add) const { }

    // Register a DB listener.
    ListenerId Register(ChangeCallback callback,
        const std::string &name = "unspecified");
    void Unregister(ListenerId listener);

    void RunNotify(DBTablePartBase *tpart, DBEntryBase *entry);

    // Manage db state count for a listener.
    void AddToDBStateCount(ListenerId listener, int count);

    uint64_t GetDBStateCount(ListenerId listener);

    // Calculate the size across all partitions.
    // Must be called from Task which is mutually exclusive with db::DBTable.
    virtual size_t Size() const { return 0; }
    bool empty() const { return (Size() == 0); }

    // Suspended deletion resume hook for user function
    virtual void RetryDelete() { }
    virtual bool MayDelete() const;

    DB *database() { return db_; }
    const DB *database() const { return db_; }

    const std::string &name() const { return name_; }

    bool HasListeners() const;
    size_t GetListenerCount() const;
    void FillListeners(std::vector<ShowTableListener> *listeners) const;

    uint64_t enqueue_count() const { return enqueue_count_; }
    void incr_enqueue_count() { enqueue_count_++; }
    void reset_enqueue_count() { enqueue_count_ = 0; }

    uint64_t input_count() const { return input_count_; }
    void incr_input_count() { input_count_++; }
    void reset_input_count() { input_count_ = 0; }

    uint64_t notify_count() const { return notify_count_; }
    void incr_notify_count() { notify_count_++; }
    void reset_notify_count() { notify_count_ = 0; }

    bool HasWalkers() const { return walker_count_ != 0; }
    uint64_t walker_count() const { return walker_count_; }
    void incr_walker_count() { walker_count_++; }
    uint64_t decr_walker_count() { return --walker_count_; }

    uint64_t walk_request_count() const { return walk_request_count_; }
    uint64_t walk_complete_count() const { return walk_complete_count_; }
    uint64_t walk_cancel_count() const { return walk_cancel_count_; }
    uint64_t walk_again_count() const { return walk_again_count_; }
    uint64_t walk_count() const { return walk_count_; }
    void incr_walk_request_count() { walk_request_count_++; }
    void incr_walk_complete_count() { walk_complete_count_++; }
    void incr_walk_cancel_count() { walk_cancel_count_++; }
    void incr_walk_again_count() { walk_again_count_++; }
    void incr_walk_count() { walk_count_++; }

private:
    class ListenerInfo;
    DB *db_;
    std::string name_;
    std::auto_ptr<ListenerInfo> info_;
    uint64_t enqueue_count_;
    uint64_t input_count_;
    uint64_t notify_count_;
    tbb::atomic<uint64_t> walker_count_;
    tbb::atomic<uint64_t> walk_count_;
    tbb::atomic<uint64_t> walk_request_count_;
    tbb::atomic<uint64_t> walk_complete_count_;
    tbb::atomic<uint64_t> walk_cancel_count_;
    tbb::atomic<uint64_t> walk_again_count_;
};

// An implementation of DBTableBase that uses boost::set as data-store
// Most of the DB Table implementations should derive from here instead of
// DBTableBase directly.
// Derive directly from DBTableBase only if there is a strong reason to do so
//
// Additionally, provides a set of virtual functions to override the default 
// functionality
class DBTable : public DBTableBase {
public:
    typedef boost::intrusive_ptr<DBTableWalk> DBTableWalkRef;

    // Walker function:
    // Called for each DBEntry under a db::DBTable task that corresponds to the
    // specific partition.
    // arguments: DBTable partition and DBEntry.
    // returns: true (continue); false (stop).
    typedef boost::function<bool(DBTablePartBase *, DBEntryBase *)> WalkFn;

    // Called when all partitions are done iterating.
    typedef boost::function<void(DBTableWalkRef, DBTableBase *)> WalkCompleteFn;

    static const int kIterationToYield = 256;

    DBTable(DB *db, const std::string &name);
    virtual ~DBTable();
    void Init();

    ///////////////////////////////////////////////////////////
    // virtual functions to be implemented by derived class
    ///////////////////////////////////////////////////////////

    // Alloc a derived DBEntry
    virtual std::auto_ptr<DBEntry> AllocEntry(const DBRequestKey *key) const = 0;

    // Hash for an entry. Used to identify partition
    virtual size_t Hash(const DBEntry *entry) const {return 0;};

    // Hash for key. Used to identify partition
    virtual size_t Hash(const DBRequestKey *key) const {return 0;};

    // Alloc a derived DBTablePartBase entry. The default implementation
    // allocates DBTablePart should be good for most common cases.
    // Override if *really* necessary
    virtual DBTablePartition *AllocPartition(int index);

    // Input processing implemented by derived class. Default 
    // implementation takes care of Add/Delete/Change.
    // Override if *really* necessary
    virtual void Input(DBTablePartition *tbl_partition, DBClient *client,
                       DBRequest *req);

    // Add hook for user function. Must return entry to be inserted into tree
    // Must return NULL if no entry is to be added into tree
    virtual DBEntry *Add(const DBRequest *req);
    // Change hook for user function. Return 'true' if clients need to be
    // notified of the change
    virtual bool OnChange(DBEntry *entry, const DBRequest *req);
    // Delete hook for user function
    virtual bool Delete(DBEntry *entry, const DBRequest *req);

    void NotifyAllEntries();

    ///////////////////////////////////////////////////////////
    // Utility methods for table
    ///////////////////////////////////////////////////////////
    // Find DB Entry. Get key from from argument
    DBEntry *Find(const DBEntry *entry);

    DBEntry *Find(const DBRequestKey *key);

    // Find DB Entry without taking lock. Calling routine must ensure its
    // running in exclusion with DB task
    DBEntry *FindNoLock(const DBEntry *entry);
    DBEntry *FindNoLock(const DBRequestKey *key);

    ///////////////////////////////////////////////////////////////
    // Virtual functions from DBTableBase implemented by DBTable
    ///////////////////////////////////////////////////////////////

    // Return the table partition for a specific request.
    virtual DBTablePartBase *GetTablePartition(const DBRequestKey *key);
    // Return the table partition for a DBEntryBase
    virtual DBTablePartBase *GetTablePartition(const DBEntryBase *entry);
    // Return the table partition for a index
    virtual DBTablePartBase *GetTablePartition(const int index);

    // Change notification handler.
    virtual void Change(DBEntryBase *entry);

    virtual int PartitionCount() const;

    // Calculate the size across all partitions.
    virtual size_t Size() const;

    // helper functions

    // Delete all the state entries of a specific listener.
    // Not thread-safe. Used to shutdown and cleanup the process.
    static void DBStateClear(DBTable *table, ListenerId id);


    // Walk APIs
    // Create a DBTable Walker
    // Concurrency : can be invoked from any task
    DBTableWalkRef AllocWalker(WalkFn walk_fn, WalkCompleteFn walk_complete);

    // Release the Walker
    // Concurrency : can be invoked from any task
    void ReleaseWalker(DBTableWalkRef &walk);

    // Start a walk on the table.
    // Concurrency : should be invoked from a task which is mutually exclusive
    // "db::Walker" task
    void WalkTable(DBTableWalkRef walk);

    // Walk the table again
    // Concurrency : should be invoked from a task which is mutually exclusive
    // "db::Walker" task
    void WalkAgain(DBTableWalkRef walk);

    void SetWalkIterationToYield(int count) {
        max_walk_iteration_to_yield_ = count;
    }

    int GetWalkIterationToYield() {
        return max_walk_iteration_to_yield_;
    }

    void SetWalkTaskId(int task_id) {
        walker_task_id_ = task_id;
    }

    int GetWalkerTaskId() {
        return walker_task_id_;
    }
private:
    friend class DBTableWalkMgr;
    class TableWalker;
    // A Job for walking through the DBTablePartition
    class WalkWorker;

    static void db_walker_wait() {
        static int walk_sleep_usecs_;
        static bool once;

        if (!once) {
            once = true;

            char *wait = getenv("DB_WALKER_WAIT_USECS");
            if (wait) walk_sleep_usecs_ = strtoul(wait, NULL, 0);
        }

        if (walk_sleep_usecs_) {
            usleep(walk_sleep_usecs_);
        }
    }

    ///////////////////////////////////////////////////////////
    // Utility methods
    ///////////////////////////////////////////////////////////
    // Hash key to a partition id
    int GetPartitionId(const DBRequestKey *key);
    // Hash entry to a partition id
    int GetPartitionId(const DBEntry *entry);

    // Called from DBTableWalkMgr to start the walk
    void StartWalk();

    // Call DBTableWalkMgr to notify the walkers
    bool InvokeWalkCb(DBTablePartBase *part, DBEntryBase *entry);

    // Call DBTableWalkMgr::WalkDone
    void WalkDone();

    // Walker callback for NotifyAllEntries()
    bool WalkCallback(DBTablePartBase *tpart, DBEntryBase *entry);
    void WalkCompleteCallback(DBTableBase *tbl_base);

    std::auto_ptr<TableWalker> walker_;
    std::vector<DBTablePartition *> partitions_;
    DBTable::DBTableWalkRef walk_ref_;
    int walker_task_id_;
    int max_walk_iteration_to_yield_;

    DISALLOW_COPY_AND_ASSIGN(DBTable);
};

class DBTableWalk {
public:
    enum WalkState {
        INIT = 1,
        WALK_REQUESTED = 2,
        WALK_IN_PROGRESS = 3,
        WALK_DONE = 4,
        WALK_STOPPED = 5,
    };

    DBTableWalk(DBTable *table, DBTable::WalkFn walk_fn,
                DBTable::WalkCompleteFn walk_complete)
        : table_(table), walk_fn_(walk_fn), walk_complete_(walk_complete) {
        walk_state_ = INIT;
        walk_again_ = false;
        refcount_ = 0;
    }

    DBTable *table() const { return table_;}
    DBTable::WalkFn walk_fn() const { return walk_fn_;}
    DBTable::WalkCompleteFn walk_complete() const { return walk_complete_;}

    bool requested() const { return (walk_state_ == WALK_REQUESTED);}
    bool in_progress() const { return (walk_state_ == WALK_IN_PROGRESS);}
    bool done() const { return (walk_state_ == WALK_DONE);}
    bool stopped() const { return (walk_state_ == WALK_STOPPED);}
    bool walk_again() const { return walk_again_;}
    bool walk_is_active() const {
        return ((walk_state_ == WALK_REQUESTED) ||
                (walk_state_ == WALK_IN_PROGRESS));
    }

    WalkState walk_state() const { return walk_state_;}

private:
    friend class DBTableWalkMgr;

    friend void intrusive_ptr_add_ref(DBTableWalk *walker);
    friend void intrusive_ptr_release(DBTableWalk *walker);

    void set_walk_again() { walk_again_ = true;}
    void reset_walk_again() { walk_again_ = false;}

    void set_walk_done() { walk_state_ = WALK_DONE;}
    void set_walk_requested() { walk_state_ = WALK_REQUESTED;}
    void set_in_progress() { walk_state_ = WALK_IN_PROGRESS;}
    void set_walk_stopped() { walk_state_ = WALK_STOPPED;}

    DBTable *table_;
    DBTable::WalkFn walk_fn_;
    DBTable::WalkCompleteFn walk_complete_;
    tbb::atomic<WalkState> walk_state_;
    tbb::atomic<bool> walk_again_;
    tbb::atomic<int> refcount_;

    DISALLOW_COPY_AND_ASSIGN(DBTableWalk);
};

inline void intrusive_ptr_add_ref(DBTableWalk *walker) {
    walker->refcount_.fetch_and_increment();
}

inline void intrusive_ptr_release(DBTableWalk *walker) {
    int prev = walker->refcount_.fetch_and_decrement();
    if (prev == 1) {
        DBTable *table = walker->table();
        delete walker;
        table->decr_walker_count();
        table->RetryDelete();
    }
}
#endif
