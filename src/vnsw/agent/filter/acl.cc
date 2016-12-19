/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <vector>
#include <boost/uuid/uuid_io.hpp>

#include <base/parse_object.h>
#include <ifmap/ifmap_link.h>
#include <ifmap/ifmap_table.h>
#include <base/logging.h>
#include "net/address_util.h"

#include <cmn/agent_cmn.h>
#include <vnc_cfg_types.h>
#include <agent_types.h>

#include <cfg/cfg_init.h>

#include <filter/traffic_action.h>
#include <filter/acl_entry_match.h>
#include <filter/acl_entry_spec.h>
#include <filter/acl_entry.h>

#include <filter/acl.h>
#include <cmn/agent_cmn.h>
#include <oper/vn.h>
#include <oper/sg.h>
#include <oper/vrf.h>
#include <oper/agent_sandesh.h>
#include <oper/nexthop.h>
#include <oper/mirror_table.h>
#include <oper/qos_config.h>

static AclTable *acl_table_;

using namespace autogen;

SandeshTraceBufferPtr AclTraceBuf(SandeshTraceBufferCreate("Acl", 32000));

FlowPolicyInfo::FlowPolicyInfo(const std::string &u)
    : uuid(u), drop(false), terminal(false), other(false),
      src_match_vn(), dst_match_vn() {
}

bool AclDBEntry::IsLess(const DBEntry &rhs) const {
    const AclDBEntry &a = static_cast<const AclDBEntry &>(rhs);
    return (uuid_ < a.uuid_);
}

std::string AclDBEntry::ToString() const {
    std::string str = "ACL DB Entry:";
    str.insert(str.end(), name_.begin(), name_.end());
    return str;
}

DBEntryBase::KeyPtr AclDBEntry::GetDBRequestKey() const {
    AclKey *key = new AclKey(uuid_);
    return DBEntryBase::KeyPtr(key);
}

void AclDBEntry::SetKey(const DBRequestKey *key) { 
    const AclKey *k = static_cast<const AclKey *>(key);
    uuid_ = k->uuid_;
}

bool AclDBEntry::DBEntrySandesh(Sandesh *sresp, std::string &uuid) const {
    AclResp *resp = static_cast<AclResp *>(sresp);

    std::string str_uuid = UuidToString(GetUuid());

    // request uuid is null, then display upto size given by sandesh req
    // request uuid is not null, then disply the ACL that matches the uuid.
    if ((uuid.empty()) || (str_uuid == uuid)) {
        AclSandeshData data;
        SetAclSandeshData(data);
        std::vector<AclSandeshData> &list =
                const_cast<std::vector<AclSandeshData>&>(resp->get_acl_list());
        data.uuid = UuidToString(GetUuid());
        data.set_dynamic_acl(GetDynamicAcl());
        data.name = name_;
        list.push_back(data);
        return true;
    }
    return false;
}

void AclDBEntry::SetAclSandeshData(AclSandeshData &data) const {
    AclEntries::const_iterator iter;
    for (iter = acl_entries_.begin();
         iter != acl_entries_.end(); ++iter) {
        AclEntrySandeshData acl_entry_sdata;
        // Get ACL entry oper data and add it to the list
        iter->SetAclEntrySandeshData(acl_entry_sdata);
        data.entries.push_back(acl_entry_sdata);
    }
    return;
}

std::auto_ptr<DBEntry> AclTable::AllocEntry(const DBRequestKey *k) const {
    const AclKey *key = static_cast<const AclKey *>(k);
    AclDBEntry *acl = new AclDBEntry(key->uuid_);
    return std::auto_ptr<DBEntry>(static_cast<DBEntry *>(acl));
}

DBEntry *AclTable::OperDBAdd(const DBRequest *req) {
    AclKey *key = static_cast<AclKey *>(req->key.get());
    AclData *data = static_cast<AclData *>(req->data.get());
    AclDBEntry *acl = new AclDBEntry(key->uuid_);
    acl->SetName(data->cfg_name_);
    acl->SetDynamicAcl(data->acl_spec_.dynamic_acl);
    std::vector<AclEntrySpec>::iterator it;
    std::vector<AclEntrySpec> *acl_spec_ptr = &(data->acl_spec_.acl_entry_specs_);
    for (it = acl_spec_ptr->begin(); it != acl_spec_ptr->end();
         ++it) {
        acl->AddAclEntry(*it, acl->acl_entries_);
    }
    return acl;
}

