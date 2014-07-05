/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/routing-instance/routing_instance.h"

#include <boost/foreach.hpp>
#include <boost/assign/list_of.hpp>

#include "base/lifetime.h"
#include "base/task_annotations.h"
#include "bgp/bgp_config.h"
#include "bgp/bgp_factory.h"
#include "bgp/bgp_log.h"
#include "bgp/bgp_server.h"
#include "bgp/routing-instance/peer_manager.h"
#include "bgp/routing-instance/routepath_replicator.h"
#include "bgp/routing-instance/routing_instance_log.h"
#include "bgp/routing-instance/rtarget_group_mgr.h"
#include "bgp/routing-instance/rtarget_group.h"
#include "bgp/routing-instance/service_chaining.h"
#include "bgp/routing-instance/static_route.h"
#include "db/db_table.h"

using namespace std;
using namespace boost::asio;
using boost::assign::list_of;
using boost::system::error_code;

SandeshTraceBufferPtr RoutingInstanceTraceBuf(
        SandeshTraceBufferCreate(RTINSTANCE_TRACE_BUF, 1000));

class RoutingInstanceMgr::DeleteActor : public LifetimeActor {
public:
    DeleteActor(RoutingInstanceMgr *manager)
        : LifetimeActor(manager->server_->lifetime_manager()),
          manager_(manager) {
    }
    virtual bool MayDelete() const {
        return true;
    }
    virtual void Shutdown() {
    }
    virtual void Destroy() {
        // memory is deallocated by BgpServer scoped_ptr.
        manager_->server_delete_ref_.Reset(NULL);
    }

private:
    RoutingInstanceMgr *manager_;
};

RoutingInstanceMgr::RoutingInstanceMgr(BgpServer *server) :
        server_(server),
        deleter_(new DeleteActor(this)),
        server_delete_ref_(this, server->deleter()) {
}

RoutingInstanceMgr::~RoutingInstanceMgr() {
}

void RoutingInstanceMgr::ManagedDelete() {
    deleter_->Delete();
}

LifetimeActor *RoutingInstanceMgr::deleter() {
    return deleter_.get();
}

bool RoutingInstanceMgr::deleted() {
    return deleter()->IsDeleted();
}

//
// Go through all export targets for the RoutingInstance and add an entry for
// each one to the InstanceTargetMap.
//
void RoutingInstanceMgr::InstanceTargetAdd(RoutingInstance *rti) {
    for (RoutingInstance::RouteTargetList::const_iterator it =
         rti->GetExportList().begin(); it != rti->GetExportList().end(); ++it) {
        target_map_.insert(make_pair(*it, rti));
    }
}

//
// Go through all export targets for the RoutingInstance and remove the entry
// for each one from the InstanceTargetMap.  Note that there may be multiple
// entries in the InstanceTargetMap for a given export target.  Hence we need
// to make sure that we only remove the entry that matches the RoutingInstance.
//
void RoutingInstanceMgr::InstanceTargetRemove(const RoutingInstance *rti) {
    for (RoutingInstance::RouteTargetList::const_iterator it =
         rti->GetExportList().begin(); it != rti->GetExportList().end(); ++it) {
        for (InstanceTargetMap::iterator loc = target_map_.find(*it);
             loc != target_map_.end() && loc->first == *it; ++loc) {
            if (loc->second == rti) {
                target_map_.erase(loc);
                break;
            }
        }
    }
}

//
// Lookup the RoutingInstance for the given RouteTarget.
//
const RoutingInstance *RoutingInstanceMgr::GetInstanceByTarget(
        const RouteTarget &rtarget) const {
    InstanceTargetMap::const_iterator loc = target_map_.find(rtarget);
    if (loc == target_map_.end()) {
        return NULL;
    }
    return loc->second;
}

//
// Add an entry for the vn index to the VnIndexMap.
//
void RoutingInstanceMgr::InstanceVnIndexAdd(RoutingInstance *rti) {
    if (rti->virtual_network_index())
        vn_index_map_.insert(make_pair(rti->virtual_network_index(), rti));
}

//
// Remove the entry for the vn index from the VnIndexMap.  Note that there may
// be multiple entries in the VnIndexMap for a given vn index target. Hence we
// need to make sure that we remove the entry that matches the RoutingInstance.
//
void RoutingInstanceMgr::InstanceVnIndexRemove(const RoutingInstance *rti) {
    if (!rti->virtual_network_index())
        return;

    int vn_index = rti->virtual_network_index();
    for (VnIndexMap::iterator loc = vn_index_map_.find(vn_index);
         loc != vn_index_map_.end() && loc->first == vn_index; ++loc) {
        if (loc->second == rti) {
            vn_index_map_.erase(loc);
            break;
        }
    }
}

//
// Lookup the RoutingInstance for the given vn index.
//
const RoutingInstance *RoutingInstanceMgr::GetInstanceByVnIndex(
        int vn_index) const {
    VnIndexMap::const_iterator loc = vn_index_map_.find(vn_index);
    if (loc == vn_index_map_.end())
        return NULL;
    return loc->second;
}

