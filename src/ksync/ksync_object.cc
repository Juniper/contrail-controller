/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <sys/socket.h>
#include <sys/types.h>
#if defined(__linux__)
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <linux/sockios.h>
#endif

#include <boost/bind.hpp>
#include <boost/assign/list_of.hpp>

#include <base/logging.h>
#include <db/db.h>
#include <db/db_entry.h>
#include <db/db_table.h>
#include <db/db_table_partition.h>

#include <sandesh/sandesh_trace.h>

#include "ksync_index.h"
#include "ksync_entry.h"
#include "ksync_object.h"
#include "ksync_types.h"

SandeshTraceBufferPtr KSyncErrorTraceBuf(
                      SandeshTraceBufferCreate("KSync Error", 5000));

KSyncObject::FwdRefTree  KSyncObject::fwd_ref_tree_;
KSyncObject::BackRefTree  KSyncObject::back_ref_tree_;
KSyncObjectManager *KSyncObjectManager::singleton_ = NULL;
std::auto_ptr<KSyncEntry> KSyncObjectManager::default_defer_entry_;

typedef std::map<uint32_t, std::string> VrouterErrorDescriptionMap;
VrouterErrorDescriptionMap g_error_description = {
    {ENOENT, "Entry not present"},         {EBADF, "Key mismatch"},
    {ENOMEM, "Memory insufficient"},       {EBUSY, "Object cannot be modified"},
    {EEXIST, "Object already present"},    {ENODEV, "Object not present"},
    {EINVAL, "Invalid object parameters"}, {ENOSPC, "Object table full"}};

// to be used only by test code, for triggering
// stale entry timer callback explicitly
void TestTriggerStaleEntryCleanupCb(KSyncObject *obj) {
    obj->StaleEntryCleanupCb();
}

KSyncObject::KSyncObject(const std::string &name) : need_index_(false), index_table_(),
                         delete_scheduled_(false), stale_entry_tree_(),
                         stale_entry_cleanup_timer_(NULL),
                         stale_entry_cleanup_intvl_(0),
                         stale_entries_per_intvl_(0) {
    KSyncTraceBuf = SandeshTraceBufferCreate(name, 1000);
}

KSyncObject::KSyncObject(const std::string &name, int max_index) :
                         need_index_(true), index_table_(max_index),
                         delete_scheduled_(false), stale_entry_tree_(),
                         stale_entry_cleanup_timer_(NULL),
                         stale_entry_cleanup_intvl_(0),
                         stale_entries_per_intvl_(0) {
    KSyncTraceBuf = SandeshTraceBufferCreate(name, 1000);
}

KSyncObject::~KSyncObject() {
    assert(tree_.size() == 0);
    if (stale_entry_cleanup_timer_ != NULL) {
        TimerManager::DeleteTimer(stale_entry_cleanup_timer_);
    }
}

void KSyncObject::InitStaleEntryCleanup(boost::asio::io_service &ios,
                                        uint32_t cleanup_time,
                                        uint32_t cleanup_intvl,
                                        uint16_t entries_per_intvl) {
    // init should be called only once
    assert(stale_entry_cleanup_timer_ == NULL);
    stale_entry_cleanup_timer_ = TimerManager::CreateTimer(ios,
            "KSync Stale Entry Cleanup Timer",
            TaskScheduler::GetInstance()->GetTaskId("Agent::KSync"), 0);
    stale_entry_cleanup_timer_->Start(cleanup_time,
            boost::bind(&KSyncObject::StaleEntryCleanupCb, this));
    stale_entry_cleanup_intvl_ = cleanup_intvl;
    stale_entries_per_intvl_ = entries_per_intvl;
}

void KSyncObject::Shutdown() {
    assert(fwd_ref_tree_.size() == 0);
    assert(back_ref_tree_.size() == 0);
}

KSyncEntry *KSyncObject::Find(const KSyncEntry *key) {
    Tree::iterator  it = tree_.find(*key);
    if (it != tree_.end()) {
        return it.operator->();
    }

    return NULL;
}

KSyncEntry *KSyncObject::Next(const KSyncEntry *entry) const {
    tbb::recursive_mutex::scoped_lock lock(lock_);
    Tree::const_iterator it;
    if (entry == NULL) {
        it = tree_.begin();
    } else {
        it = tree_.iterator_to(*entry);
        it++;
    }
    if (it != tree_.end()) {
        return const_cast<KSyncEntry *>(it.operator->());
    }
    return NULL;
}
KSyncEntry *KSyncObject::CreateImpl(const KSyncEntry *key) {
    // should not create an entry while scheduled for deletion
    assert(delete_scheduled_ == false);

    KSyncEntry *entry;
    if (need_index_) {
        entry = Alloc(key, index_table_.Alloc());
    } else {
        entry = Alloc(key, KSyncEntry::kInvalidIndex);
    }
    std::pair<Tree::iterator, bool> ret = tree_.insert(*entry);
    if (ret.second == false) {
        // entry with same key already exists in the Ksync tree
        // delete the allocated entry and use the entry available
        // in ksync tree
        delete entry;
        entry = ret.first.operator->();
    } else {
        // add reference only if tree insert for newly allocated
        // entry succeeds, otherwise reference for tree insertion
        // is already accounted for
        intrusive_ptr_add_ref(entry);
    }
    return entry;
}

void KSyncObject::ClearStale(KSyncEntry *entry) {
    // Clear stale marked entry and remove from stale entry tree
    entry->stale_ = false;
    stale_entry_tree_.erase(entry);
}

// Creates a KSync entry. Calling routine sets no_lookup to TRUE when its
// guaranteed that KSync entry is not present (ex: flow)
KSyncEntry *KSyncObject::Create(const KSyncEntry *key, bool no_lookup) {
    tbb::recursive_mutex::scoped_lock lock(lock_);

    KSyncEntry *entry = NULL;
    if (no_lookup == false)
        entry = Find(key);
    if (entry == NULL) {
        entry = CreateImpl(key);
    } else {
        if (entry->stale_) {
            // Clear stale marked entry
            ClearStale(entry);
        } else if (entry->GetState() != KSyncEntry::TEMP && !entry->IsDeleted()) {
            // If entry is already present, it should be in TEMP state
            // or deleted state.
            assert(0);
        }
    }

    NotifyEvent(entry, KSyncEntry::ADD_CHANGE_REQ);
    return entry;
}

KSyncEntry *KSyncObject::Create(const KSyncEntry *key) {
    return Create(key, false);
}

