/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_controller_route_walker_hpp
#define vnsw_controller_route_walker_hpp

#include <oper/agent_route_walker.h>

/*
 * Handles all kind of walks issued in context ofr controller i.e. bgp peer.
 * This walker is existing per peer. Internally it uses agent_route_walker.
 * Kind of walks supported are:
 * 1) NOTIFYALL - Walks all VRF and corresponding route table  to notify to peer
 * 2) NOTIFYMULTICAST - Only sends multicast entries from all tables to peer.
 * 3) DELPEER - Deletes path received from this peer from all.
 * 4) STALE - Marks the path/info from this peer as stale and does not delete
 * it. Valid only for headless agent mode. In the case of unicast it marks peer
 * path as stale and in multicast it doesnt delete  the info sent by this peer.
 */
class ControllerRouteWalker : public AgentRouteWalker {
public:    
    enum Type {
        NOTIFYALL,
        NOTIFYMULTICAST,
        DELPEER,
        STALE,
    };
    ControllerRouteWalker(Agent *agent, Peer *peer);
    virtual ~ControllerRouteWalker() { }

    //Starts the walk- walk_done_cb is used to get callback when walk is over
    //i.e. all VRF and all corresponding route walks are over.
    void Start(Type type, bool associate, 
               AgentRouteWalker::WalkDone walk_done_cb);
    void Cancel();
    //Callback for identifying walk complete of all route tables for given vrf
    void RouteWalkDoneForVrf(VrfEntry *vrf);

    //Override vrf notification
    virtual bool VrfWalkNotify(DBTablePartBase *partition, DBEntryBase *e);
    //Override route notification
    virtual bool RouteWalkNotify(DBTablePartBase *partition, DBEntryBase *e);

private:
    //VRF notification handlers
    bool VrfNotifyInternal(DBTablePartBase *partition, DBEntryBase *e);
    bool VrfNotifyMulticast(DBTablePartBase *partition, DBEntryBase *e);
    bool VrfNotifyStale(DBTablePartBase *partition, DBEntryBase *e);
    bool VrfNotifyAll(DBTablePartBase *partition, DBEntryBase *e);
    bool VrfDelPeer(DBTablePartBase *partition, DBEntryBase *e);
    bool VrfStaleMarker(DBTablePartBase *partition, DBEntryBase *e);

    //Route notification handlers
    bool RouteNotifyInternal(DBTablePartBase *partition, DBEntryBase *e);
    bool RouteNotifyAll(DBTablePartBase *partition, DBEntryBase *e);
    bool RouteNotifyMulticast(DBTablePartBase *partition, DBEntryBase *e);
    bool RouteDelPeer(DBTablePartBase *partition, DBEntryBase *e);
    bool RouteStaleMarker(DBTablePartBase *partition, DBEntryBase *e);

    Peer *peer_;
    bool associate_;
    Type type_;
    DISALLOW_COPY_AND_ASSIGN(ControllerRouteWalker);
};

#endif
