/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_bgp_evpn_h
#define ctrlplane_bgp_evpn_h

#include <set>
#include <vector>

#include <boost/scoped_ptr.hpp>

#include "base/lifetime.h"
#include "bgp/bgp_attr.h"
#include "db/db_entry.h"
#include "net/address.h"

class DBTablePartition;
class DBTablePartBase;
class EvpnRoute;
class EvpnTable;
class EvpnManagerPartition;
class EvpnManager;
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

    virtual bool Update(EvpnRoute *route);
    virtual void TriggerUpdate() = 0;

    EvpnRoute *route() { return route_; }
    uint8_t type() const { return type_; }
    const BgpAttr *attr() const { return attr_.get(); }
    uint32_t label() const { return label_; }
    Ip4Address address() const { return address_; }
    bool edge_replication_not_supported() const {
        return edge_replication_not_supported_;
    }

protected:
    EvpnManagerPartition *partition_;
    EvpnRoute *route_;
    uint8_t type_;
    BgpAttrPtr attr_;
    uint32_t label_;
    Ip4Address address_;
    bool edge_replication_not_supported_;

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

    virtual bool Update(EvpnRoute *route);
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

    virtual bool Update(EvpnRoute *route);
    virtual void TriggerUpdate();

private:
    DISALLOW_COPY_AND_ASSIGN(EvpnRemoteMcastNode);
};

//
// This class represents a partition in the EvpnManager.  It is used to keep
// track of local and remote EvpnMcastNodes that belong to the partition. The
// partition is determined on the ethernet tag in the EvpnRoute.
//
class EvpnManagerPartition {
public:
    typedef std::set<EvpnMcastNode *> EvpnMcastNodeList;

    EvpnManagerPartition(EvpnManager *evpn_manager, size_t part_id);
    ~EvpnManagerPartition();

    DBTablePartition *GetTablePartition();
    void NotifyBroadcastMacRoutes();
    void AddMcastNode(EvpnMcastNode *node);
    void DeleteMcastNode(EvpnMcastNode *node);

    bool empty() const;
    const EvpnMcastNodeList &remote_mcast_node_list() const {
        return remote_mcast_node_list_;
    }
    const EvpnMcastNodeList &local_mcast_node_list() const {
        return local_mcast_node_list_;
    }
    BgpServer *server();

private:
    friend class BgpEvpnManagerTest;

    EvpnManager *evpn_manager_;
    size_t part_id_;
    EvpnMcastNodeList local_mcast_node_list_;
    EvpnMcastNodeList remote_mcast_node_list_;

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
// In future, it can be used to implement glue logic for EVPN multi-homing.
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
class EvpnManager {
public:
    EvpnManager(EvpnTable *table);
    virtual ~EvpnManager();

    virtual void Initialize();
    virtual void Terminate();

    virtual UpdateInfo *GetUpdateInfo(EvpnRoute *route);
    DBTablePartition *GetTablePartition(size_t part_id);
    BgpServer *server();

    void ManagedDelete();
    void Shutdown();
    bool MayDelete() const;
    void RetryDelete();

    LifetimeActor *deleter();

private:
    friend class BgpEvpnManagerTest;

    class DeleteActor;
    typedef std::vector<EvpnManagerPartition *> PartitionList;

    void AllocPartitions();
    void FreePartitions();
    void RouteListener(DBTablePartBase *tpart, DBEntryBase *db_entry);

    EvpnTable *table_;
    int listener_id_;
    PartitionList partitions_;

    boost::scoped_ptr<DeleteActor> deleter_;
    LifetimeRef<EvpnManager> table_delete_ref_;

    DISALLOW_COPY_AND_ASSIGN(EvpnManager);
};

#endif
