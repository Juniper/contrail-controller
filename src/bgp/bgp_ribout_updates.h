/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_BGP_BGP_RIBOUT_UPDATES_H_
#define SRC_BGP_BGP_RIBOUT_UPDATES_H_

#include <boost/scoped_ptr.hpp>

#include <vector>

#include "base/util.h"

class BgpTable;
class DBEntryBase;
class IPeerUpdate;
class Message;
class RibPeerSet;
class RibUpdateMonitor;
class RibOut;
class RouteUpdate;
class RouteUpdatePtr;
class ShowRibOutStatistics;
class UpdateQueue;
struct UpdateInfo;
struct UpdateMarker;

//
// This class is a logical abstraction of the update processing state and
// functionality for a RibOut. An instance of RibOutUpdates is created per
// DB partition. So there's a 1:N mapping between RibOut and RibOutUpdates.
// Further, the RibOutUpdates keeps a smart pointer to a RibUpdateMonitor
// with which it has a 1:1 association.
//
// A RibOutUpdates also maintains a vector of pointers to UpdateQueue. The
// 2 UpdateQueues are used for bulk and regular updates.  Note that access
// to all the UpdateQueue goes through the RibUpdateMonitor which enforces
// all the concurrency constraints.  There's an exception for UpdateMarkers
// which are accessed directly through the UpdateQueue.
//
class RibOutUpdates {
public:
    typedef std::vector<UpdateQueue *> QueueVec;
    static const int kQueueIdInvalid = -1;
    enum QueueId {
        QFIRST   = 0,
        QBULK   = 0,
        QUPDATE,
        QCOUNT
    };

    struct Stats {
        uint64_t messages_built_count_;
        uint64_t messages_sent_count_;
        uint64_t reach_count_;
        uint64_t unreach_count_;
        uint64_t tail_dequeue_count_;
        uint64_t peer_dequeue_count_;
        uint64_t marker_split_count_;
        uint64_t marker_merge_count_;
        uint64_t marker_move_count_;
    };

    RibOutUpdates(RibOut *ribout, int index);
    virtual ~RibOutUpdates();

    static void Initialize();
    static void Terminate();

    void Enqueue(DBEntryBase *db_entry, RouteUpdate *rt_update);

    virtual bool TailDequeue(int queue_id, const RibPeerSet &msync,
                             RibPeerSet *blocked, RibPeerSet *unsync);
    virtual bool PeerDequeue(int queue_id, IPeerUpdate *peer,
                             RibPeerSet *blocked);

    // Enqueue a marker at the head of the queue with this bit set.
    bool QueueJoin(int queue_id, int bit);
    void QueueLeave(int queue_id, int bit);

    bool Empty() const;
    size_t queue_size(int queue_id) const;
    size_t queue_marker_count(int queue_id) const;

    RibUpdateMonitor *monitor() { return monitor_.get(); }

    UpdateQueue *queue(int queue_id) {
        return queue_vec_[queue_id];
    }

    QueueVec &queue_vec() { return queue_vec_; }
    const QueueVec &queue_vec() const { return queue_vec_; }

    void AddStatisticsInfo(int queue_id, Stats *stats) const;

private:
    friend class RibOutUpdatesTest;
    friend class XmppMessageBuilderTest;
    friend class XmppMvpnMessageBuilderTest;

    Message *GetMessage() const;
    bool DequeueCommon(UpdateQueue *queue, UpdateMarker *marker,
                       RouteUpdate *rt_update, RibPeerSet *blocked);

    // Add additional updates.
    void UpdatePack(int queue_id, Message *message, UpdateInfo *start_uinfo,
                    const RibPeerSet &isect);

    // Transmit the updates to a set of peers.
    void UpdateSend(int queue_id, Message *message, const RibPeerSet &dst,
                    RibPeerSet *blocked);
    void UpdateFlush(const RibPeerSet &dst, RibPeerSet *blocked);

    // Remove the advertised bits on an update. This updates the history
    // information. Returns true if the UpdateInfo should be deleted.
    bool ClearAdvertisedBits(RouteUpdate *rt_update, UpdateInfo *uinfo,
                             const RibPeerSet &bits, bool update_history);

    void StoreHistory(RouteUpdate *rt_update);
    void ClearState(RouteUpdate *rt_update);
    void ClearUpdate(RouteUpdatePtr *update);

    bool UpdateMarkersOnBlocked(UpdateMarker *marker, RouteUpdate *rt_update,
                                const RibPeerSet *blocked);

    RibOut *ribout_;
    int index_;
    QueueVec queue_vec_;
    Stats stats_[QCOUNT];
    boost::scoped_ptr<RibUpdateMonitor> monitor_;
    static std::vector<Message *> bgp_messages_;
    static std::vector<Message *> xmpp_messages_;

    DISALLOW_COPY_AND_ASSIGN(RibOutUpdates);
};

#endif  // SRC_BGP_BGP_RIBOUT_UPDATES_H_
