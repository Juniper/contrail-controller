/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/bgp_update_sender.h"

#include <boost/bind.hpp>
#include <boost/foreach.hpp>

#include <map>
#include <string>

#include "base/task_annotations.h"
#include "bgp/ipeer.h"
#include "bgp/bgp_ribout.h"
#include "bgp/bgp_ribout_updates.h"
#include "db/db.h"

using std::auto_ptr;
using std::make_pair;
using std::map;
using std::string;
using std::vector;

//
// This struct represents RibOut specific state for a PeerState.  There's one
// instance of this for each RibOut that an IPeerUpdate has joined.
//
// The PeerRibState contains a bit mask to keep track of the QueueIds that are
// currently active for the RibOut for the IPeerUpdate.
//
struct BgpSenderPartition::PeerRibState {
    PeerRibState() : qactive(0) { }
    uint8_t qactive;
};

//
// This nested class represents IPeerUpdate related state that's specific to
// the BgpSenderPartition.
//
// A PeerState contains a Map of the index for a RibState to a PeerRibState.
// Each entry in the map logically represents the state of the peer for the
// ribout.
//
// The Map is used in conjunction with the RibStateMap in BgpSenderPartition
// to implement regular and circular iterator classes that are used to walk
// through all the RibState entries for a peer.
//
// A PeerState maintains the in_sync and send_ready state for the IPeerUpdate.
//
// The PeerState is considered to be send_ready when the underlying socket is
// is writable.  Note that the send_ready state in the PeerState may be out of
// date with the actual socket state because the socket could have got blocked
// when writing from another partition. Hence IPeerUpdate::send_ready() is the
// more authoritative source.
//
// The PeerState is considered to be in_sync if it's send_ready and the marker
// IPeerUpdate the peer has merged with the tail marker for all QueueIds in
// all RiBOuts for which the IPeerUpdate is subscribed.
//
// The PeerState keeps count of the number of active RibOuts for each QueueId.
// A (RibOut, QueueId) pair is considered to be active if the PeerState isn't
// send_ready and there's RouteUpdates for the pair.
//
class BgpSenderPartition::PeerState {
public:
    typedef map<size_t, PeerRibState> Map;

    class iterator : public boost::iterator_facade<
        iterator, RibOut, boost::forward_traversal_tag> {
    public:
        explicit iterator(const RibStateMap &indexmap, Map *map, size_t index)
            : indexmap_(indexmap), map_(map), index_(index) {
        }
        size_t index() const { return index_; }
        RibState *rib_state() { return indexmap_.At(index_); }
        const PeerRibState &peer_rib_state() const { return (*map_)[index_]; }

    private:
        friend class boost::iterator_core_access;
        void increment() {
            Map::const_iterator loc = map_->upper_bound(index_);
            if (loc == map_->end()) {
                index_ = -1;
            } else {
                index_ = loc->first;
            }
        }
        bool equal(const iterator &rhs) const {
            return index_ == rhs.index_;
        }
        RibOut &dereference() const;

        const RibStateMap &indexmap_;
        Map *map_;
        size_t index_;
    };

    class circular_iterator : public boost::iterator_facade<
        circular_iterator, RibOut, boost::forward_traversal_tag> {
    public:
        explicit circular_iterator(const RibStateMap &indexmap, Map *map,
                                   int start, bool is_valid)
        : indexmap_(indexmap), map_(map), index_(-1), match_(true) {
            if (map_->empty()) {
                return;
            }
            Map::const_iterator loc = map_->lower_bound(start);
            if (loc == map_->end()) {
                loc = map_->begin();
            }
            index_ = loc->first;
            if (is_valid) match_ = false;
        }
        int index() const { return index_; }
        RibState *rib_state() { return indexmap_.At(index_); }
        const PeerRibState &peer_rib_state() const { return (*map_)[index_]; }

    private:
        friend class boost::iterator_core_access;
        void increment() {
            match_ = true;
            assert(!map_->empty());
            Map::const_iterator loc = map_->upper_bound(index_);
            if (loc == map_->end()) {
                loc = map_->begin();
            }
            index_ = loc->first;
        }
        bool equal(const circular_iterator &rhs) const {
            return ((match_ == rhs.match_) && (index_ == rhs.index_));
        }
        RibOut &dereference() const;

        const RibStateMap &indexmap_;
        Map *map_;
        int index_;
        bool match_;
    };

    explicit PeerState(IPeerUpdate *peer)
        : key_(peer), index_(-1),
        qactive_cnt_(RibOutUpdates::QCOUNT),
        in_sync_(true), rib_iterator_(BitSet::npos) {
        send_ready_ = true;
        for (int i = 0; i < RibOutUpdates::QCOUNT; i++) {
            qactive_cnt_[i] = 0;
        }
    }

