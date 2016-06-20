/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/bgp_server.h"

#include <boost/tuple/tuple.hpp>

#include "base/connection_info.h"
#include "base/task_annotations.h"
#include "bgp/bgp_condition_listener.h"
#include "bgp/bgp_factory.h"
#include "bgp/bgp_lifetime.h"
#include "bgp/bgp_log.h"
#include "bgp/bgp_membership.h"
#include "bgp/bgp_peer.h"
#include "bgp/bgp_session_manager.h"
#include "bgp/bgp_table_types.h"
#include "bgp/peer_stats.h"
#include "bgp/scheduling_group.h"
#include "bgp/routing-instance/iservice_chain_mgr.h"
#include "bgp/routing-instance/istatic_route_mgr.h"
#include "bgp/routing-instance/peer_manager.h"
#include "bgp/routing-instance/routepath_replicator.h"
#include "bgp/routing-instance/routing_instance.h"
#include "bgp/routing-instance/rtarget_group_mgr.h"
#include "bgp/routing-policy/routing_policy.h"

#include "sandesh/sandesh.h"
#include "control-node/sandesh/control_node_types.h"

using boost::system::error_code;
using boost::tie;
using process::ConnectionState;
using std::boolalpha;
using std::make_pair;
using std::noboolalpha;
using std::string;

// The ConfigUpdater serves as glue between the BgpConfigManager and the
// BgpServer.
class BgpServer::ConfigUpdater {
public:
    explicit ConfigUpdater(BgpServer *server) : server_(server) {
        BgpConfigManager::Observers obs;
        obs.instance = boost::bind(&ConfigUpdater::ProcessInstanceConfig,
            this, _1, _2);
        obs.protocol = boost::bind(&ConfigUpdater::ProcessProtocolConfig,
            this, _1, _2);
        obs.neighbor = boost::bind(&ConfigUpdater::ProcessNeighborConfig,
            this, _1, _2);
        obs.policy = boost::bind(&ConfigUpdater::ProcessRoutingPolicyConfig,
            this, _1, _2);
        obs.system= boost::bind(&ConfigUpdater::ProcessGlobalSystemConfig,
            this, _1, _2);
        server->config_manager()->RegisterObservers(obs);
    }

    void ProcessGlobalSystemConfig(const BgpGlobalSystemConfig *system,
            BgpConfigManager::EventType event) {
        server_->global_config()->set_gr_time(system->gr_time());
        server_->global_config()->set_llgr_time(system->llgr_time());
        server_->global_config()->set_eor_rx_time(system->eor_rx_time());

        RoutingInstanceMgr *ri_mgr = server_->routing_instance_mgr();
        RoutingInstance *rti =
            ri_mgr->GetRoutingInstance(BgpConfigManager::kMasterInstance);
        assert(rti);
        PeerManager *peer_manager = rti->peer_manager();
        peer_manager->ClearAllPeers();
    }

    void ProcessProtocolConfig(const BgpProtocolConfig *protocol_config,
                               BgpConfigManager::EventType event) {
        const string &instance_name = protocol_config->instance_name();

        // We only support BGP sessions in master instance for now.
        if (instance_name != BgpConfigManager::kMasterInstance) {
            return;
        }

        if (event == BgpConfigManager::CFG_ADD ||
            event == BgpConfigManager::CFG_CHANGE) {
            BgpServerConfigUpdate(instance_name, protocol_config);
        } else if (event == BgpConfigManager::CFG_DELETE) {
            BgpServerConfigUpdate(instance_name, NULL);
        } else {
            assert(false);
        }
    }

