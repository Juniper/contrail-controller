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
#include <route/route.h>
#include <oper/route_common.h>
#include <oper/vn.h>
#include <oper/vrf.h>
#include <oper/peer.h>
#include <oper/mirror_table.h>
#include <controller/controller_init.h>
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
    struct VrfNHMapKey {
        VrfNHMapKey(Ip4Address ip, uint8_t plen):
            ip_(ip), plen_(plen) { }

        bool operator< (const VrfNHMapKey &rhs) const{
            if (ip_ != rhs.ip_) {
                return ip_ < rhs.ip_;
            }
            return plen_ < rhs.plen_;
        }

        Ip4Address ip_;
        uint32_t   plen_;
    };
    void AddNH(Ip4Address ip, uint8_t plen, ComponentNHData nh_data) {
        VrfNHMapKey key(ip, plen);
        ComponentNHData::ComponentNHDataList component_list = nh_map_[key];
        ComponentNHData::ComponentNHDataList::iterator it = 
            component_list.begin();
        while (it != component_list.end()) {
            if (*it == nh_data) {
                *it = nh_data;
                return;
            }
            it++;
        }
        nh_map_[key].push_back(nh_data);
    }

        //Decrement the count of NH, a route points to.
    void DeleteNH(Ip4Address ip, uint8_t plen, ComponentNHData nh_data) {
        VrfNHMapKey key(ip, plen);
        ComponentNHData::ComponentNHDataList::iterator it = 
                                                  nh_map_[key].begin();
        while (it != nh_map_[key].end()) {
            if (*it == nh_data) {
                nh_map_[key].erase(it);
                break;
            }
            it++;
        }
        return;
    }

    uint32_t GetNHCount(Ip4Address ip, uint8_t plen) {
        VrfNHMapKey key(ip, plen);
        return nh_map_[key].size();
    }

    bool FindNH(const Ip4Address &ip, uint8_t plen, 
                const ComponentNHData &nh_data) {
        VrfNHMapKey key(ip, plen);
        ComponentNHData::ComponentNHDataList component_list = nh_map_[key];
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

    ComponentNHData::ComponentNHDataList* GetNHList(Ip4Address ip,
                                                    uint8_t plen) {
        VrfNHMapKey key(ip, plen);
        return &nh_map_[key];
    }

    void UpdateLabel(Ip4Address addr, uint8_t plen, uint32_t label) {
        VrfNHMapKey key(addr, plen);
        label_map_[key] = label;
    }

    uint32_t GetLabel(Ip4Address addr, uint8_t plen) {
        VrfNHMapKey key(addr, plen);
        return label_map_[key];
    }

private:
    std::map<VrfNHMapKey, ComponentNHData::ComponentNHDataList> nh_map_;
    std::map<VrfNHMapKey, uint32_t> label_map_;
};