bool AclTable::OperDBOnChange(DBEntry *entry, const DBRequest *req) {
    bool changed = false;
    AclDBEntry *acl = static_cast<AclDBEntry *>(entry);
    AclData *data = dynamic_cast<AclData *>(req->data.get());
    AclResyncQosConfigData *qos_config_data =
        dynamic_cast<AclResyncQosConfigData *>(req->data.get());

    DeleteUnresolvedEntry(acl);

    if (qos_config_data != NULL) {
        changed = acl->ResyncQosConfigEntries();
        if (acl->IsQosConfigResolved() == false) {
            AddUnresolvedEntry(acl);
        }
        return changed;
    }

    if (!data) {
        return false;
    }

    if (data->ace_id_to_del_) {
        acl->DeleteAclEntry(data->ace_id_to_del_);
        return true;
    }

    AclDBEntry::AclEntries entries;
    std::vector<AclEntrySpec>::iterator it;
    std::vector<AclEntrySpec> *acl_spec_ptr = &(data->acl_spec_.acl_entry_specs_);
    for (it = acl_spec_ptr->begin(); it != acl_spec_ptr->end();
         ++it) {
        if (!data->ace_add) { //Replace existing aces
            acl->AddAclEntry(*it, entries);
        } else { // Add to the existing entries
            if (acl->AddAclEntry(*it, acl->acl_entries_)) {
                changed = true;
            }
        }
    }

    // Replace the existing aces, ace_add is to add to the existing
    // entries
    if (!data->ace_add) {
        if (acl->Changed(entries)) {
            //Delete All acl entries for now and set newly created one.
            acl->DeleteAllAclEntries();
            acl->SetAclEntries(entries);
            changed = true;
        }
    }

    if (changed == false) {
        //Remove temporary create acl entries
        AclDBEntry::AclEntries::iterator iter;
        iter = entries.begin();
        while (iter != entries.end()) {
            AclEntry *ae = iter.operator->();
            entries.erase(iter++);
            delete ae;
        }
    }

    if (acl->IsQosConfigResolved() == false) {
        AddUnresolvedEntry(acl);
    }
    return changed;
}

bool AclTable::OperDBResync(DBEntry *entry, const DBRequest *req) {
    return OperDBOnChange(entry, req);
}

bool AclTable::OperDBDelete(DBEntry *entry, const DBRequest *req) {
    AclDBEntry *acl = static_cast<AclDBEntry *>(entry);
    ACL_TRACE(Info, "Delete " + UuidToString(acl->GetUuid()));
    DeleteUnresolvedEntry(acl);
    acl->DeleteAllAclEntries();
    return true;
}

void AclTable::ActionInit() {
    ta_map_["deny"] = TrafficAction::DENY;
    ta_map_["pass"] = TrafficAction::PASS;
    ta_map_["mirror"] = TrafficAction::MIRROR;
}

TrafficAction::Action
AclTable::ConvertActionString(std::string action_str) const {
    TrafficActionMap::const_iterator it;
    it = ta_map_.find(action_str);
    if (it != ta_map_.end()) {
        return it->second;
    } else {
        return TrafficAction::UNKNOWN;
    }
}

void AclTable::AddUnresolvedEntry(AclDBEntry *entry) {
    unresolved_acl_entries_.insert(entry);
}

void AclTable::DeleteUnresolvedEntry(AclDBEntry *entry) {
    unresolved_acl_entries_.erase(entry);
}

void AclTable::Notify(DBTablePartBase *partition,
                      DBEntryBase *e) {
    AgentQosConfig *qc = static_cast<AgentQosConfig *>(e);
    DBState *state = qc->GetState(partition->parent(), qos_config_listener_id_);

    if (qc->IsDeleted()) {
        qc->ClearState(partition->parent(), qos_config_listener_id_);
        delete state;
    }

    if (state) {
        return;
    }

    state = new DBState();
    qc->SetState(partition->parent(), qos_config_listener_id_, state);

    UnResolvedAclEntries::const_iterator it = unresolved_acl_entries_.begin();
    for (; it != unresolved_acl_entries_.end(); it++) {
        DBRequest req;
        req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;

        AclKey *key = new AclKey((*it)->GetUuid());
        key->sub_op_ = AgentKey::RESYNC;

        req.key.reset(key);
        req.data.reset(new AclResyncQosConfigData(NULL, NULL));
        Enqueue(&req);
    }
    unresolved_acl_entries_.clear();
}

void AclTable::ListenerInit() {
    qos_config_listener_id_ = agent()->qos_config_table()->Register(
                boost::bind(&AclTable::Notify, this, _1, _2));

}

DBTableBase *AclTable::CreateTable(DB *db, const std::string &name) {
    acl_table_ = new AclTable(db, name);
    acl_table_->Init();
    acl_table_->ActionInit();
    return acl_table_;
}

