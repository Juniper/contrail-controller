/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/bgp_mvpn.h"

#include <utility>

#include <boost/foreach.hpp>

#include "base/task_annotations.h"
#include "bgp/ermvpn/ermvpn_route.h"
#include "bgp/ermvpn/ermvpn_table.h"
#include "bgp/extended-community/vrf_route_import.h"
#include "bgp/bgp_log.h"
#include "bgp/bgp_multicast.h"
#include "bgp/bgp_server.h"
#include "bgp/bgp_update.h"
#include "bgp/mvpn/mvpn_table.h"
#include "bgp/routing-instance/path_resolver.h"
#include "bgp/routing-instance/routing_instance.h"
#include "bgp/routing-instance/routing_instance_analytics_types.h"
#include "bgp/routing-instance/routing_instance_log.h"
#include "bgp/rtarget/rtarget_address.h"
#include "bgp/tunnel_encap/tunnel_encap.h"

using std::make_pair;
using std::ostringstream;
using std::pair;
using std::string;
using std::vector;

// A global MVPN state for a given <S.G> within a MvpnProjectManager.
MvpnState::MvpnState(const SG &sg, StatesMap *states, MvpnProjectManager *pm) :
        sg_(sg), global_ermvpn_tree_rt_(NULL), spmsi_rt_(NULL),
        source_active_rt_(NULL), states_(states), project_manager_(pm) {
    refcount_ = 0;
}

MvpnState::~MvpnState() {
    assert(!global_ermvpn_tree_rt_);
    assert(!spmsi_rt_);
    assert(!source_active_rt_);
    assert(spmsi_routes_received_.empty());
    assert(leafad_routes_attr_received_.empty());
    MVPN_TRACE(MvpnStateCreate, sg_.source.to_string(), sg_.group.to_string());
}

const ErmVpnTable *MvpnState::table() const {
    return project_manager_ ? project_manager_->table() : NULL;
}

// MvpnProjectManager is deleted when parent ErmVpnTable is deleted.
class MvpnProjectManager::DeleteActor : public LifetimeActor {
public:
    explicit DeleteActor(MvpnProjectManager *manager)
        : LifetimeActor(manager->table_->routing_instance()->server()->
                lifetime_manager()), manager_(manager) {
    }

    virtual ~DeleteActor() {
    }

    virtual bool MayDelete() const {
        CHECK_CONCURRENCY("bgp::Config");
        return manager_->MayDelete();
    }

    virtual void Shutdown() {
    }

    virtual void Destroy() {
        manager_->table_->DestroyMvpnProjectManager();
    }

private:
    MvpnProjectManager *manager_;
};

// Create MvpnProjectManager object and take a lifetime reference to the
// parent ErmVpnTable object.
MvpnProjectManager::MvpnProjectManager(ErmVpnTable *table)
        : table_(table),
          listener_id_(DBTable::kInvalidId),
          table_delete_ref_(this, table->deleter()) {
    deleter_.reset(new DeleteActor(this));
}

MvpnProjectManager::~MvpnProjectManager() {
}

// MvpnProjectManager can be deleted only after all <S,G> MvpnState objects
// are deleted from the map.
bool MvpnProjectManager::MayDelete() const {
    BOOST_FOREACH(const MvpnProjectManagerPartition *partition, partitions_) {
        if (!partition->states().empty()) {
            MVPN_LOG(MvpnProjectManagerDelete,
                "MvpnProjectManager::MayDelete() paused due to pending " +
                integerToString(partition->states().size()) + " MvpnStates");
            return false;
        }
    }
    return true;
}

LifetimeActor *MvpnProjectManager::deleter() {
    return deleter_.get();
}

const LifetimeActor *MvpnProjectManager::deleter() const {
    return deleter_.get();
}

// Create MvpnProjectManagerPartitions and register with the ErmVpnTable to
// get route change notifications.
void MvpnProjectManager::Initialize() {
    if (!table_->server()->mvpn_ipv4_enable())
        return;

    AllocPartitions();

    listener_id_ = table_->Register(
        boost::bind(&MvpnProjectManager::RouteListener, this, _1, _2),
        "MvpnProjectManager");
    MVPN_LOG(MvpnProjectManagerCreate, "Initialized MvpnProjectManager");
}

void MvpnProjectManager::Terminate() {
    CHECK_CONCURRENCY("bgp::Config");
    table_->Unregister(listener_id_);
    listener_id_ = DBTable::kInvalidId;
    FreePartitions();
    MVPN_LOG(MvpnProjectManagerDelete, "Terminated MvpnProjectManager");
}

void MvpnProjectManager::AllocPartitions() {
    for (int part_id = 0; part_id < table_->PartitionCount(); part_id++)
        partitions_.push_back(new MvpnProjectManagerPartition(this, part_id));
}

void MvpnProjectManager::FreePartitions() {
    for (size_t part_id = 0; part_id < partitions_.size(); part_id++) {
        delete partitions_[part_id];
    }
    partitions_.clear();
}

MvpnProjectManagerPartition *MvpnProjectManager::GetPartition(int part_id) {
    return partitions_[part_id];
}

const MvpnProjectManagerPartition *MvpnProjectManager::GetPartition(
        int part_id) const {
    return partitions_[part_id];
}

void MvpnProjectManager::ManagedDelete() {
    deleter_->Delete();
}

bool MvpnProjectManager::deleted() const {
    return deleter_->IsDeleted();
}

MvpnStatePtr MvpnProjectManager::GetState(MvpnRoute *route) const {
    MvpnState::SG sg(route->GetPrefix().source(), route->GetPrefix().group());
    return GetPartition(route->get_table_partition()->index())->GetState(sg);
}

MvpnStatePtr MvpnProjectManager::GetState(MvpnRoute *route) {
    return static_cast<const MvpnProjectManager *>(this)->GetState(route);
}

MvpnStatePtr MvpnProjectManager::GetState(ErmVpnRoute *route) const {
    MvpnState::SG sg(route->GetPrefix().source(), route->GetPrefix().group());
    return GetPartition(route->get_table_partition()->index())->GetState(sg);
}

MvpnProjectManagerPartition::MvpnProjectManagerPartition(
        MvpnProjectManager *manager, int part_id)
    : manager_(manager), part_id_(part_id) {
}

