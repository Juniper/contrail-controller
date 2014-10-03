/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <algorithm>
#include <boost/uuid/uuid_io.hpp>
#include <base/parse_object.h>
#include <ifmap/ifmap_link.h>
#include <ifmap/ifmap_table.h>
#include <vnc_cfg_types.h>

#include <cmn/agent_cmn.h>
#include <cfg/cfg_init.h>
#include <cfg/cfg_listener.h>
#include <oper/sg.h>
#include <filter/acl.h>

#include <oper/interface_common.h>
#include <oper/mirror_table.h>
#include <oper/agent_sandesh.h>

using namespace autogen;
using namespace std;

SgTable *SgTable::sg_table_;

bool SgEntry::IsLess(const DBEntry &rhs) const {
    const SgEntry &a = static_cast<const SgEntry &>(rhs);
    return (sg_uuid_ < a.sg_uuid_);
}

string SgEntry::ToString() const {
    std::stringstream uuidstring;
    uuidstring << sg_uuid_;
    return uuidstring.str();
}

DBEntryBase::KeyPtr SgEntry::GetDBRequestKey() const {
    SgKey *key = new SgKey(sg_uuid_);
    return DBEntryBase::KeyPtr(key);
}

void SgEntry::SetKey(const DBRequestKey *key) { 
    const SgKey *k = static_cast<const SgKey *>(key);
    sg_uuid_ = k->sg_uuid_;
}

std::auto_ptr<DBEntry> SgTable::AllocEntry(const DBRequestKey *k) const {
    const SgKey *key = static_cast<const SgKey *>(k);
    SgEntry *sg = new SgEntry(key->sg_uuid_);
    return std::auto_ptr<DBEntry>(static_cast<DBEntry *>(sg));
}

DBEntry *SgTable::Add(const DBRequest *req) {
    SgKey *key = static_cast<SgKey *>(req->key.get());
    SgData *data = static_cast<SgData *>(req->data.get());
    SgEntry *sg = new SgEntry(key->sg_uuid_);
    sg->sg_id_ = data->sg_id_;
    ChangeHandler(sg, req);
    sg->SendObjectLog(AgentLogEvent::ADD);
    return sg;
}

bool SgTable::OnChange(DBEntry *entry, const DBRequest *req) {
    bool ret = ChangeHandler(entry, req);
    SgEntry *sg = static_cast<SgEntry *>(entry);
    sg->SendObjectLog(AgentLogEvent::CHANGE);
    return ret;
}

bool SgTable::ChangeHandler(DBEntry *entry, const DBRequest *req) {
    bool ret = false;
    SgEntry *sg = static_cast<SgEntry *>(entry);
    SgData *data = static_cast<SgData *>(req->data.get());
    
    AclKey key(data->egress_acl_id_);
    AclDBEntry *acl = static_cast<AclDBEntry *>(Agent::GetInstance()->acl_table()->FindActiveEntry(&key));
    if (sg->egress_acl_ != acl) {
        sg->egress_acl_ = acl;
        ret = true;
    }
    key = AclKey(data->ingress_acl_id_);
    acl = static_cast<AclDBEntry *>(Agent::GetInstance()->acl_table()->FindActiveEntry(&key));
    if (sg->ingress_acl_ != acl) {
        sg->ingress_acl_ = acl;
        ret = true;
    }
    return ret;
}

void SgTable::Delete(DBEntry *entry, const DBRequest *req) {
    SgEntry *sg = static_cast<SgEntry *>(entry);
    sg->SendObjectLog(AgentLogEvent::DELETE);
}

DBTableBase *SgTable::CreateTable(DB *db, const std::string &name) {
    sg_table_ = new SgTable(db, name);
    sg_table_->Init();
    return sg_table_;
};

