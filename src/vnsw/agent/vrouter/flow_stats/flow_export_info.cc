#include <vrouter/flow_stats/flow_stats_collector.h>
#include <pkt/flow_table.h>

FlowExportInfo::FlowExportInfo() :
    flow_(), egress_uuid_(boost::uuids::nil_uuid()),
    setup_time_(0), teardown_time_(0), last_modified_time_(0),
    bytes_(0), packets_(0), underlay_source_port_(0), changed_(false),
    tcp_flags_(0), delete_enqueue_time_(0) {
}

FlowExportInfo::FlowExportInfo(const FlowEntryPtr &fe) :
    flow_(fe), egress_uuid_(boost::uuids::nil_uuid()),
    setup_time_(0), teardown_time_(0), last_modified_time_(0),
    bytes_(0), packets_(0), underlay_source_port_(0), changed_(true),
    tcp_flags_(0), delete_enqueue_time_(0) {
}

FlowExportInfo::FlowExportInfo(const FlowEntryPtr &fe, uint64_t setup_time) :
    flow_(fe), egress_uuid_(boost::uuids::nil_uuid()),
    setup_time_(setup_time), teardown_time_(0), last_modified_time_(setup_time),
    bytes_(0), packets_(0), underlay_source_port_(0), changed_(true),
    tcp_flags_(0), delete_enqueue_time_(0) {
}

FlowEntry* FlowExportInfo::reverse_flow() const {
    FlowEntry *rflow = NULL;
    if (flow_.get()) {
        rflow = flow_->reverse_flow_entry();
    }
    return rflow;
}

bool FlowExportInfo::IsActionLog() const {
    uint32_t fe_action = flow_->data().match_p.action_info.action;
    if (fe_action & (1 << TrafficAction::LOG)) {
        return true;
    }
    return false;
}

///////////////////////////////////////////////////////////////////
// APIs used only by UT
//////////////////////////////////////////////////////////////////
bool FlowExportInfo::IsActionLogLocked() const {
    FlowEntry *fe = flow();
    FlowEntry *rfe = NULL;
    if (!fe) {
        return false;
    }
    FLOW_LOCK(fe, rfe);
    return IsActionLog();
}

void FlowExportInfo::SetActionLog() {
    FlowEntry *fe = flow();
    FlowEntry *rfe = NULL;
    if (!fe) {
        return;
    }
    FLOW_LOCK(fe, rfe);
    fe->data().match_p.action_info.action =
        fe->data().match_p.action_info.action | (1 << TrafficAction::LOG);
}

uint32_t FlowExportInfo::FlowHandleLocked() const {
    FlowEntry *fe = flow();
    FlowEntry *rfe = NULL;
    if (!fe) {
        return FlowEntry::kInvalidFlowHandle;
    }
    FLOW_LOCK(fe, rfe);
    return fe->flow_handle();
}