static void AclEntryObjectTrace(AclEntrySandeshData &ace_sandesh, AclEntrySpec &ace_spec)
{
    ace_sandesh.set_ace_id(integerToString(ace_spec.id));
    if (ace_spec.terminal) {
        ace_sandesh.set_rule_type("T");
    } else {
        ace_sandesh.set_rule_type("NT");
    }
    ace_sandesh.set_uuid(ace_spec.rule_uuid);

    std::string src;
    if (ace_spec.src_addr_type == AddressMatch::NETWORK_ID) {
        src = ace_spec.src_policy_id_str;
    } else if (ace_spec.src_addr_type == AddressMatch::IP_ADDR) {
        src = AddressMatch::BuildIpMaskList(ace_spec.src_ip_list);
    } else if (ace_spec.src_addr_type == AddressMatch::SG) {
        src = integerToString(ace_spec.src_sg_id);
    } else {
        src = "UnKnown Adresss";
    }
    ace_sandesh.set_src(src);

    std::string dst;
    if (ace_spec.dst_addr_type == AddressMatch::NETWORK_ID) {
        dst = ace_spec.dst_policy_id_str;
    } else if (ace_spec.dst_addr_type == AddressMatch::IP_ADDR) {
        dst = AddressMatch::BuildIpMaskList(ace_spec.dst_ip_list);
    } else if (ace_spec.dst_addr_type == AddressMatch::SG) {
        dst = integerToString(ace_spec.dst_sg_id);
    } else {
        dst = "UnKnown Adresss";
    }
    ace_sandesh.set_dst(dst);

    std::vector<SandeshRange> sr_l;
    std::vector<RangeSpec>::iterator it;
    for (it = ace_spec.protocol.begin(); it != ace_spec.protocol.end(); ++it) {
        SandeshRange sr;
        sr.min = (*it).min;
        sr.max = (*it).max;
        sr_l.push_back(sr);
    }
    ace_sandesh.set_proto_l(sr_l);
    sr_l.clear();

    for (it = ace_spec.src_port.begin(); it != ace_spec.src_port.end(); ++it) {
        SandeshRange sr;
        sr.min = (*it).min;
        sr.max = (*it).max;
        sr_l.push_back(sr);
    }
    ace_sandesh.set_src_port_l(sr_l);
    sr_l.clear();

    for (it = ace_spec.dst_port.begin(); it != ace_spec.dst_port.end(); ++it) {
        SandeshRange sr;
        sr.min = (*it).min;
        sr.max = (*it).max;
        sr_l.push_back(sr);
    }
    ace_sandesh.set_dst_port_l(sr_l);
    sr_l.clear();

    std::vector<ActionStr> astr_l;
    std::vector<ActionSpec>::iterator action_it;
    for (action_it = ace_spec.action_l.begin(); action_it != ace_spec.action_l.end();
         ++ action_it) {
        ActionSpec action = *action_it;
        ActionStr astr;
        if (action.ta_type == TrafficAction::SIMPLE_ACTION) {
            astr.action = TrafficAction::ActionToString(action.simple_action);
        } else if (action.ta_type == TrafficAction::LOG_ACTION) {
            astr.action = TrafficAction::kActionLogStr;
        } else if (action.ta_type == TrafficAction::ALERT_ACTION) {
            astr.action = TrafficAction::kActionAlertStr;
        } else if (action.ta_type == TrafficAction::MIRROR_ACTION) {
            astr.action = action.ma.vrf_name + " " + 
                    action.ma.analyzer_name + " " +
                    action.ma.ip.to_string() + " " +
                    integerToString(action.ma.port);
        }
        if (astr.action.size()) {
            astr_l.push_back(astr);
        }
    }
    ace_sandesh.set_action_l(astr_l);
}

static void AclObjectTrace(AgentLogEvent::type event, AclSpec &acl_spec)
{
    AclSandeshData acl;
    acl.set_uuid(UuidToString(acl_spec.acl_id));
    acl.set_dynamic_acl(acl_spec.dynamic_acl);
    if (event == AgentLogEvent::ADD || event == AgentLogEvent::CHANGE) {
        std::vector<AclEntrySpec>::iterator it;
        std::vector<AclEntrySandeshData> acl_entries;
        for (it = acl_spec.acl_entry_specs_.begin(); 
             it != acl_spec.acl_entry_specs_.end(); ++it) {
            AclEntrySandeshData ae_sandesh;
            AclEntrySpec ae_spec = *it;
            AclEntryObjectTrace(ae_sandesh, ae_spec);
            acl_entries.push_back(ae_sandesh);
        }
        acl.set_entries(acl_entries);
        ACL_TRACE(AclTrace, "Add", "", acl);
    } else if (event == AgentLogEvent::DELETE) {
        ACL_TRACE(AclTrace, "Delete", UuidToString(acl_spec.acl_id), acl);
    }
    switch (event) {
        case AgentLogEvent::ADD:
        case AgentLogEvent::CHANGE:
            break;
        case AgentLogEvent::DELETE:
            break;
        default:
            break;
    }
}

bool AclTable::IFNodeToUuid(IFMapNode *node, boost::uuids::uuid &u) {
    AccessControlList *cfg_acl = static_cast <AccessControlList *> (node->GetObject());
    autogen::IdPermsType id_perms = cfg_acl->id_perms();
    CfgUuidSet(id_perms.uuid.uuid_mslong, id_perms.uuid.uuid_lslong, u);
    return true;
}

static void AddAceToAcl(AclSpec *acl_spec, const AclTable *acl_table,
                        AccessControlList *cfg_acl,
                        const MatchConditionType *match_condition,
                        const ActionListType action_list,
                        const string rule_uuid, uint32_t id) {
    // ACE clean up
    AclEntrySpec ace_spec;
    ace_spec.id = id;

    if (ace_spec.Populate(match_condition) == false) {
        return;
    }
    // Make default as terminal rule,
    // all the dynamic acl have non-terminal rules
    if (cfg_acl->entries().dynamic) {
        ace_spec.terminal = false;
    } else {
        ace_spec.terminal = true;
    }

    ace_spec.PopulateAction(acl_table, action_list);
    ace_spec.rule_uuid = rule_uuid;
    // Add the Ace to the acl
    acl_spec->acl_entry_specs_.push_back(ace_spec);


    // Trace acl entry object
    AclEntrySandeshData ae_spec;
    AclEntryObjectTrace(ae_spec, ace_spec);
    ACL_TRACE(EntryTrace, ae_spec);
}

