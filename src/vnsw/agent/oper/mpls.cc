/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/uuid/uuid_io.hpp>
#include <cmn/agent_cmn.h>
#include <init/agent_param.h>
#include <base/task_annotations.h>
#include <oper/interface_common.h>
#include <oper/vrf.h>
#include <oper/nexthop.h>
#include <oper/mpls.h>
#include <oper/mirror_table.h>
#include <oper/agent_sandesh.h>

using namespace std;

MplsTable *MplsTable::mpls_table_;

MplsLabel::~MplsLabel() { 
    if (label_ == MplsTable::kInvalidLabel) {
        return;
    }
    if (!IsFabricMulticastReservedLabel() &&
        free_label_) {
        agent_->mpls_table()->FreeLabel(label_);
    }
}

DBEntryBase::KeyPtr MplsLabel::GetDBRequestKey() const {
    MplsLabelKey *key = new MplsLabelKey(type_, label_);
    return DBEntryBase::KeyPtr(key);
}

void MplsLabel::SetKey(const DBRequestKey *k) { 
    const MplsLabelKey *key = static_cast<const MplsLabelKey *>(k);
    type_ = key->type_;
    label_ = key->label_;
}

std::auto_ptr<DBEntry> MplsTable::AllocEntry(const DBRequestKey *k) const {
    const MplsLabelKey *key = static_cast<const MplsLabelKey *>(k);
    MplsLabel *mpls = new MplsLabel(agent(), key->type_, key->label_);
    return std::auto_ptr<DBEntry>(static_cast<DBEntry *>(mpls));
}

DBEntry *MplsTable::Add(const DBRequest *req) {
    MplsLabelKey *key = static_cast<MplsLabelKey *>(req->key.get());
    MplsLabel *mpls = new MplsLabel(agent(), key->type_, key->label_);

    mpls->free_label_ = true;
    if (!mpls->IsFabricMulticastReservedLabel()) {
        UpdateLabel(key->label_, mpls);
    }
    ChangeHandler(mpls, req);
    mpls->SendObjectLog(this, AgentLogEvent::ADD);
    return mpls;
}

bool MplsTable::OnChange(DBEntry *entry, const DBRequest *req) {
    bool ret;
    MplsLabel *mpls = static_cast<MplsLabel *>(entry);
    ret = ChangeHandler(mpls, req);
    mpls->SendObjectLog(this, AgentLogEvent::CHANGE);
    return ret;
}

bool MplsTable::ChangeNH(MplsLabel *mpls, NextHop *nh) {
    if (mpls == NULL)
        return false;

    if (mpls->nh_ != nh) {
        mpls->nh_ = nh;
        assert(nh);
        mpls->SyncDependentPath();
        return true;
    }
    return false;
}

// No Change expected for MPLS Label
bool MplsTable::ChangeHandler(MplsLabel *mpls, const DBRequest *req) {
    bool ret = false;
    MplsLabelData *data = static_cast<MplsLabelData *>(req->data.get());
    NextHop *nh = static_cast<NextHop *>
        (agent()->nexthop_table()->FindActiveEntry(data->nh_key));
    if (!nh) {
        //NextHop not found, point mpls label to discard
        DiscardNH key;
        nh = static_cast<NextHop *>
            (agent()->nexthop_table()->FindActiveEntry(&key));
    }
    ret = ChangeNH(mpls, nh);

    return ret;
}

bool MplsTable::Delete(DBEntry *entry, const DBRequest *req) {
    MplsLabel *mpls = static_cast<MplsLabel *>(entry);
    mpls->SendObjectLog(this, AgentLogEvent::DELETE);
    return true;
}

void MplsTable::CreateTableLabel(const Agent *agent,
                                 uint32_t label,
                                 const std::string &vrf_name,
                                 bool policy) {
    DBRequest nh_req;
    nh_req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    VrfNHKey *vrf_nh_key = new VrfNHKey(vrf_name, false, false);
    nh_req.key.reset(vrf_nh_key);
    nh_req.data.reset(new VrfNHData(false));
    agent->nexthop_table()->Process(nh_req);

    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;

    MplsLabelKey *key = new MplsLabelKey(MplsLabel::VPORT_NH, label);
    req.key.reset(key);

    MplsLabelData *data = new MplsLabelData(vrf_name, policy);
    req.data.reset(data);

    agent->mpls_table()->Process(req);
    return;
}

void MplsTable::ReserveLabel(uint32_t start, uint32_t end) {
    // We want to allocate labels from an offset
    // Pre-allocate entries
    for (unsigned int i = start; i < end; i++) {
        InsertAtIndex(i, NULL);
    }
}

