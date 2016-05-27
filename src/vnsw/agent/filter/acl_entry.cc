/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <vector>
#include <sstream>

#include <boost/cast.hpp>

#include <base/util.h>
#include <base/logging.h>
#include <net/address.h>

#include <cmn/agent_cmn.h>
#include <vnc_cfg_types.h>
#include <agent_types.h>

#include <filter/traffic_action.h>
#include <filter/acl_entry_match.h>
#include <filter/acl_entry.h>
#include <filter/acl_entry_spec.h>
#include <filter/packet_header.h>
#include <filter/acl.h>
#include <oper/qos_config.h>
#include <oper/mirror_table.h>

AclEntry::ActionList AclEntry::kEmptyActionList;

AclEntry::~AclEntry() {
    // Clean up Matches
    std::vector<AclEntryMatch *>::iterator it;
    for (it = matches_.begin(); it != matches_.end();) {
        delete(*it);
        matches_.erase(it);
    }
    
    // Clean up Actions
    ActionList::iterator ial;
    for (ial = actions_.begin(); ial != actions_.end(); ial++) {
        delete (*ial);
    }
    actions_.clear();
}

void AclEntry::PopulateAclEntry(const AclEntrySpec &acl_entry_spec)
{
    id_ = acl_entry_spec.id;
    uuid_ = acl_entry_spec.rule_uuid;

    if (acl_entry_spec.src_addr_type == AddressMatch::IP_ADDR) {
        AddressMatch *src_addr = new AddressMatch();
        src_addr->SetSource(true);
        src_addr->SetIPAddress(acl_entry_spec.src_ip_list);
        matches_.push_back(src_addr);
    } else if (acl_entry_spec.src_addr_type == AddressMatch::NETWORK_ID){
        AddressMatch *src_addr = new AddressMatch();
        src_addr->SetSource(true);
        src_addr->SetNetworkIDStr(acl_entry_spec.src_policy_id_str);
        matches_.push_back(src_addr);
    } else if (acl_entry_spec.src_addr_type == AddressMatch::SG) {
        AddressMatch *src_addr = new AddressMatch();
        src_addr->SetSource(true);
        src_addr->SetSGId(acl_entry_spec.src_sg_id);
        matches_.push_back(src_addr);
    }

    if (acl_entry_spec.dst_addr_type == AddressMatch::IP_ADDR) {
        AddressMatch *dst_addr = new AddressMatch();
        dst_addr->SetSource(false);
        dst_addr->SetIPAddress(acl_entry_spec.dst_ip_list);
        matches_.push_back(dst_addr);
    } else if (acl_entry_spec.dst_addr_type == AddressMatch::NETWORK_ID){
        AddressMatch *dst_addr = new AddressMatch();
        dst_addr->SetSource(false);
        dst_addr->SetNetworkIDStr(acl_entry_spec.dst_policy_id_str);
        matches_.push_back(dst_addr);
    } else if (acl_entry_spec.dst_addr_type == AddressMatch::SG) {
        AddressMatch *dst_addr = new AddressMatch();
        dst_addr->SetSource(false);
        dst_addr->SetSGId(acl_entry_spec.dst_sg_id);
        matches_.push_back(dst_addr);
    }    

    if (acl_entry_spec.protocol.size() > 0) {
        ProtocolMatch *proto = new ProtocolMatch();
        std::vector<RangeSpec>::const_iterator it;
        for (it = acl_entry_spec.protocol.begin(); 
             it != acl_entry_spec.protocol.end(); it++) {
            proto->SetProtocolRange((*it).min, (*it).max);
        }
        matches_.push_back(proto);
    }

    if (acl_entry_spec.dst_port.size() > 0) {
        DstPortMatch *port = new DstPortMatch();
        std::vector<RangeSpec>::const_iterator it;
        for (it = acl_entry_spec.dst_port.begin(); 
             it != acl_entry_spec.dst_port.end(); it++) {
            port->SetPortRange((*it).min, (*it).max);
        }
        matches_.push_back(port);
    }

    if (acl_entry_spec.src_port.size() > 0) {
        SrcPortMatch *port = new SrcPortMatch();
        std::vector<RangeSpec>::const_iterator it;
        for (it = acl_entry_spec.src_port.begin(); 
             it != acl_entry_spec.src_port.end(); it++) {
            port->SetPortRange((*it).min, (*it).max);
        }
        matches_.push_back(port);
    }

    if (acl_entry_spec.action_l.size() > 0) {
        std::vector<ActionSpec>::const_iterator it;
        for (it = acl_entry_spec.action_l.begin();
             it != acl_entry_spec.action_l.end(); ++it) {
            if ((*it).ta_type == TrafficAction::SIMPLE_ACTION) {
                SimpleAction *act = new SimpleAction((*it).simple_action);
                actions_.push_back(act);
            } else if ((*it).ta_type == TrafficAction::LOG_ACTION) {
                LogAction *act = new LogAction();
                actions_.push_back(act);
            } else if ((*it).ta_type == TrafficAction::ALERT_ACTION) {
                AlertAction *act = new AlertAction();
                actions_.push_back(act);
            } else if ((*it).ta_type == TrafficAction::MIRROR_ACTION) {
                MirrorAction *act = new MirrorAction((*it).ma.analyzer_name,
                                                     (*it).ma.vrf_name,
                                                     (*it).ma.ip,
                                                     (*it).ma.port,
                                                     (*it).ma.encap);
                actions_.push_back(act);
            } else if ((*it).ta_type == TrafficAction::VRF_TRANSLATE_ACTION) {
                VrfTranslateAction *act =
                    new VrfTranslateAction((*it).vrf_translate.vrf_name(),
                                           (*it).vrf_translate.ignore_acl());
                actions_.push_back(act);
            } else if ((*it).ta_type == TrafficAction::QOS_ACTION) {
                QosConfigAction *act =
                    new QosConfigAction((*it).qos_config_action.name());

                const AgentQosConfig *qos_config = Agent::GetInstance()->
                    qos_config_table()->FindByName((*it).qos_config_action.name());
                act->set_qos_config_ref(qos_config);
                actions_.push_back(act);
            } else {
                ACL_TRACE(Err, "Not supported action " + integerToString((*it).ta_type));
            }
        }
    }

    if (acl_entry_spec.terminal) {
        type_ = TERMINAL;
    } else {
        type_ = NON_TERMINAL;
    }
}