    void Add(RibState *rs);

    void Remove(RibState *rs);

    bool IsMember(size_t index) const {
        return rib_bitset_.test(index);
    }

    iterator begin(const RibStateMap &indexmap) {
        Map::const_iterator it = rib_set_.begin();
        size_t index = (it != rib_set_.end() ? it->first : -1);
        return iterator(indexmap, &rib_set_, index);
    }
    iterator end(const RibStateMap &indexmap) {
        return iterator(indexmap, &rib_set_, -1);
    }

    circular_iterator circular_begin(const RibStateMap &indexmap) {
        return circular_iterator(indexmap, &rib_set_, rib_iterator_, true);
    }
    circular_iterator circular_end(const RibStateMap &indexmap) {
        return circular_iterator(indexmap, &rib_set_, rib_iterator_, false);
    }

    void SetIteratorStart(size_t start) { rib_iterator_ = start; }

    void SetQueueActive(size_t rib_index, int queue_id) {
        CHECK_CONCURRENCY("bgp::SendUpdate");
        Map::iterator loc = rib_set_.find(rib_index);
        assert(loc != rib_set_.end());
        if (!BitIsSet(loc->second.qactive, queue_id)) {
            SetBit(loc->second.qactive, queue_id);
            qactive_cnt_[queue_id]++;
        }
    }

    void SetQueueInactive(size_t rib_index, int queue_id) {
        CHECK_CONCURRENCY("bgp::SendUpdate", "bgp::PeerMembership");
        Map::iterator loc = rib_set_.find(rib_index);
        assert(loc != rib_set_.end());
        if (BitIsSet(loc->second.qactive, queue_id)) {
            ClearBit(loc->second.qactive, queue_id);
            qactive_cnt_[queue_id]--;
        }
    }

    bool IsQueueActive(size_t rib_index, int queue_id) {
        CHECK_CONCURRENCY("bgp::SendUpdate");
        Map::iterator loc = rib_set_.find(rib_index);
        assert(loc != rib_set_.end());
        return BitIsSet(loc->second.qactive, queue_id);
    }

    int QueueCount(int queue_id) { return qactive_cnt_[queue_id]; }

    IPeerUpdate *peer() const { return key_; }
    void set_index(size_t index) { index_ = index; }
    size_t index() const { return index_; }

    bool in_sync() const { return in_sync_; }
    void clear_sync() { in_sync_ = false; }
    void SetSync();

    bool send_ready() const { return send_ready_; }
    void set_send_ready(bool toggle) { send_ready_ = toggle; }

    bool empty() const { return rib_set_.empty(); }

    bool CheckInvariants() const {
        for (int i = 0; i < RibOutUpdates::QCOUNT; i++) {
            CHECK_INVARIANT(qactive_cnt_[i] <= (int) rib_set_.size());
        }
        return true;
    }

private:
    IPeerUpdate *key_;
    size_t index_;          // assigned from PeerStateMap
    Map rib_set_;           // list of RibOuts advertised by the peer.
    BitSet rib_bitset_;     // bitset of RibOuts advertised by the peer
    vector<int> qactive_cnt_;
    bool in_sync_;          // whether the peer may dequeue tail markers.
    tbb::atomic<bool> send_ready_;    // whether the peer may send updates.
    size_t rib_iterator_;   // index of last processed rib.

    DISALLOW_COPY_AND_ASSIGN(PeerState);
};

//
// This nested class represents RibOut related state that's specific to the
// BgpSenderPartition.
//
// A RibState contains a BitSet of all the peers that are advertising the
// RibOut associated with the RibState.
//
// The BitSet is used in conjunction with PeerStateMap in BgpSenderPartition
// to implement an iterator that is used to walk through all the PeerState
// entries for the ribout.
//
class BgpSenderPartition::RibState {
public:
    class iterator : public boost::iterator_facade<
        iterator, PeerState, boost::forward_traversal_tag> {
    public:
        explicit iterator(const PeerStateMap &indexmap,
                          const BitSet &set, size_t index)
            : indexmap_(indexmap), set_(set), index_(index) {
        }
        size_t index() const { return index_; }

    private:
        friend class boost::iterator_core_access;
        void increment() {
            index_ = set_.find_next(index_);
        }
        bool equal(const iterator &rhs) const {
            return index_ == rhs.index_;
        }
        PeerState &dereference() const {
            return *indexmap_.At(index_);
        }
        const PeerStateMap &indexmap_;
        const BitSet &set_;
        size_t index_;
    };

