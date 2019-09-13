/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <vector>
#include <boost/uuid/uuid_io.hpp>

#include <base/parse_object.h>
#include <ifmap/ifmap_link.h>
#include <ifmap/ifmap_table.h>
#include <base/logging.h>
#include <base/address_util.h>

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
#include <oper/config_manager.h>

static AclTable *acl_table_;

using namespace autogen;

SandeshTraceBufferPtr AclTraceBuf(SandeshTraceBufferCreate("Acl", 32000));

FlowPolicyInfo::FlowPolicyInfo(const std::string &u)
    : uuid(u), drop(false), terminal(false), other(false),
      src_match_vn(), dst_match_vn(), acl_name() {
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

    AclSandeshData sandesh_data;
    acl->SetAclSandeshData(sandesh_data);
    ACL_TRACE(AclTrace, "Add", UuidToString(acl->uuid_), sandesh_data);

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

    AclSandeshData sandesh_data;
    acl->SetAclSandeshData(sandesh_data);
    ACL_TRACE(AclTrace, "Changed", UuidToString(acl->uuid_), sandesh_data);
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
    uint32_t id;

    if (stringToInteger(ace_spec.id.id_, id)) {
        //XXX ci sanity expects integers
        //and since we are now comparing string
        //id are prepended with 0, and verification fails
        //To be removed once ci scripts would be corrected.
        ace_sandesh.set_ace_id(integerToString(id));
    } else {
        ace_sandesh.set_ace_id(ace_spec.id.id_);
    }
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
        } else if (action.ta_type == TrafficAction::HBS_ACTION) {
            astr.action = TrafficAction::kActionHbsStr;
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
    } else if (event == AgentLogEvent::DEL) {
        ACL_TRACE(AclTrace, "Delete", UuidToString(acl_spec.acl_id), acl);
    }
}

bool AclTable::IFNodeToUuid(IFMapNode *node, boost::uuids::uuid &u) {
    AccessControlList *cfg_acl = dynamic_cast <AccessControlList *> (node->GetObject());
    if (cfg_acl) {
        autogen::IdPermsType id_perms = cfg_acl->id_perms();
        CfgUuidSet(id_perms.uuid.uuid_mslong, id_perms.uuid.uuid_lslong, u);
    }

    FirewallPolicy *fw_acl =
        dynamic_cast <FirewallPolicy *> (node->GetObject());
    if (fw_acl) {
        autogen::IdPermsType id_perms = fw_acl->id_perms();
        CfgUuidSet(id_perms.uuid.uuid_mslong, id_perms.uuid.uuid_lslong, u);
    }

    return true;
}

