/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_bgp_update_h
#define ctrlplane_bgp_update_h

#include <list>

#include <boost/intrusive/list.hpp>
#include <boost/intrusive/slist.hpp>
#include <boost/intrusive/set.hpp>

#include <tbb/mutex.h>

#include "bgp/bgp_ribout.h"

class BgpRoute;
class RibUpdateMonitor;
class RouteUpdate;
class UpdateList;

//
// This is the base class for elements in the UpdatesByOrder list container
// in an UpdateQueue. Each element can either be an RouteUpdate (UPDATE) or
// an UpdateMarker (MARKER).
//
class UpdateEntry {
public:
    enum EntryType {
        UPDATE  = 1,
        MARKER
    };

    explicit UpdateEntry(EntryType type) : type_(type) { }

    bool IsMarker() { return (type_ == MARKER); }
    bool IsUpdate() { return (type_ == UPDATE); }

    // Intrusive DLL
    boost::intrusive::list_member_hook<> list_node;

private:
    EntryType type_;
    DISALLOW_COPY_AND_ASSIGN(UpdateEntry);
};

//
// This class represents an update marker in the UpdatesByOrder container
// in an UpdateQueue. It contains a set of bits corresponding to the peers
// that have not seen any updates after the marker.
//
struct UpdateMarker : public UpdateEntry {
    UpdateMarker() : UpdateEntry(MARKER) {
    }
    RibPeerSet members;
};

//
// This class represents a unique collection of attributes for a particular
// prefix that needs to be sent to a subset of peers.  It is essentially a
// combination of a RouteUpdate and a RibOutAttr that keeps track of the
// subset of peers by using a RibPeerSet.
//
// Since an UpdateInfo is relevant only in the context of a RouteUpdate, it
// is on a singly linked list container in the RouteUpdate and maintains a
// back pointer to the RouteUpdate.
//
// An UpdateInfo is also part of the set container in an UpdateQueue that
// is sorted by attribute, timestamp and prefix.
//
struct UpdateInfo {
    UpdateInfo() { }
    UpdateInfo(RibPeerSet target) : target(target) { }
    UpdateInfo(RibPeerSet target, RibOutAttr roattr)
        : roattr(roattr), target(target) {
    }

    void clear() {
        roattr.clear();
        target.clear();
    }

    void swap(UpdateInfo &rhs) {
        std::swap(roattr, rhs.roattr);
        std::swap(target, rhs.target);
    }

    // Intrusive slist node for RouteUpdate.
    boost::intrusive::slist_member_hook<> slist_node;

    // Node in UpdateQueue tree. Sorted by attribute, timestamp, rt prefix.
    boost::intrusive::set_member_hook<> update_node;

    // Update attributes.
    RibOutAttr roattr;

    // Update mask
    RibPeerSet target;

    // Backpointer to the RouteUpdate.
    RouteUpdate *update;

private:
    DISALLOW_COPY_AND_ASSIGN(UpdateInfo);
};

//
// Disposer for UpdateInfo.
//
struct UpdateInfoDisposer {
    void operator()(UpdateInfo *uinfo) { delete uinfo; }
};

#ifndef _LIBCPP_VERSION
namespace std {
template <>
inline void swap<UpdateInfo>(UpdateInfo &lhs, UpdateInfo &rhs) {
    swap(lhs.roattr, rhs.roattr);
    swap(lhs.target, rhs.target);
}
}
#endif

//
// Wrapper for intrusive slist of UpdateInfos. Destructor automatically
// deletes any elements still on the slist.
//
// TBD: create a class template.
//
class UpdateInfoSList {
public:

    typedef boost::intrusive::member_hook<
        UpdateInfo,
        boost::intrusive::slist_member_hook<>,
        &UpdateInfo::slist_node
    > Node;
    typedef boost::intrusive::slist<
        UpdateInfo,
        Node,
        boost::intrusive::linear<true>
    > List;

    UpdateInfoSList() { }
    ~UpdateInfoSList() { list_.clear_and_dispose(UpdateInfoDisposer()); }

    List *operator->() { return &list_; }
    const List *operator->() const { return &list_; }
    void swap(UpdateInfoSList &uinfo_slist) { list_.swap(uinfo_slist.list_); }

private:
    List list_;
};

//
// This class represents an UPDATE element in the FIFO of UpdateEntry that
// is maintained within an UpdateQueue.
//
// A RouteUpdate contains a back pointer to a Route (which corresponds to
// the prefix) and a list of UpdateInfo elements. Each element contains a
// unique set of attributes that need to be advertised to a subset of the
// peers.  In the typical case, the list of UpdateInfo elements contains
// only a single entry.  However, separating the prefix information from
// the attributes in this manner allows the implementation to be extended
// to support add-path or route reflection.
//
// A RouteUpdate is also derived from a DBState which means that it is
// part of the state map within a DBEntry. This allows a RouteUpdate to
// be associated with a DBEntry using the listener id of the RibOut as
// the index. In the steady state, the DBEntry maps the listener id for
// the RibOut to a RouteState which contains the history of what has
// already been advertised.  When a RouteUpdate is created this history
// gets transferred from the RouteState to the RouteUpdate. This allows
// the DbEntry to map a listener id to either a RouteState or RouteUpdate
// instead of maintaining references to both.
//
// Access to a RouteUpdate is controlled via a mutex.  A lock on it gives
// the owner the right to access or modify the contents of the RouteUpdate.
// However it does not give the right to manipulate the linkage of the
// RouteUpdate in the FIFO. That's controlled via a mutex in UpdateQueue.
//
class RouteUpdate : public DBState, public UpdateEntry {
public:

    enum Flag {
        F_LIST = 0,     // Entry is part of a list.
    };

    RouteUpdate(BgpRoute *route, int queue_id);
    ~RouteUpdate();

    void SetUpdateInfo(UpdateInfoSList &uinfo_slist);
    void BuildNegativeUpdateInfo(UpdateInfoSList &uinfo_slist) const;
    void ClearUpdateInfo();
    bool CompareUpdateInfo(const UpdateInfoSList &uinfo_slist) const;
    UpdateInfo *FindUpdateInfo(const RibOutAttr &roattr);
    const UpdateInfo *FindUpdateInfo(const RibOutAttr &roattr) const;
    void MergeUpdateInfo(UpdateInfoSList &uinfo_slist);
    bool RemoveUpdateInfo(UpdateInfo *uinfo);
    void ResetUpdateInfo(RibPeerSet &peerset);
    void TrimRedundantUpdateInfo(UpdateInfoSList &uinfo_slist) const;

    void SetHistory(AdvertiseSList &history);
    void ClearHistory();
    void SwapHistory(AdvertiseSList &history) { history_.swap(history); }
    void MoveHistory(RouteState *rstate);
    void UpdateHistory(RibOut *ribout, const RibOutAttr *roattr,
                       const RibPeerSet &bits);
    const AdvertiseInfo *FindHistory(const RibOutAttr &roattr) const;

    AdvertiseSList &History() { return history_; }
    const AdvertiseSList &History() const { return history_; }

    UpdateInfoSList &Updates() { return updates_; }
    const UpdateInfoSList &Updates() const { return updates_; }


    // Return true if there is an history entry with a non-null attribute.
    bool IsAdvertised() const;
    bool OnUpdateList () { return FlagIsSet(RouteUpdate::F_LIST); }

    UpdateList *MakeUpdateList();
    UpdateList *GetUpdateList(RibOut *ribout);
    void set_update_list() { FlagSet(RouteUpdate::F_LIST); }
    void clear_update_list() { FlagReset(RouteUpdate::F_LIST); }

    BgpRoute *route() { return route_; }

    int queue_id() const { return queue_id_; }
    void set_queue_id(int queue_id) { queue_id_ = queue_id; }

    uint64_t tstamp() const { return tstamp_; }
    void set_tstamp_now();

    bool empty() const { return updates_->empty(); }

private:
    friend class RibUpdateMonitor;

    bool FlagIsSet(Flag flag) const { return flags_ & (1 << flag); }
    void FlagSet(Flag flag) { flags_ |= (1 << flag); }
    void FlagReset(Flag flag) { flags_ &= ~(1 << flag); }

    tbb::mutex mutex_;
    BgpRoute *route_;
    int8_t queue_id_;
    int8_t flags_;
    AdvertiseSList history_;  // Update history
    UpdateInfoSList updates_;       // The state we want to advertise
    uint64_t tstamp_;
    DISALLOW_COPY_AND_ASSIGN(RouteUpdate);
};

//
// If multiple RouteUpdates are active across different queues they are
// stored as an UpdateList.  An UpdateList is created when there's more
// than 1 RouteUpdate and is deleted when the number goes back to 1. An
// UpdateList keeps a list of pointers to RouteUpdates.
//
// The main motivation for creating an UpdateList is to avoid impacting
// the order in which updates from the QUPDATE queue are sent out when
// new peer(s) join or existing peer(s) request a refresh.  Without the
// notion of an UpdateList, we would have to dequeue a RouteUpdate and
// enqueue it again at the end when handling a join/refresh.
//
// The scheduled UpdateInfos are maintained in individual RouteUpdates in
// each queue, while the history is maintained in the UpdateList.
//
class UpdateList : public DBState {
public:
    typedef std::list<RouteUpdate *> List;

    UpdateList() { }

    AdvertiseSList &History() { return history_; }
    const AdvertiseSList &History() const { return history_; }

    void SetHistory(AdvertiseSList &history);
    void SwapHistory(AdvertiseSList &history) { history_.swap(history); }
    void MoveHistory(RouteState *rstate);
    void MoveHistory(RouteUpdate *rt_update);
    const AdvertiseInfo *FindHistory(const RibOutAttr &roattr) const;

    List *GetList() { return &list_; }
    const List *GetList() const { return &list_; }

    void AddUpdate(RouteUpdate *rt_update);
    void RemoveUpdate(RouteUpdate *rt_update);
    RouteUpdate *FindUpdate(int queue_id);
    RouteUpdate *MakeRouteUpdate();

private:
    friend class RibUpdateMonitor;

    tbb::mutex mutex_;
    AdvertiseSList history_;  // Update history
    List list_;
    DISALLOW_COPY_AND_ASSIGN(UpdateList);
};

#endif