    explicit RibState(RibOut *ribout)
        : key_(ribout), index_(-1), in_sync_(RibOutUpdates::QCOUNT, true) {
    }

    void Add(PeerState *ps);
    void Remove(PeerState *ps);

    bool QueueSync(int queue_id);
    void SetQueueSync(int queue_id);
    void SetQueueUnsync(int queue_id);

    RibOut *ribout() { return key_; }

    iterator begin(const PeerStateMap &indexmap) {
        return iterator(indexmap, peer_set_, peer_set_.find_first());
    }

    iterator end(const PeerStateMap &indexmap) {
        return iterator(indexmap, peer_set_, BitSet::npos);
    }

    void set_index(size_t index) { index_ = index; }
    size_t index() const { return index_; }

    bool empty() const { return peer_set_.none(); }

    const BitSet &peer_set() const { return peer_set_; }

private:
    RibOut *key_;
    size_t index_;
    BitSet peer_set_;
    vector<bool> in_sync_;

    DISALLOW_COPY_AND_ASSIGN(RibState);
};


void BgpSenderPartition::RibState::Add(BgpSenderPartition::PeerState *ps) {
    CHECK_CONCURRENCY("bgp::PeerMembership");
    peer_set_.set(ps->index());
}

void BgpSenderPartition::RibState::Remove(BgpSenderPartition::PeerState *ps) {
    CHECK_CONCURRENCY("bgp::PeerMembership");
    peer_set_.reset(ps->index());
}

bool BgpSenderPartition::RibState::QueueSync(int queue_id) {
    CHECK_CONCURRENCY("bgp::SendUpdate");
    return (in_sync_[queue_id]);
}

void BgpSenderPartition::RibState::SetQueueSync(int queue_id) {
    CHECK_CONCURRENCY("bgp::SendUpdate");
    in_sync_[queue_id] = true;
}

void BgpSenderPartition::RibState::SetQueueUnsync(int queue_id) {
    CHECK_CONCURRENCY("bgp::SendUpdate");
    in_sync_[queue_id] = false;
}

void BgpSenderPartition::PeerState::Add(RibState *rs) {
    CHECK_CONCURRENCY("bgp::PeerMembership");
    PeerRibState init;
    rib_set_.insert(make_pair(rs->index(), init));
    rib_bitset_.set(rs->index());
}

void BgpSenderPartition::PeerState::Remove(RibState *rs) {
    CHECK_CONCURRENCY("bgp::PeerMembership");
    for (int queue_id = 0; queue_id < RibOutUpdates::QCOUNT; queue_id++) {
        SetQueueInactive(rs->index(), queue_id);
    }
    rib_set_.erase(rs->index());
    rib_bitset_.reset(rs->index());
}

void BgpSenderPartition::PeerState::SetSync() {
    CHECK_CONCURRENCY("bgp::SendUpdate");
    for (Map::iterator it = rib_set_.begin(); it != rib_set_.end(); ++it) {
        assert(it->second.qactive == 0);
    }
    for (int i = 0; i < RibOutUpdates::QCOUNT; i++) {
        assert(qactive_cnt_[i] == 0);
    }
    in_sync_ = true;
}

RibOut &BgpSenderPartition::PeerState::iterator::dereference() const {
    return *indexmap_.At(index_)->ribout();
}

RibOut &BgpSenderPartition::PeerState::circular_iterator::dereference() const {
    return *indexmap_.At(index_)->ribout();
}

class BgpSenderPartition::Worker : public Task {
public:
    explicit Worker(BgpSenderPartition *partition)
        : Task(partition->task_id(), partition->index()),
          partition_(partition) {
    }

    virtual bool Run() {
        CHECK_CONCURRENCY("bgp::SendUpdate");

        while (true) {
            auto_ptr<WorkBase> wentry = partition_->WorkDequeue();
            if (!wentry.get())
                break;
            if (!wentry->valid)
                continue;
            switch (wentry->type) {
            case WorkBase::WRibOut: {
                WorkRibOut *workrib = static_cast<WorkRibOut *>(wentry.get());
                partition_->UpdateRibOut(workrib->ribout, workrib->queue_id);
                break;
            }
            case WorkBase::WPeer: {
                WorkPeer *workpeer = static_cast<WorkPeer *>(wentry.get());
                partition_->UpdatePeer(workpeer->peer);
                break;
            }
            }
        }

        return true;
    }
    string Description() const { return "BgpSenderPartition::Worker"; }

private:
    BgpSenderPartition *partition_;
};

BgpSenderPartition::BgpSenderPartition(BgpUpdateSender *sender, int index)
    : sender_(sender),
      index_(index),
      running_(false),
      disabled_(false),
      worker_task_(NULL) {
}