bool AclEntry::IsQosConfigResolved() {
    ActionList::iterator al;
    for (al = actions_.begin(); al != actions_.end(); al++) {
        if ((*al)->action_type() == TrafficAction::QOS_ACTION) {
            QosConfigAction *act = static_cast<QosConfigAction *>(*al);
            if (act->qos_config_ref() == NULL) {
                return false;
            }
        }
    }
    return true;
}

bool AclEntry::ResyncQosConfigEntries() {
    bool ret = false;
    ActionList::iterator al;
    for (al = actions_.begin(); al != actions_.end(); al++) {
        if ((*al)->action_type() == TrafficAction::QOS_ACTION) {
            QosConfigAction *act = static_cast<QosConfigAction *>(*al);
            const AgentQosConfig *qos_config = Agent::GetInstance()->
                qos_config_table()->FindByName(act->name());
            if (act->qos_config_ref() != qos_config) {
                act->set_qos_config_ref(qos_config);
                ret = true;
            }
        }
    }
    return ret;
}

void AclEntry::set_mirror_entry(MirrorEntryRef me) { 
        mirror_entry_ = me;
}

const AclEntry::ActionList &AclEntry::PacketMatch(const PacketHeader &packet_header,
                                                  FlowPolicyInfo *info) const
{
    std::vector<AclEntryMatch *>::const_iterator it;
    for (it = matches_.begin(); it != matches_.end(); it++) {
        if (!((*it)->Match(&packet_header, info))) {
            return AclEntry::kEmptyActionList;
        }
    }
    return Actions();
}

void AclEntry::SetAclEntrySandeshData(AclEntrySandeshData &data) const {

    // Set match data
    std::vector<AclEntryMatch *>::const_iterator mit;
    for (mit = matches_.begin(); mit != matches_.end(); mit++) {
        (*mit)->SetAclEntryMatchSandeshData(data);
    }

    // Set action list
    ActionList::const_iterator ait;
    for (ait = actions_.begin(); ait != actions_.end(); ait++) {
        (*ait)->SetActionSandeshData(data.action_l);
    }

    // AclEntry type
    if (type_ == TERMINAL) {
        data.rule_type = "Terminal";
    } else if (type_ == NON_TERMINAL) {
        data.rule_type = "Non-Terminal";
    } else {
        data.rule_type = "Unknown";
    }

    // AclEntry ID
    data.ace_id = integerToString(id_);
    // UUID
    data.uuid = uuid_;
}

