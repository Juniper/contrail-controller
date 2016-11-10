/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_interface_stats_io_context_h
#define vnsw_interface_stats_io_context_h

#include <vrouter/stats_collector/agent_stats_sandesh_context.h>

class InterfaceStatsIoContext: public IoContext {
public:
    InterfaceStatsIoContext(int msg_len, char *msg, uint32_t seqno,
                       AgentStatsSandeshContext *ctx,
                       IoContext::Type type);
    virtual ~InterfaceStatsIoContext();
    virtual void Handler();
    virtual void ErrorHandler(int err);
private:
    void UpdateMarker();
    DISALLOW_COPY_AND_ASSIGN(InterfaceStatsIoContext);
};

#endif //vnsw_interface_stats_io_context_h
