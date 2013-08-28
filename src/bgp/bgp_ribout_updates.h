/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_bgp_ribout_updates_h
#define ctrlplane_bgp_ribout_updates_h

#include "bgp/bgp_ribout.h"

class BgpTable;
class Message;
class MessageBuilder;
class RibUpdateMonitor;
class RouteUpdate;
class RouteUpdatePtr;
class UpdateQueue;
struct UpdateInfo;
struct UpdateMarker;

//
// This class is a logical abstraction of the update processing state and
// functionality that would have otherwise been part of RibOut itself. As
// such, there's a 1:1 association between a RibOut and a RibOutUpdates.
// Further, the RibOutUpdates keeps a smart pointer to a RibUpdateMonitor
// with which it also has a 1:1 association.
//
// A RibOutUpdates also maintains a vector of pointers to UpdateQueue. The
// 2 UpdateQueues are used for bulk and regular updates.  Note that access
// to all the UpdateQueue goes through the RibUpdateMonitor which enforces
// all the concurrency constraints.  There's an exception for UpdateMarker
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
    explicit RibOutUpdates(RibOut *ribout);
    virtual ~RibOutUpdates();

    void Enqueue(DBEntryBase *db_entry, RouteUpdate *rt_update);

    virtual bool TailDequeue(int queue_id,
                             const RibPeerSet &msync, RibPeerSet *blocked);
    virtual bool PeerDequeue(int queue_id, IPeerUpdate *peer,
                             const RibPeerSet &mready, RibPeerSet *blocked);

    // Enqueue a marker at the head of the queue with this bit set.
    void QueueJoin(int queue_id, int bit);
    void QueueLeave(int queue_id, int bit);
    
    bool Empty() const;

    RibUpdateMonitor *monitor() { return monitor_.get(); }

    UpdateQueue *queue(int queue_id) {
        return queue_vec_[queue_id];
    }

    QueueVec &queue_vec() { return queue_vec_; }

    // Testing only
    void SetMessageBuilder(MessageBuilder *builder) { builder_ = builder; }

private:
    friend class RibOutUpdatesTest;

    bool DequeueCommon(UpdateMarker *marker, RouteUpdate *rt_update,
                       RibPeerSet *blocked);
    
    // Add additional updates.
    void UpdatePack(int queue_id, Message *message, UpdateInfo *start_uinfo,
                    const RibPeerSet &isect);

    // Transmit the updates to a set of peers.
    void UpdateSend(Message *message, const RibPeerSet &dst,
                    RibPeerSet *blocked);

    // Remove the advertised bits on an update. This updates the history
    // information. Returns true if the UpdateInfo should be deleted.
    bool ClearAdvertisedBits(RouteUpdate *rt_update, UpdateInfo *uinfo,
                             const RibPeerSet &bits);
    
    void StoreHistory(RouteUpdate *rt_update);
    void ClearState(RouteUpdate *rt_update);
    void ClearUpdate(RouteUpdatePtr *update);

    bool UpdateMarkersOnBlocked(UpdateMarker *marker, RouteUpdate *rt_update,
                                const RibPeerSet *blocked);

    RibOut *ribout_;
    MessageBuilder *builder_;
    QueueVec queue_vec_;
    boost::scoped_ptr<RibUpdateMonitor> monitor_;
    DISALLOW_COPY_AND_ASSIGN(RibOutUpdates);
};

#endif