KSyncEntry *KSyncObject::CreateStale(const KSyncEntry *key) {
    // Should not be called without initialising stale entry
    // cleanup InitStaleEntryCleanup
    assert(stale_entry_cleanup_timer_ != NULL);
    tbb::recursive_mutex::scoped_lock lock(lock_);
    KSyncEntry *entry = Find(key);
    if (entry == NULL) {
        entry = CreateImpl(key);
    } else {
        if (entry->GetState() != KSyncEntry::TEMP && !entry->IsDeleted()) {
            // If entry is already present, it should be in TEMP state
            // or deleted state to form a stale entry
            return NULL;
        }
        // cleanup associated DB entry for KSyncDBObject
        // so that DB operation on this KSyncEntry does not happen
        // without re-claiming this entry
        CleanupOnDel(entry);
    }

    // mark the entry stale and add to stale entry tree.
    entry->stale_ = true;
    stale_entry_tree_.insert(entry);

    NotifyEvent(entry, KSyncEntry::ADD_CHANGE_REQ);
    // try starting the timer if not running already
    stale_entry_cleanup_timer_->Start(stale_entry_cleanup_intvl_,
            boost::bind(&KSyncObject::StaleEntryCleanupCb, this));
    return entry;
}

KSyncEntry *KSyncObject::GetReference(const KSyncEntry *key) {
    KSyncEntry *entry = Find(key);

    if (entry != NULL)
        return entry;

    entry = CreateImpl(key);
    entry->SetState(KSyncEntry::TEMP);
    return entry;
}

void KSyncObject::Change(KSyncEntry *entry) {
    SafeNotifyEvent(entry, KSyncEntry::ADD_CHANGE_REQ);
}

void KSyncObject::Delete(KSyncEntry *entry) {
    if (entry->stale_) {
        ClearStale(entry);
    }
    SafeNotifyEvent(entry, KSyncEntry::DEL_REQ);
}

void KSyncObject::ChangeKey(KSyncEntry *entry, uint32_t arg) {
    tbb::recursive_mutex::scoped_lock lock(lock_);
    assert(tree_.erase(*entry) > 0);
    uint32_t old_key = GetKey(entry);
    UpdateKey(entry, arg);
    std::pair<Tree::iterator, bool> ret = tree_.insert(*entry);
    if (ret.second == false) {
        // entry with the same key already exist, to proceed further
        // switch place with the existing entry
        KSyncEntry *current = ret.first.operator->();
        assert(tree_.erase(*current) > 0);
        UpdateKey(current, old_key);
        // following tree insertions should always pass
        assert(tree_.insert(*current).second == true);
        assert(tree_.insert(*entry).second == true);
    }
}

uint32_t KSyncObject::GetKey(KSyncEntry *entry) {
    assert(false);
    return 0;
}

void KSyncObject::FreeInd(KSyncEntry *entry, uint32_t index) {
    assert(tree_.erase(*entry) > 0);
    if (need_index_ == true && index != KSyncEntry::kInvalidIndex) {
        index_table_.Free(index);
    }
    PreFree(entry);
    Free(entry);
}

void KSyncObject::Free(KSyncEntry *entry) {
    delete entry;
}

void KSyncObject::SafeNotifyEvent(KSyncEntry *entry,
                                  KSyncEntry::KSyncEvent event) {
    tbb::recursive_mutex::scoped_lock lock(lock_);
    NotifyEvent(entry, event);
}

///////////////////////////////////////////////////////////////////////////////
// KSyncDBObject routines
///////////////////////////////////////////////////////////////////////////////
KSyncDBObject::KSyncDBObject(const std::string &name) : KSyncObject(name), test_id_(-1) {
    table_ = NULL;
}

KSyncDBObject::KSyncDBObject(const std::string &name,
                             int max_index) : KSyncObject(name, max_index), test_id_(-1) {
    table_ = NULL;
}

KSyncDBObject::KSyncDBObject(const std::string &name,
                             DBTableBase *table) : KSyncObject(name), test_id_(-1) {
    table_ = table;
    id_ = table->Register(boost::bind(&KSyncDBObject::Notify, this, _1, _2));
}

KSyncDBObject::KSyncDBObject(const std::string &name,
                             DBTableBase *table, int max_index)
    : KSyncObject(name, max_index), test_id_(-1) {
    table_ = table;
    id_ = table->Register(boost::bind(&KSyncDBObject::Notify, this, _1, _2));
}

KSyncDBObject::~KSyncDBObject() {
    if (table_) {
        UnregisterDb(table_);
    }
}

void KSyncDBObject::RegisterDb(DBTableBase *table) {
    assert(table_ == NULL);
    table_ = table;
    id_ = table->Register(boost::bind(&KSyncDBObject::Notify, this, _1, _2));
}

void KSyncDBObject::UnregisterDb(DBTableBase *table) {
    assert(table_ == table);
    table_->Unregister(id_);
    id_ = -1;
    table_ = NULL;
}

KSyncDBObject::DBFilterResp KSyncDBObject::DBEntryFilter(
        const DBEntry *entry, const KSyncDBEntry *ksync) {
    // Default accept all
    return DBFilterAccept;
}

void KSyncDBObject::set_test_id(DBTableBase::ListenerId id) {
    test_id_ = id;
}

DBTableBase::ListenerId KSyncDBObject::GetListenerId(DBTableBase *table) {
    assert(table_ == table);
    if (test_id_ != -1) {
        return test_id_;
    }
    return id_;
}

void KSyncDBObject::CleanupOnDel(KSyncEntry *entry) {
    KSyncDBEntry *kentry = static_cast<KSyncDBEntry *>(entry);
    if (kentry->GetDBEntry() != NULL) {
        // when object is created only because of reference it will be in
        // temp state without DB entry, deletion of which doesn't need
        // this cleanup
        kentry->GetDBEntry()->ClearState(table_, id_);
        kentry->SetDBEntry(NULL);
    }

    if (delete_scheduled()) {
        // we are in cleanup process remove all duplicate entries
        while (kentry->dup_entry_list_.empty() == false) {
            // and clear db entry state
            kentry->dup_entry_list_.front()->ClearState(table_, id_);
            kentry->dup_entry_list_.pop_front();
        }
    }
}

