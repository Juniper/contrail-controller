
//
//  vxlan.cc
//  vnsw/agent
//

#include <cmn/agent_cmn.h>
#include <base/task_annotations.h>
#include <oper/vrf.h>
#include <oper/vn.h>
#include <oper/nexthop.h>
#include <oper/vxlan.h>
#include <oper/mirror_table.h>
#include <oper/agent_sandesh.h>

using namespace std;

VxLanId::~VxLanId() { 
    if (vxlan_id_ == VxLanTable::kInvalidvxlan_id) {
        return;
    }
}

DBEntryBase::KeyPtr VxLanId::GetDBRequestKey() const {
    VxLanIdKey *key = new VxLanIdKey(vxlan_id_);
    return DBEntryBase::KeyPtr(key);
}

void VxLanId::SetKey(const DBRequestKey *k) { 
    const VxLanIdKey *key = static_cast<const VxLanIdKey *>(k);
    vxlan_id_ = key->vxlan_id();
}

std::auto_ptr<DBEntry> VxLanTable::AllocEntry(const DBRequestKey *k) const {
    const VxLanIdKey *key = static_cast<const VxLanIdKey *>(k);
    VxLanId *vxlan_id = new VxLanId(key->vxlan_id());
    return std::auto_ptr<DBEntry>(static_cast<DBEntry *>(vxlan_id));
}

void VxLanTable::Process(DBRequest &req) {
    agent()->ConcurrencyCheck();
    DBTablePartition *tpart =
        static_cast<DBTablePartition *>(GetTablePartition(req.key.get()));
    Input(tpart, NULL, &req);
}

DBEntry *VxLanTable::Add(const DBRequest *req) {
    VxLanIdKey *key = static_cast<VxLanIdKey *>(req->key.get());
    VxLanId *vxlan_id = new VxLanId(key->vxlan_id());

    ChangeHandler(vxlan_id, req);
    vxlan_id->SendObjectLog(this, AgentLogEvent::ADD);
    return vxlan_id;
}

bool VxLanTable::OnChange(DBEntry *entry, const DBRequest *req) {
    bool ret;
    VxLanId *vxlan_id = static_cast<VxLanId *>(entry);
    ret = ChangeHandler(vxlan_id, req);
    vxlan_id->SendObjectLog(this, AgentLogEvent::CHANGE);
    return ret;
}

// No Change expected for vxlan_id vxlan_id
bool VxLanTable::ChangeHandler(VxLanId *vxlan_id, const DBRequest *req) {
    bool ret = false;
    VxLanIdData *data = static_cast<VxLanIdData *>(req->data.get());

    Agent::GetInstance()->nexthop_table()->Process(data->nh_req());

    VrfNHKey nh_key(data->vrf_name(), false, true);
    NextHop *nh = static_cast<NextHop *>
        (Agent::GetInstance()->nexthop_table()->FindActiveEntry(&nh_key));

    if (vxlan_id->nh_ != nh) {
        vxlan_id->nh_ = nh;
        ret = true;
    }

    return ret;
}

bool VxLanTable::Delete(DBEntry *entry, const DBRequest *req) {
    VxLanId *vxlan_id = static_cast<VxLanId *>(entry);
    vxlan_id->SendObjectLog(this, AgentLogEvent::DELETE);
    return true;
}

VxLanId *VxLanTable::Find(uint32_t vxlan_id) {
    VxLanIdKey key(vxlan_id);
    return static_cast<VxLanId *>(FindActiveEntry(&key));
}

VxLanId *VxLanTable::FindNoLock(uint32_t vxlan_id) {
    VxLanIdKey key(vxlan_id);
    return static_cast<VxLanId *>(FindActiveEntryNoLock(&key));
}

void VxLanTable::OnZeroRefcount(AgentDBEntry *e) {
    const VxLanId *vxlan_id = static_cast<const VxLanId *>(e);
    Delete(vxlan_id->vxlan_id());
}