    void BgpServerConfigUpdate(string instance_name,
                               const BgpProtocolConfig *config) {
        boost::system::error_code ec;
        uint32_t config_identifier = 0;
        uint32_t config_autonomous_system = 0;
        uint32_t config_local_autonomous_system = 0;
        uint32_t config_hold_time = 0;
        bool config_admin_down = false;
        if (config) {
            config_admin_down = config->admin_down();
            config_identifier = config->identifier();
            config_autonomous_system = config->autonomous_system();
            config_local_autonomous_system = config->local_autonomous_system();
            config_hold_time = config->hold_time();
        }

        if (server_->admin_down_ != config_admin_down) {
            SandeshLevel::type log_level;
            if (server_->admin_down_) {
                log_level = SandeshLevel::SYS_DEBUG;
            } else {
                log_level = SandeshLevel::SYS_NOTICE;
            }
            BGP_LOG_STR(BgpConfig, log_level, BGP_LOG_FLAG_SYSLOG,
                        "Updated Admin Down from " <<
                        boolalpha << server_->admin_down_ << noboolalpha <<
                        " to " <<
                        boolalpha << config_admin_down << noboolalpha);
            server_->admin_down_ = config_admin_down;
            if (server_->admin_down_)
                server_->NotifyAdminDown();
        }

        Ip4Address identifier(ntohl(config_identifier));
        if (server_->bgp_identifier_ != identifier) {
            SandeshLevel::type log_level;
            if (!server_->bgp_identifier_.is_unspecified()) {
                log_level = SandeshLevel::SYS_NOTICE;
            } else {
                log_level = SandeshLevel::SYS_DEBUG;
            }
            BGP_LOG_STR(BgpConfig, log_level, BGP_LOG_FLAG_SYSLOG,
                        "Updated Router ID from " <<
                        server_->bgp_identifier_.to_string() << " to " <<
                        identifier.to_string());
            Ip4Address old_identifier = server_->bgp_identifier_;
            server_->bgp_identifier_ = identifier;
            server_->NotifyIdentifierUpdate(old_identifier);
        }

        bool notify_asn_update = false;
        uint32_t old_asn = server_->autonomous_system_;
        uint32_t old_local_asn = server_->local_autonomous_system_;
        server_->autonomous_system_ = config_autonomous_system;
        if (config_local_autonomous_system) {
            server_->local_autonomous_system_ = config_local_autonomous_system;
        } else {
            server_->local_autonomous_system_ = config_autonomous_system;
        }

        if (server_->autonomous_system_ != old_asn) {
            SandeshLevel::type log_level;
            if (old_asn != 0) {
                log_level = SandeshLevel::SYS_NOTICE;
            } else {
                log_level = SandeshLevel::SYS_DEBUG;
            }
            BGP_LOG_STR(BgpConfig, log_level, BGP_LOG_FLAG_SYSLOG,
                        "Updated Autonomous System from " << old_asn <<
                        " to " << server_->autonomous_system_);
            notify_asn_update = true;
        }

        if (server_->local_autonomous_system_ != old_local_asn) {
            SandeshLevel::type log_level;
            if (old_local_asn != 0) {
                log_level = SandeshLevel::SYS_NOTICE;
            } else {
                log_level = SandeshLevel::SYS_DEBUG;
            }
            BGP_LOG_STR(BgpConfig, log_level, BGP_LOG_FLAG_SYSLOG,
                        "Updated Local Autonomous System from " <<
                        old_local_asn << " to " <<
                        server_->local_autonomous_system_);
            notify_asn_update = true;
        }

        if (notify_asn_update) {
            server_->NotifyASNUpdate(old_asn, old_local_asn);
        }

        if (server_->hold_time_ != config_hold_time) {
            BGP_LOG_STR(BgpConfig, SandeshLevel::SYS_DEBUG, BGP_LOG_FLAG_SYSLOG,
                        "Updated Hold Time from " <<
                        server_->hold_time_ << " to " << config_hold_time);
            server_->hold_time_ = config_hold_time;
        }

        ConnectionState::GetInstance()->Update();
    }

    void ProcessNeighborConfig(const BgpNeighborConfig *neighbor_config,
                               BgpConfigManager::EventType event) {
        string instance_name = neighbor_config->instance_name();
        RoutingInstanceMgr *ri_mgr = server_->routing_instance_mgr();
        RoutingInstance *rti = ri_mgr->GetRoutingInstance(instance_name);
        assert(rti);
        PeerManager *peer_manager = rti->peer_manager();

        if (event == BgpConfigManager::CFG_ADD ||
            event == BgpConfigManager::CFG_CHANGE) {
            BgpPeer *peer = peer_manager->PeerLocate(server_, neighbor_config);
            if (peer) {
                server_->RemovePeer(peer->endpoint(), peer);
                peer->ConfigUpdate(neighbor_config);
                server_->InsertPeer(peer->endpoint(), peer);
            }
        } else if (event == BgpConfigManager::CFG_DELETE) {
            BgpPeer *peer = peer_manager->TriggerPeerDeletion(neighbor_config);
            if (peer) {
                server_->RemovePeer(peer->endpoint(), peer);
            }
        }
    }

