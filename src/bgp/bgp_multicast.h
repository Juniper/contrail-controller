/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_bgp_multicast_h
#define ctrlplane_bgp_multicast_h

#include <set>
#include <vector>

#include <boost/scoped_ptr.hpp>

#include "base/label_block.h"
#include "base/lifetime.h"
#include "base/queue_task.h"
#include "bgp/bgp_attr.h"
#include "db/db_entry.h"
#include "net/address.h"
#include "net/rd.h"

class DBEntryBase;
class DBTablePartBase;
class InetMcastRoute;
class InetMcastTable;
class McastForwarder;
class McastManagerPartition;
class McastTreeManager;
struct UpdateInfo;

typedef std::vector<McastForwarder *> McastForwarderList;

//
// This class represents membership of an end system in a (G,S) within a
// inet multicast table.  An end system is an entity that participates in
// edge replicated multicast and is typically a physical server but could
// also be some kind of gateway device that connects the distribution tree
// to other more traditional multicast distribution trees.
//
// There's a 1:1 correspondence between a route in an InetMcastTable and
// a McastForwarder. Routes in the InetMcastTable are keyed by RD, Group
// and Source. When these routes are processed, we invert the information
// and create a McastSGEntry for each unique (G,S).  Information from the
// RD and the IPeer from which we learnt the route is used to construct a
// McastForwarder. The McastForwarder is part of a set in the McastSGEntry
// for the (G,S).
//
// A McastForwarder is created when the DBListener for the McastTreeManager
// sees a new route in the InetMcastTable. It's deleted when the DBListener
// detects that the route in question has been marked for deletion.
//
// The McastForwarder is associated with the InetMcastRoute by setting it
// to be the DBState for the McastTreeManager's listener id.  Conversely,
// the McastForwarder maintains a back pointer to the InetMcastRoute. The
// LabelBlockPtr is copied from the BgpAttr of the route's active path.
// It's used to allocate labels for the McastForwarder without needing to
// reach into the route.  More importantly, it's required to release the
// currently allocated label when the route gets marked for deletion, at
// which time there's no active path for the route.
//
// A McastForwarder contains a vector of pointers to other McastForwarders
// within the same McastSGEntry. The collection of these links constitutes
// the distribution tree for the McastSGEntry. Note that only a single MPLS
// label is used for a McastForwarder in a given distribution tree.  Hence
// the label can be stored in the McastForwarder itself and does not need
// to be part of the link information.
//
class McastForwarder : public DBState {
public:
    McastForwarder(InetMcastRoute *route);
    ~McastForwarder();

    bool Update(InetMcastRoute *route);
    std::string ToString() const;

    McastForwarder *FindLink(McastForwarder *forwarder);
    void AddLink(McastForwarder *forwarder);
    void RemoveLink(McastForwarder *forwarder);
    void FlushLinks();

    void AllocateLabel();
    void ReleaseLabel();

    UpdateInfo *GetUpdateInfo(InetMcastTable *table);

    uint32_t label() const { return label_; }
    Ip4Address address() const { return address_; }
    std::vector<std::string> encap() const { return encap_; }
    InetMcastRoute *route() { return route_; }
    RouteDistinguisher route_distinguisher() const { return rd_; }

    bool empty() { return tree_links_.empty(); }

private:
    friend class BgpMulticastTest;
    friend class ShowMulticastManagerDetailHandler;

    InetMcastRoute *route_;
    LabelBlockPtr label_block_;
    uint32_t label_;
    RouteDistinguisher rd_;
    Ip4Address address_;
    std::vector<std::string> encap_;
    McastForwarderList tree_links_;

    DISALLOW_COPY_AND_ASSIGN(McastForwarder);
};

