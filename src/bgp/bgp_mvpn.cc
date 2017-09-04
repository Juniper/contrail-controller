/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/bgp_mvpn.h"

#include <boost/foreach.hpp>

#include "base/task_annotations.h"
#include "bgp/ermvpn/ermvpn_route.h"
#include "bgp/ermvpn/ermvpn_table.h"
#include "bgp/bgp_multicast.h"
#include "bgp/bgp_server.h"
#include "bgp/bgp_update.h"
#include "bgp/extended-community/vrf_route_import.h"
#include "bgp/mvpn/mvpn_table.h"
#include "bgp/routing-instance/path_resolver.h"
#include "bgp/routing-instance/routing_instance.h"
#include "bgp/rtarget/rtarget_address.h"
#include "bgp/tunnel_encap/tunnel_encap.h"

bool MvpnManager::enable_;

using std::make_pair;
using std::ostringstream;
using std::pair;
using std::string;
using std::vector;

MvpnState::MvpnState(const SG &sg, StatesMap *states) :
    sg_(sg), global_ermvpn_tree_rt_(NULL), spmsi_rt_(NULL), states_(states) {
    refcount_ = 0;
}

MvpnState::~MvpnState() {
    assert(!global_ermvpn_tree_rt_);
    assert(spmsi_routes_received_.empty());
    assert(leafad_routes_received_.empty());
}

class MvpnProjectManager::DeleteActor : public LifetimeActor {
public:
    explicit DeleteActor(MvpnProjectManager *manager)
        : LifetimeActor(manager->table_->routing_instance()->server()->
                lifetime_manager()), manager_(manager) {
    }
    virtual ~DeleteActor() {
    }

    virtual bool MayDelete() const {
        return true;
    }

    virtual void Shutdown() {
    }

    virtual void Destroy() {
        manager_->table_->DestroyMvpnProjectManager();
    }

private:
    MvpnProjectManager *manager_;
};

MvpnProjectManager::MvpnProjectManager(ErmVpnTable *table)
        : table_(table),
          listener_id_(DBTable::kInvalidId),
          table_delete_ref_(this, table->deleter()) {
    deleter_.reset(new DeleteActor(this));
}

MvpnProjectManager::~MvpnProjectManager() {
}

void MvpnProjectManager::Initialize() {
    AllocPartitions();

    listener_id_ = table_->Register(
        boost::bind(&MvpnProjectManager::RouteListener, this, _1, _2),
        "MvpnProjectManager");
}

