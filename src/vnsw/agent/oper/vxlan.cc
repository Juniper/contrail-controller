
//
//  vxlan.cc
//  vnsw/agent
//

#include <cmn/agent_cmn.h>
#include <oper/vrf.h>
#include <oper/nexthop.h>
#include <oper/vxlan.h>
#include <oper/mirror_table.h>
#include <oper/agent_sandesh.h>

static VxLanTable *vxlan_id_table_;

using namespace std;

VxLanId::~VxLanId() { 
    if (label_ == VxLanTable::kInvalidLabel) {
        return;
    }
}

DBEntryBase::KeyPtr VxLanId::GetDBRequestKey() const {
    VxLanIdKey *key = new VxLanIdKey(label_);
    return DBEntryBase::KeyPtr(key);
}

void VxLanId::SetKey(const DBRequestKey *k) { 
    const VxLanIdKey *key = static_cast<const VxLanIdKey *>(k);
    label_ = key->label_;
}

AgentDBTable *VxLanId::DBToTable() const {
    return vxlan_id_table_;
}

std::auto_ptr<DBEntry> VxLanTable::AllocEntry(const DBRequestKey *k) const {
    const VxLanIdKey *key = static_cast<const VxLanIdKey *>(k);
    VxLanId *vxlan_id = new VxLanId(key->label_);
    return std::auto_ptr<DBEntry>(static_cast<DBEntry *>(vxlan_id));
}

DBEntry *VxLanTable::Add(const DBRequest *req) {
    VxLanIdKey *key = static_cast<VxLanIdKey *>(req->key.get());
    VxLanId *vxlan_id = new VxLanId(key->label_);

    ChangeHandler(vxlan_id, req);
    vxlan_id->SendObjectLog(AgentLogEvent::ADD);
    return vxlan_id;
}

bool VxLanTable::OnChange(DBEntry *entry, const DBRequest *req) {
    bool ret;
    VxLanId *vxlan_id = static_cast<VxLanId *>(entry);
    ret = ChangeHandler(vxlan_id, req);
    vxlan_id->SendObjectLog(AgentLogEvent::CHANGE);
    return ret;
}

// No Change expected for vxlan_id Label
bool VxLanTable::ChangeHandler(VxLanId *vxlan_id, const DBRequest *req) {
    bool ret = false;
    VxLanIdData *data = static_cast<VxLanIdData *>(req->data.get());
    VrfNHKey nh_key(data->vrf_name_, false);
    NextHop *nh = static_cast<NextHop *>
        (Agent::GetInstance()->GetNextHopTable()->FindActiveEntry(&nh_key));

    if (vxlan_id->nh_ != nh) {
        vxlan_id->nh_ = nh;
        ret = true;
    }

    return ret;
}

void VxLanTable::Delete(DBEntry *entry, const DBRequest *req) {
    VxLanId *vxlan_id = static_cast<VxLanId *>(entry);
    vxlan_id->SendObjectLog(AgentLogEvent::DELETE);
}

DBTableBase *VxLanTable::CreateTable(DB *db, const std::string &name) {
    vxlan_id_table_ = new VxLanTable(db, name);
    vxlan_id_table_->Init();

    return vxlan_id_table_;
}

void VxLanId::CreateReq(uint32_t label, const string &vrf_name) {
    //Enqueue creation of NH 
    DBRequest nh_req;
    nh_req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    VrfNHKey *vrf_nh_key = new VrfNHKey(vrf_name, false);
    nh_req.key.reset(vrf_nh_key);
    nh_req.data.reset(NULL);
    Agent::GetInstance()->GetNextHopTable()->Enqueue(&nh_req);

    //Enqueue vxlan_id addition
    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;

    VxLanIdKey *key = new VxLanIdKey(label);
    req.key.reset(key);

    VxLanIdData *data = new VxLanIdData(vrf_name);
    req.data.reset(data);

    vxlan_id_table_->Enqueue(&req);
    return;
}
                                   
void VxLanId::DeleteReq(uint32_t label) {
    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_DELETE;

    VxLanIdKey *key = new VxLanIdKey(label);
    req.key.reset(key);
    req.data.reset(NULL);
    vxlan_id_table_->Enqueue(&req);
}

bool VxLanId::DBEntrySandesh(Sandesh *sresp, std::string &name) const {
    VxLanResp *resp = static_cast<VxLanResp *>(sresp);

    VxLanSandeshData data;
    data.set_label(label_);
    nh_->SetNHSandeshData(data.nh);
    std::vector<VxLanSandeshData> &list =
            const_cast<std::vector<VxLanSandeshData>&>(resp->get_vxlan_list());
    list.push_back(data);

    return true;
}

void VxLanId::SendObjectLog(AgentLogEvent::type event) const {
    VxLanObjectLogInfo info;
    string str, nh_type;
    
    info.set_label((int)label_);
    switch (event) {
        case AgentLogEvent::ADD:
            str.assign("Addition ");
            break;
        case AgentLogEvent::DELETE:
            str.assign("Deletion ");
            info.set_event(str);
            OPER_TRACE(VxLan, info);
            return;
        case AgentLogEvent::CHANGE:
            str.assign("Modification ");
            break;
        default:
            str.assign("Unknown");
            break;
    }
    const NextHop *nh = GetNextHop();
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
    OPER_TRACE(VxLan, info);
}

void VxLanReq::HandleRequest() const {
    AgentVxLanSandesh *sand = new AgentVxLanSandesh(context());
    sand->DoSandesh();
}
