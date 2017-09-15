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

MvpnState::MvpnState(const SG &sg, StatesMap *states) : sg_(sg),
        global_ermvpn_tree_rt_(NULL), spmsi_rt_(NULL), states_(states) {
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
    listener_id_ = DBTable::kInvalidId;
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
          identifier_listener_id_(-1),
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
    MvpnRoute *type1_route = table_->FindType1ADRoute();
    if (type1_route) {
        BgpPath *path = type1_route->FindPath(BgpPath::Local, 0);
        if (path)
            type1_route->DeletePath(path);
        type1_route->NotifyOrDelete();
    }

    MvpnRoute *type2_route = table_->FindType2ADRoute();
    if (type2_route) {
        BgpPath *path = type2_route->FindPath(BgpPath::Local, 0);
        if (path)
            type2_route->DeletePath(path);
        type2_route->NotifyOrDelete();
    }

    if (identifier_listener_id_ != -1) {
        table_->server()->UnregisterIdentifierUpdateCallback(
            identifier_listener_id_);
        identifier_listener_id_ = -1;
    }
    table_->Unregister(listener_id_);
    listener_id_ = DBTable::kInvalidId;
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
    return table_->GetProjectManager();
}

const MvpnProjectManager *MvpnManager::GetProjectManager() const {
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
        uint32_t *label, Ip4Address *address, vector<string> *encap) const {
    if (!table()->tree_manager())
        return false;
    return table()->tree_manager()->GetForestNodePMSI(rt, label, address,
                                                      encap);
}

bool MvpnManagerPartition::GetForestNodePMSI(ErmVpnRoute *rt, uint32_t *label,
        Ip4Address *address, vector<string> *encap) const {
    const MvpnProjectManagerPartition *pm = GetProjectManagerPartition();
    return pm ?  pm->GetForestNodePMSI(rt, label, address, encap) : false;
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

    if (!IsEnabled())
        return;

    identifier_listener_id_ =
        table_->server()->RegisterIdentifierUpdateCallback(boost::bind(
            &MvpnManager::ReOriginateType1Route, this, _1));
    OriginateType1Route();
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
    BgpServer *server = table()->server();

    // Check for the presence of valid identifier.
    if (!table()->server()->bgp_identifier())
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
    RouteDistinguisher rd = route->GetPrefix().route_distinguisher();

    // Check if an entry is already present.
    MvpnNeighbor old_neighbor;
    bool found = FindNeighbor(rd, &old_neighbor);

    if (!route->IsUsable()) {
        if (!found)
            return;
        tbb::reader_writer_lock::scoped_lock lock(neighbors_mutex_);
        neighbors_.erase(rd);
        return;
    }

    // Ignore primary paths.
    if (!route->BestPath()->IsSecondary())
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
}

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

void MvpnProjectManagerPartition::RouteListener(DBEntryBase *db_entry) {
    ErmVpnRoute *ermvpn_route = dynamic_cast<ErmVpnRoute *>(db_entry);
    assert(ermvpn_route);

    // We only care about global tree routes for mvpn stitching.
    if (ermvpn_route->GetPrefix().type() != ErmVpnPrefix::GlobalTreeRoute)
        return;

    MvpnDBState *mvpn_dbstate = dynamic_cast<MvpnDBState *>(
        ermvpn_route->GetState(table(), listener_id()));

    if (!IsUsableGlobalTreeRootRoute(ermvpn_route)) {
        if (!mvpn_dbstate)
            return;
        MvpnStatePtr mvpn_state = mvpn_dbstate->state();
        mvpn_state->set_global_ermvpn_tree_rt(NULL);

        // Notify all originated Type3 spmsi routes for PMSI re-computation.
        BOOST_FOREACH(MvpnRoute *route, mvpn_state->spmsi_routes_received()) {
            route->Notify();
        }
        ermvpn_route->ClearState(table(), listener_id());
        delete mvpn_dbstate;
        return;
    }

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
}

bool MvpnManagerPartition::ProcessType7SourceTreeJoinRoute(MvpnRoute *join_rt) {
    return false;
}

void MvpnManagerPartition::ProcessType4LeafADRoute(MvpnRoute *leaf_ad) {
}

// Process changes to Type3 S-PMSI routes by originating or deleting Type4 Leaf
// AD paths as appropriate.
void MvpnManagerPartition::ProcessType3SPMSIRoute(MvpnRoute *spmsi_rt) {
    // Retrieve any state associcated with this S-PMSI route.
    MvpnDBState *mvpn_dbstate = dynamic_cast<MvpnDBState *>(
        spmsi_rt->GetState(table(), listener_id()));

    MvpnRoute *leaf_ad_route = NULL;
    if (!spmsi_rt->IsUsable()) {
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
        spmsi_rt->ClearState(table(), listener_id());
        delete mvpn_dbstate;
        if (leaf_ad_route) {
            leaf_ad_route->NotifyOrDelete();
            NotifyForestNode(spmsi_rt->GetPrefix().source(),
                             spmsi_rt->GetPrefix().group());
        }
        return;
    }

    // Ignore notifications of primary S-PMSI paths.
    if (!spmsi_rt->BestPath()->IsSecondary())
        return;

    // A valid S-PMSI path has been imported to a table. Originate a new
    // LeafAD path, if GlobalErmVpnTreeRoute is available to stitch.
    // TODO(Ananth) If LeafInfoRequired bit is not set in the S-PMSI route,
    // then we do not need to originate a leaf ad route for this s-pmsi rt.
    MvpnStatePtr mvpn_state = LocateState(spmsi_rt);
    assert(mvpn_state);
    if (!mvpn_dbstate) {
        mvpn_dbstate = new MvpnDBState(mvpn_state);
        spmsi_rt->SetState(table(), listener_id(), mvpn_dbstate);
        assert(mvpn_state->spmsi_routes_received().insert(spmsi_rt).second);
    } else {
        leaf_ad_route = mvpn_dbstate->route();
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
    pmsi_spec.SetLabel(label, true);
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

    // Matching Type-3 S-PMSI route was found. Return the table that holds this
    // route, if it is usable.
    BgpTable *table = dynamic_cast<BgpTable *>(
        state->spmsi_rt()->get_table_partition()->parent());
    assert(table);

    // Update table list to let replicator invoke RouteReplicate() for this
    // LeafAD route for this table which has the corresponding Type3 SPMSI
    // route. This was originated as the 'Sender' since receiver joined to
    // the <C-S,G> group.
    secondary_tables->insert(table);
}
