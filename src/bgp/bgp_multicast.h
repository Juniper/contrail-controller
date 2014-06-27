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
class ErmVpnRoute;
class ErmVpnTable;
class McastForwarder;
class McastManagerPartition;
class McastSGEntry;
class McastTreeManager;
class RoutingInstance;
struct UpdateInfo;

typedef std::vector<McastForwarder *> McastForwarderList;

//
// This class represents membership of a vRouter or a control-node in a (G,S)
// within a multicast table.
//
// A vRouter is considered a member if it has advertised a route for (G,S) via
// XMPP. The level_ field in McastForwarder is set to LevelNative in this case.
//
// A control-node is considered to be a member if it has advertised (via BGP)
// an ErmVpnRoute of type LocalRoute for (G,S). The level_ field gets set to
// LevelLocal in this case.

// There's a 1:1 correspondence between an ErmVpnRoute of type NativeRoute or
// LocalRoute and a McastForwarder. Routes in the ErmVpnTable are keyed by RD,
// RouterId, Group and Source. When these routes are processed, we invert the
// information and create a McastSGEntry for each unique (G,S).  Information
// from the RD and the IPeer from which we learnt the route is used to create
// a McastForwarder. The McastForwarder is part of a set in the McastSGEntry
// for the (G,S).
//
// A McastForwarder gets created when the DBListener for the McastTreeManager
// sees a new ErmVpnRoute of type NativeRoute or LocalRoute. It's deleted when
// the DBListener detects that the route in question has been deleted.
//
// A McastForwarder is associated with the ErmVpnRoute by setting it to be the
// DBState for the McastTreeManager's listener id. The McastForwarder keeps a
// back pointer to the ErmVpnRoute.
//
// The LabelBlockPtr is obtained from the attributes of the best path for the
// ErmVpnRoute.  It's used to allocate labels for the McastForwarder without
// needing to reach into the route.  More importantly, it's required to release
// the currently allocated label when the route gets marked for deletion, at
// which time there's no active path for the route.
//
// A McastForwarder contains a vector of pointers to other McastForwarders at
// the same NodeLevel within the McastSGEntry.  The collection of these links
// constitutes the distribution tree for the McastSGEntry at that NodeLevel.
// Note that only a single MPLS label is used for a McastForwarder in a given
// distribution tree. Thus the label can be stored in the McastForwarder itself
// and does not need to be part of the link information.
//
// If this control-node is elected as the tree builder for the (G,S), a global
// distribution tree of all Local McastForwarders is built.  Relevant edges of
// this global distribution tree are advertised to each control-node by adding
// a GlobalTreeRoute for each Local McastForwarder. The global_tree_route_ is
// used to keep track of this ErmVpnRoute and [Add|Delete]GlobalTreeRoute are
// used to add or delete the route.
//
class McastForwarder : public DBState {
public:
    McastForwarder(McastSGEntry *sg_entry, ErmVpnRoute *route);
    ~McastForwarder();

    bool Update(ErmVpnRoute *route);
    std::string ToString() const;
    uint8_t GetLevel() const;

    McastForwarder *FindLink(McastForwarder *forwarder);
    void AddLink(McastForwarder *forwarder);
    void RemoveLink(McastForwarder *forwarder);
    void FlushLinks();

    void AllocateLabel();
    void ReleaseLabel();

    void AddGlobalTreeRoute();
    void DeleteGlobalTreeRoute();
    UpdateInfo *GetUpdateInfo(ErmVpnTable *table);

    uint8_t level() const { return level_; }
    uint32_t label() const { return label_; }
    Ip4Address address() const { return address_; }
    std::vector<std::string> encap() const { return encap_; }
    ErmVpnRoute *route() { return route_; }
    RouteDistinguisher route_distinguisher() const { return rd_; }
    Ip4Address router_id() const { return router_id_; }

    bool empty() { return tree_links_.empty(); }

private:
    friend class BgpMulticastTest;
    friend class ShowMulticastManagerDetailHandler;

    void AddLocalOListElems(BgpOListPtr olist);
    void AddGlobalOListElems(BgpOListPtr olist);

    McastSGEntry *sg_entry_;
    ErmVpnRoute *route_;
    ErmVpnRoute *global_tree_route_;
    uint8_t level_;
    LabelBlockPtr label_block_;
    uint32_t label_;
    Ip4Address address_;
    RouteDistinguisher rd_;
    Ip4Address router_id_;
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
        if (lhs->route_distinguisher() < rhs->route_distinguisher())
            return true;
        if (lhs->route_distinguisher() > rhs->route_distinguisher())
            return false;
        if (lhs->router_id() < rhs->router_id())
            return true;
        if (lhs->router_id() > rhs->router_id())
            return false;

