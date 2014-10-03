/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/scheduling_group.h"

#include <boost/bind.hpp>
#include <boost/iterator/iterator_facade.hpp>

#include "base/logging.h"
#include "base/task.h"
#include "base/task_annotations.h"
#include "base/util.h"
#include "bgp/bgp_factory.h"
#include "bgp/bgp_log.h"
#include "bgp/bgp_ribout.h"
#include "bgp/bgp_ribout_updates.h"
#include "bgp/bgp_update.h"

using namespace std;

int SchedulingGroup::send_task_id_ = -1;

//
// This struct represents RibOut specific state for a PeerState.  There's one
// instance of this for each RibOut that an IPeerUpdate has joined.
//
// The PeerRibState contains a bit mask to keep track of the QueueIds that are
// currently active for the RibOut for the IPeerUpdate.
//
struct SchedulingGroup::PeerRibState {
    PeerRibState() : qactive(0) { }
    uint8_t qactive;
};

//
// This nested class represents IPeerUpdate related state that's specific to
// the SchedulingGroup.
//
// A PeerState contains a Map of the index for a RibState to a PeerRibState.
// Each entry in the map logically represents the state of the peer for the
// ribout.
//
// The Map is used in conjunction with the RibStateMap in SchedulingGroup to
// implement regular and circular iterator nested classes. These provide the
// functionality to walk through all the RibState entries for the peer.
//
// A PeerState maintains the in_sync and send_ready state for the IPeer. An
// IPeer/PeerState is considered to be send_ready when the underlying socket
// is writable. It is considered to be in_sync if it's send_ready and the
// marker for the IPeer has merged with the tail marker for all QueueIds in
// all RiBOuts that the IPeer is subscribed.
//
// The PeerState keeps count of the number of active RibOuts for each QueueId.
// A (RibOut, QueueId) pair is considered to be active if the PeerState isn't
// send_ready and there's RouteUpdates for the pair.
//
class SchedulingGroup::PeerState {
public:
    typedef map<int, PeerRibState> Map;

