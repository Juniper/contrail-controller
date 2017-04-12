/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_BGP_BGP_SERVER_H_
#define SRC_BGP_BGP_SERVER_H_

#include <tbb/spin_rw_mutex.h>
#include <boost/asio/ip/tcp.hpp>
#include <boost/dynamic_bitset.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/bind.hpp>
#include "io/event_manager.h"

#include <map>
#include <set>
#include <string>
#include <vector>

#include "bgp/bgp_common.h"
#include "db/db.h"
#include "io/tcp_server.h"
#include "io/tcp_session.h"
#include "net/address.h"

class AsPathDB;
class BgpAttrDB;
class BgpConditionListener;
class BgpConfigManager;
class BgpGlobalSystemConfig;
class BgpGlobalQosConfig;
class BgpMembershipManager;
class BgpOListDB;
class BgpPeer;
class BgpRouterState;
class BgpSessionManager;
class BgpUpdateSender;
class ClusterListDB;
class CommunityDB;
class EdgeDiscoveryDB;
class EdgeForwardingDB;
class ExtCommunityDB;
class IServiceChainMgr;
class IStaticRouteMgr;
class LifetimeActor;
class LifetimeManager;
class OriginVnPathDB;
class PmsiTunnelDB;
class RoutePathReplicator;
class RoutingInstanceMgr;
class RoutingPolicyMgr;
class RTargetGroupMgr;
class XmppServer;

class BgpServer {
public:
    static const int kEndOfRibTime = 30; // seconds
    typedef boost::function<void()> AdminDownCb;
    typedef boost::function<void(uint8_t)> DSCPUpdateCb;
    typedef boost::function<void(as_t, as_t)> ASNUpdateCb;
    typedef boost::function<void(Ip4Address)> IdentifierUpdateCb;
    typedef boost::function<void(BgpPeer *)> VisitorFn;
    typedef std::set<IStaticRouteMgr *> StaticRouteMgrList;

    explicit BgpServer(EventManager *evm);
    virtual ~BgpServer();

    static void Initialize();
    static void Terminate();

    virtual std::string ToString() const;

    uint16_t GetGracefulRestartTime() const;
    uint32_t GetLongLivedGracefulRestartTime() const;
    uint32_t GetEndOfRibReceiveTime() const;
    uint32_t GetEndOfRibSendTime() const;

    int RegisterPeer(BgpPeer *peer);
    void UnregisterPeer(BgpPeer *peer);
    BgpPeer *FindPeer(const std::string &name);
    BgpPeer *FindNextPeer(const std::string &name = std::string());
    void InsertPeer(TcpSession::Endpoint remote, BgpPeer *peer);
    void RemovePeer(TcpSession::Endpoint remote, BgpPeer *peer);
    virtual BgpPeer *FindPeer(TcpSession::Endpoint remote) const;
    BgpPeer *FindExactPeer(const BgpPeer *peer) const;

    void Shutdown();

    void VisitBgpPeers(BgpServer::VisitorFn) const;

