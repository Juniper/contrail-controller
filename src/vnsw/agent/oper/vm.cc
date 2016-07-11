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
#include <oper/config_manager.h>

using namespace std;
using namespace autogen;

VmTable *VmTable::vm_table_;

VmEntry::VmEntry(const uuid &id) : uuid_(id), name_(""),
    drop_new_flows_(false) {
    flow_count_ = 0;
    linklocal_flow_count_ = 0;
}

VmEntry::~VmEntry() {
    assert(flow_count_ == 0);
    assert(linklocal_flow_count_ == 0);
}

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
        data.set_drop_new_flows(drop_new_flows_);
        list.push_back(data);
        return true;
    }

    return false;
}

void VmEntry::update_flow_count(int val) const {
    VmTable *vm_table = static_cast<VmTable *>(get_table());
    int max_flows = vm_table->agent()->max_vm_flows();
    int tmp = flow_count_.fetch_and_add(val);

    if (max_flows == 0) {
        // max_flows are not configured,
        // disable drop new flows and return
        SetInterfacesDropNewFlows(false);
        return;
    }

    if (val < 0) {
        assert(tmp >= val);
        if ((tmp + val) < ((max_flows * kDropNewFlowsRecoveryThreshold)/100)) {
            SetInterfacesDropNewFlows(false);
        }
    } else {
        if ((tmp + val) >= max_flows) {
            SetInterfacesDropNewFlows(true);
        }
    }
}

void VmEntry::SetInterfacesDropNewFlows(bool drop_new_flows) const {
    if (drop_new_flows_ == drop_new_flows) {
        return;
    }
    drop_new_flows_ = drop_new_flows;
    VmTable *vm_table = static_cast<VmTable *>(get_table());
    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    tbb::mutex::scoped_lock lock(back_ref_mutex_);
    std::set<IntrusiveReferrer>::const_iterator it = back_ref_set_.begin();
    for (; it != back_ref_set_.end(); it++) {
        VmInterface *vm_intf = static_cast<VmInterface *>((*it).first);
        req.key.reset(new VmInterfaceKey(AgentKey::RESYNC,
                                         vm_intf->GetUuid(), ""));
        req.data.reset(new VmInterfaceNewFlowDropData(drop_new_flows));
        vm_table->agent()->interface_table()->Enqueue(&req);
    }
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

DBEntry *VmTable::OperDBAdd(const DBRequest *req) {
    VmKey *key = static_cast<VmKey *>(req->key.get());
    VmData *data = static_cast<VmData *>(req->data.get());
    VmEntry *vm = new VmEntry(key->uuid_);
    vm->SetCfgName(data->name_);
    vm->SendObjectLog(AgentLogEvent::ADD);
    return vm;
}

// Do DIFF walk for Interface and SG List.
bool VmTable::OperDBOnChange(DBEntry *entry, const DBRequest *req) {
    VmEntry *vm = static_cast<VmEntry *>(entry);
    vm->SendObjectLog(AgentLogEvent::CHANGE);
    return false;
}

bool VmTable::OperDBDelete(DBEntry *entry, const DBRequest *req) {
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

bool VmTable::IFNodeToReq(IFMapNode *node, DBRequest &req,
        const boost::uuids::uuid &id) {

    if ((req.oper == DBRequest::DB_ENTRY_DELETE) || node->IsDeleted()) {
        req.key.reset(new VmKey(id));
        req.oper = DBRequest::DB_ENTRY_DELETE;
        return true;
    }

    agent()->config_manager()->AddVmNode(node);
    return false;
}

bool VmTable::ProcessConfig(IFMapNode *node, DBRequest &req,
        const boost::uuids::uuid &id) {

    if (node->IsDeleted()) {
        return false;
    }

    req.key.reset(new VmKey(id));
    VmData::SGUuidList sg_list(0);
    req.data.reset(new VmData(agent(), node, node->name(), sg_list));
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
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