MvpnProjectManagerPartition::~MvpnProjectManagerPartition() {
    assert(states_.empty());
}

MvpnStatePtr MvpnProjectManagerPartition::CreateState(const SG &sg) {
    MvpnStatePtr state(new MvpnState(sg, &states_, manager_));
    assert(states_.insert(make_pair(sg, state.get())).second);
    MVPN_TRACE(MvpnStateCreate, sg.source.to_string(), sg.group.to_string());
    return state;
}

MvpnStatePtr MvpnProjectManagerPartition::LocateState(const SG &sg) {
    MvpnStatePtr mvpn_state = GetState(sg);
    if (mvpn_state)
        return mvpn_state;
    mvpn_state = CreateState(sg);
    assert(mvpn_state);
    return mvpn_state;
}

MvpnStatePtr MvpnProjectManagerPartition::GetState(const SG &sg) const {
    MvpnState::StatesMap::const_iterator iter = states_.find(sg);
    return iter != states_.end() ?  iter->second : NULL;
}

MvpnStatePtr MvpnProjectManagerPartition::GetState(const SG &sg) {
    MvpnState::StatesMap::iterator iter = states_.find(sg);
    return iter != states_.end() ?  iter->second : NULL;
}

MvpnNeighbor::MvpnNeighbor() : source_as_(0) {
}

MvpnNeighbor::MvpnNeighbor(const RouteDistinguisher &rd,
                           const IpAddress &originator) :
        rd_(rd), originator_(originator), source_as_(0) {
}

const RouteDistinguisher &MvpnNeighbor::rd() const {
    return rd_;
}

uint32_t MvpnNeighbor::source_as() const {
    return source_as_;
}

const IpAddress &MvpnNeighbor::originator() const {
    return originator_;
}

bool MvpnNeighbor::operator==(const MvpnNeighbor &rhs) const {
    return rd_ == rhs.rd_ && originator_ == rhs.originator_ &&
           source_as_ == rhs.source_as_;
}

bool MvpnManager::FindNeighbor(const RouteDistinguisher &rd,
                               MvpnNeighbor *nbr) const {
    tbb::reader_writer_lock::scoped_lock_read lock(neighbors_mutex_);
    NeighborMap::const_iterator iter = neighbors_.find(rd);
    if (iter != neighbors_.end()) {
        *nbr = iter->second;
        return true;
    }
    return false;
}

const MvpnManager::NeighborMap &MvpnManager::neighbors() const {
    // Assert that lock cannot be taken now as it must have been taken already.
    // assert(!neighbors_mutex_.try_lock_read());
    return neighbors_;
}

size_t MvpnManager::neighbors_count() const {
    tbb::reader_writer_lock::scoped_lock_read lock(neighbors_mutex_);
    return neighbors_.size();
}

MvpnState::SG::SG(const Ip4Address &source, const Ip4Address &group) :
    source(IpAddress(source)), group(IpAddress(group)) {
}

MvpnState::SG::SG(const ErmVpnRoute *route) :
        source(route->GetPrefix().source()),
        group(route->GetPrefix().group()) {
}

MvpnState::SG::SG(const MvpnRoute *route) :
        source(route->GetPrefix().source()), group(route->GetPrefix().group()) {
}

MvpnState::SG::SG(const IpAddress &source, const IpAddress &group) :
    source(source), group(group) {
}

bool MvpnState::SG::operator<(const SG &other) const {
    if (source < other.source)
        return true;
    if (source > other.source)
        return false;
    if (group < other.group)
        return true;
    if (group > other.group)
        return false;
    return false;
}

const MvpnState::SG &MvpnState::sg() const {
    return sg_;
}

ErmVpnRoute *MvpnState::global_ermvpn_tree_rt() {
    return global_ermvpn_tree_rt_;
}

const ErmVpnRoute *MvpnState::global_ermvpn_tree_rt() const {
    return global_ermvpn_tree_rt_;
}

MvpnRoute *MvpnState::spmsi_rt() {
    return spmsi_rt_;
}

const MvpnRoute *MvpnState::spmsi_rt() const {
    return spmsi_rt_;
}

MvpnState::RoutesSet &MvpnState::spmsi_routes_received() {
    return spmsi_routes_received_;
}

const MvpnState::RoutesSet &MvpnState::spmsi_routes_received() const {
    return spmsi_routes_received_;
}

MvpnState::RoutesMap &MvpnState::leafad_routes_attr_received() {
    return leafad_routes_attr_received_;
}

const MvpnState::RoutesMap &MvpnState::leafad_routes_attr_received() const {
    return leafad_routes_attr_received_;
}

void MvpnState::set_global_ermvpn_tree_rt(ErmVpnRoute *global_ermvpn_tree_rt) {
    global_ermvpn_tree_rt_ = global_ermvpn_tree_rt;
}

void MvpnState::set_spmsi_rt(MvpnRoute *spmsi_rt) {
    spmsi_rt_ = spmsi_rt;
}

MvpnRoute *MvpnState::source_active_rt() {
    return source_active_rt_;
}

const MvpnRoute *MvpnState::source_active_rt() const {
    return source_active_rt_;
}

void MvpnState::set_source_active_rt(MvpnRoute *source_active_rt) {
    source_active_rt_ = source_active_rt;
}

MvpnDBState::MvpnDBState(MvpnStatePtr state) : state_(state) , route_(NULL) {
}

MvpnDBState::~MvpnDBState() {
    set_state(NULL);
}

MvpnStatePtr MvpnDBState::state() {
    return state_;
}

MvpnRoute *MvpnDBState::route() {
    return route_;
}

void MvpnDBState::set_route(MvpnRoute *route) {
    route_ = route;
}

void MvpnDBState::set_state(MvpnStatePtr state) {
    state_ = state;
}

class MvpnManager::DeleteActor : public LifetimeActor {
public:
    explicit DeleteActor(MvpnManager *manager)
        : LifetimeActor(manager->table_->routing_instance()->server()->
                lifetime_manager()), manager_(manager) {
    }
    virtual ~DeleteActor() {
    }

    virtual bool MayDelete() const {
        CHECK_CONCURRENCY("bgp::Config");
        return manager_->MayDelete();
    }

