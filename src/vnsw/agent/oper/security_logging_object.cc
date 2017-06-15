/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#include <cmn/agent.h>
#include <oper_db.h>
#include <vnc_cfg_types.h>
#include <agent_types.h>
#include <oper_db.h>
#include <oper/config_manager.h>
#include <oper/agent_sandesh.h>
#include <security_logging_object.h>
#include <oper/ifmap_dependency_manager.h>
#include <cfg/cfg_init.h>

SecurityLoggingObject::SecurityLoggingObject(const boost::uuids::uuid &uuid):
    uuid_(uuid) {
}

SecurityLoggingObject::~SecurityLoggingObject() {
}

DBEntryBase::KeyPtr SecurityLoggingObject::GetDBRequestKey() const {
    SecurityLoggingObjectKey *key = new SecurityLoggingObjectKey(uuid_);
    return DBEntryBase::KeyPtr(key);
}

void SecurityLoggingObject::SetKey(const DBRequestKey *key) {
    const SecurityLoggingObjectKey *slo_key =
        static_cast<const SecurityLoggingObjectKey *>(key);
    uuid_ = slo_key->uuid_;
}

std::string SecurityLoggingObject::ToString() const {
    std::ostringstream buffer;
    buffer << uuid_;
    buffer << "rate : " << rate_;
    return buffer.str();
}

bool SecurityLoggingObject::DBEntrySandesh(Sandesh *sresp, std::string &name)
    const {
    SLOListResp *resp = static_cast<SLOListResp *>(sresp);
    SLOSandeshData data;
    vector<SLOSandeshRule> rule_list;
    data.set_name(name_);
    data.set_uuid(to_string(uuid_));
    data.set_rate(rate_);
    vector<autogen::SecurityLoggingObjectRuleEntryType>::const_iterator it =
        rules_.begin();
    while (it != rules_.end()) {
        SLOSandeshRule rule;
        rule.set_uuid(it->rule_uuid);
        rule.set_rate(it->rate);
        rule_list.push_back(rule);
        ++it;
    }
    data.set_rules(rule_list);

    vector<SLOSandeshData> &list =
        const_cast<std::vector<SLOSandeshData>&>(resp->get_slo_list());
    list.push_back(data);

    return true;
}

bool SecurityLoggingObject::IsLess(const DBEntry &rhs) const {
    const SecurityLoggingObject &fc = static_cast<const SecurityLoggingObject &>
        (rhs);
    return (uuid_ < fc.uuid_);
}

bool SecurityLoggingObject::IsEqual
(const std::vector<autogen::SecurityLoggingObjectRuleEntryType> &lhs,
 const std::vector<autogen::SecurityLoggingObjectRuleEntryType> &rhs) const {
    if (lhs.size() != rhs.size()) {
        return false;
    }
    vector<autogen::SecurityLoggingObjectRuleEntryType>::const_iterator lit =
        lhs.begin();
    vector<autogen::SecurityLoggingObjectRuleEntryType>::const_iterator rit =
        rhs.begin();
    while (lit != lhs.end() && rit != rhs.end()) {
        if (lit->rule_uuid != rit->rule_uuid) {
            return false;
        }
        if (lit->rate != rit->rate) {
            return false;
        }
        ++lit;
        ++rit;
    }
    return true;
}

bool SecurityLoggingObject::Change(const DBRequest *req) {
    bool ret = false;
    const SecurityLoggingObjectData *data =
        static_cast<const SecurityLoggingObjectData *>(req->data.get());

    if (rate_ != data->rate_) {
        rate_ = data->rate_;
        ret = true;
    }

    if (!IsEqual(rules_, data->rules_)) {
        rules_ = data->rules_;
        ret = true;
    }

    if (name_ != data->name_) {
        name_ = data->name_;
        ret = true;
    }
    return ret;
}

/////////////////////////////////////////////////////////////////////////////
// SecurityLoggingObjectTable routines
/////////////////////////////////////////////////////////////////////////////
SecurityLoggingObjectTable::SecurityLoggingObjectTable(Agent *agent,
                                           DB *db, const std::string &name):
    AgentOperDBTable(db, name) {
    set_agent(agent);
}