    void ProcessRoutingPolicyConfig(const BgpRoutingPolicyConfig *policy_config,
                                    BgpConfigManager::EventType event) {
        RoutingPolicyMgr *mgr = server_->routing_policy_mgr();
        if (event == BgpConfigManager::CFG_ADD) {
            mgr->CreateRoutingPolicy(policy_config);
        } else if (event == BgpConfigManager::CFG_CHANGE) {
            mgr->UpdateRoutingPolicy(policy_config);
        } else if (event == BgpConfigManager::CFG_DELETE) {
            mgr->DeleteRoutingPolicy(policy_config->name());
        }
    }

    void ProcessInstanceConfig(const BgpInstanceConfig *instance_config,
                               BgpConfigManager::EventType event) {
        RoutingInstanceMgr *mgr = server_->routing_instance_mgr();
        if (event == BgpConfigManager::CFG_ADD) {
            mgr->CreateRoutingInstance(instance_config);
        } else if (event == BgpConfigManager::CFG_CHANGE) {
            mgr->UpdateRoutingInstance(instance_config);
        } else if (event == BgpConfigManager::CFG_DELETE) {
            mgr->DeleteRoutingInstance(instance_config->name());
        }
    }

private:
    BgpServer *server_;
};

class BgpServer::DeleteActor : public LifetimeActor {
public:
    explicit DeleteActor(BgpServer *server)
        : LifetimeActor(server->lifetime_manager()), server_(server) {
    }
    virtual bool MayDelete() const {
        CHECK_CONCURRENCY("bgp::Config");
        return server_->session_manager()->MayDelete();
    }
    virtual void Shutdown() {
        CHECK_CONCURRENCY("bgp::Config");
        server_->session_manager()->Shutdown();
    }
    virtual void Destroy() {
        CHECK_CONCURRENCY("bgp::Config");
        server_->config_manager()->Terminate();
        server_->session_manager()->Terminate();
        TcpServerManager::DeleteServer(server_->session_manager());
        server_->session_mgr_ = NULL;
        server_->set_destroyed();
    }

private:
    BgpServer *server_;
};

bool BgpServer::IsDeleted() const {
    return deleter_->IsDeleted();
}

void BgpServer::RetryDelete() {
    if (!deleter_->IsDeleted())
        return;
    deleter_->RetryDelete();
}

bool BgpServer::IsReadyForDeletion() {
    CHECK_CONCURRENCY("bgp::Config");

    static TaskScheduler *scheduler = TaskScheduler::GetInstance();
    static int resolver_path_task_id =
        scheduler->GetTaskId("bgp::ResolverPath");
    static int resolver_nexthop_task_id =
        scheduler->GetTaskId("bgp::ResolverNexthop");

    // Check if any PathResolver is active.
    // Need to ensure that there's no pending deletes of BgpPaths added by
    // PathResolver since they hold pointers to IPeer.
    if (!scheduler->IsTaskGroupEmpty(resolver_path_task_id) ||
        !scheduler->IsTaskGroupEmpty(resolver_nexthop_task_id)) {
        return false;
    }

    // Check if the membership manager queue is empty.
    if (!membership_mgr_->IsQueueEmpty()) {
        return false;
    }

    // Check if the Service Chain Manager Work Queues are empty.
    if (!inet_service_chain_mgr_->IsQueueEmpty() ||
        !inet6_service_chain_mgr_->IsQueueEmpty()) {
        return false;
    }

    // Check if the DB requests queue and change list is empty.
    if (!db_.IsDBQueueEmpty()) {
        return false;
    }

    // Check if the RTargetGroupManager has processed all RTargetRoute updates.
    // This is done to ensure that the InterestedPeerList of RtargetGroup gets
    // updated before allowing the peer to get deleted.
    if (!rtarget_group_mgr_->IsRTargetRoutesProcessed()) {
        return false;
    }

    return true;
}

