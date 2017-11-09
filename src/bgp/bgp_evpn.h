/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_BGP_BGP_EVPN_H_
#define SRC_BGP_BGP_EVPN_H_

#include <boost/ptr_container/ptr_map.hpp>
#include <boost/scoped_ptr.hpp>
#include <tbb/spin_rw_mutex.h>

#include <list>
#include <set>
#include <vector>

#include "base/lifetime.h"
#include "base/task_trigger.h"
#include "bgp/bgp_attr.h"
#include "db/db_entry.h"
#include "net/address.h"

class BgpPath;
class DBTablePartition;
class DBTablePartBase;
class EvpnRoute;
class EvpnTable;
class EvpnManagerPartition;
class EvpnManager;
class ShowEvpnTable;
struct UpdateInfo;

//
// This is the base class for a multicast node in an EVPN instance. The node
// could either represent a local vRouter that's connected to a control node
// via XMPP or a remote vRouter/PE that's discovered via BGP.
//
// In normal operation, the EvpnMcastNodes corresponding to vRouters (local
// or remote) support edge replication, while those corresponding to EVPN PEs
// do not.  However, for test purposes, it's possible to have vRouters that
// no not support edge replication.
//
class EvpnMcastNode : public DBState {
public:
    enum Type {
        LocalNode,
        RemoteNode
    };

    EvpnMcastNode(EvpnManagerPartition *partition, EvpnRoute *route,
        uint8_t type);
    ~EvpnMcastNode();

    bool UpdateAttributes(EvpnRoute *route);
    virtual void TriggerUpdate() = 0;

    EvpnRoute *route() { return route_; }
    uint8_t type() const { return type_; }
    const BgpAttr *attr() const { return attr_.get(); }
    uint32_t label() const { return label_; }
    Ip4Address address() const { return address_; }
    Ip4Address replicator_address() const { return replicator_address_; }
    bool edge_replication_not_supported() const {
        return edge_replication_not_supported_;
    }
    bool assisted_replication_supported() const {
        return assisted_replication_supported_;
    }
    bool assisted_replication_leaf() const {
        return assisted_replication_leaf_;
    }

protected:
    EvpnManagerPartition *partition_;
    EvpnRoute *route_;
    uint8_t type_;
    BgpAttrPtr attr_;
    uint32_t label_;
    Ip4Address address_;
    Ip4Address replicator_address_;
    bool edge_replication_not_supported_;
    bool assisted_replication_supported_;
    bool assisted_replication_leaf_;

private:
    DISALLOW_COPY_AND_ASSIGN(EvpnMcastNode);
};

//
// This class represents (in the context of an EVPN instance) a local vRouter
// that's connected to a control node via XMPP.  An EvpnLocalMcastNode gets
// created and associated as DBState with the broadcast MAC route advertised
// by the vRouter.
//
// The EvpnLocalMcastNode mainly exists to translate the broadcast MAC route
// advertised by the vRouter into an EVPN Inclusive Multicast route.  In the
// other direction, EvpnLocalMcastNode serves as the anchor point to build a
// vRouter specific ingress replication olist so that the vRouter can send
// multicast traffic to EVPN PEs (and possibly vRouters in test environment)
// that do not support edge replication.
//
// An Inclusive Multicast route is added for each EvpnLocalMcastNode. The
// attributes of the Inclusive Multicast route are based on the broadcast
// MAC route corresponding to the EvpnLocalMcastNode.  The label for the
// broadcast MAC route is advertised as the label for ingress replication
// in the PmsiTunnel attribute.
//
class EvpnLocalMcastNode : public EvpnMcastNode {
public:
    EvpnLocalMcastNode(EvpnManagerPartition *partition, EvpnRoute *route);
    virtual ~EvpnLocalMcastNode();

    virtual void TriggerUpdate();
    UpdateInfo *GetUpdateInfo();
    EvpnRoute *inclusive_mcast_route() { return inclusive_mcast_route_; }

private:
    void AddInclusiveMulticastRoute();
    void DeleteInclusiveMulticastRoute();

    EvpnRoute *inclusive_mcast_route_;

    DISALLOW_COPY_AND_ASSIGN(EvpnLocalMcastNode);
};