SecurityLoggingObjectTable::~SecurityLoggingObjectTable() {
}

DBTableBase*
SecurityLoggingObjectTable::CreateTable(Agent *agent, DB *db,
                                        const std::string &name) {
    SecurityLoggingObjectTable *table = new SecurityLoggingObjectTable(agent,
                                                                       db,
                                                                       name);
    (static_cast<DBTable *>(table))->Init();
    return table;
}

std::auto_ptr<DBEntry>
SecurityLoggingObjectTable::AllocEntry(const DBRequestKey *k) const {
    const SecurityLoggingObjectKey *key =
        static_cast<const SecurityLoggingObjectKey *>(k);
    SecurityLoggingObject *slo = new SecurityLoggingObject(key->uuid_);
    return std::auto_ptr<DBEntry>(static_cast<DBEntry *>(slo));
}

DBEntry* SecurityLoggingObjectTable::OperDBAdd(const DBRequest *req) {
    const SecurityLoggingObjectKey *key =
        static_cast<const SecurityLoggingObjectKey *>(req->key.get());
    SecurityLoggingObject *slo = new SecurityLoggingObject(key->uuid_);
    slo->Change(req);
    return static_cast<DBEntry *>(slo);
}

bool SecurityLoggingObjectTable::OperDBOnChange(DBEntry *entry,
                                                const DBRequest *req) {
    SecurityLoggingObject *slo = static_cast<SecurityLoggingObject *>(entry);
    return slo->Change(req);
}

/*
 * Do we need resync
bool SecurityLoggingObjectTable::OperDBResync(DBEntry *entry, const DBRequest *req) {
    return OperDBOnChange(entry, req);
}
*/

bool SecurityLoggingObjectTable::OperDBDelete(DBEntry *entry,
                                              const DBRequest *req) {
    return true;
}

bool SecurityLoggingObjectTable::IFNodeToReq(IFMapNode *node, DBRequest &req,
                                             const boost::uuids::uuid &u) {
    assert(!u.is_nil());
    if ((req.oper == DBRequest::DB_ENTRY_DELETE) || node->IsDeleted()) {
        req.key.reset(new SecurityLoggingObjectKey(u));
        req.oper = DBRequest::DB_ENTRY_DELETE;
        Enqueue(&req);
        return false;
    }

    agent()->config_manager()->AddSecurityLoggingObjectNode(node);
    return false;
}

bool SecurityLoggingObjectTable::IFNodeToUuid(IFMapNode *node,
                                              boost::uuids::uuid &u) {
    autogen::SecurityLoggingObject *cfg =
        static_cast <autogen::SecurityLoggingObject *> (node->GetObject());
    assert(cfg);
    autogen::IdPermsType id_perms = cfg->id_perms();
    CfgUuidSet(id_perms.uuid.uuid_mslong, id_perms.uuid.uuid_lslong, u);
    return true;
}

bool SecurityLoggingObjectTable::ProcessConfig(IFMapNode *node, DBRequest &req,
                                               const boost::uuids::uuid &u) {
    if (node->IsDeleted()) {
        return false;
    }

    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    req.key.reset(new SecurityLoggingObjectKey(u));
    req.data.reset(BuildData(node));
    Enqueue(&req);
    return false;
}

SecurityLoggingObjectData*
SecurityLoggingObjectTable::BuildData(IFMapNode *node) {
    autogen::SecurityLoggingObject *data =
        static_cast<autogen::SecurityLoggingObject *>(node->GetObject());

    SecurityLoggingObjectData *slo_data =
        new SecurityLoggingObjectData(agent(), node, data->rules(),
                                      data->rate(), node->name());
    return slo_data;
}

void SLOListReq::HandleRequest() const {
    AgentSandeshPtr sand(new AgentSecurityLoggingObjectSandesh(context(),
                                                               get_uuid()));
    sand->DoSandesh(sand);
}

AgentSandeshPtr
SecurityLoggingObjectTable::GetAgentSandesh(const AgentSandeshArguments *args,
                                            const std::string &context) {
    return AgentSandeshPtr(new AgentSecurityLoggingObjectSandesh(context,
                           args->GetString("uuid")));
}