BgpServer::BgpServer(EventManager *evm)
    : admin_down_(false),
      autonomous_system_(0),
      local_autonomous_system_(0),
      bgp_identifier_(0),
      hold_time_(0),
      gr_helper_enable_(getenv("GR_HELPER_BGP_ENABLE") != NULL),
      lifetime_manager_(BgpObjectFactory::Create<BgpLifetimeManager>(this,
          TaskScheduler::GetInstance()->GetTaskId("bgp::Config"))),
      deleter_(new DeleteActor(this)),
      destroyed_(false),
      logging_disabled_(false),
      aspath_db_(new AsPathDB(this)),
      olist_db_(new BgpOListDB(this)),
      cluster_list_db_(new ClusterListDB(this)),
      comm_db_(new CommunityDB(this)),
      edge_discovery_db_(new EdgeDiscoveryDB(this)),
      edge_forwarding_db_(new EdgeForwardingDB(this)),
      extcomm_db_(new ExtCommunityDB(this)),
      ovnpath_db_(new OriginVnPathDB(this)),
      pmsi_tunnel_db_(new PmsiTunnelDB(this)),
      attr_db_(new BgpAttrDB(this)),
      session_mgr_(BgpObjectFactory::Create<BgpSessionManager>(evm, this)),
      sched_mgr_(new SchedulingGroupManager),
      inst_mgr_(BgpObjectFactory::Create<RoutingInstanceMgr>(this)),
      policy_mgr_(BgpObjectFactory::Create<RoutingPolicyMgr>(this)),
      rtarget_group_mgr_(BgpObjectFactory::Create<RTargetGroupMgr>(this)),
      membership_mgr_(BgpObjectFactory::Create<BgpMembershipManager>(this)),
      inet_condition_listener_(new BgpConditionListener(this)),
      inet6_condition_listener_(new BgpConditionListener(this)),
      inetvpn_replicator_(new RoutePathReplicator(this, Address::INETVPN)),
      ermvpn_replicator_(new RoutePathReplicator(this, Address::ERMVPN)),
      evpn_replicator_(new RoutePathReplicator(this, Address::EVPN)),
      inet6vpn_replicator_(new RoutePathReplicator(this, Address::INET6VPN)),
      inet_service_chain_mgr_(
          BgpObjectFactory::Create<IServiceChainMgr, Address::INET>(this)),
      inet6_service_chain_mgr_(
          BgpObjectFactory::Create<IServiceChainMgr, Address::INET6>(this)),
      global_config_(new BgpGlobalSystemConfig()),
      config_mgr_(BgpObjectFactory::Create<BgpConfigManager>(this)),
      updater_(new ConfigUpdater(this)) {
    bgp_count_ = 0;
    num_up_peer_ = 0;
    deleting_count_ = 0;
    bgpaas_count_ = 0;
    num_up_bgpaas_peer_ = 0;
    deleting_bgpaas_count_ = 0;
    message_build_error_ = 0;
}

BgpServer::~BgpServer() {
    assert(deleting_count_ == 0);
    assert(deleting_bgpaas_count_ == 0);
    assert(srt_manager_list_.empty());
}

string BgpServer::ToString() const {
    return bgp_identifier_.to_string();
}

void BgpServer::Shutdown() {
    deleter_->Delete();
}

LifetimeActor *BgpServer::deleter() {
    return deleter_.get();
}

bool BgpServer::HasSelfConfiguration() const {
    if (!bgp_identifier())
        return false;
    if (!local_autonomous_system())
        return false;
    if (!autonomous_system())
        return false;
    return true;
}

int BgpServer::RegisterPeer(BgpPeer *peer) {
    CHECK_CONCURRENCY("bgp::Config");

    if (peer->router_type() == "bgpaas-client") {
        bgpaas_count_++;
    } else {
        bgp_count_++;
    }

    BgpPeerList::iterator loc;
    bool result;
    tie(loc, result) = peer_list_.insert(make_pair(peer->peer_name(), peer));
    assert(result);
    assert(loc->second == peer);

    size_t bit = peer_bmap_.find_first();
    if (bit == peer_bmap_.npos) {
        bit = peer_bmap_.size();
        peer_bmap_.resize(bit + 1, true);
    }
    peer_bmap_.reset(bit);
    return bit;
}

void BgpServer::UnregisterPeer(BgpPeer *peer) {
    CHECK_CONCURRENCY("bgp::Config");

    if (peer->router_type() == "bgpaas-client") {
        assert(bgpaas_count_);
        bgpaas_count_--;
    } else {
        assert(bgp_count_);
        bgp_count_--;
    }

    BgpPeerList::iterator loc = peer_list_.find(peer->peer_name());
    assert(loc != peer_list_.end());
    peer_list_.erase(loc);

    peer_bmap_.set(peer->GetIndex());
    for (size_t i = peer_bmap_.size(); i != 0; i--) {
        if (peer_bmap_[i-1] != true) {
            if (i != peer_bmap_.size()) {
                peer_bmap_.resize(i);
            }
            return;
        }
    }
    peer_bmap_.clear();
}