//
// Lookup the VN name for the given vn index.
//
std::string RoutingInstanceMgr::GetVirtualNetworkByVnIndex(
        int vn_index) const {
    const RoutingInstance *rti = GetInstanceByVnIndex(vn_index);
    return rti ? rti->virtual_network() : "unresolved";
}

//
// Lookup the vn index for the given RouteTarget.
//
// Return 0 if the RouteTarget does not map to a RoutingInstance.
// Return -1 if the RouteTarget maps to multiple RoutingInstances
// that belong to different VNs.
//
int RoutingInstanceMgr::GetVnIndexByRouteTarget(
        const RouteTarget &rtarget) const {
    int vn_index = 0;
    for (InstanceTargetMap::const_iterator loc = target_map_.find(rtarget);
         loc != target_map_.end() && loc->first == rtarget; ++loc) {
        int ri_vn_index = loc->second->virtual_network_index();
        if (vn_index && ri_vn_index && ri_vn_index != vn_index)
            return -1;
        vn_index = ri_vn_index;
    }

    return vn_index;
}

//
// Derive the vn index from the route targets in the ExtCommunity.
//
// If the result is ambiguous i.e. we have a RouteTarget that maps
// to multiple vn indexes or we have multiple RouteTargets that map
// to different vn indexes, return 0.
//
int RoutingInstanceMgr::GetVnIndexByExtCommunity(
        const ExtCommunity *ext_community) const {
    int vn_index = 0;
    BOOST_FOREACH(const ExtCommunity::ExtCommunityValue &comm,
                  ext_community->communities()) {
        if (!ExtCommunity::is_route_target(comm))
            continue;

        RouteTarget rtarget(comm);
        int rtgt_vn_index = GetVnIndexByRouteTarget(rtarget);
        if (rtgt_vn_index < 0 ||
            (vn_index && rtgt_vn_index && rtgt_vn_index != vn_index)) {
            vn_index = 0;
            break;
        } else if (rtgt_vn_index) {
            vn_index = rtgt_vn_index;
        }
    }

    return vn_index;
}

int
RoutingInstanceMgr::RegisterInstanceOpCallback(RoutingInstanceCb callback) {
    tbb::spin_rw_mutex::scoped_lock write_lock(rw_mutex_, true);
    size_t i = bmap_.find_first();
    if (i == bmap_.npos) {
        i = callbacks_.size();
        callbacks_.push_back(callback);
    } else {
        bmap_.reset(i);
        if (bmap_.none()) {
            bmap_.clear();
        }
        callbacks_[i] = callback;
    }
    return i;
}

void RoutingInstanceMgr::UnregisterInstanceOpCallback(int listener) {
    tbb::spin_rw_mutex::scoped_lock write_lock(rw_mutex_, true);
    callbacks_[listener] = NULL;
    if ((size_t) listener == callbacks_.size() - 1) {
        while (!callbacks_.empty() && callbacks_.back() == NULL) {
            callbacks_.pop_back();
        }
        if (bmap_.size() > callbacks_.size()) {
            bmap_.resize(callbacks_.size());
        }
    } else {
        if ((size_t) listener >= bmap_.size()) {
            bmap_.resize(listener + 1);
        }
        bmap_.set(listener);
    }
}

void RoutingInstanceMgr::NotifyInstanceOp(std::string name, Operation op) {
    tbb::spin_rw_mutex::scoped_lock read_lock(rw_mutex_, false);
    for (InstanceOpListenersList::iterator iter = callbacks_.begin();
         iter != callbacks_.end(); ++iter) {
        if (*iter != NULL) {
            RoutingInstanceCb cb = *iter;
            (cb)(name, op);
        }
    }
}

RoutingInstance *RoutingInstanceMgr::CreateRoutingInstance(
        const BgpInstanceConfig *config) {
    RoutingInstance *rtinstance = GetRoutingInstance(config->name());

    if (rtinstance) {
        if (rtinstance->deleted()) {
            RTINSTANCE_LOG_MESSAGE(server_,
                SandeshLevel::SYS_WARN, RTINSTANCE_LOG_FLAG_ALL, config->name(),
                "Instance is recreated before pending deletion is complete");
            return NULL;
        } else {
            // Duplicate instance creation request can be safely ignored
            RTINSTANCE_LOG_MESSAGE(server_,
                SandeshLevel::SYS_WARN, RTINSTANCE_LOG_FLAG_ALL, config->name(),
                "Instance already found during creation");
        }
        return rtinstance;
    }

    rtinstance = BgpObjectFactory::Create<RoutingInstance>(
                     config->name(), server_,this, config);
    rtinstance->ProcessConfig(server_);
    int index = instances_.Insert(config->name(), rtinstance);

    rtinstance->set_index(server_, index);
    InstanceTargetAdd(rtinstance);
    InstanceVnIndexAdd(rtinstance);

    // Notify clients about routing instance create
    NotifyInstanceOp(config->name(), INSTANCE_ADD);

    std::vector<string> import_rt(config->import_list().begin(),
                                  config->import_list().end());
    std::vector<string> export_rt(config->export_list().begin(),
                                  config->export_list().end());
    RTINSTANCE_LOG(Create, rtinstance,
        SandeshLevel::SYS_DEBUG, RTINSTANCE_LOG_FLAG_ALL,
        import_rt, export_rt,
        rtinstance->virtual_network(), rtinstance->virtual_network_index());

    return rtinstance;
}

