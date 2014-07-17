/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <cmn/agent_cmn.h>
#include <boost/uuid/uuid_io.hpp>

#include <oper/interface_common.h>
#include <oper/mirror_table.h>
#include <oper/vrf.h>
#include <oper/vrf_assign.h>
#include <oper/nexthop.h>
#include <oper/agent_sandesh.h>

using namespace std;
using namespace boost::uuids;

VrfAssignTable *VrfAssignTable::vrf_assign_table_;

///////////////////////////////////////////////////////////////////////////
// VRF Assignment table routines
///////////////////////////////////////////////////////////////////////////
std::auto_ptr<DBEntry> VrfAssignTable::AllocEntry(const DBRequestKey *k) const {
    return std::auto_ptr<DBEntry>(static_cast<DBEntry *>(AllocWithKey(k)));
}

VrfAssign *VrfAssignTable::AllocWithKey(const DBRequestKey *k) const {
    const VrfAssign::VrfAssignKey *key = 
        static_cast<const VrfAssign::VrfAssignKey *>(k);
    VrfAssign *assign = NULL;

    VmInterfaceKey intf_key(AgentKey::ADD_DEL_CHANGE, key->intf_uuid_, "");
    Interface *interface = static_cast<Interface *>
        (agent()->interface_table()->Find(&intf_key, true));

    switch (key->type_) {
    case VrfAssign::VLAN: {
        VlanVrfAssign *vlan_assign = 
            new VlanVrfAssign(interface, key->vlan_tag_);
        assign = static_cast<VrfAssign *>(vlan_assign);
    }
    break;

    default:
        assert(0);
        break;
    }

    return assign;
}

DBEntry *VrfAssignTable::Add(const DBRequest *req) {
    const VrfAssign::VrfAssignKey *key = 
        static_cast<const VrfAssign::VrfAssignKey *>(req->key.get());
    VrfAssign *rule = AllocWithKey(key);
    if (rule->interface_.get() == NULL) {
        delete rule;
        return NULL;
    }

    OnChange(rule, req);
    return rule;
}

// Assigned VRF can potentially change
bool VrfAssignTable::OnChange(DBEntry *entry, const DBRequest *req) {
    return (static_cast<VrfAssign *>(entry))->Change(req);
}

void VrfAssignTable::Delete(DBEntry *entry, const DBRequest *req) {
}

DBTableBase *VrfAssignTable::CreateTable(DB *db, const string &name) {
    vrf_assign_table_ = new VrfAssignTable(db, name);
    vrf_assign_table_->Init();
    return vrf_assign_table_;
}

Interface *VrfAssignTable::FindInterface(const uuid &intf_uuid) {
    VmInterfaceKey key(AgentKey::ADD_DEL_CHANGE, intf_uuid, "");
    return static_cast<Interface *>
        (Agent::GetInstance()->interface_table()->FindActiveEntry(&key));
}

VrfEntry *VrfAssignTable::FindVrf(const string &name) {
    return Agent::GetInstance()->vrf_table()->FindVrfFromName(name);
}

void VrfAssignTable::CreateVlanReq(const boost::uuids::uuid &intf_uuid,
        const std::string &vrf_name, uint16_t vlan_tag) {
    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;

    VrfAssign::VrfAssignKey *key = new VrfAssign::VrfAssignKey();
    key->VlanInit(intf_uuid, vlan_tag);
    req.key.reset(key);

    VrfAssign::VrfAssignData *data = new VrfAssign::VrfAssignData(vrf_name);
    req.data.reset(data);

    vrf_assign_table_->Enqueue(&req);
    return;
}

void VrfAssignTable::DeleteVlanReq(const boost::uuids::uuid &intf_uuid,
        uint16_t vlan_tag) {
    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_DELETE;

    VrfAssign::VrfAssignKey *key = new VrfAssign::VrfAssignKey();
    key->VlanInit(intf_uuid, vlan_tag);
    req.key.reset(key);

    req.data.reset(NULL);
    vrf_assign_table_->Enqueue(&req);
    return;
}