//
// Key comparison class for McastForwarder.
//
struct McastForwarderCompare {
    bool operator()(const McastForwarder *lhs,
                    const McastForwarder *rhs) const {
        int cmp =
            lhs->route_distinguisher().CompareTo(rhs->route_distinguisher());
        if (cmp < 0)
            return true;
        if (cmp > 0)
            return false;

        return false;
    }
};

//
// This class represents a (G,S) entry within a McastManagerPartition. The
// routes in the InetMcastTable are keyed by the RD, Group and Source. When
// these routes are processed, we rearrange the information and create a
// McastSGEntry for each unique (G,S). The RD in the InetMcastTable route
// represents the McastForwarder that sent the join/leave.
//
// A McastSGEntry is part of a set in the McastManagerPartition. In addition
// is may also temporarily be on the WorkQueue in the McastManagerPartition
// if the distribution tree needs to be updated.
//
// A McastSGEntry is created when the DBListener for the InetMcastTable sees
// the first InetMcastRoute containing the (G,S) and hence needs to create a
// McastForwarder. It can be destroyed when all McastForwarders under it are
// gone. The actual delete is done from the McastManagerPartition's WorkQueue
// callback routine to ensure that there are no stale references to it on the
// WorkQueue. Note that the WorkQueue cannot contain more than one reference
// to a given McastSGEntry.
//
// A set of pointers to McastForwarders is maintatined to keep track of the
// forwarders that have sent joins for this (G,S).  The set is keyed by the
// RouteDistinguisher of the McastForwarders. It's used to build distribution
// tree for this McastSGEntry. The McastSGEntry is enqueued on the WorkQueue
// in the McastManagerPartition when a McastForwarder is added or deleted,
// so that the distribution tree gets updtaed.
//
class McastSGEntry {
public:
    McastSGEntry(McastManagerPartition *partition,
                 Ip4Address group, Ip4Address source);
    ~McastSGEntry();

    std::string ToString() const;

    void AddForwarder(McastForwarder *forwarder);
    void DeleteForwarder(McastForwarder *forwarder);

    void UpdateTree();

    Ip4Address group() const { return group_; }
    Ip4Address source() const { return source_; }

    bool on_work_queue() { return on_work_queue_; }
    void set_on_work_queue() { on_work_queue_ = true; }
    void clear_on_work_queue() { on_work_queue_ = false; }

    bool empty() { return forwarders_.empty(); }

private:
    friend class BgpMulticastTest;
    friend class ShowMulticastManagerDetailHandler;

    typedef std::set<McastForwarder *, McastForwarderCompare> ForwarderSet;

    McastManagerPartition *partition_;
    Ip4Address group_, source_;
    bool on_work_queue_;
    ForwarderSet forwarders_;

    DISALLOW_COPY_AND_ASSIGN(McastSGEntry);
};

//
// Key comparison class for McastSGEntry.
//
struct McastSGEntryCompare {
    bool operator()(const McastSGEntry *lhs, const McastSGEntry *rhs) const {
        if (lhs->group().to_ulong() < rhs->group().to_ulong()) {
            return true;
        }
        if (lhs->group().to_ulong() > rhs->group().to_ulong()) {
            return false;
        }
        if (lhs->source().to_ulong() < rhs->source().to_ulong()) {
            return true;
        }
        if (lhs->source().to_ulong() > rhs->source().to_ulong()) {
            return false;
        }
        return false;
    }
};

//
// This class represents a partition in the McastTreeManager. It is used to
// maintain membership information for a subset of the (G,S) entries in the
// associated InetMcastTable, as well as to calculate and store distribution
// trees for each of those (G,S) entries.
//
// The partition for a (G,S) is determined by a hash function. Consequently,
// information about all McastForwarders that have sent joins for a (G,S) is
// always under a single partition.
//
// A McastManagerPartition keeps a set of pointers to McastSGEntrys for the
// (G,S) states that fall under the parition. The set is keyed by the group
// and source addresses.
//
// A WorkQueue of pointers to McastSGEntrys is used to keep track of entries
// that need their distribution tree to be updated. The use of the WorkQueue
// also allows us to combine multiple McastForwarder join/leave events into
// a smaller number of updates to the distribution tree.
//
// All McastManagerPartitions all allocated when the McastTreeManager gets
// initialized and are freed when the McastTreeManager is terminated.
//
class McastManagerPartition {
public:
    McastManagerPartition(McastTreeManager *tree_manager, size_t part_id);
    ~McastManagerPartition();

