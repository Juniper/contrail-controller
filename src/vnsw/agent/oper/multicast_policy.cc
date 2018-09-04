/*
 * Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
 */

#include <algorithm>
#include <boost/uuid/uuid_io.hpp>
#include <base/parse_object.h>
#include <ifmap/ifmap_link.h>
#include <ifmap/ifmap_table.h>
#include <vnc_cfg_types.h>

#include <cmn/agent_cmn.h>
#include <cfg/cfg_init.h>
#include <oper/config_manager.h>
#include <oper/multicast_policy.h>

#include <oper/agent_sandesh.h>

using namespace autogen;
using namespace std;

SandeshTraceBufferPtr
    MulticastPolicyTraceBuf(SandeshTraceBufferCreate("MulticastPolicy", 5000));

MulticastPolicyTable *MulticastPolicyTable::mp_table_;

bool MulticastPolicyEntry::IsLess(const DBEntry &rhs) const {
    const MulticastPolicyEntry &a =
                            static_cast<const MulticastPolicyEntry &>(rhs);
    return (mp_uuid_ < a.mp_uuid_);
}

string MulticastPolicyEntry::ToString() const {
    std::stringstream uuidstring;
    uuidstring << mp_uuid_;
    return uuidstring.str();
}

DBEntryBase::KeyPtr MulticastPolicyEntry::GetDBRequestKey() const {
    MulticastPolicyKey *key = new MulticastPolicyKey(mp_uuid_);
    return DBEntryBase::KeyPtr(key);
}

void MulticastPolicyEntry::SetKey(const DBRequestKey *key) {
    const MulticastPolicyKey *k = static_cast<const MulticastPolicyKey *>(key);
    mp_uuid_ = k->mp_uuid_;
}

SourceGroupInfo::Action MulticastPolicyEntry::GetAction(IpAddress source,
                                    IpAddress group) const {

    std::vector<SourceGroupInfo>::const_iterator it = mcast_sg_.begin();
    while (it != mcast_sg_.end()) {
        if ((it->source == source) && (it->group == group)) {
            return it->action;
        }
        it++;
    }

    return SourceGroupInfo::ACTION_DENY;
}

std::auto_ptr<DBEntry> MulticastPolicyTable::AllocEntry(const DBRequestKey *k)
                                    const {

    const MulticastPolicyKey *key = static_cast<const MulticastPolicyKey *>(k);
    MulticastPolicyEntry *mp = new MulticastPolicyEntry(key->mp_uuid_);
    return std::auto_ptr<DBEntry>(static_cast<DBEntry *>(mp));
}

DBEntry *MulticastPolicyTable::OperDBAdd(const DBRequest *req) {
    MulticastPolicyKey *key = static_cast<MulticastPolicyKey *>(req->key.get());
    MulticastPolicyEntry *mp = new MulticastPolicyEntry(key->mp_uuid_);
    ChangeHandler(mp, req);
    return mp;
}

bool MulticastPolicyTable::OperDBOnChange(DBEntry *entry,
                                    const DBRequest *req) {

    MulticastPolicyEntry *mp = static_cast<MulticastPolicyEntry *>(entry);
    bool ret = ChangeHandler(mp, req);
    return ret;
}

bool MulticastPolicyTable::OperDBDelete(DBEntry *entry, const DBRequest *req) {
    std::vector<SourceGroupInfo> mcast_sg;
    MulticastPolicyEntry *mp = static_cast<MulticastPolicyEntry *>(entry);
    mp->set_mcast_sg(mcast_sg);
    return true;
}

bool MulticastPolicyTable::IsEqual(
                const std::vector<SourceGroupInfo> &lhs,
                const std::vector<SourceGroupInfo> &rhs) const {

    if (lhs.size() != rhs.size()) {
        return false;
    }

    vector<SourceGroupInfo>::const_iterator lit = lhs.begin();
    vector<SourceGroupInfo>::const_iterator rit = rhs.begin();
    while (lit != lhs.end() && rit != rhs.end()) {
        if ((lit->source != rit->source) ||
            (lit->group != rit->group) ||
            (lit->action != rit->action)) {
            return false;
        }
        ++lit;
        ++rit;
    }
    return true;
}

bool MulticastPolicyTable::ChangeHandler(DBEntry *entry, const DBRequest *req) {
    bool ret = false;
    MulticastPolicyEntry *mp = static_cast<MulticastPolicyEntry *>(entry);
    MulticastPolicyData *data = static_cast<MulticastPolicyData *>(req->data.get());

    if (!IsEqual(mp->mcast_sg(), data->mcast_sg_)) {
        mp->set_mcast_sg(data->mcast_sg_);
        ret = true;
    }

    if (mp->name() != data->name_) {
        mp->set_name(data->name_);
        ret = true;
    }

    return ret;
}

