/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/bgp_server.h"

#include <boost/assign.hpp>

#include "base/logging.h"
#include "base/lifetime.h"
#include "base/task_annotations.h"
#include "bgp/bgp_config.h"
#include "bgp/bgp_condition_listener.h"
#include "bgp/bgp_factory.h"
#include "bgp/bgp_log.h"
#include "bgp/bgp_peer.h"
#include "bgp/bgp_peer_membership.h"
#include "bgp/bgp_session.h"
#include "bgp/bgp_session_manager.h"
#include "bgp/scheduling_group.h"
#include "bgp/routing-instance/peer_manager.h"
#include "bgp/routing-instance/routing_instance.h"
#include "bgp/routing-instance/routepath_replicator.h"
#include "bgp/routing-instance/service_chaining.h"
#include "io/event_manager.h"

using namespace std;
using namespace boost::asio;
using namespace boost::assign;
using boost::system::error_code;

// The ConfigUpdater serves as glue between the BgpConfigManager and the
// BgpServer.
class BgpServer::ConfigUpdater {
public:
    ConfigUpdater(BgpServer *server) : server_(server) {
        BgpConfigManager::Observers obs;
        obs.instance =
            boost::bind(&ConfigUpdater::ProcessInstanceConfig, this, _1, _2);
        obs.protocol =
            boost::bind(&ConfigUpdater::ProcessBgpConfig, this, _1, _2);
        obs.neighbor =
            boost::bind(&ConfigUpdater::ProcessNeighborConfig, this, _1, _2);

        server->config_manager()->RegisterObservers(obs);
    }

    void ProcessBgpConfig(const BgpProtocolConfig *bgp_config,
                          BgpConfigManager::EventType event) {

        const string &instance_name = bgp_config->InstanceName();

        //
        // At the moment, we only support BGP sessions in master instance
        //
        if (instance_name != BgpConfigManager::kMasterInstance) {
            return;
        }

        autogen::BgpRouterParams params;

        //
        // Update with default parameters
        //
        server_->config_manager()->DefaultBgpRouterParams(params);

        if (event == BgpConfigManager::CFG_ADD ||
            event == BgpConfigManager::CFG_CHANGE) {
            if (bgp_config->bgp_router()) {
                params = bgp_config->router_params();
            }
        } else if (event != BgpConfigManager::CFG_DELETE) {
            assert(false);
            return;
        }

        BgpServerConfigUpdate(instance_name, params);
    }

    void BgpServerConfigUpdate(string instance_name,
                               autogen::BgpRouterParams &params) {
        boost::system::error_code ec;
        Ip4Address bgp_identifier =
            Ip4Address::from_string(params.identifier, ec);

        if (server_->bgp_identifier_ != bgp_identifier) {
            BGP_LOG_STR(BgpConfig, SandeshLevel::SYS_DEBUG,
                        BGP_LOG_FLAG_SYSLOG,
                        "Updated bgp_identifier from " <<
                        server_->bgp_identifier_.to_string() << " to " <<
                        params.identifier);
            server_->bgp_identifier_ = bgp_identifier;
        }

        if (server_->autonomous_system_ != params.autonomous_system) {
            BGP_LOG_STR(BgpConfig, SandeshLevel::SYS_DEBUG,
                        BGP_LOG_FLAG_SYSLOG,
                        "Updated local autonomous_system number from " <<
                        server_->autonomous_system_ << " to "
                        << params.autonomous_system);
            server_->autonomous_system_ = params.autonomous_system;
        }
    }

