/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_drop_stats_io_context_h
#define vnsw_drop_stats_io_context_h

#include <vrouter/stats_collector/agent_stats_sandesh_context.h>

class DropStatsIoContext: public IoContext {
public:
    DropStatsIoContext(int msg_len, char *msg, uint32_t seqno,
                       AgentStatsSandeshContext *ctx,
                       IoContext::Type type);
    virtual ~DropStatsIoContext();
    virtual void Handler();
    virtual void ErrorHandler(int err);
private:
    DISALLOW_COPY_AND_ASSIGN(DropStatsIoContext);
};

#endif //vnsw_drop_stats_io_context_h
