/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <cmn/agent_cmn.h>

#include <base/lifetime.h>
#include <base/parse_object.h>
#include <ifmap/ifmap_link.h>
#include <ifmap/ifmap_table.h>

#include <bgp_schema_types.h>
#include <vnc_cfg_types.h>

#include <cfg/cfg_init.h>
#include <cfg/cfg_listener.h>
#include <route/route.h>
#include <oper/route_common.h>
#include <oper/vn.h>
#include <oper/vrf.h>
#include <oper/peer.h>
#include <oper/mirror_table.h>
#include <oper/agent_route_walker.h>
#include <controller/controller_init.h>
#include <controller/controller_vrf_export.h>
#include <oper/agent_sandesh.h>
#include <oper/nexthop.h>

using namespace std;
using namespace autogen;

VrfTable *VrfTable::vrf_table_;

class VrfEntry::DeleteActor : public LifetimeActor {
  public:
    DeleteActor(VrfEntry *vrf) : 
        LifetimeActor((static_cast<VrfTable *>(vrf->get_table()))->
                      agent()->lifetime_manager()), table_(vrf) {
    }
    virtual ~DeleteActor() { 
    }
    virtual bool MayDelete() const {
        //No route entry present, then this VRF can be deleted
        return true;
    }
    virtual void Shutdown() {
    }
    virtual void Destroy() {
        table_->SendObjectLog(AgentLogEvent::DELETE);
        table_ = NULL;
    }

  private:
    VrfEntryRef table_;
};

VrfEntry::VrfEntry(const string &name, uint32_t flags) : 
        name_(name), id_(kInvalidIndex), flags_(flags),
        walkid_(DBTableWalker::kInvalidWalkerId), deleter_(NULL),
        rt_table_db_(), delete_timeout_timer_(NULL) { 
}

VrfEntry::~VrfEntry() {
    if (id_ != kInvalidIndex) {
        VrfTable *table = static_cast<VrfTable *>(get_table());
        table->FreeVrfId(id_);
        table->VrfReuse(GetName());
    }
    //Delete timer
    if (delete_timeout_timer_)
        TimerManager::DeleteTimer(delete_timeout_timer_);
}

bool VrfEntry::IsLess(const DBEntry &rhs) const {
    const VrfEntry &a = static_cast<const VrfEntry &>(rhs);
    return (name_ < a.name_);
}

string VrfEntry::ToString() const {
    return "VRF";
}

void VrfEntry::PostAdd() {
    // get_table() would return NULL in Add(), so move dependent functions and 
    // initialization to PostAdd
    deleter_.reset(new DeleteActor(this));
    // Create the route-tables and insert them into dbtree_
    Agent::RouteTableType type = Agent::INET4_UNICAST;
    DB *db = get_table()->database();
    rt_table_db_[type] = static_cast<AgentRouteTable *>
        (db->CreateTable(name_ + AgentRouteTable::GetSuffix(type)));
    rt_table_db_[type]->SetVrf(this);
    ((VrfTable *)get_table())->dbtree_[type].insert(VrfTable::VrfDbPair(name_, 
                                                        rt_table_db_[type]));

    type = Agent::INET4_MULTICAST;
    rt_table_db_[type] = static_cast<AgentRouteTable *>
        (db->CreateTable(name_ + AgentRouteTable::GetSuffix(type)));
    rt_table_db_[type]->SetVrf(this);
    ((VrfTable *)get_table())->dbtree_[type].insert(VrfTable::VrfDbPair(name_, 
                                                        rt_table_db_[type]));

    type = Agent::LAYER2;
    rt_table_db_[type] = static_cast<AgentRouteTable *>
        (db->CreateTable(name_ + AgentRouteTable::GetSuffix(type)));
    rt_table_db_[type]->SetVrf(this);
    ((VrfTable *)get_table())->dbtree_[type].insert(VrfTable::VrfDbPair(name_, 
                                                        rt_table_db_[type]));

    type = Agent::INET6_UNICAST;
    rt_table_db_[type] = static_cast<AgentRouteTable *>
        (db->CreateTable(name_ + AgentRouteTable::GetSuffix(type)));
    rt_table_db_[type]->SetVrf(this);
    ((VrfTable *)get_table())->dbtree_[type].insert(VrfTable::VrfDbPair(name_, 
                                                        rt_table_db_[type]));
}

