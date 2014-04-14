/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_ksync_object_h 
#define ctrlplane_ksync_object_h 

#include <tbb/mutex.h>
#include <tbb/recursive_mutex.h>
#include <base/queue_task.h>
#include <sandesh/sandesh_trace.h>
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
    KSyncObject();
    // Constructor for objects needing index
    KSyncObject(int max_index);
    // Destructor
    virtual ~KSyncObject();

    // Notify an event to KSyncEvent state-machine
    void NotifyEvent(KSyncEntry *entry, KSyncEntry::KSyncEvent event);
    // Call Notify event with mutex lock held
    void SafeNotifyEvent(KSyncEntry *entry, KSyncEntry::KSyncEvent event);
    // Handle Netlink ACK message
    void NetlinkAck(KSyncEntry *entry, KSyncEntry::KSyncEvent event);
    // Add a back-reference entry
    void BackRefAdd(KSyncEntry *key, KSyncEntry *reference);
    // Delete a back-reference entry
    void BackRefDel(KSyncEntry *key);
    // Re-valuate the back-reference entries
    void BackRefReEval(KSyncEntry *key);

    // Create an entry
    KSyncEntry *Create(const KSyncEntry *key);
    // Called on change to ksync_entry. Will resulting in sync of the entry
    void Change(KSyncEntry *entry);
    // Delete a KSyncEntry
    void Delete(KSyncEntry *entry);
    // Query function. Key is in entry
    KSyncEntry *Find(const KSyncEntry *key);
    // Query KSyncEntry for key in entry. Create temporary entry if not present
    KSyncEntry *GetReference(const KSyncEntry *key);

    // Called from Create or GetReference to Allocate a KSyncEntry.
    // The KSyncEntry must be populated with fields in key and index
    virtual KSyncEntry *Alloc(const KSyncEntry *key, uint32_t index) = 0;

    //Callback when all the entries in table are deleted
    virtual void EmptyTable(void) { };
    bool IsEmpty(void) { return tree_.empty(); }; 

    virtual bool DoEventTrace(void) { return true; }
    static void Shutdown();
protected:
    // Create an entry with default state. Used internally
    KSyncEntry *CreateImpl(const KSyncEntry *key);
    // Big lock on the tree
    // TODO: Make this more fine granular
    tbb::recursive_mutex  lock_;

private:
    friend class KSyncEntry;
    // Free indication of an KSyncElement. 
    // Removes from tree and free index if allocated earlier
    void FreeInd(KSyncEntry *entry, uint32_t index);
    void NetlinkAckInternal(KSyncEntry *entry, KSyncEntry::KSyncEvent event);

    bool IsIndexValid() const { return need_index_; }

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
    DISALLOW_COPY_AND_ASSIGN(KSyncObject);
};

// Special KSyncObject for DB client
class KSyncDBObject : public KSyncObject {
public:
    // Create KSyncObject. DB Table will be registered later
    KSyncDBObject();
    KSyncDBObject(int max_index);

    KSyncDBObject(DBTableBase *table);
    // KSync DB Object with index allocation
    KSyncDBObject(DBTableBase *table, int max_index);

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

    // Populate Key in KSyncEntry from DB Entry.
    // Used for lookup of KSyncEntry from DBEntry
    virtual KSyncEntry *DBToKSyncEntry(const DBEntry *entry) = 0;
    void set_test_id(DBTableBase::ListenerId id);
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
        UNREGISTER
    };
    KSyncObjectEvent(KSyncObject *obj, Event event) :
        obj_(obj), event_(event) {
    }
    KSyncObject *obj_;
    Event event_;
};

class KSyncObjectManager {
public:
    KSyncObjectManager();
    ~KSyncObjectManager();
    bool Process(KSyncObjectEvent *event);
    void Enqueue(KSyncObjectEvent *event);
    static void Init();
    static void Shutdown();
    static void Unregister(KSyncObject *);
private:
    WorkQueue<KSyncObjectEvent *> *event_queue_;
    static KSyncObjectManager *singleton_;
};

class KSyncDebug {
public:
    static void set_debug(bool debug) { debug_ = debug;}
    static bool debug() { return debug_; }
private:
    static bool debug_;
};

#define KSYNC_TRACE(obj, ...)\
do {\
   KSync##obj::TraceMsg(KSyncTraceBuf, __FILE__, __LINE__, ##__VA_ARGS__);\
} while (false);\

#define KSYNC_ASSERT(cond)\
do {\
   if (KSyncDebug::debug() == true) {\
       assert(cond);\
   }\
} while (false);\

extern SandeshTraceBufferPtr KSyncTraceBuf;

#endif // ctrlplane_ksync_object_h 
