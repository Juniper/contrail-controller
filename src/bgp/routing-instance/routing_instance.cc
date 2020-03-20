/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/routing-instance/routing_instance.h"

#include <boost/foreach.hpp>
#include <boost/assign/list_of.hpp>
#include <boost/functional/hash.hpp>

#include <algorithm>

#include "base/set_util.h"
#include "base/task_annotations.h"
#include "base/task_trigger.h"
#include "bgp/bgp_config.h"
#include "bgp/bgp_factory.h"
#include "bgp/bgp_log.h"
#include "bgp/bgp_mvpn.h"
#include "bgp/bgp_server.h"
#include "bgp/mvpn/mvpn_table.h"
#include "bgp/routing-instance/iroute_aggregator.h"
#include "bgp/routing-instance/istatic_route_mgr.h"
#include "bgp/routing-instance/peer_manager.h"
#include "bgp/routing-instance/routepath_replicator.h"
#include "bgp/routing-instance/routing_instance_log.h"
#include "bgp/routing-instance/routing_table_types.h"
#include "bgp/routing-policy/routing_policy.h"
#include "bgp/routing-instance/rtarget_group_mgr.h"
#include "bgp/rtarget/rtarget_route.h"

using boost::assign::list_of;
using boost::system::error_code;
using boost::tie;
using std::make_pair;
using std::ostringstream;
using std::set;
using std::string;
using std::vector;

SandeshTraceBufferPtr RoutingInstanceTraceBuf(
        SandeshTraceBufferCreate(RTINSTANCE_TRACE_BUF, 1000));

class RoutingInstanceMgr::DeleteActor : public LifetimeActor {
public:
    explicit DeleteActor(RoutingInstanceMgr *manager)
        : LifetimeActor(manager->server_->lifetime_manager()),
          manager_(manager) {
    }
    virtual bool MayDelete() const {
        return manager_->MayDelete();
    }
    virtual void Shutdown() {
        manager_->Shutdown();
    }
    virtual void Destroy() {
        manager_->server_delete_ref_.Reset(NULL);
    }

private:
    RoutingInstanceMgr *manager_;
};

RoutingInstanceMgr::RoutingInstanceMgr(BgpServer *server) :
        server_(server),
        instance_config_lists_(
            TaskScheduler::GetInstance()->HardwareThreadCount()),
        default_rtinstance_(NULL),
        deleted_count_(0),
        asn_listener_id_(server->RegisterASNUpdateCallback(
            boost::bind(&RoutingInstanceMgr::ASNUpdateCallback, this, _1, _2))),
        identifier_listener_id_(server->RegisterIdentifierUpdateCallback(
            boost::bind(&RoutingInstanceMgr::IdentifierUpdateCallback,
                this, _1))),
        dormant_trace_buf_size_(
                GetEnvRoutingInstanceDormantTraceBufferCapacity()),
        trace_buf_threshold_(
                GetEnvRoutingInstanceDormantTraceBufferThreshold()),
        deleter_(new DeleteActor(this)),
        server_delete_ref_(this, server->deleter()) {
    int task_id = TaskScheduler::GetInstance()->GetTaskId("bgp::ConfigHelper");
    int hw_thread_count = TaskScheduler::GetInstance()->HardwareThreadCount();
    for (int idx = 0; idx < hw_thread_count; ++idx) {
        instance_config_triggers_.push_back(new TaskTrigger(boost::bind(
            &RoutingInstanceMgr::ProcessInstanceConfigList, this, idx),
            task_id, idx));
    }
    neighbor_config_trigger_.reset(new TaskTrigger(
        boost::bind(&RoutingInstanceMgr::ProcessNeighborConfigList, this),
        TaskScheduler::GetInstance()->GetTaskId("bgp::Config"), 0));
}

RoutingInstanceMgr::~RoutingInstanceMgr() {
    assert(mvpn_project_managers_.empty());
    assert(virtual_networks_.empty());
    assert(deleted_count_ == 0);
    server_->UnregisterASNUpdateCallback(asn_listener_id_);
    server_->UnregisterIdentifierUpdateCallback(identifier_listener_id_);
    STLDeleteValues(&instance_config_triggers_);
}

size_t RoutingInstanceMgr::GetMvpnProjectManagerCount(
            const string &network) const {
    tbb::mutex::scoped_lock lock(mutex_);

    MvpnProjectManagerNetworks::const_iterator iter =
        mvpn_project_managers_.find(network);
    if (iter == mvpn_project_managers_.end())
        return 0;
    return iter->second.size();
}

void RoutingInstanceMgr::ManagedDelete() {
    deleter_->Delete();
}

void RoutingInstanceMgr::Shutdown() {
    for (RoutingInstanceIterator it = begin(); it != end(); ++it) {
        InstanceTargetRemove(it->second);
        InstanceVnIndexRemove(it->second);
    }
}

