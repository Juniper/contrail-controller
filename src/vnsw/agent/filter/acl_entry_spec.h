/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __AGENT_ACL_ENTRY_SPEC_H__
#define __AGENT_ACL_ENTRY_SPEC_H__

#include <vector>
#include <boost/uuid/uuid.hpp>
#include <net/address.h>

#include <cmn/agent_cmn.h>
#include <cmn/agent.h>
#include <vnc_cfg_types.h>

#include <filter/traffic_action.h>
#include <filter/acl_entry_match.h>
#include <vnc_cfg_types.h>

struct RangeSpec {
    uint16_t min;
    uint16_t max;
};

struct MirrorActionSpec {
    std::string analyzer_name;
    std::string vrf_name;
    IpAddress ip;
    uint16_t port;
    std::string encap;
    bool operator == (const MirrorActionSpec &rhs) const {
        return analyzer_name == rhs.analyzer_name;
    }
};

struct VrfTranslateActionSpec {
    VrfTranslateActionSpec() : vrf_name_(""), ignore_acl_(false) { }
    VrfTranslateActionSpec(std::string vrf_name, bool ignore_acl):
        vrf_name_(vrf_name), ignore_acl_(ignore_acl) { }
    const std::string& vrf_name() const { return vrf_name_;}
    bool ignore_acl() const { return ignore_acl_;}
    void set_vrf_name(const std::string &vrf_name) {
        vrf_name_ = vrf_name;
    }
    void set_ignore_acl(bool ignore_acl) {
        ignore_acl_ = ignore_acl;
    }
    std::string vrf_name_;
    bool ignore_acl_;
};

struct ActionSpec {
    /* For actions log and alert we don't have any specific field. Only ta_type
     * of TrafficAction::LOG_ACTION and TrafficAction::ALERT_ACTION is enough
     */
    TrafficAction::Action simple_action;
    TrafficAction::TrafficActionType ta_type;
    MirrorActionSpec ma;
    VrfTranslateActionSpec vrf_translate;
    ActionSpec() {}
    ActionSpec(TrafficAction::TrafficActionType type) : ta_type(type) {}
};

typedef enum AclTypeSpec {
    TERM = 1,
    NON_TERM = 2,
} AclTypeSpecT;

class AclEntrySpec {
public:
  AclEntrySpec(): src_addr_type(AddressMatch::UNKNOWN_TYPE),
        dst_addr_type(AddressMatch::UNKNOWN_TYPE), terminal(true) { }
    typedef boost::uuids::uuid uuid;
    AclTypeSpecT type;
    uint32_t id;
    
    // Address
    AddressMatch::AddressType src_addr_type;
    std::vector<AclAddressInfo> src_ip_list;
    uuid src_policy_id;
    std::string src_policy_id_str;
    int src_sg_id;


    AddressMatch::AddressType dst_addr_type;
    std::vector<AclAddressInfo> dst_ip_list;
    uuid dst_policy_id;
    std::string dst_policy_id_str;
    int dst_sg_id;

    // Protocol
    std::vector<RangeSpec> protocol;
    
    // Source port range
    std::vector<RangeSpec> src_port;

    // Destination port range
    std::vector<RangeSpec> dst_port;

    bool terminal;

    // Action
    std::vector<ActionSpec> action_l;

    // Rule-UUID
    std::string rule_uuid;
    bool Populate(const autogen::MatchConditionType *match_condition);
    void PopulateAction(const AclTable *acl_table,
                        const autogen::ActionListType &action_list);
    void AddMirrorEntry() const;
    void BuildAddressInfo(const std::string &prefix, int plen,
                          std::vector<AclAddressInfo> *list);
};

struct AclSpec {
  AclSpec() : dynamic_acl(false) { };
    typedef boost::uuids::uuid uuid;
    uuid acl_id;
    // Dynamic
    bool dynamic_acl;
    std::vector<AclEntrySpec> acl_entry_specs_;
};

#endif