bool VrfEntry::CanDelete(DBRequest *req) {
    VrfData *data = static_cast<VrfData *>(req->data.get());
    // Update flags
    flags_ &= ~data->flags_;
    // Do not delete the VRF if config VRF or gateway VRF flag is still set
    return flags_ ? false : true;
}

DBEntryBase::KeyPtr VrfEntry::GetDBRequestKey() const {
    VrfKey *key = new VrfKey(name_);
    return DBEntryBase::KeyPtr(key);
}

void VrfEntry::SetKey(const DBRequestKey *key) { 
    const VrfKey *k = static_cast<const VrfKey *>(key);
    name_ = k->name_;
}

Inet4UnicastRouteEntry *VrfEntry::GetUcRoute(const Ip4Address &addr) const {
    Inet4UnicastAgentRouteTable *table;
    table = static_cast<Inet4UnicastAgentRouteTable *>
        (GetInet4UnicastRouteTable());
    if (table == NULL)
        return NULL;

    return table->FindLPM(addr);
}

Inet4UnicastRouteEntry *VrfEntry::GetUcRoute(const Inet4UnicastRouteEntry &rt_key) const {
    Inet4UnicastAgentRouteTable *table = static_cast<Inet4UnicastAgentRouteTable *>
        (GetInet4UnicastRouteTable());
    if (table == NULL)
        return NULL;

    return table->FindLPM(rt_key);
}

Inet6UnicastRouteEntry *VrfEntry::GetUcRoute(const Ip6Address &addr) const {
    Inet6UnicastAgentRouteTable *table;
    table = static_cast<Inet6UnicastAgentRouteTable *>
        (GetInet6UnicastRouteTable());
    if (table == NULL)
        return NULL;

    return table->FindLPM(addr);
}

Inet6UnicastRouteEntry *VrfEntry::GetUcRoute
(const Inet6UnicastRouteEntry &rt_key) const {
    Inet6UnicastAgentRouteTable *table = NULL;
    table = static_cast<Inet6UnicastAgentRouteTable *>
        (GetInet6UnicastRouteTable());
    if (table == NULL)
        return NULL;

    return table->FindLPM(rt_key);
}

LifetimeActor *VrfEntry::deleter() {
    return deleter_.get();
}

AgentRouteTable *VrfEntry::GetRouteTable(uint8_t table_type) const {
    return rt_table_db_[table_type];
}

AgentRouteTable *VrfEntry::GetInet4UnicastRouteTable() const {
    return rt_table_db_[Agent::INET4_UNICAST];
}

AgentRouteTable *VrfEntry::GetInet4MulticastRouteTable() const {
    return rt_table_db_[Agent::INET4_MULTICAST];
}

AgentRouteTable *VrfEntry::GetLayer2RouteTable() const {
    return rt_table_db_[Agent::LAYER2];
}

AgentRouteTable *VrfEntry::GetInet6UnicastRouteTable() const {
    return rt_table_db_[Agent::INET6_UNICAST];
}

bool VrfEntry::DBEntrySandesh(Sandesh *sresp, std::string &name) const {
    VrfListResp *resp = static_cast<VrfListResp *>(sresp);

    if (name.empty() || GetName() == name) {
        VrfSandeshData data;
        data.set_name(GetName());
        data.set_ucindex(vrf_id());
        data.set_mcindex(vrf_id());
        data.set_l2index(vrf_id());
        data.set_uc6index(vrf_id());
        std::string vrf_flags;
        if (flags() & VrfData::ConfigVrf)
            vrf_flags += "Config; ";
        if (flags() & VrfData::GwVrf)
            vrf_flags += "Gateway; ";
        data.set_source(vrf_flags);

        std::vector<VrfSandeshData> &list = 
                const_cast<std::vector<VrfSandeshData>&>(resp->get_vrf_list());
        list.push_back(data);
        return true;
    }

    return false;
}

void VrfEntry::SendObjectLog(AgentLogEvent::type event) const {
    VrfObjectLogInfo vrf;
    string str;
    switch (event) {
        case AgentLogEvent::ADD:
            str.assign("Addition ");
            break;
        case AgentLogEvent::DELETE:
            str.assign("Deletion ");
            break;
        case AgentLogEvent::CHANGE:
            str.assign("Modification ");
            break;
        case AgentLogEvent::DELETE_TRIGGER:
            str.assign("Deletion Triggered ");
            break;
        default:
            str.assign("");
            break;
    }
    vrf.set_event(str);
    vrf.set_name(name_);
    vrf.set_index(id_);
    VRF_OBJECT_LOG_LOG("AgentVrf", SandeshLevel::SYS_INFO, vrf);
}