//
// This class represents (in the context of an EVPN instance) a remote vRouter
// or PE discovered via BGP.  An EvpnRemoteMcastNode is created and associated
// as DBState with the Inclusive Multicast route in question.
//
// An EvpnRemoteMcastNode also gets created for the Inclusive Multicast route
// that's added for each EvpnLocalMcastNode. This is required only to support
// test mode where vRouter acts like PE that doesn't support edge replication.
//
class EvpnRemoteMcastNode : public EvpnMcastNode {
public:
    EvpnRemoteMcastNode(EvpnManagerPartition *partition, EvpnRoute *route);
    virtual ~EvpnRemoteMcastNode();

    virtual void TriggerUpdate();

private:
    DISALLOW_COPY_AND_ASSIGN(EvpnRemoteMcastNode);
};

//
// This class represents a remote EVPN segment that has 2 or more PEs that are
// multi-homed to it. An EvpnSegment is created when we see a MAC route with a
// non-NULL ESI or when we see an AD route for the ESI in question.
//
// An EvpnSegment contains a vector of lists of MAC routes that are dependent
// on it. There's a list entry in the vector for each DB partition.  All the
// MAC routes in a given partition that are associated with the EvpnSegment are
// in inserted in the list for that partition. The lists are updated as and
// when the EvpnSegment for MAC routes is updated.
//
// An EvpnSegment contains a list of Remote PEs that have advertised per-ESI
// AD routes for the EVPN segment in question. The list is updated when paths
// are added/deleted from the AD route. A change in the contents of the list
// triggers an update of all dependent MAC routes, so that their aliased paths
// can be updated. The single-active state of the EvpnSegment is also updated
// when the PE list is updated. The PE list is updated from the context of the
// bgp::EvpnSegment task.
//
class EvpnSegment : public DBState {
public:
    EvpnSegment(EvpnManager *evpn_manager, const EthernetSegmentId &esi);
    ~EvpnSegment();

    class RemotePe {
    public:
        RemotePe(bool single_active, const IpAddress &address)
          : esi_valid(true),
            single_active(single_active),
            address(address) {
        }

        bool esi_valid;
        bool single_active;
        IpAddress address;
    };

    typedef std::list<RemotePe> RemotePeList;
    typedef RemotePeList::const_iterator const_iterator;

    const_iterator begin() const { return pe_list_.begin(); }
    const_iterator end() const { return pe_list_.end(); }

    void AddMacRoute(size_t part_id, EvpnRoute *route);
    void DeleteMacRoute(size_t part_id, EvpnRoute *route);
    void TriggerMacRouteUpdate();
    bool UpdatePeList();
    bool MayDelete() const;

    const EthernetSegmentId &esi() const { return esi_; }
    EvpnRoute *esi_ad_route() { return esi_ad_route_; }
    void set_esi_ad_route(EvpnRoute *esi_ad_route) {
        esi_ad_route_ = esi_ad_route;
    }
    void clear_esi_ad_route() { esi_ad_route_ = NULL; }
    bool single_active() const { return single_active_; }

private:
    typedef std::set<EvpnRoute *> RouteList;
    typedef std::vector<RouteList> RouteListVector;

    EvpnManager *evpn_manager_;
    EthernetSegmentId esi_;
    EvpnRoute *esi_ad_route_;
    bool single_active_;
    RouteListVector route_lists_;
    RemotePeList pe_list_;

    DISALLOW_COPY_AND_ASSIGN(EvpnSegment);
};

//
// This class represents the EvpnManager state associated with a MAC route.
//
// In the steady state, a EvpnMacState should exist only for MAC routes that
// have a non-zero ESI. The segment_ field is a pointer to the EvpnSegment for
// the ESI in question. A EvpnMacState is created from the route listener when
// we see a MAC route with a non-zero ESI. It is deleted after processing the
// MAC route if the route has been deleted or if it has a zero ESI.
//
// The AliasedPathList keeps track of the aliased BgpPaths that we've added.
// An aliased BgpPath is added for each remote PE for all-active EvpnSegment.
// The contents of the AliasedPathList are updated when the ESI for the MAC
// route changes or when the list of remote PEs for the EvpnSegment changes.
//
class EvpnMacState : public DBState {
public:
    EvpnMacState(EvpnManager *evpn_manager, EvpnRoute *route);
    ~EvpnMacState();