bool AclTable::IFNodeToReq(IFMapNode *node, DBRequest &req,
        const boost::uuids::uuid &u) {

    AccessControlList *cfg_acl = static_cast <AccessControlList *> (node->GetObject());
    assert(!u.is_nil());

    // Delete ACL
    if (req.oper == DBRequest::DB_ENTRY_DELETE) {
        AclSpec acl_spec;
        AclKey *key = new AclKey(u);
        req.key.reset(key);
        req.data.reset(NULL);
        Agent::GetInstance()->acl_table()->Enqueue(&req);
        acl_spec.acl_id = u;
        AclObjectTrace(AgentLogEvent::DELETE, acl_spec);
        return false;
    }

    // Add ACL
    const std::vector<AclRuleType> &entrs = cfg_acl->entries().acl_rule;

    AclSpec acl_spec;
    acl_spec.acl_id = u;
    acl_spec.dynamic_acl = cfg_acl->entries().dynamic;
    
    //AclEntrySpec *ace_spec;
    std::vector<AclRuleType>::const_iterator ir;
    uint32_t id = 1;
    for(ir = entrs.begin(); ir != entrs.end(); ++ir) {
        AddAceToAcl(&acl_spec, this, cfg_acl, &(ir->match_condition),
                        ir->action_list, ir->rule_uuid, id++);
        //Add reverse rule if needed
        if ((ir->direction.compare("<>") == 0)) {
            MatchConditionType rmatch_condition;
            rmatch_condition = ir->match_condition;
            rmatch_condition.src_address = ir->match_condition.dst_address;
            rmatch_condition.dst_address = ir->match_condition.src_address;
            rmatch_condition.src_port = ir->match_condition.dst_port;
            rmatch_condition.dst_port = ir->match_condition.src_port;
            AddAceToAcl(&acl_spec, this, cfg_acl, &rmatch_condition,
                        ir->action_list, ir->rule_uuid, id++);
        }
    }

    AclKey *key = new AclKey(acl_spec.acl_id);
    AclData *data = new AclData(agent(), node, acl_spec);
    data->cfg_name_ = node->name();
    req.key.reset(key);
    req.data.reset(data);
    Agent::GetInstance()->acl_table()->Enqueue(&req);
    AclObjectTrace(AgentLogEvent::ADD, acl_spec);
    return false;
}

// ACL methods
void AclDBEntry::SetAclEntries(AclEntries &entries)
{
    AclEntries::iterator it, tmp;
    it = entries.begin();
    while (it != entries.end()) {
        AclEntry *ae = it.operator->();
        tmp = it++;
        entries.erase(tmp);
        acl_entries_.insert(acl_entries_.end(), *ae);
    }
}

bool AclDBEntry::IsQosConfigResolved() {
    AclEntries::iterator it;
    it = acl_entries_.begin();
    while (it != acl_entries_.end()) {
        AclEntry *ae = it.operator->();
        if (ae->IsQosConfigResolved() == false) {
            return false;
        }
        it++;
    }
    return true;
}

bool AclDBEntry::ResyncQosConfigEntries() {
    bool ret = false;
    AclEntries::iterator it;
    it = acl_entries_.begin();
    while (it != acl_entries_.end()) {
        AclEntry *ae = it.operator->();
        if (ae->ResyncQosConfigEntries()) {
            ret = true;
        }
        it++;
    }
    return ret;
}

AclEntry *AclDBEntry::AddAclEntry(const AclEntrySpec &acl_entry_spec, AclEntries &entries)
{
    AclEntries::iterator iter;

    if (acl_entry_spec.id < 0) {
        ACL_TRACE(Err, "acl entry id is negative value");
        return NULL;
    }
    for (iter = entries.begin();
         iter != entries.end(); ++iter) {
        if (acl_entry_spec.id == iter->id()) {
            ACL_TRACE(Err, "acl entry id " + integerToString(acl_entry_spec.id) + 
                " already exists");
            return NULL;
        } else if (iter->id() > acl_entry_spec.id) {
            // Insert
            break;
        }
    }
    
    AclEntry *entry = new AclEntry();
    entry->PopulateAclEntry(acl_entry_spec);
    
    std::vector<ActionSpec>::const_iterator it;
    for (it = acl_entry_spec.action_l.begin(); it != acl_entry_spec.action_l.end();
         ++it) {
        if ((*it).ta_type == TrafficAction::MIRROR_ACTION) {
            Ip4Address sip;
            if (Agent::GetInstance()->router_id() == (*it).ma.ip) {
                sip = Ip4Address(METADATA_IP_ADDR);
            } else {
                sip = Agent::GetInstance()->router_id();
            }
            MirrorEntryKey mirr_key((*it).ma.analyzer_name);
            MirrorEntry *mirr_entry = static_cast<MirrorEntry *>
                    (Agent::GetInstance()->mirror_table()->FindActiveEntry(&mirr_key));
            assert(mirr_entry);
            // Store the mirror entry
            entry->set_mirror_entry(mirr_entry);
        }
    }
    entries.insert(iter, *entry);
    ACL_TRACE(Info, "acl entry " + integerToString(acl_entry_spec.id) + " added");
    return entry;
}