BgpSenderPartition::~BgpSenderPartition() {
    if (worker_task_) {
        TaskScheduler *scheduler = TaskScheduler::GetInstance();
        scheduler->Cancel(worker_task_);
    }
    assert(peer_state_imap_.empty());
    assert(rib_state_imap_.empty());
}

int BgpSenderPartition::task_id() const {
    return sender_->task_id();
}

//
// Add the (RibOut, IPeerUpdate) combo to the BgpSenderPartition.
// Find or create the corresponding RibState and PeerState and sets up the
// cross-linkage.
//
void BgpSenderPartition::Add(RibOut *ribout, IPeerUpdate *peer) {
    CHECK_CONCURRENCY("bgp::PeerMembership");

    RibState *rs = rib_state_imap_.Locate(ribout);
    PeerState *ps = peer_state_imap_.Locate(peer);
    rs->Add(ps);
    ps->Add(rs);
}

//
// Remove the (RibOut, IPeerUpdate) combo from the BgpSenderPartition.
// Decouple cross linkage between the corresponding RibState and PeerState and
// get rid of the RibState and/or PeerState if they are no longer needed.
//
void BgpSenderPartition::Remove(RibOut *ribout, IPeerUpdate *peer) {
    CHECK_CONCURRENCY("bgp::PeerMembership");

    RibState *rs = rib_state_imap_.Find(ribout);
    PeerState *ps = peer_state_imap_.Find(peer);
    assert(rs != NULL);
    assert(ps != NULL);
    rs->Remove(ps);
    ps->Remove(rs);
    if (rs->empty()) {
        WorkRibOutInvalidate(ribout);
        rib_state_imap_.Remove(ribout, rs->index());
    }
    if (ps->empty())  {
        WorkPeerInvalidate(peer);
        peer_state_imap_.Remove(peer, ps->index());
    }
}

//
// Create and enqueue new WorkRibOut entry since the RibOut is now
// active.
//
void BgpSenderPartition::RibOutActive(RibOut *ribout, int queue_id) {
    CHECK_CONCURRENCY("db::DBTable", "bgp::SendUpdate", "bgp::PeerMembership");

    WorkRibOutEnqueue(ribout, queue_id);
}

//
// Mark an IPeerUpdate to be send ready.
//
void BgpSenderPartition::PeerSendReady(IPeerUpdate *peer) {
    CHECK_CONCURRENCY("bgp::SendReadyTask");

    // The IPeerUpdate may not be registered if it has not reached Established
    // state or it may already have been unregistered by the time we get around
    // to processing the notification.
    PeerState *ps = peer_state_imap_.Find(peer);
    if (!ps)
        return;

    // Nothing to do if the IPeerUpdate's already in that state.
    if (ps->send_ready())
        return;

    // Create and enqueue new WorkPeer entry.
    ps->set_send_ready(true);
    WorkPeerEnqueue(peer);
}

//
// Return true if the IPeer is send ready.
//
bool BgpSenderPartition::PeerIsSendReady(IPeerUpdate *peer) const {
    CHECK_CONCURRENCY("bgp::PeerMembership", "bgp::ShowCommand");

    PeerState *ps = peer_state_imap_.Find(peer);
    return ps->send_ready();
}

//
// Return true if the IPeer is registered.
//
bool BgpSenderPartition::PeerIsRegistered(IPeerUpdate *peer) const {
    CHECK_CONCURRENCY("bgp::PeerMembership", "bgp::ShowCommand");

    return (peer_state_imap_.Find(peer) != NULL);
}

//
// Return true if the IPeer is in sync.
//
bool BgpSenderPartition::PeerInSync(IPeerUpdate *peer) const {
    CHECK_CONCURRENCY("bgp::PeerMembership", "bgp::ShowCommand");

    PeerState *ps = peer_state_imap_.Find(peer);
    return (ps ? ps->in_sync() : false);
}

//
// Create a Worker if warranted and enqueue it to the TaskScheduler.
// Assumes that the caller holds the BgpSenderPartition mutex.
//
void BgpSenderPartition::MaybeStartWorker() {
    if (!running_ && !disabled_) {
        worker_task_ = new Worker(this);
        TaskScheduler *scheduler = TaskScheduler::GetInstance();
        scheduler->Enqueue(worker_task_);
        running_ = true;
    }
}

//
// Dequeue the first WorkBase item from the work queue and return an
// auto_ptr to it.  Clear out Worker related state if the work queue
// is empty.
//
auto_ptr<BgpSenderPartition::WorkBase> BgpSenderPartition::WorkDequeue() {
    CHECK_CONCURRENCY("bgp::SendUpdate");

    auto_ptr<WorkBase> wentry;
    if (work_queue_.empty()) {
        worker_task_ = NULL;
        running_ = false;
    } else {
        wentry.reset(work_queue_.pop_front().release());
    }
    return wentry;
}

