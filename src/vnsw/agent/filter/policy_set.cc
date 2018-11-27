/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/uuid/uuid_io.hpp>
#include <vnc_cfg_types.h>
#include <agent_types.h>
#include <cfg/cfg_init.h>
#include <ifmap/ifmap_node.h>
#include <oper/ifmap_dependency_manager.h>
#include "base/logging.h"
#include "filter/acl.h"
#include "filter/policy_set.h"
#include "oper/config_manager.h"
#include "oper/agent_sandesh.h"

PolicySet::PolicySet(const boost::uuids::uuid &uuid):
    uuid_(uuid), global_(false) {
}

PolicySet::~PolicySet() {
    fw_policy_list_.clear();
}

bool PolicySet::IsLess(const DBEntry &db) const {
    const PolicySet &rhs = static_cast<const PolicySet&>(db);
    return uuid_ < rhs.uuid_;
}

std::string PolicySet::ToString() const {
    std::string str = "Policy set ";
    str.append(name_);
    str.append(" Uuid :");
    str.append(UuidToString(uuid_));
    return str;
}

DBEntryBase::KeyPtr PolicySet::GetDBRequestKey() const {
    PolicySetKey *key = new PolicySetKey(uuid_);
    return DBEntryBase::KeyPtr(key);
}

void PolicySet::SetKey(const DBRequestKey *k) {
    const PolicySetKey *key = static_cast<const PolicySetKey *>(k);
    uuid_ = key->uuid_;
}

bool PolicySet::Change(PolicySetTable *table, const PolicySetData *data) {
    bool ret = false;
    bool resync_intf = false;

    if (data->fw_policy_uuid_list_ != fw_policy_uuid_list_) {
        fw_policy_uuid_list_ = data->fw_policy_uuid_list_;
        ret = true;
        fw_policy_list_.clear();
        FirewallPolicyUuidList::const_iterator it = fw_policy_uuid_list_.begin();
        for(; it != fw_policy_uuid_list_.end(); it++) {
            AclKey key(it->second);
            AclDBEntry *acl = static_cast<AclDBEntry *>(table->agent()->
                                  acl_table()->FindActiveEntry(&key));
            if (acl) {
                fw_policy_list_.push_back(acl);
            }
        }
    }

    if (data->global_ != global_) {
        global_ = data->global_;
        ret = true;
    }

    if (global_) {
        if (table->global_policy_set() != this) {
            table->set_global_policy_set(this);
        }

        if (ret) {
            resync_intf = true;
        }
    } else if (global_ == false && table->global_policy_set() == this) {
        table->set_global_policy_set(NULL);
        resync_intf = true;
    }

    if (resync_intf) {
        table->agent()->cfg()->cfg_vm_interface_table()->NotifyAllEntries();
    }

    return ret;
}

bool PolicySet::DBEntrySandesh(Sandesh *sresp, std::string &name) const {
    ApplicationPolicySetResp *resp = static_cast<ApplicationPolicySetResp*>(sresp);

    ApplicationPolicySetSandeshData apsd;
    apsd.set_name(name_);
    apsd.set_uuid(UuidToString(uuid_));

    std::vector<PolicyLinkData> acl_list;
    FirewallPolicyList::const_iterator it = fw_policy_list_.begin();
    for(; it != fw_policy_list_.end(); it++) {
        PolicyLinkData pld;
        pld.set_firewall_policy(UuidToString((*it)->GetUuid()));
        acl_list.push_back(pld);
    }

    apsd.set_firewall_policy_list(acl_list);
    apsd.set_all_applications(global_);

    std::vector<ApplicationPolicySetSandeshData> &list =
        const_cast<std::vector<ApplicationPolicySetSandeshData>&>(
            resp->get_application_policy_set_list());
    list.push_back(apsd);
    return true;
}

std::auto_ptr<DBEntry>
PolicySetTable::AllocEntry(const DBRequestKey *key) const {
    const PolicySetKey *psk = static_cast<const PolicySetKey *>(key);
    PolicySet *ps = new PolicySet(psk->uuid_);
    return std::auto_ptr<DBEntry>(static_cast<DBEntry *>(ps));
}

static PolicySetKey* BuildKey(const boost::uuids::uuid &u) {
    return new PolicySetKey(u);
}

static bool BuildFirewallPolicy(
    Agent *agent, IFMapNode *node, boost::uuids::uuid &fp_uuid) {

    IFMapAgentTable *table = static_cast<IFMapAgentTable *>(node->table());
    DBGraph *graph = table->GetGraph();

    for (DBGraphVertex::adjacency_iterator iter = node->begin(graph);
         iter != node->end(table->GetGraph()); ++iter) {

        IFMapNode *adj_node = static_cast<IFMapNode *>(iter.operator->());
        if (agent->config_manager()->SkipNode(adj_node,
                    agent->cfg()->cfg_firewall_policy_table())) {
            continue;
        }

        autogen::FirewallPolicy *fp =
            static_cast<autogen::FirewallPolicy *>(adj_node->GetObject());
        autogen::IdPermsType id_perms = fp->id_perms();
        CfgUuidSet(id_perms.uuid.uuid_mslong, id_perms.uuid.uuid_lslong,
                   fp_uuid);
        return true;
    }

    return false;
}