    virtual void Shutdown() {
        if (manager_->table()->IsDeleted())
            return;
        manager_->table()->NotifyAllEntries();
    }

    virtual void Destroy() {
        manager_->table_->DestroyManager();
    }

private:
    MvpnManager *manager_;
};

MvpnManager::MvpnManager(MvpnTable *table, ErmVpnTable *ermvpn_table)
        : table_(table),
          ermvpn_table_(ermvpn_table),
          listener_id_(DBTable::kInvalidId),
          identifier_listener_id_(-1),
          table_delete_ref_(this, table->deleter()),
          ermvpn_table_delete_ref_(this, ermvpn_table->deleter()) {
    deleter_.reset(new DeleteActor(this));
    db_states_count_ = 0;
}

MvpnManager::~MvpnManager() {
}

MvpnTable *MvpnManager::table() {
    return table_;
}

const MvpnTable *MvpnManager::table() const {
    return table_;
}

int MvpnManager::listener_id() const {
    return listener_id_;
}

bool MvpnManager::deleted() const {
    return deleter_->IsDeleted();
}

const LifetimeActor *MvpnManager::deleter() const {
    return deleter_.get();
}

void MvpnManager::Terminate() {
    CHECK_CONCURRENCY("bgp::Config");

    // Delete locally originated type-1 route.
    MvpnRoute *type1_route = table_->FindType1ADRoute();
    if (type1_route) {
        BgpPath *path = type1_route->FindPath(BgpPath::Local, 0);
        if (path)
            type1_route->DeletePath(path);
        type1_route->NotifyOrDelete();
    }

    if (identifier_listener_id_ != -1) {
        table_->server()->UnregisterIdentifierUpdateCallback(
            identifier_listener_id_);
        identifier_listener_id_ = -1;
    }
    table_->Unregister(listener_id_);
    listener_id_ = DBTable::kInvalidId;
    FreePartitions();
    MVPN_LOG(MvpnManagerDelete, "Terminated MvpnManager");
}

void MvpnManager::ManagedDelete() {
    deleter_->Delete();
}

void MvpnManager::AllocPartitions() {
    for (int part_id = 0; part_id < table_->PartitionCount(); part_id++)
        partitions_.push_back(new MvpnManagerPartition(this, part_id));
}

void MvpnManager::FreePartitions() {
    for (size_t part_id = 0; part_id < partitions_.size(); part_id++) {
        delete partitions_[part_id];
    }
    partitions_.clear();
}

// MvpnManager can be deleted only after all associated DB States are cleared.
bool MvpnManager::MayDelete() const {
    if (!db_states_count_)
        return true;
    MVPN_LOG(MvpnManagerDelete,
             "MvpnManager::MayDelete() paused due to pending " +
             integerToString(db_states_count_) + " MvpnDBStates");
    return false;
}

// Set DB State and update count.
void MvpnManager::SetDBState(MvpnRoute *route, MvpnDBState *mvpn_dbstate) {
    route->SetState(table_, listener_id_, mvpn_dbstate);
    db_states_count_++;
}

// Create DB State and update count. If there is no DB State associated in the
// table, resume table deletion if the deletion was pending.
void MvpnManager::ClearDBState(MvpnRoute *route) {
    route->ClearState(table_, listener_id_);
    assert(db_states_count_);
    db_states_count_--;

    // Retry deletion now as there is no more attached db state in the table.
    if (!db_states_count_ && deleter_->IsDeleted())
        deleter_->RetryDelete();
}

MvpnTable *MvpnManagerPartition::table() {
    return manager_->table();
}

int MvpnManagerPartition::listener_id() const {
    return manager_->listener_id();
}

MvpnManagerPartition::MvpnManagerPartition(MvpnManager *manager, int part_id)
    : manager_(manager), part_id_(part_id) {
}

MvpnManagerPartition::~MvpnManagerPartition() {
}

MvpnProjectManagerPartition *
MvpnManagerPartition::GetProjectManagerPartition() {
    MvpnProjectManager *project_manager = manager_->GetProjectManager();
    return project_manager ? project_manager->GetPartition(part_id_) : NULL;
}

const MvpnProjectManagerPartition *
MvpnManagerPartition::GetProjectManagerPartition() const {
    MvpnProjectManager *project_manager = manager_->GetProjectManager();
    return project_manager ? project_manager->GetPartition(part_id_) : NULL;
}

MvpnProjectManager *MvpnManager::GetProjectManager() {
    return table_->GetProjectManager();
}

int MvpnProjectManager::listener_id() const {
    return listener_id_;
}

int MvpnProjectManagerPartition::listener_id() const {
    return manager_->listener_id();
}

MvpnStatePtr MvpnManagerPartition::LocateState(MvpnRoute *rt) {
    MvpnProjectManagerPartition *project_manager_partition =
        GetProjectManagerPartition();
    assert(project_manager_partition);
    MvpnState::SG sg = MvpnState::SG(rt->GetPrefix().sourceIpAddress(),
                                     rt->GetPrefix().groupIpAddress());
    return project_manager_partition->LocateState(sg);
}

MvpnStatePtr MvpnManagerPartition::GetState(MvpnRoute *rt) const {
    const MvpnProjectManagerPartition *project_manager_partition =
        GetProjectManagerPartition();
    if (!project_manager_partition)
        return NULL;
    MvpnState::SG sg = MvpnState::SG(rt->GetPrefix().sourceIpAddress(),
                                     rt->GetPrefix().groupIpAddress());
    return project_manager_partition->GetState(sg);
}

MvpnStatePtr MvpnManagerPartition::GetState(MvpnRoute *rt) {
    return static_cast<const MvpnManagerPartition *>(this)->GetState(rt);
}

ErmVpnTable *MvpnProjectManager::table() {
    return table_;
}

const ErmVpnTable *MvpnProjectManager::table() const {
    return table_;
}

ErmVpnTable *MvpnProjectManagerPartition::table() {
    return manager_->table();
}

const ErmVpnTable *MvpnProjectManagerPartition::table() const {
    return manager_->table();
}