//
// Enqueue a WorkBase entry into the the work queue and start a new Worker
// task if required.
//
void BgpSenderPartition::WorkEnqueue(WorkBase *wentry) {
    CHECK_CONCURRENCY("db::DBTable", "bgp::SendUpdate", "bgp::SendReadyTask",
        "bgp::PeerMembership");

    work_queue_.push_back(wentry);
    MaybeStartWorker();
}

//
// Disable or enable the worker.
// For unit testing.
//
void BgpSenderPartition::set_disabled(bool disabled) {
    disabled_ = disabled;
    MaybeStartWorker();
}

//
// Enqueue a WorkPeer to the work queue.
//
void BgpSenderPartition::WorkPeerEnqueue(IPeerUpdate *peer) {
    CHECK_CONCURRENCY("bgp::SendReadyTask");

    WorkBase *wentry = new WorkPeer(peer);
    WorkEnqueue(wentry);
}

//
// Invalidate all WorkBases for the given IPeerUpdate.
// Used when a IPeerUpdate is removed.
//
void BgpSenderPartition::WorkPeerInvalidate(IPeerUpdate *peer) {
    CHECK_CONCURRENCY("bgp::PeerMembership");

    for (WorkQueue::iterator it = work_queue_.begin();
         it != work_queue_.end(); ++it) {
        WorkBase *wentry = it.operator->();
        if (wentry->type != WorkBase::WPeer)
            continue;
        WorkPeer *wpeer = static_cast<WorkPeer *>(wentry);
        if (wpeer->peer != peer)
            continue;
        wpeer->valid = false;
    }
}

//
// Enqueue a WorkRibOut to the work queue.
//
void BgpSenderPartition::WorkRibOutEnqueue(RibOut *ribout, int queue_id) {
    CHECK_CONCURRENCY("db::DBTable", "bgp::SendUpdate", "bgp::PeerMembership");

    WorkBase *wentry = new WorkRibOut(ribout, queue_id);
    WorkEnqueue(wentry);
}

//
// Invalidate all WorkBases for the given RibOut.
// Used when a RibOut is removed.
//
void BgpSenderPartition::WorkRibOutInvalidate(RibOut *ribout) {
    CHECK_CONCURRENCY("bgp::PeerMembership");

    for (WorkQueue::iterator it = work_queue_.begin();
         it != work_queue_.end(); ++it) {
        WorkBase *wentry = it.operator->();
        if (wentry->type != WorkBase::WRibOut)
            continue;
        WorkRibOut *wribout = static_cast<WorkRibOut *>(wentry);
        if (wribout->ribout != ribout)
            continue;
        wribout->valid = false;
    }
}

//
// Build the RibPeerSet of IPeers for the RibOut that are in sync. Note that
// we need to use bit indices that are specific to the RibOut, not the ones
// from the BgpSenderPartition.
//
void BgpSenderPartition::BuildSyncBitSet(const RibOut *ribout, RibState *rs,
    RibPeerSet *msync) {
    CHECK_CONCURRENCY("bgp::SendUpdate");

    for (RibState::iterator it = rs->begin(peer_state_imap_);
         it != rs->end(peer_state_imap_); ++it) {
        PeerState *ps = it.operator->();

        // If the PeerState is in sync but the IPeerUpdate is not send ready
        // then update the sync and send ready state in the PeerState.  Note
        // that the RibOut queue for the PeerState will get marked active via
        // the call the SetQueueActive from UpdateRibOut.
        if (ps->in_sync()) {
            if (ps->peer()->send_ready()) {
                int rix = ribout->GetPeerIndex(ps->peer());
                msync->set(rix);
            } else {
                ps->clear_sync();
                ps->set_send_ready(false);
            }
        }
    }
}

//
// Take the RibPeerSet of blocked IPeers and update the relevant PeerStates.
// Note that bit indices in the RibPeerSet and are specific to the RibOut.
//
void BgpSenderPartition::SetSendBlocked(const RibOut *ribout, RibState *rs,
    int queue_id, const RibPeerSet &blocked) {
    CHECK_CONCURRENCY("bgp::SendUpdate");

    for (size_t bit = blocked.find_first(); bit != RibPeerSet::npos;
         bit = blocked.find_next(bit)) {
        IPeerUpdate *peer = ribout->GetPeer(bit);
        PeerState *ps = peer_state_imap_.Find(peer);
        ps->SetQueueActive(rs->index(), queue_id);
        ps->clear_sync();
        ps->set_send_ready(false);
    }
}

