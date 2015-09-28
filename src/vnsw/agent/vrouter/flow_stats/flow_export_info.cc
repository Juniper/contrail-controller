#include <vrouter/flow_stats/flow_stats_collector.h>
#include <pkt/flow_table.h>

const std::map<uint16_t, const char*>
    FlowExportInfo::FlowDropReasonStr = boost::assign::map_list_of
        ((uint16_t)FlowEntry::DROP_UNKNOWN,                 "UNKNOWN")
        ((uint16_t)FlowEntry::SHORT_UNAVIALABLE_INTERFACE,
         "SHORT_UNAVIALABLE_INTERFACE")
        ((uint16_t)FlowEntry::SHORT_IPV4_FWD_DIS,       "SHORT_IPV4_FWD_DIS")
        ((uint16_t)FlowEntry::SHORT_UNAVIALABLE_VRF,
         "SHORT_UNAVIALABLE_VRF")
        ((uint16_t)FlowEntry::SHORT_NO_SRC_ROUTE,       "SHORT_NO_SRC_ROUTE")
        ((uint16_t)FlowEntry::SHORT_NO_DST_ROUTE,       "SHORT_NO_DST_ROUTE")
        ((uint16_t)FlowEntry::SHORT_AUDIT_ENTRY,        "SHORT_AUDIT_ENTRY")
        ((uint16_t)FlowEntry::SHORT_VRF_CHANGE,         "SHORT_VRF_CHANGE")
        ((uint16_t)FlowEntry::SHORT_NO_REVERSE_FLOW,    "SHORT_NO_REVERSE_FLOW")
        ((uint16_t)FlowEntry::SHORT_REVERSE_FLOW_CHANGE,
         "SHORT_REVERSE_FLOW_CHANGE")
        ((uint16_t)FlowEntry::SHORT_NAT_CHANGE,         "SHORT_NAT_CHANGE")
        ((uint16_t)FlowEntry::SHORT_FLOW_LIMIT,         "SHORT_FLOW_LIMIT")
        ((uint16_t)FlowEntry::SHORT_LINKLOCAL_SRC_NAT,
         "SHORT_LINKLOCAL_SRC_NAT")
        ((uint16_t)FlowEntry::SHORT_FAILED_VROUTER_INSTALL,
         "SHORT_FAILED_VROUTER_INST")
        ((uint16_t)FlowEntry::DROP_POLICY,              "DROP_POLICY")
        ((uint16_t)FlowEntry::DROP_OUT_POLICY,          "DROP_OUT_POLICY")
        ((uint16_t)FlowEntry::DROP_SG,                  "DROP_SG")
        ((uint16_t)FlowEntry::DROP_OUT_SG,              "DROP_OUT_SG")
        ((uint16_t)FlowEntry::DROP_REVERSE_SG,          "DROP_REVERSE_SG")
        ((uint16_t)FlowEntry::DROP_REVERSE_OUT_SG,      "DROP_REVERSE_OUT_SG");

FlowExportInfo::FlowExportInfo() :
    source_vn_(), dest_vn_(), sg_rule_uuid_(), nw_ace_uuid_(),
    setup_time_(0), teardown_time_(0), last_modified_time_(0),
    bytes_(0), packets_(0), flags_(0),
    flow_handle_(FlowEntry::kInvalidFlowHandle), action_info_(),
    vm_cfg_name_(), peer_vrouter_(), tunnel_type_(TunnelType::INVALID),
    underlay_source_port_(0), underlay_sport_exported_(false), exported_(false),
    fip_(0), fip_vmi_(AgentKey::ADD_DEL_CHANGE, nil_uuid(), "") {
    drop_reason_ = FlowDropReasonStr.at(FlowEntry::DROP_UNKNOWN);
    rev_flow_key_.Reset();
}

FlowExportInfo::FlowExportInfo(FlowEntry *fe, uint64_t setup_time) :
    source_vn_(fe->data().source_vn), dest_vn_(fe->data().dest_vn),
    sg_rule_uuid_(fe->sg_rule_uuid()), nw_ace_uuid_(fe->nw_ace_uuid()),
    setup_time_(setup_time), teardown_time_(0), last_modified_time_(setup_time),
    bytes_(0), packets_(0), flags_(fe->flags()),
    flow_handle_(fe->flow_handle()), action_info_(fe->match_p().action_info),
    vm_cfg_name_(fe->data().vm_cfg_name), peer_vrouter_(fe->peer_vrouter()),
    tunnel_type_(fe->tunnel_type()), underlay_source_port_(0),
    underlay_sport_exported_(false), exported_(false), fip_(fe->fip()),
    fip_vmi_(fe->fip_vmi()) {
    flow_uuid_ = FlowTable::rand_gen_();
    egress_uuid_ = FlowTable::rand_gen_();
    FlowEntry *rflow = fe->reverse_flow_entry();
    if (rflow) {
        rev_flow_key_ = fe->key();
    } else {
        rev_flow_key_.Reset();
    }
    drop_reason_ = FlowDropReasonStr.at(fe->data().drop_reason);
}

bool FlowExportInfo::IsActionLog() const {
    uint32_t fe_action = action_info_.action;
    if (fe_action & (1 << TrafficAction::LOG)) {
        return true;
    }
    return false;
}