    void ProcessNeighborConfig(const BgpNeighborConfig *neighbor_config,
                               BgpConfigManager::EventType event) {
        string instance_name = neighbor_config->InstanceName();
        RoutingInstanceMgr *ri_mgr = server_->routing_instance_mgr();
        RoutingInstance *rti = ri_mgr->GetRoutingInstance(instance_name);
        PeerManager *peer_manager = rti->peer_manager();

        if (event == BgpConfigManager::CFG_ADD ||
            event == BgpConfigManager::CFG_CHANGE) {
            BgpPeer *peer = peer_manager->PeerLocate(server_, neighbor_config);
            peer->ConfigUpdate(neighbor_config);
        } else if (event == BgpConfigManager::CFG_DELETE) {
            peer_manager->TriggerPeerDeletion(neighbor_config);
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
    DeleteActor(BgpServer *server)
        : LifetimeActor(server->lifetime_manager()), server_(server) { }
    virtual bool MayDelete() const {
        return true;
    }
    virtual void Shutdown() {
        CHECK_CONCURRENCY("bgp::Config");
        server_->session_manager()->Shutdown();
    }
    virtual void Destroy() {
        CHECK_CONCURRENCY("bgp::Config");
        server_->config_manager()->Terminate();
        TcpServerManager::DeleteServer(server_->session_manager());
        server_->session_mgr_ = NULL;
    }
private:
    BgpServer *server_;
};

bool BgpServer::IsReadyForDeletion() {
    CHECK_CONCURRENCY("bgp::Config");

    //
    // Check if the IPeer membership manager queue is empty
    //
    if (!membership_mgr_->IsQueueEmpty()) {
        return false;
    }

    // Check if the Service Chain Manager Work Queue is empty
    if (!service_chain_mgr_->IsQueueEmpty()) {
        return false;
    }

    //
    // Check if the DB requests queue and change list is empty
    //
    if (!db_.IsDBQueueEmpty()) {
        return false;
    }

    return true;
}

BgpServer::BgpServer(EventManager *evm)
    : autonomous_system_(0), bgp_identifier_(0),
      lifetime_manager_(new LifetimeManager(
          TaskScheduler::GetInstance()->GetTaskId("bgp::Config"),
          boost::bind(&BgpServer::IsReadyForDeletion, this))),
      deleter_(new DeleteActor(this)),
      aspath_db_(new AsPathDB(this)),
      comm_db_(new CommunityDB(this)),
      extcomm_db_(new ExtCommunityDB(this)),
      attr_db_(new BgpAttrDB(this)),
      session_mgr_(BgpObjectFactory::Create<BgpSessionManager>(evm, this)),
      sched_mgr_(new SchedulingGroupManager),
      inst_mgr_(BgpObjectFactory::Create<RoutingInstanceMgr>(this)),
      membership_mgr_(BgpObjectFactory::Create<PeerRibMembershipManager>(this)),
      condition_listener_(new BgpConditionListener(this)),
      inetvpn_replicator_(new RoutePathReplicator(this, Address::INETVPN)),
      evpn_replicator_(new RoutePathReplicator(this, Address::EVPN)),
      service_chain_mgr_(new ServiceChainMgr(this)),
      config_mgr_(BgpObjectFactory::Create<BgpConfigManager>(this)),
      updater_(new ConfigUpdater(this)) {
    num_up_peer_ = 0;
}

BgpServer::~BgpServer() {
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

int BgpServer::AllocPeerIndex() {
    size_t bit = peer_bmap_.find_first();
    if (bit == peer_bmap_.npos) {
        bit = peer_bmap_.size();
        peer_bmap_.resize(bit + 1, true);
    }
    peer_bmap_.reset(bit);
    return bit;
}

void BgpServer::FreePeerIndex(int id) {
    peer_bmap_.set(id);

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

const std::string &BgpServer::localname() const {
    return config_mgr_->localname();
}

boost::asio::io_service *BgpServer::ioservice() { 
   return session_manager()->event_manager()->io_service(); 
}

bool BgpServer::IsPeerCloseGraceful() {

    //
    // If the server is deleted, do not do graceful restart
    //
    if (deleter()->IsDeleted()) return false;

    static bool init = false;
    static bool enabled = false;

    if (!init) {
        init = true;
        char *p = getenv("BGP_GRACEFUL_RESTART_ENABLE");
        if (p && !strcasecmp(p, "true")) enabled = true;
    }
    return enabled;
}

uint32_t BgpServer::num_routing_instance() const {
    assert(inst_mgr_.get());
    return inst_mgr_->count();
}

uint32_t BgpServer::get_output_queue_depth() const {
    uint32_t out_q_depth = 0;
    RoutingInstanceMgr::RoutingInstanceIterator rit = inst_mgr_->begin();
    for (;rit != inst_mgr_->end(); rit++) {
        RoutingInstance::RouteTableList const rt_list = rit->GetTables();
        for (RoutingInstance::RouteTableList::const_iterator it = 
             rt_list.begin(); it != rt_list.end(); ++it) {
            BgpTable *table = it->second;
            size_t markers;
            out_q_depth += table->GetPendingRiboutsCount(markers);
        }
    }
    return out_q_depth;
}

void BgpServer::VisitBgpPeers(BgpServer::VisitorFn fn) const {
    RoutingInstanceMgr::RoutingInstanceIterator rit = inst_mgr_->begin();
    for (;rit != inst_mgr_->end(); rit++) {
        BgpPeerKey key = BgpPeerKey();
        while (BgpPeer *peer = rit->peer_manager()->NextPeer(key)) {
            fn(peer);
        }
    }
}