bool VrfEntry::DeleteTimeout() {
    std::ostringstream str;
    str << "Unicast routes: " << rt_table_db_[Agent::INET4_UNICAST]->Size();
    str << " Mutlicast routes: " << rt_table_db_[Agent::INET4_MULTICAST]->Size();
    str << " Layer2 routes: " << rt_table_db_[Agent::LAYER2]->Size();
    str << "Unicast v6 routes: " << rt_table_db_[Agent::INET6_UNICAST]->Size();
    str << " Reference: " << GetRefCount();
    OPER_TRACE(Vrf, "VRF delete failed, " + str.str(), name_);
    assert(0);
    return false;
}

void VrfEntry::StartDeleteTimer() {
    Agent *agent = (static_cast<VrfTable *>(get_table()))->agent();
    delete_timeout_timer_ = TimerManager::CreateTimer(
                                *(agent->event_manager())->io_service(),
                                "VrfDeleteTimer");
    delete_timeout_timer_->Start(kDeleteTimeout, 
                                 boost::bind(&VrfEntry::DeleteTimeout,
                                 this));
}

void VrfEntry::CancelDeleteTimer() {
    delete_timeout_timer_->Cancel();
}


std::auto_ptr<DBEntry> VrfTable::AllocEntry(const DBRequestKey *k) const {
    const VrfKey *key = static_cast<const VrfKey *>(k);
    VrfEntry *vrf = new VrfEntry(key->name_, 0);
    return std::auto_ptr<DBEntry>(static_cast<DBEntry *>(vrf));
}

DBEntry *VrfTable::Add(const DBRequest *req) {
    VrfKey *key = static_cast<VrfKey *>(req->key.get());
    VrfData *data = static_cast<VrfData *>(req->data.get());
    VrfEntry *vrf = new VrfEntry(key->name_, data->flags_);

    // Add VRF into name based tree
    if (FindVrfFromName(key->name_)) {
        delete vrf;
        assert(0);
        return NULL;
    }
    vrf->id_ = index_table_.Insert(vrf);
    name_tree_.insert( VrfNamePair(key->name_, vrf));

    vrf->SendObjectLog(AgentLogEvent::ADD);

    return vrf;
}

bool VrfTable::OnChange(DBEntry *entry, const DBRequest *req) {
    VrfEntry *vrf = static_cast<VrfEntry *>(entry);
    VrfData *data = static_cast<VrfData *>(req->data.get());
    vrf->set_flags(data->flags_);

    return false;
}

void VrfTable::Delete(DBEntry *entry, const DBRequest *req) {
    VrfEntry *vrf = static_cast<VrfEntry *>(entry);
    vrf->deleter_->Delete();
    vrf->StartDeleteTimer();
    vrf->SendObjectLog(AgentLogEvent::DELETE_TRIGGER);
}

void VrfTable::VrfReuse(const std::string  name) {
    IFMapTable::RequestKey req_key;
    req_key.id_type = "routing-instance";
    req_key.id_name = name;
    IFMapNode *node = IFMapAgentTable::TableEntryLookup(database(), &req_key);

    if (!node || node->IsDeleted()) {
        return;
    }

    OPER_TRACE(Vrf, "Resyncing configuration for VRF: ", name);
    agent()->cfg_listener()->NodeReSync(node);
}

void VrfTable::OnZeroRefcount(AgentDBEntry *e) {
    VrfEntry *vrf = static_cast<VrfEntry *>(e);
    if (e->IsDeleted()) {
        int table_type;
        for (table_type = 0; table_type < Agent::ROUTE_TABLE_MAX; table_type++) {
            database()->RemoveTable(vrf->GetRouteTable(table_type));
            dbtree_[table_type].erase(vrf->GetName());
            delete vrf->GetRouteTable(table_type);
        }

        name_tree_.erase(vrf->GetName());
        vrf->CancelDeleteTimer();
    }
}

DBTableBase *VrfTable::CreateTable(DB *db, const std::string &name) {
    vrf_table_ = new VrfTable(db, name);
    vrf_table_->Init();
    return vrf_table_;
};

VrfEntry *VrfTable::FindVrfFromName(const string &name) {
    VrfNameTree::const_iterator it;
    
    it = name_tree_.find(name);
    if (it == name_tree_.end()) {
        return NULL;
    }

    return static_cast<VrfEntry *>(it->second);
}