bool AclDBEntry::DeleteAclEntry(const uint32_t acl_entry_id)
{
    AclEntries::iterator iter;
    for (iter = acl_entries_.begin();
         iter != acl_entries_.end(); ++iter) {
        if (acl_entry_id == iter->id()) {
            AclEntry *ae = iter.operator->();
            acl_entries_.erase(acl_entries_.iterator_to(*iter));
            ACL_TRACE(Info, "acl entry " + integerToString(acl_entry_id) + " deleted");
            delete ae;
            return true;
        }
    }
    ACL_TRACE(Err, "acl entry " + integerToString(acl_entry_id) + " doesn't exist");
    return false;
}

void AclDBEntry::DeleteAllAclEntries()
{
    AclEntries::iterator iter;
    iter = acl_entries_.begin();
    while (iter != acl_entries_.end()) {
        AclEntry *ae = iter.operator->();
        acl_entries_.erase(iter++);
        delete ae;
    }
    return;
}

bool AclDBEntry::PacketMatch(const PacketHeader &packet_header, 
			                 MatchAclParams &m_acl, FlowPolicyInfo *info) const
{
    AclEntries::const_iterator iter;
    bool ret_val = false;
    m_acl.terminal_rule = false;
	m_acl.action_info.action = 0;
    for (iter = acl_entries_.begin();
         iter != acl_entries_.end();
         ++iter) {
        const AclEntry::ActionList &al = iter->PacketMatch(packet_header, info);
	AclEntry::ActionList::const_iterator al_it;
	for (al_it = al.begin(); al_it != al.end(); ++al_it) {
	     TrafficAction *ta = static_cast<TrafficAction *>(*al_it.operator->());
	     m_acl.action_info.action |= 1 << ta->action();
	     if (ta->action_type() == TrafficAction::MIRROR_ACTION) {
	         MirrorAction *a = static_cast<MirrorAction *>(*al_it.operator->());
                 MirrorActionSpec as;
		 as.ip = a->GetIp();
		 as.port = a->GetPort();
                 as.vrf_name = a->vrf_name();
                 as.analyzer_name = a->GetAnalyzerName();
                 as.encap = a->GetEncap();
                 m_acl.action_info.mirror_l.push_back(as);
	     }
         if (ta->action_type() == TrafficAction::VRF_TRANSLATE_ACTION) {
             const VrfTranslateAction *a =
                 static_cast<VrfTranslateAction *>(*al_it.operator->());
             VrfTranslateActionSpec vrf_translate_action(a->vrf_name(),
                                                         a->ignore_acl());
             m_acl.action_info.vrf_translate_action_ = vrf_translate_action;
         }
         if (ta->action_type() == TrafficAction::QOS_ACTION) {
             const QosConfigAction *a =
                 static_cast<const QosConfigAction *>(*al_it.operator->());
             if (a->qos_config_ref() != NULL) {
                 QosConfigActionSpec qos_action_spec(a->name());
                 if (a->qos_config_ref() &&
                     a->qos_config_ref()->IsDeleted() == false) {
                     qos_action_spec.set_id(a->qos_config_ref()->id());
                     m_acl.action_info.qos_config_action_ = qos_action_spec;
                 }
             }
         }

         if (info && ta->IsDrop()) {
             if (!info->drop) {
                 info->drop = true;
                 info->terminal = false;
                 info->other = false;
                 info->uuid = iter->uuid();
             }
         }
	}
        if (!(al.empty())) {
            ret_val = true;
            m_acl.ace_id_list.push_back((int32_t)(iter->id()));
            if (iter->IsTerminal()) {
                m_acl.terminal_rule = true;
                /* Set uuid only if it is NOT already set as
                 * drop/terminal uuid */
                if (info && !info->drop && !info->terminal) {
                    info->terminal = true;
                    info->other = false;
                    info->uuid = iter->uuid();
                }
                return ret_val;
            }
            /* If the ace action is not drop and if ace is not terminal rule
             * then set the uuid with the first matching uuid */
            if (info && !info->drop && !info->terminal && !info->other) {
                info->other = true;
                info->uuid = iter->uuid();
            }
        }
    }
    return ret_val;
}

const AclEntry*
AclDBEntry::GetAclEntryAtIndex(uint32_t index) const {
    uint32_t i = 0;
    AclEntries::const_iterator it = acl_entries_.begin();
    while (it != acl_entries_.end()) {
        if (i == index) {
            return it.operator->();
        }
        it++;
        i++;
    }

    return NULL;
}

