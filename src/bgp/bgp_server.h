/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __BGP_SERVER_H__
#define __BGP_SERVER_H__

#include <tbb/spin_rw_mutex.h>

#include <boost/asio/ip/tcp.hpp>
#include <boost/dynamic_bitset.hpp>
#include <boost/scoped_ptr.hpp>

#include "bgp/bgp_common.h"
#include "db/db.h"
#include "io/tcp_server.h"
#include "io/tcp_session.h"
#include "net/address.h"

class AsPathDB;
class BgpAttrDB;
class BgpConditionListener;
class BgpConfigManager;
class BgpPeer;
class BgpSessionManager;
class CommunityDB;
class ExtCommunityDB;
class LifetimeActor;
class LifetimeManager;
class PeerRibMembershipManager;
class RoutePathReplicator;
class RoutingInstanceMgr;
class RTargetGroupMgr;
class SchedulingGroupManager;
class ServiceChainMgr;

class BgpServer {
public:
    typedef boost::function<void(as_t)> ASNUpdateCb;
    typedef boost::function<void(BgpPeer *)> VisitorFn;
    explicit BgpServer(EventManager *evm);
    virtual ~BgpServer();

    virtual std::string ToString() const;

    virtual bool IsPeerCloseGraceful();

    int RegisterPeer(BgpPeer *peer);
    void UnregisterPeer(BgpPeer *peer);
    BgpPeer *FindPeer(const std::string &name);

    void Shutdown();

    void VisitBgpPeers(BgpServer::VisitorFn) const;

    // accessors
    BgpSessionManager *session_manager() { return session_mgr_; }
    SchedulingGroupManager *scheduling_group_manager() {
        return sched_mgr_.get();
    }
    LifetimeManager *lifetime_manager() { return lifetime_manager_.get(); }
    BgpConfigManager *config_manager() { return config_mgr_.get(); }
    RoutingInstanceMgr *routing_instance_mgr() { return inst_mgr_.get(); }
    const RoutingInstanceMgr *routing_instance_mgr() const {
        return inst_mgr_.get();
    }
    RTargetGroupMgr *rtarget_group_mgr() { return rtarget_group_mgr_.get(); }
    BgpConditionListener *condition_listener() { 
        return condition_listener_.get();
    }
    ServiceChainMgr *service_chain_mgr() {
        return service_chain_mgr_.get();
    }
    RoutePathReplicator *replicator(Address::Family family) {
        if (family == Address::INETVPN)
            return inetvpn_replicator_.get();
        if (family == Address::EVPN)
            return evpn_replicator_.get();
        if (family == Address::ERMVPN)
            return ermvpn_replicator_.get();

        assert(false);
        return NULL;
    }

    PeerRibMembershipManager *membership_mgr() { return membership_mgr_.get(); }
    AsPathDB *aspath_db() { return aspath_db_.get(); }
    BgpAttrDB *attr_db() { return attr_db_.get(); }
    CommunityDB *comm_db() { return comm_db_.get(); }
    ExtCommunityDB *extcomm_db() { return extcomm_db_.get(); }

    bool IsDeleted() const;
    bool IsReadyForDeletion();
    void RetryDelete();

    DB *database() { return &db_; }
    const std::string &localname() const;
    as_t autonomous_system() const { return autonomous_system_; }
    uint32_t bgp_identifier() const { return bgp_identifier_.to_ulong(); };
    uint16_t hold_time() const { return hold_time_; }

    // Status
    uint32_t num_routing_instance() const;
    uint32_t num_bgp_peer() const {
        return (peer_bmap_.size() - peer_bmap_.count());
    }

    uint32_t get_output_queue_depth() const;

    void IncUpPeerCount() {
        num_up_peer_++;
    }
    void DecUpPeerCount() {
        assert(num_up_peer_);
        num_up_peer_--;
    }
    uint32_t NumUpPeer() const {
        return num_up_peer_;
    }
    LifetimeActor *deleter();
    boost::asio::io_service *ioservice();
    int RegisterASNUpdateCallback(ASNUpdateCb cb);
    void UnregisterASNUpdateCallback(int id);
    void NotifyASNUpdate(as_t old_asn);

private:
    class ConfigUpdater;
    class DeleteActor;
    friend class BgpServerTest;
    friend class BgpServerUnitTest;
    typedef std::map<std::string, BgpPeer *> BgpPeerList;
    typedef std::vector<ASNUpdateCb> ASNUpdateListenersList;

    void RoutingInstanceMgrDeletionComplete(RoutingInstanceMgr *mgr);

    // base config variables
    as_t autonomous_system_;
    tbb::spin_rw_mutex rw_mutex_;
    ASNUpdateListenersList asn_listeners_;
    boost::dynamic_bitset<> bmap_;      // free list.
    Ip4Address bgp_identifier_;
    uint16_t hold_time_;

    DB db_;
    boost::dynamic_bitset<> peer_bmap_;
    tbb::atomic<uint32_t> num_up_peer_;
    BgpPeerList peer_list_;

    boost::scoped_ptr<LifetimeManager> lifetime_manager_;
    boost::scoped_ptr<DeleteActor> deleter_;

    // databases
    boost::scoped_ptr<AsPathDB> aspath_db_;
    boost::scoped_ptr<CommunityDB> comm_db_;
    boost::scoped_ptr<ExtCommunityDB> extcomm_db_;
    boost::scoped_ptr<BgpAttrDB> attr_db_;

    // sessions and state managers
    BgpSessionManager *session_mgr_;
    boost::scoped_ptr<SchedulingGroupManager> sched_mgr_;
    boost::scoped_ptr<RoutingInstanceMgr> inst_mgr_;
    boost::scoped_ptr<RTargetGroupMgr> rtarget_group_mgr_;
    boost::scoped_ptr<PeerRibMembershipManager> membership_mgr_;
    boost::scoped_ptr<BgpConditionListener> condition_listener_;
    boost::scoped_ptr<RoutePathReplicator> inetvpn_replicator_;
    boost::scoped_ptr<RoutePathReplicator> ermvpn_replicator_;
    boost::scoped_ptr<RoutePathReplicator> evpn_replicator_;
    boost::scoped_ptr<ServiceChainMgr> service_chain_mgr_;

    // configuration
    boost::scoped_ptr<BgpConfigManager> config_mgr_;
    boost::scoped_ptr<ConfigUpdater> updater_;

    DISALLOW_COPY_AND_ASSIGN(BgpServer);
};

#endif // __BGP_SERVER_H__
