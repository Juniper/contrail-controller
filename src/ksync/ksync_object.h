/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_ksync_object_h 
#define ctrlplane_ksync_object_h 

#include <tbb/mutex.h>
#include <tbb/recursive_mutex.h>
#include <base/queue_task.h>
#include <base/timer.h>
#include <sandesh/sandesh_trace.h>

#include "ksync_entry.h"
#include "ksync_index.h"
/////////////////////////////////////////////////////////////////////////////
// Back-Ref management needs two trees,
// Back-Ref tree:
// --------------
// An entry of type <key-entry, back-ref-entry> means that key-entry is
// waiting for back-ref-entry to be added to kernel.
// Note, there can be more than one key-entry waiting on a single 
// back-ref-entry. However, a key-entry can be waiting on only one
// back-ref-entry at a time.
//
// This is a dynamic tree. Entries are added only when constraints are not
// met. Entries will not be in tree when constraints are met.
//
// Fwd-Ref tree: 
// -------------
// Holds forward reference information. If Object-A is waiting on Object-B
// Fwd-Ref tree will have an entry with Object-A as key and Object-B as data.
/////////////////////////////////////////////////////////////////////////////

struct KSyncFwdReference {
    KSyncFwdReference(KSyncEntry *key, KSyncEntry *ref) : key_(key), 
        reference_(ref) { };

    bool operator<(const KSyncFwdReference &rhs) const {
        return (key_ < rhs.key_);
    };

    boost::intrusive::set_member_hook<>     node_;
    KSyncEntry      *key_;
    KSyncEntry      *reference_;
};

struct KSyncBackReference {
    KSyncBackReference(KSyncEntry *key, KSyncEntry *ref) : 
                       key_(key), back_reference_(ref) { };

    bool operator<(const KSyncBackReference &rhs) const {
        if (key_ < rhs.key_)
            return true;

        if (key_ > rhs.key_)
            return false;
	
        if (back_reference_ < rhs.back_reference_)
            return true;

        return false;
    };

    boost::intrusive::set_member_hook<>     node_;
    KSyncEntry      *key_;
    KSyncEntry      *back_reference_;
};

class KSyncObject {
public:
    typedef boost::intrusive::member_hook<KSyncEntry,
            boost::intrusive::set_member_hook<>,
            &KSyncEntry::node_> KSyncObjectNode;
    typedef boost::intrusive::set<KSyncEntry, KSyncObjectNode> Tree;

    typedef boost::intrusive::member_hook<KSyncFwdReference,
            boost::intrusive::set_member_hook<>,
            &KSyncFwdReference::node_> KSyncFwdRefNode;
    typedef boost::intrusive::set<KSyncFwdReference, KSyncFwdRefNode> FwdRefTree;

    typedef boost::intrusive::member_hook<KSyncBackReference,
            boost::intrusive::set_member_hook<>,
            &KSyncBackReference::node_> KSyncBackRefNode;
    typedef boost::intrusive::set<KSyncBackReference, KSyncBackRefNode> BackRefTree;

    // Default constructor. No index needed
    KSyncObject(const std::string &name);
    // Constructor for objects needing index
    KSyncObject(const std::string &name, int max_index);
    // Destructor
    virtual ~KSyncObject();

    // Initialise stale entry cleanup state machine.
    void InitStaleEntryCleanup(boost::asio::io_service &ios,
                               uint32_t cleanup_time, uint32_t cleanup_intvl,
                               uint16_t entries_per_intvl);

    // Notify an event to KSyncEvent state-machine
    void NotifyEvent(KSyncEntry *entry, KSyncEntry::KSyncEvent event);
    // Call Notify event with mutex lock held
    void SafeNotifyEvent(KSyncEntry *entry, KSyncEntry::KSyncEvent event);
    // Handle Netlink ACK message
    virtual void NetlinkAck(KSyncEntry *entry, KSyncEntry::KSyncEvent event);
    // Add a back-reference entry
    void BackRefAdd(KSyncEntry *key, KSyncEntry *reference);
    // Delete a back-reference entry
    void BackRefDel(KSyncEntry *key);
    // Re-valuate the back-reference entries
    void BackRefReEval(KSyncEntry *key);