// DBTable notification handler.
// Generates events for the KSyncEntry state-machine based DBEntry
// Stores the KSyncEntry allocated as DBEntry-state
void KSyncDBObject::Notify(DBTablePartBase *partition, DBEntryBase *e) {
    tbb::recursive_mutex::scoped_lock lock(lock_);
    DBEntry *entry = static_cast<DBEntry *>(e);
    DBTableBase *table = partition->parent();
    assert(table_ == table);
    KSyncDBEntry *ksync =
        static_cast<KSyncDBEntry *>(entry->GetState(table, id_));
    DBFilterResp resp = DBFilterAccept;

    // cleanup is in-process, ignore All db notifications.
    if (delete_scheduled()) {
        return;
    }

    // Trigger DB Filter callback only for ADD/CHANGE, since we need to handle
    // cleanup for delete anyways.
    if (!entry->IsDeleted()) {
        resp = DBEntryFilter(entry, ksync);
    }

    if (entry->IsDeleted() || resp == DBFilterDelete ||
        resp == DBFilterDelAdd) {
        if (ksync != NULL) {
            // Check if there is any entry present in dup_entry_list
            if (!ksync->dup_entry_list_.empty()) {
                // Check if entry getting deleted is actively associated with
                // Ksync Entry.
                if (entry == ksync->GetDBEntry()) {
                    // clean up db entry state.
                    CleanupOnDel(ksync);
                    ksync->SetDBEntry(ksync->dup_entry_list_.front());
                    ksync->dup_entry_list_.pop_front();

                    // DB entry association changed, trigger re-sync.
                    if (ksync->Sync(ksync->GetDBEntry())) {
                        NotifyEvent(ksync, KSyncEntry::ADD_CHANGE_REQ);
                    }
                } else {
                    // iterate through entries and delete the
                    // corresponding DB ref.
                    KSyncDBEntry::DupEntryList::iterator it_dup;
                    for (it_dup = ksync->dup_entry_list_.begin();
                            it_dup != ksync->dup_entry_list_.end(); ++it_dup) {
                        if (entry == *it_dup)
                            break;
                    }
                    // something bad has happened if we fail to find the entry.
                    assert(it_dup != ksync->dup_entry_list_.end());
                    ksync->dup_entry_list_.erase(it_dup);
                    entry->ClearState(table_, id_);
                }
            } else {
                if (resp == DBFilterDelAdd) {
                    // clean up db entry state, so that other ksync entry can
                    // replace the states appropriately.
                    // cleanup needs to be triggered before notifying delete
                    // after that ksync entry might be already free'd
                    CleanupOnDel(ksync);
                }
                // We may get duplicate delete notification in
                // case of db entry reuse
                // add -> change ->delete(Notify) -> change -> delete(Notify)
                // delete and change gets suppresed as delete and we get
                // a duplicate delete notification
                if (ksync->IsDeleted() == false) {
                    NotifyEvent(ksync, KSyncEntry::DEL_REQ);
                }
            }
        }
        if (resp != DBFilterDelAdd) {
            // return from here except for DBFilterDelAdd case, where
            // ADD needs to be triggered after Delete
            return;
        }
        // reset ksync entry pointer, as ksync and DB entry is already
        // dissassociated
        ksync = NULL;
    }

    if (resp == DBFilterIgnore) {
        // DB filter tells us to ignore this Add/Change.
        return;
    }

    bool need_sync = false;
    if (ksync == NULL) {
        KSyncEntry *key, *found;

        // TODO : Memory is allocated and freed only for lookup. Fix this.
        key = DBToKSyncEntry(entry);
        found = Find(key);
        if (found == NULL) {
            ksync = static_cast<KSyncDBEntry *>(CreateImpl(key));
        } else {
            ksync = static_cast<KSyncDBEntry *>(found);
            if (ksync->stale()) {
                // Clear stale marked entry and remove from stale entry tree
                ClearStale(ksync);
            }
        }
        delete key;
        entry->SetState(table, id_, ksync);
        // Allow reuse of KSync Entry if the previous associated DB Entry
        // is marked deleted. This can happen when Key for OPER DB entry
        // deferes from that used in KSync Object.
        DBEntry *old_db_entry = ksync->GetDBEntry();
        if (old_db_entry != NULL) {
            // cleanup previous state id the old db entry is delete marked.
            if (old_db_entry->IsDeleted()) {
                CleanupOnDel(ksync);
            } else {
                // In case Oper DB and Ksync use different Keys, its
                // possible to have multiple Oper DB entries pointing to
                // same Ksync Entry.
                // add the entry to dup_entry_list and return
                ksync->dup_entry_list_.push_back(entry);
                return;
            }
        }
        ksync->SetDBEntry(entry);
        need_sync = true;
    } else {
        // ignore change on non-associated entry.
        if (entry != ksync->GetDBEntry()) {
            return;
        }
    }

    if (ksync->IsDeleted()) {
        // ksync entry was marked as delete, sync required.
        need_sync = true;
    }

    if (ksync->Sync(entry) || need_sync) {
        NotifyEvent(ksync, KSyncEntry::ADD_CHANGE_REQ);
    }
}

///////////////////////////////////////////////////////////////////////////////
// KSyncEntry routines
///////////////////////////////////////////////////////////////////////////////
bool KSyncEntry::IsResolved() {
    KSyncObject *obj = GetObject();
    if (obj->IsIndexValid() && index_ == kInvalidIndex)
        return false;
    if (IsDataResolved() == false)
        return false;
    return ((state_ >= IN_SYNC) && (state_ < DEL_DEFER_SYNC));
}

std::string KSyncEntry::VrouterErrorToString(uint32_t error) {
    std::map<uint32_t, std::string>::iterator iter =
        g_error_description.find(error);
    if (iter == g_error_description.end())
        return strerror(error);
    return iter->second;
}

std::string KSyncEntry::VrouterError(uint32_t error) const {
    return VrouterErrorToString(error);
}

void KSyncEntry::ErrorHandler(int err, uint32_t seq_no,
                              KSyncEvent event) const {
    if (err == 0) {
        return;
    }
    std::string error_msg = VrouterError(err);
    KSYNC_ERROR(VRouterError, "VRouter operation failed. Error <", err,
                ":", error_msg, ">. Object <", ToString(),
                ">. Operation <", AckOperationString(event),
                ">. Message number :", seq_no);

    std::stringstream sstr;
    sstr << "VRouter operation failed. Error <" << err << ":" << error_msg <<
            ">. Object <" << ToString() << ">. Operation <" <<
            AckOperationString(event) << ">. Message number :" << seq_no;
    KSYNC_ERROR_TRACE(Trace, sstr.str().c_str());
    LOG(ERROR, sstr.str().c_str());
}

std::string KSyncEntry::AckOperationString(KSyncEvent event) const {
    switch(event) {
    case ADD_ACK:
        return "Addition";

    case CHANGE_ACK:
        return "Change";

    case DEL_ACK:
        return "Deletion";

    default:
        // AckOperationString should track only acks, if something else is
        // passed convert it to EventString
        return EventString(event);
    }
}

std::string KSyncEntry::StateString() const {
    std::stringstream str;

    switch (state_) {
        case INIT:
            str << "Init";
            break;

        case TEMP:
            str << "Temp";
            break;

        case ADD_DEFER:
            str << "Add defer";
            break;

        case CHANGE_DEFER:
            str << "Change defer";
            break;

        case IN_SYNC:
            str << "In sync";
            break;

        case SYNC_WAIT:
            str << "Sync wait";
            break;

        case NEED_SYNC:
            str << "Need sync";
            break;

        case DEL_DEFER_SYNC:
            str << "Delete defer sync";
            break;

        case DEL_DEFER_REF:
            str << "Delete pending due to reference";
            break;

        case DEL_DEFER_DEL_ACK:
            str << "Delete pending due to Delete ack wait";
            break;

        case DEL_ACK_WAIT:
            str << "Delete ack wait";
            break;

        case RENEW_WAIT:
            str << "Renew wait";
            break;

        case FREE_WAIT:
            str << "Free wait";
            break;
    }

    if (stale_) {
        str << " (Stale entry) ";
    }

    str << '(' << state_ << ')';
    str << '(' << refcount_ << ')';
    return str.str();
}