bool AclDBEntry::Changed(const AclEntries &new_entries) const {
    AclEntries::const_iterator it = acl_entries_.begin();
    AclEntries::const_iterator new_entries_it = new_entries.begin();
    while (it != acl_entries_.end() &&
           new_entries_it != new_entries.end()) {
        if (*it == *new_entries_it) {
            it++;
            new_entries_it++;
            continue;
        }
        return true;
    }
    if (it == acl_entries_.end() &&
        new_entries_it == new_entries.end()) {
        return false;
    }
    return true;
}

const AclDBEntry* AclTable::GetAclDBEntry(const string acl_uuid_str, 
                                          const string ctx, 
                                          SandeshResponse *resp) {
    if (acl_uuid_str.empty()) {
        return NULL;
    }

    // Get acl entry from acl uuid string
    AclTable *table = Agent::GetInstance()->acl_table();
    boost::uuids::uuid acl_id = StringToUuid(acl_uuid_str);
    AclKey key(acl_id);
    AclDBEntry *acl_entry = static_cast<AclDBEntry *>(table->FindActiveEntry(&key));

    return acl_entry;
}

bool AclDBEntry::IsRulePresent(const string &rule_uuid) const {
    AclDBEntry::AclEntries::const_iterator it = acl_entries_.begin();
    while (it != acl_entries_.end()) {
        const AclEntry *ae = it.operator->();
        if (ae->uuid() == rule_uuid) {
            return true;
        }
        ++it;
    }
    return false;
}

void AclTable::AclFlowResponse(const string acl_uuid_str, const string ctx,
                               const int last_count) {
    AclFlowResp *resp = new AclFlowResp();
    const AclDBEntry *acl_entry = AclTable::GetAclDBEntry(acl_uuid_str, ctx, resp);

    if (acl_entry) {
        AclTable *table = Agent::GetInstance()->acl_table();
        if (!table->flow_acl_sandesh_data_cb_.empty()) {
            table->flow_acl_sandesh_data_cb_(acl_entry, *resp, last_count);
        }
    }

    resp->set_context(ctx);
    resp->Response();
}

void AclTable::AclFlowCountResponse(const string acl_uuid_str, 
                                    const string ctx, int ace_id) {
    AclFlowCountResp *resp = new AclFlowCountResp();
    const AclDBEntry *acl_entry = AclTable::GetAclDBEntry(acl_uuid_str, ctx, resp);

    if (acl_entry) {
        AclTable *table = Agent::GetInstance()->acl_table();
        if (!table->flow_ace_sandesh_data_cb_.empty()) {
            table->flow_ace_sandesh_data_cb_(acl_entry, *resp, ace_id);
        }
    }
    resp->set_context(ctx);
    resp->Response();
}

void AclReq::HandleRequest() const {
    AgentSandeshPtr sand(new AgentAclSandesh(context(), get_uuid()));
    sand->DoSandesh(sand);
}

AgentSandeshPtr AclTable::GetAgentSandesh(const AgentSandeshArguments *args,
                                          const std::string &context) {
    return AgentSandeshPtr(new AgentAclSandesh(context,
                                               args->GetString("name")));
}

void NextAclFlowReq::HandleRequest() const {
    string key = get_iteration_key();
    int last_count = 0;
    size_t n = std::count(key.begin(), key.end(), ':');
    if (n != 1) {
        AclFlowCountResp *resp = new AclFlowCountResp();
        resp->set_context(context());
        resp->Response();
    }
    stringstream ss(key);
    string item, uuid;
    if (getline(ss, item, ':')) {
        uuid = item;
    }
    if (getline(ss, item, ':')) {
        istringstream(item) >> last_count;
    }

    AclTable::AclFlowResponse(uuid, context(), last_count);
}

void AclFlowReq::HandleRequest() const {
    AclTable::AclFlowResponse(get_uuid(), context(), 0); 
}

void AclFlowCountReq::HandleRequest() const { 
    AclTable::AclFlowCountResponse(get_uuid(), context(), 0);
}

void NextAclFlowCountReq::HandleRequest() const { 
    string key = get_iteration_key();
    size_t n = std::count(key.begin(), key.end(), ':');
    if (n != 1) {
        AclFlowCountResp *resp = new AclFlowCountResp();
        resp->set_context(context());
        resp->Response();
    }
    stringstream ss(key);
    string uuid_str, item;
    int ace_id = 0;
    if (getline(ss, item, ':')) {
        uuid_str = item;
    }
    if (getline(ss, item, ':')) {
        istringstream(item) >> ace_id;
    }

    AclTable::AclFlowCountResponse(uuid_str, context(), ace_id);
}

void AclEntrySpec::BuildAddressInfo(const std::string &prefix, int plen,
                                    std::vector<AclAddressInfo> *list) {
    AclAddressInfo info;
    boost::system::error_code ec;
    info.ip_addr = IpAddress::from_string(prefix.c_str(), ec);
    if (ec.value() != 0) {
        ACL_TRACE(Err, "Invalid source ip prefix " + prefix);
        return;
    }
    info.ip_plen = plen;
    if (info.ip_addr.is_v4()) {
        info.ip_mask = PrefixToIpNetmask(plen);
        info.ip_addr = Address::GetIp4SubnetAddress(info.ip_addr.to_v4(), plen);
    } else{
        info.ip_mask = PrefixToIp6Netmask(plen);
        info.ip_addr = Address::GetIp6SubnetAddress(info.ip_addr.to_v6(), plen);
    }
    list->push_back(info);
}