        return false;
    }
};

//
// This class represents a (G,S) entry within a McastManagerPartition. The
// routes in the ErmVpnTable are keyed by RD, RouterId, Group and Source.
// When these routes are processed, we rearrange the information and create a
// McastSGEntry for each unique (G,S). The RD and RouterId in the ErmVpnRoute
// represent a McastForwarder.
//
// A McastSGEntry is part of a set in the McastManagerPartition. In addition
// is may also temporarily be on the WorkQueue in the McastManagerPartition
// if the distribution tree needs to be updated.
//
// A McastSGEntry is created when the DBListener for the ErmVpnTable sees the
// first ErmVpnRoute containing the (G,S) and needs to create a McastForwarder.
// It is destroyed when all McastForwarders under it are gone. The delete is
// done from the McastManagerPartition's WorkQueue callback routine to ensure
// that there are no stale references to it on the WorkQueue.  Note that the
// WorkQueue cannot contain more than one reference to a given McastSGEntry.
//
// Two sets of pointers to McastForwarders are maintained in a McastSGEntry -
// for Native and Local tree levels. The McastForwarders at the Native level
// correspond to vRouters that have subscribed to the (G,S). McastForwarders
// at the Local level correspond to control-nodes (including this one) that
// are advertising their local subtree's candidate edges via a LocalTreeRoute.
// The sets are keyed by the RD and RouterId of the McastForwarders.
//
// A local distribution tree of all Native McastForwarders is built and the
// last leaf in the tree is designated as the forest node.  The forest_node_
// is used to keep track of this McastForwarder.  A LocalTreeRoute is added to
// the ErmVpnTable and the forest node's candidate edges are advertised using
// the EdgeDiscovery attribute. The local_tree_route_ keeps track of the route.
//
// If this control-node is elected to be the tree builder for this (G,S), a
// global distribution tree of all Local McastForwarders is built.  Relevant
// edges of this global distribution tree are advertised to each control-node
// by adding a GlobalTreeRoute for each Local McastForwarder.  The forwarding
// edges are encoded using the EdgeForwarding attribute.
//
// Whether the tree builder is this control-node or another control-node, the
// tree_result_route_ member keeps track of the GlobalTreeRoute that contains
// the forwarding edges relevant to this control-node.  The RouterId in the
// GlobalTreeRoute is used to decide if it's for this control-node.  We set
// our DBState on this ErmVpnRoute to be the McastSGEntry.
//
// The McastSGEntry is enqueued on the WorkQueue in the McastManagerPartition
// when a McastForwarder is added, changed or deleted so that the distribution
// tree and the necessary LocalTreeRoute or GlobalTreeRoutes can be updated.
//
class McastSGEntry : public DBState {
public:
    McastSGEntry(McastManagerPartition *partition,
                 Ip4Address group, Ip4Address source);
    ~McastSGEntry();

    std::string ToString() const;

    void AddForwarder(McastForwarder *forwarder);
    void ChangeForwarder(McastForwarder *forwarder);
    void DeleteForwarder(McastForwarder *forwarder);

    RouteDistinguisher GetSourceRd();
    void AddLocalTreeRoute();
    void DeleteLocalTreeRoute();
    void UpdateLocalTreeRoute();
    void UpdateTree();
    void NotifyForestNode();
    bool IsForestNode(McastForwarder *forwarder);

    Ip4Address group() const { return group_; }
    Ip4Address source() const { return source_; }
    McastManagerPartition *partition() { return partition_; }
    const ErmVpnRoute *tree_result_route() const { return tree_result_route_; }
    void set_tree_result_route(ErmVpnRoute *route) { tree_result_route_ = route; }
    void clear_tree_result_route() { tree_result_route_ = NULL; }

    bool on_work_queue() { return on_work_queue_; }
    void set_on_work_queue() { on_work_queue_ = true; }
    void clear_on_work_queue() { on_work_queue_ = false; }

    bool empty() const;

private:
    friend class BgpMulticastTest;
    friend class ShowMulticastManagerDetailHandler;

    typedef std::set<McastForwarder *, McastForwarderCompare> ForwarderSet;

    bool IsTreeBuilder(uint8_t level);
    void UpdateTree(uint8_t level);
    void UpdateRoutes(uint8_t level);