    // Create an entry
    KSyncEntry *Create(const KSyncEntry *key);
    KSyncEntry *Create(const KSyncEntry *key, bool skip_lookup);
    // Create a Stale entry, which needs to be cleanedup as part for
    // stale entry cleanup (timer).
    // Derived class can choose to create this entry to manage stale
    // states in Kernel
    KSyncEntry *CreateStale(const KSyncEntry *key);
    // Called on change to ksync_entry. Will resulting in sync of the entry
    void Change(KSyncEntry *entry);
    // Delete a KSyncEntry
    void Delete(KSyncEntry *entry);
    // Query function. Key is in entry
    KSyncEntry *Find(const KSyncEntry *key);
    // Get Next Function.
    KSyncEntry *Next(const KSyncEntry *entry) const;
    // Query KSyncEntry for key in entry. Create temporary entry if not present
    KSyncEntry *GetReference(const KSyncEntry *key);

    // Called from Create or GetReference to Allocate a KSyncEntry.
    // The KSyncEntry must be populated with fields in key and index
    virtual KSyncEntry *Alloc(const KSyncEntry *key, uint32_t index) = 0;
    virtual void Free(KSyncEntry *entry);

    //Callback when all the entries in table are deleted
    virtual void EmptyTable(void) { };
    bool IsEmpty(void) { return tree_.empty(); }; 

    virtual bool DoEventTrace(void) { return true; }
    virtual void PreFree(KSyncEntry *entry) { }
    static void Shutdown();

    std::size_t Size() { return tree_.size(); }
    void set_delete_scheduled() { delete_scheduled_ = true;}
    bool delete_scheduled() { return delete_scheduled_;}
    virtual SandeshTraceBufferPtr GetKSyncTraceBuf() {return KSyncTraceBuf;}

protected:
    // Create an entry with default state. Used internally
    KSyncEntry *CreateImpl(const KSyncEntry *key);
    // Clear Stale Entry flag
    void ClearStale(KSyncEntry *entry);
    // Big lock on the tree
    // TODO: Make this more fine granular
    mutable tbb::recursive_mutex  lock_;
    void ChangeKey(KSyncEntry *entry, uint32_t arg);
    virtual void UpdateKey(KSyncEntry *entry, uint32_t arg) { }

    // derived class needs to implement GetKey,
    // default impl will assert
    virtual uint32_t GetKey(KSyncEntry *entry);

private:
    friend class KSyncEntry;
    friend void TestTriggerStaleEntryCleanupCb(KSyncObject *obj);

    // Free indication of an KSyncElement. 
    // Removes from tree and free index if allocated earlier
    void FreeInd(KSyncEntry *entry, uint32_t index);
    void NetlinkAckInternal(KSyncEntry *entry, KSyncEntry::KSyncEvent event);

    bool IsIndexValid() const { return need_index_; }

    // timer Callback to trigger delete of stale entries.
    bool StaleEntryCleanupCb();

    //Callback to do cleanup when DEL ACK is received.
    virtual void CleanupOnDel(KSyncEntry *kentry) {}

    // Tree of all KSyncEntries
    Tree tree_;
    // Forward reference tree
    static FwdRefTree  fwd_ref_tree_;
    // Back reference tree
    static BackRefTree  back_ref_tree_;
    // Does the KSyncEntry need index?
    bool need_index_;
    // Index table for KSyncObject
    KSyncIndexTable index_table_;
    // scheduled for deletion
    bool delete_scheduled_;

    // stale entry tree
    std::set<KSyncEntry::KSyncEntryPtr> stale_entry_tree_;

    // Stale Entry Cleanup Timer
    Timer *stale_entry_cleanup_timer_;

    uint32_t stale_entry_cleanup_intvl_;
    uint16_t stale_entries_per_intvl_;
    SandeshTraceBufferPtr KSyncTraceBuf;