void RoutingInstanceMgr::UpdateRoutingInstance(
        const BgpInstanceConfig *config) {
    CHECK_CONCURRENCY("bgp::Config");

    RoutingInstance *rtinstance = GetRoutingInstance(config->name());
    if (rtinstance && rtinstance->deleted()) {
        RTINSTANCE_LOG_MESSAGE(server_,
            SandeshLevel::SYS_WARN, RTINSTANCE_LOG_FLAG_ALL, config->name(),
            "Instance is updated before pending deletion is complete");
        return;
    } else if (!rtinstance) {
        RTINSTANCE_LOG_MESSAGE(server_,
            SandeshLevel::SYS_WARN, RTINSTANCE_LOG_FLAG_ALL, config->name(),
            "Instance not found during update");
        assert(rtinstance != NULL);
    }

    InstanceTargetRemove(rtinstance);
    InstanceVnIndexRemove(rtinstance);
    rtinstance->UpdateConfig(server_, config);
    InstanceTargetAdd(rtinstance);
    InstanceVnIndexAdd(rtinstance);

    // Notify clients about routing instance create
    NotifyInstanceOp(config->name(), INSTANCE_UPDATE);

    std::vector<string> import_rt(config->import_list().begin(),
                                  config->import_list().end());
    std::vector<string> export_rt(config->export_list().begin(),
                                  config->export_list().end());
    RTINSTANCE_LOG(Update, rtinstance,
        SandeshLevel::SYS_DEBUG, RTINSTANCE_LOG_FLAG_ALL, import_rt, export_rt,
        rtinstance->virtual_network(), rtinstance->virtual_network_index());
}

//
// Concurrency: BGP Config task
//
// Trigger deletion of a particular routing-instance
//
// This involves several asynchronous steps such as
//
// 1. Close all peers (RibIn and RibOut) from every IPeerRib in the instance
// 2. Close all tables (Flush all notifications, registrations and user data)
// 3. etc.
//
void RoutingInstanceMgr::DeleteRoutingInstance(const string &name) {
    CHECK_CONCURRENCY("bgp::Config");

    RoutingInstance *rtinstance = GetRoutingInstance(name);

    // Ignore if instance is not found as it might already have been deleted.
    if (rtinstance && rtinstance->deleted()) {
        RTINSTANCE_LOG_MESSAGE(server_,
            SandeshLevel::SYS_WARN, RTINSTANCE_LOG_FLAG_ALL, name,
            "Duplicate instance delete while pending deletion");
        return;
    } else if (!rtinstance) {
        RTINSTANCE_LOG_MESSAGE(server_,
            SandeshLevel::SYS_WARN, RTINSTANCE_LOG_FLAG_ALL, name,
            "Instance not found during delete");
        assert(rtinstance != NULL);
    }

    InstanceVnIndexRemove(rtinstance);
    InstanceTargetRemove(rtinstance);
    rtinstance->ClearConfig();

    RTINSTANCE_LOG(Delete, rtinstance,
        SandeshLevel::SYS_DEBUG, RTINSTANCE_LOG_FLAG_ALL);
    rtinstance->ClearRouteTarget();

    server()->service_chain_mgr()->StopServiceChain(rtinstance);

    // Remove Static Route config
    if (rtinstance->static_route_mgr()) 
        rtinstance->static_route_mgr()->FlushStaticRouteConfig();

    NotifyInstanceOp(name, INSTANCE_DELETE);

    rtinstance->ManagedDelete();
}

//
// Concurrency: Called from BGP config task manager
//
// Destroy a routing instance from the data structures
//
void RoutingInstanceMgr::DestroyRoutingInstance(RoutingInstance *rtinstance) {
    CHECK_CONCURRENCY("bgp::Config");

    RTINSTANCE_LOG(Destroy, rtinstance,
        SandeshLevel::SYS_DEBUG, RTINSTANCE_LOG_FLAG_ALL);

    // Remove call here also deletes the instance.
    const std::string name = rtinstance->name();
    instances_.Remove(rtinstance->name(), rtinstance->index());

    if (deleted()) return;

    if (name == BgpConfigManager::kMasterInstance) return;

    const BgpInstanceConfig *config 
        = server()->config_manager()->config().FindInstance(name);
    if (config) {
        CreateRoutingInstance(config);
        return;
    }
}

