/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_BGP_BGP_UPDATE_QUEUE_H_
#define SRC_BGP_BGP_UPDATE_QUEUE_H_

#include <stdint.h>

#include <list>
#include <set>
#include <vector>

#include "bgp/bgp_update.h"

//
// Comparator used to order UpdateInfos in the UpdateQueue set container.
// Looks at the BgpAttr, Timestamp and the associated RouteUpdate but not
// the Label, in order to achieve optimal packing of BGP updates. Ignore
// the BgpAttr in case of XMPP since each item is self contained - there's
// no notion of common attributes for all items in an update message.
//
// Compare the UpdateInfo pointers themselves as the final tie-breaker to
// handle the case where there are 2 UpdateInfos with the same BgpAttr in
// the same RouteUpdate. This can happen if the label (or the set for ecmp
// nexthops in case of an XMPP ribout) for a route changes between the Join
// operations for 2 different sets of IPeerUpdates.
//
struct UpdateByAttrCmp {
    bool operator()(const UpdateInfo &lhs, const UpdateInfo &rhs) const {
        if (lhs.roattr.is_xmpp()) {
            if (lhs.roattr.IsReachable() < rhs.roattr.IsReachable()) {
                return true;
            }
            if (lhs.roattr.IsReachable() > rhs.roattr.IsReachable()) {
                return false;
            }
        } else {
            if (lhs.roattr.attr() < rhs.roattr.attr()) {
                return true;
            }
            if (lhs.roattr.attr() > rhs.roattr.attr()) {
                return false;
            }
        }
        if (lhs.update->tstamp() < rhs.update->tstamp())  {
            return true;
        }
        if (lhs.update->tstamp() > rhs.update->tstamp()) {
            return false;
        }
        if (lhs.update < rhs.update)  {
            return true;
        }
        if (lhs.update > rhs.update) {
            return false;
        }
        return (&lhs < &rhs);
    }
};

//
// This class implements an update queue for a RibOut.  A RibUpdateMonitor
// contains a vector of pointers to UpdateQueue. The UpdateQueue instances
// are created and the corresponding pointers added to the vector from the
// RibOutUpdates constructor.  The following relationships are relevant:
//
//     RibOut -> RibOutUpdates (1:N)
//     RibOutUpdates -> RibUpdateMonitor (1:1)
//     RibUpdateMonitor -> UpdateQueue (1:N)
//
// Each UpdateQueue has an id which indicates whether it's a BULK queue or
// an UPDATE queue.  A BULK queue is used for route refresh and also when
// a new peer comes up.
//
// An UpdateQueue contains an intrusive doubly linked list of UpdateEntry
// base elements, which could be either be UpdateMarker or RouteUpdate.
// This list is maintained in temporal order i.e. it's a FIFO.
//
// An UpdateQueue also has a intrusive set of UpdateInfo elements.  These
// are ordered by attribute, timestamp and the corresponding prefix. This
// container is used when building updates since it allows us to traverse
// prefixes grouped by attributes. The relationship between a RouteUpdate
// and UpdateInfo is described elsewhere.
//
// An UpdateQueue also maintains a mapping from a peer's bit position to
// it's UpdateMarker. Note that it's possible for multiple peers to point
// to the same marker.
//
// A special UpdateMarker called the tail marker is used as an easy way to
// keep track of whether the list is empty.  The tail marker is always the
// last marker in the list.  Another way to think about this is that all
// the RouteUpdates after the tail marker have not been seen by any peer.
// This property is maintained by making sure that when the update marker
// for a peer (or set of peers) reaches the tail marker, it's merged into
// the tail marker.
//
class UpdateQueue {
public:
    // Typedefs for the intrusive list.
    typedef boost::intrusive::member_hook<
        UpdateEntry,
        boost::intrusive::list_member_hook<>,
        &UpdateEntry::list_node
    > UpdateEntryNode;

    typedef boost::intrusive::list<UpdateEntry, UpdateEntryNode> UpdatesByOrder;

    // Typedefs for the intrusive set.
    typedef boost::intrusive::member_hook<UpdateInfo,
        boost::intrusive::set_member_hook<>,
        &UpdateInfo::update_node
    > UpdateSetNode;

    typedef boost::intrusive::set<UpdateInfo, UpdateSetNode,
        boost::intrusive::compare<UpdateByAttrCmp>
    > UpdatesByAttr;

    typedef std::vector<UpdateMarker *> MarkerList;

    UpdateQueue(const RibOut *ribout, int queue_id);
    ~UpdateQueue();

    bool Enqueue(RouteUpdate *rt_update);
    void Dequeue(RouteUpdate *rt_update);

    RouteUpdate *NextUpdate(UpdateEntry *upentry);
    UpdateEntry *NextEntry(UpdateEntry *upentry);

    void AttrDequeue(UpdateInfo *current_uinfo);
    UpdateInfo *AttrNext(UpdateInfo *current_uinfo);

    void AddMarker(UpdateMarker *marker, RouteUpdate *rt_update);
    void MoveMarker(UpdateMarker *marker, RouteUpdate *rt_update);
    void MarkerSplit(UpdateMarker *marker, const RibPeerSet &msplit);
    void MarkerMerge(UpdateMarker *dst_marker, UpdateMarker *src_marker,
                     const RibPeerSet &bitset);
    UpdateMarker *GetMarker(size_t bit);

    bool Join(size_t bit);
    void Leave(size_t bit);

    bool CheckInvariants() const;

    UpdateMarker *tail_marker() { return &tail_marker_; }

    bool empty() const;
    size_t size() const;
    size_t marker_count() const;

private:
    friend class BgpExportTest;
    friend class RibOutUpdatesTest;

    int queue_id_;
    bool encoding_is_xmpp_;
    uint64_t tstamp_;
    size_t marker_count_;
    UpdatesByOrder queue_;
    UpdatesByAttr attr_set_;
    MarkerList markers_;
    UpdateMarker tail_marker_;

    DISALLOW_COPY_AND_ASSIGN(UpdateQueue);
};

#endif  // SRC_BGP_BGP_UPDATE_QUEUE_H_