std::string KSyncEntry::EventString(KSyncEvent event) const {
    std::stringstream str;
    switch (event) {
    case ADD_CHANGE_REQ:
        str << "Add/Change request";
        break;

    case ADD_ACK:
        str << "Add Ack";
        break;

    case CHANGE_ACK:
        str << "Change ack";
        break;

    case DEL_REQ:
        str << "Delete request";
        break;

    case DEL_ADD_REQ:
        str << "Delete followed by Add request";
        break;

    case DEL_ACK:
        str << "Delete ack";
        break;

    case RE_EVAL:
        str << "Re-evaluate";
        break;

    case INT_PTR_REL:
        str << "Reference release";
        break;
    case INVALID:
        str << "Invalid";
        break;
    }
    str << '(' << event << ')';
    return str.str();
}

void intrusive_ptr_add_ref(KSyncEntry *p) {
    p->refcount_++;
};

// KSync adds a reference to object when its created.
// Delete the object if reference falls to 1 and either
// (i) delete was deferred due to refcount or
// (ii) the ksync entry is in TEMP state.
void intrusive_ptr_release(KSyncEntry *p) {
    if (--p->refcount_ == 1) {
        KSyncObject *obj = p->GetObject();
        switch(p->state_) {
            case KSyncEntry::TEMP:
            // FALLTHRU
            case KSyncEntry::DEL_DEFER_REF:
                obj->SafeNotifyEvent(p, KSyncEntry::INT_PTR_REL);
                break;
            default:
                break;
        }
    }
}

///////////////////////////////////////////////////////////////////////////////
// KSyncEntry state machine.
//
// Brief description of States:
//
// INIT         : KSyncEntry created. No events notified to the object yet
// TEMP         : Temporary object created either due to reference or on
//                process restart.
//                Ex: Obj-A refers to Obj-B. If Obj-B is not present when Obj-A
//                    is to be sent, then Obj-B is created in TEMP state
//
// ADD_DEFER    : Object deferred since it has some unmet constraints.
//                Ex: Obj-A refers to Obj-B. Obj-B is not yet added to kernel.
//                    Obj-A will get to ADD_DEFER state
//                    Obj-A goes into BackRefTree for Obj-B.
//                    Creation of Obj-B will take Obj-A out of this state
// CHANGE_DEFER : Object already added to kernel, subsequent change deferred
//                since it has some unmet constraints.
//                Ex: Obj-A refers to Obj-B. Obj-B is not yet added to kernel.
//                    Obj-A will get to ADD_DEFER state
//                    Obj-A goes into BackRefTree for Obj-B.
//                    Add of Obj-B will take Obj-A out of this state
// IN_SYNC      : Object in-sync with kernel
// SYNC_WAIT    : Add or Change sent to kernel. Waiting for ACK from Kernel
// NEED_SYNC    : Object out-of-sync with kernel. Need to send a change message
//                to sync kernel state
// DEL_DEFER_SYNC:Object deleted when waiting for ACK from kernel. Delete must
//                be sent to kernel on getting ACK
// DEL_DEFER_REF: Object deleted with pending references . Delete must be sent
//                to kernel when all pending references goes away.
// DEL_DEFER_DEL_ACK: Object delete with pending delete ack wait, Delete
//                needs to be sent after receiving a del ACK.
// DEL_ACK_WAIT : Delete sent to Kernel. Waiting for ACK from kernel.
//                Can get renewed if there is request to ADD in this case
// RENEW_WAIT   : Object being renewed. Waiting for ACK of delete to renew the
//                object
// FREE_WAIT    : Object marked to be freed at the end of this function call.
//                Can only be a temporary state
//
// Brief description of Events:
// ADD_CHANGE_REQ   : Request to Add or Change an entry
// ADD_ACK,         : Ack from kernel for ADD request
// CHANGE_ACK       : Ack from kernel for CHANGE request
// DEL_REQ          : Request to DEL an entry
// DEL_ADD_REQ      : Request to DEL an entry followed by ADD for the same
// DEL_ACK          : Ack from kernel for DEL request
// RE_EVAL          : Event to re-evaluate dependencies.
//                    Ex: If Obj-A is added into Obj-B back-ref tree
//                        When Obj-B is created, RE_EVAL is sent to Obj-A
///////////////////////////////////////////////////////////////////////////////

// Utility function to handle Add of KSyncEntry.
// If operation is complete, move state to IN_SYNC. Else move to SYNC_WAIT
KSyncEntry::KSyncState KSyncSM_Add(KSyncObject *obj, KSyncEntry *entry) {
    KSyncEntry *dep;
    if ((dep = entry->UnresolvedReference()) != NULL) {
        obj->BackRefAdd(entry, dep);
        return KSyncEntry::ADD_DEFER;
    }

    entry->SetSeen();
    if (entry->Add()) {
        return KSyncEntry::IN_SYNC;
    } else {
        return KSyncEntry::SYNC_WAIT;
    }
}

// Utility function to handle Change of KSyncEntry.
// If operation is complete, move state to IN_SYNC. Else move to SYNC_WAIT
KSyncEntry::KSyncState KSyncSM_Change(KSyncObject *obj, KSyncEntry *entry) {
    KSyncEntry *dep;

    assert(entry->Seen());
    if ((dep = entry->UnresolvedReference()) != NULL) {
        obj->BackRefAdd(entry, dep);
        return KSyncEntry::CHANGE_DEFER;
    }

    if (entry->Change()) {
        return KSyncEntry::IN_SYNC;
    } else {
        return KSyncEntry::SYNC_WAIT;
    }
}

// Utility function to handle Delete of KSyncEntry.
// If there are more references to the object, then move it to DEL_DEFER
// state. Object will be deleted when all references drop. If object is
// still not seen by Kernel yet we don't have to delete it.
//
// If operation is complete, move state to IN_SYNC. Else move to SYNC_WAIT
KSyncEntry::KSyncState KSyncSM_Delete(KSyncEntry *entry) {
    if (entry->GetRefCount() > 1) {
        return KSyncEntry::DEL_DEFER_REF;
    }

    assert(entry->GetRefCount() == 1);
    if (!entry->Seen() && entry->AllowDeleteStateComp()) {
        return KSyncEntry::FREE_WAIT;
    }
    if (entry->Delete()) {
        return KSyncEntry::FREE_WAIT;
    } else {
        return KSyncEntry::DEL_ACK_WAIT;
    }
}