VrfEntry::VrfEntry(const string &name) : 
        name_(name), id_(kInvalidIndex), 
        walkid_(DBTableWalker::kInvalidWalkerId), deleter_(NULL),
        nh_map_(NULL), rt_table_db_(), delete_timeout_timer_(NULL) { 
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

void VrfEntry::PostAdd() {
    Init();

    // Create the route-tables and insert them into dbtree_
    Agent::RouteTableType type = Agent::INET4_UNICAST;
    DB *db = get_table()->database();
    rt_table_db_[type] = static_cast<AgentRouteTable *>
        (db->CreateTable(name_ + AgentRouteTable::GetSuffix(type)));
    ((VrfTable *)get_table())->dbtree_[type].insert(VrfTable::VrfDbPair(name_, 
                                                        rt_table_db_[type]));

    type = Agent::INET4_MULTICAST;
    rt_table_db_[type] = static_cast<AgentRouteTable *>
        (db->CreateTable(name_ + AgentRouteTable::GetSuffix(type)));
    ((VrfTable *)get_table())->dbtree_[type].insert(VrfTable::VrfDbPair(name_, 
                                                        rt_table_db_[type]));

    type = Agent::LAYER2;
    rt_table_db_[type] = static_cast<AgentRouteTable *>
        (db->CreateTable(name_ + AgentRouteTable::GetSuffix(type)));
    ((VrfTable *)get_table())->dbtree_[type].insert(VrfTable::VrfDbPair(name_, 
                                                        rt_table_db_[type]));

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

bool VrfEntry::DelPeerRoutes(DBTablePartBase *part, DBEntryBase *entry, 
                             Peer *peer) {
    VrfEntry *vrf = static_cast<VrfEntry *>(entry);
    DBTableWalker *walker = Agent::GetInstance()->GetDB()->GetWalker();

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

        int route_tables;
        for (route_tables = 0; route_tables < Agent::ROUTE_TABLE_MAX; route_tables++) {
            AgentRouteTable *table = static_cast<AgentRouteTable *> 
                (vrf->GetRouteTable(route_tables));
            if (state->ucwalkid_[route_tables] != DBTableWalker::kInvalidWalkerId) {
                AGENT_DBWALK_TRACE(AgentDBWalkLog, "Cancel  walk (DelPeerRoutes)", 
                                   table->GetTableName(),
                                   state->ucwalkid_[route_tables],
                                   peer->GetName(), "Del Route",
                                   peer->NoOfWalks());

                walker->WalkCancel(state->ucwalkid_[route_tables]);
            }
            state->ucwalkid_[route_tables] = walker->WalkTable(table, NULL,
              boost::bind(&AgentRouteTable::DelPeerRoutes, table, _1, _2, peer), 
              boost::bind(&VrfEntry::DelPeerDone, _1, state, 
                          route_tables, table->GetTableName(), peer));

            AGENT_DBWALK_TRACE(AgentDBWalkLog, "Start walk (DelPeerRoutes)", 
                               table->GetTableName(),
                               state->ucwalkid_[route_tables],
                               peer->GetName(), "Del Route",
                               peer->NoOfWalks());
        }
        return true;
    }
    return false;
}

void VrfEntry::DelPeerDone(DBTableBase *base, DBState *state, 
                           uint8_t table_type, const string &table_name,
                           Peer *peer) {
    VrfExport::State *vrf_state = static_cast<VrfExport::State *>(state);

    AGENT_DBWALK_TRACE(AgentDBWalkLog, "Done walk(DelPeerDone)", 
                       table_name, vrf_state->ucwalkid_[table_type],
                       peer->GetName(), "Add/Del Route",
                       peer->NoOfWalks());

    vrf_state->ucwalkid_[table_type] = DBTableWalker::kInvalidWalkerId;
    peer->DecrementWalks();
    if (peer->NoOfWalks() == 0) {
        AGENT_DBWALK_TRACE(AgentDBWalkLog, "Done all walks ", 
                           table_name,
                           vrf_state->ucwalkid_[table_type],
                           peer->GetName(), "Add/Del Route",
                           peer->NoOfWalks());
        VNController::Cleanup();
    }
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

bool VrfEntry::VrfNotifyEntryMulticastWalk(DBTablePartBase *part, 
                                           DBEntryBase *entry,
                                           Peer *peer, bool associate) {
    VrfEntry *vrf = static_cast<VrfEntry *>(entry);
    if (peer->GetType() == Peer::BGP_PEER) {
        BgpPeer *bgp_peer = static_cast<BgpPeer *>(peer);

        DBTableBase::ListenerId id = bgp_peer->GetVrfExportListenerId();
        VrfExport::State *state = 
            static_cast<VrfExport::State *>(vrf->GetState(part->parent(), id)); 

        if (state && (vrf->GetName().compare(Agent::GetInstance()->GetDefaultVrf()) != 0)) {

            int rt_table_cnt;
            for (rt_table_cnt = 0; rt_table_cnt < Agent::ROUTE_TABLE_MAX;
                 rt_table_cnt++) {
                AgentRouteTable *table = static_cast<AgentRouteTable *> 
                    (vrf->GetRouteTable(rt_table_cnt));
                table->RouteTableWalkerNotify(vrf, bgp_peer->GetBgpXmppPeer(),
                                              state, associate, false, true);
            }
        }

        return true;
    }

    return false;
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

bool VrfEntry::DBEntrySandesh(Sandesh *sresp, std::string &name) const {
    VrfListResp *resp = static_cast<VrfListResp *>(sresp);

    if (GetName().find(name) != std::string::npos) {
        VrfSandeshData data;
        data.set_name(GetName());
        data.set_ucindex(GetVrfId());
        data.set_mcindex(GetVrfId());
        data.set_l2index(GetVrfId());

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
void VrfEntry::AddNH(Ip4Address ip, uint8_t plen, ComponentNHData *nh_data) {
    nh_map_->AddNH(ip, plen, *nh_data);
}

//Decrement the count of NH, a route points to.
void VrfEntry::DeleteNH(Ip4Address ip, uint8_t plen, ComponentNHData *nh_data) {
    nh_map_->DeleteNH(ip, plen, *nh_data);
}

uint32_t VrfEntry::GetNHCount(Ip4Address ip, uint8_t plen) {
    return nh_map_->GetNHCount(ip, plen);
}

void VrfEntry::UpdateLabel(Ip4Address ip, uint8_t plen, uint32_t label) {
    nh_map_->UpdateLabel(ip, plen, label);
}

uint32_t VrfEntry::GetLabel(Ip4Address ip, uint8_t plen) {
    return nh_map_->GetLabel(ip, plen);
}

bool VrfEntry::FindNH(const Ip4Address &ip, uint8_t plen,
                      const ComponentNHData &nh_data) {
    return nh_map_->FindNH(ip, plen, nh_data);

}
ComponentNHData::ComponentNHDataList* VrfEntry::GetNHList(Ip4Address ip,
                                                          uint8_t plen) {
    return nh_map_->GetNHList(ip, plen);
}

void VrfEntry::Init() {
    deleter_.reset(new DeleteActor(this));
    nh_map_.reset(new VrfNHMap());
}

std::auto_ptr<DBEntry> VrfTable::AllocEntry(const DBRequestKey *k) const {
    const VrfKey *key = static_cast<const VrfKey *>(k);
    VrfEntry *vrf = new VrfEntry(key->name_);
    return std::auto_ptr<DBEntry>(static_cast<DBEntry *>(vrf));
}

DBEntry *VrfTable::Add(const DBRequest *req) {
    VrfKey *key = static_cast<VrfKey *>(req->key.get());
    //VrfData *data = static_cast<VrfData *>(req->data.get());
    VrfEntry *vrf = new VrfEntry(key->name_);

    // Add VRF into name based tree
    if (FindVrfFromName(key->name_)) {
        delete vrf;
        assert(0);
        return NULL;
    }
    name_tree_.insert( VrfNamePair(key->name_, vrf));

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
    Agent::GetInstance()->cfg_listener()->NodeReSync(node);
}

void VrfTable::OnZeroRefcount(AgentDBEntry *e) {
    VrfEntry *vrf = static_cast<VrfEntry *>(e);
    if (e->IsDeleted()) {
        int table_type;
        for (table_type = 0; table_type < Agent::ROUTE_TABLE_MAX; table_type++) {
            Agent::GetInstance()->GetDB()->RemoveTable(vrf->GetRouteTable(table_type));
            dbtree_[table_type].erase(vrf->GetName());
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

AgentRouteTable *VrfTable::GetInet4UnicastRouteTable(const string &vrf_name) {
    return GetRouteTable(vrf_name, Agent::INET4_UNICAST);
}

AgentRouteTable *VrfTable::GetInet4MulticastRouteTable(const string &vrf_name) {
    return GetRouteTable(vrf_name, Agent::INET4_MULTICAST);
}

AgentRouteTable *VrfTable::GetLayer2RouteTable(const string &vrf_name) {
    return GetRouteTable(vrf_name, Agent::LAYER2);
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
                           peer->GetName(), "Del VrfEntry",
                           peer->NoOfWalks());

        walker->WalkCancel(peer->GetPeerVrfUCWalkId());
    }

    DBTableWalker::WalkId id = walker->WalkTable(this, NULL, 
                boost::bind(&VrfEntry::DelPeerRoutes, _1, _2, peer), 
                boost::bind(&VrfTable::DelPeerDone, this, _1, peer, cb));
    peer->SetPeerVrfUCWalkId(id); 

    AGENT_DBWALK_TRACE(AgentDBWalkLog, "Start  walk ", 
                       "VrfTable(DelPeerRoutes)",
                       peer->GetPeerVrfUCWalkId(),
                       peer->GetName(), "Del VrfEntry",
                       peer->NoOfWalks());
}

void VrfTable::DelPeerDone(DBTableBase *base, 
                           Peer *peer,
                           Peer::DelPeerDone cb) {

    AGENT_DBWALK_TRACE(AgentDBWalkLog, "Done  walk ", "VrfTable(DelPeerDone)",
                       peer->GetPeerVrfUCWalkId(),
                       peer->GetName(), "Del VrfEntry",
                       peer->NoOfWalks());
    peer->ResetPeerVrfUCWalkId();
    peer->DecrementWalks();
    cb();
}

void VrfTable::VrfNotifyDone(DBTableBase *base, Peer *peer) {

    AGENT_DBWALK_TRACE(AgentDBWalkLog, "Done  walk ", 
                       "VrfTable(VrfNotifyDone)",
                       peer->GetPeerVrfUCWalkId(),
                       peer->GetName(), "Notify VrfEntry",
                       peer->NoOfWalks());
    peer->ResetPeerVrfUCWalkId();
}

void VrfTable::VrfTableWalkerNotify(Peer *peer) {
    DBTableWalker *walker = db_->GetWalker();

    if (peer->GetPeerVrfUCWalkId() != DBTableWalker::kInvalidWalkerId) {

        AGENT_DBWALK_TRACE(AgentDBWalkLog, "Cancel walk ", 
                           "VrfTable(VrfTableWalkerNotify)",
                           peer->GetPeerVrfUCWalkId(),
                           peer->GetName(), "Notify VrfEntry",
                           peer->NoOfWalks());
        walker->WalkCancel(peer->GetPeerVrfUCWalkId());
    }

    DBTableWalker::WalkId id = walker->WalkTable(this, NULL, 
        boost::bind(&VrfEntry::VrfNotifyEntryWalk, _1, _2, peer), 
        boost::bind(&VrfTable::VrfNotifyDone, this, _1, peer));
    peer->SetPeerVrfUCWalkId(id);

    AGENT_DBWALK_TRACE(AgentDBWalkLog, "Start walk ", 
                       "VrfTable(VrfTableWalkerNotify)",
                       peer->GetPeerVrfUCWalkId(),
		       peer->GetName(), "Notify VrfEntry",
                       peer->NoOfWalks());
}

// Subset walker for subnet and broadcast routes
void VrfTable::VrfNotifyMulticastDone(DBTableBase *base, 
                                       Peer *peer) {
    AGENT_DBWALK_TRACE(AgentDBWalkLog, "Done walk ", 
                       "VrfTable(VrfNotifyMulticastDone)",
                       peer->GetPeerVrfMCWalkId(),
		       peer->GetName(), "Add/Withdraw Route",
                       peer->NoOfWalks());
    peer->ResetPeerVrfMCWalkId();
}

void VrfTable::VrfTableWalkerMulticastNotify(Peer *peer, bool associate) {
    DBTableWalker *walker = db_->GetWalker();

    if (peer->GetPeerVrfMCWalkId() != 
        DBTableWalker::kInvalidWalkerId) {

        AGENT_DBWALK_TRACE(AgentDBWalkLog, "Cancel walk ", 
                           "VrfTable(VrfTableWalkerMulticastNotify)",
                           peer->GetPeerVrfMCWalkId(),
		           peer->GetName(),
                           "Add/Withdraw Route",
                           peer->NoOfWalks());
        walker->WalkCancel(peer->GetPeerVrfMCWalkId());
    }

    DBTableWalker::WalkId id = walker->WalkTable(this, NULL, 
        boost::bind(&VrfEntry::VrfNotifyEntryMulticastWalk, 
                    _1, _2, peer, associate), 
        boost::bind(&VrfTable::VrfNotifyMulticastDone, this, _1, peer));
    peer->SetPeerVrfMCWalkId(id);

    AGENT_DBWALK_TRACE(AgentDBWalkLog, "Start walk ", 
                       "VrfTable(VrfTableWalkerMulticastNotify)",
                       peer->GetPeerVrfMCWalkId(),
                       peer->GetName(),
                       (associate)? "Add Route": "Withdraw Route",
                       peer->NoOfWalks()); 
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
            IFMapAgentTable *table = 
                static_cast<IFMapAgentTable *>(node->table());
            for (DBGraphVertex::adjacency_iterator iter =
                 node->begin(table->GetGraph()); 
                 iter != node->end(table->GetGraph()); ++iter) {
                IFMapNode *adj_node = 
                    static_cast<IFMapNode *>(iter.operator->());

                if (iter->IsDeleted() || 
                    (adj_node->table() != 
                     Agent::GetInstance()->cfg()->cfg_vn_table())) {
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

        VrfData *data = new VrfData();
        req.key.reset(key);
        req.data.reset(data);
        Agent::GetInstance()->GetVrfTable()->Enqueue(&req);
    }

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
        if (Agent::GetInstance()->cfg_listener()->SkipNode
            (adj_node, Agent::GetInstance()->cfg()->cfg_vm_port_vrf_table())) {
            continue;
        }

        Agent::GetInstance()->GetInterfaceTable()->VmInterfaceVrfSync(adj_node);
    }

    // Resync dependent Floating-IP
    VmInterface::FloatingIpVrfSync(Agent::GetInstance()->GetInterfaceTable(),
                                   node);
    return false;
}

void VrfListReq::HandleRequest() const {
    AgentVrfSandesh *sand = new AgentVrfSandesh(context(), get_name());
    sand->DoSandesh();
}