DBTableBase *MplsTable::CreateTable(DB *db, const std::string &name) {
    mpls_table_ = new MplsTable(db, name);
    mpls_table_->Init();
    return mpls_table_;
};

void MplsTable::Process(DBRequest &req) {
    agent()->ConcurrencyCheck();
    DBTablePartition *tpart =
        static_cast<DBTablePartition *>(GetTablePartition(req.key.get()));
    tpart->Process(NULL, &req);
}

void MplsLabel::CreateVlanNh(const Agent *agent,
                             uint32_t label,
                             const uuid &intf_uuid,
                             uint16_t tag) {
    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;

    MplsLabelKey *key = new MplsLabelKey(MplsLabel::VPORT_NH, label);
    req.key.reset(key);

    MplsLabelData *data = new MplsLabelData(intf_uuid, tag);
    req.data.reset(data);

    agent->mpls_table()->Process(req);
    return;
}

void MplsLabel::CreateInetInterfaceLabel(const Agent *agent,
                                         uint32_t label,
                                         const string &ifname,
                                         bool policy, 
                                         InterfaceNHFlags::Type type,
                                         const MacAddress &mac) {
    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;

    MplsLabelKey *key = new MplsLabelKey(MplsLabel::VPORT_NH, label);
    req.key.reset(key);

    MplsLabelData *data = new MplsLabelData(ifname, policy, type, mac);
    req.data.reset(data);

    agent->mpls_table()->Process(req);
    return;
}

void MplsLabel::CreateVPortLabel(const Agent *agent,
                                 uint32_t label,
                                 const uuid &intf_uuid,
                                 bool policy,
                                 InterfaceNHFlags::Type type,
                                 const MacAddress &mac) {
    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;

    MplsLabelKey *key = new MplsLabelKey(MplsLabel::VPORT_NH, label);
    req.key.reset(key);

    MplsLabelData *data = new MplsLabelData(intf_uuid, policy, type, mac);
    req.data.reset(data);

    agent->mpls_table()->Process(req);
    return;
}

void MplsLabel::CreateEcmpLabel(const Agent *agent,
                                uint32_t label, COMPOSITETYPE type, bool policy,
                                ComponentNHKeyList &component_nh_key_list,
                                const std::string vrf_name) {
    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;

    MplsLabelKey *key = new MplsLabelKey(MplsLabel::VPORT_NH, label);
    req.key.reset(key);

    MplsLabelData *data = new MplsLabelData(type, policy, component_nh_key_list,
                                            vrf_name);
    req.data.reset(data);

    agent->mpls_table()->Process(req);
    return;
}

void MplsLabel::Delete(const Agent *agent, uint32_t label) {
    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_DELETE;

    MplsLabelKey *key = new MplsLabelKey(MplsLabel::INVALID, label);
    req.key.reset(key);
    req.data.reset(NULL);

    agent->mpls_table()->Process(req);
}

void MplsLabel::DeleteReq(const Agent *agent, uint32_t label) {
    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_DELETE;

    MplsLabelKey *key = new MplsLabelKey(MplsLabel::INVALID, label);
    req.key.reset(key);
    req.data.reset(NULL);

    agent->mpls_table()->Enqueue(&req);
}

bool MplsLabel::IsFabricMulticastReservedLabel() const {
    if (type_ != MplsLabel::MCAST_NH)
        return false;

    return (agent_->mpls_table()->
            IsFabricMulticastLabel(label_));
}

bool MplsLabel::DBEntrySandesh(Sandesh *sresp, std::string &name) const {
    MplsResp *resp = static_cast<MplsResp *>(sresp);

    MplsSandeshData data;
    data.set_label(label_);
    switch (type_) {
        case MplsLabel::INVALID:
            data.set_type("invalid");
            break;
        case MplsLabel::MCAST_NH:
            data.set_type("mcast_nh");
            break;
        case MplsLabel::VPORT_NH:
            data.set_type("vport_nh");
            break;
        default:
            assert(0);
    }
    nh_->SetNHSandeshData(data.nh);
    std::vector<MplsSandeshData> &list =
            const_cast<std::vector<MplsSandeshData>&>(resp->get_mpls_list());
    list.push_back(data);

    return true;
}