class RoutingInstance::DeleteActor : public LifetimeActor {
public:
    DeleteActor(BgpServer *server, RoutingInstance *parent)
            : LifetimeActor(server->lifetime_manager()), parent_(parent) {
    }
    virtual bool MayDelete() const {
        return parent_->MayDelete();
    }
    virtual void Shutdown() {
        parent_->mgr_->NotifyInstanceOp(parent_->name(), 
                                        RoutingInstanceMgr::INSTANCE_DELETE);
        parent_->Shutdown();
    }
    virtual void Destroy() {
        parent_->mgr_->DestroyRoutingInstance(parent_);
    }

private:
    RoutingInstance *parent_;
};

RoutingInstance::RoutingInstance(std::string name, BgpServer *server,
                                 RoutingInstanceMgr *mgr,
                                 const BgpInstanceConfig *config)
    : name_(name), index_(-1), mgr_(mgr), config_(config),
      is_default_(false), virtual_network_index_(0),
      deleter_(new DeleteActor(server, this)),
      manager_delete_ref_(this, mgr->deleter()) {
      peer_manager_.reset(BgpObjectFactory::Create<PeerManager>(this));
}

RoutingInstance::~RoutingInstance() {
}

void RoutingInstance::ProcessConfig(BgpServer *server) {
    RoutingInstanceInfo info = GetDataCollection("");

    // Initialize virtual network info.
    virtual_network_ = config_->virtual_network();
    virtual_network_index_ = config_->virtual_network_index();

    std::vector<std::string> import_rt, export_rt;
    BOOST_FOREACH(string irt, config_->import_list()) {
        import_.insert(RouteTarget::FromString(irt));
        import_rt.push_back(irt);
    }
    BOOST_FOREACH(string ert, config_->export_list()) {
        export_.insert(RouteTarget::FromString(ert));
        export_rt.push_back(ert);
    }

    if (import_rt.size())
        info.set_add_import_rt(import_rt);
    if (export_rt.size())
        info.set_add_export_rt(export_rt);
    if (import_rt.size() || export_rt.size())
        ROUTING_INSTANCE_COLLECTOR_INFO(info);

    // Create BGP Table
    if (name_ == BgpConfigManager::kMasterInstance) {
        is_default_ = true;

        InetVpnTableCreate(server);
        ErmVpnTableCreate(server);
        EvpnTableCreate(server);
        RTargetTableCreate(server);

        BgpTable *table_inet = static_cast<BgpTable *>(
                server->database()->CreateTable("inet.0"));
        // TODO: log
        if (table_inet != NULL) {
            AddTable(table_inet);
        }
    } else {

        // Create foo.inet.0.
        BgpTable *table_inet = static_cast<BgpTable *>(
            server->database()->CreateTable((name_ + ".inet.0")));
        if (table_inet == NULL) {
            // TODO: log
            return;
        }
        AddTable(table_inet);
        RTINSTANCE_LOG_TABLE(Create, this, table_inet,
            SandeshLevel::SYS_DEBUG, RTINSTANCE_LOG_FLAG_ALL);

        RoutePathReplicator *inetvpn_replicator =
            server->replicator(Address::INETVPN);
        BOOST_FOREACH(RouteTarget rt, import_) {
            inetvpn_replicator->Join(table_inet, rt, true);
        }
        BOOST_FOREACH(RouteTarget rt, export_) {
            inetvpn_replicator->Join(table_inet, rt, false);
        }

        // Create foo.ermvpn.0.
        BgpTable *table_ermvpn = static_cast<BgpTable *>(
            server->database()->CreateTable((name_ + ".ermvpn.0")));
        if (table_ermvpn == NULL) {
            // TODO: log
            return;
        }
        AddTable(table_ermvpn);
        RTINSTANCE_LOG_TABLE(Create, this, table_ermvpn,
            SandeshLevel::SYS_DEBUG, RTINSTANCE_LOG_FLAG_ALL);

        RoutePathReplicator *ermvpn_replicator =
            server->replicator(Address::ERMVPN);
        BOOST_FOREACH(RouteTarget rt, import_) {
            ermvpn_replicator->Join(table_ermvpn, rt, true);
        }
        BOOST_FOREACH(RouteTarget rt, export_) {
            ermvpn_replicator->Join(table_ermvpn, rt, false);
        }

        // Create foo.enet.0.
        BgpTable *table_enet = static_cast<BgpTable *>(
            server->database()->CreateTable((name_ + ".enet.0")));
        if (table_enet == NULL) {
            // TODO: log
            return;
        }
        AddTable(table_enet);
        RTINSTANCE_LOG_TABLE(Create, this, table_enet,
            SandeshLevel::SYS_DEBUG, RTINSTANCE_LOG_FLAG_ALL);

        RoutePathReplicator *evpn_replicator =
            server->replicator(Address::EVPN);
        BOOST_FOREACH(RouteTarget rt, import_) {
            evpn_replicator->Join(table_enet, rt, true);
        }
        BOOST_FOREACH(RouteTarget rt, export_) {
            evpn_replicator->Join(table_enet, rt, false);
        }
    }

    if (config_->instance_config() == NULL) {
        return;
    }

    // Service Chain
    const autogen::ServiceChainInfo &cfg =
        config_->instance_config()->service_chain_information();
    if (cfg.routing_instance != "") {
        server->service_chain_mgr()->LocateServiceChain(this, cfg);
    }

    if (static_route_mgr())
        static_route_mgr()->ProcessStaticRouteConfig();
}