VrfEntry *VrfTable::FindVrfFromId(size_t index) {
    VrfEntry *vrf = index_table_.At(index);
    if (vrf && vrf->IsDeleted() == false) {
        return vrf;
    }
    return NULL;
}

AgentRouteTable *VrfTable::GetInet4UnicastRouteTable(const string &vrf_name) {
    return GetRouteTable(vrf_name, Agent::INET4_UNICAST);
}

AgentRouteTable *VrfTable::GetInet4MulticastRouteTable(const string &vrf_name) {
    return GetRouteTable(vrf_name, Agent::INET4_MULTICAST);
}

AgentRouteTable *VrfTable::GetLayer2RouteTable(const string &vrf_name) {
    return GetRouteTable(vrf_name, Agent::LAYER2);
}

AgentRouteTable *VrfTable::GetInet6UnicastRouteTable(const string &vrf_name) {
    return GetRouteTable(vrf_name, Agent::INET6_UNICAST);
}

AgentRouteTable *VrfTable::GetRouteTable(const string &vrf_name,
                                         uint8_t table_type) {
    VrfDbTree::const_iterator it;
    
    it = dbtree_[table_type].find(vrf_name);
    if (it == dbtree_[table_type].end()) {
        return NULL;
    }

    return static_cast<AgentRouteTable *>(it->second);
}

void VrfTable::CreateVrfReq(const string &name, uint32_t flags) {
    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    req.key.reset(new VrfKey(name));
    req.data.reset(new VrfData(flags));
    Enqueue(&req);
}

void VrfTable::CreateVrf(const string &name, uint32_t flags) {
    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    req.key.reset(new VrfKey(name));
    req.data.reset(new VrfData(flags));
    Process(req);
}

void VrfTable::DeleteVrfReq(const string &name, uint32_t flags) {
    DBRequest req(DBRequest::DB_ENTRY_DELETE);
    req.key.reset(new VrfKey(name));
    req.data.reset(new VrfData(flags));
    Enqueue(&req);
}

void VrfTable::DeleteVrf(const string &name, uint32_t flags) {
    DBRequest req(DBRequest::DB_ENTRY_DELETE);
    req.key.reset(new VrfKey(name));
    req.data.reset(new VrfData(flags));
    Process(req);
}

void VrfTable::CreateStaticVrf(const string &name) {
    static_vrf_set_.insert(name);
    CreateVrf(name);
}

void VrfTable::DeleteStaticVrf(const string &name) {
    static_vrf_set_.erase(name);
    DeleteVrfReq(name);
}

void VrfTable::Input(DBTablePartition *partition, DBClient *client,
                     DBRequest *req) {

    VrfKey *key = static_cast<VrfKey *>(req->key.get());
    VrfEntry *entry = static_cast<VrfEntry *>(partition->Find(key));

    if (entry && entry->IsDeleted()) {
        OPER_TRACE(Vrf, "VRF pending delete, Ignoring DB operation for ",
                   entry->GetName());
        return;
    }

    AgentDBTable::Input(partition, client, req);
    return;
}

bool VrfTable::CanNotify(IFMapNode *node) {
    VrfKey key(node->name());
    VrfEntry *entry = static_cast<VrfEntry *>(Find(&key, true));
    // Check if there is an entry with given name in *any* DBState
    if (entry && entry->IsDeleted()) {
        OPER_TRACE(Vrf, "VRF pending delete, Ignoring config for ", node->name());
        return false;
    }

    return true;
}