bool AclEntrySpec::Populate(const MatchConditionType *match_condition) {
    RangeSpec rs;
    if (match_condition->protocol.compare("any") == 0) {
        rs.min = 0x0;
        rs.max = 0xff;
    } else {
        std::stringstream ss;
        ss<<match_condition->protocol;
        ss>>rs.min;
        ss.clear();
        rs.max = rs.min;
    }
    protocol.push_back(rs);

    // check for not icmp/icmpv6
    if ((match_condition->protocol.compare("1") != 0) &&
        (match_condition->protocol.compare("58") != 0)) {
        //src port
        PortType sp;
        sp = match_condition->src_port;
        rs.min = sp.start_port;
        rs.max = sp.end_port;
        if ((rs.min == (uint16_t)-1) && (rs.max == (uint16_t)-1)) {
            rs.min = 0;
        }
        src_port.push_back(rs);

        //dst port
        PortType dp;
        dp = match_condition->dst_port;
        rs.min = dp.start_port;
        rs.max = dp.end_port;
        if ((rs.min == (uint16_t)-1) && (rs.max == (uint16_t)-1)) {
            rs.min = 0;
        }
        dst_port.push_back(rs);
    }

    /* We need to support both subnet and subnet-list configurations being
     * present in a single ACL rule */
    const std::vector<SubnetType> &slist =
        match_condition->src_address.subnet_list;
    if (slist.size() ||
        match_condition->src_address.subnet.ip_prefix.size()) {
        src_addr_type = AddressMatch::IP_ADDR;
        //Build src_ip_list from 'subnet'
        if (match_condition->src_address.subnet.ip_prefix.size()) {
            BuildAddressInfo(match_condition->src_address.subnet.ip_prefix,
                             match_condition->src_address.subnet.ip_prefix_len,
                             &src_ip_list);
        }
        //Build src_ip_list from 'subnet-list'
        std::vector<SubnetType>::const_iterator it = slist.begin();
        while (it != slist.end()) {
            const SubnetType &subnet = *it;
            BuildAddressInfo(subnet.ip_prefix, subnet.ip_prefix_len,
                             &src_ip_list);
            ++it;
        }
    } else if (match_condition->src_address.virtual_network.size()) {
        std::string nt;
        nt = match_condition->src_address.virtual_network;
        src_addr_type = AddressMatch::NETWORK_ID;
        src_policy_id_str = nt;
    } else if (match_condition->src_address.security_group.size()) {
        std::stringstream ss;
        ss<<match_condition->src_address.security_group;
        ss>>src_sg_id;
        src_addr_type = AddressMatch::SG;
    }

    /* We need to support both subnet and subnet-list configurations being
     * present in a single ACL rule */
    const std::vector<SubnetType> &dlist =
        match_condition->dst_address.subnet_list;
    if (dlist.size() ||
        match_condition->dst_address.subnet.ip_prefix.size()) {
        dst_addr_type = AddressMatch::IP_ADDR;
        //Build src_ip_list from 'subnet'
        if (match_condition->dst_address.subnet.ip_prefix.size()) {
            BuildAddressInfo(match_condition->dst_address.subnet.ip_prefix,
                             match_condition->dst_address.subnet.ip_prefix_len,
                             &dst_ip_list);
            dst_addr_type = AddressMatch::IP_ADDR;
        }
        //Build src_ip_list from 'subnet-list'
        std::vector<SubnetType>::const_iterator it = dlist.begin();
        while (it != dlist.end()) {
            const SubnetType &subnet = *it;
            BuildAddressInfo(subnet.ip_prefix, subnet.ip_prefix_len,
                             &dst_ip_list);
            ++it;
        }
    } else if (match_condition->dst_address.virtual_network.size()) {
        std::string nt;
        nt = match_condition->dst_address.virtual_network;
        dst_addr_type = AddressMatch::NETWORK_ID;
        dst_policy_id_str = nt;
    } else if (match_condition->dst_address.security_group.size()) {
        std::stringstream ss;
        ss<<match_condition->dst_address.security_group;
        ss>>dst_sg_id;
        dst_addr_type = AddressMatch::SG;
    }
    return true;
}

