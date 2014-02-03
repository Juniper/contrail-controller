/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_vrf_stats_io_context_h
#define vnsw_vrf_stats_io_context_h

#include <uve/agent_stats_sandesh_context.h>

class VrfStatsIoContext: public IoContext {
public:
    VrfStatsIoContext(int msg_len, char *msg, uint32_t seqno, 
                      AgentStatsSandeshContext *ctx, 
                      IoContext::IoContextWorkQId id) 
        : IoContext(msg, msg_len, seqno, ctx, id) {}
    virtual ~VrfStatsIoContext() {}
    void Handler();
    void ErrorHandler(int err);
private:
    DISALLOW_COPY_AND_ASSIGN(VrfStatsIoContext);
};

#endif //vnsw_vrf_stats_io_context_h