    DISALLOW_COPY_AND_ASSIGN(KSyncObject);
};

// Special KSyncObject for DB client
class KSyncDBObject : public KSyncObject {
public:
    // Response to DB Filter API using which derived class can choose to
    // ignore or trigger delete for some of the DB entries.
    // This can be used where we don't want to handle certain type of OPER
    // DB entries in KSync, using this simplifies the behaviour defination
    // for KSync Object.
    enum DBFilterResp {
        DBFilterAccept,  // Accept DB Entry Add/Change for processing
        DBFilterIgnore,  // Ignore DB Entry Add/Change
        DBFilterDelete,  // Ignore DB Entry Add/Change and clear previous state
        DBFilterDelAdd,  // Delete current ksync and add new one (key change)
        DBFilterMax
    };
    // Create KSyncObject. DB Table will be registered later
    KSyncDBObject(const std::string &name);
    KSyncDBObject(const std::string &name, int max_index);

    KSyncDBObject(const std::string &name, DBTableBase *table);
    // KSync DB Object with index allocation
    KSyncDBObject(const std::string &name,
                  DBTableBase *table,
                  int max_index);

    // Destructor
    virtual ~KSyncDBObject();

    // Register to a DB Table
    void RegisterDb(DBTableBase *table);

    //Unregister from a DB table
    void UnregisterDb(DBTableBase *table);

    // Callback registered to DB Table
    void Notify(DBTablePartBase *partition, DBEntryBase *entry);

    DBTableBase *GetDBTable() { return table_; }
    DBTableBase::ListenerId GetListenerId(DBTableBase *table);

    // Function to filter DB Entries to be used, default behaviour will accept
    // All DB Entries, needs to be overriden by derived class to get desired
    // behavior.
    virtual DBFilterResp DBEntryFilter(const DBEntry *entry,
                                       const KSyncDBEntry *ksync);
    // Populate Key in KSyncEntry from DB Entry.
    // Used for lookup of KSyncEntry from DBEntry
    virtual KSyncEntry *DBToKSyncEntry(const DBEntry *entry) = 0;
    void set_test_id(DBTableBase::ListenerId id);
    DBTableBase::ListenerId id() const {return id_;}

private:
    //Callback to do cleanup when DEL ACK is received.
    virtual void CleanupOnDel(KSyncEntry *kentry);

    DBTableBase *table_;
    DBTableBase::ListenerId id_;
    DBTableBase::ListenerId test_id_;

    KSyncIndexTable index_table_;
    DISALLOW_COPY_AND_ASSIGN(KSyncDBObject);
};

struct KSyncObjectEvent {
    enum Event {
        UNKNOWN,
        UNREGISTER,
        DELETE,
    };
    KSyncObjectEvent(KSyncObject *obj, Event event) :
        obj_(obj), event_(event) {
    }
    KSyncEntry::KSyncEntryPtr ref_;
    KSyncObject *obj_;
    Event event_;
};

class KSyncObjectManager {
public:
    static const int kMaxEntriesProcess = 100;

    KSyncObjectManager();
    ~KSyncObjectManager();
    bool Process(KSyncObjectEvent *event);
    void Enqueue(KSyncObjectEvent *event);
    static KSyncEntry *default_defer_entry();
    static KSyncObjectManager *Init();
    static void Shutdown();
    static void Unregister(KSyncObject *);
    void Delete(KSyncObject *);
    static KSyncObjectManager *GetInstance();
private:
    WorkQueue<KSyncObjectEvent *> *event_queue_;
    static std::auto_ptr<KSyncEntry> default_defer_entry_;
    static KSyncObjectManager *singleton_;
};

#define KSYNC_TRACE(obj, parent, ...)\
do {\
   KSync##obj::TraceMsg(parent->GetKSyncTraceBuf(), __FILE__, __LINE__, ##__VA_ARGS__);\
} while (false);\

#endif // ctrlplane_ksync_object_h 
