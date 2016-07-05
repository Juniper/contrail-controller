#include <vrouter/flow_stats/flow_stats_collector.h>
#include <pkt/flow_table.h>

FlowExportInfo::FlowExportInfo() :
    flow_(), setup_time_(0), teardown_time_(0), last_modified_time_(0),
    bytes_(0), packets_(0), prev_diff_bytes_(0), prev_diff_packets_(0),
    underlay_source_port_(0), changed_(false),
    tcp_flags_(0), delete_enqueue_time_(0), evict_enqueue_time_(0),
    exported_atleast_once_(false) {
}

FlowExportInfo::FlowExportInfo(const FlowEntryPtr &fe) :
    flow_(fe), setup_time_(0), teardown_time_(0), last_modified_time_(0),
    bytes_(0), packets_(0), prev_diff_bytes_(0), prev_diff_packets_(0),
    underlay_source_port_(0), changed_(true),
    tcp_flags_(0), delete_enqueue_time_(0), evict_enqueue_time_(0),
    exported_atleast_once_(false) {
}

FlowExportInfo::FlowExportInfo(const FlowEntryPtr &fe, uint64_t setup_time) :
    flow_(fe), setup_time_(setup_time),
    teardown_time_(0), last_modified_time_(setup_time),
    bytes_(0), packets_(0), prev_diff_bytes_(0), prev_diff_packets_(0),
    underlay_source_port_(0), changed_(true),
    tcp_flags_(0), delete_enqueue_time_(0), evict_enqueue_time_(0),
    exported_atleast_once_(false) {
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
void FlowExportInfo::SetActionLog() {
    FlowEntry *fe = flow();
    fe->data().match_p.action_info.action =
        fe->data().match_p.action_info.action | (1 << TrafficAction::LOG);
}
