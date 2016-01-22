/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */
#ifndef VNSW_AGENT_SERVICES_ICMPV6_ERROR_PROTO_H_
#define VNSW_AGENT_SERVICES_ICMPV6_ERROR_PROTO_H_

#include <boost/asio.hpp>
#include "pkt/proto.h"

class Icmpv6ErrorProto : public Proto {
 public:
    struct Stats {
        Stats() { Reset(); }
        ~Stats() { }
        void Reset() {
            drops = 0;
            interface_errors = 0;
        }

        uint32_t drops;
        uint32_t interface_errors;
    };

    Icmpv6ErrorProto(Agent *agent, boost::asio::io_service &io);
    virtual ~Icmpv6ErrorProto();
    ProtoHandler *AllocProtoHandler(boost::shared_ptr<PktInfo> info,
                                    boost::asio::io_service &io);

    void increment_drops() { stats_.drops++; }
    void increment_interface_errors() { stats_.interface_errors++; }
    const Stats &GetStats() const { return stats_; }
    void ClearStats() { stats_.Reset(); }

 private:
    Stats stats_;
    DISALLOW_COPY_AND_ASSIGN(Icmpv6ErrorProto);
};

#endif  // VNSW_AGENT_SERVICES_ICMPV6_ERROR_PROTO_H_