bool RoutingInstanceMgr::MayDelete() const {
    if (!neighbor_config_list_.empty())
        return false;
    for (size_t idx = 0; idx < instance_config_lists_.size(); ++idx) {
        if (!instance_config_lists_[idx].empty())
            return false;
    }
    return true;
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
// Ignore export targets that are not in the import list.  This is done since
// export only targets are managed by user and the same export only target can
// be configured on multiple virtual networks.
//
void RoutingInstanceMgr::InstanceTargetAdd(RoutingInstance *rti) {
    tbb::mutex::scoped_lock lock(mutex_);
    for (RoutingInstance::RouteTargetList::const_iterator it =
         rti->GetExportList().begin(); it != rti->GetExportList().end(); ++it) {
        if (rti->GetImportList().find(*it) == rti->GetImportList().end())
            continue;
        target_map_.insert(make_pair(*it, rti));
    }
}

//
// Go through all export targets for the RoutingInstance and remove the entry
// for each one from the InstanceTargetMap.  Note that there may be multiple
// entries in the InstanceTargetMap for a given export target.  Hence we need
// to make sure that we only remove the entry that matches the RoutingInstance.
//
// Ignore export targets that are not in the import list. They shouldn't be in
// in the map because of the same check in InstanceTargetAdd.
//
void RoutingInstanceMgr::InstanceTargetRemove(const RoutingInstance *rti) {
    tbb::mutex::scoped_lock lock(mutex_);
    for (RoutingInstance::RouteTargetList::const_iterator it =
         rti->GetExportList().begin(); it != rti->GetExportList().end(); ++it) {
        if (rti->GetImportList().find(*it) == rti->GetImportList().end())
            continue;
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
    tbb::mutex::scoped_lock lock(mutex_);
    if (rti->virtual_network_index())
        vn_index_map_.insert(make_pair(rti->virtual_network_index(), rti));
}

//
// Remove the entry for the vn index from the VnIndexMap.  Note that there may
// be multiple entries in the VnIndexMap for a given vn index target. Hence we
// need to make sure that we remove the entry that matches the RoutingInstance.
//
void RoutingInstanceMgr::InstanceVnIndexRemove(const RoutingInstance *rti) {
    tbb::mutex::scoped_lock lock(mutex_);
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
string RoutingInstanceMgr::GetVirtualNetworkByVnIndex(
        int vn_index) const {
    const RoutingInstance *rti = GetInstanceByVnIndex(vn_index);
    return rti ? rti->GetVirtualNetworkName() : "unresolved";
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
    tbb::mutex::scoped_lock lock(mutex_);
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
    tbb::mutex::scoped_lock lock(mutex_);
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

void RoutingInstanceMgr::NotifyInstanceOp(string name, Operation op) {
    tbb::mutex::scoped_lock lock(mutex_);
    for (InstanceOpListenersList::iterator iter = callbacks_.begin();
         iter != callbacks_.end(); ++iter) {
        if (*iter != NULL) {
            RoutingInstanceCb cb = *iter;
            (cb)(name, op);
        }
    }
}

bool RoutingInstanceMgr::ProcessInstanceConfigList(int idx) {
    if (deleted()) {
        deleter()->RetryDelete();
    } else {
        BOOST_FOREACH(const string &name, instance_config_lists_[idx]) {
            const BgpInstanceConfig *config =
                server()->config_manager()->FindInstance(name);
            if (!config)
                continue;
            LocateRoutingInstance(config);
        }
    }

    instance_config_lists_[idx].clear();
    return true;
}

RoutingInstance *RoutingInstanceMgr::GetDefaultRoutingInstance() {
    return default_rtinstance_;
}

const RoutingInstance *RoutingInstanceMgr::GetDefaultRoutingInstance() const {
    return default_rtinstance_;
}

RoutingInstance *RoutingInstanceMgr::GetRoutingInstance(const string &name) {
    RoutingInstanceList::iterator iter = instances_.find(name);
    if (iter != instances_.end())
        return iter->second;
    return NULL;
}

const RoutingInstance *RoutingInstanceMgr::GetRoutingInstance(
    const string &name) const {
    RoutingInstanceList::const_iterator iter = instances_.find(name);
    if (iter != instances_.end())
        return iter->second;
    return NULL;
}

RoutingInstance *RoutingInstanceMgr::GetRoutingInstanceLocked(
    const string &name) {
    tbb::mutex::scoped_lock lock(mutex_);
    RoutingInstanceList::iterator iter = instances_.find(name);
    if (iter != instances_.end())
        return iter->second;
    return NULL;
}

void RoutingInstanceMgr::InsertRoutingInstance(RoutingInstance *rtinstance) {
    tbb::mutex::scoped_lock lock(mutex_);
    instances_.insert(make_pair(rtinstance->config()->name(),
                rtinstance));
}

void RoutingInstanceMgr::LocateRoutingInstance(
    const BgpInstanceConfig *config) {
    RoutingInstance *rtinstance = GetRoutingInstanceLocked(config->name());
    if (rtinstance) {
        if (rtinstance->deleted()) {
            RTINSTANCE_LOG_WARNING_MESSAGE(server_,
                RTINSTANCE_LOG_FLAG_ALL, rtinstance->GetVirtualNetworkName(),
                config->name(),
                "Instance recreated before pending deletion is complete");
        } else {
            UpdateRoutingInstance(rtinstance, config);
        }
    } else {
        rtinstance = CreateRoutingInstance(config);
    }
}

void RoutingInstanceMgr::LocateRoutingInstance(const string &name) {
    static boost::hash<string> string_hash;

    if (name == BgpConfigManager::kMasterInstance) {
        const BgpInstanceConfig *config =
            server()->config_manager()->FindInstance(name);
        assert(config);
        LocateRoutingInstance(config);
    } else {
        size_t idx = string_hash(name) % instance_config_lists_.size();
        instance_config_lists_[idx].insert(name);
        instance_config_triggers_[idx]->Set();
    }
}

// Update VirtualNetwork to RoutingInstance name mapping.
bool RoutingInstanceMgr::CreateVirtualNetworkMapping(
        const string &virtual_network, const string &instance_name) {
    tbb::mutex::scoped_lock lock(mutex_);

    VirtualNetworksMap::iterator iter;
    bool inserted;
    tie(iter, inserted) = virtual_networks_.insert(make_pair(virtual_network,
                                                             set<string>()));
    assert(iter->second.insert(instance_name).second);
    return true;
}

bool RoutingInstanceMgr::DeleteVirtualNetworkMapping(
        const string &virtual_network, const string &instance_name) {
    tbb::mutex::scoped_lock lock(mutex_);

    VirtualNetworksMap::iterator iter = virtual_networks_.find(virtual_network);
    assert(iter != virtual_networks_.end());

    // Delete this instance from the set of virtual-network to instances map.
    assert(iter->second.erase(instance_name) == 1);

    RoutingInstanceStatsData instance_info;
    bool mapping_deleted = iter->second.empty();

    // Delete the mapping if the member set is empty.
    if (mapping_deleted) {
        virtual_networks_.erase(iter);

        // Delete the virtual-network specific UVE.
        instance_info.set_deleted(true);
    } else {

        // Delete this instance's uve, by deleting all table stats.
        map<string, RoutingTableStats> stats_map;
        stats_map.insert(make_pair(instance_name, RoutingTableStats()));
        stats_map[instance_name].set_deleted(true);

        // Delete all uve stats across all address families.
        for (Address::Family i = Address::UNSPEC; i < Address::NUM_FAMILIES;
                i = static_cast<Address::Family>(i + 1)) {
            SetTableStatsUve(i, stats_map, &instance_info);
        }
    }

    // Send delete uve.
    instance_info.set_name(virtual_network);
    assert(!instance_info.get_name().empty());
    BGP_UVE_SEND(RoutingInstanceStats, instance_info);

    return mapping_deleted;
}

void RoutingInstanceMgr::SetTableStatsUve(Address::Family family,
                             const map<string, RoutingTableStats> &stats_map,
                             RoutingInstanceStatsData *instance_info) const {
    // By switching over all families, we get compiler to tell us in future,
    // when ever familiy enum list gets modified.
    switch (family) {
    case Address::UNSPEC:
        break;
    case Address::INET:
    case Address::INETMPLS:
        instance_info->set_raw_ipv4_stats(stats_map);
        break;
    case Address::INET6:
        instance_info->set_raw_ipv6_stats(stats_map);
        break;
    case Address::INETVPN:
        instance_info->set_raw_inetvpn_stats(stats_map);
        break;
    case Address::INET6VPN:
        instance_info->set_raw_inet6vpn_stats(stats_map);
        break;
    case Address::RTARGET:
        instance_info->set_raw_rtarget_stats(stats_map);
        break;
    case Address::EVPN:
        instance_info->set_raw_evpn_stats(stats_map);
        break;
    case Address::ERMVPN:
        instance_info->set_raw_ermvpn_stats(stats_map);
        break;
    case Address::MVPN:
        instance_info->set_raw_mvpn_stats(stats_map);
        break;
    case Address::NUM_FAMILIES:
        break;
    }
}

uint32_t RoutingInstanceMgr::SendTableStatsUve() {
    uint32_t out_q_depth = 0;

    for (RoutingInstanceIterator rit = begin(); rit != end(); ++rit) {
        RoutingInstanceStatsData instance_info;
        map<string, RoutingTableStats> stats_map;

        stats_map.insert(make_pair(rit->second->name(), RoutingTableStats()));
        RoutingInstance::RouteTableList const rt_list =
                                         rit->second->GetTables();

        // Prepare and send Statistics UVE for each routing-instance.
        for (RoutingInstance::RouteTableList::const_iterator it =
             rt_list.begin(); it != rt_list.end(); ++it) {
            BgpTable *table = it->second;

            size_t markers;
            out_q_depth += table->GetPendingRiboutsCount(&markers);

            stats_map[rit->second->name()].set_prefixes(table->Size());
            stats_map[rit->second->name()].set_primary_paths(
                table->GetPrimaryPathCount());
            stats_map[rit->second->name()].set_secondary_paths(
                table->GetSecondaryPathCount());
            stats_map[rit->second->name()].set_infeasible_paths(
                table->GetInfeasiblePathCount());

            uint64_t total_paths = table->GetPrimaryPathCount() +
                                   table->GetSecondaryPathCount() +
                                   table->GetInfeasiblePathCount();
            stats_map[rit->second->name()].set_total_paths(total_paths);
            SetTableStatsUve(table->family(), stats_map, &instance_info);
        }

        // Set the primary key and trigger uve send.
        instance_info.set_name(rit->second->GetVirtualNetworkName());
        assert(!instance_info.get_name().empty());
        BGP_UVE_SEND(RoutingInstanceStats, instance_info);
    }

    return out_q_depth;
}

RoutingInstance *RoutingInstanceMgr::CreateRoutingInstance(
    const BgpInstanceConfig *config) {
    RoutingInstance *rtinstance = BgpObjectFactory::Create<RoutingInstance>(
        config->name(), server_, this, config);
    uint32_t ri_index = config->index();
    if (config->name() == BgpConfigManager::kMasterInstance) {
        default_rtinstance_ = rtinstance;
        ri_index = 0;
    }

    rtinstance->ProcessConfig();
    InsertRoutingInstance(rtinstance);
    rtinstance->set_index(ri_index);

    InstanceTargetAdd(rtinstance);
    InstanceVnIndexAdd(rtinstance);
    rtinstance->InitAllRTargetRoutes(server_->local_autonomous_system());

    // Notify clients about routing instance create
    NotifyInstanceOp(config->name(), INSTANCE_ADD);

    vector<string> import_rt(config->import_list().begin(),
                                  config->import_list().end());
    vector<string> export_rt(config->export_list().begin(),
                                  config->export_list().end());
    RTINSTANCE_LOG(Create, rtinstance,
        SandeshLevel::SYS_DEBUG, RTINSTANCE_LOG_FLAG_ALL,
        import_rt, export_rt, rtinstance->virtual_network_index());

    // Schedule creation of neighbors for this instance.
    CreateRoutingInstanceNeighbors(config);

    //  Update instance to virtual-network mapping structures.
    CreateVirtualNetworkMapping(rtinstance->GetVirtualNetworkName(),
                                rtinstance->name());

    // Create MvpnManager for all children mvpn networks.
    MvpnTable *mvpn_table =
        dynamic_cast<MvpnTable *>(rtinstance->GetTable(Address::MVPN));
    mvpn_table->CreateMvpnManagers();
    return rtinstance;
}

void RoutingInstanceMgr::UpdateRoutingInstance(RoutingInstance *rtinstance,
    const BgpInstanceConfig *config) {
    bool old_always_subscribe = rtinstance->always_subscribe();

    // Note that InstanceTarget[Remove|Add] depend on the RouteTargetList in
    // the RoutingInstance.
    InstanceTargetRemove(rtinstance);
    InstanceVnIndexRemove(rtinstance);
    rtinstance->UpdateConfig(config);
    InstanceTargetAdd(rtinstance);
    InstanceVnIndexAdd(rtinstance);

    if (old_always_subscribe != rtinstance->always_subscribe()) {
        rtinstance->FlushAllRTargetRoutes(server_->local_autonomous_system());
        rtinstance->InitAllRTargetRoutes(server_->local_autonomous_system());
    }

    // Notify clients about routing instance create
    NotifyInstanceOp(config->name(), INSTANCE_UPDATE);

    vector<string> import_rt(config->import_list().begin(),
                                  config->import_list().end());
    vector<string> export_rt(config->export_list().begin(),
                                  config->export_list().end());
    RTINSTANCE_LOG(Update, rtinstance,
        SandeshLevel::SYS_DEBUG, RTINSTANCE_LOG_FLAG_ALL, import_rt, export_rt,
        rtinstance->virtual_network_index());
}

//
// Concurrency: BGP Config task
//
// Trigger deletion of a particular routing-instance
//
// This involves several asynchronous steps such as deleting all tables in
// routing-instance (flush all notifications, registrations and user data).
//
void RoutingInstanceMgr::DeleteRoutingInstance(const string &name) {
    CHECK_CONCURRENCY("bgp::Config");

    RoutingInstance *rtinstance = GetRoutingInstance(name);

    // Ignore if instance is not found as it might already have been deleted.
    if (rtinstance && rtinstance->deleted()) {
        RTINSTANCE_LOG_WARNING_MESSAGE(server_, RTINSTANCE_LOG_FLAG_ALL,
            rtinstance->GetVirtualNetworkName(), name,
            "Duplicate instance delete while pending deletion");
        return;
    } else if (!rtinstance) {
        RTINSTANCE_LOG_WARNING_MESSAGE(server_, RTINSTANCE_LOG_FLAG_ALL,
            name, name, "Instance not found during delete");
        return;
    }

    InstanceVnIndexRemove(rtinstance);
    InstanceTargetRemove(rtinstance);
    rtinstance->ClearConfig();

    RTINSTANCE_LOG(Delete, rtinstance,
        SandeshLevel::SYS_DEBUG, RTINSTANCE_LOG_FLAG_ALL);

    rtinstance->ProcessServiceChainConfig();
    rtinstance->FlushStaticRouteConfig();
    rtinstance->FlushRouteAggregationConfig();

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

    DeleteVirtualNetworkMapping(rtinstance->GetVirtualNetworkName(),
                                rtinstance->name());

    // instance should also be deleted here
    const string name = rtinstance->name();
    RoutingInstanceList::iterator loc = instances_.find(rtinstance->name());
    assert(loc != instances_.end());
    instances_.erase(loc);
    int index = rtinstance->index();
    delete rtinstance;
    // disable the TraceBuf for the deleted RoutingInstance
    DisableTraceBuffer(name);
    // index was allocated in Config Manager so needs to be freed
    if (index >= 0)
        server()->config_manager()->ResetRoutingInstanceIndexBit(index);

    if (deleted())
        return;

    if (rtinstance == default_rtinstance_) {
        default_rtinstance_ = NULL;
        return;
    }

    const BgpInstanceConfig *config
        = server()->config_manager()->FindInstance(name);
    if (config) {
        CreateRoutingInstance(config);
        return;
    }
}

SandeshTraceBufferPtr RoutingInstanceMgr::LocateTraceBuffer(
        const std::string &name) {
    SandeshTraceBufferPtr trace_buf;

    trace_buf = GetActiveTraceBuffer(name);
    if (trace_buf == NULL) {
        trace_buf = GetTraceBuffer(name);
    }

    return trace_buf;
}

SandeshTraceBufferPtr RoutingInstanceMgr::GetTraceBuffer(
        const std::string &name) {
    tbb::spin_rw_mutex::scoped_lock write_lock(rw_mutex_, true);
    SandeshTraceBufferPtr trace_buf;
    RoutingInstanceTraceBufferMap::iterator iter;

    iter = trace_buffer_active_.find(name);
    if (iter != trace_buffer_active_.end()) {
        return iter->second;
    }

    iter = trace_buffer_dormant_.find(name);
    if (iter != trace_buffer_dormant_.end()) {
        // tracebuf was created for this RoutingInstance in its prior
        // incarnation
        trace_buf = iter->second;
        trace_buffer_dormant_.erase(iter);
        trace_buffer_dormant_list_.remove(name);
        trace_buffer_active_.insert(make_pair(name, trace_buf));
        return trace_buf;
    }

    // create new sandesh Buffer
    trace_buf = SandeshTraceBufferCreate(
                                  name + RTINSTANCE_TRACE_BUF, 1000);
    trace_buffer_active_.insert(make_pair(name, trace_buf));

    return trace_buf;
}

void RoutingInstanceMgr::DisableTraceBuffer(const std::string &name) {
    tbb::spin_rw_mutex::scoped_lock write_lock(rw_mutex_, true);
    SandeshTraceBufferPtr trace_buf;
    SandeshTraceBufferPtr trace_buf_delete;
    RoutingInstanceTraceBufferMap::iterator iter;
    std::pair<RoutingInstanceTraceBufferMap::iterator, bool> ret;
    std::string ri_name;

    // Moving the trace buff from Active map to Dormant map
    iter = trace_buffer_active_.find(name);
    if (iter != trace_buffer_active_.end()) {
        trace_buf = iter->second;
        trace_buffer_active_.erase(iter);
        if (dormant_trace_buf_size_) {
            // Move to Dormant map.
            if (trace_buffer_dormant_.size() >= dormant_trace_buf_size_) {
                // Make room in the Dormant map, so creating by:
                // 1. Delete oldest entries in dormant map
                // 2. Insert the new entry.
                size_t del_count = std::min(trace_buffer_dormant_list_.size(),
                                               trace_buf_threshold_);
                if (!del_count) {
                    // retain the latest deleted RI's TraceBuf
                    del_count = 1;
                }
                for (size_t i = 0; i < del_count; i++) {
                    iter = trace_buffer_dormant_.find(
                            trace_buffer_dormant_list_.front());
                    assert(iter != trace_buffer_dormant_.end());
                    trace_buf_delete = iter->second;
                    trace_buffer_dormant_.erase(iter);
                    trace_buffer_dormant_list_.pop_front();
                    trace_buf_delete.reset();
                }
            }
            ret = trace_buffer_dormant_.insert(make_pair(name, trace_buf));
            assert(ret.second == true);
            trace_buffer_dormant_list_.push_back(name);
        }
    }

    return;
}

SandeshTraceBufferPtr
RoutingInstanceMgr::GetActiveTraceBuffer(const std::string &name) const {
    tbb::spin_rw_mutex::scoped_lock read_lock(rw_mutex_, false);
    RoutingInstanceTraceBufferMap::const_iterator iter =
                                         trace_buffer_active_.find(name);

    if (iter != trace_buffer_active_.end()) {
        return iter->second;
    } else {
        return SandeshTraceBufferPtr();
    }
}

SandeshTraceBufferPtr
RoutingInstanceMgr::GetDormantTraceBuffer(const std::string &name) const {
    tbb::spin_rw_mutex::scoped_lock read_lock(rw_mutex_, false);
    RoutingInstanceTraceBufferMap::const_iterator iter =
                                         trace_buffer_dormant_.find(name);

    if (iter != trace_buffer_dormant_.end()) {
        return iter->second;
    } else {
        return SandeshTraceBufferPtr();
    }
}

bool RoutingInstanceMgr::HasRoutingInstanceActiveTraceBuf(const std::string
                                                          &name) const {
    tbb::spin_rw_mutex::scoped_lock read_lock(rw_mutex_, false);
    RoutingInstanceTraceBufferMap::const_iterator iter =
                                          trace_buffer_active_.find(name);

    return (iter != trace_buffer_active_.end());
}

bool RoutingInstanceMgr::HasRoutingInstanceDormantTraceBuf(const std::string
                                                           &name) const {
    tbb::spin_rw_mutex::scoped_lock read_lock(rw_mutex_, false);
    RoutingInstanceTraceBufferMap::const_iterator iter =
                                          trace_buffer_dormant_.find(name);
    return (iter != trace_buffer_dormant_.end());
}

size_t
RoutingInstanceMgr::GetEnvRoutingInstanceDormantTraceBufferCapacity() const {
    char *buffer_capacity_str = getenv(
            "CONTRAIL_ROUTING_INSTANCE_DORMANT_TRACE_BUFFER_SIZE");
    // return Trace buf capacity 0, if not set in env variable.
    if (buffer_capacity_str) {
        size_t env_buffer_capacity = strtoul(buffer_capacity_str, NULL, 0);
        return env_buffer_capacity;
    }
    return 0;
}

size_t
RoutingInstanceMgr::GetEnvRoutingInstanceDormantTraceBufferThreshold() const {
    char *buffer_threshold_str = getenv(
            "CONTRAIL_ROUTING_INSTANCE_DORMANT_TRACE_BUFFER_THRESHOLD");
    if (buffer_threshold_str) {
        size_t env_buffer_threshold = strtoul(buffer_threshold_str, NULL, 0);
        return env_buffer_threshold;
    }
    return ROUTING_INSTANCE_DORMANT_TRACE_BUFFER_THRESHOLD_1K;
}

size_t RoutingInstanceMgr::GetRoutingInstanceActiveTraceBufSize() const {
    tbb::spin_rw_mutex::scoped_lock read_lock(rw_mutex_, false);
    return trace_buffer_active_.size();
}

size_t RoutingInstanceMgr::GetRoutingInstanceDormantTraceBufSize() const {
    tbb::spin_rw_mutex::scoped_lock read_lock(rw_mutex_, false);
    return trace_buffer_dormant_.size();
}

void RoutingInstanceMgr::CreateRoutingInstanceNeighbors(
    const BgpInstanceConfig *config) {
    if (config->neighbor_list().empty())
        return;
    tbb::mutex::scoped_lock lock(mutex_);
    neighbor_config_list_.insert(config->name());
    neighbor_config_trigger_->Set();
}

bool RoutingInstanceMgr::ProcessNeighborConfigList() {
    if (deleted()) {
        deleter()->RetryDelete();
    } else {
        BOOST_FOREACH(const string &name, neighbor_config_list_) {
            RoutingInstance *rtinstance = GetRoutingInstance(name);
            if (!rtinstance || rtinstance->deleted())
                continue;
            rtinstance->CreateNeighbors();
        }
    }

    neighbor_config_list_.clear();
    return true;
}

void RoutingInstanceMgr::ASNUpdateCallback(as_t old_asn, as_t old_local_asn) {
    if (server_->local_autonomous_system() == old_local_asn)
        return;
    for (RoutingInstanceIterator it = begin(); it != end(); ++it) {
        it->second->FlushAllRTargetRoutes(old_local_asn);
        it->second->InitAllRTargetRoutes(server_->local_autonomous_system());
    }
    RoutingInstance *master = GetDefaultRoutingInstance();
    const Ip4Address old_identifier(server()->bgp_identifier());
    master->DeleteMvpnRTargetRoute(old_local_asn, old_identifier);
    master->AddMvpnRTargetRoute(server_->local_autonomous_system());
}

void RoutingInstanceMgr::IdentifierUpdateCallback(Ip4Address old_identifier) {
    for (RoutingInstanceIterator it = begin(); it != end(); ++it) {
        it->second->ProcessIdentifierUpdate(server_->local_autonomous_system());
    }
    RoutingInstance *master = GetDefaultRoutingInstance();
    master->DeleteMvpnRTargetRoute(server_->local_autonomous_system(),
            old_identifier);
    master->AddMvpnRTargetRoute(server_->local_autonomous_system());
}

//
// Disable processing of all instance config lists.
// For testing only.
//
void RoutingInstanceMgr::DisableInstanceConfigListProcessing() {
    for (size_t idx = 0; idx < instance_config_triggers_.size(); ++idx) {
        instance_config_triggers_[idx]->set_disable();
    }
}

//
// Enable processing of all instance config lists.
// For testing only.
//
void RoutingInstanceMgr::EnableInstanceConfigListProcessing() {
    for (size_t idx = 0; idx < instance_config_triggers_.size(); ++idx) {
        instance_config_triggers_[idx]->set_enable();
    }
}

//
// Disable processing of neighbor config list.
// For testing only.
//
void RoutingInstanceMgr::DisableNeighborConfigListProcessing() {
    neighbor_config_trigger_->set_disable();
}

//
// Enable processing of neighbor config list.
// For testing only.
//
void RoutingInstanceMgr::EnableNeighborConfigListProcessing() {
    neighbor_config_trigger_->set_enable();
}

class RoutingInstance::DeleteActor : public LifetimeActor {
public:
    DeleteActor(BgpServer *server, RoutingInstance *parent)
            : LifetimeActor(server ? server->lifetime_manager() : NULL),
                            parent_(parent) {
    }
    virtual bool MayDelete() const {
        return parent_->MayDelete();
    }
    virtual void Shutdown() {
        parent_->mgr_->increment_deleted_count();
        parent_->mgr_->NotifyInstanceOp(parent_->name(),
                                        RoutingInstanceMgr::INSTANCE_DELETE);
        parent_->Shutdown();
    }
    virtual void Destroy() {
        parent_->mgr_->decrement_deleted_count();
        parent_->mgr_->DestroyRoutingInstance(parent_);
    }

private:
    RoutingInstance *parent_;
};

RoutingInstance::RoutingInstance(string name, BgpServer *server,
                                 RoutingInstanceMgr *mgr,
                                 const BgpInstanceConfig *config)
    : name_(name), index_(-1), server_(server), mgr_(mgr), config_(config),
      is_master_(false), always_subscribe_(false), virtual_network_index_(0),
      virtual_network_allow_transit_(false),
      virtual_network_pbb_evpn_enable_(false),
      vxlan_id_(0),
      deleter_(new DeleteActor(server, this)),
      manager_delete_ref_(this, NULL),
      mvpn_project_manager_network_(BgpConfigManager::kFabricInstance) {
    if (mgr) {
        tbb::mutex::scoped_lock lock(mgr->mutex());
        manager_delete_ref_.Reset(mgr->deleter());
    }
}

RoutingInstance::~RoutingInstance() {
}

void RoutingInstance::CreateNeighbors() {
    CHECK_CONCURRENCY("bgp::Config");

    if (config_->neighbor_list().empty())
        return;
    PeerManager *peer_manager = LocatePeerManager();
    BOOST_FOREACH(const string &name, config_->neighbor_list()) {
        if (peer_manager->PeerLookup(name))
            continue;
        peer_manager->PeerResurrect(name);
    }
}

void RoutingInstance::ProcessRoutingPolicyConfig() {
    RoutingPolicyMgr *policy_mgr = server()->routing_policy_mgr();
    BOOST_FOREACH(RoutingPolicyAttachInfo info,
                  config_->routing_policy_list()) {
        RoutingPolicy *policy =
            policy_mgr->GetRoutingPolicy(info.routing_policy_);
        if (policy) {
            routing_policies_.push_back(make_pair(policy,
                                                  policy->generation()));
        }
    }
}

SandeshTraceBufferPtr RoutingInstance::trace_buffer() const {
    return mgr_->LocateTraceBuffer(name_);
}

//
// Update the routing policy on the instance
// 1. New policy is added
// 2. Policy order changed
// 3. Policy is removed
// 4. Existing policy gets updated(terms in the policy got modified)
// In any of the above cases, we call routing policy manager to re-evaluate the
// routes in the BgpTables of this routing instance with new set of policies
//
void RoutingInstance::UpdateRoutingPolicyConfig() {
    CHECK_CONCURRENCY("bgp::Config", "bgp::ConfigHelper");
    RoutingPolicyMgr *policy_mgr = server()->routing_policy_mgr();
    if (policy_mgr->UpdateRoutingPolicyList(config_->routing_policy_list(),
                                            routing_policies())) {
        // Let RoutingPolicyMgr handle update of routing policy on the instance
        policy_mgr->ApplyRoutingPolicy(this);
    }
}

void RoutingInstance::ProcessServiceChainConfig() {
    if (is_master_)
        return;

    /**
      * If any VN (both internal and non-internal) is connnected to an internal
      * VN via a service-chain, we will create a service chain for EVPN address
      * family as well so that we can re-originate routes from the internal VN
      * RI's EVPN table.
      * For other cases (any VN communicating with a non-internal VN via service
      * chain), we do not need a service chain to watch the EVPN table.
      * Schema-transformer needs to make sure to add the service chain config for
      * EVPN address family to all VNs which need to communicate via a
      * service-chain with an internal VN.
      */
    vector<SCAddress::Family> families = list_of
        (SCAddress::INET) (SCAddress::INET6)
        (SCAddress::EVPN) (SCAddress::EVPN6);

    BOOST_FOREACH(SCAddress::Family family, families) {
        const ServiceChainConfig *sc_config =
            config_ ? config_->service_chain_info(family) : NULL;
        IServiceChainMgr *manager = server_->service_chain_mgr(family);
        if (sc_config && !sc_config->routing_instance.empty()) {
            manager->LocateServiceChain(this, *sc_config);
        } else {
            manager->StopServiceChain(this);
        }
    }
}

void RoutingInstance::ProcessStaticRouteConfig() {
    if (is_master_)
        return;

    vector<Address::Family> families = list_of(Address::INET)(Address::INET6);
    BOOST_FOREACH(Address::Family family, families) {
        IStaticRouteMgr *manager = static_route_mgr(family);
        if (!manager && !config_->static_routes(family).empty())
            manager = LocateStaticRouteMgr(family);
        if (manager)
            manager->ProcessStaticRouteConfig();
    }
}

void RoutingInstance::UpdateStaticRouteConfig() {
    if (is_master_)
        return;

    vector<Address::Family> families = list_of(Address::INET)(Address::INET6);
    BOOST_FOREACH(Address::Family family, families) {
        IStaticRouteMgr *manager = static_route_mgr(family);
        if (!manager && !config_->static_routes(family).empty())
            manager = LocateStaticRouteMgr(family);
        if (manager)
            manager->UpdateStaticRouteConfig();
    }
}

void RoutingInstance::FlushStaticRouteConfig() {
    if (is_master_)
        return;

    vector<Address::Family> families = list_of(Address::INET)(Address::INET6);
    BOOST_FOREACH(Address::Family family, families) {
        IStaticRouteMgr *manager = static_route_mgr(family);
        if (manager)
            manager->FlushStaticRouteConfig();
    }
}

void RoutingInstance::UpdateAllStaticRoutes() {
    if (is_master_)
        return;

    vector<Address::Family> families = list_of(Address::INET)(Address::INET6);
    BOOST_FOREACH(Address::Family family, families) {
        IStaticRouteMgr *manager = static_route_mgr(family);
        if (manager)
            manager->UpdateAllRoutes();
    }
}

void RoutingInstance::ProcessRouteAggregationConfig() {
    if (is_master_)
        return;

    vector<Address::Family> families = list_of(Address::INET)(Address::INET6);
    BOOST_FOREACH(Address::Family family, families) {
        IRouteAggregator *aggregator = route_aggregator(family);
        if (!aggregator && !config_->aggregate_routes(family).empty())
            aggregator = LocateRouteAggregator(family);
        if (aggregator)
            aggregator->ProcessAggregateRouteConfig();
    }
}

void RoutingInstance::UpdateRouteAggregationConfig() {
    if (is_master_)
        return;

    vector<Address::Family> families = list_of(Address::INET)(Address::INET6);
    BOOST_FOREACH(Address::Family family, families) {
        IRouteAggregator *aggregator = route_aggregator(family);
        if (!aggregator && !config_->aggregate_routes(family).empty())
            aggregator = LocateRouteAggregator(family);
        if (aggregator)
            aggregator->UpdateAggregateRouteConfig();
    }
}

void RoutingInstance::FlushRouteAggregationConfig() {
    if (is_master_)
        return;

    vector<Address::Family> families = list_of(Address::INET)(Address::INET6);
    BOOST_FOREACH(Address::Family family, families) {
        IRouteAggregator *aggregator = route_aggregator(family);
        if (aggregator)
            aggregator->FlushAggregateRouteConfig();
    }
}

void RoutingInstance::DeleteMvpnRTargetRoute(as_t asn, Ip4Address old_ip) {
    if (asn == 0)
        return;
    string id_str = "target:" + old_ip.to_string() + ":0";
    RouteTarget rtarget = RouteTarget::FromString(id_str);
    DeleteRTargetRoute(asn, rtarget);
}

void RoutingInstance::AddMvpnRTargetRoute(as_t asn) {
    if (asn == 0)
        return;
    const Ip4Address server_ip(server()->bgp_identifier());
    string id_str = "target:" + server_ip.to_string() + ":0";
    RouteTarget rtarget = RouteTarget::FromString(id_str);
    AddRTargetRoute(asn, rtarget);
}

void RoutingInstance::ProcessConfig() {
    RoutingInstanceInfo info = GetDataCollection("");

    // Initialize virtual network info.
    virtual_network_ = config_->virtual_network();
    virtual_network_index_ = config_->virtual_network_index();
    virtual_network_allow_transit_ = config_->virtual_network_allow_transit();
    virtual_network_pbb_evpn_enable_ =
        config_->virtual_network_pbb_evpn_enable();
    vxlan_id_ = config_->vxlan_id();

    // Always subscribe (using RTF) for RTs of PNF service chain instances.
    always_subscribe_ = config_->has_pnf();

    vector<string> import_rt, export_rt;
    BOOST_FOREACH(string irt, config_->import_list()) {
        error_code error;
        RouteTarget rt = RouteTarget::FromString(irt, &error);
        if (!error) {
            import_.insert(rt);
            import_rt.push_back(irt);
        }
    }
    BOOST_FOREACH(string ert, config_->export_list()) {
        error_code error;
        RouteTarget rt = RouteTarget::FromString(ert, &error);
        if (!error) {
            export_.insert(rt);
            export_rt.push_back(ert);
        }
    }
    RTINSTANCE_LOG(Update, this,
                SandeshLevel::SYS_DEBUG, RTINSTANCE_LOG_FLAG_ALL,
                import_rt, export_rt, this->virtual_network_index());

    if (import_rt.size())
        info.set_add_import_rt(import_rt);
    if (export_rt.size())
        info.set_add_export_rt(export_rt);
    if (import_rt.size() || export_rt.size())
        ROUTING_INSTANCE_COLLECTOR_INFO(info);

    // Create BGP Table
    if (name_ == BgpConfigManager::kMasterInstance) {
        assert(mgr_->count() == 0);
        is_master_ = true;

        LocatePeerManager();
        VpnTableCreate(Address::INETVPN);
        VpnTableCreate(Address::INET6VPN);
        VpnTableCreate(Address::ERMVPN);
        VpnTableCreate(Address::MVPN);
        VpnTableCreate(Address::EVPN);
        RTargetTableCreate();

        BgpTable *table_inet = static_cast<BgpTable *>(
            server_->database()->CreateTable("inet.0"));
        assert(table_inet);
        AddTable(table_inet);

        BgpTable *table_inetmpls = static_cast<BgpTable *>(
            server_->database()->CreateTable("inet.3"));
        assert(table_inetmpls);
        AddTable(table_inetmpls);

        BgpTable *table_inet6 = static_cast<BgpTable *>(
            server_->database()->CreateTable("inet6.0"));
        assert(table_inet6);
        AddTable(table_inet6);

    } else {
        // Create foo.family.0.
        VrfTableCreate(Address::INET, Address::INETVPN);
        VrfTableCreate(Address::INET6, Address::INET6VPN);
        VrfTableCreate(Address::ERMVPN, Address::ERMVPN);
        VrfTableCreate(Address::EVPN, Address::EVPN);
        BgpTable *table = VrfTableCreate(Address::MVPN, Address::MVPN);

        // Create path-resolver in mvpn table.
        table->LocatePathResolver();
    }

    ProcessServiceChainConfig();
    ProcessStaticRouteConfig();
    ProcessRouteAggregationConfig();
    ProcessRoutingPolicyConfig();
}

void RoutingInstance::UpdateConfig(const BgpInstanceConfig *cfg) {
    CHECK_CONCURRENCY("bgp::Config", "bgp::ConfigHelper");

    // This is a noop in production code. However unit tests may pass a
    // new object.
    config_ = cfg;

    // Always subscribe (using RTF) for RTs of PNF service chain instances.
    always_subscribe_ = config_->has_pnf();

    // Figure out if there's a significant configuration change that requires
    // notifying routes to all listeners.
    bool notify_routes = false;
    if (virtual_network_allow_transit_ != cfg->virtual_network_allow_transit())
        notify_routes = true;
    if (virtual_network_pbb_evpn_enable_ !=
        cfg->virtual_network_pbb_evpn_enable())
        notify_routes = true;
    if (virtual_network_ != cfg->virtual_network())
        notify_routes = true;
    if (virtual_network_index_ != cfg->virtual_network_index())
        notify_routes = true;

    // Trigger notification of all routes in each table.
    if (notify_routes) {
        BOOST_FOREACH(RouteTableList::value_type &entry, vrf_tables_by_name_) {
            BgpTable *table = entry.second;
            table->NotifyAllEntries();
        }
    }

    // Update all static routes so that they reflect the new OriginVn.
    if (virtual_network_index_ != cfg->virtual_network_index())
        UpdateAllStaticRoutes();

    string prev_vn_name = GetVirtualNetworkName();

    // Update virtual network info.
    virtual_network_ = cfg->virtual_network();

    // Recreate instance stats' UVE, if its key: virtual_network is modified.
    if (prev_vn_name != GetVirtualNetworkName()) {
        mgr_->DeleteVirtualNetworkMapping(prev_vn_name, name_);
        mgr_->CreateVirtualNetworkMapping(GetVirtualNetworkName(), name_);
    }

    virtual_network_index_ = cfg->virtual_network_index();
    virtual_network_allow_transit_ = cfg->virtual_network_allow_transit();
    virtual_network_pbb_evpn_enable_ = cfg->virtual_network_pbb_evpn_enable();
    vxlan_id_ = cfg->vxlan_id();

    // Master routing instance doesn't have import & export list
    // Master instance imports and exports all RT
    if (IsMasterRoutingInstance())
        return;

    RouteTargetList future_import;
    vector<string> add_import_rt, remove_import_rt;
    BOOST_FOREACH(const string &rtarget_str, cfg->import_list()) {
        error_code error;
        RouteTarget rt = RouteTarget::FromString(rtarget_str, &error);
        if (!error)
            future_import.insert(rt);
    }
    set_synchronize(&import_, &future_import,
        boost::bind(&RoutingInstance::AddRouteTarget, this, true,
            &add_import_rt, _1),
        boost::bind(&RoutingInstance::DeleteRouteTarget, this, true,
            &remove_import_rt, _1));

    RouteTargetList future_export;
    vector<string> add_export_rt, remove_export_rt;
    BOOST_FOREACH(const string &rtarget_str, cfg->export_list()) {
        error_code error;
        RouteTarget rt = RouteTarget::FromString(rtarget_str, &error);
        if (!error)
            future_export.insert(rt);
    }
    set_synchronize(&export_, &future_export,
        boost::bind(&RoutingInstance::AddRouteTarget, this, false,
            &add_export_rt, _1),
        boost::bind(&RoutingInstance::DeleteRouteTarget, this, false,
            &remove_export_rt, _1));

    RoutingInstanceInfo info = GetDataCollection("");
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

    ProcessServiceChainConfig();
    UpdateStaticRouteConfig();
    UpdateRouteAggregationConfig();
    UpdateRoutingPolicyConfig();
}

void RoutingInstance::ClearConfig() {
    CHECK_CONCURRENCY("bgp::Config");
    config_ = NULL;
}

void RoutingInstance::ManagedDelete() {
    // RoutingInstanceMgr logs the delete for non-master instances.
    if (IsMasterRoutingInstance()) {
        RTINSTANCE_LOG(Delete, this,
            SandeshLevel::SYS_DEBUG, RTINSTANCE_LOG_FLAG_ALL);
    }
    deleter_->Delete();
}

void RoutingInstance::Shutdown() {
    CHECK_CONCURRENCY("bgp::Config");

    RTINSTANCE_LOG(Shutdown, this,
        SandeshLevel::SYS_DEBUG, RTINSTANCE_LOG_FLAG_ALL);

    ClearConfig();
    FlushAllRTargetRoutes(server_->local_autonomous_system());
    if (is_master_) {
        const Ip4Address old_identifier(server()->bgp_identifier());
        DeleteMvpnRTargetRoute(server_->local_autonomous_system(),
                old_identifier);
    }
    ClearRouteTarget();

    ProcessServiceChainConfig();
    FlushStaticRouteConfig();
    FlushRouteAggregationConfig();
}

bool RoutingInstance::MayDelete() const {
    return true;
}

LifetimeActor *RoutingInstance::deleter() {
    return deleter_.get();
}

const LifetimeActor *RoutingInstance::deleter() const {
    return deleter_.get();
}

bool RoutingInstance::deleted() const {
    return deleter()->IsDeleted();
}

const string RoutingInstance::GetVirtualNetworkName() const {
    if (!virtual_network_.empty())
        return virtual_network_;
    size_t pos = name_.rfind(':');
    if (pos == string::npos) {
        return name_;
    } else {
        return name_.substr(0, pos);
    }
}

int RoutingInstance::virtual_network_index() const {
    return virtual_network_index_;
}

bool RoutingInstance::virtual_network_allow_transit() const {
    return virtual_network_allow_transit_;
}

bool RoutingInstance::virtual_network_pbb_evpn_enable() const {
    return virtual_network_pbb_evpn_enable_;
}

int RoutingInstance::vxlan_id() const {
    return vxlan_id_;
}

void RoutingInstance::ClearFamilyRouteTarget(Address::Family vrf_family,
                                             Address::Family vpn_family) {
    BgpTable *table = GetTable(vrf_family);
    if (table) {
        RoutePathReplicator *replicator = server_->replicator(vpn_family);
        BOOST_FOREACH(RouteTarget rt, import_) {
            replicator->Leave(table, rt, true);
        }
        BOOST_FOREACH(RouteTarget rt, export_) {
            replicator->Leave(table, rt, false);
        }
    }
}

void RoutingInstance::AddRTargetRoute(uint32_t asn,
    const RouteTarget &rtarget) {
    CHECK_CONCURRENCY("bgp::Config", "bgp::ConfigHelper");

    RTargetPrefix prefix(asn, rtarget);
    RTargetRoute rt_key(prefix);

    RoutingInstance *master = mgr_->GetDefaultRoutingInstance();
    BgpTable *table = master->GetTable(Address::RTARGET);
    DBTablePartition *tbl_partition =
        static_cast<DBTablePartition *>(table->GetTablePartition(0));

    tbb::mutex::scoped_lock lock(mgr_->mutex());
    RTargetRoute *route =
        static_cast<RTargetRoute *>(tbl_partition->Find(&rt_key));
    if (!route) {
        route = new RTargetRoute(prefix);
        tbl_partition->Add(route);
    } else {
        route->ClearDelete();
    }

    if (route->FindPath(BgpPath::Local, index_))
        return;

    BgpAttrSpec attr_spec;
    BgpAttrNextHop nexthop(server_->bgp_identifier());
    attr_spec.push_back(&nexthop);
    BgpAttrOrigin origin(BgpAttrOrigin::IGP);
    attr_spec.push_back(&origin);
    BgpAttrPtr attr = server_->attr_db()->Locate(attr_spec);

    BgpPath *path = new BgpPath(index_, BgpPath::Local, attr);
    route->InsertPath(path);
    tbl_partition->Notify(route);
    BGP_LOG_ROUTE(table, static_cast<IPeer *>(NULL),
        route, "Insert Local path with path id " << index_);
}

void RoutingInstance::DeleteRTargetRoute(as_t asn,
    const RouteTarget &rtarget) {
    CHECK_CONCURRENCY("bgp::Config", "bgp::ConfigHelper");

    if (asn == 0)
        return;

    RTargetPrefix prefix(asn, rtarget);
    RTargetRoute rt_key(prefix);

    RoutingInstance *master = mgr_->GetDefaultRoutingInstance();
    BgpTable *table = master->GetTable(Address::RTARGET);
    DBTablePartition *tbl_partition =
        static_cast<DBTablePartition *>(table->GetTablePartition(0));

    tbb::mutex::scoped_lock lock(mgr_->mutex());
    RTargetRoute *route =
        static_cast<RTargetRoute *>(tbl_partition->Find(&rt_key));
    if (!route || !route->RemovePath(BgpPath::Local, index_))
        return;

    BGP_LOG_ROUTE(table, static_cast<IPeer *>(NULL),
        route, "Delete Local path with path id " << index_);
    if (!route->HasPaths()) {
        tbl_partition->Delete(route);
    } else {
        tbl_partition->Notify(route);
    }
}

void RoutingInstance::InitAllRTargetRoutes(as_t asn) {
    if (is_master_)
        return;

    CHECK_CONCURRENCY("bgp::Config", "bgp::ConfigHelper");
    BOOST_FOREACH(RouteTarget rtarget, import_) {
        if (asn != 0 && always_subscribe_)
            AddRTargetRoute(asn, rtarget);
    }
}

void RoutingInstance::FlushAllRTargetRoutes(as_t asn) {
    if (is_master_)
        return;

    CHECK_CONCURRENCY("bgp::Config", "bgp::ConfigHelper");
    BOOST_FOREACH(RouteTarget rtarget, import_) {
        DeleteRTargetRoute(asn, rtarget);
    }
}

void RoutingInstance::ProcessIdentifierUpdate(as_t asn) {
    FlushAllRTargetRoutes(asn);
    InitAllRTargetRoutes(asn);
    rd_.reset(new RouteDistinguisher(server_->bgp_identifier(), index_));
}

void RoutingInstance::AddRouteTarget(bool import,
    vector<string> *change_list, RouteTargetList::const_iterator it) {
    BgpTable *ermvpn_table = GetTable(Address::ERMVPN);
    RoutePathReplicator *ermvpn_replicator =
        server_->replicator(Address::ERMVPN);
    BgpTable *mvpn_table = GetTable(Address::MVPN);
    RoutePathReplicator *mvpn_replicator =
        server_->replicator(Address::MVPN);
    BgpTable *evpn_table = GetTable(Address::EVPN);
    RoutePathReplicator *evpn_replicator =
        server_->replicator(Address::EVPN);
    BgpTable *inet_table = GetTable(Address::INET);
    RoutePathReplicator *inetvpn_replicator =
        server_->replicator(Address::INETVPN);
    BgpTable *inet6_table = GetTable(Address::INET6);
    RoutePathReplicator *inet6vpn_replicator =
        server_->replicator(Address::INET6VPN);

    change_list->push_back(it->ToString());
    if (import) {
        import_.insert(*it);
        if (server_->local_autonomous_system() != 0 && always_subscribe_)
            AddRTargetRoute(server_->local_autonomous_system(), *it);
    } else {
        export_.insert(*it);
    }

    ermvpn_replicator->Join(ermvpn_table, *it, import);
    mvpn_replicator->Join(mvpn_table, *it, import);
    evpn_replicator->Join(evpn_table, *it, import);
    inetvpn_replicator->Join(inet_table, *it, import);
    inet6vpn_replicator->Join(inet6_table, *it, import);
}

void RoutingInstance::DeleteRouteTarget(bool import,
    vector<string> *change_list, RouteTargetList::iterator it) {
    BgpTable *ermvpn_table = GetTable(Address::ERMVPN);
    RoutePathReplicator *ermvpn_replicator =
        server_->replicator(Address::ERMVPN);
    BgpTable *mvpn_table = GetTable(Address::MVPN);
    RoutePathReplicator *mvpn_replicator =
        server_->replicator(Address::MVPN);
    BgpTable *evpn_table = GetTable(Address::EVPN);
    RoutePathReplicator *evpn_replicator =
        server_->replicator(Address::EVPN);
    BgpTable *inet_table = GetTable(Address::INET);
    RoutePathReplicator *inetvpn_replicator =
        server_->replicator(Address::INETVPN);
    BgpTable *inet6_table = GetTable(Address::INET6);
    RoutePathReplicator *inet6vpn_replicator =
        server_->replicator(Address::INET6VPN);

    ermvpn_replicator->Leave(ermvpn_table, *it, import);
    mvpn_replicator->Leave(mvpn_table, *it, import);
    evpn_replicator->Leave(evpn_table, *it, import);
    inetvpn_replicator->Leave(inet_table, *it, import);
    inet6vpn_replicator->Leave(inet6_table, *it, import);

    change_list->push_back(it->ToString());
    if (import) {
        DeleteRTargetRoute(server_->local_autonomous_system(), *it);
        import_.erase(it);
    } else {
        export_.erase(it);
    }
}

void RoutingInstance::ClearRouteTarget() {
    CHECK_CONCURRENCY("bgp::Config");
    if (IsMasterRoutingInstance()) {
        return;
    }

    ClearFamilyRouteTarget(Address::INET, Address::INETVPN);
    ClearFamilyRouteTarget(Address::INET6, Address::INET6VPN);
    ClearFamilyRouteTarget(Address::ERMVPN, Address::ERMVPN);
    ClearFamilyRouteTarget(Address::MVPN, Address::MVPN);
    ClearFamilyRouteTarget(Address::EVPN, Address::EVPN);

    import_.clear();
    export_.clear();
}

BgpTable *RoutingInstance::RTargetTableCreate() {
    BgpTable *rtargettbl = static_cast<BgpTable *>(
        server_->database()->CreateTable("bgp.rtarget.0"));
    RTINSTANCE_LOG_TABLE(Create, this, rtargettbl, SandeshLevel::SYS_DEBUG,
                         RTINSTANCE_LOG_FLAG_ALL);
    AddTable(rtargettbl);
    return rtargettbl;
}

BgpTable *RoutingInstance::VpnTableCreate(Address::Family vpn_family) {
    string table_name = GetTableName(name(), vpn_family);
    BgpTable *table = static_cast<BgpTable *>(
        server_->database()->CreateTable(table_name));
    assert(table);

    AddTable(table);
    RTINSTANCE_LOG_TABLE(Create, this, table, SandeshLevel::SYS_DEBUG,
        RTINSTANCE_LOG_FLAG_ALL);
    assert(server_->rtarget_group_mgr()->empty());
    RoutePathReplicator *replicator = server_->replicator(vpn_family);
    replicator->Initialize();
    return table;
}

BgpTable *RoutingInstance::VrfTableCreate(Address::Family vrf_family,
    Address::Family vpn_family) {
    string table_name = GetTableName(name(), vrf_family);
    BgpTable *table = static_cast<BgpTable *>(
        server_->database()->CreateTable(table_name));
    assert(table);

    AddTable(table);
    RTINSTANCE_LOG_TABLE(Create, this, table, SandeshLevel::SYS_DEBUG,
        RTINSTANCE_LOG_FLAG_ALL);
    RoutePathReplicator *replicator = server_->replicator(vpn_family);
    BOOST_FOREACH(RouteTarget rt, import_) {
        replicator->Join(table, rt, true);
    }
    BOOST_FOREACH(RouteTarget rt, export_) {
        replicator->Join(table, rt, false);
    }
    return table;
}

void RoutingInstance::AddTable(BgpTable *tbl) {
    vrf_tables_by_name_.insert(make_pair(tbl->name(), tbl));
    vrf_tables_by_family_.insert(make_pair(tbl->family(), tbl));
    tbl->set_routing_instance(this);
    RoutingInstanceInfo info = GetDataCollection("Add");
    info.set_family(Address::FamilyToString(tbl->family()));
    ROUTING_INSTANCE_COLLECTOR_INFO(info);
}

void RoutingInstance::RemoveTable(BgpTable *tbl) {
    RoutingInstanceInfo info = GetDataCollection("Remove");
    info.set_family(Address::FamilyToString(tbl->family()));
    vrf_tables_by_name_.erase(tbl->name());
    vrf_tables_by_family_.erase(tbl->family());
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

    // Remove this table from various data structures
    server_->database()->RemoveTable(table);
    RemoveTable(table);

    // Make sure that there are no routes left in this table
    assert(table->Size() == 0);

    delete table;
}

string RoutingInstance::GetTableName(string instance_name,
    Address::Family fmly) {
    string table_name;
    if (instance_name == BgpConfigManager::kMasterInstance) {
        if ((fmly == Address::INET) || (fmly == Address::INET6)) {
            table_name = Address::FamilyToTableString(fmly) + ".0";
        } else if (fmly == Address::INETMPLS) {
            table_name = Address::FamilyToTableString(fmly) + ".3";
        } else {
            table_name = "bgp." + Address::FamilyToTableString(fmly) + ".0";
        }
    } else {
        table_name =
            instance_name + "." + Address::FamilyToTableString(fmly) + ".0";
    }
    return table_name;
}

BgpTable *RoutingInstance::GetTable(Address::Family fmly) {
    RouteTableFamilyList::const_iterator loc = vrf_tables_by_family_.find(fmly);
    return (loc != vrf_tables_by_family_.end() ? loc->second : NULL);
}

const BgpTable *RoutingInstance::GetTable(Address::Family fmly) const {
    RouteTableFamilyList::const_iterator loc = vrf_tables_by_family_.find(fmly);
    return (loc != vrf_tables_by_family_.end() ? loc->second : NULL);
}

string RoutingInstance::GetVrfFromTableName(const string table) {
    static set<string> master_tables = list_of("inet.0")("inet.3")("inet6.0");
    static set<string> vpn_tables =
        list_of("bgp.l3vpn.0")("bgp.ermvpn.0")("bgp.evpn.0")("bgp.rtarget.0")
                ("bgp.l3vpn-inet6.0")("bgp.mvpn.0");

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

void RoutingInstance::set_index(int index) {
    index_ = index;
    if (is_master_)
        return;

    rd_.reset(new RouteDistinguisher(server_->bgp_identifier(), index));
}

RoutingInstanceInfo RoutingInstance::GetDataCollection(const char *operation) {
    RoutingInstanceInfo info;
    info.set_name(GetVirtualNetworkName());
    info.set_instance_name(name_);
    info.set_hostname(server_->localname());
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

// Return true if the route is a service-chain route
bool RoutingInstance::IsServiceChainRoute(const BgpRoute *route) const {
    const BgpTable *table = route->table();
    if (!table) {
        return false;
    }
    const BgpInstanceConfig *rt_config = this->config();
    if (!rt_config) {
        return false;
    }
    SCAddress::Family sc_family =
        SCAddress::AddressFamilyToSCFamily(table->family());
    const ServiceChainConfig *sc_config =
           rt_config->service_chain_info(sc_family);
    if (!sc_config) {
        return false;
    }
    return true;
}

//
// On given route/path apply the routing policy
//    Called from
//        * InsertPath [While adding the path for the first time]
//        * While walking the routing table to apply updating routing policy
//

bool RoutingInstance::ProcessRoutingPolicy(const BgpRoute *route,
                                           BgpPath *path) const {
    ExtCommunityPtr ext_community = path->GetOriginalAttr()->ext_community();
    if (path->IsReplicated() &&
            ext_community && ext_community->GetSubClusterId()) {
        // If self sub-cluster id macthes with what is associated with the route
        // then no need to apply policy.
        if (route->SubClusterId() == ext_community->GetSubClusterId()) {
            BGP_LOG_ROUTE(route->table(), static_cast<IPeer *>(path->GetPeer()),
                    route, "Not applying policy since sub-cluster associated "
                    "with path is same as that with route");
            return true;
        }
    }
    // Don't apply routing policy on secondary path for service chain route
    if (path->IsReplicated() && IsServiceChainRoute(route)) {
        BGP_LOG_ROUTE(route->table(), static_cast<IPeer *>(path->GetPeer()),
                route, "Not applying policy for service-chain secondary "
                "routes");
        return true;
    }
    const RoutingPolicyMgr *policy_mgr = server()->routing_policy_mgr();
    // Take snapshot of original attribute
    BgpAttr *out_attr = new BgpAttr(*(path->GetOriginalAttr()));
    BOOST_FOREACH(RoutingPolicyInfo info, routing_policies()) {
        RoutingPolicyPtr policy = info.first;
        // Process the routing policy on original attribute and prefix
        // Update of the attribute based on routing policy action is done
        // on the snapshot of original attribute passed to this function
        RoutingPolicy::PolicyResult result =
            policy_mgr->ExecuteRoutingPolicy(policy.get(), route,
                                             path, out_attr);
        if (result.first) {
            // Hit a terminal policy
            if (!result.second) {
                // Result of the policy is reject
                path->SetPolicyReject();
            } else if (path->IsPolicyReject()) {
                // Result of the policy is Accept
                // Clear the reject flag is marked before
                path->ResetPolicyReject();
            }
            IPeer *peer = path->GetPeer();
            // Process default tunnel encapsulation that may be configured per
            // address family on the peer. 'peer' is not expected to be NULL
            // except in unit tests.
            if (peer) {
                peer->ProcessPathTunnelEncapsulation(path, out_attr,
                    server_->extcomm_db(), route->table());
            }
            BgpAttrPtr modified_attr = server_->attr_db()->Locate(out_attr);
            // Update the path with new set of attributes
            path->SetAttr(modified_attr, path->GetOriginalAttr());
            return result.second;
        }
    }
    // After processing all the routing policy,,
    // We are here means, all the routing policies have accepted the route
    IPeer *peer = path->GetPeer();
    if (peer) {
        peer->ProcessPathTunnelEncapsulation(path, out_attr,
            server_->extcomm_db(), route->table());
    }
    BgpAttrPtr modified_attr = server_->attr_db()->Locate(out_attr);
    path->SetAttr(modified_attr, path->GetOriginalAttr());
    // Clear the reject if marked so in past
    if (path->IsPolicyReject()) path->ResetPolicyReject();
    return true;
}

IStaticRouteMgr *RoutingInstance::LocateStaticRouteMgr(
    Address::Family family) {
    IStaticRouteMgr *manager = static_route_mgr(family);
    if (manager)
        return manager;
    if (family == Address::INET) {
        inet_static_route_mgr_.reset(
            BgpObjectFactory::Create<IStaticRouteMgr, Address::INET>(this));
        return inet_static_route_mgr_.get();
    } else if (family == Address::INET6) {
        inet6_static_route_mgr_.reset(
            BgpObjectFactory::Create<IStaticRouteMgr, Address::INET6>(this));
        return inet6_static_route_mgr_.get();
    }
    return NULL;
}

IRouteAggregator *RoutingInstance::LocateRouteAggregator(
    Address::Family family) {
    IRouteAggregator *aggregator = route_aggregator(family);
    if (aggregator)
        return aggregator;
    if (family == Address::INET) {
        GetTable(family)->LocatePathResolver();
        inet_route_aggregator_.reset(
            BgpObjectFactory::Create<IRouteAggregator, Address::INET>(this));
        inet_route_aggregator_->Initialize();
        return inet_route_aggregator_.get();
    } else if (family == Address::INET6) {
        GetTable(family)->LocatePathResolver();
        inet6_route_aggregator_.reset(
            BgpObjectFactory::Create<IRouteAggregator, Address::INET6>(this));
        inet6_route_aggregator_->Initialize();
        return inet6_route_aggregator_.get();
    }
    return NULL;
}

void RoutingInstance::DestroyRouteAggregator(Address::Family family) {
    if (family == Address::INET) {
        inet_route_aggregator_.reset();
    } else if (family == Address::INET6) {
        inet6_route_aggregator_.reset();
    }
}

// Check whether the route is aggregating route
bool RoutingInstance::IsAggregateRoute(const BgpTable *table,
                                       const BgpRoute *route) const {
    if (!route_aggregator(table->family())) return false;
    return route_aggregator(table->family())->IsAggregateRoute(route);
}

// Check whether the route is contributing route to aggregate route
bool RoutingInstance::IsContributingRoute(const BgpTable *table,
                                          const BgpRoute *route) const {
    if (!route_aggregator(table->family())) return false;
    return route_aggregator(table->family())->IsContributingRoute(route);
}

int RoutingInstance::GetOriginVnForAggregateRoute(Address::Family fmly) const {
    SCAddress sc_addr;
    SCAddress::Family sc_family =
         sc_addr.AddressFamilyToSCFamily(fmly);
    const ServiceChainConfig *sc_config =
        config_ ? config_->service_chain_info(sc_family) : NULL;
    if (sc_config && !sc_config->routing_instance.empty()) {
        RoutingInstance *dest =
            mgr_->GetRoutingInstanceLocked(sc_config->routing_instance);
        if (dest) return dest->virtual_network_index();
    }
    return virtual_network_index_;
}

size_t RoutingInstance::peer_manager_size() const {
    return (peer_manager_ ? peer_manager_->size() : 0);
}

PeerManager *RoutingInstance::LocatePeerManager() {
    if (!peer_manager_)
        peer_manager_.reset(BgpObjectFactory::Create<PeerManager>(this));
    return peer_manager_.get();
}

// Get primary routing instance name from service RI names which works for both
// in-network and transparent service chains. Use name based lookup to identify
// the primary RI of service instance.
// Service RI Name Format: <domain>:<project>:<VN>:<ServiceRI>
// Primaru RI Name Format: <domain>:<project>:<VN>:<VN>
string RoutingInstanceMgr::GetPrimaryRoutingInstanceName(const string &name) {
    string n = name;
    vector<string> tokens;
    boost::split(tokens, n, boost::is_any_of(":"));
    if (tokens.size() < 3)
        return "";
    ostringstream os;
    os << tokens[0] << ":" << tokens[1] << ":" << tokens[2] << ":" << tokens[2];
    return os.str();
}