void AclEntrySpec::AddMirrorEntry(Agent *agent) const {
    std::vector<ActionSpec>::const_iterator it;
    for (it = action_l.begin(); it != action_l.end(); ++it) {
        ActionSpec action = *it;
        if (action.ta_type != TrafficAction::MIRROR_ACTION) {
            continue;
        }

        IpAddress sip = agent->GetMirrorSourceIp(action.ma.ip);
         MirrorEntryData::MirrorEntryFlags mirror_flag =
             MirrorTable::DecodeMirrorFlag(action.ma.nh_mode,
                                           action.ma.juniper_header);
        if (mirror_flag == MirrorEntryData::DynamicNH_With_JuniperHdr) {
            agent->mirror_table()->AddMirrorEntry(action.ma.analyzer_name,
                action.ma.vrf_name, sip, agent->mirror_port(), action.ma.ip,
                action.ma.port);
        } else if (mirror_flag == MirrorEntryData::DynamicNH_Without_JuniperHdr) {
            // remote_vm_analyzer mac provided from the config
            agent->mirror_table()->AddMirrorEntry(action.ma.analyzer_name,
                    action.ma.vrf_name, sip, agent->mirror_port(), action.ma.ip,
                    action.ma.port, 0, mirror_flag,  action.ma.mac);
        } else if (mirror_flag == MirrorEntryData::StaticNH_Without_JuniperHdr) {
            // Vtep dst ip & Vni will be provided from the config
            agent->mirror_table()->AddMirrorEntry(action.ma.analyzer_name,
                    action.ma.vrf_name, sip, agent->mirror_port(),
                    action.ma.staticnhdata.vtep_dst_ip, action.ma.port,
                    action.ma.staticnhdata.vni, mirror_flag,
                    action.ma.staticnhdata.vtep_dst_mac);
        } else {
            LOG(ERROR, "Mirror nh mode not supported");
        }
    }
}

void AclEntrySpec::PopulateAction(const AclTable *acl_table,
                                  const ActionListType &action_list) {
    if (!action_list.simple_action.empty()) {
        ActionSpec saction;
        saction.ta_type = TrafficAction::SIMPLE_ACTION;
        saction.simple_action =
            acl_table->ConvertActionString(action_list.simple_action);
        action_l.push_back(saction);
    }

    if (action_list.log) {
        ActionSpec action(TrafficAction::LOG_ACTION);
        action_l.push_back(action);
    }

    if (action_list.alert) {
        ActionSpec action(TrafficAction::ALERT_ACTION);
        action_l.push_back(action);
    }

    if (!action_list.mirror_to.analyzer_name.empty()) {
        ActionSpec maction;
        maction.ta_type = TrafficAction::MIRROR_ACTION;
        maction.simple_action = TrafficAction::MIRROR;
        boost::system::error_code ec;
        maction.ma.vrf_name = std::string();
        maction.ma.analyzer_name = action_list.mirror_to.analyzer_name;
        maction.ma.ip =
            IpAddress::from_string(action_list.mirror_to.analyzer_ip_address, ec);
        maction.ma.juniper_header = action_list.mirror_to.juniper_header;
        maction.ma.nh_mode = action_list.mirror_to.nh_mode;
        MirrorEntryData::MirrorEntryFlags mirror_flag =
            MirrorTable::DecodeMirrorFlag (maction.ma.nh_mode,
                                           maction.ma.juniper_header);
        if (mirror_flag == MirrorEntryData::StaticNH_Without_JuniperHdr) {
            maction.ma.staticnhdata.vtep_dst_ip =
                IpAddress::from_string(
                action_list.mirror_to.static_nh_header.vtep_dst_ip_address, ec);
            maction.ma.staticnhdata.vtep_dst_mac =
               MacAddress::FromString(action_list.mirror_to.static_nh_header.vtep_dst_mac_address);
            maction.ma.staticnhdata.vni =
                action_list.mirror_to.static_nh_header.vni;
        }  else if(mirror_flag == MirrorEntryData::DynamicNH_Without_JuniperHdr) {
           maction.ma.vrf_name = action_list.mirror_to.routing_instance;
           maction.ma.mac =
               MacAddress::FromString(action_list.mirror_to.analyzer_mac_address);
        }

        if (ec.value() == 0) {
            if (action_list.mirror_to.udp_port) {
                maction.ma.port = action_list.mirror_to.udp_port;
            } else {
                // Adding default port
                maction.ma.port = ContrailPorts::AnalyzerUdpPort();
            }
            action_l.push_back(maction);
            AddMirrorEntry(acl_table->agent());
        } else {
            ACL_TRACE(Err, "Invalid analyzer ip address " +
                      action_list.mirror_to.analyzer_ip_address);
        }
    }

    if (!action_list.assign_routing_instance.empty()) {
        ActionSpec vrf_translate_spec;
        vrf_translate_spec.ta_type = TrafficAction::VRF_TRANSLATE_ACTION;
        vrf_translate_spec.simple_action = TrafficAction::VRF_TRANSLATE;
        vrf_translate_spec.vrf_translate.set_vrf_name(
            action_list.assign_routing_instance);
        vrf_translate_spec.vrf_translate.set_ignore_acl(false);
        action_l.push_back(vrf_translate_spec);
    }

    if (!action_list.qos_action.empty()) {
        ActionSpec qos_translate_spec;
        qos_translate_spec.ta_type = TrafficAction::QOS_ACTION;
        qos_translate_spec.simple_action = TrafficAction::APPLY_QOS;
        qos_translate_spec.qos_config_action.set_name(
                action_list.qos_action);
        action_l.push_back(qos_translate_spec);
    }
}

void AclTable::set_ace_flow_sandesh_data_cb(FlowAceSandeshDataFn fn) {
    flow_ace_sandesh_data_cb_ = fn;
}

void AclTable::set_acl_flow_sandesh_data_cb(FlowAclSandeshDataFn fn) {
    flow_acl_sandesh_data_cb_ = fn;
}
