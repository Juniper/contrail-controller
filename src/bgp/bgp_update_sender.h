/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_BGP_BGP_UPDATE_SENDER_H_
#define SRC_BGP_BGP_UPDATE_SENDER_H_

#include <boost/ptr_container/ptr_list.hpp>

#include <vector>

#include "base/bitset.h"
#include "base/index_map.h"
#include "base/queue_task.h"

class BgpServer;
class BgpUpdateSender;
class IPeerUpdate;
class RibOut;
class RibPeerSet;

//
// This class maintains state to generate updates for a DB partition for all
// RibOuts and IPeerUpdates in a BgpServer.  All BgpSenderPartitions work in
// parallel and maintain their own view of whether an IPeerUpdate is blocked,
// in sync etc.
//
// A BgpSenderPartition maintains two indexed maps for internal bookkeeping.
//
// o PeerStateMap allocates an index per IPeerUpdate to allow direct access to
//   the corresponding PeerState using the index.  The PeerState nested class
//   is described elsewhere.
// o RibStateMap allocates a bit index per RibOut and allows direct access to
//   the corresponding RibState using the index. The RibState nested class is
//   described elsewhere.
//
// BgpSenderPartition contains a WorkQueue of WorkBase entries to represent
// pending work. A WorkBase can either be a WorkRibOut, which corresponds to
// to a tail dequeue for a (RibOut, QueueId) or a WorkPeer which corresponds
// to a peer dequeue.
//
// A mutex is used to control access to the WorkQueue between producers that
// need to enqueue WorkBase entries and the Worker which dequeues the entries
// and processes them. The producers are the BgpExport class which creates a
// WorkRibOut entry after adding a RouteUpdate to an empty UpdateQueue, and
// IPeerUpdate class which creates a WorkPeer entry when it becomes unblocked.
//
class BgpSenderPartition {
public:
    BgpSenderPartition(BgpUpdateSender *sender, int index);
    ~BgpSenderPartition();

    void Add(RibOut *ribout, IPeerUpdate *peer);
    void Remove(RibOut *ribout, IPeerUpdate *peer);

    void RibOutActive(RibOut *ribout, int queue_id);

    void PeerSendReady(IPeerUpdate *peer);
    bool PeerIsSendReady(IPeerUpdate *peer) const;
    bool PeerIsRegistered(IPeerUpdate *peer) const;
    bool PeerInSync(IPeerUpdate *peer) const;

    bool CheckInvariants() const;

    int task_id() const;
    int index() const { return index_; }

    // For unit testing.
    void set_disabled(bool disabled);

private:
    friend class BgpUpdateSenderTest;
    friend class RibOutUpdatesTest;

    struct WorkBase {
        enum Type {
            WPeer,
            WRibOut
        };
        explicit WorkBase(Type type)
            : type(type), valid(true) {
        }
        Type type;
        bool valid;
    };

    struct WorkRibOut : public WorkBase {
        WorkRibOut(RibOut *ribout, int queue_id)
            : WorkBase(WRibOut), ribout(ribout), queue_id(queue_id) {
        }
        RibOut *ribout;
        int queue_id;
    };

    struct WorkPeer : public WorkBase {
        explicit WorkPeer(IPeerUpdate *peer)
            : WorkBase(WPeer), peer(peer) {
        }
        IPeerUpdate *peer;
    };

    class PeerState;
    struct PeerRibState;
    class RibState;
    class Worker;

    typedef boost::ptr_list<WorkBase> WorkQueue;
    typedef IndexMap<IPeerUpdate *, PeerState> PeerStateMap;
    typedef IndexMap<RibOut *, RibState> RibStateMap;

    void MaybeStartWorker();
    std::auto_ptr<WorkBase> WorkDequeue();
    void WorkEnqueue(WorkBase *wentry);
    void WorkPeerEnqueue(IPeerUpdate *peer);
    void WorkPeerInvalidate(IPeerUpdate *peer);
    void WorkRibOutEnqueue(RibOut *ribout, int queue_id);
    void WorkRibOutInvalidate(RibOut *ribout);

    void UpdateRibOut(RibOut *ribout, int queue_id);
    void UpdatePeer(IPeerUpdate *peer);

    bool UpdatePeerQueue(IPeerUpdate *peer, PeerState *ps, int queue_id);

    void BuildSyncBitSet(const RibOut *ribout, RibState *rs, RibPeerSet *msync);

    void SetQueueActive(const RibOut *ribout, RibState *rs, int queue_id,
                        const RibPeerSet &munsync);
    void SetQueueActive(RibOut *ribout, int queue_id, IPeerUpdate *peer);
    bool IsQueueActive(RibOut *ribout, int queue_id, IPeerUpdate *peer);
    void SetSendBlocked(RibOut *ribout, int queue_id,
                        const RibPeerSet &blocked);
    void SetSendBlocked(const RibOut *ribout, RibState *rs, int queue_id,
                        const RibPeerSet &blocked);
    void SetQueueSync(PeerState *ps, int queue_id);

    BgpUpdateSender *sender_;
    int index_;
    bool running_;
    bool disabled_;
    WorkQueue work_queue_;
    Worker *worker_task_;
    PeerStateMap peer_state_imap_;
    RibStateMap rib_state_imap_;

    DISALLOW_COPY_AND_ASSIGN(BgpSenderPartition);
};

//
// This is a wrapper that hides the existence of multiple BgpSenderPartitions
// from client classes.  It relays APIs to appropriate/all BgpSenderPartition
// instance(s).
//
// The send ready WorkQueue is needed to process send ready notifications for
// IPeerUpdates in the context of bgp::SendReadyTask. This ensures that there
// are no concurrency issues in case the IPeerUpdate gets unblocked while we
// are still processing the previous WorkBase which caused it to get blocked
// in the first place.
//
class BgpUpdateSender {
public:
    explicit BgpUpdateSender(BgpServer *server);
    ~BgpUpdateSender();

    void Join(RibOut *ribout, IPeerUpdate *peer);
    void Leave(RibOut *ribout, IPeerUpdate *peer);

    void RibOutActive(int index, RibOut *ribout, int queue_id);
    void PeerSendReady(IPeerUpdate *peer);
    bool PeerIsRegistered(IPeerUpdate *peer) const;
    bool PeerInSync(IPeerUpdate *peer) const;

    int task_id() const { return task_id_; }
    bool CheckInvariants() const;

    // For unit testing.
    void DisableProcessing();
    void EnableProcessing();

private:
    friend class BgpTestPeer;
    friend class BgpUpdateSenderTest;
    friend class RibOutUpdatesTest;

    bool SendReadyCallback(IPeerUpdate *peer);
    BgpSenderPartition *partition(int index) { return partitions_[index]; }

    BgpServer *server_;
    int task_id_;
    std::vector<BgpSenderPartition *> partitions_;
    WorkQueue<IPeerUpdate *> send_ready_queue_;

    DISALLOW_COPY_AND_ASSIGN(BgpUpdateSender);
};

#endif  // SRC_BGP_BGP_UPDATE_SENDER_H_
