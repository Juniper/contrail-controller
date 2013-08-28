/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/lexical_cast.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <cmn/agent_cmn.h>

#include "ifmap/ifmap_node.h"
#include <oper/interface.h>
#include <oper/vm.h>
#include <oper/mirror_table.h>

#include <vnc_cfg_types.h>
#include <cfg/interface_cfg.h>
#include <oper/agent_sandesh.h>

using namespace std;
using namespace autogen;

static VmTable *vm_table_;

bool VmEntry::IsLess(const DBEntry &rhs) const {
    const VmEntry &a = static_cast<const VmEntry &>(rhs);
    return (uuid_ < a.uuid_);
}

string VmEntry::ToString() const {
    return boost::lexical_cast<std::string>(GetUuid());
}

DBEntryBase::KeyPtr VmEntry::GetDBRequestKey() const {
    VmKey *key = new VmKey(uuid_);
    return DBEntryBase::KeyPtr(key);
}

void VmEntry::SetKey(const DBRequestKey *key) { 
    const VmKey *k = static_cast<const VmKey *>(key);
    uuid_ = k->uuid_;
}

AgentDBTable *VmEntry::DBToTable() const {
    return vm_table_;
}

bool VmEntry::DBEntrySandesh(Sandesh *sresp, std::string &name) const {
    VmListResp *resp = static_cast<VmListResp *>(sresp);

    std::string str_uuid = boost::lexical_cast<std::string>(GetUuid());
    if (str_uuid.find(name) != std::string::npos) {
        VmSandeshData data;
        data.set_uuid(boost::lexical_cast<std::string>(GetUuid()));
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
    string str_uuid = boost::lexical_cast<string>(GetUuid());
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
    return vm;
}

// Do DIFF walk for Interface and SG List.
bool VmTable::OnChange(DBEntry *entry, const DBRequest *req) {
    return false;
}

void VmTable::Delete(DBEntry *entry, const DBRequest *req) {
    return;
}

DBTableBase *VmTable::CreateTable(DB *db, const std::string &name) {
    vm_table_ = new VmTable(db, name);
    vm_table_->Init();
    return vm_table_;
};

bool VmTable::IFNodeToReq(IFMapNode *node, DBRequest &req){
    VirtualMachine *cfg = static_cast <VirtualMachine *> (node->GetObject());
    assert(cfg);
    autogen::IdPermsType id_perms = cfg->id_perms();
    boost::uuids::uuid u;
    CfgUuidSet(id_perms.uuid.uuid_mslong, id_perms.uuid.uuid_lslong, u);

    VmKey *key = new VmKey(u);
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
    AgentVmSandesh *sand = new AgentVmSandesh(context());
    sand->DoSandesh();
}