bool AclEntry::IsTerminal() const
{
    if (type_ == TERMINAL) {
        return true;
    }
    return false;
}

bool AclEntry::operator==(const AclEntry &rhs) const {
    if (id_ != rhs.id_) {
        return false;
    }

    if (type_ != rhs.type_) {
        return false;
    }

    if (uuid_ != rhs.uuid_) {
        return false;
    }

   std::vector<AclEntryMatch *>::const_iterator it = matches_.begin();
   std::vector<AclEntryMatch *>::const_iterator rhs_it = rhs.matches_.begin();
   while (it != matches_.end() &&
          rhs_it != rhs.matches_.end()) {
       if (**it == **rhs_it) {
           it++;
           rhs_it++;
           continue;
       }
       return false;
   }

   if (it != matches_.end() || rhs_it != rhs.matches_.end()) {
       return false;
   }

   ActionList::const_iterator action_it = actions_.begin();
   ActionList::const_iterator rhs_action_it = rhs.actions_.begin();
   while (action_it != actions_.end() &&
          rhs_action_it != rhs.actions_.end()) {
       if (**action_it == **rhs_action_it) {
           action_it++;
           rhs_action_it++;
           continue;
       }
       return false;
   }

   if (action_it != actions_.end() || rhs_action_it != rhs.actions_.end()) {
       return false;
   }

   return true;
}

void AddressMatch::SetIPAddress(const std::vector<AclAddressInfo> &list)
{
    addr_type_ = IP_ADDR;
    ip_list_ = list;
}

void AddressMatch::SetNetworkID(const uuid id)
{
    addr_type_ = NETWORK_ID;
    policy_id_ = id;
}

void AddressMatch::SetNetworkIDStr(const std::string id)
{
    addr_type_ = NETWORK_ID;
    policy_id_s_ = id;
}

void AddressMatch::SetSGId(const uint32_t sg_id)
{
    addr_type_ = SG;
    sg_id_ = sg_id;
}

void AddressMatch::SetSource(const bool src)
{
    src_ = src;
}

bool AddressMatch::SGMatch(const SecurityGroupList *sg_l, int id) const
{
    if (!sg_l) { 
        return false;
    }

    if (id == kAny) {
        return true;
    }

    SecurityGroupList::const_iterator it;
    for (it = sg_l->begin(); it != sg_l->end(); ++it) {
        if (*it == id) return true;
    }
    return false;
}

bool AddressMatch::SGMatch(const SecurityGroupList &sg_l, int id) const
{
    if (id == kAny) {
        return true;
    }

    SecurityGroupList::const_iterator it;
    for (it = sg_l.begin(); it != sg_l.end(); ++it) {
        if (*it == id) return true;
    }
    return false;
}

static bool SubnetMatch(const std::vector<AclAddressInfo> &list,
                        const IpAddress &data) {
    std::vector<AclAddressInfo>::const_iterator it = list.begin();
    while (it != list.end()) {
        IpAddress ip = it->ip_addr;
        IpAddress mask = it->ip_mask;
        if (data.is_v4() && ip.is_v4()) {
            if((mask.to_v4().to_ulong() & data.to_v4().to_ulong()) ==
               ip.to_v4().to_ulong()) {
                return true;
            }
        }

        if (data.is_v6() && ip.is_v6()) {
            const Ip6Address &ip6 = ip.to_v6();
            const Ip6Address &data6 = data.to_v6();
            const Ip6Address &mask6 = mask.to_v6();
            boost::array<uint8_t, 16> ip6_bytes = ip6.to_bytes();
            boost::array<uint8_t, 16> data6_bytes = data6.to_bytes();
            boost::array<uint8_t, 16> mask6_bytes = mask6.to_bytes();
            const uint32_t *ip6_words = (uint32_t *)ip6_bytes.c_array();
            const uint32_t *data6_words = (uint32_t *)data6_bytes.c_array();
            const uint32_t *mask6_words = (uint32_t *)mask6_bytes.c_array();
            bool matched = true;
            for (int i = 0; i < 4; i++) {
                if ((data6_words[i] & mask6_words[i]) != ip6_words[i]) {
                    matched = false;
                    break;
                }
            }
            if (matched) {
                return true;
            }
        }
        ++it;
    }

    return false;
}

