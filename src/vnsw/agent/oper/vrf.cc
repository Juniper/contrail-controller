/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <cmn/agent_cmn.h>

#include <base/lifetime.h>
#include <base/parse_object.h>
#include <ifmap/ifmap_link.h>
#include <ifmap/ifmap_table.h>
#include <cfg/init_config.h>

#include <bgp_schema_types.h>
#include <vnc_cfg_types.h>

#include <route/route.h>
#include <oper/vn.h>
#include <oper/vrf.h>
#include <oper/inet4_ucroute.h>
#include <oper/inet4_mcroute.h>
#include <oper/mirror_table.h>
#include <controller/controller_vrf_export.h>
#include <oper/agent_sandesh.h>
#include <oper/nexthop.h>

using namespace std;
using namespace autogen;

VrfTable *VrfTable::vrf_table_;

class VrfEntry::DeleteActor : public LifetimeActor {
  public:
    DeleteActor(VrfEntry *vrf) : LifetimeActor(Agent::GetInstance()->GetLifetimeManager()), 
                                 table_(vrf) { 
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

class VrfEntry::VrfNHMap {
public:
    void AddNH(Ip4Address ip, ComponentNHData nh_data) {
        ComponentNHData::ComponentNHDataList component_list = nh_map_[ip];
        ComponentNHData::ComponentNHDataList::iterator it = 
            component_list.begin();
        while (it != component_list.end()) {
            if (*it == nh_data) {
                *it = nh_data;
                return;
            }
            it++;
        }
        nh_map_[ip].push_back(nh_data);
    }

        //Decrement the count of NH, a route points to.
    void DeleteNH(Ip4Address ip, ComponentNHData nh_data) {
        ComponentNHData::ComponentNHDataList::iterator it = 
                                                  nh_map_[ip].begin();
        while (it != nh_map_[ip].end()) {
            if (*it == nh_data) {
                nh_map_[ip].erase(it);
                break;
            }
            it++;
        }
        return;
    }

    uint32_t GetNHCount(Ip4Address ip) {
        return nh_map_[ip].size();
    }

    bool FindNH(const Ip4Address &ip, const ComponentNHData &nh_data) {
        ComponentNHData::ComponentNHDataList component_list = nh_map_[ip];
        ComponentNHData::ComponentNHDataList::iterator it = 
            component_list.begin();
        while (it != component_list.end()) {
            if (*it == nh_data) {
                return true;
            }
            it++;
        }
        return false;
    }

    ComponentNHData::ComponentNHDataList* GetNHList(Ip4Address ip) {
        return &nh_map_[ip];
    }

    void UpdateLabel(Ip4Address addr, uint32_t label) {
        label_map_[addr] = label;
    }

    uint32_t GetLabel(Ip4Address addr) {
        return label_map_[addr];
    }

private:
    std::map<Ip4Address, ComponentNHData::ComponentNHDataList> nh_map_;
    std::map<Ip4Address, uint32_t> label_map_;
};

VrfEntry::VrfEntry(const string &name) : 
        name_(name), id_(kInvalidIndex),
        inet4_uc_db_(NULL), inet4_mc_db_(NULL), 
        walkid_(DBTableWalker::kInvalidWalkerId), 
        deleter_(new DeleteActor(this)), nh_map_(new VrfNHMap),
        delete_timeout_timer_(NULL) {
}

VrfEntry::~VrfEntry() {
    if (id_ != kInvalidIndex) {
        VrfTable::GetInstance()->FreeVrfId(id_);
        Agent::GetInstance()->GetVrfTable()->VrfReuse(GetName());
    }
}

bool VrfEntry::IsLess(const DBEntry &rhs) const {
    const VrfEntry &a = static_cast<const VrfEntry &>(rhs);
    return (name_ < a.name_);
}

string VrfEntry::ToString() const {
    return "VRF";
}

DBEntryBase::KeyPtr VrfEntry::GetDBRequestKey() const {
    VrfKey *key = new VrfKey(name_);
    return DBEntryBase::KeyPtr(key);
}

void VrfEntry::SetKey(const DBRequestKey *key) { 
    const VrfKey *k = static_cast<const VrfKey *>(key);
    name_ = k->name_;
}

AgentDBTable *VrfEntry::DBToTable() const {
    return VrfTable::GetInstance();
}

Inet4UcRouteTable *VrfEntry::GetInet4UcRouteTable() const {
    return static_cast<Inet4UcRouteTable *>(inet4_uc_db_);
};

Inet4McRouteTable *VrfEntry::GetInet4McRouteTable() const {
    return static_cast<Inet4McRouteTable *>(inet4_mc_db_);
};

Inet4UcRoute *VrfEntry::GetUcRoute(const Ip4Address &addr) const {
    Inet4UcRouteTable *table = GetInet4UcRouteTable();
    if (table == NULL)
        return NULL;

    return table->FindLPM(addr);
}

bool VrfEntry::DelPeerRoutes(DBTablePartBase *part, DBEntryBase *entry, 
                             Peer *peer) {
    VrfEntry *vrf = static_cast<VrfEntry *>(entry);

    if (entry->IsDeleted()) {
        return true;
    }

    if (peer->GetType() == Peer::BGP_PEER) {
        BgpPeer *bgp_peer = static_cast<BgpPeer *>(peer);

        DBTableBase::ListenerId id = bgp_peer->GetVrfExportListenerId();
        VrfExport::State *state = 
            static_cast<VrfExport::State *>(vrf->GetState(part->parent(), id)); 
        if (state == NULL) {
            return true;
        }

        Inet4UcRouteTable *table = vrf->GetInet4UcRouteTable();
        DBTableWalker *walker = Agent::GetInstance()->GetDB()->GetWalker();

        if (state->inet4_uc_walkid_ != DBTableWalker::kInvalidWalkerId) { 
            AGENT_DBWALK_TRACE(AgentDBWalkLog, "Cancel  walk ", 
                               "Inet4UcRouteTable(DelPeerRoutes)",
                               state->inet4_uc_walkid_,
                               peer->GetName(), "Del Route");

            walker->WalkCancel(state->inet4_uc_walkid_);
        }

        state->inet4_uc_walkid_ = walker->WalkTable(table, NULL, 
            boost::bind(&Inet4UcRouteTable::DelPeerRoutes, table, _1, _2, peer), 
            boost::bind(&VrfEntry::DelPeerDone, _1, state));

        AGENT_DBWALK_TRACE(AgentDBWalkLog, "Start walk ", 
                           "Inet4UcRouteTable(DelPeerRoutes)",
                           state->inet4_uc_walkid_,
                           peer->GetName(), "Del Route");
        return true;
    }
    return false;
}

void VrfEntry::DelPeerDone(DBTableBase *base, DBState *state) {
    VrfExport::State *vrf_state = static_cast<VrfExport::State *>(state);

    AGENT_DBWALK_TRACE(AgentDBWalkLog, "Done walk ", 
                       "Inet4UcRouteTable(DelPeerDone)",
                       vrf_state->inet4_uc_walkid_,
                       "peer-unknown", "Add/Del Route");

    vrf_state->inet4_uc_walkid_ = DBTableWalker::kInvalidWalkerId;
}

LifetimeActor *VrfEntry::deleter() {
    return deleter_.get();
}

bool VrfEntry::VrfNotifyEntryWalk(DBTablePartBase *part, DBEntryBase *entry,
                                  Peer *peer) {
    VrfEntry *vrf = static_cast<VrfEntry *>(entry);
    if (peer->GetType() == Peer::BGP_PEER) {
        BgpPeer *bgp_peer = static_cast<BgpPeer *>(peer);

        DBTableBase::ListenerId id = bgp_peer->GetVrfExportListenerId();
        VrfExport::State *state = 
            static_cast<VrfExport::State *>(vrf->GetState(part->parent(), id)); 
        if (state) {
            /* state for __default__ instance will not be created if the 
             * xmpp channel is up the first time as export code registers to 
             * vrf-table after entry for __default__ instance is created */
            state->force_chg_ = true;
        }

        VrfExport::Notify(bgp_peer->GetBgpXmppPeer(), part, entry);
        return true;
    }

    return false;
}

bool VrfEntry::VrfNotifyEntryMcastBcastWalk(DBTablePartBase *part, DBEntryBase *entry,
                                            Peer *peer, bool associate) {
    VrfEntry *vrf = static_cast<VrfEntry *>(entry);
    bool subnet_only = true;
    if (peer->GetType() == Peer::BGP_PEER) {
        BgpPeer *bgp_peer = static_cast<BgpPeer *>(peer);

        DBTableBase::ListenerId id = bgp_peer->GetVrfExportListenerId();
        VrfExport::State *state = 
            static_cast<VrfExport::State *>(vrf->GetState(part->parent(), id)); 

        if (state && (vrf->GetName().compare(Agent::GetInstance()->GetDefaultVrf()) != 0)) {

            Inet4UcRouteTable *table = vrf->GetInet4UcRouteTable();
            table->Inet4UcRouteTableWalkerNotify(vrf, bgp_peer->GetBgpXmppPeer(), 
                                                 state, subnet_only, associate);

            Inet4McRouteTable *mc_table = vrf->GetInet4McRouteTable();
            mc_table->Inet4McRouteTableWalkerNotify(vrf, bgp_peer->GetBgpXmppPeer(), 
                                                    state, associate);
        }

        return true;
    }

    return false;
}

bool VrfEntry::DBEntrySandesh(Sandesh *sresp, std::string &name) const {
    VrfListResp *resp = static_cast<VrfListResp *>(sresp);

    if (GetName().find(name) != std::string::npos) {
        VrfSandeshData data;
        data.set_name(GetName());
        data.set_ucindex(GetVrfId());
        data.set_mcindex(GetVrfId());

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
    str << "Unicast routes: " << inet4_uc_db_->Size();
    str << " Mutlicast routes: " << inet4_mc_db_->Size();
    str << " Reference: " << GetRefCount();
    OPER_TRACE(Vrf, "VRF delete failed, " + str.str(), name_);
    assert(0);
    return false;
}

void VrfEntry::StartDeleteTimer() {
    delete_timeout_timer_ = TimerManager::CreateTimer(
                                *(Agent::GetInstance()->GetEventManager())->io_service(),
                                "VrfDeleteTimer");
    delete_timeout_timer_->Start(kDeleteTimeout, 
                                 boost::bind(&VrfEntry::DeleteTimeout,
                                 this));
}

void VrfEntry::CancelDeleteTimer() {
    delete_timeout_timer_->Cancel();
}

//Increment the count of NH, a route has
//Used in ECMP case
void VrfEntry::AddNH(Ip4Address ip, ComponentNHData *nh_data) {
    nh_map_->AddNH(ip, *nh_data);
}

//Decrement the count of NH, a route points to.
void VrfEntry::DeleteNH(Ip4Address ip, ComponentNHData *nh_data) {
    nh_map_->DeleteNH(ip, *nh_data);
}

uint32_t VrfEntry::GetNHCount(Ip4Address ip) {
    return nh_map_->GetNHCount(ip);
}

void VrfEntry::UpdateLabel(Ip4Address ip, uint32_t label) {
    nh_map_->UpdateLabel(ip, label);
}

uint32_t VrfEntry::GetLabel(Ip4Address ip) {
    return nh_map_->GetLabel(ip);
}

bool VrfEntry::FindNH(const Ip4Address &ip, const ComponentNHData &nh_data) {
    return nh_map_->FindNH(ip, nh_data);

}
ComponentNHData::ComponentNHDataList* VrfEntry::GetNHList(Ip4Address ip) {
    return nh_map_->GetNHList(ip);
}

std::auto_ptr<DBEntry> VrfTable::AllocEntry(const DBRequestKey *k) const {
    const VrfKey *key = static_cast<const VrfKey *>(k);
    VrfEntry *vrf = new VrfEntry(key->name_);
    return std::auto_ptr<DBEntry>(static_cast<DBEntry *>(vrf));
}

DBEntry *VrfTable::Add(const DBRequest *req) {
    VrfKey *key = static_cast<VrfKey *>(req->key.get());
    VrfEntry *vrf = new VrfEntry(key->name_);

    // Add VRF into name based tree
    if (FindVrfFromName(key->name_)) {
        delete vrf;
        assert(0);
        return NULL;
    }
    name_tree_.insert( VrfNamePair(key->name_, vrf));

    vrf->inet4_uc_db_ = static_cast<Inet4RouteTable *>
            (db_->CreateTable(key->name_ + VrfTable::GetInet4UcSuffix()));
    
    inet4_uc_dbtree_.insert(VrfDbPair(key->name_, vrf->inet4_uc_db_));

    vrf->inet4_mc_db_ = static_cast<Inet4RouteTable *>
            (db_->CreateTable(key->name_ + VrfTable::GetInet4McSuffix()));
    inet4_mc_dbtree_.insert(VrfDbPair(key->name_, vrf->inet4_mc_db_));

    vrf->id_ = index_table_.Insert(vrf);
    vrf->SendObjectLog(AgentLogEvent::ADD);
    return vrf;
}

// No Change expected for VRF
bool VrfTable::OnChange(DBEntry *entry, const DBRequest *req) {
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
    IFMapNode *node = IFMapAgentTable::TableEntryLookup(Agent::GetInstance()->GetDB(), &req_key);

    if (!node || node->IsDeleted()) {
        return;
    }

    OPER_TRACE(Vrf, "Resyncing configuration for VRF: ", name);
    Agent::GetInstance()->GetCfgListener()->NodeReSync(node);
}

void VrfTable::OnZeroRefcount(AgentDBEntry *e) {
    VrfEntry *vrf = static_cast<VrfEntry *>(e);
    if (e->IsDeleted()) {
        Agent::GetInstance()->GetDB()->RemoveTable(vrf->GetInet4UcRouteTable());
        delete vrf->GetInet4UcRouteTable();
        Agent::GetInstance()->GetDB()->RemoveTable(vrf->GetInet4McRouteTable());
        delete vrf->GetInet4McRouteTable();

        name_tree_.erase(vrf->GetName());
        inet4_uc_dbtree_.erase(vrf->GetName());
        inet4_mc_dbtree_.erase(vrf->GetName());
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

Inet4UcRouteTable *VrfTable::GetInet4UcRouteTable(const string &vrf_name) {
    VrfDbTree::const_iterator it;
    
    it = inet4_uc_dbtree_.find(vrf_name);
    if (it == inet4_uc_dbtree_.end()) {
        return NULL;
    }

    return static_cast<Inet4UcRouteTable *>(it->second);
}

Inet4McRouteTable *VrfTable::GetInet4McRouteTable(const string &vrf_name) {
    VrfDbTree::const_iterator it;
    
    it = inet4_mc_dbtree_.find(vrf_name);
    if (it == inet4_mc_dbtree_.end()) {
        return NULL;
    }

    return static_cast<Inet4McRouteTable *>(it->second);
}

void VrfTable::CreateVrf(const string &name) {
    VrfKey *key = new VrfKey(name);
    VrfData *data = new VrfData();
    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    req.key.reset(key);
    req.data.reset(data);
    Enqueue(&req);
}

void VrfTable::DeleteVrf(const string &name) {
    VrfKey *key = new VrfKey(name);
    DBRequest req;
            req.oper = DBRequest::DB_ENTRY_DELETE;
    req.key.reset(key);
    req.data.reset(NULL);
    Enqueue(&req);
}

void VrfTable::DelPeerRoutes(Peer *peer, Peer::DelPeerDone cb) {
    DBTableWalker *walker = db_->GetWalker();

    if (peer->GetPeerVrfUCWalkId() != DBTableWalker::kInvalidWalkerId) {
        AGENT_DBWALK_TRACE(AgentDBWalkLog, "Cancel  walk ", 
                           "VrfTable(DelPeerRoutes)",
                           peer->GetPeerVrfUCWalkId(),
                           peer->GetName(), "Del VrfEntry");

        walker->WalkCancel(peer->GetPeerVrfUCWalkId());
    }

    DBTableWalker::WalkId id = walker->WalkTable(this, NULL, 
                boost::bind(&VrfEntry::DelPeerRoutes, _1, _2, peer), 
                boost::bind(&VrfTable::DelPeerDone, this, _1, peer, cb));
    peer->SetPeerVrfUCWalkId(id); 

    AGENT_DBWALK_TRACE(AgentDBWalkLog, "Start  walk ", 
                       "VrfTable(DelPeerRoutes)",
                       peer->GetPeerVrfUCWalkId(),
                       peer->GetName(), "Del VrfEntry");
}

void VrfTable::DelPeerDone(DBTableBase *base, 
                           Peer *peer,
                           Peer::DelPeerDone cb) {

    AGENT_DBWALK_TRACE(AgentDBWalkLog, "Done  walk ", "VrfTable(DelPeerDone)",
                       peer->GetPeerVrfUCWalkId(),
                       peer->GetName(), "Del VrfEntry");
    peer->ResetPeerVrfUCWalkId();
    cb();
}

void VrfTable::VrfNotifyDone(DBTableBase *base, Peer *peer) {

    AGENT_DBWALK_TRACE(AgentDBWalkLog, "Done  walk ", 
                       "VrfTable(VrfNotifyDone)",
                       peer->GetPeerVrfUCWalkId(),
                       peer->GetName(), "Notify VrfEntry");
    peer->ResetPeerVrfUCWalkId();
}

void VrfTable::VrfTableWalkerNotify(Peer *peer) {
    DBTableWalker *walker = db_->GetWalker();

    if (peer->GetPeerVrfUCWalkId() != DBTableWalker::kInvalidWalkerId) {

        AGENT_DBWALK_TRACE(AgentDBWalkLog, "Cancel walk ", 
                           "VrfTable(VrfTableWalkerNotify)",
                           peer->GetPeerVrfUCWalkId(),
                           peer->GetName(), "Notify VrfEntry");
        walker->WalkCancel(peer->GetPeerVrfUCWalkId());
    }

    DBTableWalker::WalkId id = walker->WalkTable(this, NULL, 
        boost::bind(&VrfEntry::VrfNotifyEntryWalk, _1, _2, peer), 
        boost::bind(&VrfTable::VrfNotifyDone, this, _1, peer));
    peer->SetPeerVrfUCWalkId(id);

    AGENT_DBWALK_TRACE(AgentDBWalkLog, "Start walk ", 
                       "VrfTable(VrfTableWalkerNotify)",
                       peer->GetPeerVrfUCWalkId(),
		       peer->GetName(), "Notify VrfEntry");
}

// Subset walker for subnet and broadcast routes
void VrfTable::VrfNotifyMcastBcastDone(DBTableBase *base, 
                                       Peer *peer) {
    AGENT_DBWALK_TRACE(AgentDBWalkLog, "Done walk ", 
                       "VrfTable(VrfNotifyMcastBcastDone)",
                       peer->GetPeerVrfMCWalkId(),
		       peer->GetName(), "Add/Withdraw Route");
    peer->ResetPeerVrfMCWalkId();
}

void VrfTable::VrfTableWalkerMcastBcastNotify(Peer *peer, bool associate) {
    DBTableWalker *walker = db_->GetWalker();

    if (peer->GetPeerVrfMCWalkId() != 
        DBTableWalker::kInvalidWalkerId) {

        AGENT_DBWALK_TRACE(AgentDBWalkLog, "Cancel walk ", 
                           "VrfTable(VrfTableWalkerMcastBcastNotify)",
                           peer->GetPeerVrfMCWalkId(),
		           peer->GetName(),
                           "Add/Withdraw Route");
        walker->WalkCancel(peer->GetPeerVrfMCWalkId());
    }

    DBTableWalker::WalkId id = walker->WalkTable(this, NULL, 
        boost::bind(&VrfEntry::VrfNotifyEntryMcastBcastWalk, 
                    _1, _2, peer, associate), 
        boost::bind(&VrfTable::VrfNotifyMcastBcastDone, this, _1, peer));
    peer->SetPeerVrfMCWalkId(id);

    AGENT_DBWALK_TRACE(AgentDBWalkLog, "Start walk ", 
                       "VrfTable(VrfTableWalkerMcastBcastNotify)",
                       peer->GetPeerVrfMCWalkId(),
                       peer->GetName(),
                       (associate)? "Add Route": "Withdraw Route"); 
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
    VrfEntry *entry = static_cast<VrfEntry *>
        (Agent::GetInstance()->GetVrfTable()->Find(&key, true));
    // Check if there is an entry with given name in *any* DBState
    if (entry && entry->IsDeleted()) {
        OPER_TRACE(Vrf, "VRF pending delete, Ignoring config for ", node->name());
        return false;
    }

    return true;
}

bool VrfTable::IFNodeToReq(IFMapNode *node, DBRequest &req) {
    if (node->name() != Agent::GetInstance()->GetDefaultVrf() && 
        node->name() != Agent::GetInstance()->GetLinkLocalVrfName()) {
        VrfKey *key = new VrfKey(node->name());
        //Trigger add or delete only for non fabric VRF
        if (node->IsDeleted()) {
            req.oper = DBRequest::DB_ENTRY_DELETE;
        } else {
            req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
        }

        req.key.reset(key);
        req.data.reset(NULL);
        Agent::GetInstance()->GetVrfTable()->Enqueue(&req);
    }

    if (node->IsDeleted()) {
        return false;
    }

    // Resync any vmport dependent on this VRF
    // While traversing the path 
    // virtual-machine-interface <-> virtual-machine-interface-routing-instance 
    // <-> routing-instance path, we may have skipped a routing-instance that
    // failed CanUseNode() 
    IFMapAgentTable *table = static_cast<IFMapAgentTable *>(node->table());
    for (DBGraphVertex::adjacency_iterator iter =
         node->begin(table->GetGraph()); 
         iter != node->end(table->GetGraph()); ++iter) {
        if (iter->IsDeleted()) {
            continue;
        }

        IFMapNode *adj_node = static_cast<IFMapNode *>(iter.operator->());
        if (CfgListener::CanUseNode
            (adj_node, AgentConfig::GetInstance()->GetVmPortVrfTable()) == false) {
            continue;
        }

        InterfaceTable::VmInterfaceVrfSync(adj_node);
    }

    // Resync dependent Floating-IP
    VmPortInterface::FloatingIpVrfSync(node);
    return false;
}

void VrfListReq::HandleRequest() const {
    AgentVrfSandesh *sand = new AgentVrfSandesh(context(), get_name());
    sand->DoSandesh();
}

