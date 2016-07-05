/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */
#ifndef __AGENT_FLOW_EXPORT_INFO_H__
#define __AGENT_FLOW_EXPORT_INFO_H__

#include <pkt/flow_entry.h>
#include <filter/acl.h>

class FlowExportInfo {
public:
    FlowExportInfo();
    FlowExportInfo(const FlowEntryPtr &fe, uint64_t setup_time);
    FlowExportInfo(const FlowEntryPtr &fe);
    ~FlowExportInfo() {}

    FlowEntry* flow() const { return flow_.get(); }
    FlowEntry* reverse_flow() const;
    const boost::uuids::uuid &egress_uuid() const { return flow_->egress_uuid(); }
    uint64_t setup_time() const { return setup_time_; }
    uint64_t teardown_time() const { return teardown_time_; }
    void set_teardown_time(uint64_t time) { teardown_time_ = time; }
    uint64_t last_modified_time() const { return last_modified_time_; }
    void set_last_modified_time(uint64_t time) { last_modified_time_ = time; }

    uint64_t bytes() const { return bytes_; }
    void set_bytes(uint64_t value) { bytes_ = value; }
    uint64_t packets() const { return packets_; }
    void set_packets(uint64_t value) { packets_ = value; }
    uint64_t prev_diff_bytes() const { return prev_diff_bytes_; }
    void set_prev_diff_bytes(uint64_t value) { prev_diff_bytes_ = value; }
    uint64_t prev_diff_packets() const { return prev_diff_packets_; }
    void set_prev_diff_packets(uint64_t value) { prev_diff_packets_ = value; }
    uint16_t underlay_source_port() const { return underlay_source_port_; }
    void set_underlay_source_port(uint16_t port) {
        underlay_source_port_ = port;
    }
    bool changed() const { return changed_; }
    void set_changed(bool value) {
        changed_ = value;
    }

    bool exported_atleast_once() const { return exported_atleast_once_; }
    void set_exported_atleast_once(bool value) {
        exported_atleast_once_ = value;
    }
    bool IsActionLog() const;
    void SetActionLog();
    uint16_t tcp_flags() const { return tcp_flags_; }
    void set_tcp_flags(uint16_t tflags) {
        tcp_flags_ = tflags;
    }
    void set_delete_enqueue_time(uint64_t value) { delete_enqueue_time_ = value; }
    uint64_t delete_enqueue_time() const { return delete_enqueue_time_; }
    void set_evict_enqueue_time(uint64_t value) { evict_enqueue_time_ = value; }
    uint64_t evict_enqueue_time() const { return evict_enqueue_time_; }
private:
    FlowEntryPtr flow_;
    uint64_t setup_time_;
    uint64_t teardown_time_;
    uint64_t last_modified_time_; //used for aging
    uint64_t bytes_;
    uint64_t packets_;
    /* When flow samples are dropped the diff stats for that sample is
     * accumulated in the following fields. This used to compute aggregate diff
     * when the flow is being sent again. On successful send the following
     * fields are reset */
    uint64_t prev_diff_bytes_;
    uint64_t prev_diff_packets_;
    //IP address of the src vrouter for egress flows and dst vrouter for
    //ingress flows. Used only during flow-export
    //Underlay IP protocol type. Used only during flow-export
    //Underlay source port. 0 for local flows. Used during flow-export
    uint16_t underlay_source_port_;
    bool changed_;
    uint16_t tcp_flags_;
    uint64_t delete_enqueue_time_;
    uint64_t evict_enqueue_time_;
    bool exported_atleast_once_;
};

#endif //  __AGENT_FLOW_EXPORT_INFO_H__