// Follows semantics defined for the ConfigTree in vxlan.h
// Return values:
//    - vxlan dbentry if "vn" is "active"
//    - NULL if "vn" is "inactive"
VxLanId *VxLanTable::Locate(uint32_t vxlan_id, const boost::uuids::uuid &vn,
                            const std::string &vrf, bool flood_unknown_unicast){
    // Treat a request without VRF as delete of config entry
    if (vrf.empty()) {
        Delete(vxlan_id, vn);
        return NULL;
    }

    // If there are no config-entries persent for the vxlan,
    //  - Add the config-entry and make it active
    //  - Create VxLan entry
    ConfigTree::iterator it = config_tree_.lower_bound(ConfigKey(vxlan_id,
                                                                 nil_uuid()));
    if (it == config_tree_.end() || it->first.vxlan_id_ != vxlan_id) {
        config_tree_.insert(make_pair(ConfigKey(vxlan_id, vn),
                                      ConfigEntry(vrf, flood_unknown_unicast,
                                                  true)));
        Create(vxlan_id, vrf, flood_unknown_unicast);
        return Find(vxlan_id);
    }

    // Handle change to existing config-entry
    it = config_tree_.find(ConfigKey(vxlan_id, vn));
    if (it != config_tree_.end()) {
        it->second.vrf_ = vrf;
        it->second.flood_unknown_unicast_ = flood_unknown_unicast;

        // If entry is active, update vxlan dbentry with new information
        if (it->second.active_) {
            Create(vxlan_id, vrf, flood_unknown_unicast);
            return Find(vxlan_id);
        }
        // If entry inactive, return NULL
        return NULL;
    }

    // Entry not present, add it to config-tree
    config_tree_.insert(make_pair(ConfigKey(vxlan_id, vn),
                                  ConfigEntry(vrf, flood_unknown_unicast,
                                              false)));
    // Return NULL since the VN is active
    return NULL;
}

VxLanId *VxLanTable::Delete(uint32_t vxlan_id, const boost::uuids::uuid &vn) {
    ConfigTree::iterator it = config_tree_.find(ConfigKey(vxlan_id, vn));
    // Entry not found
    if (it == config_tree_.end()) {
        return NULL;
    }

    // If the entry is active and getting deleted, make new "active" entry
    bool active = it->second.active_;
    config_tree_.erase(it);
    if (active == false)
        return NULL;

    // Make first entry as active
    it = config_tree_.lower_bound(ConfigKey(vxlan_id, nil_uuid()));
    if (it == config_tree_.end() || it->first.vxlan_id_ != vxlan_id)
        return NULL;

    it->second.active_ = true;
    Create(vxlan_id, it->second.vrf_, it->second.flood_unknown_unicast_);
    agent()->vn_table()->ResyncVxlan(it->first.vn_);
    return NULL;
}

DBTableBase *VxLanTable::CreateTable(DB *db, const std::string &name) {
    VxLanTable *table = new VxLanTable(db, name);
    table->Init();
    return table;
}

void VxLanTable::Create(uint32_t vxlan_id, const string &vrf_name,
                        bool flood_unknown_unicast) {
    DBRequest nh_req(DBRequest::DB_ENTRY_ADD_CHANGE);
    nh_req.key.reset(new VrfNHKey(vrf_name, false, true));
    nh_req.data.reset(new VrfNHData(flood_unknown_unicast));

    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    req.key.reset(new VxLanIdKey(vxlan_id));
    req.data.reset(new VxLanIdData(vrf_name, nh_req));

    Process(req);
    return;
}
                                   
void VxLanTable::Delete(uint32_t vxlan_id) {
    DBRequest req(DBRequest::DB_ENTRY_DELETE);
    req.key.reset(new VxLanIdKey(vxlan_id));
    req.data.reset(NULL);
    Process(req);
}

bool VxLanId::DBEntrySandesh(Sandesh *sresp, std::string &name) const {
    VxLanResp *resp = static_cast<VxLanResp *>(sresp);

    VxLanSandeshData data;
    data.set_vxlan_id(vxlan_id_);
    nh_->SetNHSandeshData(data.nh);
    std::vector<VxLanSandeshData> &list =
            const_cast<std::vector<VxLanSandeshData>&>(resp->get_vxlan_list());
    list.push_back(data);

    return true;
}

