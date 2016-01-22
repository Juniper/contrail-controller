#include <vrouter/flow_stats/flow_stats_collector.h>
#include <pkt/flow_table.h>

FlowExportInfo::FlowExportInfo() :
    source_vn_(), dest_vn_(), sg_rule_uuid_(), nw_ace_uuid_(),
    setup_time_(0), teardown_time_(0), last_modified_time_(0),
    bytes_(0), packets_(0), flags_(0),
    flow_handle_(FlowEntry::kInvalidFlowHandle), action_info_(),
    vm_cfg_name_(), peer_vrouter_(), tunnel_type_(TunnelType::INVALID),
    underlay_source_port_(0), changed_(false),
    fip_(0), fip_vmi_(AgentKey::ADD_DEL_CHANGE, nil_uuid(), ""), tcp_flags_(0) {
    drop_reason_ = FlowEntry::DropReasonStr(FlowEntry::DROP_UNKNOWN);
    rev_flow_key_.Reset();
    interface_uuid_ = boost::uuids::nil_uuid();
}

FlowExportInfo::FlowExportInfo(FlowEntry *fe, uint64_t setup_time) :
    source_vn_(fe->data().source_vn_match), dest_vn_(fe->data().dest_vn_match),
    sg_rule_uuid_(fe->sg_rule_uuid()), nw_ace_uuid_(fe->nw_ace_uuid()),
    setup_time_(setup_time), teardown_time_(0), last_modified_time_(setup_time),
    bytes_(0), packets_(0), flags_(fe->flags()),
    flow_handle_(fe->flow_handle()), action_info_(fe->match_p().action_info),
    vm_cfg_name_(fe->data().vm_cfg_name), peer_vrouter_(fe->peer_vrouter()),
    tunnel_type_(fe->tunnel_type()), underlay_source_port_(0),
    changed_(true), fip_(fe->fip()), fip_vmi_(fe->fip_vmi()), tcp_flags_(0) {
    flow_uuid_ = FlowTable::rand_gen_();
    egress_uuid_ = FlowTable::rand_gen_();
    FlowEntry *rflow = fe->reverse_flow_entry();
    if (rflow) {
        rev_flow_key_ = rflow->key();
    } else {
        rev_flow_key_.Reset();
    }
    if (fe->intf_entry()) {
        interface_uuid_ = fe->intf_entry()->GetUuid();
    } else {
        interface_uuid_ = boost::uuids::nil_uuid();
    }
    drop_reason_ = FlowEntry::DropReasonStr(fe->data().drop_reason);
}

/* This API compares only fields which are copied from FlowEntry. Fields which
 * are locally generated or read from vrouter are not compared. */
bool FlowExportInfo::IsEqual(const FlowExportInfo &rhs) const {
    if (source_vn_ != rhs.source_vn()) {
        return false;
    }
    if (dest_vn_ != rhs.dest_vn()) {
        return false;
    }
    if (sg_rule_uuid_ != rhs.sg_rule_uuid()) {
        return false;
    }
    if (nw_ace_uuid_ != rhs.nw_ace_uuid()) {
        return false;
    }
    if (flags_ != rhs.flags()) {
        return false;
    }
    if (action_info_.action != rhs.action_info().action) {
        return false;
    }
    if (vm_cfg_name_ != rhs.vm_cfg_name()) {
        return false;
    }
    if (peer_vrouter_ != rhs.peer_vrouter()) {
        return false;
    }
    if (!tunnel_type_.Compare(rhs.tunnel_type())) {
        return false;
    }
    if (fip_ != rhs.fip()) {
        return false;
    }
    if (!fip_vmi_.IsEqual(rhs.fip_vmi())) {
        return false;
    }
    if (!rev_flow_key_.IsEqual(rhs.rev_flow_key())) {
        return false;
    }
    if (interface_uuid_ != rhs.interface_uuid()) {
        return false;
    }
    if (drop_reason_ != rhs.drop_reason()) {
        return false;
    }
    return true;
}

void FlowExportInfo::Copy(const FlowExportInfo &rhs) {
    source_vn_ = rhs.source_vn();
    dest_vn_ = rhs.dest_vn();
    sg_rule_uuid_ = rhs.sg_rule_uuid();
    nw_ace_uuid_ = rhs.nw_ace_uuid();
    flags_ = rhs.flags();
    action_info_ = rhs.action_info();
    vm_cfg_name_ = rhs.vm_cfg_name();
    peer_vrouter_ = rhs.peer_vrouter();
    tunnel_type_ = rhs.tunnel_type();
    fip_ = rhs.fip();
    fip_vmi_ = rhs.fip_vmi();
    rev_flow_key_ = rhs.rev_flow_key();
    interface_uuid_ = rhs.interface_uuid();
    drop_reason_ = rhs.drop_reason();
    changed_ = true;
}

bool FlowExportInfo::IsActionLog() const {
    uint32_t fe_action = action_info_.action;
    if (fe_action & (1 << TrafficAction::LOG)) {
        return true;
    }
    return false;
}

void FlowExportInfo::SetActionLog() {
    action_info_.action = action_info_.action | (1 << TrafficAction::LOG);
}