void RoutingInstance::UpdateConfig(BgpServer *server,
        const BgpInstanceConfig *cfg) {
    CHECK_CONCURRENCY("bgp::Config");

    // This is a noop in production code. However unit tests may pass a
    // new object.
    config_ = cfg;

    // Update virtual network info.
    virtual_network_ = cfg->virtual_network();
    virtual_network_index_ = cfg->virtual_network_index();

    // Master routing instance doesn't have import & export list
    // Master instance imports and exports all RT
    if (IsDefaultRoutingInstance())
        return;

    // Do a diff walk of Routing Instance config and Routing Instance.
    //
    // Note that for EVPN and ERMVPN, we use the export targets as both
    // import and import targets and ignore the routing instance import
    // targets. This is done because we do not want to leak routes between
    // VNs.
    BgpInstanceConfig::RouteTargetList::const_iterator cfg_it =
        cfg->import_list().begin();
    RoutingInstance::RouteTargetList::const_iterator rt_it = import_.begin();
    RoutingInstance::RouteTargetList::const_iterator rt_next_it = rt_it;

    BgpTable *inet_table = GetTable(Address::INET);
    RoutePathReplicator *inetvpn_replicator =
        server->replicator(Address::INETVPN);
    BgpTable *enet_table = GetTable(Address::ENET);
    RoutePathReplicator *evpn_replicator =
        server->replicator(Address::EVPN);
    BgpTable *ermvpn_table = GetTable(Address::ERMVPN);
    RoutePathReplicator *ermvpn_replicator =
        server->replicator(Address::ERMVPN);

    RoutingInstanceInfo info = GetDataCollection("");
    std::vector<std::string> add_import_rt, remove_import_rt;
    std::vector<std::string> add_export_rt, remove_export_rt;
    while ((cfg_it != cfg->import_list().end()) &&  (rt_it != import_.end())) {
        RouteTarget cfg_rtarget(RouteTarget::FromString(*cfg_it));
        if (cfg_rtarget.GetExtCommunity() < rt_it->GetExtCommunity()) {
            // If present in config and not in Routing Instance,
            //   a. Add to import list
            //   b. Add the table to import from the RouteTarget
            import_.insert(cfg_rtarget);
            add_import_rt.push_back(*cfg_it);
            inetvpn_replicator->Join(inet_table, cfg_rtarget, true);
            ermvpn_replicator->Join(ermvpn_table, cfg_rtarget, true);
            evpn_replicator->Join(enet_table, cfg_rtarget, true);
            cfg_it++;
        } else if (cfg_rtarget.GetExtCommunity() > rt_it->GetExtCommunity()) {
            // If present not present config and but in Routing Instance,
            //   a. Remove to import list
            //   b. Leave the Import RtGroup
            rt_next_it++;
            remove_import_rt.push_back(rt_it->ToString());
            inetvpn_replicator->Leave(inet_table, *rt_it, true);
            ermvpn_replicator->Leave(ermvpn_table, *rt_it, true);
            evpn_replicator->Leave(enet_table, *rt_it, true);
            import_.erase(rt_it);
            rt_it = rt_next_it;
        } else {
            // Present in both, Nop
            rt_it++;
            cfg_it++;
        }
        rt_next_it = rt_it;
    }

    // Walk through the entire left over config list and add to import list
    for (; cfg_it != cfg->import_list().end(); ++cfg_it) {
        RouteTarget cfg_rtarget(RouteTarget::FromString(*cfg_it));
        import_.insert(cfg_rtarget);
        add_import_rt.push_back(*cfg_it);
        inetvpn_replicator->Join(inet_table, cfg_rtarget, true);
        ermvpn_replicator->Join(ermvpn_table, cfg_rtarget, true);
        evpn_replicator->Join(enet_table, cfg_rtarget, true);
    }

    // Walk through the entire left over RoutingInstance import list and purge
    for (rt_next_it = rt_it; rt_it != import_.end(); rt_it = rt_next_it) {
        rt_next_it++;
        remove_import_rt.push_back(rt_it->ToString());
        inetvpn_replicator->Leave(inet_table, *rt_it, true);
        ermvpn_replicator->Leave(ermvpn_table, *rt_it, true);
        evpn_replicator->Leave(enet_table, *rt_it, true);
        import_.erase(rt_it);
    }

    // Same step for Export_rt config
    cfg_it = cfg->export_list().begin();
    rt_next_it = rt_it = export_.begin();

    while ((cfg_it != cfg->export_list().end()) &&  (rt_it != export_.end())) {
        RouteTarget cfg_rtarget(RouteTarget::FromString(*cfg_it));
        if (cfg_rtarget.GetExtCommunity() < rt_it->GetExtCommunity()) {
            export_.insert(cfg_rtarget);
            add_export_rt.push_back(*cfg_it);
            inetvpn_replicator->Join(inet_table, cfg_rtarget, false);
            ermvpn_replicator->Join(ermvpn_table, cfg_rtarget, false);
            evpn_replicator->Join(enet_table, cfg_rtarget, false);
            cfg_it++;
        } else if (cfg_rtarget.GetExtCommunity() > rt_it->GetExtCommunity()) {
            rt_next_it++;
            remove_export_rt.push_back(rt_it->ToString());
            inetvpn_replicator->Leave(inet_table, *rt_it, false);
            ermvpn_replicator->Leave(ermvpn_table, *rt_it, false);
            evpn_replicator->Leave(enet_table, *rt_it, false);
            export_.erase(rt_it);
            rt_it = rt_next_it;
        } else {
            rt_it++;
            cfg_it++;
        }
        rt_next_it = rt_it;
    }
    for (; cfg_it != cfg->export_list().end(); ++cfg_it) {
        RouteTarget cfg_rtarget(RouteTarget::FromString(*cfg_it));
        export_.insert(cfg_rtarget);
        add_export_rt.push_back(*cfg_it);
        inetvpn_replicator->Join(inet_table, cfg_rtarget, false);
        ermvpn_replicator->Join(ermvpn_table, cfg_rtarget, false);
        evpn_replicator->Join(enet_table, cfg_rtarget, false);
    }
    for (rt_next_it = rt_it; rt_it != export_.end(); rt_it = rt_next_it) {
        rt_next_it++;
        remove_export_rt.push_back(rt_it->ToString());
        inetvpn_replicator->Leave(inet_table, *rt_it, false);
        ermvpn_replicator->Leave(ermvpn_table, *rt_it, false);
        evpn_replicator->Leave(enet_table, *rt_it, false);
        export_.erase(rt_it);
    }

    if (add_import_rt.size())
        info.set_add_import_rt(add_import_rt);
    if (remove_import_rt.size())
        info.set_remove_import_rt(remove_import_rt);
    if (add_export_rt.size())
        info.set_add_export_rt(add_export_rt);
    if (remove_export_rt.size())
        info.set_remove_export_rt(remove_export_rt);
    if (add_import_rt.size() || remove_import_rt.size() ||
        add_export_rt.size() || remove_export_rt.size())
        ROUTING_INSTANCE_COLLECTOR_INFO(info);

    //
    // Service Chain update
    //
    if (cfg->instance_config() == NULL) {
        server->service_chain_mgr()->StopServiceChain(this);
        if (static_route_mgr())
            static_route_mgr()->FlushStaticRouteConfig();
        return;
    }

    const autogen::ServiceChainInfo &service_chain_cfg =
        cfg->instance_config()->service_chain_information();
    if (service_chain_cfg.routing_instance != "") {
        server->service_chain_mgr()->LocateServiceChain(this,
                                                        service_chain_cfg);
    }

    if (static_route_mgr())
        static_route_mgr()->UpdateStaticRouteConfig();
}

