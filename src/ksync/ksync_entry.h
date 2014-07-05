/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_ksync_entry_h 
#define ctrlplane_ksync_entry_h 

#include <boost/intrusive_ptr.hpp>
#include <tbb/atomic.h>
#include <sandesh/common/vns_constants.h>
#include <sandesh/common/vns_types.h>

#define KSYNC_ERROR(obj, ...)\
do {\
    if (LoggingDisabled()) break;\
    obj::Send(g_vns_constants.CategoryNames.find(Category::VROUTER)->second,\
              SandeshLevel::SYS_ERR, __FILE__, __LINE__, ##__VA_ARGS__);\
} while (false);\

class KSyncObject;

class KSyncEntry {
public:
    enum KSyncState {
        INIT,           // Init state. Not notified
        TEMP,           // Temporary entry created on reference
        ADD_DEFER,      // Add of entry deferred due to unmet dependencies
        CHANGE_DEFER,   // Change of entry deferred due to unmet dependencies
        IN_SYNC,        // Object in sync
        SYNC_WAIT,      // Waiting on ACK for add/change
        NEED_SYNC,      // Object changed. Needs Sync
        DEL_DEFER_SYNC, // Del pending to be sent due to sync_wait
        DEL_DEFER_REF,  // Del pending to be sent due to ref-count
        DEL_DEFER_DEL_ACK, // Del pending to be sent due to Del Ack wait
        DEL_ACK_WAIT,   // Del request sent waiting for ack
        RENEW_WAIT,     // Object renewal waiting for delete-ack
        FREE_WAIT       // Entry to be freed
    };

    enum KSyncEvent {
        ADD_CHANGE_REQ,
        ADD_ACK,
        CHANGE_ACK,
        DEL_REQ,
        DEL_ACK,
        RE_EVAL,
        INT_PTR_REL
    };

    std::string StateString() const;
    std::string OperationString() const;
    std::string EventString(KSyncEvent event);
    // All referring KSyncEntries must use KSyncEntryPtr. The ref-count
    // maintained is optionally used to defer DELETE till refcount is 0
    typedef boost::intrusive_ptr<KSyncEntry> KSyncEntryPtr;
    static const size_t kInvalidIndex = 0xFFFFFFFF;

    // Use this constructor if automatic index allocation is *not* needed
    KSyncEntry() : index_(kInvalidIndex), state_(INIT), seen_(false) { 
        refcount_ = 0;
    };
    // Use this constructor if automatic index allocation is needed
    KSyncEntry(uint32_t index) : index_(index), state_(INIT), seen_(false) {
        refcount_ = 0;
    };
    virtual ~KSyncEntry() { assert(refcount_ == 0);};

    // Comparator for boost::set containing all KSyncEntries in an KSyncObject
    bool operator<(const KSyncEntry &rhs) const {
        return IsLess(rhs);
    };
    // Comparator to manage the tree
    virtual bool IsLess(const KSyncEntry &rhs) const = 0;

    // Convert KSync to String
    virtual std::string ToString() const = 0;

    // Create handler. 
    // Return true if operation is complete
    // Return false if operation asynchronously
    virtual bool Add() = 0;

    // Change handler. 
    // Return true if operation is complete
    // Return false if operation asynchronously
    virtual bool Change() = 0;

    // Delete handler. 
    // Return true if operation is complete
    // Return false if operation asynchronously
    virtual bool Delete() = 0;

    // KSyncObject for this entry. Used to release the index
    virtual KSyncObject *GetObject() = 0;
    // Get an unresolved reference. 
    // This entry will be added into resolveq_ of unresolved-entry
    virtual KSyncEntry *UnresolvedReference() = 0;

    // Returns true if entry is resolved and referring entry can be written
    bool IsResolved();

    // User define KSync Response handler
    virtual void Response() { };

    // Allow State Compression for delete.
    virtual bool AllowDeleteStateComp() {return true;}

    // User defined error handler
    virtual void ErrorHandler(int err, uint32_t seqno) const;

    size_t GetIndex() const {return index_;};
    KSyncState GetState() const {return state_;};
    uint32_t GetRefCount() const {return refcount_;} 
    bool Seen() const {return seen_;}
    void SetSeen() {seen_ = true;}
    bool IsDeleted() { return (state_ == DEL_ACK_WAIT ||
                               state_ == DEL_DEFER_DEL_ACK ||
                               state_ == DEL_DEFER_SYNC ||
                               state_ == DEL_DEFER_REF); };

protected:
    void SetIndex(size_t index) {index_ = index;};
    void SetState(KSyncState state) {state_ = state;};
private:
    friend void intrusive_ptr_add_ref(KSyncEntry *p);
    friend void intrusive_ptr_release(KSyncEntry *p);
    friend class KSyncSock;
    friend class KSyncObject;

    boost::intrusive::set_member_hook<> node_;

    size_t              index_;
    KSyncState          state_;
    tbb::atomic<int>    refcount_;
    bool                seen_;
    DISALLOW_COPY_AND_ASSIGN(KSyncEntry);
};

// Implementation of KSyncEntry with with DBTable. Must be used along
// with KSyncDBObject.
// Registers with DBTable and drives state-machine based on DBTable 
// notifications
// Applications are not needed to generate any events to the state-machine
class KSyncDBEntry : public KSyncEntry, public DBState {
public:
    KSyncDBEntry() : KSyncEntry(), DBState() { db_entry_ = NULL; };
    KSyncDBEntry(uint32_t index) : KSyncEntry(index), DBState() { db_entry_ = NULL; };
    virtual ~KSyncDBEntry() { };

    // Check if object is in-sync with kernel.
    // Return true if object needs sync. Else return false
    virtual bool Sync(DBEntry *entry) = 0;

    void SetDBEntry(DBEntry *db_entry) { db_entry_ = db_entry; }
    DBEntry * GetDBEntry() { return db_entry_; }
private:
    DBEntry *db_entry_;
    DISALLOW_COPY_AND_ASSIGN(KSyncDBEntry);
};

#endif // ctrlplane_ksync_entry_h 
