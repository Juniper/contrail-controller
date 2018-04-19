#include <vrouter/flow_stats/flow_stats_collector.h>
#include <pkt/flow_table.h>

FlowExportInfo::FlowExportInfo() :
    flow_(), teardown_time_(0), last_modified_time_(0),
    bytes_(0), packets_(0),
    delete_enqueue_time_(0), evict_enqueue_time_(0),
    gen_id_(0),
    flow_handle_(FlowEntry::kInvalidFlowHandle) {
}

FlowExportInfo::FlowExportInfo(const FlowEntryPtr &fe) :
    flow_(fe), teardown_time_(0), last_modified_time_(0),
    bytes_(0), packets_(0),
    delete_enqueue_time_(0), evict_enqueue_time_(0),
    gen_id_(0),
    flow_handle_(FlowEntry::kInvalidFlowHandle) {
}

FlowExportInfo::FlowExportInfo(const FlowEntryPtr &fe, uint64_t setup_time) :
    flow_(fe),
    teardown_time_(0), last_modified_time_(setup_time),
    bytes_(0), packets_(0),
    delete_enqueue_time_(0), evict_enqueue_time_(0),
    gen_id_(0),
    flow_handle_(FlowEntry::kInvalidFlowHandle) {
}

FlowEntry* FlowExportInfo::reverse_flow() const {
    FlowEntry *rflow = NULL;
    if (flow_.get()) {
        rflow = flow_->reverse_flow_entry();
    }
    return rflow;
}

void FlowExportInfo::CopyFlowInfo(FlowEntry *fe) {
    gen_id_ = fe->gen_id();
    flow_handle_ = fe->flow_handle();
    uuid_ = fe->uuid();
}

void FlowExportInfo::ResetStats() {
    bytes_ = packets_ = 0;
}