void MplsLabel::SendObjectLog(const AgentDBTable *table,
                              AgentLogEvent::type event) const {
    MplsObjectLogInfo info;
    string str, type_str, nh_type;

    switch(type_) {
    case INVALID:
        type_str.assign("Invalid ");
        break;
    case VPORT_NH:
        type_str.assign("Virtual Port");
        break;
    case MplsLabel::MCAST_NH:
        type_str.assign("mcast_nh");
        break;
    }
    info.set_type(type_str);
    info.set_label((int)label_);
    switch (event) {
    case AgentLogEvent::ADD:
        str.assign("Addition ");
        break;
    case AgentLogEvent::DELETE:
        str.assign("Deletion ");
        info.set_event(str);
        OPER_TRACE_ENTRY(Mpls, table, info);
        return;
    case AgentLogEvent::CHANGE:
        str.assign("Modification ");
        break;
    default:
        str.assign("Unknown");
        break;
    }
    info.set_event(str);
    const NextHop *nh = nexthop();
    const Interface *intf = NULL;
    /* Mpls is not expected to have any other nexthop apart from Interface 
       or Vlan */
    if (nh != NULL) {
        string policy_str("Disabled");
        const InterfaceNH *if_nh;
        const VlanNH *vlan_nh;

        switch(nh->GetType()) {
        case NextHop::INTERFACE:
            nh_type.assign("INTERFACE");
            if_nh = static_cast<const InterfaceNH *>(nh);
            intf = if_nh->GetInterface();
            if (if_nh->PolicyEnabled()) {
                policy_str.assign("Enabled");
            }
            info.set_policy(policy_str);
            break;
        case NextHop::VLAN:
            nh_type.assign("VLAN");
            vlan_nh = static_cast<const VlanNH *>(nh);
            intf = vlan_nh->GetInterface();
            info.set_vlan_tag(vlan_nh->GetVlanTag());
            break;
        case NextHop::COMPOSITE:
            nh_type.assign("Composite");    
            break;
        default:
            nh_type.assign("unknown");
            break;
        }
    }
    info.set_nh_type(nh_type);
    /* Interface Nexthop pointed by Mpls object will always be of type VMPORT */
    if (intf) {
        string if_type_str;
        switch(intf->type()) {
        case Interface::VM_INTERFACE:
            if_type_str.assign("VM_INTERFACE");
            break;
        default:
            if_type_str.assign("Invalid");
            break;
        }
        info.set_intf_type(if_type_str);
        info.set_intf_uuid(UuidToString(intf->GetUuid()));
        info.set_intf_name(intf->name());
    }
    OPER_TRACE_ENTRY(Mpls, table, info);
}

void MplsLabel::SyncDependentPath() {
    for (DependentPathList::iterator iter =
         mpls_label_.begin(); iter != mpls_label_.end(); iter++) {
        AgentRoute *rt = iter.operator->();
        LOG(DEBUG, "Syncing route" << rt->ToString());
        rt->EnqueueRouteResync();
    }
}

void MplsReq::HandleRequest() const {
    AgentSandeshPtr sand(new AgentMplsSandesh(context(), get_type(),
                               get_label()));
    sand->DoSandesh(sand);
}

AgentSandeshPtr MplsTable::GetAgentSandesh(const AgentSandeshArguments *args,
                                           const std::string &context) {
    return AgentSandeshPtr(new AgentMplsSandesh(context,
                                           args->GetString("type"), args->GetString("label")));
}

void MplsTable::CreateMcastLabel(uint32_t label,
                                 COMPOSITETYPE type,
                                 ComponentNHKeyList &component_nh_key_list,
                                 const std::string vrf_name) {
    if (label == 0 || label == MplsTable::kInvalidLabel)
        return;

    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;

    MplsLabelKey *key = new MplsLabelKey(MplsLabel::MCAST_NH, label);
    req.key.reset(key);

    MplsLabelData *data = new MplsLabelData(type, false, component_nh_key_list,
                                            vrf_name);
    req.data.reset(data);

    Process(req);
    return;
}

void MplsTable::DeleteMcastLabel(uint32_t src_label) {
    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_DELETE;

    MplsLabelKey *key = new MplsLabelKey(MplsLabel::MCAST_NH, src_label);
    req.key.reset(key);
    req.data.reset(NULL);

    Process(req);
    return;
}

void MplsTable::ReserveMulticastLabel(uint32_t start,
                                      uint32_t end,
                                      uint8_t idx) {
    multicast_label_start_[idx] = start;
    multicast_label_end_[idx] = end;
    ReserveLabel(start, end);
}

bool MplsTable::IsFabricMulticastLabel(uint32_t label) const {
    for (uint8_t count = 0; count < MAX_XMPP_SERVERS; count++) {
        if ((label >= multicast_label_start_[count]) &&
            (label <= multicast_label_end_[count])) return true;
    }
    return false;
}