VrfAssign *VrfAssignTable::FindVlanReq(const boost::uuids::uuid &intf_uuid,
                                       uint16_t vlan_tag) {
    VrfAssign::VrfAssignKey key;
    key.VlanInit(intf_uuid, vlan_tag);
    return static_cast<VrfAssign *>(vrf_assign_table_->FindActiveEntry(&key));
}

///////////////////////////////////////////////////////////////////////////
// VRF Assignment routines
///////////////////////////////////////////////////////////////////////////
bool VrfAssign::IsLess(const DBEntry &rhs) const {
    const VrfAssign &vassign = static_cast<const VrfAssign &>(rhs);
    if (type_ != vassign.type_) {
        return type_ < vassign.type_;
    }

    if (interface_.get() != vassign.interface_.get()) {
        return interface_.get() < vassign.interface_.get();
    }

    return VrfAssignIsLess(vassign);
}

bool VrfAssign::Change(const DBRequest *req) {
    bool ret = false;
    VrfAssign::VrfAssignData *data = 
        static_cast<VrfAssign::VrfAssignData *>(req->data.get());

    VrfEntry *vrf = VrfAssignTable::GetInstance()->FindVrf(data->vrf_name_);
    if (vrf_.get() != vrf) {
        vrf_ = vrf;
        ret = true;
    }

    if (VrfAssignChange(req)) {
        ret = true;
    }

    return ret;
}

bool VlanVrfAssign::VrfAssignChange(const DBRequest *req) {
    bool ret = false;
    const VrfAssign::VrfAssignKey *key =
        static_cast<const VrfAssign::VrfAssignKey *>(req->key.get());
    VlanNHKey nh_key(key->intf_uuid_, key->vlan_tag_);
    Agent *agent = Agent::GetInstance();
    const NextHop *nh =  static_cast<NextHop *>
        (agent->nexthop_table()->FindActiveEntry(&nh_key));
    assert(nh);
    if (nh_ != nh) {
        nh_ = nh;
        ret = true;
    }
    return ret;
}

///////////////////////////////////////////////////////////////////////////
// VLAN based VRF Assignment routines
///////////////////////////////////////////////////////////////////////////
bool VlanVrfAssign::VrfAssignIsLess(const VrfAssign &rhs) const {
    const VlanVrfAssign &rhs_vassign = static_cast<const VlanVrfAssign &>(rhs);
    return (vlan_tag_ < rhs_vassign.vlan_tag_);
}

DBEntryBase::KeyPtr VlanVrfAssign::GetDBRequestKey() const {
    VrfAssignKey *key = new VrfAssignKey();
    key->VlanInit(interface_->GetUuid(), vlan_tag_);
    return DBEntryBase::KeyPtr(key);
}

void VlanVrfAssign::SetKey(const DBRequestKey *k) {
    const VrfAssignKey *key = static_cast<const VrfAssignKey *>(k);
    type_ = key->type_;
    interface_ = VrfAssignTable::GetInstance()->FindInterface(key->intf_uuid_);
    vlan_tag_ = key->vlan_tag_;
}

bool VlanVrfAssign::DBEntrySandesh(Sandesh *sresp, std::string &name) const {
    VrfAssignResp *resp = static_cast<VrfAssignResp *> (sresp);

    if (!name.empty() && interface_->name() != name) {
        return false;
    }

    VrfAssignSandeshData data;
    assert(type_ == VrfAssign::VLAN);

    data.set_vlan_tag(vlan_tag_);
    data.set_itf(interface_->name());
    if (vrf_.get()) {
        data.set_vrf(vrf_->GetName());
    } else {
        data.set_vrf("--ERROR--");
    }
    std::vector<VrfAssignSandeshData> &list =
            const_cast<std::vector<VrfAssignSandeshData>&>
            (resp->get_vrf_assign_list());
    list.push_back(data);
    return true;
}

void VrfAssignReq::HandleRequest() const {
    AgentVrfAssignSandesh *sand =
        new AgentVrfAssignSandesh(context(), get_uuid());
    sand->DoSandesh();
}