    bool ProcessMacRouteAliasing();

    EvpnSegment *segment() { return segment_; }
    const EvpnSegment *segment() const { return segment_; }
    void set_segment(EvpnSegment *segment) { segment_ = segment; }
    void clear_segment() { segment_ = NULL; }

private:
    typedef std::set<BgpPath *> AliasedPathList;

    void AddAliasedPath(AliasedPathList::const_iterator it);
    void DeleteAliasedPath(AliasedPathList::const_iterator it);
    BgpPath *LocateAliasedPath(const BgpPath *orig_path, const BgpAttr *attr,
        uint32_t label);

    EvpnManager *evpn_manager_;
    EvpnRoute *route_;
    EvpnSegment *segment_;
    AliasedPathList aliased_path_list_;

    DISALLOW_COPY_AND_ASSIGN(EvpnMacState);
};

//
// This class represents a partition in the EvpnManager.
//
// It is used to keep track of local and remote EvpnMcastNodes that belong to
// the partition. The partition is determined on the ethernet tag in the IM
// route.
//
// An EvpnManagerPartition contains a set of MAC routes whose alias paths need
// to be updated. Entries are added to the list using the TriggerMacRouteUpdate
// method.
//
class EvpnManagerPartition {
public:
    typedef std::set<EvpnMcastNode *> EvpnMcastNodeList;

    EvpnManagerPartition(EvpnManager *evpn_manager, size_t part_id);
    ~EvpnManagerPartition();

    DBTablePartition *GetTablePartition();
    void NotifyNodeRoute(EvpnMcastNode *node);
    void NotifyReplicatorNodeRoutes();
    void NotifyIrClientNodeRoutes(bool exclude_edge_replication_supported);
    void AddMcastNode(EvpnMcastNode *node);
    void DeleteMcastNode(EvpnMcastNode *node);
    void UpdateMcastNode(EvpnMcastNode *node);
    void TriggerMacRouteUpdate(EvpnRoute *route);

    bool empty() const;
    const EvpnMcastNodeList &remote_mcast_node_list() const {
        return remote_mcast_node_list_;
    }
    const EvpnMcastNodeList &local_mcast_node_list() const {
        return local_mcast_node_list_;
    }
    const EvpnMcastNodeList &leaf_node_list() const {
        return leaf_node_list_;
    }
    BgpServer *server();
    const EvpnTable *table() const;
    size_t part_id() const { return part_id_; }

private:
    friend class EvpnManager;
    friend class BgpEvpnManagerTest;

    typedef std::set<EvpnRoute *> EvpnRouteList;

    bool ProcessMacUpdateList();
    void DisableMacUpdateProcessing();
    void EnableMacUpdateProcessing();

    EvpnManager *evpn_manager_;
    size_t part_id_;
    DBTablePartition *table_partition_;
    EvpnMcastNodeList local_mcast_node_list_;
    EvpnMcastNodeList remote_mcast_node_list_;
    EvpnMcastNodeList replicator_node_list_;
    EvpnMcastNodeList leaf_node_list_;
    EvpnMcastNodeList regular_node_list_;
    EvpnMcastNodeList ir_client_node_list_;
    EvpnRouteList mac_update_list_;
    boost::scoped_ptr<TaskTrigger> mac_update_trigger_;

    DISALLOW_COPY_AND_ASSIGN(EvpnManagerPartition);
};

