/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_scheduling_group_h
#define ctrlplane_scheduling_group_h

#include <list>
#include <map>
#include <vector>
#include <boost/ptr_container/ptr_deque.hpp>
#include <tbb/mutex.h>

#include "base/bitset.h"
#include "base/index_map.h"
#include "base/queue_task.h"

class IPeerUpdate;
class RibOut;
class RibPeerSet;

class GroupPeerSet : public BitSet {
};

//
// This class represents a group of IPeers and their respective RibOuts such
// that for all the IPeers, all RibOuts they advertise are part of the same
// SchedulingGroup and for all RibOuts, all their member IPeers are part of
// the same SchedulingGroup.  SchedulingGroups can merge and split as their
// respective membership changes.
//
// With the above definition of a SchedulingGroup, we can make it correspond
// to one sending thread. We want one thread to write to a IPeerUpdate across
// all RibOuts so that we can avoid contention on the IPeerUpdate.
//
// A SchedulingGroup maintains two indexed maps for it's internal bookkeeping
// purposes.
//
// o PeerStateMap allocates an index per IPeerUpdate to allow direct access to
//   the corresponding PeerState using the index.  The PeerState nested class
//   is described elsewhere.
// o RibStateMap allocates a bit index per RibOut and allows direct access to
//   the corresponding RibState using the index. The RibState nested class is
//   described elsewhere.
//
// The SchedulingGroup contains a WorkQueue of WorkBase entries to represent
// pending work. A WorkBase can either be a WorkRibOut, which corresponds to
// to a tail dequeue for a (RibOut, QueueId) or a WorkPeer which corresponds
// to a peer dequeue.
//
// A mutex is used to control access to the WorkQueue between producers that
// need to enqueue WorkBase entries and the Worker which dequeues the entries
// and processes them. The producers are the BgpExport class which creates a
// WorkRibOut entry after adding a RouteUpdate to an empty UpdateQueue, and
// the IPeer class which create a WorkPeer entry when it becomes unblocked.
//
class SchedulingGroup {
public:
    typedef std::vector<RibOut *> RibOutList;
    typedef std::vector<IPeerUpdate *> PeerList;

    SchedulingGroup();
    ~SchedulingGroup();

    void Merge(SchedulingGroup *rhs);
    void Split(SchedulingGroup *other, const RibOutList &rg1,
               const RibOutList &rg2);

    void Add(RibOut *ribout, IPeerUpdate *peer);
    void Remove(RibOut *ribout, IPeerUpdate *peer);

    // When an update is enqueued at the tail of a particular RibOut,
    // the peers that are in-sync across all ribs can start dequeing
    // updates.
    void RibOutActive(RibOut *ribout, int queue_id);

    // Warning: unsafe to call these from arbitrary tasks.
    bool IsSendReady(IPeerUpdate *peer) const;
    bool PeerInSync(IPeerUpdate *peer) const;

    // The index for a specific peer in this group.
    int GetPeerIndex(IPeerUpdate *peer) const;
    
    // The index for a specific ribout.
    int GetRibOutIndex(RibOut *ribout) const;

    // Retrieve the list of all the ribs which this peer is advertising as
    // well as its complement.
    void GetPeerRibList(IPeerUpdate *peer, RibOutList *rlist,
                        RibOutList *rcomplement) const;

    void GetRibPeerList(RibOut *ribout, PeerList *plist) const;

    // Retrieve the list of peers with this specific set of ribs.
    void GetSubsetPeers(const RibOutList &list, GroupPeerSet *pg);

    void GetRibOutList(RibOutList *rlist) const;
    void GetPeerList(PeerList *plist) const;

    bool CheckInvariants() const;

    void clear();
    bool empty() const;

private:
    friend class RibOutUpdatesTest;
    friend class BgpUpdateTest;
    friend class SGTest;
    friend void IPeerSendReady(SchedulingGroup *sg, IPeerUpdate *peer);

    class RibState;
    class PeerState;
    struct PeerRibState;
    struct WorkBase;
    struct WorkRibOut;
    struct WorkPeer;

