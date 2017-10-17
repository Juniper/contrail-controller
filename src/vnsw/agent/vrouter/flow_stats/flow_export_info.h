/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */
#ifndef __AGENT_FLOW_EXPORT_INFO_H__
#define __AGENT_FLOW_EXPORT_INFO_H__

#include <pkt/flow_entry.h>
#include <filter/acl.h>

class FlowExportInfo : public boost::intrusive::list_base_hook<> {
public:
    FlowExportInfo();
    FlowExportInfo(const FlowEntryPtr &fe, uint64_t setup_time);
    FlowExportInfo(const FlowEntryPtr &fe);
    ~FlowExportInfo() {}

    FlowEntry* flow() const { return flow_.get(); }
    FlowEntry* reverse_flow() const;
    uint64_t teardown_time() const { return teardown_time_; }
    void set_teardown_time(uint64_t time) { teardown_time_ = time; }
    uint64_t last_modified_time() const { return last_modified_time_; }
    void set_last_modified_time(uint64_t time) { last_modified_time_ = time; }

    uint64_t bytes() const { return bytes_; }
    void set_bytes(uint64_t value) { bytes_ = value; }
    uint64_t packets() const { return packets_; }
    void set_packets(uint64_t value) { packets_ = value; }

    void set_delete_enqueue_time(uint64_t value) { delete_enqueue_time_ = value; }
    uint64_t delete_enqueue_time() const { return delete_enqueue_time_; }
    void set_evict_enqueue_time(uint64_t value) { evict_enqueue_time_ = value; }
    uint64_t evict_enqueue_time() const { return evict_enqueue_time_; }

    uint8_t gen_id() const { return gen_id_; }
    void set_gen_id(uint8_t value) { gen_id_ = value; }
    uint32_t flow_handle() const { return flow_handle_; }
    void set_flow_handle(uint32_t value) { flow_handle_ = value; }
    const boost::uuids::uuid &uuid() const { return uuid_; }
    void CopyFlowInfo(FlowEntry *fe);
    void ResetStats();

private:
    FlowEntryPtr flow_;
    uint64_t teardown_time_;
    uint64_t last_modified_time_; //used for aging
    uint64_t bytes_;
    uint64_t packets_;
    uint64_t delete_enqueue_time_;
    uint64_t evict_enqueue_time_;
    uint8_t gen_id_;
    uint32_t flow_handle_;
    boost::uuids::uuid uuid_;
};

typedef boost::intrusive::list<FlowExportInfo> FlowExportInfoList;
#endif //  __AGENT_FLOW_EXPORT_INFO_H__