//
// This class represents the EVPN manager for an EvpnTable in a VRF.
//
// It is responsible for listening to route notifications on the associated
// EvpnTable and implementing glue logic to massage routes so that vRouters
// can communicate properly with EVPN PEs.
//
// It currently implements glue logic for multicast connectivity between the
// vRouters and EVPN PEs.  This is achieved by keeping track of local/remote
// EvpnMcastNodes and constructing ingress replication OList for any given
// EvpnLocalMcastNode when requested.
//
// It also provides the EvpnTable class with an API to get the UpdateInfo for
// a route in EvpnTable.  This is used by the table's Export method to build
// the RibOutAttr for the broadcast MAC routes.  This is how we send ingress
// replication OList information for an EVPN instance to the XMPP peers.
//
// An EvpnManager keeps a vector of pointers to EvpnManagerPartitions.  The
// number of partitions is the same as the DB partition count.  A partition
// contains a subset of EvpnMcastNodes that are created based on EvpnRoutes
// in the EvpnTable.
//
// The concurrency model is that each EvpnManagerPartition can be updated and
// can build the ingress replication OLists independently of other partitions.
//
// The EvpnManager is also used to implement glue logic for EVPN aliasing when
// remote PEs have multi-homed segments.
//
// An EvpnManager maintains a map of EvpnSegments keyed by EthernetSegmentId.
// It also keeps sets of EvpnSegments that need to be updated or evaluated for
// deletion.
//
// An EvpnSegment gets added to the segment update list in the EvpnManager when
// there's a change in the AD route for the EvpnSegment. The update list gets
// processed in the context of the bgp::EvpnSegment task.
//
// An EvpnSegment gets added to the segment delete list in the EvpnManager when
// the PE list becomes empty (bgp::EvpnSegment task) or when the MAC route list
// for a given partition becomes empty (db::DBTable task).  The actual call to
// MayDelete and subsequent destroy, if appropriate, happens in in the context
// of bgp::EvpnSegment task.
//
// The bgp::EvpnSegment task is mutually exclusive with the db::DBTable task.
//
class EvpnManager {
public:
    explicit EvpnManager(EvpnTable *table);
    virtual ~EvpnManager();

    virtual void Initialize();
    virtual void Terminate();

    virtual UpdateInfo *GetUpdateInfo(EvpnRoute *route);
    EvpnManagerPartition *GetPartition(size_t part_id);
    DBTablePartition *GetTablePartition(size_t part_id);
    void FillShowInfo(ShowEvpnTable *sevt) const;
    BgpServer *server();
    EvpnTable *table() { return table_; }
    const EvpnTable *table() const { return table_; }
    int listener_id() const { return listener_id_; }

    EvpnSegment *LocateSegment(const EthernetSegmentId &esi);
    EvpnSegment *FindSegment(const EthernetSegmentId &esi);
    void TriggerSegmentDelete(EvpnSegment *segment);
    void TriggerSegmentUpdate(EvpnSegment *segment);

    void ManagedDelete();
    void Shutdown();
    bool MayDelete() const;
    void RetryDelete();

    LifetimeActor *deleter();

private:
    friend class BgpEvpnManagerTest;
    friend class BgpEvpnAliasingTest;

    class DeleteActor;
    typedef std::vector<EvpnManagerPartition *> PartitionList;
    typedef boost::ptr_map<const EthernetSegmentId, EvpnSegment> SegmentMap;
    typedef std::set<EvpnSegment *> SegmentSet;

    void AllocPartitions();
    void FreePartitions();
    void AutoDiscoveryRouteListener(EvpnRoute *route);
    void MacAdvertisementRouteListener(EvpnManagerPartition *partition,
        EvpnRoute *route);
    void InclusiveMulticastRouteListener(EvpnManagerPartition *partition,
        EvpnRoute *route);
    void RouteListener(DBTablePartBase *tpart, DBEntryBase *db_entry);
    bool ProcessSegmentDeleteSet();
    bool ProcessSegmentUpdateSet();

    void DisableSegmentUpdateProcessing();
    void EnableSegmentUpdateProcessing();
    void DisableSegmentDeleteProcessing();
    void EnableSegmentDeleteProcessing();
    void DisableMacUpdateProcessing();
    void EnableMacUpdateProcessing();

    EvpnTable *table_;
    int listener_id_;
    PartitionList partitions_;
    tbb::spin_rw_mutex segment_rw_mutex_;
    SegmentMap segment_map_;
    SegmentSet segment_delete_set_;
    SegmentSet segment_update_set_;
    boost::scoped_ptr<TaskTrigger> segment_delete_trigger_;
    boost::scoped_ptr<TaskTrigger> segment_update_trigger_;

    boost::scoped_ptr<DeleteActor> deleter_;
    LifetimeRef<EvpnManager> table_delete_ref_;

    DISALLOW_COPY_AND_ASSIGN(EvpnManager);
};

#endif  // SRC_BGP_BGP_EVPN_H_
