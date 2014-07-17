/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_kstate_io_context_h
#define vnsw_agent_kstate_io_context_h

class KStateIoContext: public IoContext {
public:
    KStateIoContext(int msg_len, char *msg, uint32_t seqno, 
                    AgentSandeshContext *obj)
        : IoContext(msg, msg_len, seqno, obj) {}
    void Handler();
    void ErrorHandler(int err);
};

#endif // vnsw_agent_kstate_io_context_h
