/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_diag_proto_hpp
#define vnsw_agent_diag_proto_hpp

#include <pkt/pkt_handler.h>
#include <pkt/proto.h>
#include <pkt/proto_handler.h>
#include <oper/health_check.h>

class SegmentHealthCheckPkt;
class SegmentHealthCheckPktStatsResp;

class DiagProto : public Proto {
public:
    // map from interface id to segment health check service packet object
    typedef std::map<uint32_t, SegmentHealthCheckPkt *> SessionMap;
    typedef std::pair<uint32_t, SegmentHealthCheckPkt *> SessionPair;

    enum DiagStatsType {
        REQUESTS_SENT,
        REQUESTS_RECEIVED,
        REPLIES_SENT,
        REPLIES_RECEIVED
    };

    struct DiagStats {
        uint64_t requests_sent;
        uint64_t requests_received;
        uint64_t replies_sent;
        uint64_t replies_received;
        DiagStats() { Reset(); }
        void Reset() {
            requests_sent = requests_received = replies_sent = 0;
            replies_received = 0;
        }
    };
    typedef std::map<uint32_t, DiagStats> DiagStatsMap;
    typedef std::pair<uint32_t, DiagStats> DiagStatsPair;

    DiagProto(Agent *agent, boost::asio::io_service &io);
    virtual ~DiagProto() {}

    ProtoHandler *AllocProtoHandler(boost::shared_ptr<PktInfo> info,
                                    boost::asio::io_service &io);
    bool SegmentHealthCheckProcess(
        HealthCheckTable::HealthCheckServiceAction action,
        HealthCheckInstanceService *service);
    void IncrementDiagStats(uint32_t itf_id, DiagStatsType type);
    const DiagStatsMap &stats() const { return stats_; }
    void FillSandeshHealthCheckResponse(SegmentHealthCheckPktStatsResp *resp);

private:
    SessionMap session_map_;
    /* lock for stats_ field for access between
     * health-check (which sends first health check packet),
     * timer-task (which sends periodic health check packets),
     * Diag task (which receives health-check requests/replies and sends
     *            health-check replies)
     * Introspect (which reads stats for filling introspect response)
     */
    tbb::mutex stats_mutex_;
    DiagStatsMap stats_;
    DISALLOW_COPY_AND_ASSIGN(DiagProto);
};

#endif