void MvpnProjectManagerPartition::NotifyForestNode(
        const Ip4Address &source, const Ip4Address &group) {
    if (table()->tree_manager())
        table()->tree_manager()->NotifyForestNode(part_id_, source, group);
}

void MvpnManagerPartition::NotifyForestNode(
        const Ip4Address &source, const Ip4Address &group) {
    MvpnProjectManagerPartition *pm = GetProjectManagerPartition();
    if (pm)
        pm->NotifyForestNode(source, group);
}

bool MvpnProjectManagerPartition::GetForestNodePMSI(ErmVpnRoute *rt,
        uint32_t *label, Ip4Address *address, vector<string> *enc) const {
    if (!table()->tree_manager())
        return false;
    return table()->tree_manager()->GetForestNodePMSI(rt, label, address, enc);
}

bool MvpnManagerPartition::GetForestNodePMSI(ErmVpnRoute *rt, uint32_t *label,
        Ip4Address *address, vector<string> *encap) const {
    const MvpnProjectManagerPartition *pm = GetProjectManagerPartition();
    return pm ? pm->GetForestNodePMSI(rt, label, address, encap) : false;
}

////////////////////////////////////////////////////////////////////////////////

// Initialize MvpnManager by allcating one MvpnManagerPartition for each DB
// partition, and register a route listener for the MvpnTable.
void MvpnManager::Initialize() {
    if (!table_->server()->mvpn_ipv4_enable())
        return;

    assert(!table_->IsMaster());
    AllocPartitions();

    listener_id_ = table_->Register(
        boost::bind(&MvpnManager::RouteListener, this, _1, _2),
        "MvpnManager");

    identifier_listener_id_ =
        table_->server()->RegisterIdentifierUpdateCallback(boost::bind(
            &MvpnManager::ReOriginateType1Route, this, _1));
    OriginateType1Route();

    MVPN_LOG(MvpnManagerCreate, "Initialized MvpnManager");
}

void MvpnManager::ReOriginateType1Route(const Ip4Address &old_identifier) {
    // Check if a path is already origianted. If so, delete it.
    MvpnRoute *route = table_->FindType1ADRoute(old_identifier);
    if (route) {
        BgpPath *path = route->FindPath(BgpPath::Local, 0);
        if (path) {
            route->DeletePath(path);
            route->NotifyOrDelete();
        }
    }
    OriginateType1Route();
}

void MvpnManager::OriginateType1Route() {
    // Originate Type1 Intra AS Auto-Discovery path.
    BgpServer *server = table_->server();

    // Check for the presence of valid identifier.
    if (!table_->server()->bgp_identifier())
        return;
    MvpnRoute *route = table_->LocateType1ADRoute();
    BgpAttrSpec attr_spec;
    BgpAttrNextHop nexthop(server->bgp_identifier());
    attr_spec.push_back(&nexthop);
    BgpAttrPtr attr = server->attr_db()->Locate(attr_spec);
    BgpPath *path = new BgpPath(NULL, 0, BgpPath::Local, attr, 0, 0, 0);
    route->InsertPath(path);
    route->Notify();

    // TODO(Ananth) Originate Type2 Inter AS Auto-Discovery Route.
}

// MvpnTable route listener callback function.
//
// Process changes (create/update/delete) to all different types of MvpnRoute.
void MvpnManager::RouteListener(DBTablePartBase *tpart, DBEntryBase *db_entry) {
    CHECK_CONCURRENCY("db::DBTable");

    MvpnRoute *route = dynamic_cast<MvpnRoute *>(db_entry);
    assert(route);

    MvpnManagerPartition *partition = partitions_[tpart->index()];

    // Process Type1 Intra-AS AD route.
    if (route->GetPrefix().type() == MvpnPrefix::IntraASPMSIADRoute) {
        ProcessType1ADRoute(route);
        return;
    }

    // TODO(Ananth) Inter-AS Multiast Site AD.

    // Process Type3 S-PMSI route.
    if (route->GetPrefix().type() == MvpnPrefix::SPMSIADRoute) {
        partition->ProcessType3SPMSIRoute(route);
        return;
    }

    // Process Type7 C-Join route.
    if (route->GetPrefix().type() == MvpnPrefix::SourceTreeJoinRoute) {
        partition->ProcessType7SourceTreeJoinRoute(route);
        return;
    }

    // Process Type5 Source Active route.
    if (route->GetPrefix().type() == MvpnPrefix::SourceActiveADRoute) {
        partition->ProcessType5SourceActiveRoute(route);
        return;
    }

    // Process Type4 LeafAD route.
    if (route->GetPrefix().type() == MvpnPrefix::LeafADRoute) {
        partition->ProcessType4LeafADRoute(route);
        return;
    }
}

// Update MVPN neighbor list with create/delete/update of auto-discovery routes.
//
// Protect access to neighbors_ map with a mutex as the same be 'read' off other
// DB tasks in parallel. (Type-1 and Type-2 do not carrry any <S,G> information)
void MvpnManager::ProcessType1ADRoute(MvpnRoute *route) {
    RouteDistinguisher rd = route->GetPrefix().route_distinguisher();

    // Check if an entry is already present.
    MvpnNeighbor old_neighbor;
    bool found = FindNeighbor(rd, &old_neighbor);

    if (!route->IsUsable()) {
        if (!found)
            return;
        tbb::reader_writer_lock::scoped_lock lock(neighbors_mutex_);
        MVPN_LOG(MvpnNeighborDelete, old_neighbor.rd().ToString(),
                 old_neighbor.originator().to_string(),
                 old_neighbor.source_as());
        neighbors_.erase(rd);
        return;
    }

    // Ignore primary paths.
    if (!route->BestPath()->IsReplicated())
        return;

    MvpnNeighbor neighbor(route->GetPrefix().route_distinguisher(),
                          route->GetPrefix().originator());

    // Ignore if there is no change.
    if (found && old_neighbor == neighbor)
        return;

    tbb::reader_writer_lock::scoped_lock lock(neighbors_mutex_);
    if (found)
        neighbors_.erase(rd);
    neighbors_.insert(make_pair(rd, neighbor));
    MVPN_LOG(MvpnNeighborCreate, neighbor.rd().ToString(),
             neighbor.originator().to_string(), neighbor.source_as());
}

