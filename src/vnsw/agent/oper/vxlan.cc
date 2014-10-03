
//
//  vxlan.cc
//  vnsw/agent
//

#include <cmn/agent_cmn.h>
#include <base/task_annotations.h>
#include <oper/vrf.h>
#include <oper/nexthop.h>
#include <oper/vxlan.h>
#include <oper/mirror_table.h>
#include <oper/agent_sandesh.h>

static VxLanTable *vxlan_id_table_;

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
    CHECK_CONCURRENCY("db::DBTable");
    DBTablePartition *tpart =
        static_cast<DBTablePartition *>(GetTablePartition(req.key.get()));
    Input(tpart, NULL, &req);
}

DBEntry *VxLanTable::Add(const DBRequest *req) {
    VxLanIdKey *key = static_cast<VxLanIdKey *>(req->key.get());
    VxLanId *vxlan_id = new VxLanId(key->vxlan_id());

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

// No Change expected for vxlan_id vxlan_id
bool VxLanTable::ChangeHandler(VxLanId *vxlan_id, const DBRequest *req) {
    bool ret = false;
    VxLanIdData *data = static_cast<VxLanIdData *>(req->data.get());

    Agent::GetInstance()->nexthop_table()->Process(data->nh_req());

    VrfNHKey nh_key(data->vrf_name(), false);
    NextHop *nh = static_cast<NextHop *>
        (Agent::GetInstance()->nexthop_table()->FindActiveEntry(&nh_key));

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

void VxLanTable::OnZeroRefcount(AgentDBEntry *e) {
    const VxLanId *vxlan_id = static_cast<const VxLanId *>(e);
    VxLanId::Delete(vxlan_id->vxlan_id());
}

DBTableBase *VxLanTable::CreateTable(DB *db, const std::string &name) {
    vxlan_id_table_ = new VxLanTable(db, name);
    vxlan_id_table_->Init();

    return vxlan_id_table_;
}

void VxLanId::Create(uint32_t vxlan_id, const string &vrf_name) {
    DBRequest nh_req;
    nh_req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    VrfNHKey *vrf_nh_key = new VrfNHKey(vrf_name, false);
    nh_req.key.reset(vrf_nh_key);
    nh_req.data.reset(NULL);

    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;

    VxLanIdKey *key = new VxLanIdKey(vxlan_id);
    req.key.reset(key);

    VxLanIdData *data = new VxLanIdData(vrf_name, nh_req);
    req.data.reset(data);

    vxlan_id_table_->Process(req);
    return;
}
                                   
void VxLanId::Delete(uint32_t vxlan_id) {
    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_DELETE;

    VxLanIdKey *key = new VxLanIdKey(vxlan_id);
    req.key.reset(key);
    req.data.reset(NULL);
    vxlan_id_table_->Process(req);
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

void VxLanId::SendObjectLog(AgentLogEvent::type event) const {
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
            OPER_TRACE(VxLan, info);
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
    OPER_TRACE(VxLan, info);
}

void VxLanReq::HandleRequest() const {
    AgentVxLanSandesh *sand = new AgentVxLanSandesh(context());
    sand->DoSandesh();
}