    // accessors
    BgpSessionManager *session_manager() { return session_mgr_; }
    BgpUpdateSender *update_sender() { return update_sender_.get(); }
    LifetimeManager *lifetime_manager() { return lifetime_manager_.get(); }
    BgpConfigManager *config_manager() { return config_mgr_.get(); }
    const BgpConfigManager *config_manager() const { return config_mgr_.get(); }
    RoutingInstanceMgr *routing_instance_mgr() { return inst_mgr_.get(); }
    const RoutingInstanceMgr *routing_instance_mgr() const {
        return inst_mgr_.get();
    }
    RoutingPolicyMgr *routing_policy_mgr() { return policy_mgr_.get(); }
    const RoutingPolicyMgr *routing_policy_mgr() const {
        return policy_mgr_.get();
    }
    RTargetGroupMgr *rtarget_group_mgr() { return rtarget_group_mgr_.get(); }
    const RTargetGroupMgr *rtarget_group_mgr() const {
        return rtarget_group_mgr_.get();
    }
    BgpConditionListener *condition_listener(Address::Family family) {
        if (family == Address::INET)
            return inet_condition_listener_.get();
        if (family == Address::INET6)
            return inet6_condition_listener_.get();
        assert(false);
        return NULL;
    }
    IServiceChainMgr *service_chain_mgr(Address::Family family) {
        if (family == Address::INET)
            return inet_service_chain_mgr_.get();
        if (family == Address::INET6)
            return inet6_service_chain_mgr_.get();
        assert(false);
        return NULL;
    }
    RoutePathReplicator *replicator(Address::Family family) {
        if (family == Address::INETVPN)
            return inetvpn_replicator_.get();
        if (family == Address::EVPN)
            return evpn_replicator_.get();
        if (family == Address::ERMVPN)
            return ermvpn_replicator_.get();
        if (family == Address::INET6VPN)
            return inet6vpn_replicator_.get();
        return NULL;
    }
    const RoutePathReplicator *replicator(Address::Family family) const {
        if (family == Address::INETVPN)
            return inetvpn_replicator_.get();
        if (family == Address::EVPN)
            return evpn_replicator_.get();
        if (family == Address::ERMVPN)
            return ermvpn_replicator_.get();
        if (family == Address::INET6VPN)
            return inet6vpn_replicator_.get();
        return NULL;
    }

    BgpMembershipManager *membership_mgr() { return membership_mgr_.get(); }
    const BgpMembershipManager *membership_mgr() const {
        return membership_mgr_.get();
    }

    AsPathDB *aspath_db() { return aspath_db_.get(); }
    BgpAttrDB *attr_db() { return attr_db_.get(); }
    BgpOListDB *olist_db() { return olist_db_.get(); }
    ClusterListDB *cluster_list_db() { return cluster_list_db_.get(); }
    CommunityDB *comm_db() { return comm_db_.get(); }
    EdgeDiscoveryDB *edge_discovery_db() { return edge_discovery_db_.get(); }
    EdgeForwardingDB *edge_forwarding_db() { return edge_forwarding_db_.get(); }
    ExtCommunityDB *extcomm_db() { return extcomm_db_.get(); }
    OriginVnPathDB *ovnpath_db() { return ovnpath_db_.get(); }
    PmsiTunnelDB *pmsi_tunnel_db() { return pmsi_tunnel_db_.get(); }

    bool IsDeleted() const;
    bool IsReadyForDeletion();
    void RetryDelete();
    bool logging_disabled() const { return logging_disabled_; }
    void set_logging_disabled(bool flag) { logging_disabled_ = flag; }

    bool destroyed() const { return destroyed_; }
    void set_destroyed() { destroyed_  = true; }

    DB *database() { return &db_; }
    const std::string &localname() const;
    bool admin_down() const { return admin_down_; }
    as_t autonomous_system() const { return autonomous_system_; }
    as_t local_autonomous_system() const { return local_autonomous_system_; }
    uint32_t bgp_identifier() const { return bgp_identifier_.to_ulong(); }
    std::string bgp_identifier_string() const {
        return bgp_identifier_.to_string();
    }
    uint32_t hold_time() const { return hold_time_; }
    bool HasSelfConfiguration() const;

    // Status
    uint32_t num_routing_instance() const;
    uint32_t num_deleted_routing_instance() const;

    uint32_t num_bgp_peer() const { return bgp_count_; }
    uint32_t num_deleting_bgp_peer() const { return deleting_count_; }
    void increment_deleting_count() { deleting_count_++; }
    void decrement_deleting_count() {
        assert(deleting_count_ > 0);
        deleting_count_--;
    }

