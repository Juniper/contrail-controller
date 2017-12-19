/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#ifndef windows_vnsw_agent_router_id_h
#define windows_vnsw_agent_router_id_h

#include <cmn/agent.h>

class VnswInterfaceListenerWindows {
public:
    VnswInterfaceListenerWindows(Agent *agent);
    void Init();
    void Shutdown();
private:
    DISALLOW_COPY_AND_ASSIGN(VnswInterfaceListenerWindows);
};

typedef VnswInterfaceListenerWindows VnswInterfaceListener;

#endif /* windows_vnsw_agent_router_id_h */