// Utility function to handle Delete followed by ADD of KSyncEntry.
// delete is triggered irrespective of the references to the object
// followed by ADD of the object
//
// If operation is complete, move state to IN_SYNC. Else move to SYNC_WAIT
KSyncEntry::KSyncState KSyncSM_DeleteAdd(KSyncObject *obj, KSyncEntry *entry) {
    // DeleteAdd operation is not supported/defined for stale entries
    // when such an operation is required for stale entry, it must be
    // sufficient to trigger only a delete and let Add happen when
    // entry is ready to become non-stale
    assert(!entry->stale());

    // NOTE this API doesnot support managing references for delete trigger
    if (entry->Seen() || !entry->AllowDeleteStateComp()) {
        if (!entry->Delete()) {
            // move to renew wait to trigger Add on DEL_ACK
            return KSyncEntry::RENEW_WAIT;
        }
    }

    return KSyncSM_Add(obj, entry);
}

//
//
// ADD_CHANGE_REQ :
//      If entry has unresolved references, move it to ADD_DEFER
//      Else, send ADD message and move to SYNC_WAIT state
//
// No other events are expected in this state
KSyncEntry::KSyncState KSyncSM_Init(KSyncObject *obj, KSyncEntry *entry,
                                    KSyncEntry::KSyncEvent event) {
    KSyncEntry::KSyncState state = KSyncEntry::INIT;

    assert(entry->GetRefCount());
    switch (event) {
    case KSyncEntry::ADD_CHANGE_REQ:
        state = KSyncSM_Add(obj, entry);
        break;

    default:
        assert(0);
        break;
    }

    return state;
}

// ADD_CHANGE_REQ :
//      ADD_CHANGE_REQ event for an entry in TEMP state.
//      If entry has unresolved references, move it to ADD_DEFER
//      Else, send ADD message and move to SYNC_WAIT state
//
// DEL_REQ :
//      DEL_REQ event for an entry in TEMP state. Can happen only when reference
//      for the TEMP entry is dropped
//
//      Explicit DEL_REQ event is not expected. This is enforced by checking
//      ref-count
//
//      Event notified when refcount for object goes to 1 In TEMP state.
//      Entry not sent to kernel, so dont send delete message
//      Delete object in this state
//
KSyncEntry::KSyncState KSyncSM_Temp(KSyncObject *obj, KSyncEntry *entry,
                                    KSyncEntry::KSyncEvent event) {
    KSyncEntry::KSyncState state = KSyncEntry::TEMP;

    assert(entry->GetRefCount());
    switch (event) {
    case KSyncEntry::ADD_CHANGE_REQ:
    case KSyncEntry::DEL_ADD_REQ:
        state = KSyncSM_Add(obj, entry);
        break;

    case KSyncEntry::INT_PTR_REL:
    case KSyncEntry::DEL_REQ:
        if (entry->GetRefCount() == 1) {
            state = KSyncEntry::FREE_WAIT;
        }
        break;

    default:
        assert(0);
        break;
    }

    return state;
}

// ADD_CHANGE_REQ :
//  ADD_CHANGE_REQ event for an entry in ADD_DEFER state.
//  Remove the old dependency constraint.
//  Re-evaluate to see if there are any further unmet dependencies
//
// RE_EVAL:
//  Triggred from back-ref tree when KSyncEntry waited on is added to Kernel
//  Entry would already be removed from backref tree
//  Re-evaluate to see if there are any further unmet dependencies
//
// DEL_REQ :
//  Delete when entry is not yet added to kernel. Dont delete entry. Move state
//  based on ref-count
KSyncEntry::KSyncState KSyncSM_AddDefer(KSyncObject *obj, KSyncEntry *entry,
                                        KSyncEntry::KSyncEvent event) {
    KSyncEntry::KSyncState state = KSyncEntry::ADD_DEFER;

    assert(entry->GetRefCount());
    switch (event) {
    case KSyncEntry::ADD_CHANGE_REQ:
        obj->BackRefDel(entry);
        // FALLTHRU
    case KSyncEntry::RE_EVAL:
        state = KSyncSM_Add(obj, entry);
        break;

    // Remove any back-ref entry
    // Free entry if there are no more references. Else wait in TEMP
    // state for either release of reference or for movement to different
    // state.
    case KSyncEntry::DEL_REQ:
        obj->BackRefDel(entry);
        if (entry->AllowDeleteStateComp() == false) {
            state = KSyncSM_Delete(entry);
        } else if (entry->GetRefCount() > 1) {
            state = KSyncEntry::TEMP;
        } else {
            state = KSyncEntry::FREE_WAIT;
        }
        break;

    case KSyncEntry::DEL_ADD_REQ:
        obj->BackRefDel(entry);
        state = KSyncSM_DeleteAdd(obj, entry);
        break;

    case KSyncEntry::INT_PTR_REL:
        break;

    default:
        assert(0);
        break;
    }

    return state;
}

// ADD_CHANGE_REQ :
//  ADD_CHANGE_REQ event for an entry in CHANGE_DEFER state.
//  Remove the old dependency constraint.
//  Re-evaluate to see if there are any further unmet dependencies
//
// RE_EVAL:
//  Triggred from back-ref tree when KSyncEntry waited on is added to Kernel
//  Entry would already be removed from backref tree
//  Re-evaluate to see if there are any further unmet dependencies
//
// DEL_REQ :
//  Move state based on ref-count.
KSyncEntry::KSyncState KSyncSM_ChangeDefer(KSyncObject *obj, KSyncEntry *entry,
                                           KSyncEntry::KSyncEvent event) {
    KSyncEntry::KSyncState state = KSyncEntry::CHANGE_DEFER;

    assert(entry->GetRefCount());
    switch (event) {
    case KSyncEntry::ADD_CHANGE_REQ:
        obj->BackRefDel(entry);
        // FALLTHRU
    case KSyncEntry::RE_EVAL:
        state = KSyncSM_Change(obj, entry);
        break;

    // Remove any back-ref entry and process delete
    case KSyncEntry::DEL_REQ:
        obj->BackRefDel(entry);
        state = KSyncSM_Delete(entry);
        break;

    case KSyncEntry::DEL_ADD_REQ:
        obj->BackRefDel(entry);
        state = KSyncSM_DeleteAdd(obj, entry);
        break;

    case KSyncEntry::INT_PTR_REL:
        break;

    default:
        assert(0);
        break;
    }

    return state;
}

// Object state IN-SYNC with kernel
//
// ADD_CHANGE_REQ :
// Invoke Change on the object
//
// DEL_REQ :
//  Delete of entry that is already added to kernel.
KSyncEntry::KSyncState KSyncSM_InSync(KSyncObject *obj, KSyncEntry *entry,
                                      KSyncEntry::KSyncEvent event) {
    KSyncEntry::KSyncState state = KSyncEntry::IN_SYNC;

    assert(entry->GetRefCount());
    switch (event) {
    case KSyncEntry::ADD_CHANGE_REQ:
        state = KSyncSM_Change(obj, entry);
        break;

    case KSyncEntry::DEL_REQ:
        state = KSyncSM_Delete(entry);
        break;

    case KSyncEntry::DEL_ADD_REQ:
        state = KSyncSM_DeleteAdd(obj, entry);
        break;

    case KSyncEntry::INT_PTR_REL:
        break;

    default:
        assert(0);
        break;
    }

    return state;
}

