/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_drop_stats_io_context_h
#define vnsw_drop_stats_io_context_h

#include <uve/agent_stats_sandesh_context.h>

class DropStatsIoContext: public IoContext {
public:
    DropStatsIoContext(int msg_len, char *msg, uint32_t seqno, 
                       AgentStatsSandeshContext *ctx, 
                       IoContext::IoContextWorkQId id);
    virtual ~DropStatsIoContext();
    void Handler();
    void ErrorHandler(int err);
private:
    DISALLOW_COPY_AND_ASSIGN(DropStatsIoContext);
};

#endif //vnsw_drop_stats_io_context_h