    McastSGEntry *FindSGEntry(Ip4Address group, Ip4Address source);
    McastSGEntry *LocateSGEntry(Ip4Address group, Ip4Address source);
    void EnqueueSGEntry(McastSGEntry *sg_entry);

    DBTablePartBase *GetTablePartition();

    bool empty() { return sg_list_.empty(); }
    size_t size() { return sg_list_.size(); }

private:
    friend class BgpMulticastTest;
    friend class ShowMulticastManagerDetailHandler;

    typedef std::set<McastSGEntry *, McastSGEntryCompare> SGList;

    bool ProcessSGEntry(McastSGEntry *sg_entry);

    McastTreeManager *tree_manager_;
    size_t part_id_;
    SGList sg_list_;
    int update_count_;
    WorkQueue<McastSGEntry *> work_queue_;

    DISALLOW_COPY_AND_ASSIGN(McastManagerPartition);
};

//
// This class represents the multicast tree manager for an InetMcastTable.
//
// It is responsible for listening to route notifications on the associated
// InetMcastTable and building distribution trees for edge replicated mcast.
// A unique distribution tree is constructed for every (G,S) in the table.
//
// It also provides the InetMcastTable class with an API to get the UpdateInfo
// for a route in the InetMcastTable. This is utilized by the table's Export
// method to construct the RibOutAttr for the multicast routes.  This is how
// we send the label and OList information for a (G,S) to the XMPP peers.
//
// A McastTableManager keeps a vector of pointers to McastManagerPartitions.
// The number of paritions is the same as the DB partition count. Each such
// partition contains a subset of (G,S) entries learnt from the route table.
// The concurrency model is that each McastManagerPartition can be updated
// with membership information and can build distribution trees independently
// of the other partitions.
//
// There's a 1:1 relationship between the McastTreeManager a InetMcastTable
// with the McastTreeManager being a dependent of the InetMcastTable via the
// LifetimeManager infrastructure. The McastTreeManager is created when the
// InetMcastTable gets associated with a RoutingInstance. A McastTreeManager
// can be destroyed when all McastManagerPartitions are empty i.e. when all
// McastSGEntrys in the partition have been cleaned up.  The actual deletion
// happens via the LifetimeManager infrastructure.
//
class McastTreeManager {
public:
    static const int kDegree = 4;

    McastTreeManager(InetMcastTable *table);
    virtual ~McastTreeManager();

    virtual void Initialize();
    virtual void Terminate();

    McastManagerPartition *GetPartition(int part_id);

    virtual UpdateInfo *GetUpdateInfo(InetMcastRoute *route);
    DBTablePartBase *GetTablePartition(size_t part_id);

    void ManagedDelete();
    void Shutdown();
    bool MayDelete() const;
    void MayResumeDelete();

    LifetimeActor *deleter();

private:
    friend class BgpMulticastTest;
    friend class ShowMulticastManagerDetailHandler;

    class DeleteActor;
    typedef std::vector<McastManagerPartition *> PartitionList;

    void AllocPartitions();
    void FreePartitions();
    void RouteListener(DBTablePartBase *tpart, DBEntryBase *db_entry);

    InetMcastTable *table_;
    int listener_id_;
    PartitionList partitions_;

    boost::scoped_ptr<DeleteActor> deleter_;
    LifetimeRef<McastTreeManager> table_delete_ref_;

    DISALLOW_COPY_AND_ASSIGN(McastTreeManager);
};

#endif