bool AddressMatch::Match(const PacketHeader *pheader,
                         FlowPolicyInfo *info) const
{
    if (policy_id_s_.compare("any") == 0) {
        return true;
    }
    if (src_) {
        if (addr_type_ == IP_ADDR) {
            return SubnetMatch(ip_list_, pheader->src_ip);
        } else if (addr_type_ == NETWORK_ID) {
            if (!pheader->src_policy_id)
                return false;
            VnListType::iterator it =
                pheader->src_policy_id->find(policy_id_s_);
            if (it != pheader->src_policy_id->end()) {
                if (info)
                    info->src_match_vn = *it;
                return true;
            }
            if (info)
                info->src_match_vn.clear();
            return false;
        } else if (addr_type_ == SG) {
            return SGMatch(pheader->src_sg_id_l, sg_id_);
        }
    } else { 
        if (addr_type_ == IP_ADDR) {
            return SubnetMatch(ip_list_, pheader->dst_ip);
        } else if (addr_type_ == NETWORK_ID) {
            if (!pheader->dst_policy_id)
                return false;
            VnListType::iterator it =
                pheader->dst_policy_id->find(policy_id_s_);
            if (it != pheader->dst_policy_id->end()) {
                if (info)
                    info->dst_match_vn = *it;
                return true;
            }
            if (info)
                info->dst_match_vn.clear();
            return false;
        } else if (addr_type_ == SG) {
            return SGMatch(pheader->dst_sg_id_l, sg_id_);
        }
    }
    return false;
}

bool AddressMatch::Compare(const AclEntryMatch &rhs) const {
    const AddressMatch &rhs_address_match =
        static_cast<const AddressMatch &>(rhs);
    if (addr_type_ != rhs_address_match.addr_type_) {
        return false;
    }

    if (src_ != rhs_address_match.src_) {
        return false;
    }

    if (addr_type_ == IP_ADDR) {
        if (ip_list_ == rhs_address_match.ip_list_) {
            return true;
        }
    }

    if (addr_type_ == NETWORK_ID) {
        if (policy_id_s_ == rhs_address_match.policy_id_s_) {
            return true;
        }
    }

    if (addr_type_ == SG) {
        if (sg_id_ == rhs_address_match.sg_id_) {
            return true;
        }
    }
    return false;
}

std::string AddressMatch::BuildIpMaskList
    (const std::vector<AclAddressInfo> &list) {
    std::vector<AclAddressInfo>::const_iterator it = list.begin();
    std::stringstream ss;
    while (it != list.end()) {
        IpAddress ip = it->ip_addr;
        IpAddress mask = it->ip_mask;
        ss << ip.to_string();
        ss << " ";
        ss << mask.to_string();
        ++it;
        if (it != list.end()) {
            ss << ", ";
        }
    }
    return ss.str();
}

void AddressMatch::SetAclEntryMatchSandeshData(AclEntrySandeshData &data)
{

    std::string *str;
    std::string *addr_type_str;

    if (src_) {
        str = &data.src;
        addr_type_str = &data.src_type;
    } else {
        str = &data.dst;
        addr_type_str = &data.dst_type;
    }

    if (addr_type_ == IP_ADDR) {
        *str = BuildIpMaskList(ip_list_);
        *addr_type_str = "ip";
    } else if (addr_type_ == NETWORK_ID) {
        *str = policy_id_s_;
        *addr_type_str = "network";
    } else if (addr_type_ == SG) {
        std::ostringstream ss;
        ss << sg_id_;
        *str = ss.str();
        *addr_type_str = "sg";
    } else {
        *str = "Unknown Address Type";
        *addr_type_str = "unknown";
    }
    return;

}

void ProtocolMatch::SetProtocolRange(const uint16_t min_protocol, 
                                     const uint16_t max_protocol)
{
    Range *protocol_range = new Range(min_protocol, max_protocol);
    protocol_ranges_.push_back(*protocol_range);
}

