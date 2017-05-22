/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#include <algorithm>
#include <boost/uuid/uuid_io.hpp>
#include <base/parse_object.h>
#include <ifmap/ifmap_link.h>
#include <ifmap/ifmap_table.h>
#include <vnc_cfg_types.h>

#include <cmn/agent_cmn.h>
#include <cfg/cfg_init.h>
#include <oper/tag.h>

#include <oper/agent_sandesh.h>
#include <oper/interface_common.h>
#include <oper/config_manager.h>
#include <filter/policy_set.h>

using namespace autogen;
using namespace std;

TagTable *TagTable::tag_table_;

const std::map<uint32_t, std::string>
TagTable::TagTypeStr = boost::assign::map_list_of
        ((uint32_t)LABEL, "label")
        ((uint32_t)APPLICATION, "application")
        ((uint32_t)TIER, "tier")
        ((uint32_t)DEPLOYMENT, "deployment")
        ((uint32_t)SITE, "site");

bool TagEntry::IsLess(const DBEntry &rhs) const {
    const TagEntry &a = static_cast<const TagEntry &>(rhs);
    return (tag_uuid_ < a.tag_uuid_);
}

string TagEntry::ToString() const {
    std::stringstream uuidstring;
    uuidstring << tag_uuid_;
    return uuidstring.str();
}

DBEntryBase::KeyPtr TagEntry::GetDBRequestKey() const {
    TagKey *key = new TagKey(tag_uuid_);
    return DBEntryBase::KeyPtr(key);
}

void TagEntry::SetKey(const DBRequestKey *key) {
    const TagKey *k = static_cast<const TagKey *>(key);
    tag_uuid_ = k->tag_uuid_;
}

std::auto_ptr<DBEntry> TagTable::AllocEntry(const DBRequestKey *k) const {
    const TagKey *key = static_cast<const TagKey *>(k);
    TagEntry *tag = new TagEntry(key->tag_uuid_);
    return std::auto_ptr<DBEntry>(static_cast<DBEntry *>(tag));
}

DBEntry *TagTable::OperDBAdd(const DBRequest *req) {
    TagKey *key = static_cast<TagKey *>(req->key.get());
    TagEntry *tag = new TagEntry(key->tag_uuid_);
    ChangeHandler(tag, req);
    return tag;
}

bool TagTable::OperDBOnChange(DBEntry *entry, const DBRequest *req) {
    bool ret = ChangeHandler(entry, req);
    return ret;
}

bool TagTable::ChangeHandler(DBEntry *entry, const DBRequest *req) {
    bool ret = false;
    bool insert = false;
    TagEntry *tag = static_cast<TagEntry *>(entry);
    TagData *data = static_cast<TagData *>(req->data.get());
    uint32_t old_tag_id;

    if (tag->tag_id_ != data->tag_id_) {
        old_tag_id = tag->tag_id_;
        tag->tag_id_ = data->tag_id_;
        insert = true;
        ret = true;
    }

    TagEntry::PolicySetList new_policy_list;
    TagData::PolicySetList::const_iterator it = data->policy_set_list_.begin();
    for(; it != data->policy_set_list_.end(); it++) {
        PolicySetKey key(*it);
        PolicySet *aps = static_cast<PolicySet *>(
                agent()->policy_set_table()->FindActiveEntry(&key));
        if (aps) {
            new_policy_list.push_back(aps);
        }
    }

    if (tag->policy_set_list_ != new_policy_list) {
        tag->policy_set_list_ = new_policy_list;
        ret = true;
    }

    if (tag->name_ != data->name_) {
        tag->name_ = data->name_;
        insert = true;
        ret = true;
    }

    if (tag->tag_type_ != data->tag_type_) {
        tag->tag_type_ = data->tag_type_;
        ret = true;
    }

    if (tag->tag_value_ != data->tag_value_) {
        tag->tag_value_ = data->tag_value_;
        ret = true;
    }

    if (insert) {
        id_name_map_.erase(old_tag_id);
        id_name_map_.insert(TagIdNamePair(tag->tag_id_, tag->name_));
    }

    return ret;
}

bool TagTable::OperDBDelete(DBEntry *entry, const DBRequest *req) {
    TagEntry *tag = static_cast<TagEntry *>(entry);
    id_name_map_.erase(tag->tag_id_);
    return true;
}

const std::string& TagTable::GetTypeStr(uint32_t type) {
    std::map<uint32_t, std::string>::const_iterator it =
        TagTypeStr.find(type);
    if (it != TagTypeStr.end()) {
        return it->second;
    }
    return Agent::NullString();
}

uint32_t TagTable::GetTypeVal(const std::string &name) {
    std::map<uint32_t, std::string>::const_iterator it =
        TagTypeStr.begin();
    for (;it != TagTypeStr.end(); it++) {
        if (it->second == name) {
            return it->first;
        }
    }

    return INVALID;
}

DBTableBase *TagTable::CreateTable(DB *db, const std::string &name) {
    tag_table_ = new TagTable(db, name);
    tag_table_->Init();
    return tag_table_;
};

bool TagTable::IFNodeToUuid(IFMapNode *node, boost::uuids::uuid &u) {
    Tag *cfg = static_cast<Tag *>(node->GetObject());
    assert(cfg);
    autogen::IdPermsType id_perms = cfg->id_perms();
    CfgUuidSet(id_perms.uuid.uuid_mslong, id_perms.uuid.uuid_lslong, u);
    return true;
}