BgpPeer *BgpServer::FindPeer(const string &name) {
    BgpPeerList::iterator loc = peer_list_.find(name);
    return (loc != peer_list_.end() ? loc->second : NULL);
}

void BgpServer::InsertPeer(TcpSession::Endpoint remote, BgpPeer *peer) {
    if (!remote.port() && remote.address().is_unspecified())
        return;

    EndpointToBgpPeerList::iterator loc = endpoint_peer_list_.find(remote);
    if (loc != endpoint_peer_list_.end()) {
        loc->second->Clear(BgpProto::Notification::PeerDeconfigured);
        endpoint_peer_list_.erase(loc);
    }
    endpoint_peer_list_.insert(make_pair(remote, peer));
}

void BgpServer::RemovePeer(TcpSession::Endpoint remote, BgpPeer *peer) {
    EndpointToBgpPeerList::iterator loc = endpoint_peer_list_.find(remote);
    if (loc != endpoint_peer_list_.end() && loc->second == peer) {
        endpoint_peer_list_.erase(loc);
    }
}

BgpPeer *BgpServer::FindPeer(TcpSession::Endpoint remote) const {
    EndpointToBgpPeerList::const_iterator loc =
        endpoint_peer_list_.find(remote);
    return (loc == endpoint_peer_list_.end() ? NULL : loc->second);
}

BgpPeer *BgpServer::FindNextPeer(TcpSession::Endpoint remote) const {
    EndpointToBgpPeerList::const_iterator loc =
        endpoint_peer_list_.upper_bound(remote);
    return (loc == endpoint_peer_list_.end() ? NULL : loc->second);
}

const string &BgpServer::localname() const {
    return config_mgr_->localname();
}

boost::asio::io_service *BgpServer::ioservice() {
    return session_manager()->event_manager()->io_service();
}

uint16_t BgpServer::GetGracefulRestartTime() const {
    return global_config_->gr_time();
}

uint32_t BgpServer::GetLongLivedGracefulRestartTime() const {
    return global_config_->llgr_time();
}

uint32_t BgpServer::GetEndOfRibReceiveTime() const {
    return global_config_->eor_rx_time();
}

uint32_t BgpServer::num_routing_instance() const {
    assert(inst_mgr_.get());
    return inst_mgr_->count();
}

uint32_t BgpServer::num_deleted_routing_instance() const {
    assert(inst_mgr_.get());
    return inst_mgr_->deleted_count();
}

uint32_t BgpServer::get_output_queue_depth() const {
    uint32_t out_q_depth = 0;
    for (RoutingInstanceMgr::RoutingInstanceIterator rit = inst_mgr_->begin();
         rit != inst_mgr_->end(); ++rit) {
        RoutingInstance::RouteTableList const rt_list = rit->GetTables();
        for (RoutingInstance::RouteTableList::const_iterator it =
             rt_list.begin(); it != rt_list.end(); ++it) {
            BgpTable *table = it->second;
            size_t markers;
            out_q_depth += table->GetPendingRiboutsCount(&markers);
        }
    }
    return out_q_depth;
}

uint32_t BgpServer::num_service_chains() const {
    return inet_service_chain_mgr_->PendingQueueSize() +
        inet_service_chain_mgr_->ResolvedQueueSize() +
        inet6_service_chain_mgr_->PendingQueueSize() +
        inet6_service_chain_mgr_->ResolvedQueueSize();
}

uint32_t BgpServer::num_down_service_chains() const {
    return inet_service_chain_mgr_->PendingQueueSize() +
        inet_service_chain_mgr_->GetDownServiceChainCount() +
        inet6_service_chain_mgr_->PendingQueueSize() +
        inet6_service_chain_mgr_->GetDownServiceChainCount();
}

uint32_t BgpServer::num_static_routes() const {
    return GetStaticRouteCount();
}

uint32_t BgpServer::num_down_static_routes() const {
    return GetDownStaticRouteCount();
}

void BgpServer::VisitBgpPeers(BgpServer::VisitorFn fn) const {
    for (RoutingInstanceMgr::RoutingInstanceIterator rit = inst_mgr_->begin();
         rit != inst_mgr_->end(); ++rit) {
        BgpPeerKey key = BgpPeerKey();
        while (BgpPeer *peer = rit->peer_manager()->NextPeer(key)) {
            fn(peer);
        }
    }
}