// Check whether an ErmVpnRoute is locally originated GlobalTreeRoute.
bool MvpnProjectManagerPartition::IsUsableGlobalTreeRootRoute(
        ErmVpnRoute *ermvpn_route) const {
    if (!ermvpn_route || !ermvpn_route->IsUsable())
        return NULL;
    if (!table()->tree_manager())
        return false;
    ErmVpnRoute *global_rt = table()->tree_manager()->GetGlobalTreeRootRoute(
        ermvpn_route->GetPrefix().source(), ermvpn_route->GetPrefix().group());
    return (global_rt && global_rt == ermvpn_route);
}

// ErmVpnTable route listener callback function.
//
// Process changes (create/update/delete) to GlobalErmVpnRoute in vrf.ermvpn.0
void MvpnProjectManager::RouteListener(DBTablePartBase *tpart,
        DBEntryBase *db_entry) {
    CHECK_CONCURRENCY("db::DBTable");
    MvpnProjectManagerPartition *partition = GetPartition(tpart->index());
    partition->RouteListener(db_entry);
}

// Process changes to ErmVpnRoutes. We only care about changes to routes of
// type GlobalTreeRoute.
void MvpnProjectManagerPartition::RouteListener(DBEntryBase *db_entry) {
    ErmVpnRoute *ermvpn_route = dynamic_cast<ErmVpnRoute *>(db_entry);
    assert(ermvpn_route);

    // We only care about global tree routes for mvpn stitching.
    if (ermvpn_route->GetPrefix().type() != ErmVpnPrefix::GlobalTreeRoute)
        return;

    MvpnDBState *mvpn_dbstate = dynamic_cast<MvpnDBState *>(
        ermvpn_route->GetState(table(), listener_id()));

    // Handle GlobalTreeRoute route deletion.
    if (!IsUsableGlobalTreeRootRoute(ermvpn_route)) {
        // Ignore if there is no DB State associated with route.
        if (!mvpn_dbstate)
            return;
        MvpnStatePtr mvpn_state = mvpn_dbstate->state();
        mvpn_state->set_global_ermvpn_tree_rt(NULL);

        // Notify all received Type3 spmsi routes for PMSI re-computation.
        // Since usable global ermvpn is no longer available, any advertised
        // type-4 lead-ad routes must now be withdrawn.
        BOOST_FOREACH(MvpnRoute *route, mvpn_state->spmsi_routes_received()) {
            route->Notify();
        }
        ermvpn_route->ClearState(table(), listener_id());
        MVPN_ERMVPN_RT_LOG(ermvpn_route,
                           "Processed MVPN GlobalErmVpnRoute deletion");
        delete mvpn_dbstate;
        return;
    }

    // Set DB State in the route if not already done so before.
    MvpnStatePtr mvpn_state;
    if (!mvpn_dbstate) {
        MvpnState::SG sg(ermvpn_route);
        mvpn_state = LocateState(sg);
        mvpn_dbstate = new MvpnDBState(mvpn_state);
        ermvpn_route->SetState(table(), listener_id(), mvpn_dbstate);
    } else {
        mvpn_state = mvpn_dbstate->state();
    }

    // Note down current usable ermvpn route for stitching to mvpn.
    mvpn_dbstate->state()->set_global_ermvpn_tree_rt(ermvpn_route);

    // Notify all originated Type3 spmsi routes for PMSI re-computation.
    BOOST_FOREACH(MvpnRoute *route, mvpn_state->spmsi_routes_received()) {
        route->Notify();
    }

    MVPN_ERMVPN_RT_LOG(ermvpn_route,
                       "Processed MVPN GlobalErmVpnRoute creation");
}

// Process change to MVPN Type-5 SourceActive route.
void MvpnManagerPartition::ProcessType5SourceActiveRoute(MvpnRoute *rt) {
    MvpnDBState *mvpn_dbstate = dynamic_cast<MvpnDBState *>(rt->GetState(
                                    table(), listener_id()));

    // Process route change as delete if ProjectManager is not set.
    bool is_usable = rt->IsUsable() && table()->IsProjectManagerUsable();
    if (!is_usable) {
        if (!mvpn_dbstate)
            return;

        // Delete any associated type-3 s-pmsi route.
        MvpnRoute *spmsi_rt =
            mvpn_dbstate->state() ? mvpn_dbstate->state()->spmsi_rt() : NULL;
        if (spmsi_rt && spmsi_rt->IsUsable()) {
            BgpPath *path = spmsi_rt->FindPath(BgpPath::Local, 0);
            if (path)
                spmsi_rt->DeletePath(path);
        }

        mvpn_dbstate->set_route(NULL);
        mvpn_dbstate->state()->set_source_active_rt(NULL);
        mvpn_dbstate->state()->set_spmsi_rt(NULL);
        if (spmsi_rt)
            spmsi_rt->NotifyOrDelete();
        manager_->ClearDBState(rt);
        MVPN_RT_LOG(rt, "Processed MVPN Source Active route deletion");
        delete mvpn_dbstate;
        return;
    }

    const BgpPath *path = rt->BestPath();
    // Here in the sender side, we only care about changes to the primary path.
    if (path->IsReplicated())
        return;

    MvpnStatePtr state = LocateState(rt);
    state->set_source_active_rt(rt);

    // Set DB State if not already done so.
    if (!mvpn_dbstate) {
        mvpn_dbstate = new MvpnDBState(state);
        manager_->SetDBState(rt, mvpn_dbstate);
    }

    // Check if there is any receiver interested. If not, do not originate
    // type-3 spmsi route. Also, we originate Type3 S-PMSI route only if there
    // is an imported secondary path for the join route (i.e when the join
    // route reached the sender)
    const MvpnRoute *join_rt = table()->FindType7SourceTreeJoinRoute(rt);
    if (!join_rt || !join_rt->IsUsable() ||
            !join_rt->BestPath()->IsReplicated()) {
        // Remove any type-3 spmsi path originated before.
        MvpnRoute *spmsi_rt = mvpn_dbstate->route();
        if (spmsi_rt) {
            assert(!state->spmsi_rt() || spmsi_rt == state->spmsi_rt());
            state->set_spmsi_rt(NULL);
            mvpn_dbstate->set_route(NULL);
            BgpPath *path = spmsi_rt->FindPath(BgpPath::Local, 0);
            if (path) {
                MVPN_RT_LOG(spmsi_rt, "Deleted already originated SPMSI path");
                spmsi_rt->DeletePath(path);
                spmsi_rt->NotifyOrDelete();
            }
        }
        return;
    }

    // Originate Type-3 S-PMSI route to send towards the receivers.
    MvpnRoute *spmsi_rt = table()->LocateType3SPMSIRoute(join_rt);
    assert(spmsi_rt);
    state->set_spmsi_rt(spmsi_rt);
    if (!mvpn_dbstate->route()) {
        mvpn_dbstate->set_route(spmsi_rt);
    } else {
        assert(spmsi_rt == mvpn_dbstate->route());
        BgpPath *path = spmsi_rt->FindPath(BgpPath::Local, 0);
        assert(path);

        // Ignore if there is no change in the attributes.
        if (path->GetAttr() == rt->BestPath()->GetAttr())
            return;
        spmsi_rt->DeletePath(path);
    }

    PmsiTunnelSpec pmsi_spec;
    pmsi_spec.tunnel_flags = PmsiTunnelSpec::LeafInfoRequired;
    BgpAttrDB *attr_db = table()->server()->attr_db();
    BgpAttrPtr new_attrp = attr_db->ReplacePmsiTunnelAndLocate(
        rt->BestPath()->GetAttr(), &pmsi_spec);

    // Insert new path and notify.
    BgpPath *new_path = new BgpPath(NULL, 0, BgpPath::Local,
                                    new_attrp, 0, 0, 0);
    spmsi_rt->InsertPath(new_path);
    spmsi_rt->Notify();
    MVPN_RT_LOG(rt, "Processed MVPN Source Active route creation");
}