    class iterator : public boost::iterator_facade<
        iterator, RibOut, boost::forward_traversal_tag> {
    public:
        explicit iterator(const RibStateMap &indexmap, Map *map, int index)
            : indexmap_(indexmap), map_(map), index_(index) {
        }
        int index() const { return index_; }
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
        int index_;
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

    bool IsMember(int index) const {
        return rib_set_.count(index) > 0;
    }

    iterator begin(const RibStateMap &indexmap) {
        Map::const_iterator iter = rib_set_.begin();
        int index = (iter != rib_set_.end() ? iter->first : -1);
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

    void SetIteratorStart(int start) { rib_iterator_ = start; }

    void SetQueueActive(int rib_index, int queue_id) {
        CHECK_CONCURRENCY("bgp::SendTask");
        Map::iterator loc = rib_set_.find(rib_index);
        assert(loc != rib_set_.end());
        if (!BitIsSet(loc->second.qactive, queue_id)) {
            SetBit(loc->second.qactive, queue_id);
            qactive_cnt_[queue_id]++;
        }
    }

    void SetQueueInactive(int rib_index, int queue_id) {
        CHECK_CONCURRENCY("bgp::SendTask", "bgp::PeerMembership");
        Map::iterator loc = rib_set_.find(rib_index);
        assert(loc != rib_set_.end());
        if (BitIsSet(loc->second.qactive, queue_id)) {
            ClearBit(loc->second.qactive, queue_id);
            qactive_cnt_[queue_id]--;
        }
    }

    bool IsQueueActive(int rib_index, int queue_id) {
        CHECK_CONCURRENCY("bgp::SendTask");
        Map::iterator loc = rib_set_.find(rib_index);
        assert(loc != rib_set_.end());
        return BitIsSet(loc->second.qactive, queue_id);
    }

    int QueueCount(int queue_id) { return qactive_cnt_[queue_id]; }

    void RibStateMove(PeerState *rhs, int index, int rhs_index) {
        CHECK_CONCURRENCY("bgp::PeerMembership");
        Map::iterator loc = rhs->rib_set_.find(rhs_index);
        assert(loc != rhs->rib_set_.end());
        pair<Map::iterator, bool> result = 
            rib_set_.insert(make_pair(index, loc->second));
        assert(result.second);
        for (int i = 0; i < RibOutUpdates::QCOUNT; i++) {
            if (BitIsSet(loc->second.qactive, i)) {
                qactive_cnt_[i] += 1;
                rhs->qactive_cnt_[i] -= 1;
            }
        }
    }

    void PeerStateMove(PeerState *rhs) {
        CHECK_CONCURRENCY("bgp::PeerMembership");
        swap(in_sync_, rhs->in_sync_);
        swap(send_ready_, rhs->send_ready_);
    }

    IPeerUpdate *peer() const { return key_; }

    void set_index(int index) { index_ = index; }
    int index() const { return index_; }

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
    int index_;             // assigned from PeerStateMap in the group
    Map rib_set_;           // list of RibOuts advertised by the peer.
    vector<int> qactive_cnt_;
    bool in_sync_;          // whether the peer may dequeue tail markers.
    tbb::atomic<bool> send_ready_;    // whether the peer may send updates.
    size_t rib_iterator_;   // index of last processed rib.

    DISALLOW_COPY_AND_ASSIGN(PeerState);
};

//
// This nested class represents RibOut related state that's specific to the
// SchedulingGroup.
//
// A RibState contains a GroupPeerSet which is a bitset of all the peers
// that are advertising the RibOut associated with the RibState.
//
// The GroupPeerSet is used in conjunction with the PeerStateMap in the
// SchedulingGroup to implement an iterator nested class. This provides
// the functionality to walk through all the PeerState entries for the
// ribout.
//
class SchedulingGroup::RibState {
public:
    class iterator : public boost::iterator_facade<
        iterator, PeerState, boost::forward_traversal_tag> {
    public:
        explicit iterator(const PeerStateMap &indexmap,
                          const GroupPeerSet &set, size_t index)
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
        const GroupPeerSet &set_;
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

    void RibStateMove(RibState *rhs) {
        swap(in_sync_, rhs->in_sync_);
    }

    RibOut *ribout() { return key_; }

    iterator begin(const PeerStateMap &indexmap) {
        return iterator(indexmap, peer_set_, peer_set_.find_first());
    }

    iterator end(const PeerStateMap &indexmap) {
        return iterator(indexmap, peer_set_, GroupPeerSet::npos);
    }

    void set_index(int index) { index_ = index; }
    int index() const { return index_; }

    bool empty() const { return peer_set_.none(); }

    const GroupPeerSet &peer_set() const { return peer_set_; }

private:
    RibOut *key_;
    int index_;
    GroupPeerSet peer_set_;
    vector<bool> in_sync_;

    DISALLOW_COPY_AND_ASSIGN(RibState);
};


void SchedulingGroup::RibState::Add(SchedulingGroup::PeerState *ps) {
    CHECK_CONCURRENCY("bgp::PeerMembership");
    peer_set_.set(ps->index());
}

void SchedulingGroup::RibState::Remove(SchedulingGroup::PeerState *ps) {
    CHECK_CONCURRENCY("bgp::PeerMembership");
    peer_set_.reset(ps->index());
}

bool SchedulingGroup::RibState::QueueSync(int queue_id) {
    CHECK_CONCURRENCY("bgp::SendTask");
    return (in_sync_[queue_id]);
}

void SchedulingGroup::RibState::SetQueueSync(int queue_id) {
    CHECK_CONCURRENCY("bgp::SendTask");
    in_sync_[queue_id] = true;
}

void SchedulingGroup::RibState::SetQueueUnsync(int queue_id) {
    CHECK_CONCURRENCY("bgp::SendTask");
    in_sync_[queue_id] = false;
}

void SchedulingGroup::PeerState::Add(RibState *rs) {
    CHECK_CONCURRENCY("bgp::PeerMembership");
    PeerRibState init;
    rib_set_.insert(make_pair(rs->index(), init));
}

void SchedulingGroup::PeerState::Remove(RibState *rs) {
    CHECK_CONCURRENCY("bgp::PeerMembership");
    for (int queue_id = 0; queue_id < RibOutUpdates::QCOUNT; queue_id++) {
        SetQueueInactive(rs->index(), queue_id);
    }
    rib_set_.erase(rs->index());
}

void SchedulingGroup::PeerState::SetSync() {
    CHECK_CONCURRENCY("bgp::SendTask");
    for (Map::iterator iter = rib_set_.begin(); iter != rib_set_.end();
            ++iter) {
        assert(iter->second.qactive == 0);
    }
    for (int i = 0; i < RibOutUpdates::QCOUNT; i++) {
        assert(qactive_cnt_[i] == 0);
    }
    in_sync_ = true;
}

RibOut &SchedulingGroup::PeerState::iterator::dereference() const {
    return *indexmap_.At(index_)->ribout();
}

RibOut &SchedulingGroup::PeerState::circular_iterator::dereference() const {
    return *indexmap_.At(index_)->ribout();
}

struct SchedulingGroup::WorkBase {
    enum Type {
        WPeer,
        WRibOut
    };
    WorkBase(Type type) : type(type) { }
    Type type;
};

struct SchedulingGroup::WorkRibOut : public SchedulingGroup::WorkBase {
    WorkRibOut(RibOut *ribout, int queue_id)
        : WorkBase(WRibOut), ribout(ribout), queue_id(queue_id) {
    }
    RibOut *ribout;
    int queue_id;
};

struct SchedulingGroup::WorkPeer : public SchedulingGroup::WorkBase {
    explicit WorkPeer(IPeerUpdate *peer) : WorkBase(WPeer), peer(peer) { }
    IPeerUpdate *peer;
};

class SchedulingGroup::Worker : public Task {
public:
    Worker(SchedulingGroup *group)
        : Task(send_task_id_), group_(group) {
    }

    virtual bool Run() {
        CHECK_CONCURRENCY("bgp::SendTask");

        while (true) {
            auto_ptr<WorkBase> wentry = group_->WorkDequeue();
            if (wentry.get() == NULL) {
                break;
            }
            switch (wentry->type) {
            case WorkBase::WRibOut: {
                WorkRibOut *workrib = static_cast<WorkRibOut *>(wentry.get());
                group_->UpdateRibOut(workrib->ribout, workrib->queue_id);
                break;
            }
            case WorkBase::WPeer: {
                WorkPeer *workpeer = static_cast<WorkPeer *>(wentry.get());
                group_->UpdatePeer(workpeer->peer);
                break;
            }
            }
        }

        return true;
    }

private:
    SchedulingGroup *group_;
};

SchedulingGroup::SchedulingGroup() : running_(false), worker_task_(NULL) {
    if (send_task_id_ == -1) {
        TaskScheduler *scheduler = TaskScheduler::GetInstance();
        send_task_id_ = scheduler->GetTaskId("bgp::SendTask");
    }
}

SchedulingGroup::~SchedulingGroup() {
    if (worker_task_) {
        TaskScheduler *scheduler = TaskScheduler::GetInstance();
        scheduler->Cancel(worker_task_);
    }
}

void SchedulingGroup::clear() {
    peer_state_imap_.clear();
    rib_state_imap_.clear();
}

//
// Add the (RibOut, IPeerUpdate) combo to the SchedulingGroup. Finds or creates
// the corresponding RibState and PeerState and sets up the cross-linkage.
//
void SchedulingGroup::Add(RibOut *ribout, IPeerUpdate *peer) {
    CHECK_CONCURRENCY("bgp::PeerMembership");

    RibState *rs = rib_state_imap_.Locate(ribout);
    PeerState *ps = peer_state_imap_.Locate(peer);
    rs->Add(ps);
    ps->Add(rs);
}

//
// Remove the (RibOut, IPeerUpdate) combo from the SchedulingGroup.  Decouples
// cross linkage between the corresponding RibState and PeerState and gets rid
// of the RibState and PeerState if they are no longer needed.
//
void SchedulingGroup::Remove(RibOut *ribout, IPeerUpdate *peer) {
    CHECK_CONCURRENCY("bgp::PeerMembership");

    RibState *rs = rib_state_imap_.Find(ribout);
    PeerState *ps = peer_state_imap_.Find(peer);
    assert(rs != NULL);
    assert(ps != NULL);
    rs->Remove(ps);
    ps->Remove(rs);
    if (rs->empty()) {
        rib_state_imap_.Remove(ribout, rs->index());
    }
    if (ps->empty())  {
        peer_state_imap_.Remove(peer, ps->index());
    }
}

//
// Return true if the SchedulingGroup is empty.
//
bool SchedulingGroup::empty() const {
    return rib_state_imap_.empty();
}

//
// Return true if the IPeer is in sync.
//
bool SchedulingGroup::PeerInSync(IPeerUpdate *peer) const {
    CHECK_CONCURRENCY("bgp::PeerMembership");

    PeerState *ps = peer_state_imap_.Find(peer);
    return (ps) ? ps->in_sync() : false;
}

//
// Build two RibOutLists. The first is the list of RibOuts advertised by the
// IPeerUpdate and the second is the list of RibOuts that are not advertised
// by the IPeerUpdate.
//
void SchedulingGroup::GetPeerRibList(
        IPeerUpdate *peer, RibOutList *rlist, RibOutList *rcomplement) const {
    CHECK_CONCURRENCY("bgp::PeerMembership");

    PeerState *ps = peer_state_imap_.Find(peer);

    // Go through all possible indices for the RibOuts.  Need to ignore the
    // ones for which there's no RibState since those RibOuts don't exist.
    for (size_t i = 0; i < rib_state_imap_.size(); i++) {
        RibState *rs = rib_state_imap_.At(i);
        if (rs == NULL) continue;
        if (ps != NULL && ps->IsMember(i)) {
            rlist->push_back(rs->ribout());
        } else {
            rcomplement->push_back(rs->ribout());
        }
    }
}

//
// Build the list of IPeers that are advertising the specified RibOut.
//
void SchedulingGroup::GetRibPeerList(RibOut *ribout, PeerList *plist) const {
    CHECK_CONCURRENCY("bgp::PeerMembership");

    RibState *rs = rib_state_imap_.Find(ribout);
    assert(rs != NULL);
    plist->clear();
    for (RibState::iterator iter = rs->begin(peer_state_imap_);
         iter != rs->end(peer_state_imap_); ++iter) {
        PeerState *ps = iter.operator->();
        plist->push_back(ps->peer());
    }
}

//
// Build the bitset of peers advertising at least one RibOut in the specified
// list.
//
void SchedulingGroup::GetSubsetPeers(const RibOutList &list, GroupPeerSet *pg) {
    CHECK_CONCURRENCY("bgp::PeerMembership");

    pg->clear();
    for (RibOutList::const_iterator iter = list.begin(); iter != list.end();
         ++iter) {
        RibState *rs = rib_state_imap_.Find(*iter);
        *pg |= rs->peer_set();
    }
}

//
// Get the index for an IPeerUpdate.
//
int SchedulingGroup::GetPeerIndex(IPeerUpdate *peer) const {
    CHECK_CONCURRENCY("bgp::PeerMembership");

    PeerState *ps = peer_state_imap_.Find(peer);
    if (ps == NULL) {
        return -1;
    }
    return ps->index();
}

//
// Get the index for a RibOut.
//
int SchedulingGroup::GetRibOutIndex(RibOut *ribout) const {
    CHECK_CONCURRENCY("bgp::PeerMembership");

    RibState *rs = rib_state_imap_.Find(ribout);
    if (rs == NULL) {
        return -1;
    }
    return rs->index();
}

//
// Merge the specified SchedulingGroup into this one by migrating all the
// (IPeerUpdate, RibOut) combos and all the WorkBase items in the work queue.
//
// Note that the corresponding RibState and PeerState objects themselves are
// not migrated - we create new ones in this SchedulingGroup as required. This
// is done since we may need to use different indexes in this SchedulingGroup.
//
void SchedulingGroup::Merge(SchedulingGroup *rhs) {
    CHECK_CONCURRENCY("bgp::PeerMembership");

    PeerList plist;

    // Build a list of of all the IPeers in the old SchedulingGroup and go
    // through each of them.
    rhs->GetPeerList(&plist);
    for (PeerList::iterator iter = plist.begin(); iter != plist.end(); ++iter) {

        // Create the PeerState in this SchedulingGroup if required and find
        // the PeerState in the old SchedulingGroup.
        PeerState *ps = peer_state_imap_.Locate(*iter);
        PeerState *prev_ps = rhs->peer_state_imap_.Find(*iter);

        // Now run through all the RibOuts in the old PeerState.
        for (PeerState::iterator ribiter = prev_ps->begin(rhs->rib_state_imap_);
             ribiter != prev_ps->end(rhs->rib_state_imap_); ++ribiter) {

            // Create the RibState in this SchedulingGroup if required and move
            // over state from the old RibState. Then setup linkage between the
            // RibState and the PeerState in this SchedulingGroup.
            //
            // Notice that we don't simply invoke Add to add the new RibState
            // for the new PeerState.  Need to make sure that we do not lose
            // the PeerRibState information from the old PeerState.
            RibOut *ribout = ribiter.operator->();
            RibState *rs = rib_state_imap_.Find(ribout);
            RibState *prev_rs = rhs->rib_state_imap_.Find(ribout);
            if (rs == NULL) {
                rs = rib_state_imap_.Locate(ribout);
                rs->RibStateMove(prev_rs);
            }
            rs->Add(ps);
            ps->RibStateMove(prev_ps, rs->index(), ribiter.index());
        }

        // Move the rest of the logical PeerState info from the old to new.
        ps->PeerStateMove(prev_ps);
    }

    // Finally transfer the work queue from the old SchedulingGroup to this
    // one and clear the old SchedulingGroup. It's the caller responsibility
    // to delete the old SchedulingGroup.
    work_queue_.transfer(work_queue_.end(), rhs->work_queue_);
    rhs->clear();
}

//
// Split all RibOuts in the list specified as rg2 from this SchedulingGroup
// into the SchedulingGroup specified as rhs. Naturally, all the IPeers that
// advertise any of these RibOuts also moved to the new SchedulingGroup.
//
// Note that the corresponding RibState and PeerState objects themselves are
// not migrated - we create new ones in the new SchedulingGroup as required.
// This has to be done since we may need to use different indexes in the new
// SchedulingGroup.
//
// All WorkPeer and WorkRibOut items in this SchedulingGroup for the IPeers
// and RibOuts being moved are also moved over to the new SchedulingGroup.
//
void SchedulingGroup::Split(SchedulingGroup *rhs, const RibOutList &rg1,
        const RibOutList &rg2) {
    CHECK_CONCURRENCY("bgp::PeerMembership");

    // Walk through all the RibOuts in the list.
    for (RibOutList::const_iterator iter = rg2.begin(); iter != rg2.end();
         ++iter) {

        // Create new RibState and move over state from the old RibState.
        RibOut *ribout = *iter;
        RibState *rs = rhs->rib_state_imap_.Locate(ribout);
        RibState *prev_rs = rib_state_imap_.Find(ribout);
        rs->RibStateMove(prev_rs);

        // Build the list of IPeers for the RibOut and walk through them.
        PeerList plist;
        GetRibPeerList(ribout, &plist);
        for (PeerList::const_iterator peeriter = plist.begin();
             peeriter != plist.end(); ++peeriter) {

            // Create the new PeerState if required and move over the non
            // RiBOut related state from the old PeerState.
            PeerState *ps = rhs->peer_state_imap_.Find(*peeriter);
            PeerState *prev_ps = peer_state_imap_.Find(*peeriter);
            if (ps == NULL) {
                ps = rhs->peer_state_imap_.Locate(*peeriter);
                ps->PeerStateMove(prev_ps);
            }

            // Setup linkage between the new RibState and the new PeerState.
            //
            // Notice that we don't simply invoke Add to add the new RibState
            // for the new PeerState.  Need to make sure that we do not lose
            // the PeerRibState information from the old PeerState.
            rs->Add(ps);
            ps->RibStateMove(prev_ps, rs->index(), GetRibOutIndex(ribout));

            // Remove the (RibOut, IPeerUpdate) pair from this SchedulingGroup.
            Remove(ribout, *peeriter);
        }
    }

    // Go through all the WorkBase items on the work queue and move over the
    // ones associated with the RibOuts and IPeers being moved.  This block
    // of code relies on the new PeerState and RibState objects already having
    // been created above in the new SchedulingGroup.
    bool move = false;
    for (WorkQueue::iterator iter = work_queue_.begin();
         iter != work_queue_.end(); ) {
        WorkBase *wentry = iter.operator->();
        switch (wentry->type) {
        case WorkBase::WRibOut: {
            WorkRibOut *work = static_cast<WorkRibOut *>(wentry);
            move = (rhs->rib_state_imap_.Find(work->ribout) != NULL);
            break;
        }
        case WorkBase::WPeer: {
            WorkPeer *work = static_cast<WorkPeer *>(wentry);
            move = (rhs->peer_state_imap_.Find(work->peer) != NULL);
            break;
        }
        }
        WorkQueue::iterator loc = iter;
        ++iter;
        if (move) {
            rhs->work_queue_.transfer(rhs->work_queue_.end(), loc, work_queue_);
        }
    }
}

//
// Populate the RibOutList with all the RibOuts in this SchedulingGroup.
//
void SchedulingGroup::GetRibOutList(RibOutList *rlist) const {
    CHECK_CONCURRENCY("bgp::PeerMembership");

    rlist->clear();
    for (size_t i = 0; i < rib_state_imap_.size(); i++) {
        RibState *rs = rib_state_imap_.At(i);
        if (rs == NULL) continue;
        rlist->push_back(rs->ribout());
    }
}

//
// Populate the PeerList with all the IPeers in this SchedulingGroup.
//
void SchedulingGroup::GetPeerList(PeerList *plist) const {
    CHECK_CONCURRENCY("bgp::PeerMembership");

    plist->clear();
    for (size_t i = 0; i < peer_state_imap_.size(); i++) {
        PeerState *ps = peer_state_imap_.At(i);
        if (ps == NULL) continue;
        plist->push_back(ps->peer());
    }
}

//
// Concurrency: called from DB task or the bgp send task.
//
// Create and enqueue new WorkRibOut entry since the RibOut is now
// active.
//
void SchedulingGroup::RibOutActive(RibOut *ribout, int queue_id) {
    CHECK_CONCURRENCY("db::DBTable", "bgp::SendTask");

    WorkRibOutEnqueue(ribout, queue_id);
}

//
// Concurrency: called from the bgp send ready task.
//
// Mark an IPeerUpdate to be send ready.
//
void SchedulingGroup::SendReady(IPeerUpdate *peer) {
    CHECK_CONCURRENCY("bgp::SendReadyTask");

    // Nothing to do if the IPeerUpdate's already in that state.
    PeerState *ps = peer_state_imap_.Find(peer);
    if (ps->send_ready()) {
        return;
    }

    // Create and enqueue new WorkPeer entry.
    ps->set_send_ready(true);
    WorkPeerEnqueue(peer);
}

//
// Return true if the IPeerUpdate is send ready.
//
bool SchedulingGroup::IsSendReady(IPeerUpdate *peer) const {
    CHECK_CONCURRENCY("bgp::PeerMembership");

    PeerState *ps = peer_state_imap_.Find(peer);
    return ps->send_ready();
}

//
// Dequeue the first WorkBase item from the work queue and return an
// auto_ptr to it.  Clear out Worker related state if the work queue
// is empty.
//
auto_ptr<SchedulingGroup::WorkBase> SchedulingGroup::WorkDequeue() {
    CHECK_CONCURRENCY("bgp::SendTask");

    tbb::mutex::scoped_lock lock(mutex_);
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
void SchedulingGroup::WorkEnqueue(WorkBase *wentry) {
    CHECK_CONCURRENCY("db::DBTable", "bgp::SendTask", "bgp::SendReadyTask");

    tbb::mutex::scoped_lock lock(mutex_);
    work_queue_.push_back(wentry);
    if (!running_) {
        worker_task_ = new Worker(this);
        TaskScheduler *scheduler = TaskScheduler::GetInstance();
        scheduler->Enqueue(worker_task_);
        running_ = true;
    }
}

//
// Enqueue a WorkPeer to the work queue.
//
void SchedulingGroup::WorkPeerEnqueue(IPeerUpdate *peer) {
    CHECK_CONCURRENCY("bgp::SendReadyTask");

    WorkBase *wentry = new WorkPeer(peer);
    WorkEnqueue(wentry);
}

//
// Enqueue a WorkRibOut to the work queue.
//
void SchedulingGroup::WorkRibOutEnqueue(RibOut *ribout, int queue_id) {
    CHECK_CONCURRENCY("db::DBTable", "bgp::SendTask");

    WorkBase *wentry = new WorkRibOut(ribout, queue_id);
    WorkEnqueue(wentry);
}

//
// Build the RibPeerSet of IPeers for the RibOut that are in sync and out of
// sync. Note that we need to use bit indices that are specific to the RibOut,
// not the ones from the SchedulingGroup.
//
void SchedulingGroup::BuildSyncUnsyncBitSet(const RibOut *ribout, RibState *rs,
        RibPeerSet *msync, RibPeerSet *munsync) {
    CHECK_CONCURRENCY("bgp::SendTask");

    for (RibState::iterator iter = rs->begin(peer_state_imap_);
         iter != rs->end(peer_state_imap_); ++iter) {
        const PeerState *ps = iter.operator->();
        int rix = ribout->GetPeerIndex(ps->peer());
        if (ps->in_sync()) {
            msync->set(rix);
        } else {
            munsync->set(rix);
        }
    }
}

//
// Build the RibPeerSet of IPeers for the RibOut that are send ready. Note
// that we need to use bit indices that are specific to the RibOut, not the
// ones from the SchedulingGroup.
//
void SchedulingGroup::BuildSendReadyBitSet(RibOut *ribout, RibPeerSet *mready) {
    CHECK_CONCURRENCY("bgp::SendTask");

    RibState *rs = rib_state_imap_.Find(ribout);
    assert(rs != NULL);
    for (RibState::iterator iter = rs->begin(peer_state_imap_);
         iter != rs->end(peer_state_imap_); ++iter) {
        const PeerState *ps = iter.operator->();
        if (ps->send_ready()) {
            int rix = ribout->GetPeerIndex(ps->peer());
            mready->set(rix);
        }
    }
}

//
// Concurrency: called from bgp send task.
//
// Take the RibPeerSet of blocked IPeers and update the relevant PeerStates.
// Note that bit indices in the RibPeerSet and are specific to the RibOut.
//
void SchedulingGroup::SetSendBlocked(const RibOut *ribout, RibState *rs,
        int queue_id, const RibPeerSet &blocked) {
    CHECK_CONCURRENCY("bgp::SendTask");

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
// Concurrency: called from bgp send task.
//
// Take the RibPeerSet of unsync IPeers and update the relevant PeerStates.
// Note that bit indices in the RibPeerSet and are specific to the RibOut.
//
void SchedulingGroup::SetQueueActive(const RibOut *ribout, RibState *rs,
        int queue_id, const RibPeerSet &munsync) {
    CHECK_CONCURRENCY("bgp::SendTask");

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
void SchedulingGroup::SetQueueActive(RibOut *ribout, int queue_id,
    IPeerUpdate *peer) {
    CHECK_CONCURRENCY("bgp::SendTask");

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
bool SchedulingGroup::IsQueueActive(RibOut *ribout, int queue_id,
    IPeerUpdate *peer) {
    CHECK_CONCURRENCY("bgp::SendTask");

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
void SchedulingGroup::SetQueueSync(PeerState *ps, int queue_id) {
    CHECK_CONCURRENCY("bgp::SendTask");

    for (PeerState::iterator iter = ps->begin(rib_state_imap_);
         iter != ps->end(rib_state_imap_); ++iter) {
         RibState *rs = iter.rib_state();
         if (!rs->QueueSync(queue_id)) {
             RibOut *ribout = iter.operator->();
             RibOutActive(ribout, queue_id);
             rs->SetQueueSync(queue_id);
         }
    }
}

//
// Drain the queue until there are no more updates or all the members become
// blocked.
//
void SchedulingGroup::UpdateRibOut(RibOut *ribout, int queue_id) {
    CHECK_CONCURRENCY("bgp::SendTask");

    RibOutUpdates *updates = ribout->updates();
    RibState *rs = rib_state_imap_.Find(ribout);
    RibPeerSet msync, munsync;

    // Convert group in-sync list to rib specific bitset.
    BuildSyncUnsyncBitSet(ribout, rs, &msync, &munsync);

    // Drain the queue till we can do no more.
    RibPeerSet blocked;
    bool done = updates->TailDequeue(queue_id, msync, &blocked);
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
bool SchedulingGroup::UpdatePeerQueue(IPeerUpdate *peer, PeerState *ps,
        int queue_id) {
    CHECK_CONCURRENCY("bgp::SendTask");

    for (PeerState::circular_iterator iter =
            ps->circular_begin(rib_state_imap_);
         iter != ps->circular_end(rib_state_imap_); ++iter) {

        // Skip if this queue is not active in the PeerRibState.
        if (!BitIsSet(iter.peer_rib_state().qactive, queue_id))
            continue;

        RibOut *ribout = iter.operator->();

        // Build the send ready bitset. This includes all send ready peers
        // for the ribout so that we can potentially merge other peers as
        // we move forward in processing the update queue.
        RibPeerSet send_ready;
        BuildSendReadyBitSet(ribout, &send_ready);

        // Drain the queue till we can do no more.
        RibOutUpdates *updates = ribout->updates();
        RibPeerSet blocked;
        bool done = updates->PeerDequeue(queue_id, peer, send_ready, &blocked);
        assert(send_ready.Contains(blocked));

        // Process blocked mask.
        RibState *rs = iter.rib_state();
        SetSendBlocked(ribout, rs, queue_id, blocked);

        // Remember where to start next time if we got blocked.
        if (!done) {
            assert(!ps->send_ready());
            ps->SetIteratorStart(iter.index());
            return false;
        }

        // Mark the queue as inactive for the peer.  Need to check send_ready
        // since success from PeerDequeue only means that *some* peer merged
        // with the tail marker.
        if (ps->send_ready())
            ps->SetQueueInactive(rs->index(), queue_id);
    }

    return true;
}

//
// Drain the queue of all updates for this IPeerUpdate, until it is up-to date
// or it becomes blocked.
//
void SchedulingGroup::UpdatePeer(IPeerUpdate *peer) {
    CHECK_CONCURRENCY("bgp::SendTask");

    PeerState *ps = peer_state_imap_.Find(peer);
    if (!ps->send_ready()) {
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
// Check invariants for the SchedulingGroup.
//
bool SchedulingGroup::CheckInvariants() const {
    int grp_peer_count = 0;
    int peer_grp_count = 0;
    for (int i = 0; i < (int) rib_state_imap_.size(); i++) {
        if (!rib_state_imap_.bits().test(i)) {
            continue;
        }
        RibState *rs = rib_state_imap_.At(i);
        assert(rs != NULL);
        CHECK_INVARIANT(rs->index() == i);
        for (RibState::iterator iter = rs->begin(peer_state_imap_);
             iter != rs->end(peer_state_imap_); ++iter) {
            PeerState *ps = iter.operator->();
            CHECK_INVARIANT(ps->IsMember(i));
            grp_peer_count++;
        }
    }
    for (int i = 0; i < (int) peer_state_imap_.size(); i++) {
        if (!peer_state_imap_.bits().test(i)) {
            continue;
        }
        PeerState *ps = peer_state_imap_.At(i);
        assert(ps != NULL);
        CHECK_INVARIANT(ps->index() == i);
        if (!ps->CheckInvariants()) {
            return false;
        }
        for (PeerState::iterator iter = ps->begin(rib_state_imap_);
             iter != ps->end(rib_state_imap_); ++iter) {
            RibState *rs = iter.rib_state();
            CHECK_INVARIANT(rs->peer_set().test(i));
            peer_grp_count++;
        }
    }

    CHECK_INVARIANT(grp_peer_count == peer_grp_count);
    return true;
}

//
// Explicit specialilization to prevent deletion of IPeer from the destructor
// for WorkQueue<IPeer *>. In fact, the code doesn't even compile without this
// specialization since IPeer is abstract but has a non-virtual destructor by
// default.
//
template<>
struct WorkQueueDelete<IPeerUpdate *> {
    void operator()(WorkQueue<IPeerUpdate *>::Queue &queue,
                    bool delete_entry) { }
};

//
// Constructor for SchedulingGroupManager. Initialize send ready WorkQueue.
//
SchedulingGroupManager::SchedulingGroupManager() :
    send_ready_queue_(
            TaskScheduler::GetInstance()->GetTaskId("bgp::SendReadyTask"), 0,
            boost::bind(&SchedulingGroupManager::SendReadyCallback, this, _1)) {
}

SchedulingGroupManager::~SchedulingGroupManager() {
    STLDeleteValues(&groups_);
}


//
// Return the SchedulingGroup for the specified IPeerUpdate.
//
SchedulingGroup *SchedulingGroupManager::PeerGroup(IPeerUpdate *peer) const {
    PeerMap::const_iterator loc = peer_map_.find(peer);
    if (loc != peer_map_.end()) {
        return loc->second;
    }
    return NULL;
}

//
// Return the SchedulingGroup for the specified RibOut.
//
SchedulingGroup *SchedulingGroupManager::RibOutGroup(RibOut *ribout) const {
    RibOutMap::const_iterator loc = ribout_map_.find(ribout);
    if (loc != ribout_map_.end()) {
        return loc->second;
    }
    return NULL;
}

//
// Handle the join of an IPeerUpdate to a RibOut.
//
void SchedulingGroupManager::Join(RibOut *ribout, IPeerUpdate *peer) {
    CHECK_CONCURRENCY("bgp::PeerMembership");

    PeerMap::iterator i1 = peer_map_.find(peer);
    RibOutMap::iterator i2 = ribout_map_.find(ribout);

    SchedulingGroup *sg;
    if (i1 == peer_map_.end()) {
        if (i2 == ribout_map_.end()) {
            // Create new empty group
            sg = BgpObjectFactory::Create<SchedulingGroup>();
            groups_.push_back(sg);
            ribout_map_.insert(make_pair(ribout, sg));
        } else {
            // Add peer to existing group
            sg = i2->second;
        }
        peer_map_.insert(make_pair(peer, sg));
    } else {
        if (i2 == ribout_map_.end()) {
            // Add ribout to existing group
            sg = i1->second;
            ribout_map_.insert(make_pair(ribout, sg));
        } else if (i1->second == i2->second) {
            // No change.
            sg = i1->second;
        } else {
            // Merge the two existing groups
            sg = Merge(i1->second, i2->second);
        }
    }

    // Add the ribout and peer combination to the group.
    sg->Add(ribout, peer);
}

//
// Handle the leave of an IPeerUpdate from a RibOut.  We may be able to split
// the SchedulingGroup to which this combination belongs.
//
void SchedulingGroupManager::Leave(RibOut *ribout, IPeerUpdate *peer) {
    CHECK_CONCURRENCY("bgp::PeerMembership");

    SchedulingGroup *sg = PeerGroup(peer);
    assert(sg != NULL);

    // First remove the combination from the SchedulingGroup.
    sg->Remove(ribout, peer);

    // If there are no more peers interested in the ribout, remove the ribout
    // from the RiboutMap.
    if (sg->GetRibOutIndex(ribout) == -1) {
        ribout_map_.erase(ribout);
    }

    // If there are no more ribouts that the peer is interested in, remove the
    // peer from the PeerMap.
    if (sg->GetPeerIndex(peer) == -1) {
        peer_map_.erase(peer);
    }

    // Get rid of the group itself if it's empty.
    if (sg->empty()) {
        groups_.remove(sg);
        delete sg;
        return;
    }

    // Now check if it's possible to split the group.  Given how we define a
    // scheduling group and the logic of how joins are handled, we simply need
    // to check if the (peer, ribout) combo being removed was the only reason
    // that all the other (peer, ribout) combos needed to be in the same group.

    // Retrieve the list of ribs that this peer is still advertising along with
    // its complement. If either the advertised list or the complement list are
    // empty, the group can't be split.
    RibOutList rlist;
    RibOutList rcomplement;
    sg->GetPeerRibList(peer, &rlist, &rcomplement);
    if (rlist.empty() || rcomplement.empty()) {
        return;
    }

    // Get the bitsets of peers advertising one or more of the ribouts in the
    // advertised list and the complement list respectively. If the two bitsets
    // don't have any peers in common, the group can be split based on the two
    // ribout lists.
    GroupPeerSet s1;
    sg->GetSubsetPeers(rlist, &s1);
    GroupPeerSet s2;
    sg->GetSubsetPeers(rcomplement, &s2);
    if (!s1.intersects(s2)) {
        Split(sg, rlist, rcomplement);
    }
}

//
// Update the PeerMap and the RibOutMap so that all IPeers and RibOuts that
// are in the src group now point to the dst group.  Note that the groups
// themselves are left unchanged.
//
void SchedulingGroupManager::Move(
        SchedulingGroup *src_sg, SchedulingGroup *dst_sg) {
    CHECK_CONCURRENCY("bgp::PeerMembership");

    // Walk through all the RibOuts in the source group and update RibOutMap
    // entries for each of them.
    RibOutList rlist;
    src_sg->GetRibOutList(&rlist);
    for (RibOutList::const_iterator iter = rlist.begin(); iter != rlist.end();
         ++iter) {
        RibOutMap::iterator loc = ribout_map_.find(*iter);
        assert(loc != ribout_map_.end());
        loc->second = dst_sg;
    }

    // Walk through all the IPeers in the source group and update PeerMap
    // entries for each of them.
    SchedulingGroup::PeerList plist;
    src_sg->GetPeerList(&plist);
    for (SchedulingGroup::PeerList::const_iterator iter = plist.begin();
         iter != plist.end(); ++iter) {
        PeerMap::iterator loc = peer_map_.find(*iter);
        assert(loc != peer_map_.end());
        loc->second = dst_sg;
    }
}

//
// Merge the two scheduling groups and return a pointer to the combined one.
//
SchedulingGroup *SchedulingGroupManager::Merge(
        SchedulingGroup *sg1, SchedulingGroup *sg2) {
    CHECK_CONCURRENCY("bgp::PeerMembership");

    SchedulingGroup *sg = sg1;

    // First update the state in the SchedulingGroupManager.
    Move(sg2, sg);

    // Now update the state in the first SchedulingGroup and delete the
    // second one.
    sg->Merge(sg2);
    groups_.remove(sg2);
    delete sg2;

    return sg;
}

//
// Split the given scheduling group based on the two RibOutLists.  All the
// RibOuts in the second list and, by definition, all the IPeers advertising
// any of those RibOuts, will end up in a new SchedulingGroup.
//
void SchedulingGroupManager::Split(
        SchedulingGroup *sg, const RibOutList &rg1, const RibOutList &rg2) {
    CHECK_CONCURRENCY("bgp::PeerMembership");

    SchedulingGroup *sg2 = BgpObjectFactory::Create<SchedulingGroup>();
    groups_.push_back(sg2);

    // Note that calling the Split method results in the creation of all
    // necessary PeerState and RibOutState in sg2. Hence, there's no typo
    // in the arguments passed to Move. That call merely serves to update
    // the state in the SchedulingGroupManager.
    sg->Split(sg2, rg1, rg2);
    Move(sg2, sg2);
}

//
// Concurrency: called from arbitrary task.
//
// Enqueue the IPeer to the send ready processing work queue.  The callback
// will be invoked in the context of the bgp send task.
//
void SchedulingGroupManager::SendReady(IPeerUpdate *peer) {
    send_ready_queue_.Enqueue(peer);
}

//
// Concurrency: called from the bgp send ready task.
//
// Callback to handle send ready notification for IPeerUpdate. Processing it
// in the context of the bgp send task ensures that there are no concurrency
// issues w.r.t. the SchedulingGroup for the IPeerUpdate getting changed while
// we are processing the notification. This is guaranteed because the bgp peer
// membership task, which modifies the SchedulingGroup for an IPeerUpdate via
// calls to SchedulingGroupManager::Join/Leave, does not run concurrently with
// the bgp send task.
//
bool SchedulingGroupManager::SendReadyCallback(IPeerUpdate *peer) {
    CHECK_CONCURRENCY("bgp::SendReadyTask");

    // The IPeerUpdate may not be registered with the SchedulingGroupManager
    // if it has not reached Established state.  Or it may already have been
    // unregistered before we got to around to processing the queue entry.
    SchedulingGroup *sg = PeerGroup(peer);
    if (sg)
        IPeerSendReady(sg, peer);
    return true;
}

//
// Check invariants for the SchedulingGroupManager. For now, we simply check
// the invariants for each individual SchedulingGroup.
//
bool SchedulingGroupManager::CheckInvariants() const {
    for (GroupList::const_iterator iter = groups_.begin();
         iter != groups_.end(); ++iter) {
        SchedulingGroup *sg = *iter;
        if (!sg->CheckInvariants()) {
            return false;
        }
    }
    return true;
}

//
// Concurrency: called from the bgp send ready task.
//
// Free function to allow SchedulingGroupManager::SendReadyCallback to call
// SchedulingGroup::SendReady without exposing any other private members of
// SchedulingGroup to SchedulingGroupManager.
//
// Note that this function is declared to be a friend of SchedulingGroup.
//
void IPeerSendReady(SchedulingGroup *sg, IPeerUpdate *peer) {
    CHECK_CONCURRENCY("bgp::SendReadyTask");

    sg->SendReady(peer);
}