void MvpnProjectManager::Terminate() {
    table_->Unregister(listener_id_);
    FreePartitions();
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

MvpnStatePtr MvpnProjectManager::GetState(MvpnRoute *route) const {
    return static_cast<const MvpnProjectManager *>(this)->GetState(route);
}

MvpnStatePtr MvpnProjectManager::GetState(MvpnRoute *route) {
    MvpnState::SG sg(route->GetPrefix().source(), route->GetPrefix().group());
    return GetPartition(route->get_table_partition()->index())->GetState(sg);
}

MvpnProjectManagerPartition::MvpnProjectManagerPartition(
        MvpnProjectManager *manager, int part_id)
    : manager_(manager), part_id_(part_id) {
}

MvpnProjectManagerPartition::~MvpnProjectManagerPartition() {
}

MvpnStatePtr MvpnProjectManagerPartition::CreateState(const SG &sg) {
    MvpnStatePtr state(new MvpnState(sg, &states_));
    assert(states_.insert(make_pair(sg, state.get())).second);
    return state;
}

MvpnStatePtr MvpnProjectManagerPartition::LocateState(const SG &sg) {
    MvpnStatePtr mvpn_state = GetState(sg);
    return mvpn_state ?: CreateState(sg);
}

MvpnStatePtr MvpnProjectManagerPartition::GetState(const SG &sg) const {
    MvpnState::StatesMap::const_iterator iter = states_.find(sg);
    return iter != states_.end() ?  iter->second : NULL;
}

MvpnStatePtr MvpnProjectManagerPartition::GetState(const SG &sg) {
    MvpnState::StatesMap::iterator iter = states_.find(sg);
    return iter != states_.end() ?  iter->second : NULL;
}

void MvpnProjectManagerPartition::DeleteState(MvpnStatePtr mvpn_state) {
}

MvpnNeighbor::MvpnNeighbor() : source_as_(0) {
}

MvpnNeighbor::MvpnNeighbor(const RouteDistinguisher &rd,
                           const IpAddress &originator) :
        rd_(rd), originator_(originator), source_as_(0) {
}

MvpnNeighbor::MvpnNeighbor(const RouteDistinguisher &rd, uint32_t source_as) :
        rd_(rd), source_as_(source_as) {
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

bool MvpnManager::FindNeighbor(const RouteDistinguisher &rd,
                               MvpnNeighbor *nbr) const {
    return false;
}

MvpnState::SG::SG(const Ip4Address &source, const Ip4Address &group) :
    source(IpAddress(source)), group(IpAddress(group)) {
}

MvpnState::SG::SG(const ErmVpnRoute *route) :
        source(route->GetPrefix().source()),
        group(route->GetPrefix().group()) {
}

MvpnState::SG::SG(const MvpnRoute *route) :
        source(route->GetPrefix().source()),
        group(route->GetPrefix().group()) {
}

MvpnState::SG::SG(const IpAddress &source, const IpAddress &group) :
    source(source), group(group) {
}

bool MvpnState::SG::operator<(const SG &other)  const {
    return (source < other.source) ?  true : (group < other.source);
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

const MvpnState::RoutesSet &MvpnState::spmsi_routes_received() const {
    return spmsi_routes_received_;
}

MvpnState::RoutesSet &MvpnState::spmsi_routes_received() {
    return spmsi_routes_received_;
}

const MvpnState::RoutesMap &MvpnState::leafad_routes_received() const {
    return leafad_routes_received_;
}

MvpnState::RoutesMap &MvpnState::leafad_routes_received() {
    return leafad_routes_received_;
}

void MvpnState::set_global_ermvpn_tree_rt(ErmVpnRoute *global_ermvpn_tree_rt) {
    global_ermvpn_tree_rt_ = global_ermvpn_tree_rt;
}

void MvpnState::set_spmsi_rt(MvpnRoute *spmsi_rt) {
    spmsi_rt_ = spmsi_rt;
}

MvpnDBState::MvpnDBState() : state_(NULL), route_(NULL) {
}

MvpnDBState::MvpnDBState(MvpnStatePtr state) : state_(state) , route_(NULL) {
}

MvpnDBState::MvpnDBState(MvpnRoute *route) : state_(NULL) , route_(route) {
}

MvpnDBState::MvpnDBState(MvpnStatePtr state, MvpnRoute *route) :
        state_(state) , route_(route) {
}

MvpnDBState::~MvpnDBState() {
    set_state(NULL);
}

MvpnStatePtr MvpnDBState::state() {
    return state_;
}

const MvpnStatePtr MvpnDBState::state() const {
    return state_;
}

MvpnRoute *MvpnDBState::route() {
    return route_;
}

const MvpnRoute *MvpnDBState::route() const {
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
        return true;
    }

    virtual void Shutdown() {
    }

    virtual void Destroy() {
        manager_->table_->DestroyManager();
    }

private:
    MvpnManager *manager_;
};

MvpnManager::MvpnManager(MvpnTable *table)
        : table_(table),
          listener_id_(DBTable::kInvalidId),
          table_delete_ref_(this, table->deleter()) {
    deleter_.reset(new DeleteActor(this));
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

PathResolver *MvpnManager::path_resolver() {
    return table_->path_resolver();
}

PathResolver *MvpnManager::path_resolver() const {
    return table_->path_resolver();
}

void MvpnManager::Terminate() {
    table_->Unregister(listener_id_);
    FreePartitions();
}

LifetimeActor *MvpnManager::deleter() {
    return deleter_.get();
}

const LifetimeActor *MvpnManager::deleter() const {
    return deleter_.get();
}

void MvpnManager::ManagedDelete() {
    deleter_->Delete();
}

bool MvpnManager::deleted() const {
    return deleter_->IsDeleted();
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

MvpnManagerPartition *MvpnManager::GetPartition(int part_id) {
    return partitions_[part_id];
}

const MvpnManagerPartition *MvpnManager::GetPartition(int part_id) const {
    return GetPartition(part_id);
}

void MvpnManager::NotifyAllRoutes() {
    table_->NotifyAllEntries();
}

MvpnTable *MvpnManagerPartition::table() {
    return manager_->table();
}

const MvpnTable *MvpnManagerPartition::table() const {
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
    return NULL;
}

const MvpnProjectManager *MvpnManager::GetProjectManager() const {
    return NULL;
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
    if (!project_manager_partition)
        return NULL;
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

MvpnStatePtr MvpnManagerPartition::GetState(ErmVpnRoute *rt) const {
    const MvpnProjectManagerPartition *project_manager_partition =
        GetProjectManagerPartition();
    if (!project_manager_partition)
        return NULL;
    MvpnState::SG sg = MvpnState::SG(rt->GetPrefix().source(),
                                     rt->GetPrefix().group());
    return project_manager_partition->GetState(sg);
}

MvpnStatePtr MvpnManagerPartition::GetState(ErmVpnRoute *rt) {
    return static_cast<const MvpnManagerPartition *>(this)->GetState(rt);
}

void MvpnManagerPartition::DeleteState(MvpnStatePtr state) {
    MvpnProjectManagerPartition *project_manager_partition =
        GetProjectManagerPartition();
    if (!project_manager_partition)
        return;
    project_manager_partition->DeleteState(state);
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

////////////////////////////////////////////////////////////////////////////////

// Initialize MvpnManager by allcating one MvpnManagerPartition for each DB
// partition, and register a route listener for the MvpnTable.
void MvpnManager::Initialize() {
    assert(!table_->IsMaster());
    AllocPartitions();

    listener_id_ = table_->Register(
        boost::bind(&MvpnManager::RouteListener, this, _1, _2),
        "MvpnManager");
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
        UpdateNeighbor(route);
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

    // Process Type4 LeafAD route.
    if (route->GetPrefix().type() == MvpnPrefix::LeafADRoute) {
        partition->ProcessType4LeafADRoute(route);
        return;
    }
}

// Update MVPN neighbor list with create/delete/update of auto-discovery routes.
//
// Protect access to neighbors_ map with a mutex as the same be 'read' off other
// DB tasks in parallel. (Type-1 and Type-2 do not carrry any <S,G> information.
void MvpnManager::UpdateNeighbor(MvpnRoute *route) {
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

void MvpnProjectManagerPartition::RouteListener(DBEntryBase *db_entry) {
}

bool MvpnManagerPartition::ProcessType7SourceTreeJoinRoute(MvpnRoute *join_rt) {
    return false;
}

void MvpnManagerPartition::ProcessType4LeafADRoute(MvpnRoute *leaf_ad) {
}

// Process changes to Type3 S-PMSI routes by originating or deleting Type4 Leaf
// AD paths as appropriate.
void MvpnManagerPartition::ProcessType3SPMSIRoute(MvpnRoute *spmsi_rt) {
}