    McastManagerPartition *partition_;
    Ip4Address group_, source_;
    McastForwarder *forest_node_;
    ErmVpnRoute *local_tree_route_;
    ErmVpnRoute *tree_result_route_;
    std::vector<ForwarderSet *> forwarder_sets_;
    std::vector<bool> update_needed_;
    bool on_work_queue_;

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
// associated ErmVpnTable and to calculate and store distribution trees for
// each of those (G,S) entries.
//
// The partition for a (G,S) is determined by a hash function. Consequently,
// information about all McastForwarders that have sent joins for a (G,S) is
// always under a single partition.
//
// A McastManagerPartition keeps a set of pointers to McastSGEntrys for the
// (G,S) states that fall under the partition. The set is keyed by the group
// and source addresses.
//
// A WorkQueue of pointers to McastSGEntrys is used to keep track of entries
// that need their distribution tree to be updated. The use of the WorkQueue
// also allows us to combine multiple McastForwarder join/leave events into
// a smaller number of updates to the distribution tree.
//
// All McastManagerPartitions are allocated when the McastTreeManager gets
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
    const RoutingInstance *routing_instance() const;
    BgpServer *server();
    McastTreeManager *tree_manager() const { return tree_manager_; }

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
// This class represents the multicast tree manager for an ErmVpnTable.
//
// It is responsible for listening to route notifications on the associated
// ErmVpnTable and building distribution trees for edge replicated multicast.
// A local distribution tree consisting of all the vRouters that subscribed
// to this control-node is built for each (G,S).  Further, if this control
// node is elected to be the tree builder for a (G,S) a global distribution
// is also built.  The global tree is built by selecting edges from the set
// of candidate edges advertised by each control-node.
//
// It also provides the ErmVpnTable class with an API to get the UpdateInfo
// for a route in the ErmVpnTable. This is used by the table's Export method
// to construct the RibOutAttr for the multicast routes. This is how we send
// the label and OList information for a (G,S) to the XMPP peers.
//
// A McastTableManager keeps a vector of pointers to McastManagerPartitions.
// The number of partitions is the same as the DB partition count. Each such
// partition contains a subset of (G,S) entries learnt from the route table.
// The concurrency model is that each McastManagerPartition can be updated
// with membership information and can build distribution trees independently
// of the other partitions.
//
// There's a 1:1 relationship between the McastTreeManager a ErmVpnTable with
// the McastTreeManager being dependent of the ErmVpnTable via LifetimeManager
// infrastructure. The McastTreeManager is created when the ErmVpnTable gets
// associated with a RoutingInstance. A McastTreeManager can be destroyed when
// all McastManagerPartitions are empty i.e. when all McastSGEntrys in all the
// partition have been cleaned up. Actual deletion happens via LifetimeManager
// infrastructure.
//
// Note that we do not create a McastTreeManager for the ErmVpnTable in the
// default routing instance i.e. bgp.ermvpn.0.
//
class McastTreeManager {
public:
    static const int kDegree = 4;

    enum NodeLevel {
        LevelFirst = 0,
        LevelNative = 0,
        LevelLocal = 1,
        LevelCount = 2,
    };

    McastTreeManager(ErmVpnTable *table);
    virtual ~McastTreeManager();

    virtual void Initialize();
    virtual void Terminate();

    McastManagerPartition *GetPartition(int part_id);

    virtual UpdateInfo *GetUpdateInfo(ErmVpnRoute *route);
    DBTablePartBase *GetTablePartition(size_t part_id);
    ErmVpnTable *table() { return table_; }

    void ManagedDelete();
    void Shutdown();
    bool MayDelete() const;
    void RetryDelete();

    LifetimeActor *deleter();

private:
    friend class BgpMulticastTest;
    friend class ShowMulticastManagerDetailHandler;

    class DeleteActor;
    typedef std::vector<McastManagerPartition *> PartitionList;

    void AllocPartitions();
    void FreePartitions();
    void TreeNodeListener(McastManagerPartition *partition,
        ErmVpnRoute *route);
    void TreeResultListener(McastManagerPartition *partition,
        ErmVpnRoute *route);
    void RouteListener(DBTablePartBase *tpart, DBEntryBase *db_entry);

    ErmVpnTable *table_;
    int listener_id_;
    PartitionList partitions_;

    boost::scoped_ptr<DeleteActor> deleter_;
    LifetimeRef<McastTreeManager> table_delete_ref_;

    DISALLOW_COPY_AND_ASSIGN(McastTreeManager);
};

#endif