void RoutingInstance::ClearConfig() {
    CHECK_CONCURRENCY("bgp::Config");
    config_ = NULL;
}

void RoutingInstance::ManagedDelete() {
    // RoutingInstanceMgr logs the delete for non-default instances.
    if (IsDefaultRoutingInstance()) {
        RTINSTANCE_LOG(Delete, this,
            SandeshLevel::SYS_DEBUG, RTINSTANCE_LOG_FLAG_ALL);
    }
    deleter_->Delete();
}

void RoutingInstance::Shutdown() {
    CHECK_CONCURRENCY("bgp::Config");

    RTINSTANCE_LOG(Shutdown, this,
        SandeshLevel::SYS_DEBUG, RTINSTANCE_LOG_FLAG_ALL);

    ClearRouteTarget();
    server()->service_chain_mgr()->StopServiceChain(this);

    if (static_route_mgr()) 
        static_route_mgr()->FlushStaticRouteConfig();

}

bool RoutingInstance::MayDelete() const {
    return true;
}

LifetimeActor *RoutingInstance::deleter() {
    return deleter_.get();
}

bool RoutingInstance::deleted() {
    return deleter()->IsDeleted();
}

const string RoutingInstance::virtual_network() const {
    return virtual_network_.empty() ? "unresolved" : virtual_network_;
}

int RoutingInstance::virtual_network_index() const {
    return virtual_network_index_;
}

BgpServer *RoutingInstance::server() {
    return mgr_->server();
}