//
// For unit testing only.
// Take the RibPeerSet of blocked IPeers and update the relevant PeerStates.
//
void BgpSenderPartition::SetSendBlocked(RibOut *ribout,
    int queue_id, const RibPeerSet &blocked) {
    CHECK_CONCURRENCY("bgp::SendUpdate");

    RibState *rs = rib_state_imap_.Find(ribout);
    assert(rs);
    SetSendBlocked(ribout, rs, queue_id, blocked);
}

//
// Concurrency: called from bgp send task.
//
// Take the RibPeerSet of unsync IPeers and update the relevant PeerStates.
// Note that bit indices in the RibPeerSet and are specific to the RibOut.
//
void BgpSenderPartition::SetQueueActive(const RibOut *ribout, RibState *rs,
    int queue_id, const RibPeerSet &munsync) {
    CHECK_CONCURRENCY("bgp::SendUpdate");

    for (size_t bit = munsync.find_first(); bit != RibPeerSet::npos;
         bit = munsync.find_next(bit)) {
        IPeerUpdate *peer = ribout->GetPeer(bit);
        PeerState *ps = peer_state_imap_.Find(peer);
        ps->SetQueueActive(rs->index(), queue_id);
    }
}

//
// Concurrency: called from bgp send task.
//
// Mark the PeerRibState corresponding to the given IPeerUpdate and RibOut
// as active.
//
// Used by unit test code.
//
void BgpSenderPartition::SetQueueActive(RibOut *ribout, int queue_id,
    IPeerUpdate *peer) {
    CHECK_CONCURRENCY("bgp::SendUpdate");

    PeerState *ps = peer_state_imap_.Find(peer);
    RibState *rs = rib_state_imap_.Find(ribout);
    ps->SetQueueActive(rs->index(), queue_id);
}

//
// Concurrency: called from bgp send task.
//
// Check if the queue corresponding to IPeerUpdate, Ribout and queue id is
// active.
//
// Used by unit test code.
//
bool BgpSenderPartition::IsQueueActive(RibOut *ribout, int queue_id,
    IPeerUpdate *peer) {
    CHECK_CONCURRENCY("bgp::SendUpdate");

    PeerState *ps = peer_state_imap_.Find(peer);
    RibState *rs = rib_state_imap_.Find(ribout);
    return ps->IsQueueActive(rs->index(), queue_id);
}

//
// Concurrency: called from bgp send task.
//
// Mark all the RibStates for the given peer and queue id as being in sync
// and trigger a tail dequeue.
//
void BgpSenderPartition::SetQueueSync(PeerState *ps, int queue_id) {
    CHECK_CONCURRENCY("bgp::SendUpdate");

    for (PeerState::iterator it = ps->begin(rib_state_imap_);
         it != ps->end(rib_state_imap_); ++it) {
         RibState *rs = it.rib_state();
         if (!rs->QueueSync(queue_id)) {
             RibOut *ribout = it.operator->();
             RibOutActive(ribout, queue_id);
             rs->SetQueueSync(queue_id);
         }
    }
}

//
// Drain the queue until there are no more updates or all the members become
// blocked.
//
void BgpSenderPartition::UpdateRibOut(RibOut *ribout, int queue_id) {
    CHECK_CONCURRENCY("bgp::SendUpdate");

    RibOutUpdates *updates = ribout->updates(index_);
    RibState *rs = rib_state_imap_.Find(ribout);
    RibPeerSet msync;

    // Convert group in-sync list to rib specific bitset.
    BuildSyncBitSet(ribout, rs, &msync);

    // Drain the queue till we can do no more.
    RibPeerSet blocked, munsync;
    bool done = updates->TailDequeue(queue_id, msync, &blocked, &munsync);
    assert(msync.Contains(blocked));

    // Mark peers as send blocked.
    SetSendBlocked(ribout, rs, queue_id, blocked);

    // Set the queue to be active for any unsync peers. If we don't do this,
    // we will forget to mark the (RibOut,QueueId) as active for these peers
    // since the blocked RibPeerSet does not contain peers that are already
    // out of sync.  Note that the unsync peers would have been split from
    // the tail marker in TailDequeue.
    SetQueueActive(ribout, rs, queue_id, munsync);

    // If all peers are blocked, mark the queue as unsync in the RibState. We
    // will trigger tail dequeue for the (RibOut,QueueId) when any peer that
    // is interested in the RibOut becomes in sync.
    if (!done)
        rs->SetQueueUnsync(queue_id);
}

