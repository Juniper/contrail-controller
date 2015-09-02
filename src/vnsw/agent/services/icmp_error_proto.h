/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */
#ifndef VNSW_AGENT_SERVICES_ICMP_ERROR_PROTO_H_
#define VNSW_AGENT_SERVICES_ICMP_ERROR_PROTO_H_

#include <boost/asio.hpp>
#include "pkt/proto.h"

struct FlowKey;

class IcmpErrorProto : public Proto {
 public:
    struct Stats {
        Stats() { Reset(); }
        ~Stats() { }
        void Reset() {
            df_msgs = 0;
            drops = 0;
            interface_errors = 0;
            invalid_flow_index = 0;
        }

        uint32_t df_msgs;
        uint32_t drops;
        uint32_t interface_errors;
        uint32_t invalid_flow_index;
    };
    typedef boost::function<bool(uint32_t, FlowKey *)> FlowIndexToKeyFn;

    IcmpErrorProto(Agent *agent, boost::asio::io_service &io);
    virtual ~IcmpErrorProto();

    void Shutdown() { }
    void Register(FlowIndexToKeyFn fn) { flow_index_to_key_fn_ = fn; }
    bool FlowIndexToKey(uint32_t index, FlowKey *key);
    ProtoHandler *AllocProtoHandler(boost::shared_ptr<PktInfo> info,
                                    boost::asio::io_service &io);

    void incrememt_df_msgs() { stats_.df_msgs++; }
    void increment_drops() { stats_.drops++; }
    void increment_interface_errors() { stats_.interface_errors++; }
    void increment_invalid_flow_index() { stats_.invalid_flow_index++; }
    const Stats &GetStats() const { return stats_; }
    void ClearStats() { stats_.Reset(); }

 private:
    Stats stats_;
    FlowIndexToKeyFn flow_index_to_key_fn_;
    DISALLOW_COPY_AND_ASSIGN(IcmpErrorProto);
};

#endif  // VNSW_AGENT_SERVICES_ICMP_ERROR_PROTO_H_
