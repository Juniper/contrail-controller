/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/uuid/uuid_io.hpp>
#include <vnc_cfg_types.h>
#include <cmn/agent_cmn.h>

#include <ifmap/ifmap_node.h>
#include <oper/interface_common.h>
#include <oper/vm.h>
#include <oper/mirror_table.h>
#include <oper/agent_sandesh.h>
#include <cfg/cfg_listener.h>

using namespace std;
using namespace autogen;

VmTable *VmTable::vm_table_;

bool VmEntry::IsLess(const DBEntry &rhs) const {
    const VmEntry &a = static_cast<const VmEntry &>(rhs);
    return (uuid_ < a.uuid_);
}

string VmEntry::ToString() const {
    return UuidToString(GetUuid());
}

DBEntryBase::KeyPtr VmEntry::GetDBRequestKey() const {
    VmKey *key = new VmKey(uuid_);
    return DBEntryBase::KeyPtr(key);
}

void VmEntry::SetKey(const DBRequestKey *key) { 
    const VmKey *k = static_cast<const VmKey *>(key);
    uuid_ = k->uuid_;
}

bool VmEntry::DBEntrySandesh(Sandesh *sresp, std::string &name) const {
    VmListResp *resp = static_cast<VmListResp *>(sresp);

    std::string str_uuid = UuidToString(GetUuid());
    if (name.empty() || str_uuid == name) {
        VmSandeshData data;
        data.set_uuid(str_uuid);
        std::vector<VmSandeshData> &list =
                const_cast<std::vector<VmSandeshData>&>(resp->get_vm_list());
        list.push_back(data);
        return true;
    }

    return false;
}

void VmEntry::SendObjectLog(AgentLogEvent::type event) const {
    VmObjectLogInfo info;
    string str;
    string str_uuid = UuidToString(GetUuid());
    vector<string> sg_list;

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
        default:
            str.assign("");
            break;
    }
    info.set_event(str);
    info.set_uuid(str_uuid);
    if (event != AgentLogEvent::DELETE && sg_list.size()) {
        info.set_sg_uuid_list(sg_list);
    }
    info.set_ref_count(GetRefCount());
    VM_OBJECT_LOG_LOG("AgentVm", SandeshLevel::SYS_INFO, info);
}

std::auto_ptr<DBEntry> VmTable::AllocEntry(const DBRequestKey *k) const {
    const VmKey *key = static_cast<const VmKey *>(k);
    VmEntry *vm = new VmEntry(key->uuid_);
    return std::auto_ptr<DBEntry>(static_cast<DBEntry *>(vm));
}

DBEntry *VmTable::Add(const DBRequest *req) {
    VmKey *key = static_cast<VmKey *>(req->key.get());
    VmData *data = static_cast<VmData *>(req->data.get());
    VmEntry *vm = new VmEntry(key->uuid_);
    vm->SetCfgName(data->name_);
    vm->SendObjectLog(AgentLogEvent::ADD);
    return vm;
}

// Do DIFF walk for Interface and SG List.
bool VmTable::OnChange(DBEntry *entry, const DBRequest *req) {
    VmEntry *vm = static_cast<VmEntry *>(entry);
    vm->SendObjectLog(AgentLogEvent::CHANGE);
    return false;
}

bool VmTable::Delete(DBEntry *entry, const DBRequest *req) {
    VmEntry *vm = static_cast<VmEntry *>(entry);
    vm->SendObjectLog(AgentLogEvent::DELETE);
    return true;
}

DBTableBase *VmTable::CreateTable(DB *db, const std::string &name) {
    vm_table_ = new VmTable(db, name);
    vm_table_->Init();
    return vm_table_;
};
bool VmTable::IFNodeToUuid(IFMapNode *node, boost::uuids::uuid &u) {
    VirtualMachine *cfg = static_cast <VirtualMachine *> (node->GetObject());
    assert(cfg);
    autogen::IdPermsType id_perms = cfg->id_perms();
    CfgUuidSet(id_perms.uuid.uuid_mslong, id_perms.uuid.uuid_lslong, u);
    return true;
}

bool VmTable::IFNodeToReq(IFMapNode *node, DBRequest &req){
    uuid id;
    if (agent()->cfg_listener()->GetCfgDBStateUuid(node, id) == false)
        return false;

    string virtual_router_type = "none";

    VmKey *key = new VmKey(id);
    VmData *data = NULL;
    if (node->IsDeleted()) {
        req.oper = DBRequest::DB_ENTRY_DELETE;
    } else {
        req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
        VmData::SGUuidList sg_list(0);
        data = new VmData(node->name(), sg_list);
    }
    req.key.reset(key);
    req.data.reset(data);
    return true;
}

void VmListReq::HandleRequest() const {
    AgentSandeshPtr sand(new AgentVmSandesh(context(), get_uuid()));
    sand->DoSandesh(sand);
}

AgentSandeshPtr VmTable::GetAgentSandesh(const AgentSandeshArguments *args,
                                         const std::string &context) {
    return AgentSandeshPtr(new AgentVmSandesh(context,
                                              args->GetString("name")));
}