//
// Go through all RibOuts for the IPeerUpdate and drain the given queue till it
// is up-to date or it becomes blocked. If it's blocked, select the next RibOut
// to be processed when the IPeerUpdate becomes send ready.
//
// Return false if the IPeerUpdate got blocked.
//
bool BgpSenderPartition::UpdatePeerQueue(IPeerUpdate *peer, PeerState *ps,
    int queue_id) {
    CHECK_CONCURRENCY("bgp::SendUpdate");

    for (PeerState::circular_iterator it = ps->circular_begin(rib_state_imap_);
         it != ps->circular_end(rib_state_imap_); ++it) {
        // Skip if this queue is not active in the PeerRibState.
        if (!BitIsSet(it.peer_rib_state().qactive, queue_id))
            continue;

        // Drain the queue till we can do no more.
        RibOut *ribout = it.operator->();
        RibOutUpdates *updates = ribout->updates(index_);
        RibPeerSet blocked;
        bool done = updates->PeerDequeue(queue_id, peer, &blocked);

        // Process blocked mask.
        RibState *rs = it.rib_state();
        SetSendBlocked(ribout, rs, queue_id, blocked);

        // If the peer is still send_ready, mark the queue as inactive for
        // the peer.  Need to check send_ready because the return value of
        // PeerDequeue only tells that *some* peer was merged with the tail
        // marker.
        // If the peer got blocked, remember where to start next time and
        // stop processing. We don't want to continue processing for other
        // merged peers if the lead peer is blocked.  Processing for other
        // peers will continue when their own WorkPeer items are processed.
        if (ps->send_ready()) {
            assert(done);
            ps->SetQueueInactive(rs->index(), queue_id);
        } else {
            ps->SetIteratorStart(it.index());
            return false;
        }
    }

    return true;
}

//
// Drain the queue of all updates for this IPeerUpdate, until it is up-to date
// or it becomes blocked.
//
void BgpSenderPartition::UpdatePeer(IPeerUpdate *peer) {
    CHECK_CONCURRENCY("bgp::SendUpdate");

    // Bail if the PeerState is not send ready.
    PeerState *ps = peer_state_imap_.Find(peer);
    if (!ps->send_ready()) {
        return;
    }

    // Update the PeerState and bail if the IPeerUpdate is not send ready.
    // This happens if the IPeerUpdate gets blocked while processing some
    // other partition.
    if (!peer->send_ready()) {
        ps->set_send_ready(false);
        return;
    }

    // Go through all queues and drain them if there's anything on them.
    for (int queue_id = RibOutUpdates::QCOUNT - 1; queue_id >= 0; --queue_id) {
        if (ps->QueueCount(queue_id) == 0) {
            continue;
        }
        if (!UpdatePeerQueue(peer, ps, queue_id)) {
            assert(!ps->send_ready());
            return;
        }
    }

    // Checking the return value of UpdatePeerQueue above is not sufficient as
    // that only tells us that *some* peer(s) got merged with the tail marker.
    // Need to make sure that the IPeerUpdate that we are processing is still
    // send ready.
    if (!ps->send_ready()) {
        return;
    }

    // Mark the peer as being in sync across all tables.
    ps->SetSync();

    // Mark all RibStates for the peer as being in sync. This triggers a tail
    // dequeue for the corresponding (RibOut, QueueId) if necessary. This in
    // turn ensures that we do not get stuck in the case where all peers get
    // blocked and then get back in sync.
    for (int queue_id = RibOutUpdates::QCOUNT - 1; queue_id >= 0; --queue_id) {
        SetQueueSync(ps, queue_id);
    }
}

//
// Check invariants for the BgpSenderPartition.
//
bool BgpSenderPartition::CheckInvariants() const {
    int grp_peer_count = 0;
    int peer_grp_count = 0;
    for (size_t i = 0; i < rib_state_imap_.size(); i++) {
        if (!rib_state_imap_.bits().test(i)) {
            continue;
        }
        RibState *rs = rib_state_imap_.At(i);
        assert(rs != NULL);
        CHECK_INVARIANT(rs->index() == i);
        for (RibState::iterator it = rs->begin(peer_state_imap_);
             it != rs->end(peer_state_imap_); ++it) {
            PeerState *ps = it.operator->();
            CHECK_INVARIANT(ps->IsMember(i));
            grp_peer_count++;
        }
    }
    for (size_t i = 0; i < peer_state_imap_.size(); i++) {
        if (!peer_state_imap_.bits().test(i)) {
            continue;
        }
        PeerState *ps = peer_state_imap_.At(i);
        assert(ps != NULL);
        CHECK_INVARIANT(ps->index() == i);
        if (!ps->CheckInvariants()) {
            return false;
        }
        for (PeerState::iterator it = ps->begin(rib_state_imap_);
             it != ps->end(rib_state_imap_); ++it) {
            RibState *rs = it.rib_state();
            CHECK_INVARIANT(rs->peer_set().test(i));
            peer_grp_count++;
        }
    }

    CHECK_INVARIANT(grp_peer_count == peer_grp_count);
    return true;
}