bool VrfTable::IFNodeToReq(IFMapNode *node, DBRequest &req) {
    VrfKey *key = new VrfKey(node->name());

    //Trigger add or delete only for non fabric VRF
    if (node->IsDeleted()) {
        if (IsStaticVrf(node->name())) {
            //Fabric and link-local VRF will not be deleted,
            //upon config delete
            req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
        } else {
            req.oper = DBRequest::DB_ENTRY_DELETE;
        }
    } else {
        req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
        IFMapAgentTable *table =
            static_cast<IFMapAgentTable *>(node->table());
        for (DBGraphVertex::adjacency_iterator iter =
                node->begin(table->GetGraph());
                iter != node->end(table->GetGraph()); ++iter) {
            IFMapNode *adj_node =
                static_cast<IFMapNode *>(iter.operator->());

            if (iter->IsDeleted() ||
                    (adj_node->table() != agent()->cfg()->cfg_vn_table())) {
                continue;
            }

            VirtualNetwork *cfg =
                static_cast <VirtualNetwork *> (adj_node->GetObject());
            if (cfg == NULL) {
                continue;
            }
            autogen::VirtualNetworkType properties = cfg->properties();
        }
    }

    //When VRF config delete comes, first enqueue VRF delete
    //so that when link evaluation happens, all point to deleted VRF
    VrfData *data = new VrfData(VrfData::ConfigVrf);
    req.key.reset(key);
    req.data.reset(data);
    Enqueue(&req);

    if (node->IsDeleted()) {
        return false;
    }

    // Resync any vmport dependent on this VRF
    // While traversing the path
    // virtual-machine-interface <-> virtual-machine-interface-routing-instance
    // <-> routing-instance path, we may have skipped a routing-instance that
    // failed SkipNode()
    IFMapAgentTable *table = static_cast<IFMapAgentTable *>(node->table());
    for (DBGraphVertex::adjacency_iterator iter =
         node->begin(table->GetGraph());
         iter != node->end(table->GetGraph()); ++iter) {
        IFMapNode *adj_node = static_cast<IFMapNode *>(iter.operator->());
        if (agent()->cfg_listener()->SkipNode
            (adj_node, agent()->cfg()->cfg_vm_port_vrf_table())) {
            continue;
        }

        agent()->interface_table()->VmInterfaceVrfSync(adj_node);
    }

    // Resync dependent Floating-IP
    VmInterface::FloatingIpVrfSync(agent()->interface_table(), node);
    return false;
}

void VrfListReq::HandleRequest() const {
    AgentVrfSandesh *sand = new AgentVrfSandesh(context(), get_name());
    sand->DoSandesh();
}

class RouteDeleteWalker : public AgentRouteWalker {
public:
    RouteDeleteWalker(Agent *agent) : 
        AgentRouteWalker(agent, AgentRouteWalker::ALL) {
    }

    ~RouteDeleteWalker() { }

    //Override route notification
    bool RouteWalkNotify(DBTablePartBase *partition, DBEntryBase *e) {
        AgentRoute *rt = static_cast<AgentRoute *>(e); 
        for(Route::PathList::const_iterator it = rt->GetPathList().begin();
            it != rt->GetPathList().end(); ) {
            const AgentPath *path =
                static_cast<const AgentPath *>(it.operator->());
            Route::PathList::const_iterator next = ++it;

            DBRequest req(DBRequest::DB_ENTRY_DELETE);
            req.key = e->GetDBRequestKey();
            AgentRouteKey *key = static_cast<AgentRouteKey *>(req.key.get());
            key->peer_ = path->peer();
            (static_cast<AgentRouteTable *>(e->get_table()))->Process(req);
            it = next;
        }

        return true;
    }

    static void WalkDone(RouteDeleteWalker *walker) {
        walker->agent()->vrf_table()->reset_shutdown_walk();
        delete walker;
        walk_done_++;
    }

    static uint32_t walk_start_;
    static uint32_t walk_done_;
};
uint32_t RouteDeleteWalker::walk_start_;
uint32_t RouteDeleteWalker::walk_done_;

void VrfTable::DeleteRoutes() {
    assert(shutdown_walk_ == NULL);
    RouteDeleteWalker *walker = new RouteDeleteWalker(agent());
    shutdown_walk_ = walker;
    walker->WalkDoneCallback
        (boost::bind(&RouteDeleteWalker::WalkDone, walker));
    walker->walk_start_++;
    walker->StartVrfWalk();
}

class VrfDeleteWalker : public AgentRouteWalker {
public:
    VrfDeleteWalker(Agent *agent) : 
        AgentRouteWalker(agent, AgentRouteWalker::ALL) {
    }

    ~VrfDeleteWalker() { }

    //Override vrf notification
    bool VrfWalkNotify(DBTablePartBase *partition, DBEntryBase *e) {
        DBRequest req(DBRequest::DB_ENTRY_DELETE);
        req.key = e->GetDBRequestKey();
        (static_cast<VrfTable *>(e->get_table()))->Process(req);
        return true;
    }

    //Override route notification
    bool RouteWalkNotify(DBTablePartBase *partition, DBEntryBase *e) {
        assert(0);
    }

    static void WalkDone(VrfDeleteWalker *walker) { 
        delete walker;
    }

private:
};

void VrfTable::Shutdown() {
    delete shutdown_walk_;
    VrfDeleteWalker *walker = new VrfDeleteWalker(agent());
    shutdown_walk_ = walker;
    walker->WalkDoneCallback (boost::bind(&VrfDeleteWalker::WalkDone, walker));
    walker->StartVrfWalk();
}