void MvpnManagerPartition::ProcessType7SourceTreeJoinRoute(MvpnRoute *join_rt) {
    MvpnDBState *mvpn_dbstate = dynamic_cast<MvpnDBState *>(
        join_rt->GetState(table(), listener_id()));

    // Process route change as delete if ProjectManager is not set.
    bool is_usable = join_rt->IsUsable() && table()->IsProjectManagerUsable();
    if (!is_usable) {
        if (!mvpn_dbstate)
            return;

        // Notify associatd source-active route so that any s-pmsi route if
        // originated before can be withdrawn as there is no more active join
        // route (receiver) for this <S,G>.
        if (mvpn_dbstate->state()->source_active_rt())
            mvpn_dbstate->state()->source_active_rt()->Notify();
        manager_->ClearDBState(join_rt);
        MVPN_RT_LOG(join_rt, "Processed Type 7 Join route deletion");
        delete mvpn_dbstate;
        return;
    }

    // We care only for imported secondary type-7 joins (at the sender).
    if (!join_rt->BestPath()->IsReplicated())
        return;

    MvpnStatePtr state = LocateState(join_rt);
    if (!mvpn_dbstate) {
        mvpn_dbstate = new MvpnDBState(state);
        manager_->SetDBState(join_rt, mvpn_dbstate);
    }

    // A join has been received or updated at the sender. Re-evaluate the
    // type5 source active, if one such route is present.
    if (state->source_active_rt()) {
        state->source_active_rt()->Notify();
        MVPN_RT_LOG(join_rt, "Processed Type 7 Join route creation and "
                    "notified Source Active route");
    } else {
        MVPN_RT_LOG(join_rt, "Processed Type 7 Join route creation");
    }
}

void MvpnManagerPartition::ProcessType4LeafADRoute(MvpnRoute *leaf_ad) {
    MvpnDBState *mvpn_dbstate = dynamic_cast<MvpnDBState *>(
        leaf_ad->GetState(table(), listener_id()));
    // Process route change as delete if ProjectManager is not set.
    bool is_usable = leaf_ad->IsUsable() && table()->IsProjectManagerUsable();
    if (!is_usable) {
        if (!mvpn_dbstate)
            return;
        assert(mvpn_dbstate->state()->leafad_routes_attr_received().
                erase(leaf_ad));
        MvpnRoute *sa_active_rt = mvpn_dbstate->state()->source_active_rt();

        // Re-evaluate type5 route as secondary type4 leafad route is deleted.
        // olist needs to be updated and sent to the sender route agent.
        if (sa_active_rt && sa_active_rt->IsUsable()) {
            sa_active_rt->Notify();
            MVPN_RT_LOG(leaf_ad, "Processed Type 4 LeafAD route deletion"
                                 " and notified type5 source active route");
        } else {
            MVPN_RT_LOG(leaf_ad, "Processed Type 4 LeafAD route deletion");
        }
        manager_->ClearDBState(leaf_ad);
        delete mvpn_dbstate;
        return;
    }

    const BgpPath *path = leaf_ad->BestPath();
    if (!path->IsReplicated())
        return;

    // Secondary leaft-ad path has been imported.
    MvpnStatePtr state = LocateState(leaf_ad);
    if (!mvpn_dbstate) {
        mvpn_dbstate = new MvpnDBState(state);
        manager_->SetDBState(leaf_ad, mvpn_dbstate);
    }

    pair<MvpnState::RoutesMap::iterator, bool> result =
        state->leafad_routes_attr_received().insert(make_pair(leaf_ad,
                    leaf_ad->BestPath()->GetAttr()));

    // Overwrite the entry with new best path attributes if one already exists.
    if (!result.second) {
        // Ignore if there is no change in the best path's attributes.
        if (result.first->second.get() == leaf_ad->BestPath()->GetAttr())
            return;
        result.first->second = leaf_ad->BestPath()->GetAttr();
    }

    // Update the sender source-active route to update the olist.
    MvpnRoute *sa_active_rt = mvpn_dbstate->state()->source_active_rt();
    if (sa_active_rt && sa_active_rt->IsUsable()) {
        sa_active_rt->Notify();
        MVPN_RT_LOG(sa_active_rt, "Processed Type 4 Leaf AD route creation"
                    " and Type-5 source active route was notified");
    } else {
        MVPN_RT_LOG(sa_active_rt, "Processed Type 4 Leaf AD route creation");
    }
}

