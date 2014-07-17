/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/uuid/string_generator.hpp>
#include <boost/uuid/uuid_io.hpp>
#include "base/logging.h"
#include "filter/policy.h"
#include "filter/acl.h"
#include "filter/policy_config_spec.h"

void PolicyData::Init(const PolicyConfigSpec &policy_cfg) {
    vpc_id_ = policy_cfg.vpc_id;
    policy_id_ = policy_cfg.vpc_id;
    name_ = policy_cfg.name;
    inbound_ = policy_cfg.inbound;
    acl_id_ = policy_cfg.acl_id;
}

Policy::Policy(const uuid id) : policy_id_(id) 
{
    std::stringstream ss;
    ss << policy_id_;
    LOG(DEBUG, "Create Policy - " << ss.str());
    inbound_acls_.clear();
    outbound_acls_.clear();
}

Policy::~Policy()
{
    AclPtrList::iterator it;
    LOG(DEBUG, "Policy::~Policy");
    for (it = inbound_acls_.begin(); it != inbound_acls_.end(); it++) {
        (*it)->~Acl();
    }
    inbound_acls_.clear();
    for (it = outbound_acls_.begin(); it != outbound_acls_.end(); it++) {
        (*it)->~Acl();
    }
    outbound_acls_.clear();
}

void Policy::Init(const PolicyData *policy_data)
{
    vpc_id_ = policy_data->vpc_id_;
    policy_id_ = policy_data->policy_id_;
    name_ = policy_data->name_;

    std::stringstream ss;
    ss << policy_id_;
    LOG(DEBUG, "Init: " << ss.str() << " Name: " << name_);
    AclPtr a = AclPtr(new Acl(policy_data->acl_id_));
    if (policy_data->inbound_) {
        inbound_acls_.push_back(a);
    } else {
        outbound_acls_.push_back(a);
    }
}

AclPtr Policy::FindAcl(const bool inbound, const uuid acl_id)
{
    AclPtrList::iterator it;
    AclPtrList *acls;
    if (inbound) {
        acls = &inbound_acls_;
    } else {
        acls = &outbound_acls_;
    }
    for (it = acls->begin(); it < acls->end(); it++) {
        if (acl_id == (*it)->acl_id()) {
            return (*it);
        }
    }
    return NULL;
}

void Policy::InsertAcl(const bool inbound, const AclPtr acl)
{
    if (inbound) {
        inbound_acls_.push_back(acl);
    } else {
        outbound_acls_.push_back(acl);
    }
}

void Policy::DeleteAcl(const bool inbound, const uuid acl_id)
{
    AclPtrList::iterator it;
    AclPtrList *acls;
    if (inbound) {
        acls = &inbound_acls_;
    } else {
        acls = &outbound_acls_;
    }
    for (it = acls->begin(); it < acls->end(); it++) {
        if (acl_id == (*it)->acl_id()) {
            acls->erase(it);
            return;
        }
    }
}

bool Policy::PacketMatch(const PacketHeader &packet_header,
                         const bool inbound,
                         AclEntry::ActionList &sal)
{
    AclPtrList::iterator it;
    AclPtrList *acls;
    bool terminal_rule;
    bool ret_value = false;

    if (inbound) {
        acls = &inbound_acls_;
    } else {
        acls = &outbound_acls_;
    }
    for (it = acls->begin(); it < acls->end(); it++) {
        if ((*it)->PMatch(packet_header, sal, terminal_rule)) {
            ret_value = true;
            if (terminal_rule == true) {
                return ret_value;
            }
        }
    }
    return ret_value;
}

bool Policy::IsLess(const DBEntry &rhs) const {
    const Policy &a = static_cast<const Policy &>(rhs);
    return policy_id_ < a.policy_id_;
}

std::string Policy::ToString() const {
    std::string str = "Policy ";
    str.append(name_);
    return str;
}

DBEntryBase::KeyPtr Policy::GetDBRequestKey() const {
    PolicyKey *key = new PolicyKey(policy_id_);
    LOG(DEBUG, "Policy::GetDBRequestKey");
    return DBEntryBase::KeyPtr(key);
}

void Policy::SetKey(const DBRequestKey *key) { 
    const PolicyKey *k = static_cast<const PolicyKey *>(key);
    LOG(DEBUG, "Policy::SetKey");
    policy_id_ = k->id_;
}

void Policy::SetName(const std::string str) {
    name_ = str;
}

std::auto_ptr<DBEntry> PolicyTable::AllocEntry(const DBRequestKey *key) const {
    const PolicyKey *k = static_cast<const PolicyKey *>(key);
    Policy *p = new Policy(k->id_);
    return std::auto_ptr<DBEntry>(static_cast<DBEntry *>(p));
}

DBEntry *PolicyTable::Add(const DBRequest *req) {
    PolicyKey *key = static_cast<PolicyKey *>(req->key.get());
    PolicyData *data = static_cast<PolicyData *>(req->data.get());
    LOG(DEBUG, "PolicyTable::Add");
    Policy *policy = new Policy(key->id_);
    policy->Init(data);
    return policy;
}

bool PolicyTable::OnChange(DBEntry *entry, const DBRequest *req) {
    PolicyData *data = static_cast<PolicyData *>(req->data.get());
    Policy *p = static_cast<Policy *>(entry);

    LOG(DEBUG, "PolicyTable::Change");
    p->SetName(data->name_);
    if (p->FindAcl(data->inbound_, data->acl_id_) == NULL) {
        AclPtr a = AclPtr(new Acl(data->acl_id_));
        p->InsertAcl(data->inbound_, a);
    }
    return true;
}

void PolicyTable::Delete(DBEntry *entry, const DBRequest *req) {
    LOG(DEBUG, "PolicyTable::Delete");
    return;
}

DBTableBase *PolicyTable::CreateTable(DB *db, const std::string &name) {
    PolicyTable *table = new PolicyTable(db, name);
    LOG(DEBUG, "CreateTable" << name);
    table->Init();
    return table;
}

void PolicyTable::Register() {
    DB::RegisterFactory("db.policy.0", &PolicyTable::CreateTable);
}