static void AddAceToAcl(AclSpec *acl_spec, const AclTable *acl_table,
                        AccessControlList *cfg_acl,
                        const MatchConditionType *match_condition,
                        const ActionListType action_list,
                        const string rule_uuid, uint32_t id) {
    // ACE clean up
    AclEntrySpec ace_spec;
    std::stringstream stream;
    stream << std::setfill('0') << std::setw(8) << id;
    ace_spec.id.id_ = stream.str();

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

void AclTable::PopulateServicePort(AclEntrySpec &ace_spec, IFMapNode *node) {
    IFMapAgentTable *firewall_rule_table =
         static_cast<IFMapAgentTable *>(node->table());
     DBGraph *graph = firewall_rule_table->GetGraph();

     for (DBGraphVertex::adjacency_iterator iter = node->begin(graph);
          iter != node->end(graph); ++iter) {
         IFMapNode *service_group_node =
             static_cast<IFMapNode *>(iter.operator->());
         if (agent()->config_manager()->SkipNode(service_group_node,
                              agent()->cfg()->cfg_service_group_table())) {
             continue;
         }

         const ServiceGroup *service_group =
             static_cast<const ServiceGroup*>(service_group_node->GetObject());
         ace_spec.PopulateServiceGroup(service_group);
     }
}

IFMapNode* AclTable::GetFirewallRule(IFMapNode *node) {
    IFMapAgentTable *fp_fr_table =
        static_cast<IFMapAgentTable *>(node->table());
    DBGraph *graph = fp_fr_table->GetGraph();

    for (DBGraphVertex::adjacency_iterator iter = node->begin(graph);
            iter != node->end(graph); iter++) {
        IFMapNode *firewall_rule_node =
            static_cast<IFMapNode *>(iter.operator->());

        if (agent()->config_manager()->SkipNode(firewall_rule_node,
                    agent()->cfg()->cfg_firewall_rule_table())) {
            continue;
        }

        return firewall_rule_node;
    }

    return NULL;
}

void AclTable::AddImplicitRule(AclSpec &acl_spec, AclEntrySpec &ace_spec,
                               const FirewallRule *rule) {
    if ((rule->direction().compare("<>") == 0)) {
        AclEntrySpec implicit_forward_ace_spec(ace_spec);
        implicit_forward_ace_spec.Reverse(&ace_spec,
                                          AclEntryID::DERIVED,
                                          true, false);
        acl_spec.acl_entry_specs_.push_back(implicit_forward_ace_spec);
    }
}

void AclTable::FirewallPolicyIFNodeToReq(IFMapNode *node, DBRequest &req,
                                         const boost::uuids::uuid &u,
                                         AclSpec &acl_spec) {
     IFMapAgentTable *firewall_policy_table =
         static_cast<IFMapAgentTable *>(node->table());
     DBGraph *graph = firewall_policy_table->GetGraph();

     for (DBGraphVertex::adjacency_iterator iter = node->begin(graph);
          iter != node->end(graph); ++iter) {
          IFMapNode *fp_fr_rule_node =
              static_cast<IFMapNode *>(iter.operator->());
          if (agent()->config_manager()->SkipNode(fp_fr_rule_node,
                 agent()->cfg()->cfg_firewall_policy_firewall_rule_table())) {
               continue;
          }

          const FirewallPolicyFirewallRule *fp_fr =
              static_cast<const FirewallPolicyFirewallRule *>(
                      fp_fr_rule_node->GetObject());
          if (fp_fr->data().sequence == Agent::NullString()) {
              continue;
          }
          IFMapNode *rule = GetFirewallRule(fp_fr_rule_node);
          if (rule == NULL) {
              continue;
          }

          const FirewallRule *fw_rule =
              static_cast<const FirewallRule *>(rule->GetObject());
          AclEntrySpec ace_spec;
          ace_spec.Populate(agent(), rule, fw_rule);
          ace_spec.id.id_ = fp_fr->data().sequence;
          ace_spec.terminal = true;

          //Parse thru FW rule to service group and populate service group
          PopulateServicePort(ace_spec, rule);

          boost::uuids::uuid rule_uuid;
          autogen::IdPermsType id_perms = fw_rule->id_perms();
          CfgUuidSet(id_perms.uuid.uuid_mslong,
                  id_perms.uuid.uuid_lslong, rule_uuid);

          ace_spec.rule_uuid = UuidToString(rule_uuid);
          ace_spec.PopulateAction(this, fw_rule->action_list());

          if ((fw_rule->direction().compare("<") == 0)) {
              AclEntrySpec rev_ace_spec;
              rev_ace_spec.Reverse(&ace_spec, AclEntryID::FORWARD,
                                   true, false);
              acl_spec.acl_entry_specs_.push_back(rev_ace_spec);
          } else {
              acl_spec.acl_entry_specs_.push_back(ace_spec);
              if ((fw_rule->direction().compare("<>") == 0)) {
                  AddImplicitRule(acl_spec, ace_spec, fw_rule);
              }
          }
     }
}

bool AclTable::SubnetTypeEqual(const autogen::SubnetType &lhs,
                               const autogen::SubnetType &rhs) const {
    if (lhs.ip_prefix.compare(rhs.ip_prefix) != 0)
        return false;
    if (lhs.ip_prefix_len != rhs.ip_prefix_len)
        return false;
    return true;
}

bool AclTable::AddressTypeEqual(const autogen::AddressType &lhs,
                                const autogen::AddressType &rhs) const {
    if (!SubnetTypeEqual(lhs.subnet, rhs.subnet))
        return false;
    if (lhs.virtual_network.compare(rhs.virtual_network) != 0)
        return false;
    if (lhs.security_group.compare(rhs.security_group) != 0)
        return false;
    if (lhs.network_policy.compare(rhs.network_policy) != 0)
        return false;
    if (lhs.subnet_list.size() != rhs.subnet_list.size())
        return false;
    std::vector<SubnetType>::const_iterator lit = lhs.subnet_list.begin();
    std::vector<SubnetType>::const_iterator rit = lhs.subnet_list.begin();
    while ((lit != lhs.subnet_list.end()) &&
           (rit != rhs.subnet_list.end())) {
        if (!SubnetTypeEqual(*lit, *rit))
            return false;
        ++lit;
        ++rit;
    }
    return true;
}

bool AclTable::PortTypeEqual(const autogen::PortType &src,
                             const autogen::PortType &dst) const {
    if ((src.start_port == dst.start_port) &&
        (src.end_port == dst.end_port)) {
        return true;
    }
    return false;
}

void AclTable::AclIFNodeToReq(IFMapNode *node, DBRequest &req,
                              const boost::uuids::uuid &u,
                              AclSpec &acl_spec) {
    AccessControlList *cfg_acl =
        dynamic_cast <AccessControlList *> (node->GetObject());
    const std::vector<AclRuleType> &entrs = cfg_acl->entries().acl_rule;

    acl_spec.acl_id = u;
    acl_spec.dynamic_acl = cfg_acl->entries().dynamic;

    //AclEntrySpec *ace_spec;
    std::vector<AclRuleType>::const_iterator ir;
    uint32_t id = 1;
    for(ir = entrs.begin(); ir != entrs.end(); ++ir) {
        AddAceToAcl(&acl_spec, this, cfg_acl, &(ir->match_condition),
                        ir->action_list, ir->rule_uuid, id++);
        bool address_same = false;
        if (AddressTypeEqual(ir->match_condition.src_address,
                             ir->match_condition.dst_address)) {
            address_same = true;
        }

        bool port_same = false;
        if (PortTypeEqual(ir->match_condition.src_port,
                          ir->match_condition.dst_port)) {
            port_same = true;
        }

        //Add reverse rule if needed
        if ((ir->direction.compare("<>") == 0) &&
            (!address_same || !port_same)) {
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
}

bool AclTable::IFNodeToReq(IFMapNode *node, DBRequest &req,
                           const boost::uuids::uuid &u) {

    assert(!u.is_nil());
    // Delete ACL
    if (req.oper == DBRequest::DB_ENTRY_DELETE) {
        AclSpec acl_spec;
        AclKey *key = new AclKey(u);
        req.key.reset(key);
        req.data.reset(NULL);
        Enqueue(&req);
        acl_spec.acl_id = u;
        AclObjectTrace(AgentLogEvent::DEL, acl_spec);
        return false;
    }

    AccessControlList *cfg_acl = dynamic_cast <AccessControlList *> (node->GetObject());
    AclSpec acl_spec;
    acl_spec.acl_id = u;
    if (cfg_acl) {
        AclIFNodeToReq(node, req, u, acl_spec);
    } else {
        //Firewall Policy also has ACL table as handler
        FirewallPolicyIFNodeToReq(node, req, u, acl_spec);
    }

    AclKey *key = new AclKey(acl_spec.acl_id);
    AclData *data = new AclData(agent(), node, acl_spec);
    data->cfg_name_ = node->name();
    req.key.reset(key);
    req.data.reset(data);
    Enqueue(&req);
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

    for (iter = entries.begin();
         iter != entries.end(); ++iter) {
        if (acl_entry_spec.id == iter->id()) {
            ACL_TRACE(Err, "acl entry id " + acl_entry_spec.id.id_ +
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
    ACL_TRACE(Info, "acl entry " + integerToString(acl_entry_spec.id.id_) + " added");
    return entry;
}

bool AclDBEntry::DeleteAclEntry(const uint32_t acl_entry_id)
{
    AclEntries::iterator iter;
    for (iter = acl_entries_.begin();
         iter != acl_entries_.end(); ++iter) {
        AclEntryID ace_id(acl_entry_id);
        if (ace_id == iter->id()) {
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
                 info->acl_name = GetName();
             }
         }
    }
        if (!(al.empty())) {
            ret_val = true;
            m_acl.ace_id_list.push_back(iter->id());
            if (iter->IsTerminal()) {
                m_acl.terminal_rule = true;
                /* Set uuid only if it is NOT already set as
                 * drop/terminal uuid */
                if (info && !info->drop && !info->terminal) {
                    info->terminal = true;
                    info->other = false;
                    info->uuid = iter->uuid();
                    info->acl_name = GetName();
                }
                return ret_val;
            }
            /* If the ace action is not drop and if ace is not terminal rule
             * then set the uuid with the first matching uuid */
            if (info && !info->drop && !info->terminal && !info->other) {
                info->other = true;
                info->uuid = iter->uuid();
                info->acl_name = GetName();
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
                                    const string ctx,
                                    const string &ace_id) {
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
    std::stringstream ss(key);
    string item, uuid;
    if (getline(ss, item, ':')) {
        uuid = item;
    }
    if (getline(ss, item, ':')) {
        std::istringstream(item) >> last_count;
    }

    AclTable::AclFlowResponse(uuid, context(), last_count);
}

void AclFlowReq::HandleRequest() const {
    AclTable::AclFlowResponse(get_uuid(), context(), 0);
}

void AclFlowCountReq::HandleRequest() const {
    AclTable::AclFlowCountResponse(get_uuid(), context(), Agent::NullString());
}

void NextAclFlowCountReq::HandleRequest() const {
    string key = get_iteration_key();
    size_t n = std::count(key.begin(), key.end(), ':');
    if (n != 1) {
        AclFlowCountResp *resp = new AclFlowCountResp();
        resp->set_context(context());
        resp->Response();
    }
    std::stringstream ss(key);
    string uuid_str, item;
    std::string ace_id = "";
    if (getline(ss, item, ':')) {
        uuid_str = item;
    }
    if (getline(ss, item, ':')) {
        ace_id = item;
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

void AclEntrySpec::PopulateServiceType(const FirewallServiceType *fst) {
    ServicePort sp;
    if (fst == NULL) {
        service_group.push_back(sp);
        return;
    }

    if (fst->protocol.compare("any") == 0) {
        sp.protocol.min = 0x0;
        sp.protocol.max = 0xff;
        //Ignore port
        Range port;
        port.min = 0x0;
        port.max = 0xFFFF;
        sp.src_port = port;
        sp.dst_port = port;
    } else {
        sp.protocol.min = fst->protocol_id;
        sp.protocol.max = sp.protocol.min;

        sp.src_port.min = fst->src_ports.start_port;
        sp.src_port.max = fst->src_ports.end_port;

        sp.dst_port.min = fst->dst_ports.start_port;
        sp.dst_port.max = fst->dst_ports.end_port;
    }
    service_group.push_back(sp);
}

bool AclEntrySpec::PopulateServiceGroup(const ServiceGroup *s_group) {
    if (s_group->firewall_service_list().size() == 0) {
        PopulateServiceType(NULL);
    }

    std::vector<FirewallServiceType>::const_iterator it =
        s_group->firewall_service_list().begin();
    for (; it != s_group->firewall_service_list().end(); it++) {
        PopulateServiceType(&(*it));
    }
    return true;
}

void AclEntrySpec::ReverseAddress(AclEntrySpec *reverse) {
    src_addr_type = reverse->dst_addr_type;
    src_ip_list = reverse->dst_ip_list;
    src_policy_id = reverse->dst_policy_id;
    src_policy_id_str = reverse->dst_policy_id_str;
    src_sg_id = reverse->dst_sg_id;
    src_tags = reverse->dst_tags;

    dst_addr_type = reverse->src_addr_type;
    dst_ip_list = reverse->src_ip_list;
    dst_policy_id = reverse->src_policy_id;
    dst_policy_id_str = reverse->src_policy_id_str;
    dst_sg_id = reverse->src_sg_id;
    dst_tags = reverse->src_tags;
}

void AclEntrySpec::ReversePort(AclEntrySpec *reverse) {
    src_port = reverse->dst_port;
    dst_port = reverse->src_port;

    service_group.clear();

    ServiceGroupMatch::ServicePortList::const_iterator it;
    for (it = reverse->service_group.begin(); it != reverse->service_group.end();
         it++) {
        ServicePort sp;
        sp.protocol = it->protocol;
        sp.src_port = it->dst_port;
        sp.dst_port = it->src_port;
        service_group.push_back(sp);
    }
}

void AclEntrySpec::Reverse(AclEntrySpec *reverse, AclEntryID::Type id_type,
                           bool swap_address, bool swap_port) {
    type = reverse->type;
    id = reverse->id;
    id.type_ = id_type;

    if (swap_address) {
        ReverseAddress(reverse);
    }

    if (swap_port) {
        ReversePort(reverse);
    }

    protocol = reverse->protocol;

    terminal = reverse->terminal;
    action_l = reverse->action_l;

    match_tags = reverse->match_tags;
    rule_uuid = reverse->rule_uuid;
}

IFMapNode*
AclEntrySpec::GetAddressGroup(Agent *agent, IFMapNode *node,
                              const std::string &name) {

    IFMapAgentTable *fr_table = static_cast<IFMapAgentTable *>(node->table());
    DBGraph *graph = fr_table->GetGraph();

    for (DBGraphVertex::adjacency_iterator iter = node->begin(graph);
         iter != node->end(graph); ++iter) {
        IFMapNode *ag_node =
            static_cast<IFMapNode *>(iter.operator->());
        if (agent->config_manager()->SkipNode(ag_node,
                    agent->cfg()->cfg_address_group_table())) {
            continue;
        }

        if (ag_node->name() == name) {
            return ag_node;
        }
    }

    return NULL;
}

bool AclEntrySpec::BuildAddressGroup(Agent *agent, IFMapNode *node,
                                     const std::string &name,
                                     bool source) {

    IFMapNode *ag_ifmap_node = GetAddressGroup(agent, node, name);
    if (!ag_ifmap_node) {
        return false;
    }

    AddressGroup *ag = static_cast<AddressGroup *>(ag_ifmap_node->GetObject());

    //Walk thru all the labels associated with address_group
    IFMapAgentTable *ag_table =
        static_cast<IFMapAgentTable *>(ag_ifmap_node->table());
    DBGraph *graph = ag_table->GetGraph();

    for (DBGraphVertex::adjacency_iterator iter = ag_ifmap_node->begin(graph);
            iter != ag_ifmap_node->end(graph); ++iter) {
        IFMapNode *tag_node =
            static_cast<IFMapNode *>(iter.operator->());
        if (agent->config_manager()->SkipNode(tag_node,
                    agent->cfg()->cfg_tag_table())) {
            continue;
        }

        const Tag* tag=
            static_cast<const Tag *>(tag_node->GetObject());
        if (strtol(tag->id().c_str(), NULL, 16) == 0) {
            continue;
        }

        if (source) {
            src_tags.push_back(strtol(tag->id().c_str(), NULL, 16));
        } else {
            dst_tags.push_back(strtol(tag->id().c_str(), NULL, 16));
        }
    }

    std::vector<AclAddressInfo> ip_list;
    std::vector<SubnetType>::const_iterator it = ag->prefix().begin();
    for (; it != ag->prefix().end(); it++) {
        BuildAddressInfo(it->ip_prefix, it->ip_prefix_len, &ip_list);
    }

    if (source) {
        src_ip_list = ip_list;
    } else {
        dst_ip_list = ip_list;
    }

    return true;
}

bool AclEntrySpec::Populate(Agent *agent, IFMapNode *fw_rule_node,
                            const FirewallRule *fw_rule) {
    if (fw_rule->IsPropertySet(FirewallRule::SERVICE)) {
        PopulateServiceType(&(fw_rule->service()));
    }

    if (fw_rule->match_tags().size()) {
        std::vector<int>::const_iterator it =
            fw_rule->match_tag_types().begin();
        for (; it != fw_rule->match_tag_types().end(); it++) {
            match_tags.push_back(*it);
        }
        std::sort(match_tags.begin(), match_tags.end());
    }

    /* We need to support both subnet and subnet-list configurations being
     * present in a single ACL rule */
    if (fw_rule->endpoint_1().subnet.ip_prefix.size()) {
        src_addr_type = AddressMatch::IP_ADDR;
        //Build src_ip_list from 'subnet'
        if (fw_rule->endpoint_1().subnet.ip_prefix.size()) {
            BuildAddressInfo(fw_rule->endpoint_1().subnet.ip_prefix,
                             fw_rule->endpoint_1().subnet.ip_prefix_len,
                             &src_ip_list);
        }
    } else if (fw_rule->endpoint_1().virtual_network.size()) {
        std::string nt;
        nt = fw_rule->endpoint_1().virtual_network;
        src_addr_type = AddressMatch::NETWORK_ID;
        src_policy_id_str = nt;
    } else if (fw_rule->endpoint_1().tags.size()) {
        src_addr_type = AddressMatch::TAGS;
        std::vector<int>::const_iterator it =
            fw_rule->endpoint_1().tag_ids.begin();
        for (;it != fw_rule->endpoint_1().tag_ids.end(); it++) {
            src_tags.push_back(*it);
        }
         //Sort the tags for optimizing comparision
        std::sort(src_tags.begin(), src_tags.end());
    } else if (fw_rule->endpoint_1().address_group.size()) {
        if (BuildAddressGroup(agent, fw_rule_node,
                              fw_rule->endpoint_1().address_group, true)) {
            src_addr_type = AddressMatch::ADDRESS_GROUP;
        }
    }

    /* We need to support both subnet and subnet-list configurations being
     * present in a single ACL rule */
    if (fw_rule->endpoint_2().subnet.ip_prefix.size()) {
        dst_addr_type = AddressMatch::IP_ADDR;
        //Build src_ip_list from 'subnet'
        if (fw_rule->endpoint_2().subnet.ip_prefix.size()) {
            BuildAddressInfo(fw_rule->endpoint_2().subnet.ip_prefix,
                             fw_rule->endpoint_2().subnet.ip_prefix_len,
                             &dst_ip_list);
            dst_addr_type = AddressMatch::IP_ADDR;
        }
    } else if (fw_rule->endpoint_2().virtual_network.size()) {
        std::string nt;
        nt = fw_rule->endpoint_2().virtual_network;
        dst_addr_type = AddressMatch::NETWORK_ID;
        dst_policy_id_str = nt;
    } else if (fw_rule->endpoint_2().tags.size()) {
        dst_addr_type = AddressMatch::TAGS;
        std::vector<int>::const_iterator it =
            fw_rule->endpoint_2().tag_ids.begin();
        for (;it != fw_rule->endpoint_2().tag_ids.end(); it++) {
            dst_tags.push_back(*it);
        }
        //Sort the tags for optimizing comparision
        std::sort(dst_tags.begin(), dst_tags.end());
    } else if (fw_rule->endpoint_2().address_group.size()) {
        if (BuildAddressGroup(agent, fw_rule_node,
                              fw_rule->endpoint_2().address_group, false)) {
            dst_addr_type = AddressMatch::ADDRESS_GROUP;
        }
    }

    return true;
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
        if ((sp.start_port == -1) && (sp.end_port == -1)) {
            rs.min = 0;
        }
        src_port.push_back(rs);

        //dst port
        PortType dp;
        dp = match_condition->dst_port;
        rs.min = dp.start_port;
        rs.max = dp.end_port;
        if ((dp.start_port == -1) && (dp.end_port == -1)) {
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
       // Check for nic assisted mirroring
        if (action.ma.nic_assisted_mirroring) {
            agent->mirror_table()->AddMirrorEntry(
                    action.ma.analyzer_name,
                    action.ma.nic_assisted_mirroring_vlan);
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
    // Check for nic assisted mirroring
    ActionSpec maction;
    maction.ta_type = TrafficAction::MIRROR_ACTION;
    maction.simple_action = TrafficAction::MIRROR;
    // Check nic assisted mirroring supported.
    // Then Copy only mirroring_vlan.
    if (!action_list.mirror_to.analyzer_name.empty()
        && action_list.mirror_to.nic_assisted_mirroring) {
        maction.ma.nic_assisted_mirroring =
            action_list.mirror_to.nic_assisted_mirroring;
        maction.ma.nic_assisted_mirroring_vlan =
            action_list.mirror_to.nic_assisted_mirroring_vlan;
        maction.ma.analyzer_name = action_list.mirror_to.analyzer_name;
        action_l.push_back(maction);
        AddMirrorEntry(acl_table->agent());
    } else if (!action_list.mirror_to.analyzer_name.empty()) {
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

    if (action_list.host_based_service) {
        ActionSpec action;
        action.ta_type = TrafficAction::HBS_ACTION;
        action.simple_action = TrafficAction::HBS;
        action_l.push_back(action);
    }
}

void AclTable::set_ace_flow_sandesh_data_cb(FlowAceSandeshDataFn fn) {
    flow_ace_sandesh_data_cb_ = fn;
}

void AclTable::set_acl_flow_sandesh_data_cb(FlowAclSandeshDataFn fn) {
    flow_acl_sandesh_data_cb_ = fn;
}