int BgpServer::RegisterAdminDownCallback(AdminDownCb callback) {
    tbb::spin_rw_mutex::scoped_lock write_lock(rw_mutex_, true);
    size_t i = admin_down_bmap_.find_first();
    if (i == admin_down_bmap_.npos) {
        i = admin_down_listeners_.size();
        admin_down_listeners_.push_back(callback);
    } else {
        admin_down_bmap_.reset(i);
        if (admin_down_bmap_.none()) {
            admin_down_bmap_.clear();
        }
        admin_down_listeners_[i] = callback;
    }
    return i;
}

void BgpServer::UnregisterAdminDownCallback(int listener) {
    tbb::spin_rw_mutex::scoped_lock write_lock(rw_mutex_, true);
    admin_down_listeners_[listener] = NULL;
    if ((size_t) listener == admin_down_listeners_.size() - 1) {
        while (!admin_down_listeners_.empty() &&
            admin_down_listeners_.back() == NULL) {
            admin_down_listeners_.pop_back();
        }
        if (admin_down_bmap_.size() > admin_down_listeners_.size()) {
            admin_down_bmap_.resize(admin_down_listeners_.size());
        }
    } else {
        if ((size_t) listener >= admin_down_bmap_.size()) {
            admin_down_bmap_.resize(listener + 1);
        }
        admin_down_bmap_.set(listener);
    }
}

void BgpServer::NotifyAdminDown() {
    tbb::spin_rw_mutex::scoped_lock read_lock(rw_mutex_, false);
    for (AdminDownListenersList::iterator iter = admin_down_listeners_.begin();
         iter != admin_down_listeners_.end(); ++iter) {
        if (*iter != NULL) {
            AdminDownCb cb = *iter;
            (cb)();
        }
    }
}

int BgpServer::RegisterASNUpdateCallback(ASNUpdateCb callback) {
    tbb::spin_rw_mutex::scoped_lock write_lock(rw_mutex_, true);
    size_t i = asn_bmap_.find_first();
    if (i == asn_bmap_.npos) {
        i = asn_listeners_.size();
        asn_listeners_.push_back(callback);
    } else {
        asn_bmap_.reset(i);
        if (asn_bmap_.none()) {
            asn_bmap_.clear();
        }
        asn_listeners_[i] = callback;
    }
    return i;
}

void BgpServer::UnregisterASNUpdateCallback(int listener) {
    tbb::spin_rw_mutex::scoped_lock write_lock(rw_mutex_, true);
    asn_listeners_[listener] = NULL;
    if ((size_t) listener == asn_listeners_.size() - 1) {
        while (!asn_listeners_.empty() && asn_listeners_.back() == NULL) {
            asn_listeners_.pop_back();
        }
        if (asn_bmap_.size() > asn_listeners_.size()) {
            asn_bmap_.resize(asn_listeners_.size());
        }
    } else {
        if ((size_t) listener >= asn_bmap_.size()) {
            asn_bmap_.resize(listener + 1);
        }
        asn_bmap_.set(listener);
    }
}

void BgpServer::NotifyASNUpdate(as_t old_asn, as_t old_local_asn) {
    tbb::spin_rw_mutex::scoped_lock read_lock(rw_mutex_, false);
    for (ASNUpdateListenersList::iterator iter = asn_listeners_.begin();
         iter != asn_listeners_.end(); ++iter) {
        if (*iter != NULL) {
            ASNUpdateCb cb = *iter;
            (cb)(old_asn, old_local_asn);
        }
    }
}

int BgpServer::RegisterIdentifierUpdateCallback(IdentifierUpdateCb callback) {
    tbb::spin_rw_mutex::scoped_lock write_lock(rw_mutex_, true);
    size_t i = id_bmap_.find_first();
    if (i == id_bmap_.npos) {
        i = id_listeners_.size();
        id_listeners_.push_back(callback);
    } else {
        id_bmap_.reset(i);
        if (id_bmap_.none()) {
            id_bmap_.clear();
        }
        id_listeners_[i] = callback;
    }
    return i;
}

void BgpServer::UnregisterIdentifierUpdateCallback(int listener) {
    tbb::spin_rw_mutex::scoped_lock write_lock(rw_mutex_, true);
    id_listeners_[listener] = NULL;
    if ((size_t) listener == id_listeners_.size() - 1) {
        while (!id_listeners_.empty() && id_listeners_.back() == NULL) {
            id_listeners_.pop_back();
        }
        if (id_bmap_.size() > id_listeners_.size()) {
            id_bmap_.resize(id_listeners_.size());
        }
    } else {
        if ((size_t) listener >= id_bmap_.size()) {
            id_bmap_.resize(listener + 1);
        }
        id_bmap_.set(listener);
    }
}