    uint32_t num_bgpaas_peer() const { return bgpaas_count_; }
    uint32_t num_deleting_bgpaas_peer() const { return deleting_bgpaas_count_; }
    void increment_deleting_bgpaas_count() { deleting_bgpaas_count_++; }
    void decrement_deleting_bgpaas_count() {
        assert(deleting_bgpaas_count_ > 0);
        deleting_bgpaas_count_--;
    }

    uint32_t get_output_queue_depth() const;

    uint32_t num_service_chains() const;
    uint32_t num_down_service_chains() const;
    uint32_t num_static_routes() const;
    uint32_t num_down_static_routes() const;

    void IncrementUpPeerCount() {
        num_up_peer_++;
    }
    void DecrementUpPeerCount() {
        assert(num_up_peer_);
        num_up_peer_--;
    }
    uint32_t NumUpPeer() const {
        return num_up_peer_;
    }
    void IncrementUpBgpaasPeerCount() {
        num_up_bgpaas_peer_++;
    }
    void DecrementUpBgpaasPeerCount() {
        assert(num_up_bgpaas_peer_);
        num_up_bgpaas_peer_--;
    }
    uint32_t NumUpBgpaasPeer() const {
        return num_up_bgpaas_peer_;
    }

    LifetimeActor *deleter();
    boost::asio::io_service *ioservice();

    void increment_message_build_error() const { ++message_build_error_; }
    uint64_t message_build_error() const { return message_build_error_; }

    int RegisterAdminDownCallback(AdminDownCb callback);
    void UnregisterAdminDownCallback(int listener);
    void NotifyAdminDown();
    int RegisterASNUpdateCallback(ASNUpdateCb callback);
    void UnregisterASNUpdateCallback(int listener);
    void NotifyASNUpdate(as_t old_asn, as_t old_local_asn);
    int RegisterIdentifierUpdateCallback(IdentifierUpdateCb callback);
    void UnregisterIdentifierUpdateCallback(int listener);
    void NotifyIdentifierUpdate(Ip4Address old_identifier);
    int RegisterDSCPUpdateCallback(DSCPUpdateCb cb);
    void UnregisterDSCPUpdateCallback(int listener);
    void NotifyDSCPUpdate(int new_dscp_value);

    void InsertStaticRouteMgr(IStaticRouteMgr *srt_manager);
    void RemoveStaticRouteMgr(IStaticRouteMgr *srt_manager);
    void NotifyAllStaticRoutes();
    uint32_t GetStaticRouteCount() const;
    uint32_t GetDownStaticRouteCount() const;
    BgpGlobalSystemConfig *global_config() { return global_config_.get(); }
    BgpGlobalQosConfig *global_qos() { return global_qos_.get(); }
    bool gr_helper_disable() const { return gr_helper_disable_; }
    void set_gr_helper_disable(bool gr_helper_disable) {
        gr_helper_disable_ = gr_helper_disable;
    }
    bool IsGRHelperModeEnabled() const;
    bool CollectStats(BgpRouterState *state, bool first) const;

private:
    class ConfigUpdater;
    class DeleteActor;
    friend class BgpServerTest;
    friend class BgpServerUnitTest;
    typedef std::map<std::string, BgpPeer *> BgpPeerList;
    typedef std::vector<AdminDownCb> AdminDownListenersList;
    typedef std::vector<ASNUpdateCb> ASNUpdateListenersList;
    typedef std::vector<IdentifierUpdateCb> IdentifierUpdateListenersList;
    typedef std::vector<DSCPUpdateCb> DSCPUpdateListenersList;
    typedef std::multimap<TcpSession::Endpoint, BgpPeer *> EndpointPeerList;

    void RoutingInstanceMgrDeletionComplete(RoutingInstanceMgr *mgr);
    void FillPeerStats(const BgpPeer *peer) const;

