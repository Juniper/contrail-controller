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
    uuid_(uuid), firewall_policy_list_(), firewall_rule_list_() {
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
    buffer << "UUID : " << uuid_;
    buffer << " rate : " << rate_;
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
    data.set_status(status_);
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

    vector<SLOFwPolicyEntry> fp_list;
    SloRuleList::const_iterator fp_it = firewall_policy_list_.begin();
    while (fp_it != firewall_policy_list_.end()) {
        PolicyLinkData item;
        item.set_firewall_policy(to_string(fp_it->uuid_));
        SLOFwPolicyEntry entry;
        entry.set_uuid(item);
        entry.set_rate(fp_it->rate_);
        fp_list.push_back(entry);
        ++fp_it;
    }
    data.set_firewall_policy_list(fp_list);

    vector<SLOSandeshRule> fr_list;
    SloRuleList::const_iterator fr_it = firewall_rule_list_.begin();
    while (fr_it != firewall_rule_list_.end()) {
        SLOSandeshRule item;
        item.set_uuid(to_string(fr_it->uuid_));
        item.set_rate(fr_it->rate_);
        fr_list.push_back(item);
        ++fr_it;
    }
    data.set_firewall_rule_list(fr_list);

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

    if (status_ != data->status_) {
        status_ = data->status_;
    }

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

    if (firewall_policy_list_ != data->firewall_policy_list_) {
        firewall_policy_list_ = data->firewall_policy_list_;
        ret = true;
    }

    if (firewall_rule_list_ != data->firewall_rule_list_) {
        firewall_rule_list_ = data->firewall_rule_list_;
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
SecurityLoggingObjectTable::BuildData(IFMapNode *node) const {
    autogen::SecurityLoggingObject *data =
        static_cast<autogen::SecurityLoggingObject *>(node->GetObject());

    autogen::IdPermsType id_perms = data->id_perms();
    SecurityLoggingObjectData *slo_data =
        new SecurityLoggingObjectData(agent(), node, data->rules(),
                                      data->rate(),
                                      id_perms.enable, node->name());
    IFMapAgentTable *table = static_cast<IFMapAgentTable *>(node->table());
    for (DBGraphVertex::adjacency_iterator iter =
         node->begin(table->GetGraph());
         iter != node->end(table->GetGraph()); ++iter) {

        IFMapNode *adj_node = static_cast<IFMapNode *>(iter.operator->());
        if (agent()->config_manager()->SkipNode(adj_node)) {
            continue;
        }

        if (strcmp(adj_node->table()->Typename(),
                   "firewall-policy-security-logging-object") == 0) {
            autogen::FirewallPolicySecurityLoggingObject *fp_slo_link =
                static_cast<autogen::FirewallPolicySecurityLoggingObject *>
                (adj_node->GetObject());
            const autogen::SloRateType &slo_rate = fp_slo_link->data();
            IFMapNode *fp_node = agent()->config_manager()->
                FindAdjacentIFMapNode(adj_node, "firewall-policy");
            if (fp_node) {
                boost::uuids::uuid fp_uuid = boost::uuids::nil_uuid();
                autogen::FirewallPolicy *fp =
                    static_cast<autogen::FirewallPolicy *>(fp_node->GetObject());
                autogen::IdPermsType id_perms = fp->id_perms();
                CfgUuidSet(id_perms.uuid.uuid_mslong, id_perms.uuid.uuid_lslong,
                           fp_uuid);
                SloRuleInfo info(fp_uuid, slo_rate.rate);
                slo_data->firewall_policy_list_.push_back(info);
            }
        }

        if (strcmp(adj_node->table()->Typename(),
                   "firewall-rule-security-logging-object") == 0) {
            autogen::FirewallRuleSecurityLoggingObject *fr_slo_link =
                static_cast<autogen::FirewallRuleSecurityLoggingObject *>
                (adj_node->GetObject());
            const autogen::SloRateType &slo_rate = fr_slo_link->data();
            IFMapNode *fr_node = agent()->config_manager()->
                FindAdjacentIFMapNode(adj_node, "firewall-rule");
            if (fr_node) {
                boost::uuids::uuid fr_uuid = boost::uuids::nil_uuid();
                autogen::FirewallRule *fr =
                    static_cast<autogen::FirewallRule *>(fr_node->GetObject());
                autogen::IdPermsType id_perms = fr->id_perms();
                CfgUuidSet(id_perms.uuid.uuid_mslong, id_perms.uuid.uuid_lslong,
                           fr_uuid);
                SloRuleInfo info(fr_uuid, slo_rate.rate);
                slo_data->firewall_rule_list_.push_back(info);
            }
        }
    }
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
