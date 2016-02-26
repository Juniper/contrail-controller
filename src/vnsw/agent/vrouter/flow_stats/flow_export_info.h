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
    FlowExportInfo(FlowEntry *fe, uint64_t setup_time);
    ~FlowExportInfo() {}

    const boost::uuids::uuid &flow_uuid() const { return flow_uuid_; }
    const boost::uuids::uuid &egress_uuid() const { return egress_uuid_; }
    void set_egress_uuid(const boost::uuids::uuid &u) { egress_uuid_ = u; }
    const FlowKey &key() const { return key_; }
    const boost::uuids::uuid &rev_flow_uuid() const { return rev_flow_uuid_; }
    const std::string &source_vn() const { return source_vn_; }
    const std::string &dest_vn() const { return dest_vn_; }
    const std::string &sg_rule_uuid() const { return sg_rule_uuid_; }
    const std::string &nw_ace_uuid() const { return nw_ace_uuid_; }
    uint64_t setup_time() const { return setup_time_; }
    uint64_t teardown_time() const { return teardown_time_; }
    void set_teardown_time(uint64_t time) { teardown_time_ = time; }
    uint64_t last_modified_time() const { return last_modified_time_; }
    void set_last_modified_time(uint64_t time) { last_modified_time_ = time; }

    uint64_t bytes() const { return bytes_; }
    void set_bytes(uint64_t value) { bytes_ = value; }
    uint64_t packets() const { return packets_; }
    void set_packets(uint64_t value) { packets_ = value; }
    uint32_t flags() const { return flags_; }
    uint32_t flow_handle() const { return flow_handle_; }
    void set_flow_handle(uint32_t value) { flow_handle_ = value; }
    FlowAction action_info() const { return action_info_; }
    const std::string &vm_cfg_name() const { return vm_cfg_name_; }
    const std::string &peer_vrouter() const { return peer_vrouter_; }
    TunnelType tunnel_type() const { return tunnel_type_; }
    uint16_t underlay_source_port() const { return underlay_source_port_; }
    void set_underlay_source_port(uint16_t port) {
        underlay_source_port_ = port;
    }
    bool changed() const { return changed_; }
    void set_changed(bool value) {
        changed_ = value;
    }
    uint32_t fip() const { return fip_; }
    VmInterfaceKey fip_vmi() const { return fip_vmi_; }
    boost::uuids::uuid interface_uuid() const { return interface_uuid_; }

    bool IsActionLog() const;
    void SetActionLog();
    bool is_flags_set(const FlowEntry::FlowEntryFlags &value) const {
        return (flags_ & value);
    }
    const std::string &drop_reason() const { return drop_reason_; }
    uint16_t tcp_flags() const { return tcp_flags_; }
    void set_tcp_flags(uint16_t tflags) {
        tcp_flags_ = tflags;
    }
    bool IsEqual(const FlowExportInfo &rhs) const;
    void Copy(const FlowExportInfo &rhs);
    void set_delete_enqueued(bool value) { delete_enqueued_ = value; }
    bool delete_enqueued() const { return delete_enqueued_; }
    uint32_t flow_partition() const {
        return flow_partition_;
    }
private:
    boost::uuids::uuid flow_uuid_;
    boost::uuids::uuid egress_uuid_; // used/applicable only for local flows
    FlowKey key_;
    boost::uuids::uuid rev_flow_uuid_;
    std::string source_vn_;
    std::string dest_vn_;
    std::string sg_rule_uuid_;
    std::string nw_ace_uuid_;
    uint64_t setup_time_;
    uint64_t teardown_time_;
    uint64_t last_modified_time_; //used for aging
    uint64_t bytes_;
    uint64_t packets_;
    uint32_t flags_;
    uint32_t flow_handle_;
    FlowAction action_info_;
    std::string vm_cfg_name_;
    //IP address of the src vrouter for egress flows and dst vrouter for
    //ingress flows. Used only during flow-export
    std::string peer_vrouter_;
    //Underlay IP protocol type. Used only during flow-export
    TunnelType tunnel_type_;
    //Underlay source port. 0 for local flows. Used during flow-export
    uint16_t underlay_source_port_;
    bool changed_;
    uint32_t fip_;
    VmInterfaceKey fip_vmi_;
    boost::uuids::uuid interface_uuid_;
    std::string drop_reason_;
    uint16_t tcp_flags_;
    bool delete_enqueued_;
    uint32_t flow_partition_;
};

#endif //  __AGENT_FLOW_EXPORT_INFO_H__
