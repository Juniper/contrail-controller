/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_segment_hc_proto_h_
#define vnsw_agent_segment_hc_proto_h_

#include "pkt/proto.h"
#include "oper/health_check.h"

#define BFD_TX_BUFF_LEN 128

class SegmentHCProto : public Proto {
public:

    SegmentHCProto(Agent *agent, boost::asio::io_service &io);
    virtual ~SegmentHCProto();
    ProtoHandler *AllocProtoHandler(boost::shared_ptr<PktInfo> info,
                                    boost::asio::io_service &io);
    void Shutdown() {
        server_.DeleteClientSessions();
        sessions_.clear();
    }

    bool BfdSessionControl(HealthCheckTable::HealthCheckServiceAction action,
                           HealthCheckInstanceService *service);
    HealthCheckInstanceService *
        FindHealthCheckInstanceService(uint32_t interface);
    BfdCommunicator &bfd_communicator() { return communicator_; }

private:
    // map from interface id to health check instance service
    typedef std::map<uint32_t, HealthCheckInstanceService *> Sessions;
    typedef std::pair<uint32_t, HealthCheckInstanceService *> SessionsPair;

    tbb::mutex mutex_; // lock for sessions_ access between health check & BFD
    boost::shared_ptr<PktInfo> msg_;
    BfdHandler handler_;
    Sessions sessions_;

    DISALLOW_COPY_AND_ASSIGN(SegmentHCProto);
};

#endif // vnsw_agent_segment_hc_proto_h_