// Entry waiting on ACK or Add or Change
// If event is change request, move to NEED_SYNC
// If event is delete request, move to DEL_DEFER for references to drop
KSyncEntry::KSyncState KSyncSM_SyncWait(KSyncObject *obj, KSyncEntry *entry,
                                        KSyncEntry::KSyncEvent event) {
    KSyncEntry::KSyncState state = KSyncEntry::SYNC_WAIT;

    assert(entry->GetRefCount());
    switch (event) {
    case KSyncEntry::ADD_CHANGE_REQ:
        state = KSyncEntry::NEED_SYNC;
        break;

    case KSyncEntry::ADD_ACK:
    case KSyncEntry::CHANGE_ACK:
        if (entry->del_add_pending()) {
            // del_add_pending trigger DeleteAdd
            entry->set_del_add_pending(false);
            state = KSyncSM_DeleteAdd(obj, entry);
        } else {
            state = KSyncEntry::IN_SYNC;
        }
        break;

    case KSyncEntry::DEL_REQ:
        state = KSyncEntry::DEL_DEFER_SYNC;
        entry->set_del_add_pending(false);
        break;

    case KSyncEntry::DEL_ADD_REQ:
        // entry is waiting for Ack, mark del_add_pending flag
        // to trigger DeleteAdd on receiving Ack
        entry->set_del_add_pending(true);
        break;

    case KSyncEntry::INT_PTR_REL:
        break;

    default:
        assert(0);
        break;
    }

    return state;
}

// NEED_SYNC state means object was modified while waiting for ACK
KSyncEntry::KSyncState KSyncSM_NeedSync(KSyncObject *obj, KSyncEntry *entry,
                                        KSyncEntry::KSyncEvent event) {
    KSyncEntry::KSyncState state = KSyncEntry::NEED_SYNC;

    assert(entry->GetRefCount());
    switch (event) {
    // Continue in NEED_SYNC state on change
    case KSyncEntry::ADD_CHANGE_REQ:
        break;

    // Wait for ACK to arrive in DEL_DEFER_SYNC state
    case KSyncEntry::DEL_REQ:
        state = KSyncEntry::DEL_DEFER_SYNC;
        entry->set_del_add_pending(false);
        break;

    case KSyncEntry::DEL_ADD_REQ:
        // entry is waiting for Ack, mark del_add_pending flag
        // to trigger DeleteAdd on receiving Ack
        entry->set_del_add_pending(true);
        break;

    // Try to resend on getting ACK of pending operation
    case KSyncEntry::ADD_ACK:
    case KSyncEntry::CHANGE_ACK:
        if (entry->del_add_pending()) {
            // del_add_pending trigger DeleteAdd
            entry->set_del_add_pending(false);
            state = KSyncSM_DeleteAdd(obj, entry);
        } else {
            state = KSyncSM_Change(obj, entry);
        }
        break;

    case KSyncEntry::INT_PTR_REL:
        break;

    default:
        assert(0);
        break;
    }

    return state;
}

// Object waiting for DELETE to be sent.
// ADD_CHANGE_REQ will result in renew of object. Send only a Change
// On ADD/CHANGE ACK, try sending delete
KSyncEntry::KSyncState KSyncSM_DelPending_Sync(KSyncObject *obj,
                                               KSyncEntry *entry,
                                               KSyncEntry::KSyncEvent event) {
    KSyncEntry::KSyncState state = KSyncEntry::DEL_DEFER_SYNC;

    assert(entry->GetRefCount());
    switch (event) {
    case KSyncEntry::ADD_CHANGE_REQ:
        state = KSyncEntry::NEED_SYNC;
        break;

    case KSyncEntry::DEL_ADD_REQ:
        // entry is waiting for Ack, mark del_add_pending flag
        // to trigger DeleteAdd on receiving Ack
        entry->set_del_add_pending(true);
        state = KSyncEntry::NEED_SYNC;
        break;

    case KSyncEntry::ADD_ACK:
    case KSyncEntry::CHANGE_ACK:
        state = KSyncSM_Delete(entry);
        break;

    case KSyncEntry::INT_PTR_REL:
        break;

    default:
        assert(0);
        break;
    }

    return state;
}

// Object waiting for DELETE to be sent.
// ADD_CHANGE_REQ will result in renew of object. Send only a Change
// On ADD/CHANGE ACK, try sending delete
KSyncEntry::KSyncState KSyncSM_DelPending_Ref(KSyncObject *obj,
                                              KSyncEntry *entry,
                                              KSyncEntry::KSyncEvent event) {
    KSyncEntry::KSyncState state = KSyncEntry::DEL_DEFER_REF;

    assert(entry->GetRefCount());
    switch (event) {
    case KSyncEntry::ADD_CHANGE_REQ:
        if (!entry->Seen()) {
            // Trigger Add if entry was not seen earlier
            state = KSyncSM_Add(obj, entry);
        } else {
            state = KSyncSM_Change(obj, entry);
        }
        break;

    case KSyncEntry::INT_PTR_REL:
    case KSyncEntry::DEL_REQ:
        assert(entry->GetRefCount()== 1);
        state = KSyncSM_Delete(entry);
        break;

    case KSyncEntry::DEL_ADD_REQ:
        state = KSyncSM_DeleteAdd(obj, entry);
        break;

    default:
        assert(0);
        break;
    }

    return state;
}

// Object waiting for DELETE to be sent.
// ADD_CHANGE_REQ will result in renew of object. Send only a Change
// On DEL ACK, try sending delete
KSyncEntry::KSyncState KSyncSM_DelPending_DelAck(KSyncObject *obj,
                                                 KSyncEntry *entry,
                                                 KSyncEntry::KSyncEvent event) {
    KSyncEntry::KSyncState state = KSyncEntry::DEL_DEFER_DEL_ACK;

    assert(entry->GetRefCount());
    assert(entry->AllowDeleteStateComp() == false);

    switch (event) {
    case KSyncEntry::ADD_CHANGE_REQ:
        state = KSyncEntry::RENEW_WAIT;
        entry->set_del_add_pending(false);
        break;

    case KSyncEntry::DEL_ACK:
        if (entry->del_add_pending()) {
            // del_add_pending trigger DeleteAdd
            entry->set_del_add_pending(false);
            state = KSyncSM_DeleteAdd(obj, entry);
        } else {
            state = KSyncSM_Delete(entry);
        }
        break;

    case KSyncEntry::DEL_ADD_REQ:
        // entry is waiting for Ack, mark del_add_pending flag
        // to trigger DeleteAdd on receiving Ack
        entry->set_del_add_pending(true);
        break;

    case KSyncEntry::INT_PTR_REL:
        break;

    default:
        assert(0);
        break;
    }

    return state;
}