static PolicySetData* BuildData(Agent *agent, IFMapNode *node,
                                const autogen::ApplicationPolicySet *ps) {

    IFMapAgentTable *table = static_cast<IFMapAgentTable *>(node->table());
    DBGraph *graph = table->GetGraph();
    FirewallPolicyUuidList list;
    bool global = ps->all_applications();

    for (DBGraphVertex::adjacency_iterator iter = node->begin(graph);
            iter != node->end(table->GetGraph()); ++iter) {

        IFMapNode *adj_node = static_cast<IFMapNode *>(iter.operator->());
        if (agent->config_manager()->SkipNode(adj_node,
                     agent->cfg()->cfg_policy_set_firewall_policy_table())) {
            continue;
        }

        boost::uuids::uuid fp_uuid = boost::uuids::nil_uuid();
        autogen::ApplicationPolicySetFirewallPolicy *aps_fp =
            static_cast<autogen::ApplicationPolicySetFirewallPolicy *>
            (adj_node->GetObject());

        if (aps_fp->data().sequence == Agent::NullString()) {
            continue;
        }

        if (BuildFirewallPolicy(agent, adj_node, fp_uuid) == false) {
            continue;
        }

        list.insert(FirewallPolicyPair(aps_fp->data().sequence, fp_uuid));
    }

    return new PolicySetData(agent, node, node->name(), global, list);
}

bool PolicySetTable::IFNodeToReq(IFMapNode *node, DBRequest &req,
                                 const boost::uuids::uuid &u) {
    autogen::ApplicationPolicySet *ps =
        static_cast<autogen::ApplicationPolicySet *>(node->GetObject());
    assert(ps);

    assert(!u.is_nil());

    req.key.reset(BuildKey(u));
    if ((req.oper == DBRequest::DB_ENTRY_DELETE) || node->IsDeleted()) {
        req.oper = DBRequest::DB_ENTRY_DELETE;
        return true;
    }

    agent()->config_manager()->AddPolicySetNode(node);
    return false;
}

bool PolicySetTable::ProcessConfig(IFMapNode *node, DBRequest &req,
                                   const boost::uuids::uuid &u) {
    autogen::ApplicationPolicySet *ps =
        static_cast <autogen::ApplicationPolicySet *>(node->GetObject());
    assert(ps);

    req.key.reset(BuildKey(u));
    if (node->IsDeleted()) {
        return false;
    }

    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    req.data.reset(BuildData(agent(), node, ps));
    return true;
}

bool PolicySetTable::IFNodeToUuid(IFMapNode *node, boost::uuids::uuid &u) {
    autogen::ApplicationPolicySet *ps =
        static_cast<autogen::ApplicationPolicySet *>(node->GetObject());
    autogen::IdPermsType id_perms = ps->id_perms();
    CfgUuidSet(id_perms.uuid.uuid_mslong, id_perms.uuid.uuid_lslong, u);
    return true;
}

DBEntry *PolicySetTable::OperDBAdd(const DBRequest *req) {
    PolicySetKey *key = static_cast<PolicySetKey *>(req->key.get());
    PolicySetData *data = static_cast<PolicySetData *>(req->data.get());
    PolicySet *ps = new PolicySet(key->uuid_);
    ps->name_ = data->name_;
    ps->Change(this, data);
    return ps;
}

bool PolicySetTable::OperDBResync(DBEntry *entry, const DBRequest *req) {
    PolicySetData *data = static_cast<PolicySetData *>(req->data.get());
    PolicySet *ps = static_cast<PolicySet *>(entry);
    return ps->Change(this, data);
}

bool PolicySetTable::OperDBOnChange(DBEntry *entry, const DBRequest *req) {
    return OperDBResync(entry, req);
}

bool PolicySetTable::OperDBDelete(DBEntry *entry,
                                  const DBRequest *req) {
    PolicySet *ps = static_cast<PolicySet *>(entry);
    if (ps->global() && global_policy_set() == ps) {
        set_global_policy_set(NULL);
    }
    return true;
}

DBTableBase*
PolicySetTable::CreateTable(DB *db, const std::string &name) {

    PolicySetTable *table = new PolicySetTable(db, name);
    table->Init();
    return table;
}

void ApplicationPolicySetReq::HandleRequest() const {
    AgentSandeshPtr sand(new AgentPolicySetSandesh(context(), get_uuid(),
                                                   get_name()));
    sand->DoSandesh(sand);
}

AgentSandeshPtr PolicySetTable::GetAgentSandesh(const AgentSandeshArguments *args,
                                                const std::string &context) {
    return AgentSandeshPtr(new AgentPolicySetSandesh(context,
                args->GetString("name"), args->GetString("uuid")));
}