//
// Constructor for BgpUpdateSender.
// Initialize send ready WorkQueue and allocate BgpSenderPartitions.
//
BgpUpdateSender::BgpUpdateSender(BgpServer *server)
    : server_(server),
      task_id_(TaskScheduler::GetInstance()->GetTaskId("bgp::SendUpdate")),
      send_ready_queue_(
          TaskScheduler::GetInstance()->GetTaskId("bgp::SendReadyTask"), 0,
          boost::bind(&BgpUpdateSender::SendReadyCallback, this, _1)) {
    for (int idx = 0; idx < DB::PartitionCount(); ++idx) {
        partitions_.push_back(new BgpSenderPartition(this, idx));
    }
}

//
// Destructor for BgpUpdateSender.
// Shutdown the WorkQueue and delete all BgpSenderPartitions.
//
BgpUpdateSender::~BgpUpdateSender() {
    send_ready_queue_.Shutdown(false);
    STLDeleteValues(&partitions_);
}

//
// Handle the join of an IPeerUpdate to a RibOut.
//
void BgpUpdateSender::Join(RibOut *ribout, IPeerUpdate *peer) {
    CHECK_CONCURRENCY("bgp::PeerMembership");

    BOOST_FOREACH(BgpSenderPartition *partition, partitions_) {
        partition->Add(ribout, peer);
    }
}

//
// Handle the leave of an IPeerUpdate from a RibOut.
//
void BgpUpdateSender::Leave(RibOut *ribout, IPeerUpdate *peer) {
    CHECK_CONCURRENCY("bgp::PeerMembership");

    BOOST_FOREACH(BgpSenderPartition *partition, partitions_) {
        partition->Remove(ribout, peer);
    }
}

//
// Inform the specified BgpSenderPartition that it needs to schedule a tail
// dequeue for the given RibOut queue.
//
void BgpUpdateSender::RibOutActive(int index, RibOut *ribout, int queue_id) {
    CHECK_CONCURRENCY("db::DBTable", "bgp::PeerMembership");

    partitions_[index]->RibOutActive(ribout, queue_id);
}

//
// Concurrency: called from arbitrary task.
//
// Enqueue the IPeerUpdate to the send ready processing work queue.
// The callback is invoked in the context of bgp::SendReadyTask.
//
void BgpUpdateSender::PeerSendReady(IPeerUpdate *peer) {
    send_ready_queue_.Enqueue(peer);
}

//
// Return true if the IPeer is registered.
//
bool BgpUpdateSender::PeerIsRegistered(IPeerUpdate *peer) const {
    BOOST_FOREACH(BgpSenderPartition *partition, partitions_) {
        if (partition->PeerIsRegistered(peer))
            return true;
    }
    return false;
}

//
// Return true if the IPeer is in sync.
//
bool BgpUpdateSender::PeerInSync(IPeerUpdate *peer) const {
    BOOST_FOREACH(BgpSenderPartition *partition, partitions_) {
        if (!partition->PeerInSync(peer))
            return false;
    }
    return true;
}

//
// Callback to handle send ready notification for IPeerUpdate.  Processing it
// in the context of bgp::SendeReadyTask ensures that there are no concurrency
// issues w.r.t. the BgpSenderPartition working on the IPeerUpdate while we are
// processing the notification.
//
bool BgpUpdateSender::SendReadyCallback(IPeerUpdate *peer) {
    CHECK_CONCURRENCY("bgp::SendReadyTask");

    BOOST_FOREACH(BgpSenderPartition *partition, partitions_) {
        partition->PeerSendReady(peer);
    }
    return true;
}

//
// Check invariants for the BgpUpdateSender.
//
bool BgpUpdateSender::CheckInvariants() const {
    BOOST_FOREACH(BgpSenderPartition *partition, partitions_) {
        if (!partition->CheckInvariants())
            return false;
    }
    return true;
}

//
// Disable all BgpSenderPartitions.
//
void BgpUpdateSender::DisableProcessing() {
    BOOST_FOREACH(BgpSenderPartition *partition, partitions_) {
        partition->set_disabled(true);
    }
}

//
// Enable all BgpSenderPartitions.
//
void BgpUpdateSender::EnableProcessing() {
    BOOST_FOREACH(BgpSenderPartition *partition, partitions_) {
        partition->set_disabled(false);
    }
}