DBTableBase *MulticastPolicyTable::CreateTable(DB *db,
                                    const std::string &name) {
    mp_table_ = new MulticastPolicyTable(db, name);
    mp_table_->Init();
    return mp_table_;
};

bool MulticastPolicyTable::IFNodeToUuid(IFMapNode *node,
                                    boost::uuids::uuid &u) {
    MulticastPolicy *cfg = static_cast<MulticastPolicy *>(node->GetObject());
    assert(cfg);
    autogen::IdPermsType id_perms = cfg->id_perms();
    CfgUuidSet(id_perms.uuid.uuid_mslong, id_perms.uuid.uuid_lslong, u);
    return true;
}

bool MulticastPolicyTable::IFNodeToReq(IFMapNode *node, DBRequest &req,
                                    const boost::uuids::uuid &u) {
    MulticastPolicy *cfg = static_cast<MulticastPolicy *>(node->GetObject());
    assert(cfg);

    assert(!u.is_nil());

    if ((req.oper == DBRequest::DB_ENTRY_DELETE) || node->IsDeleted()) {
        req.oper = DBRequest::DB_ENTRY_DELETE;
        req.key.reset(new MulticastPolicyKey(u));
        agent()->mp_table()->Enqueue(&req);
        return false;
    }

    agent()->config_manager()->AddMulticastPolicyNode(node);
    return false;
}

bool MulticastPolicyTable::ProcessConfig(IFMapNode *node, DBRequest &req,
                                    const boost::uuids::uuid &u) {

    if (node->IsDeleted())
        return false;

    MulticastPolicy *cfg = static_cast<MulticastPolicy *>(node->GetObject());
    assert(cfg);

    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;

    autogen::MulticastPolicy *data =
                    static_cast<autogen::MulticastPolicy *>(node->GetObject());

    autogen::IdPermsType id_perms = data->id_perms();

    std::vector<autogen::MulticastSourceGroup> cfg_sg_list =
                                    data->multicast_source_groups();
    std::vector<autogen::MulticastSourceGroup>::const_iterator it =
                                    cfg_sg_list.begin();

    std::vector<SourceGroupInfo> sg_list;
    SourceGroupInfo sg;
    boost::system::error_code ec;
    while (it != cfg_sg_list.end()) {
        sg.source = IpAddress::from_string(it->source_address, ec);
        sg.group = IpAddress::from_string(it->group_address, ec);
        sg.action = (it->action == "pass") ?
                            SourceGroupInfo::ACTION_PASS :
                            SourceGroupInfo::ACTION_DENY;
        sg_list.push_back(sg);
        it++;
    }

    MulticastPolicyKey *mp_key = new MulticastPolicyKey(u);
    MulticastPolicyData *mp_data =
                new MulticastPolicyData(agent(), node, node->name(), sg_list);

    req.key.reset(mp_key);
    req.data.reset(mp_data);
    agent()->mp_table()->Enqueue(&req);
    return false;
}

bool MulticastPolicyEntry::DBEntrySandesh(Sandesh *sresp,
                                    std::string &name) const {

    MulticastPolicyResp *resp = static_cast<MulticastPolicyResp *>(sresp);
    std::string str_uuid = UuidToString(GetMpUuid());

    MulticastPolicySandeshData data;
    std::vector<MulticastPolicyEntrySandeshData> entries;
    std::vector<SourceGroupInfo>::const_iterator it =
                mcast_sg_.begin();
    uint32_t id = 0;
    while (it != mcast_sg_.end()) {
        MulticastPolicyEntrySandeshData entry;
        entry.set_entry_id(id);
        entry.set_source_address(it->source.to_string());
        entry.set_group_address(it->group.to_string());
        entry.set_action((it->action == SourceGroupInfo::ACTION_PASS) ?
                                    "pass" : "deny");
        entries.push_back(entry);
        it++;
    }
    data.set_entries(entries);

    std::vector<MulticastPolicySandeshData> &list =
                const_cast<std::vector<MulticastPolicySandeshData>&>
                                    (resp->get_mp_list());
    list.push_back(data);

    return true;
}

void MulticastPolicyReq::HandleRequest() const {
    AgentSandeshPtr sand(new AgentMulticastPolicySandesh(context(),
                                    get_uuid()));
    sand->DoSandesh(sand);
}

AgentSandeshPtr MulticastPolicyTable::GetAgentSandesh(
                                    const AgentSandeshArguments *args,
                                    const std::string &context) {

    return AgentSandeshPtr(new AgentMulticastPolicySandesh(context,
                                    args->GetString("uuid")));
}