void VxLanId::SendObjectLog(const AgentDBTable *table,
                            AgentLogEvent::type event) const {
    VxLanObjectLogInfo info;
    string str, nh_type;
    
    info.set_vxlan_id((int)vxlan_id_);
    switch (event) {
        case AgentLogEvent::ADD:
            str.assign("Addition ");
            break;
        case AgentLogEvent::DELETE:
            str.assign("Deletion ");
            info.set_event(str);
            OPER_TRACE_ENTRY(VxLan, table, info);
            return;
        case AgentLogEvent::CHANGE:
            str.assign("Modification ");
            break;
        default:
            str.assign("Unknown");
            break;
    }
    const NextHop *nh = nexthop();
    if (nh != NULL) {
        //const VrfNH *vrf_nh;
        switch(nh->GetType()) {
            case NextHop::VRF: {
                nh_type.assign("VRF"); 
                const VrfNH *vrf_nh = static_cast<const VrfNH *>(nh);   
                info.set_vrf_name(vrf_nh->GetVrf()->GetName());
                break;
            }    
            default:
                nh_type.assign("unknown");
                break;
        }
    }
    info.set_nh_type(nh_type);
    OPER_TRACE_ENTRY(VxLan, table, info);
}

void VxLanReq::HandleRequest() const {
    AgentSandeshPtr sand(new AgentVxLanSandesh(context(), get_vxlan_id()));
    sand->DoSandesh(sand);
}

AgentSandeshPtr VxLanTable::GetAgentSandesh(const AgentSandeshArguments *args,
                                            const std::string &context) {
    return AgentSandeshPtr(new AgentVxLanSandesh(context,
                                               args->GetString("vxlan_id")));
}

/////////////////////////////////////////////////////////////////////////////
// Sandesh routines to dump config tree
/////////////////////////////////////////////////////////////////////////////
class VxLanConfigSandeshTask : public Task {
 public:
    VxLanConfigSandeshTask(Agent *agent, uint32_t vxlan_id, const string &vn,
                           const string &active, const string &context) :
        Task(agent->task_scheduler()->GetTaskId("db::DBTable"), 0),
        agent_(agent), vxlan_id_(vxlan_id), vn_(vn), active_(active),
        context_(context) { }
    ~VxLanConfigSandeshTask() { }
    virtual bool Run();
    std::string Description() const { return "VxLanConfigSandeshTask"; }

 private:
    Agent *agent_;
    uint32_t vxlan_id_;
    string vn_;
    string active_;
    string context_;
    DISALLOW_COPY_AND_ASSIGN(VxLanConfigSandeshTask);
};

bool VxLanConfigSandeshTask::Run() {
    VxLanConfigResp *resp = new VxLanConfigResp();
    vector<VxLanConfigEntry> &list =
        const_cast<vector<VxLanConfigEntry>&>(resp->get_vxlan_config_entries());

    uuid u = nil_uuid();
    if (vn_.empty() == false) {
        u = StringToUuid(vn_);
    }

    const VxLanTable::ConfigTree &tree = agent_->vxlan_table()->config_tree();
    VxLanTable::ConfigTree::const_iterator it = tree.begin();
    while (it != tree.end()) {
        VxLanConfigEntry entry;
        if (vxlan_id_ != 0) {
            if (vxlan_id_ != it->first.vxlan_id_) {
                it++;
                continue;
            }
        }

        if (u != nil_uuid() && u != it->first.vn_) {
            it++;
            continue;
        }

        if (active_.empty() == false) {
            if ((active_ == "true" || active_ == "yes" || active_ == "active")
                && (it->second.active_ != true)) {
                it++;
                continue;
            }

            if ((active_ == "false" || active_ == "no" || active_ == "inactive")
                && (it->second.active_ != false)) {
                it++;
                continue;
            }
        }

        entry.set_vxlan_id(it->first.vxlan_id_);
        entry.set_vn_uuid(UuidToString(it->first.vn_));
        entry.set_vrf(it->second.vrf_);
        entry.set_flood_unknown_unicast(it->second.flood_unknown_unicast_);
        entry.set_active(it->second.active_);
        list.push_back(entry);
        it++;
    }
    resp->set_context(context_);
    resp->set_more(false);
    resp->Response();
    return true;
}

void VxLanConfigReq::HandleRequest() const {
    Agent *agent = Agent::GetInstance();
    VxLanConfigSandeshTask *task =
        new VxLanConfigSandeshTask(agent, get_vxlan_id(), get_vn(),
                                   get_active(), context());
    agent->task_scheduler()->Enqueue(task);
}