void BgpServer::NotifyIdentifierUpdate(Ip4Address old_identifier) {
    tbb::spin_rw_mutex::scoped_lock read_lock(rw_mutex_, false);
    for (IdentifierUpdateListenersList::iterator iter = id_listeners_.begin();
         iter != id_listeners_.end(); ++iter) {
        if (*iter != NULL) {
            IdentifierUpdateCb cb = *iter;
            (cb)(old_identifier);
        }
    }
}

void BgpServer::InsertStaticRouteMgr(IStaticRouteMgr *srt_manager) {
    CHECK_CONCURRENCY("bgp::Config");
    srt_manager_list_.insert(srt_manager);
}

void BgpServer::RemoveStaticRouteMgr(IStaticRouteMgr *srt_manager) {
    CHECK_CONCURRENCY("bgp::Config");
    srt_manager_list_.erase(srt_manager);
}

void BgpServer::NotifyAllStaticRoutes() {
    CHECK_CONCURRENCY("bgp::Config");
    for (StaticRouteMgrList::iterator it = srt_manager_list_.begin();
         it != srt_manager_list_.end(); ++it) {
        IStaticRouteMgr *srt_manager = *it;
        srt_manager->NotifyAllRoutes();
    }
}

uint32_t BgpServer::GetStaticRouteCount() const {
    CHECK_CONCURRENCY("bgp::Uve");
    uint32_t count = 0;
    for (StaticRouteMgrList::iterator it = srt_manager_list_.begin();
         it != srt_manager_list_.end(); ++it) {
        IStaticRouteMgr *srt_manager = *it;
        count += srt_manager->GetRouteCount();
    }
    return count;
}

uint32_t BgpServer::GetDownStaticRouteCount() const {
    CHECK_CONCURRENCY("bgp::Uve");
    uint32_t count = 0;
    for (StaticRouteMgrList::iterator it = srt_manager_list_.begin();
         it != srt_manager_list_.end(); ++it) {
        IStaticRouteMgr *srt_manager = *it;
        count += srt_manager->GetDownRouteCount();
    }
    return count;
}

uint32_t BgpServer::SendTableStatsUve(bool first) const {
    uint32_t out_q_depth = 0;
    for (RoutingInstanceMgr::RoutingInstanceIterator rit = inst_mgr_->begin();
         rit != inst_mgr_->end(); ++rit) {
        RoutingInstanceStatsData instance_info;
        RoutingInstance::RouteTableList const rt_list = rit->GetTables();
        std::vector<BgpTableStats> tables_stats;

        for (RoutingInstance::RouteTableList::const_iterator it =
             rt_list.begin(); it != rt_list.end(); ++it) {
            BgpTable *table = it->second;

            size_t markers;
            out_q_depth += table->GetPendingRiboutsCount(&markers);
            string family = Address::FamilyToString(table->family());

            bool changed = false;
            if (first || table->stats()->get_address_family() != family) {
                changed = true;
                table->stats()->set_address_family(family);
            }

            if (first || table->stats()->get_prefixes() != table->Size()) {
                changed = true;
                table->stats()->set_prefixes(table->Size());
            }

            if (first || table->stats()->get_primary_paths() !=
                    table->GetPrimaryPathCount()) {
                changed = true;
                table->stats()->set_primary_paths(table->GetPrimaryPathCount());
            }

            if (first || table->stats()->get_secondary_paths() !=
                    table->GetSecondaryPathCount()) {
                changed = true;
                table->stats()->set_secondary_paths(
                    table->GetSecondaryPathCount());
            }

            uint64_t total_paths = table->stats()->get_primary_paths() +
                                   table->stats()->get_secondary_paths() +
                                   table->stats()->get_infeasible_paths();
            if (first || table->stats()->get_total_paths() != total_paths) {
                changed = true;
                table->stats()->set_total_paths(total_paths);
            }

            if (changed) {
                tables_stats.push_back(*table->stats());

                // Reset changed flags in the uve structure.
                memset(&(table->stats()->__isset), 0,
                       sizeof(table->stats()->__isset));
            }
        }

        // Set the key and send out the uve.
        if (!tables_stats.empty()) {
            instance_info.set_name(rit->name());
            instance_info.set_table_stats(tables_stats);
            RoutingInstanceStats::Send(instance_info);
        }
    }

    return out_q_depth;
}