bool SgTable::IFNodeToReq(IFMapNode *node, DBRequest &req) {
    SecurityGroup *cfg = static_cast<SecurityGroup *>(node->GetObject());
    assert(cfg);

    autogen::IdPermsType id_perms = cfg->id_perms();
    boost::uuids::uuid u;
    CfgUuidSet(id_perms.uuid.uuid_mslong, id_perms.uuid.uuid_lslong, u);

    SgKey *key = new SgKey(u);
    SgData *data  = NULL;
    
    if (node->IsDeleted()) {
        req.oper = DBRequest::DB_ENTRY_DELETE;
    } else {
        req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
        uint32_t sg_id;
        stringToInteger(cfg->id(), sg_id);
        if (sg_id == SgTable::kInvalidSgId) {
            OPER_TRACE(Sg, "Ignore SG id 0", UuidToString(u));
            return false;
        }
        uuid egress_acl_uuid = nil_uuid();
        uuid ingress_acl_uuid = nil_uuid();
        IFMapAgentTable *table = static_cast<IFMapAgentTable *>(node->table());
        for (DBGraphVertex::adjacency_iterator iter =
                node->begin(table->GetGraph()); 
                iter != node->end(table->GetGraph()); ++iter) {
            IFMapNode *adj_node = static_cast<IFMapNode *>(iter.operator->());
            if (Agent::GetInstance()->cfg_listener()->SkipNode(adj_node)) {
                continue;
            }

            if (adj_node->table() == Agent::GetInstance()->cfg()->cfg_acl_table()) {
                AccessControlList *acl_cfg = static_cast<AccessControlList *>
                    (adj_node->GetObject());
                assert(acl_cfg);
                autogen::IdPermsType id_perms = acl_cfg->id_perms();
                if (adj_node->name().find("egress-access-control-list") != std::string::npos) {
                    CfgUuidSet(id_perms.uuid.uuid_mslong, id_perms.uuid.uuid_lslong,
                               egress_acl_uuid);
                }
                if (adj_node->name().find("ingress-access-control-list") != std::string::npos) {
                    CfgUuidSet(id_perms.uuid.uuid_mslong, id_perms.uuid.uuid_lslong,
                               ingress_acl_uuid);
                }
            }
        }
        data = new SgData(sg_id, egress_acl_uuid, ingress_acl_uuid);
    }
    req.key.reset(key);
    req.data.reset(data);
    Agent::GetInstance()->sg_table()->Enqueue(&req);

    if (node->IsDeleted()) {
        return false;
    }

    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    IFMapAgentTable *table = static_cast<IFMapAgentTable *>(node->table());
    for (DBGraphVertex::adjacency_iterator iter =
                 node->begin(table->GetGraph()); 
         iter != node->end(table->GetGraph()); ++iter) {
        IFMapNode *adj_node = static_cast<IFMapNode *>(iter.operator->());
        if (Agent::GetInstance()->cfg_listener()->SkipNode
            (adj_node, Agent::GetInstance()->cfg()->cfg_vm_interface_table())) {
            continue;
        }
        if (adj_node->GetObject() == NULL) {
            continue;
        }
        if (Agent::GetInstance()->interface_table()->IFNodeToReq(adj_node, req)) {
            Agent::GetInstance()->interface_table()->Enqueue(&req);
        }
    }
    return false;
}

bool SgEntry::DBEntrySandesh(Sandesh *sresp, std::string &name)  const {
    SgListResp *resp = static_cast<SgListResp *>(sresp);
    std::string str_uuid = UuidToString(GetSgUuid());
    if (name.empty() ||
        (str_uuid == name) ||
        (integerToString(GetSgId()) == name)) {
        SgSandeshData data;
        data.set_ref_count(GetRefCount());
        data.set_sg_uuid(str_uuid);
        data.set_sg_id(GetSgId());
        if (GetEgressAcl()) {
            data.set_egress_acl_uuid(UuidToString(GetEgressAcl()->GetUuid()));
        }
        if (GetIngressAcl()) {
            data.set_ingress_acl_uuid(UuidToString(GetIngressAcl()->GetUuid()));
        }
        std::vector<SgSandeshData> &list =
                const_cast<std::vector<SgSandeshData>&>(resp->get_sg_list());
        list.push_back(data);
        return true;
    }
    return false;
}

void SgEntry::SendObjectLog(AgentLogEvent::type event) const {
    SgObjectLogInfo info;

    string str;
    switch(event) {
        case AgentLogEvent::ADD:
            str.assign("Addition");
            break;
        case AgentLogEvent::DELETE:
            str.assign("Deletion");
            break;
        case AgentLogEvent::CHANGE:
            str.assign("Modification");
            break;
        default:
            str.assign("");
            break;
    }
    info.set_event(str);

    string sg_uuid = UuidToString(GetSgUuid());
    info.set_uuid(sg_uuid);
    info.set_id(GetSgId());
    if (GetEgressAcl()) {
        info.set_egress_acl_uuid(UuidToString(GetEgressAcl()->GetUuid()));
    }
    if (GetIngressAcl()) {
        info.set_ingress_acl_uuid(UuidToString(GetIngressAcl()->GetUuid()));
    }
    info.set_ref_count(GetRefCount());
    SG_OBJECT_LOG_LOG("AgentSg", SandeshLevel::SYS_INFO, info);
}

void SgListReq::HandleRequest() const {
    AgentSgSandesh *sand =
            new AgentSgSandesh(context(), get_name());
    sand->DoSandesh();
}