// Object waiting for ACK of DELETE sent earlier
// ADD_CHANGE_REQ will result in renew of object. TODO: This is TBD
KSyncEntry::KSyncState KSyncSM_DelAckWait(KSyncObject *obj, KSyncEntry *entry,
                                          KSyncEntry::KSyncEvent event) {
    KSyncEntry::KSyncState state = KSyncEntry::DEL_ACK_WAIT;

    assert(entry->GetRefCount());
    switch (event) {
    case KSyncEntry::ADD_CHANGE_REQ:
        state = KSyncEntry::RENEW_WAIT;
        entry->set_del_add_pending(false);
        break;

    case KSyncEntry::DEL_ACK:
        if (entry->del_add_pending()) {
            // del_add_pending trigger DeleteAdd
            entry->set_del_add_pending(false);
            state = KSyncSM_DeleteAdd(obj, entry);
        } else {
            if (entry->GetRefCount() > 1) {
                state = KSyncEntry::TEMP;
            } else {
                state = KSyncEntry::FREE_WAIT;
            }
        }
        break;

    case KSyncEntry::DEL_ADD_REQ:
        // entry is waiting for Ack, mark del_add_pending flag
        // to trigger DeleteAdd on receiving Ack
        entry->set_del_add_pending(true);
        break;

    case KSyncEntry::INT_PTR_REL:
        break;

    default:
        assert(0);
        break;
    }

    return state;
}

// TODO: Object renewal. This is not yet handled
KSyncEntry::KSyncState KSyncSM_RenewWait(KSyncObject *obj, KSyncEntry *entry,
                                         KSyncEntry::KSyncEvent event) {
    KSyncEntry::KSyncState state = KSyncEntry::RENEW_WAIT;

    assert(entry->GetRefCount());
    switch (event) {
    case KSyncEntry::ADD_CHANGE_REQ:
        entry->set_del_add_pending(false);
        break;

    case KSyncEntry::DEL_REQ:
        entry->set_del_add_pending(false);
        if (entry->AllowDeleteStateComp()) {
            state = KSyncEntry::DEL_ACK_WAIT;
        } else {
            state = KSyncEntry::DEL_DEFER_DEL_ACK;
        }
        break;

    case KSyncEntry::DEL_ADD_REQ:
        // entry is waiting for Ack, mark del_add_pending flag
        // to trigger DeleteAdd on receiving Ack
        entry->set_del_add_pending(true);
        break;

    case KSyncEntry::DEL_ACK:
        if (entry->del_add_pending()) {
            // del_add_pending trigger DeleteAdd
            entry->set_del_add_pending(false);
            state = KSyncSM_DeleteAdd(obj, entry);
        } else {
            state = KSyncSM_Add(obj, entry);
        }
        break;

    case KSyncEntry::INT_PTR_REL:
        break;

    default:
        assert(0);
        break;
    }

    return state;
}

void KSyncObject::NotifyEvent(KSyncEntry *entry, KSyncEntry::KSyncEvent event) {

    KSyncEntry::KSyncState state;
    bool dep_reval = false;
    KSyncEntry::KSyncState from_state = entry->GetState();

    if (DoEventTrace()) {
        KSYNC_TRACE(Event, this, entry->ToString(), entry->StateString(),
                    entry->EventString(event));
    }
    switch (entry->GetState()) {
        case KSyncEntry::INIT:
            state = KSyncSM_Init(this, entry, event);
            break;

        case KSyncEntry::TEMP:
            dep_reval = true;
            state = KSyncSM_Temp(this, entry, event);
            break;

        case KSyncEntry::ADD_DEFER:
            dep_reval = true;
            state = KSyncSM_AddDefer(this, entry, event);
            break;

        case KSyncEntry::CHANGE_DEFER:
            dep_reval = true;
            state = KSyncSM_ChangeDefer(this, entry, event);
            break;

        case KSyncEntry::IN_SYNC:
            state = KSyncSM_InSync(this, entry, event);
            break;

        case KSyncEntry::SYNC_WAIT:
            dep_reval = true;
            state = KSyncSM_SyncWait(this, entry, event);
            break;

        case KSyncEntry::NEED_SYNC:
            state = KSyncSM_NeedSync(this, entry, event);
            break;

        case KSyncEntry::DEL_DEFER_SYNC:
            state = KSyncSM_DelPending_Sync(this, entry, event);
            break;

        case KSyncEntry::DEL_DEFER_REF:
            dep_reval = true;
            state = KSyncSM_DelPending_Ref(this, entry, event);
            break;

        case KSyncEntry::DEL_DEFER_DEL_ACK:
            state = KSyncSM_DelPending_DelAck(this, entry, event);
            break;

        case KSyncEntry::DEL_ACK_WAIT:
            state = KSyncSM_DelAckWait(this, entry, event);
            break;

        case KSyncEntry::RENEW_WAIT:
            dep_reval = true;
            state = KSyncSM_RenewWait(this, entry, event);
            break;

        default:
            assert(0);
            break;
    }

    entry->SetState(state);
    entry->RecordTransition(from_state, state, event);

    if (dep_reval == true && entry->IsResolved() &&
        entry->ShouldReEvalBackReference()) {
        BackRefReEval(entry);
    }

    if (state == KSyncEntry::FREE_WAIT || state == KSyncEntry::TEMP) {
        CleanupOnDel(entry);
    }

    if (state == KSyncEntry::FREE_WAIT) {
        intrusive_ptr_release(entry);
        FreeInd(entry, entry->GetIndex());
    }

    if (tree_.empty() == true) {
        EmptyTable();
    }
}

void KSyncObject::NetlinkAckInternal(KSyncEntry *entry, KSyncEntry::KSyncEvent event) {
    tbb::recursive_mutex::scoped_lock lock(lock_);
    entry->Response();
    NotifyEvent(entry, event);
}

bool KSyncObject::StaleEntryCleanupCb() {
    // donot reschedule timer if no stale entries
    if (stale_entry_tree_.empty()) {
        return false;
    }

    uint32_t count = 0;
    std::set<KSyncEntry::KSyncEntryPtr>::iterator it = stale_entry_tree_.begin();
    while (it != stale_entry_tree_.end()) {
        if (count == stale_entries_per_intvl_) {
            break;
        }
        KSyncEntry *entry = (*it).get();
        // Notify entry of stale timer expiration
        entry->StaleTimerExpired();
        // Delete removes entry from stale entry tree
        Delete(entry);
        it = stale_entry_tree_.begin();
        count++;
    }

    // iterate entries and trigger delete
    return stale_entry_cleanup_timer_->Reschedule(stale_entry_cleanup_intvl_);
}

void KSyncObject::NetlinkAck(KSyncEntry *entry, KSyncEntry::KSyncEvent event) {
    NetlinkAckInternal(entry, event);
}

