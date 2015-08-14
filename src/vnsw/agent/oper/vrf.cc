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

#include <init/agent_init.h>
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
#include <oper/agent_route_resync.h>

using namespace std;
using namespace autogen;

VrfTable *VrfTable::vrf_table_;

class VrfEntry::DeleteActor : public LifetimeActor {
  public:
    DeleteActor(VrfEntry *vrf) : 
        LifetimeActor((static_cast<VrfTable *>(vrf->get_table()))->
                      agent()->lifetime_manager()), table_(vrf, this) {
    }
    virtual ~DeleteActor() { 
    }
    virtual bool MayDelete() const {
        //No table present, then this VRF can be deleted
        return table_->AllRouteTableDeleted();
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

VrfEntry::VrfEntry(const string &name, uint32_t flags, Agent *agent) : 
        name_(name), id_(kInvalidIndex), flags_(flags),
        walkid_(DBTableWalker::kInvalidWalkerId), deleter_(NULL),
        rt_table_db_(), delete_timeout_timer_(NULL),
        table_label_(MplsTable::kInvalidLabel),
        vxlan_id_(VxLanTable::kInvalidvxlan_id),
        rt_table_delete_bmap_(0),
        route_resync_walker_(new AgentRouteResync(agent)) {
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

bool VrfEntry::UpdateVxlanId(Agent *agent, uint32_t new_vxlan_id) {
    bool ret = false;
    if (new_vxlan_id == vxlan_id_) {
        return ret;
    }

    vxlan_id_ = new_vxlan_id;
    return ret;
}

void VrfEntry::CreateTableLabel() {
    if (table_label_ == MplsTable::kInvalidLabel) {
        VrfTable *table = static_cast<VrfTable *>(get_table());
        Agent *agent = table->agent();
        set_table_label(agent->mpls_table()->AllocLabel());
        MplsTable::CreateTableLabel(agent, table_label(), name_, false);
    }
}

void VrfEntry::CreateRouteTables() {
    DB *db = get_table()->database();
    VrfTable *table = static_cast<VrfTable *>(get_table());

    for (uint8_t type = (Agent::INVALID + 1); type < Agent::ROUTE_TABLE_MAX; type++) {
        rt_table_db_[type] = static_cast<AgentRouteTable *>
            (db->CreateTable(name_ +
                 AgentRouteTable::GetSuffix(Agent::RouteTableType(type))));
        rt_table_db_[type]->SetVrf(this);
        table->dbtree_[type].insert(VrfTable::VrfDbPair(name_, rt_table_db_[type]));
    }
}

void VrfEntry::DeleteRouteTables() {
    for (int table_type = (Agent::INVALID + 1);
         table_type < Agent::ROUTE_TABLE_MAX; table_type++) {
        VrfTable *vrf_table = ((VrfTable *) get_table());
        AgentRouteTable *route_table =
            vrf_table->GetRouteTable(name_, table_type);
        if (route_table) {
            vrf_table->DeleteFromDbTree(table_type, name_);
            vrf_table->database()->RemoveTable(route_table);
            delete route_table;
        }
    }
}

void VrfEntry::PostAdd() {
    VrfTable *table = static_cast<VrfTable *>(get_table());
    Agent *agent = table->agent();
    // get_table() would return NULL in Add(), so move dependent functions and 
    // initialization to PostAdd
    deleter_.reset(new DeleteActor(this));
    // Create the route-tables and insert them into dbtree_
    CreateRouteTables();

    uint32_t vxlan_id = VxLanTable::kInvalidvxlan_id;
    if (vn_) {
        vxlan_id = vn_->GetVxLanId();
    }
    UpdateVxlanId(agent, vxlan_id);

    // Add the L2 Receive routes for VRRP mac
    BridgeAgentRouteTable *l2_table = static_cast<BridgeAgentRouteTable *>
        (rt_table_db_[Agent::BRIDGE]);
    l2_table->AddBridgeReceiveRoute(agent->local_vm_peer(), name_, 0,
                                    agent->vrrp_mac(), "");

    // Add the L2 Receive routes for xconnect interface to vhost
    // Note, vhost is not created when fabric VRF is created. We only need
    // VRRP MAC on fabric vrf. So, we are good for now
    const InetInterface *vhost = static_cast<const InetInterface *>
        (agent->vhost_interface());
    if (vhost && vhost->xconnect()) {
        l2_table->AddBridgeReceiveRoute(agent->local_vm_peer(), name_, 0,
                                        vhost->xconnect()->mac(), "");
    }

    //Add receive route for vmware physical interface mac, so
    //that packets hitting this mac gets routed(inter-vn traffic)
    if (agent->isVmwareMode()) {
        PhysicalInterfaceKey key(agent->params()->vmware_physical_port());
        Interface *intf = static_cast<Interface *>
            (agent->interface_table()->FindActiveEntry(&key));
        if (intf) {
             l2_table->AddBridgeReceiveRoute(agent->local_vm_peer(), name_, 0,
                                             intf->mac(), "");
        }
    }

    SendObjectLog(AgentLogEvent::ADD);
}

DBEntryBase::KeyPtr VrfEntry::GetDBRequestKey() const {
    VrfKey *key = new VrfKey(name_);
    return DBEntryBase::KeyPtr(key);
}

void VrfEntry::SetKey(const DBRequestKey *key) { 
    const VrfKey *k = static_cast<const VrfKey *>(key);
    name_ = k->name_;
}

InetUnicastRouteEntry *VrfEntry::GetUcRoute(const IpAddress &addr) const {
    InetUnicastAgentRouteTable *table = NULL;
    if (addr.is_v4()) {
        table = GetInet4UnicastRouteTable();
    } else if (addr.is_v6()) {
        table = GetInet6UnicastRouteTable();
    }
    if (table == NULL)
        return NULL;

    return table->FindLPM(addr);
}

InetUnicastRouteEntry *VrfEntry::GetUcRoute(const InetUnicastRouteEntry &rt_key) const {
    InetUnicastAgentRouteTable *table = NULL;

    if (rt_key.addr().is_v4()) {
        table = GetInet4UnicastRouteTable();
    } else if (rt_key.addr().is_v6()) {
        table = GetInet6UnicastRouteTable();
    }
    if (table == NULL)
        return NULL;

    return table->FindLPM(rt_key);
}

LifetimeActor *VrfEntry::deleter() {
    return deleter_.get();
}

bool VrfEntry::AllRouteTableDeleted() const {
    for (int i = Agent::INET4_UNICAST; i < Agent::ROUTE_TABLE_MAX; i++) {
        if ((rt_table_delete_bmap_ & (1 << i)) == 0)
            return false;
    }

    return true;
}

bool VrfEntry::RouteTableDeleted(uint8_t table_type) const {
    return (rt_table_delete_bmap_ & (1 << table_type));
}

void VrfEntry::SetRouteTableDeleted(uint8_t table_type) {
    rt_table_delete_bmap_ |= (1 << table_type);
}

AgentRouteTable *VrfEntry::GetRouteTable(uint8_t table_type) const {
    return (RouteTableDeleted(table_type) ? NULL : rt_table_db_[table_type]);
}

InetUnicastAgentRouteTable *VrfEntry::GetInet4UnicastRouteTable() const {
    return static_cast<InetUnicastAgentRouteTable *>(GetRouteTable(Agent::INET4_UNICAST));
}

AgentRouteTable *VrfEntry::GetInet4MulticastRouteTable() const {
    return GetRouteTable(Agent::INET4_MULTICAST);
}

AgentRouteTable *VrfEntry::GetEvpnRouteTable() const {
    return GetRouteTable(Agent::EVPN);
}

AgentRouteTable *VrfEntry::GetBridgeRouteTable() const {
    return GetRouteTable(Agent::BRIDGE);
}

InetUnicastAgentRouteTable *VrfEntry::GetInet6UnicastRouteTable() const {
    return static_cast<InetUnicastAgentRouteTable *>(GetRouteTable(Agent::INET6_UNICAST));
}

bool VrfEntry::DBEntrySandesh(Sandesh *sresp, std::string &name) const {
    VrfListResp *resp = static_cast<VrfListResp *>(sresp);

    if (name.empty() || GetName().find(name) != string::npos) {
        VrfSandeshData data;
        data.set_name(GetName());
        data.set_ucindex(vrf_id());
        data.set_mcindex(vrf_id());
        data.set_evpnindex(vrf_id());
        data.set_l2index(vrf_id());
        data.set_brindex(vrf_id());
        data.set_uc6index(vrf_id());
        std::string vrf_flags;
        if (flags() & VrfData::ConfigVrf)
            vrf_flags += "Config; ";
        if (flags() & VrfData::GwVrf)
            vrf_flags += "Gateway; ";
        data.set_source(vrf_flags);
        if (vn_.get()) {
            data.set_vn(vn_->GetName());
        } else {
            data.set_vn("N/A");
        }
        data.set_table_label(table_label());

        std::vector<VrfSandeshData> &list = 
                const_cast<std::vector<VrfSandeshData>&>(resp->get_vrf_list());
        data.set_vxlan_id(vxlan_id_);
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
    str << " EVPN routes: " << rt_table_db_[Agent::EVPN]->Size();
    str << " Bridge routes: " << rt_table_db_[Agent::BRIDGE]->Size();
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

void VrfEntry::ResyncRoutes() {
    route_resync_walker_.get()->UpdateRoutesInVrf(this);
}

std::auto_ptr<DBEntry> VrfTable::AllocEntry(const DBRequestKey *k) const {
    const VrfKey *key = static_cast<const VrfKey *>(k);
    VrfEntry *vrf = new VrfEntry(key->name_, 0, agent());
    return std::auto_ptr<DBEntry>(static_cast<DBEntry *>(vrf));
}

VrfTable::~VrfTable() {
    if (route_delete_walker_)
        delete route_delete_walker_;
    if (vrf_delete_walker_)
        delete vrf_delete_walker_;
}

DBEntry *VrfTable::Add(const DBRequest *req) {
    VrfKey *key = static_cast<VrfKey *>(req->key.get());
    VrfData *data = static_cast<VrfData *>(req->data.get());
    VrfEntry *vrf = new VrfEntry(key->name_, data->flags_, agent());

    // Add VRF into name based tree
    if (FindVrfFromName(key->name_)) {
        delete vrf;
        assert(0);
        return NULL;
    }
    vrf->id_ = index_table_.Insert(vrf);
    name_tree_.insert( VrfNamePair(key->name_, vrf));

    vrf->vn_.reset(agent()->vn_table()->Find(data->vn_uuid_));
    return vrf;
}

bool VrfTable::OnChange(DBEntry *entry, const DBRequest *req) {
    bool ret = false;
    VrfEntry *vrf = static_cast<VrfEntry *>(entry);
    VrfData *data = static_cast<VrfData *>(req->data.get());
    vrf->set_flags(data->flags_);

    VnEntry *vn = agent()->vn_table()->Find(data->vn_uuid_);
    if (vn != vrf->vn_.get()) {
        vrf->vn_.reset(vn);
        vrf->ResyncRoutes();
        ret = true;
    }

    uint32_t vxlan_id = VxLanTable::kInvalidvxlan_id;
    if (vn) {
        vxlan_id = vn->GetVxLanId();
    }
    vrf->UpdateVxlanId(agent(), vxlan_id);
    return ret;
}

bool VrfTable::Delete(DBEntry *entry, const DBRequest *req) {
    VrfEntry *vrf = static_cast<VrfEntry *>(entry);
    VrfData *data = static_cast<VrfData *>(req->data.get());

    // VRF can be created by both config and VGW. VRF cannot be deleted till
    // both config and VGW delete it.
    vrf->flags_ &= ~data->flags_;
    if (vrf->flags_ != 0)
        return false;

    // Delete the L2 Receive routes added by default
    BridgeAgentRouteTable *l2_table = static_cast<BridgeAgentRouteTable *>
        (vrf->rt_table_db_[Agent::BRIDGE]);
    l2_table->Delete(agent()->local_vm_peer(), vrf->GetName(),
                     agent()->vrrp_mac(), 0);
    const InetInterface *vhost = static_cast<const InetInterface *>
        (agent()->vhost_interface());
    if (vhost && vhost->xconnect()) {
        l2_table->Delete(agent()->local_vm_peer(), vrf->GetName(),
                         vhost->xconnect()->mac(), 0);
    }

    if (agent()->isVmwareMode()) {
        PhysicalInterfaceKey key(agent()->params()->vmware_physical_port());
        Interface *intf = static_cast<Interface *>
            (agent()->interface_table()->FindActiveEntry(&key));
        if (intf) {
             l2_table->Delete(agent()->local_vm_peer(), vrf->GetName(),
                              intf->mac(), 0);
        }
    }

    vrf->UpdateVxlanId(agent(), VxLanTable::kInvalidvxlan_id);
    vrf->vn_.reset(NULL);
    if (vrf->table_label() != MplsTable::kInvalidLabel) {
        MplsLabel::Delete(agent(), vrf->table_label());
        vrf->set_table_label(MplsTable::kInvalidLabel);
    }
    vrf->deleter_->Delete();
    vrf->StartDeleteTimer();
    vrf->SendObjectLog(AgentLogEvent::DELETE_TRIGGER);
    return true;
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
        vrf->DeleteRouteTables();
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

VrfEntry *VrfTable::FindVrfFromIdIncludingDeletedVrf(size_t index) {
    VrfEntry *vrf = index_table_.At(index);
    return vrf;
}

InetUnicastAgentRouteTable *VrfTable::GetInet4UnicastRouteTable
    (const string &vrf_name) {
    return static_cast<InetUnicastAgentRouteTable *>
        (GetRouteTable(vrf_name, Agent::INET4_UNICAST));
}

AgentRouteTable *VrfTable::GetInet4MulticastRouteTable(const string &vrf_name) {
    return GetRouteTable(vrf_name, Agent::INET4_MULTICAST);
}

AgentRouteTable *VrfTable::GetEvpnRouteTable(const string &vrf_name) {
    return GetRouteTable(vrf_name, Agent::EVPN);
}

AgentRouteTable *VrfTable::GetBridgeRouteTable(const string &vrf_name) {
    return GetRouteTable(vrf_name, Agent::BRIDGE);
}

InetUnicastAgentRouteTable *VrfTable::GetInet6UnicastRouteTable
    (const string &vrf_name) {
    return static_cast<InetUnicastAgentRouteTable *>
        (GetRouteTable(vrf_name, Agent::INET6_UNICAST));
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
    req.data.reset(new VrfData(flags, nil_uuid()));
    Enqueue(&req);
}

void VrfTable::CreateVrfReq(const string &name,
                            const boost::uuids::uuid &vn_uuid, uint32_t flags) {
    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    req.key.reset(new VrfKey(name));
    req.data.reset(new VrfData(flags, vn_uuid));
    Enqueue(&req);
}

void VrfTable::CreateVrf(const string &name, uint32_t flags) {
    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    req.key.reset(new VrfKey(name));
    req.data.reset(new VrfData(flags, nil_uuid()));
    Process(req);
}

void VrfTable::DeleteVrfReq(const string &name, uint32_t flags) {
    DBRequest req(DBRequest::DB_ENTRY_DELETE);
    req.key.reset(new VrfKey(name));
    req.data.reset(new VrfData(flags, nil_uuid()));
    Enqueue(&req);
}

void VrfTable::DeleteVrf(const string &name, uint32_t flags) {
    DBRequest req(DBRequest::DB_ENTRY_DELETE);
    req.key.reset(new VrfKey(name));
    req.data.reset(new VrfData(flags, nil_uuid()));
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

void VrfTable::DeleteFromDbTree(int table_type, const std::string &vrf_name) {
    dbtree_[table_type].erase(vrf_name);
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

static VrfData *BuildData(Agent *agent, IFMapNode *node) {
    boost::uuids::uuid vn_uuid = nil_uuid();
    IFMapAgentTable *table = static_cast<IFMapAgentTable *>(node->table());
    DBGraph *graph = table->GetGraph();

    for (DBGraphVertex::adjacency_iterator iter = node->begin(graph);
         iter != node->end(graph); ++iter) {
        IFMapNode *adj_node =
            static_cast<IFMapNode *>(iter.operator->());

        if (iter->IsDeleted() ||
            (adj_node->table() != agent->cfg()->cfg_vn_table())) {
            continue;
        }

        VirtualNetwork *cfg =
            static_cast <VirtualNetwork *> (adj_node->GetObject());
        if (cfg == NULL) {
            continue;
        }

        if (!IsVRFServiceChainingInstance(adj_node->name(), node->name())) {
            autogen::IdPermsType id_perms = cfg->id_perms();
            CfgUuidSet(id_perms.uuid.uuid_mslong, id_perms.uuid.uuid_lslong,
                       vn_uuid);
            break;
        }
    }

    return new VrfData(VrfData::ConfigVrf, vn_uuid);
}

bool VrfTable::IFLinkToReq(IFMapLink *link, IFMapNode *node,
                           const string &peer_type, IFMapNode *peer,
                           DBRequest &req) {
    // If peer is VN, it means there is change in VRF to VN link. This can
    // affect config for config modules. Invoke IFNodeToReq iteself. This
    // should not affect scaling since we dont expect many changes for
    // VN to VRF link
    if (peer_type == "virtual-network") {
        return IFNodeToReq(node, req);
    }

    // Another neighbour we are interested in
    // virtual-machine-interface-routing-instance. Change to think link can 
    // modify VRF for connected interface. There are two cases here,
    // peer is NULL :
    //    Means, virtual-machine-interface-routing-instance node is deleted.
    //    It also means, VMI link is also deleted and it would have taken 
    //    care of the necessary changes.
    // peer is Non-NULL:
    //    Propogate change till the connected VMI
    if (peer && peer->table() == agent()->cfg()->cfg_vm_port_vrf_table()) {
        if (agent()->cfg_listener()->SkipNode
            (peer, agent()->cfg()->cfg_vm_port_vrf_table())) {
            return false;
        }

        agent()->interface_table()->VmInterfaceVrfSync(peer);
    }

    return false;
}

bool VrfTable::IFNodeToReq(IFMapNode *node, DBRequest &req) {
    req.key.reset(new VrfKey(node->name()));
    IFMapAgentTable *table = static_cast<IFMapAgentTable *>(node->table());
    DBGraph *graph = table->GetGraph();

    //Trigger add or delete only for non fabric VRF
    if (node->IsDeleted()) {
        if (IsStaticVrf(node->name())) {
            //Fabric and link-local VRF will not be deleted,
            //upon config delete
            req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
        } else {
            req.oper = DBRequest::DB_ENTRY_DELETE;
        }
        req.data.reset(new VrfData(VrfData::ConfigVrf, nil_uuid()));
    } else {
        req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
        req.data.reset(BuildData(agent(), node));
    }

    //When VRF config delete comes, first enqueue VRF delete
    //so that when link evaluation happens, all point to deleted VRF
    Enqueue(&req);

    if (node->IsDeleted()) {
        return false;
    }

    // Resync any vmport dependent on this VRF
    // While traversing the path
    // virtual-machine-interface <-> virtual-machine-interface-routing-instance
    // <-> routing-instance path, we may have skipped a routing-instance that
    // failed SkipNode()
    for (DBGraphVertex::adjacency_iterator iter = node->begin(graph);
         iter != node->end(graph); ++iter) {
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
    AgentSandeshPtr sand(new AgentVrfSandesh(context(), get_name()));
    sand->DoSandesh(sand);
}

AgentSandeshPtr VrfTable::GetAgentSandesh(const AgentSandeshArguments *args,
                                          const std::string &context) {
    return AgentSandeshPtr(new AgentVrfSandesh(context,
                                               args->GetString("name")));
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
        walk_done_++;
    }

    static uint32_t walk_start_;
    static uint32_t walk_done_;
};
uint32_t RouteDeleteWalker::walk_start_;
uint32_t RouteDeleteWalker::walk_done_;

void VrfTable::DeleteRoutes() {
    if (route_delete_walker_ == NULL) {
        route_delete_walker_ = new RouteDeleteWalker(agent());
    }
    RouteDeleteWalker *route_delete_walker =
        static_cast<RouteDeleteWalker *>(route_delete_walker_);
    route_delete_walker->WalkDoneCallback
        (boost::bind(&RouteDeleteWalker::WalkDone, route_delete_walker));
    route_delete_walker->walk_start_++;
    route_delete_walker->StartVrfWalk();
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
    }

private:
};

void VrfTable::Shutdown() {
    if (vrf_delete_walker_ == NULL) {
        vrf_delete_walker_ = new VrfDeleteWalker(agent());
    }
    VrfDeleteWalker *vrf_delete_walker =
        static_cast<VrfDeleteWalker *>(vrf_delete_walker_);
    vrf_delete_walker->WalkDoneCallback(boost::bind
                                        (&VrfDeleteWalker::WalkDone,
                                         vrf_delete_walker));
    vrf_delete_walker->StartVrfWalk();
}