bool ProtocolMatch::Compare(const AclEntryMatch &rhs) const {
    const ProtocolMatch &rhs_port_match =
        static_cast<const ProtocolMatch &>(rhs);
    RangeSList::const_iterator it = protocol_ranges_.begin();
    RangeSList::const_iterator rhs_it =
        rhs_port_match.protocol_ranges_.begin();
    while (it != protocol_ranges_.end() &&
            rhs_it !=  rhs_port_match.protocol_ranges_.end()) {
        if (*it == *rhs_it) {
            it++;
            rhs_it++;
            continue;
        }
        return false;
    }
    if (it == protocol_ranges_.end() &&
        rhs_it == rhs_port_match.protocol_ranges_.end()) {
        return true;
    }
    return false;
}

bool ProtocolMatch::Match(const PacketHeader *packet_header,
                          FlowPolicyInfo *info) const
{
    for (RangeSList::const_iterator it = protocol_ranges_.begin(); 
         it != protocol_ranges_.end(); it++) {
        if(packet_header->protocol < (*it).min ||
           packet_header->protocol > (*it).max) {
            continue;
        } else {
            return true;
        }
    }
    return false;
}

void ProtocolMatch::SetAclEntryMatchSandeshData(AclEntrySandeshData &data)
{
    for (RangeSList::const_iterator it = protocol_ranges_.begin(); 
         it != protocol_ranges_.end(); it++) {
        class SandeshRange proto;
        proto.min = (*it).min;
        proto.max = (*it).max;
        data.proto_l.push_back(proto);
    }
}


void PortMatch::SetPortRange(const uint16_t min_port, const uint16_t max_port)
{
    Range *port_range = new Range(min_port, max_port);
    port_ranges_.push_back(*port_range);
}

bool PortMatch::Compare(const AclEntryMatch &rhs) const {
    const PortMatch &rhs_port_match = static_cast<const PortMatch &>(rhs);
    RangeSList::const_iterator it = port_ranges_.begin();
    RangeSList::const_iterator rhs_it = rhs_port_match.port_ranges_.begin();
    while (it != port_ranges_.end() ||
            rhs_it !=  rhs_port_match.port_ranges_.end()) {
        if (*it == *rhs_it) {
            it++;
            rhs_it++;
            continue;
        }
        return false;
    }
    if (it == port_ranges_.end() &&
            rhs_it == rhs_port_match.port_ranges_.end()) {
        return true;
    }
    return false;
}

bool SrcPortMatch::Match(const PacketHeader *packet_header,
                         FlowPolicyInfo *info) const
{
    if (packet_header->protocol != IPPROTO_TCP &&
            packet_header->protocol != IPPROTO_UDP) {
        return true;
    }

    for (RangeSList::const_iterator it = port_ranges_.begin(); 
         it != port_ranges_.end(); it++) {
        if(packet_header->src_port < (*it).min ||
           packet_header->src_port > (*it).max) {
            continue;
        } else {
            return true;
        }
    }
    return false;
}

void SrcPortMatch::SetAclEntryMatchSandeshData(AclEntrySandeshData &data)
{
    for (RangeSList::const_iterator it = port_ranges_.begin(); 
         it != port_ranges_.end(); it++) {
        class SandeshRange port;
        port.min = (*it).min;
        port.max = (*it).max;
        data.src_port_l.push_back(port);
    }
}


bool DstPortMatch::Match(const PacketHeader *packet_header,
                         FlowPolicyInfo *info) const
{
    if (packet_header->protocol != IPPROTO_TCP &&
            packet_header->protocol != IPPROTO_UDP) {
        return true;
    }

    for (RangeSList::const_iterator it = port_ranges_.begin(); 
         it != port_ranges_.end(); it++) {
        if(packet_header->dst_port < (*it).min ||
           packet_header->dst_port > (*it).max) {
            continue;
        } else {
            return true;
        }
    }
    return false;
}

void DstPortMatch::SetAclEntryMatchSandeshData(AclEntrySandeshData &data)
{
    for (RangeSList::const_iterator it = port_ranges_.begin(); 
         it != port_ranges_.end(); it++) {
        class SandeshRange port;
        port.min = (*it).min;
        port.max = (*it).max;
        data.dst_port_l.push_back(port);
    }
}