///////////////////////////////////////////////////////////////////////////////
// KSyncEntry dependency management
///////////////////////////////////////////////////////////////////////////////
void KSyncObject::BackRefAdd(KSyncEntry *key, KSyncEntry *reference) {
    KSyncFwdReference *fwd_node = new KSyncFwdReference(key, reference);
    FwdRefTree::iterator fwd_it = fwd_ref_tree_.find(*fwd_node);
    assert(fwd_it == fwd_ref_tree_.end());
    fwd_ref_tree_.insert(*fwd_node);
    intrusive_ptr_add_ref(key);
    intrusive_ptr_add_ref(reference);

    KSyncBackReference *back_node = new KSyncBackReference(reference, key);
    BackRefTree::iterator back_it = back_ref_tree_.find(*back_node);
    assert(back_it == back_ref_tree_.end());
    back_ref_tree_.insert(*back_node);
}

void KSyncObject::BackRefDel(KSyncEntry *key) {
    KSyncFwdReference fwd_search_node(key, NULL);
    FwdRefTree::iterator fwd_it = fwd_ref_tree_.find(fwd_search_node);
    if (fwd_it == fwd_ref_tree_.end()) {
        return;
    }
    KSyncFwdReference *entry = fwd_it.operator->();
    KSyncEntry *reference = entry->reference_;
    fwd_ref_tree_.erase(fwd_it);
    delete entry;

    KSyncBackReference back_search_node(reference, key);
    BackRefTree::iterator back_it = back_ref_tree_.find(back_search_node);
    assert(back_it != back_ref_tree_.end());
    KSyncBackReference *back_node = back_it.operator->();
    back_ref_tree_.erase(back_it);
    delete back_node;

    intrusive_ptr_release(key);
    intrusive_ptr_release(reference);
}

void KSyncObject::BackRefReEval(KSyncEntry *key) {
    std::vector<KSyncEntry *> buf;
    KSyncBackReference node(key, NULL);

    for (BackRefTree::iterator it = back_ref_tree_.upper_bound(node);
         it != back_ref_tree_.end(); ) {
        BackRefTree::iterator it_work = it;

        KSyncBackReference *entry = it_work.operator->();
        if (entry->key_ != key) {
            break;
        }
        KSyncEntry *back_ref = entry->back_reference_;
        buf.push_back(back_ref);
        BackRefDel(entry->back_reference_);
        it = back_ref_tree_.upper_bound(node);
    }

    std::vector<KSyncEntry *>::iterator it = buf.begin();
    while (it != buf.end()) {
        tbb::recursive_mutex::scoped_lock lock((*it)->GetObject()->lock_);
        NotifyEvent(*it, KSyncEntry::RE_EVAL);
        it++;
    }
}

bool KSyncObjectManager::Process(KSyncObjectEvent *event) {
    switch(event->event_) {
    case KSyncObjectEvent::UNREGISTER:
        if (event->obj_->Size() == 0) {
            delete event->obj_;
        }
        break;
    case KSyncObjectEvent::DEL:
        {
            int count = 0;
            // hold reference to entry to ensure the pointer sanity
            KSyncEntry::KSyncEntryPtr entry(NULL);
            if (event->ref_.get() == NULL) {
                event->obj_->set_delete_scheduled();
                if (event->obj_->IsEmpty()) {
                    // trigger explicit empty table callback for client to
                    // complete deletion of object in KSync Context.
                    event->obj_->EmptyTable();
                    break;
                }
                // get the first entry to start with
                entry = event->obj_->Next(NULL);
            } else {
                entry = event->ref_.get();
            }

            // hold reference to entry to ensure the pointer sanity
            // next entry can get free'd in certain cases while processing
            // current entry
            KSyncEntry::KSyncEntryPtr next_entry(NULL);
            while (entry.get() != NULL) {
                next_entry = event->obj_->Next(entry.get());
                count++;
                if (entry->IsDeleted() == false) {
                    // trigger delete if entry is not marked delete already.
                    event->obj_->Delete(entry.get());
                }

                if (count == kMaxEntriesProcess && next_entry.get() != NULL) {
                    // update reference with which entry to start with
                    // in next iteration.
                    event->ref_ = next_entry.get();
                    // yeild and re-enqueue event for processing later.
                    event_queue_->Enqueue(event);

                    // release reference to deleted entry.
                    entry = NULL;
                    return false;
                }
                entry = next_entry.get();
            }
            break;
        }
    default:
        assert(0);
    }
    delete event;
    return true;
}

void KSyncObjectManager::Enqueue(KSyncObjectEvent *event) {
    event_queue_->Enqueue(event);
}

void KSyncObjectManager::Unregister(KSyncObject *table) {
    KSyncObjectEvent *event = new KSyncObjectEvent(table,
                                      KSyncObjectEvent::UNREGISTER);
    singleton_->Enqueue(event);
}

KSyncObjectManager::KSyncObjectManager() {
    event_queue_ = new WorkQueue<KSyncObjectEvent *>
                   (TaskScheduler::GetInstance()->GetTaskId("Agent::KSync"), 0,
                    boost::bind(&KSyncObjectManager::Process, this, _1));
}

KSyncObjectManager::~KSyncObjectManager() {
    delete event_queue_;
}

KSyncObjectManager *KSyncObjectManager::Init() {
    if (singleton_ == NULL) {
        singleton_ = new KSyncObjectManager();
    }
    return singleton_;
}

void KSyncObjectManager::Shutdown() {
    if (singleton_) {
        delete singleton_;
    }
    singleton_ = NULL;
}

// Create a KSync Object event to trigger Delete of all the KSync Entries
// present in the given object.
// Once the delete is scheduled new entry creation is not allowed for this
// object and EmptyTable callback is trigger when all the entries of given
// object are cleaned up. As part of which client can delete the object.
//
// This API can be used to clean up KSync objects irrespective of config
// or oper tables

void KSyncObjectManager::Delete(KSyncObject *object) {
    KSyncObjectEvent *event = new KSyncObjectEvent(object,
                                      KSyncObjectEvent::DEL);
    Enqueue(event);
}

KSyncObjectManager* KSyncObjectManager::GetInstance() {
    return singleton_;
}

// Create a dummy KSync Entry. This entry will all ways be in deferred state
// Any back-ref added to it will never get resolved.
// Can be used to defer an incomplete entry

class KSyncDummyEntry : public KSyncEntry {
public:
    KSyncDummyEntry() : KSyncEntry() { }
    virtual ~KSyncDummyEntry() { }
    virtual bool IsLess(const KSyncEntry &rhs) const {
        return false;
    }
    std::string ToString() const { return "Dummy"; }
    bool Add() { return false;}
    bool Change() { return false; }
    bool Delete() { return false; }
    KSyncObject *GetObject() const { return NULL; }
    KSyncEntry *UnresolvedReference() { return NULL; }
    bool IsDataResolved() {return false;}
private:
    DISALLOW_COPY_AND_ASSIGN(KSyncDummyEntry);
};

KSyncEntry *KSyncObjectManager::default_defer_entry() {
    if (default_defer_entry_.get() == NULL) {
        default_defer_entry_.reset(new KSyncDummyEntry());
    }
    return default_defer_entry_.get();
}