void RoutingInstance::ClearRouteTarget() {
    CHECK_CONCURRENCY("bgp::Config");
    if (IsDefaultRoutingInstance()) {
        return;
    }

    BgpTable *inet_table = GetTable(Address::INET);
    if (inet_table == NULL) {
        return;
    }
    RoutePathReplicator *inetvpn_replicator = server()->replicator(Address::INETVPN);
    BOOST_FOREACH(RouteTarget rt, import_) {
        inetvpn_replicator->Leave(inet_table, rt, true);
    }
    BOOST_FOREACH(RouteTarget rt, export_) {
        inetvpn_replicator->Leave(inet_table, rt, false);
    }

    BgpTable *ermvpn_table = GetTable(Address::ERMVPN);
    if (ermvpn_table == NULL) {
        return;
    }
    RoutePathReplicator *ermvpn_replicator =
        server()->replicator(Address::ERMVPN);
    BOOST_FOREACH(RouteTarget rt, import_) {
        ermvpn_replicator->Leave(ermvpn_table, rt, true);
    }
    BOOST_FOREACH(RouteTarget rt, export_) {
        ermvpn_replicator->Leave(ermvpn_table, rt, false);
    }

    BgpTable *enet_table = GetTable(Address::ENET);
    if (enet_table == NULL) {
        return;
    }
    RoutePathReplicator *evpn_replicator =
        server()->replicator(Address::EVPN);
    BOOST_FOREACH(RouteTarget rt, import_) {
        evpn_replicator->Leave(enet_table, rt, true);
    }
    BOOST_FOREACH(RouteTarget rt, export_) {
        evpn_replicator->Leave(enet_table, rt, false);
    }

    import_.clear();
    export_.clear();
}

BgpTable *RoutingInstance::InetVpnTableCreate(BgpServer *server) {
    BgpTable *vpntbl = static_cast<BgpTable *>(
            server->database()->CreateTable("bgp.l3vpn.0"));

    RTINSTANCE_LOG_TABLE(Create, this, vpntbl,
        SandeshLevel::SYS_DEBUG, RTINSTANCE_LOG_FLAG_ALL);

    AddTable(vpntbl);

    // For all the RouteTarget in the server, add the VPN table as
    // importer and exporter
    RoutePathReplicator *replicator = server->replicator(Address::INETVPN);
    for (RTargetGroupMgr::RtGroupMap::iterator it =
         server->rtarget_group_mgr()->GetRtGroupMap().begin();
        it != server->rtarget_group_mgr()->GetRtGroupMap().end(); ++it) {
        RtGroup *group = it->second;
        if ((group->GetImportTables(Address::INETVPN).size() == 0) &&
            (group->GetExportTables(Address::INETVPN).size() == 0)) 
            continue;
        replicator->Join(vpntbl, it->first, true);
        replicator->Join(vpntbl, it->first, false);
    }
    return vpntbl;
}

BgpTable *RoutingInstance::RTargetTableCreate(BgpServer *server) {
    BgpTable *rtargettbl = static_cast<BgpTable *>(
            server->database()->CreateTable("bgp.rtarget.0"));

    RTINSTANCE_LOG_TABLE(Create, this, rtargettbl,
        SandeshLevel::SYS_DEBUG, RTINSTANCE_LOG_FLAG_ALL);

    AddTable(rtargettbl);

    return rtargettbl;
}

BgpTable *RoutingInstance::EvpnTableCreate(BgpServer *server) {
    BgpTable *vpntbl = static_cast<BgpTable *>(
            server->database()->CreateTable("bgp.evpn.0"));

    RTINSTANCE_LOG_TABLE(Create, this, vpntbl,
        SandeshLevel::SYS_DEBUG, RTINSTANCE_LOG_FLAG_ALL);

    AddTable(vpntbl);

    // For all the RouteTarget in the server, add the VPN table as
    // importer and exporter
    RoutePathReplicator *replicator = server->replicator(Address::EVPN);
    for (RTargetGroupMgr::RtGroupMap::iterator it =
         server->rtarget_group_mgr()->GetRtGroupMap().begin();
        it != server->rtarget_group_mgr()->GetRtGroupMap().end(); ++it) {
        RtGroup *group = it->second;
        if ((group->GetImportTables(Address::EVPN).size() == 0) &&
            (group->GetExportTables(Address::EVPN).size() == 0)) 
            continue;
        replicator->Join(vpntbl, it->first, true);
        replicator->Join(vpntbl, it->first, false);
    }
    return vpntbl;
}

BgpTable *RoutingInstance::ErmVpnTableCreate(BgpServer *server) {
    BgpTable *vpntbl = static_cast<BgpTable *>(
            server->database()->CreateTable("bgp.ermvpn.0"));

    RTINSTANCE_LOG_TABLE(Create, this, vpntbl,
        SandeshLevel::SYS_DEBUG, RTINSTANCE_LOG_FLAG_ALL);

    AddTable(vpntbl);

    // For all the RouteTarget in the server, add the VPN table as
    // importer and exporter
    RoutePathReplicator *replicator = server->replicator(Address::ERMVPN);
    for (RTargetGroupMgr::RtGroupMap::iterator it =
         server->rtarget_group_mgr()->GetRtGroupMap().begin();
        it != server->rtarget_group_mgr()->GetRtGroupMap().end(); ++it) {
        RtGroup *group = it->second;
        if ((group->GetImportTables(Address::ERMVPN).size() == 0) &&
            (group->GetExportTables(Address::ERMVPN).size() == 0))
            continue;
        replicator->Join(vpntbl, it->first, true);
        replicator->Join(vpntbl, it->first, false);
    }
    return vpntbl;
}