void BgpServer::FillPeerStats(const BgpPeer *peer) const {
    PeerStatsInfo stats;
    PeerStats::FillPeerDebugStats(peer->peer_stats(), &stats);

    BgpPeerInfoData peer_info;
    peer_info.set_name(peer->ToUVEKey());
    peer_info.set_peer_stats_info(stats);
    BGPPeerInfo::Send(peer_info);
}

bool BgpServer::CollectStats(BgpRouterState *state, bool first) const {
    CHECK_CONCURRENCY("bgp::Uve");

    VisitBgpPeers(boost::bind(&BgpServer::FillPeerStats, this, _1));
    bool change = false;
    uint32_t is_admin_down = admin_down();
    if (first || is_admin_down != state->get_admin_down()) {
        state->set_admin_down(is_admin_down);
        change = true;
    }

    string router_id = bgp_identifier_string();
    if (first || router_id != state->get_router_id()) {
        state->set_router_id(router_id);
        change = true;
    }

    uint32_t local_asn = local_autonomous_system();
    if (first || local_asn != state->get_local_asn()) {
        state->set_local_asn(local_asn);
        change = true;
    }

    uint32_t global_asn = autonomous_system();
    if (first || global_asn != state->get_global_asn()) {
        state->set_global_asn(global_asn);
        change = true;
    }

    uint32_t num_bgp = num_bgp_peer();
    if (first || num_bgp != state->get_num_bgp_peer()) {
        state->set_num_bgp_peer(num_bgp);
        change = true;
    }

    uint32_t num_up_bgp_peer = NumUpPeer();
    if (first || num_up_bgp_peer != state->get_num_up_bgp_peer()) {
        state->set_num_up_bgp_peer(num_up_bgp_peer);
        change = true;
    }

    uint32_t deleting_bgp_peer = num_deleting_bgp_peer();
    if (first || deleting_bgp_peer != state->get_num_deleting_bgp_peer()) {
        state->set_num_deleting_bgp_peer(deleting_bgp_peer);
        change = true;
    }

    uint32_t num_bgpaas = num_bgpaas_peer();
    if (first || num_bgpaas != state->get_num_bgpaas_peer()) {
        state->set_num_bgpaas_peer(num_bgpaas);
        change = true;
    }

    uint32_t num_up_bgpaas_peer = NumUpBgpaasPeer();
    if (first || num_up_bgpaas_peer != state->get_num_up_bgpaas_peer()) {
        state->set_num_up_bgpaas_peer(num_up_bgpaas_peer);
        change = true;
    }

    uint32_t deleting_bgpaas_peer = num_deleting_bgpaas_peer();
    if (first || deleting_bgpaas_peer !=
            state->get_num_deleting_bgpaas_peer()) {
        state->set_num_deleting_bgpaas_peer(deleting_bgpaas_peer);
        change = true;
    }

    uint32_t num_ri = num_routing_instance();
    if (first || num_ri != state->get_num_routing_instance()) {
        state->set_num_routing_instance(num_ri);
        change = true;
    }

    uint32_t num_deleted_ri = num_deleted_routing_instance();
    if (first || num_deleted_ri != state->get_num_deleted_routing_instance()) {
        state->set_num_deleted_routing_instance(num_deleted_ri);
        change = true;
    }

    uint32_t service_chains = num_service_chains();
    if (first || service_chains != state->get_num_service_chains()) {
        state->set_num_service_chains(service_chains);
        change = true;
    }

    uint32_t down_service_chains = num_down_service_chains();
    if (first || down_service_chains != state->get_num_down_service_chains()) {
        state->set_num_down_service_chains(down_service_chains);
        change = true;
    }

    uint32_t static_routes = num_static_routes();
    if (first || static_routes != state->get_num_static_routes()) {
        state->set_num_static_routes(static_routes);
        change = true;
    }

    uint32_t down_static_routes = num_down_static_routes();
    if (first || down_static_routes != state->get_num_down_static_routes()) {
        state->set_num_down_static_routes(down_static_routes);
        change = true;
    }

    uint32_t out_load = SendTableStatsUve(first);
    if (first || out_load != state->get_output_queue_depth()) {
        state->set_output_queue_depth(out_load);
        change = true;
    }

    return change;
}