    typedef boost::ptr_deque<WorkBase> WorkQueue;
    typedef IndexMap<IPeerUpdate *, PeerState, GroupPeerSet> PeerStateMap;
    typedef IndexMap<RibOut *, RibState> RibStateMap;
    class Worker;

    class PeerIterator;

    std::auto_ptr<WorkBase> WorkDequeue();
    void WorkEnqueue(WorkBase *wentry);

    void UpdateRibOut(RibOut *ribout, int queue_id);
    void UpdatePeer(IPeerUpdate *peer);

    // Notification that a peer is send ready.
    void SendReady(IPeerUpdate *peer);

    // Process ribs for a specific queue_id.
    // Returns true if all RIBs synced, false if send blocked.
    bool UpdatePeerQueue(IPeerUpdate *peer, PeerState *ps, int queue_id);

    void BuildSyncUnsyncBitSet(const RibOut *ribout, RibState *rs,
                               RibPeerSet *msync, RibPeerSet *munsync);
    void BuildSendReadyBitSet(RibOut *ribout, RibPeerSet *mready);

    void SetQueueActive(const RibOut *ribout, RibState *rs, int queue_id,
                        const RibPeerSet &munsync);
    void SetSendBlocked(const RibOut *ribout, RibState *rs, int queue_id,
                        const RibPeerSet &blocked);
    void SetQueueSync(PeerState *ps, int queue_id);

    RibOut *PeerRibOutFirst(PeerState *ps, size_t *start);
    RibOut *PeerRibOutNext(PeerState *ps, size_t start);

    // The mutex controls access to WorkQueue and related Worker state.
    tbb::mutex mutex_;
    WorkQueue work_queue_;
    bool running_;
    Worker *worker_task_;

    PeerStateMap peer_state_imap_;
    RibStateMap rib_state_imap_;
    
    static int send_task_id_;

    DISALLOW_COPY_AND_ASSIGN(SchedulingGroup);
};

//
// This class implements the logic to create, merge, split and delete
// SchedulingGroup as IPeerUpdate membership in RibOut changes.
//
// A SchedulingGroupManager keeps a map of IPeerUpdate ptrs to SchedulingGroup
// pointers as well as a map of RibOut pointers to SchedulingGroup pointers to
// allow fast lookup.
//
// It also maintains a non-intrusive list of SchedulingGroup. This is used to
// iterate through all SchedulingGroups.
//
// The send ready WorkQueue is needed to process send ready notifications for
// IPeers in the context of the bgp send task. Comments for SendReadyCallback
// provide more details.
//
class SchedulingGroupManager {
public:
    typedef SchedulingGroup::RibOutList RibOutList;
    typedef std::list<SchedulingGroup *> GroupList;
    typedef std::map<IPeerUpdate *, SchedulingGroup *> PeerMap;
    typedef std::map<RibOut *, SchedulingGroup *> RibOutMap;

    SchedulingGroupManager();
    ~SchedulingGroupManager();

    SchedulingGroup *PeerGroup(IPeerUpdate *peer) const;
    SchedulingGroup *RibOutGroup(RibOut *ribout) const;

    // Called when a peer is added to a RibOut.
    void Join(RibOut *ribout, IPeerUpdate *peer);

    // Called when a peer is removed from a RibOut.
    void Leave(RibOut *ribout, IPeerUpdate *peer);

    // Notification that a peer is send ready.
    void SendReady(IPeerUpdate *peer);

    bool CheckInvariants() const;

    // Number of SchedulingGroups.
    int size() const { return groups_.size(); }

private:
    // Merge two existing scheduling groups.
    SchedulingGroup *Merge(SchedulingGroup *sg1, SchedulingGroup *sg2);

    // Divide an existing scheduling group into the two component subsets.
    void Split(SchedulingGroup *sg, const RibOutList &rg1,
               const RibOutList &rg2);

    void Move(SchedulingGroup *group, SchedulingGroup *dst);

    GroupList groups_;
    PeerMap peer_map_;
    RibOutMap ribout_map_;

    // Deferred send ready processing.
    WorkQueue<IPeerUpdate *> send_ready_queue_;
    bool SendReadyCallback(IPeerUpdate *peer);

    DISALLOW_COPY_AND_ASSIGN(SchedulingGroupManager);
};

#endif