void RoutingInstance::AddTable(BgpTable *tbl) {
    vrf_table_.insert(std::make_pair(tbl->name(), tbl));
    tbl->set_routing_instance(this);
    RoutingInstanceInfo info = GetDataCollection("Add");
    info.set_family(Address::FamilyToString(tbl->family()));
    ROUTING_INSTANCE_COLLECTOR_INFO(info);
}

void RoutingInstance::RemoveTable(BgpTable *tbl) {
    RoutingInstanceInfo info = GetDataCollection("Remove");
    info.set_family(Address::FamilyToString(tbl->family()));
    vrf_table_.erase(tbl->name());
    ROUTING_INSTANCE_COLLECTOR_INFO(info);
}

//
// Concurrency: BGP Config task
//
// Remove the table from the map and delete the table data structure
//
void RoutingInstance::DestroyDBTable(DBTable *dbtable) {
    CHECK_CONCURRENCY("bgp::Config");

    BgpTable *table = static_cast<BgpTable *>(dbtable);

    RTINSTANCE_LOG_TABLE(Destroy, this, table,
        SandeshLevel::SYS_DEBUG, RTINSTANCE_LOG_FLAG_ALL);

    //
    // Remove this table from various data structures
    //
    server()->database()->RemoveTable(table);
    RemoveTable(table);


    //
    // Make sure that there are no routes left in this table
    //
    assert(table->Size() == 0);

    delete table;
}

std::string RoutingInstance::GetTableNameFromVrf(std::string name, 
                                          Address::Family fmly) {
    std::string table_name;
    if (name == BgpConfigManager::kMasterInstance) {
        if (fmly == Address::INETVPN) {
            table_name = "bgp.l3vpn.0";
        } else if (fmly == Address::ERMVPN) {
            table_name = "bgp.ermvpn.0";
        } else if (fmly == Address::EVPN) {
            table_name = "bgp.evpn.0";
        } else if (fmly == Address::RTARGET) {
            table_name = "bgp.rtarget.0";
        } else {
            table_name = Address::FamilyToTableString(fmly) + ".0";
        }
    } else {
        table_name = name + "." + Address::FamilyToTableString(fmly) + ".0";
    }
    return table_name;
}

BgpTable *RoutingInstance::GetTable(Address::Family fmly) {
    std::string table_name = RoutingInstance::GetTableNameFromVrf(name_, fmly);
    RouteTableList::const_iterator loc = GetTables().find(table_name);
    if (loc != GetTables().end()) {
        return loc->second;
    }
    return NULL;
}

string RoutingInstance::GetVrfFromTableName(const string table) {
    static set<string> master_tables = list_of("inet.0");
    static set<string> vpn_tables =
        list_of("bgp.l3vpn.0")("bgp.ermvpn.0")("bgp.evpn.0")("bgp.rtarget.0");

    if (master_tables.find(table) != master_tables.end())
        return BgpConfigManager::kMasterInstance;
    if (vpn_tables.find(table) != vpn_tables.end())
        return BgpConfigManager::kMasterInstance;

    size_t pos1 = table.rfind('.');
    if (pos1 == string::npos)
        return "__unknown__";
    size_t pos2 = table.rfind('.', pos1 - 1);
    if (pos2 == string::npos)
        return "__unknown__";

    return table.substr(0, pos2);
}

void RoutingInstance::set_index(BgpServer *server, int index) {
    index_ = index;
    if (!is_default_) {
        rd_.reset(new RouteDistinguisher(server->bgp_identifier(), index));

        static_route_mgr_.reset(new StaticRouteMgr(this));
    }
}

RoutingInstanceInfo RoutingInstance::GetDataCollection(const char *operation) {
    RoutingInstanceInfo info;
    info.set_name(name_);
    info.set_hostname(mgr_->server()->localname());
    if (rd_.get()) info.set_route_distinguisher(rd_->ToString());
    if (operation) info.set_operation(operation);
    return info;
}

//
// Return true if one of the route targets in the ExtCommunity is in the
// set of export RouteTargets for this RoutingInstance.
//
bool RoutingInstance::HasExportTarget(const ExtCommunity *extcomm) const {
    if (!extcomm)
        return false;

    BOOST_FOREACH(const ExtCommunity::ExtCommunityValue &value,
                  extcomm->communities()) {
        if (!ExtCommunity::is_route_target(value))
            continue;
        RouteTarget rtarget(value);
        if (export_.find(rtarget) != export_.end())
            return true;
    }

    return false;
}
