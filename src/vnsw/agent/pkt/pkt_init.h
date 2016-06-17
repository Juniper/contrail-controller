/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __VNSW_PKT_INIT__
#define __VNSW_PKT_INIT__

#include <sandesh/sandesh_trace.h>

class PktHandler;
class FlowTable;
class FlowProto;
class PacketBufferManager;
class ControlInterface;
class FlowMgmtManager;

// Packet Module
class PktModule {
public:
    PktModule(Agent *agent);
    virtual ~PktModule();

    typedef std::vector<FlowMgmtManager *> FlowMgmtManagerList;
    void Init(bool run_with_vrouter);
    void Shutdown();
    void IoShutdown();
    void FlushFlows();
    void InitDone();

    Agent *agent() const { return agent_; }
    PktHandler *pkt_handler() const { return pkt_handler_.get(); }
    FlowTable *flow_table(uint16_t index) const;
    PacketBufferManager *packet_buffer_manager() const {
        return packet_buffer_manager_.get();
    }
    FlowMgmtManagerList flow_mgmt_manager_list() const {
        return flow_mgmt_manager_list_;
    }
    FlowMgmtManager *flow_mgmt_manager(uint16_t index) const {
        assert(index < flow_mgmt_manager_list_.size());
        return flow_mgmt_manager_list_[index];
    }
    FlowProto *get_flow_proto() const { return flow_proto_.get(); }

    void set_control_interface(ControlInterface *val);
    ControlInterface *control_interface() const { return control_interface_; }

    void CreateInterfaces();

private:
    Agent *agent_;
    ControlInterface *control_interface_;
    boost::scoped_ptr<PktHandler> pkt_handler_;
    boost::scoped_ptr<FlowProto> flow_proto_;
    boost::scoped_ptr<PacketBufferManager> packet_buffer_manager_;
    FlowMgmtManagerList flow_mgmt_manager_list_;
    DISALLOW_COPY_AND_ASSIGN(PktModule);
};

extern SandeshTraceBufferPtr PacketTraceBuf;

#endif // __VNSW_PKT_INIT__