// Process changes to Type3 S-PMSI routes by originating or deleting Type4 Leaf
// AD paths as appropriate.
void MvpnManagerPartition::ProcessType3SPMSIRoute(MvpnRoute *spmsi_rt) {
    // Retrieve any state associcated with this S-PMSI route.
    MvpnDBState *mvpn_dbstate = dynamic_cast<MvpnDBState *>(
        spmsi_rt->GetState(table(), listener_id()));

    MvpnRoute *leaf_ad_route = NULL;
    // Process route change as delete if ProjectManager is not set.
    bool is_usable = spmsi_rt->IsUsable() && table()->IsProjectManagerUsable();
    if (!is_usable) {
        if (!mvpn_dbstate)
            return;
        MvpnStatePtr mvpn_state = GetState(spmsi_rt);
        assert(mvpn_dbstate->state() == mvpn_state);

        // Check if a Type4 LeafAD path was already originated before for this
        // S-PMSI path. If so, delete it as the S-PMSI path is no nonger usable.
        leaf_ad_route = mvpn_dbstate->route();
        if (leaf_ad_route) {
            BgpPath *path = leaf_ad_route->FindPath(BgpPath::Local, 0);
            if (path)
                leaf_ad_route->DeletePath(path);
            mvpn_dbstate->set_route(NULL);
        }

        assert(mvpn_state->spmsi_routes_received().erase(spmsi_rt));
        manager_->ClearDBState(spmsi_rt);
        delete mvpn_dbstate;
        if (leaf_ad_route) {
            leaf_ad_route->NotifyOrDelete();

            // Forest node route needs to be updated to delete the source
            // address if advertised before.
            NotifyForestNode(spmsi_rt->GetPrefix().source(),
                             spmsi_rt->GetPrefix().group());
            MVPN_RT_LOG(spmsi_rt, "Processed Type 3 S-PMSI route deletion"
                        " and notified local ForestNode");
        } else {
            MVPN_RT_LOG(spmsi_rt, "Processed Type 3 S-PMSI route deletion");
        }
        return;
    }

    // Ignore notifications of primary S-PMSI paths.
    if (!spmsi_rt->BestPath()->IsReplicated())
        return;

    // Don't send Type 4 route if there is no receiver in this vrf
    const MvpnRoute *join_rt = table()->FindType7SourceTreeJoinRoute(spmsi_rt);
    if (!join_rt || !join_rt->IsUsable())
        return;

    // A valid S-PMSI path has been imported to a table. Originate a new
    // LeafAD path, if GlobalErmVpnTreeRoute is available to stitch.
    // TODO(Ananth) If LeafInfoRequired bit is not set in the S-PMSI route,
    // then we do not need to originate a leaf ad route for this s-pmsi rt.
    MvpnStatePtr mvpn_state = LocateState(spmsi_rt);
    assert(mvpn_state);
    if (!mvpn_dbstate) {
        mvpn_dbstate = new MvpnDBState(mvpn_state);
        manager_->SetDBState(spmsi_rt, mvpn_dbstate);
        assert(mvpn_state->spmsi_routes_received().insert(spmsi_rt).second);
    } else {
        leaf_ad_route = mvpn_dbstate->route();
    }

    // If LeafInfoRequired bit is not set, no need to process further
    if (!spmsi_rt->BestPath()->GetAttr()->pmsi_tunnel() ||
        (!(spmsi_rt->BestPath()->GetAttr()->pmsi_tunnel()->tunnel_flags() &
                PmsiTunnelSpec::LeafInfoRequired))) {
            MVPN_RT_LOG(spmsi_rt, "No need to process Type 3 S-PMSI route as"
                        " LeafInfoRequired bit is not set");
            return;
    }

    ErmVpnRoute *global_rt = mvpn_state->global_ermvpn_tree_rt();
    uint32_t label;
    Ip4Address address;
    vector<string> tunnel_encaps;
    bool pmsi_found =
        GetForestNodePMSI(global_rt, &label, &address, &tunnel_encaps);

    if (!pmsi_found) {
        // There is no ermvpn route available to stitch at this time. Remove any
        // originated Type4 LeafAD route. DB State shall remain on the route as
        // SPMSI route itself is still a usable route.
        if (leaf_ad_route) {
            BgpPath *path = leaf_ad_route->FindPath(BgpPath::Local, 0);
            if (path)
                leaf_ad_route->DeletePath(path);
            mvpn_dbstate->set_route(NULL);
            leaf_ad_route->NotifyOrDelete();
            NotifyForestNode(spmsi_rt->GetPrefix().source(),
                             spmsi_rt->GetPrefix().group());
            MVPN_RT_LOG(spmsi_rt, "Processed Type 3 S-PMSI route as deletion"
                        " and notified local ForestNode due to missing PMSI");
        }
        return;
    }

    if (!leaf_ad_route) {
        leaf_ad_route = table()->LocateType4LeafADRoute(spmsi_rt);
        mvpn_dbstate->set_route(leaf_ad_route);
    }
    BgpPath *old_path = leaf_ad_route->FindPath(BgpPath::Local, 0);

    // For LeafAD routes, rtarget is always <sender-router-id>:0.
    BgpAttrPtr attrp = BgpAttrPtr(spmsi_rt->BestPath()->GetAttr());
    ExtCommunity::ExtCommunityList rtarget;
    rtarget.push_back(RouteTarget(spmsi_rt->GetPrefix().originator(), 0).
                                  GetExtCommunity());
    ExtCommunityPtr ext_community = table()->server()->extcomm_db()->
            ReplaceRTargetAndLocate(attrp->ext_community(), rtarget);

    ExtCommunity::ExtCommunityList tunnel_encaps_list;
    BOOST_FOREACH(string encap, tunnel_encaps) {
        tunnel_encaps_list.push_back(TunnelEncap(encap).GetExtCommunity());
    }

    ext_community = table()->server()->extcomm_db()->
        ReplaceTunnelEncapsulationAndLocate(ext_community.get(),
                tunnel_encaps_list);

    attrp = table()->server()->attr_db()->ReplaceExtCommunityAndLocate(
        attrp.get(), ext_community);

    // Retrieve PMSI tunnel attribute from the GlobalErmVpnTreeRoute.
    PmsiTunnelSpec pmsi_spec;
    pmsi_spec.tunnel_flags = 0;
    pmsi_spec.tunnel_type = PmsiTunnelSpec::IngressReplication;
    pmsi_spec.SetLabel(label, ext_community.get());
    pmsi_spec.SetIdentifier(address);

    // Replicate the LeafAD path with appropriate PMSI tunnel info as part of
    // the path attributes. Community should be route-target with root ingress
    // PE router-id + 0 (Page 254).
    BgpAttrPtr new_attrp =
        table()->server()->attr_db()->ReplacePmsiTunnelAndLocate(attrp.get(),
                                                                 &pmsi_spec);

    // Ignore if there is no change in the path attributes of already originated
    // leaf ad path.
    if (old_path && old_path->GetAttr() == new_attrp.get())
        return;

    BgpPath *path = new BgpPath(NULL, 0, BgpPath::Local, new_attrp, 0, 0, 0);
    if (old_path)
        leaf_ad_route->DeletePath(old_path);
    leaf_ad_route->InsertPath(path);
    leaf_ad_route->NotifyOrDelete();
    NotifyForestNode(spmsi_rt->GetPrefix().source(),
                     spmsi_rt->GetPrefix().group());
    MVPN_RT_LOG(spmsi_rt, "Processed Type 3 S-PMSI route creation");
}