    // base config variables
    tbb::spin_rw_mutex rw_mutex_;
    bool admin_down_;
    AdminDownListenersList admin_down_listeners_;
    boost::dynamic_bitset<> admin_down_bmap_;  // free list.
    as_t autonomous_system_;
    as_t local_autonomous_system_;
    ASNUpdateListenersList asn_listeners_;
    boost::dynamic_bitset<> asn_bmap_;     // free list.
    Ip4Address bgp_identifier_;
    IdentifierUpdateListenersList id_listeners_;
    boost::dynamic_bitset<> id_bmap_;      // free list.
    DSCPUpdateListenersList dscp_listeners_;
    boost::dynamic_bitset<> dscp_bmap_;  // free list.
    uint32_t hold_time_;
    bool gr_helper_disable_;
    StaticRouteMgrList srt_manager_list_;

    DB db_;
    boost::dynamic_bitset<> peer_bmap_;
    tbb::atomic<uint32_t> bgp_count_;
    tbb::atomic<uint32_t> num_up_peer_;
    tbb::atomic<uint32_t> deleting_count_;
    tbb::atomic<uint32_t> bgpaas_count_;
    tbb::atomic<uint32_t> num_up_bgpaas_peer_;
    tbb::atomic<uint32_t> deleting_bgpaas_count_;
    BgpPeerList peer_list_;
    EndpointPeerList endpoint_peer_list_;

    boost::scoped_ptr<LifetimeManager> lifetime_manager_;
    boost::scoped_ptr<DeleteActor> deleter_;
    bool destroyed_;
    bool logging_disabled_;

    // databases
    boost::scoped_ptr<AsPathDB> aspath_db_;
    boost::scoped_ptr<BgpOListDB> olist_db_;
    boost::scoped_ptr<ClusterListDB> cluster_list_db_;
    boost::scoped_ptr<CommunityDB> comm_db_;
    boost::scoped_ptr<EdgeDiscoveryDB> edge_discovery_db_;
    boost::scoped_ptr<EdgeForwardingDB> edge_forwarding_db_;
    boost::scoped_ptr<ExtCommunityDB> extcomm_db_;
    boost::scoped_ptr<OriginVnPathDB> ovnpath_db_;
    boost::scoped_ptr<PmsiTunnelDB> pmsi_tunnel_db_;
    boost::scoped_ptr<BgpAttrDB> attr_db_;

    // sessions and state managers
    BgpSessionManager *session_mgr_;
    boost::scoped_ptr<BgpUpdateSender> update_sender_;
    boost::scoped_ptr<RoutingInstanceMgr> inst_mgr_;
    boost::scoped_ptr<RoutingPolicyMgr> policy_mgr_;
    boost::scoped_ptr<RTargetGroupMgr> rtarget_group_mgr_;
    boost::scoped_ptr<BgpMembershipManager> membership_mgr_;
    boost::scoped_ptr<BgpConditionListener> inet_condition_listener_;
    boost::scoped_ptr<BgpConditionListener> inet6_condition_listener_;
    boost::scoped_ptr<RoutePathReplicator> inetvpn_replicator_;
    boost::scoped_ptr<RoutePathReplicator> ermvpn_replicator_;
    boost::scoped_ptr<RoutePathReplicator> evpn_replicator_;
    boost::scoped_ptr<RoutePathReplicator> inet6vpn_replicator_;
    boost::scoped_ptr<IServiceChainMgr> inet_service_chain_mgr_;
    boost::scoped_ptr<IServiceChainMgr> inet6_service_chain_mgr_;

    // configuration
    boost::scoped_ptr<BgpGlobalSystemConfig> global_config_;
    boost::scoped_ptr<BgpGlobalQosConfig> global_qos_;
    boost::scoped_ptr<BgpConfigManager> config_mgr_;
    boost::scoped_ptr<ConfigUpdater> updater_;

    mutable tbb::atomic<uint64_t> message_build_error_;

    DISALLOW_COPY_AND_ASSIGN(BgpServer);
};

#endif  // SRC_BGP_BGP_SERVER_H_