bool TagTable::IFNodeToReq(IFMapNode *node, DBRequest &req,
        const boost::uuids::uuid &u) {
    Tag *cfg = static_cast<Tag *>(node->GetObject());
    assert(cfg);

    assert(!u.is_nil());

    if ((req.oper == DBRequest::DB_ENTRY_DELETE) || node->IsDeleted()) {
        req.oper = DBRequest::DB_ENTRY_DELETE;
        req.key.reset(new TagKey(u));
        agent()->tag_table()->Enqueue(&req);
        return false;
    }

    agent()->config_manager()->AddTagNode(node);
    return false;
}

//Check if application-policy-set is defined at global level
//if yes this takes priority over projects defined at local level
bool TagTable::IsGlobalAps(Agent *agent, IFMapNode *node) {
    //Check if this node has link to policy-management
    //If yes consider this to be global aps.
    IFMapAgentTable *aps_table = static_cast<IFMapAgentTable *>(node->table());
    DBGraph *graph = aps_table->GetGraph();

    for (DBGraphVertex::adjacency_iterator iter = node->begin(graph);
            iter != node->end(graph); ++iter) {
        IFMapNode *policy_management_node =
            static_cast<IFMapNode *>(iter.operator->());
        if (agent->config_manager()->SkipNode(policy_management_node,
                    agent->cfg()->cfg_policy_management_table())) {
            continue;
        }

        return true;
    }

    return false;
}

TagData* TagTable::BuildData(Agent *agent, IFMapNode *node) {
    Tag *cfg = static_cast<Tag *>(node->GetObject());
    //Parse thru all the application policy set attached to a tag
    //If there are mutliple application policy set attached to a tag
    //First priority would be given to APS defined at global level
    //and the APS defined at project level
    TagData::PolicySetList prj_policy_set;
    TagData::PolicySetList global_policy_set;
    uuid aps_uuid(nil_uuid());
    IFMapAgentTable *table = static_cast<IFMapAgentTable *>(node->table());
    for (DBGraphVertex::adjacency_iterator iter =
         node->begin(table->GetGraph());
         iter != node->end(table->GetGraph()); ++iter) {
        IFMapNode *adj_node = static_cast<IFMapNode *>(iter.operator->());
        if (agent->config_manager()->SkipNode(adj_node,
                    agent->cfg()->cfg_policy_set_table())) {
            continue;
        }

        ApplicationPolicySet *aps =
            static_cast<ApplicationPolicySet *>(adj_node->GetObject());
        autogen::IdPermsType id_perms = aps->id_perms();
        CfgUuidSet(id_perms.uuid.uuid_mslong, id_perms.uuid.uuid_lslong,
                   aps_uuid);

        if (IsGlobalAps(agent, adj_node)) {
            global_policy_set.push_back(aps_uuid);
        } else {
            prj_policy_set.push_back(aps_uuid);
        }
    }

    TagData *data = new TagData(agent, node, cfg->id());
    data->policy_set_list_ = global_policy_set;
    data->policy_set_list_.insert(data->policy_set_list_.end(),
                                  prj_policy_set.begin(), prj_policy_set.end());
    data->name_ = node->name();
    data->tag_type_ = GetTypeVal(cfg->type());
    data->tag_value_ = cfg->value();
    return data;
}

bool TagTable::ProcessConfig(IFMapNode *node, DBRequest &req,
                             const boost::uuids::uuid &u) {

    if (node->IsDeleted())
        return false;

    Tag *cfg = static_cast<Tag *>(node->GetObject());
    assert(cfg);

    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    TagKey *key = new TagKey(u);
    req.key.reset(key);
    req.data.reset(BuildData(agent(), node));
    return true;
}

bool TagEntry::DBEntrySandesh(Sandesh *sresp, std::string &name)  const {
    TagSandeshData data;
    data.set_uuid(UuidToString(tag_uuid_));
    data.set_name(name_);
    data.set_id(tag_id_);

    PolicySetList::const_iterator aps_it = policy_set_list_.begin();
    std::vector<ApplicationPolicySetLink> aps_uuid_list;
    for(; aps_it != policy_set_list_.end(); aps_it++) {
        std::string aps_id =
            UuidToString(aps_it->get()->uuid());
        ApplicationPolicySetLink apl;
        apl.set_application_policy_set(aps_id);
        aps_uuid_list.push_back(apl);
    }
    data.set_application_policy_set_list(aps_uuid_list);
    TagSandeshResp *resp =
        static_cast<TagSandeshResp *>(sresp);
    std::vector<TagSandeshData> &list =
        const_cast<std::vector<TagSandeshData>&>(resp->get_tag_list());
    list.push_back(data);
    return true;
}

bool TagEntry::IsApplicationTag() const {
    if (tag_type_ == TagTable::APPLICATION) {
        return true;
    }

    return false;
}

AgentSandeshPtr TagTable::GetAgentSandesh(const AgentSandeshArguments *args,
                                          const std::string &context) {
    return AgentSandeshPtr(new TagSandesh(context, args->GetString("uuid"),
                                          args->GetString("name")));
}

void TagSandeshReq::HandleRequest() const {
    AgentSandeshPtr sand(new TagSandesh(context(), get_uuid(), get_name()));
    sand->DoSandesh(sand);
}