void MvpnManager::UpdateSecondaryTablesForReplication(MvpnRoute *mvpn_rt,
        BgpTable::TableSet *secondary_tables) const {
    // Find the right MvpnProjectManagerPartition based on the rt's partition.
    const MvpnProjectManagerPartition *partition =
        table()->GetProjectManagerPartition(mvpn_rt);
    if (!partition)
        return;

    // Retrieve MVPN state. Ignore if there is no state or if there is no usable
    // Type3 SPMSI route 0associated with it (perhaps it was deleted already).
    MvpnState::SG sg(mvpn_rt);
    MvpnStatePtr state = partition->GetState(sg);
    if (!state || !state->spmsi_rt() || !state->spmsi_rt()->IsUsable())
        return;

    // Matching Type-3 S-PMSI route was found. Return its table.
    BgpTable *table = dynamic_cast<BgpTable *>(
        state->spmsi_rt()->get_table_partition()->parent());
    assert(table);

    // Update table list to let replicator invoke RouteReplicate() for this
    // LeafAD route for this table which has the corresponding Type3 SPMSI
    // route. This was originated as the 'Sender' since receiver joined to
    // the <C-S,G> group.
    secondary_tables->insert(table);
    MVPN_RT_LOG(mvpn_rt, "Updated tables for replication with table " +
                table->name());
}

// Return source_address of the type-3 s-pmsi route used for rpf check in the
// forest node.
void MvpnProjectManager::GetMvpnSourceAddress(ErmVpnRoute *ermvpn_route,
                                              Ip4Address *addrp) const {
    // Bail if project manager is deleted.
    if (deleter_->IsDeleted())
        return;

    // Bail if there is no state for this <S,G>.
    MvpnStatePtr state = GetState(ermvpn_route);
    if (!state)
        return;

    // Bail if there is no usable global_ermvpn_tree_rt.
    if (!state->global_ermvpn_tree_rt() ||
            !state->global_ermvpn_tree_rt()->IsUsable()) {
        return;
    }

    // Bail if there is no s-pmsi route received (no active sender)
    if (state->spmsi_routes_received().empty())
        return;

    // Use mvpn type3 spmsi route originator address as the source address.
    *addrp = (*(state->spmsi_routes_received().begin()))->
                GetPrefix().originator();
    MVPN_ERMVPN_RT_LOG(ermvpn_route, "Found Source Address for RPF Check " +
                       addrp->to_string());
}

UpdateInfo *MvpnProjectManager::GetType7UpdateInfo(MvpnRoute *route) {
    BgpAttrPtr attr = route->BestPath()->GetAttr();
    UpdateInfo *uinfo = new UpdateInfo;
    uinfo->roattr = RibOutAttr(table(), route, attr.get(), 0, false, true);
    return uinfo;
}

UpdateInfo *MvpnProjectManager::GetUpdateInfo(MvpnRoute *route) {
    assert((route->GetPrefix().type() == MvpnPrefix::SourceActiveADRoute) ||
            (route->GetPrefix().type() == MvpnPrefix::SourceTreeJoinRoute));

    if (route->GetPrefix().type() == MvpnPrefix::SourceTreeJoinRoute)
        return GetType7UpdateInfo(route);
    MvpnStatePtr state = GetState(route);

    // If there is no imported leaf-ad route, then essentially there is no
    // olist that can be formed. Route can be withdrawn if already advertised.
    if (!state || state->leafad_routes_attr_received().empty())
        return NULL;

    // Retrieve olist element from each of the imported type-4 leaf-ad route.
    BgpOListSpec olist_spec(BgpAttribute::OList);
    BOOST_FOREACH(MvpnState::RoutesMap::value_type &iter,
                  state->leafad_routes_attr_received()) {
        BgpAttrPtr attr = iter.second;
        const PmsiTunnel *pmsi = attr->pmsi_tunnel();
        if (!pmsi)
            continue;
        if (pmsi->tunnel_type() != PmsiTunnelSpec::IngressReplication)
            continue;
        const ExtCommunity *extcomm = attr->ext_community();
        uint32_t label = attr->pmsi_tunnel()->GetLabel(extcomm);
        if (!label)
            continue;
        BgpOListElem elem(pmsi->identifier(), label,
            extcomm ? extcomm->GetTunnelEncap() : vector<string>());
        olist_spec.elements.push_back(elem);
        MVPN_RT_LOG(route, "Encoded olist " + pmsi->pmsi_tunnel().ToString());
    }

    if (olist_spec.elements.empty())
        return NULL;

    BgpAttrDB *attr_db = table()->server()->attr_db();
    BgpAttrPtr attr = attr_db->ReplaceOListAndLocate(
        route->BestPath()->GetAttr(), &olist_spec);
    UpdateInfo *uinfo = new UpdateInfo;
    uinfo->roattr = RibOutAttr(table(), route, attr.get(), 0, false, true);
    return uinfo;
}
