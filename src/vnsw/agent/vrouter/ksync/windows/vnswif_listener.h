/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#ifndef windows_vnsw_agent_router_id_h
#define windows_vnsw_agent_router_id_h

#include <cmn/agent.h>
#include "vrouter/ksync/vnswif_listener_base.h"
#include <boost/asio.hpp>

class VnswInterfaceListenerWindows : public VnswInterfaceListenerBase {
public:
    VnswInterfaceListenerWindows(Agent *agent);

    virtual int CreateSocket();
    virtual void SyncCurrentState();
    virtual void RegisterAsyncReadHandler();
    void ReadHandler(const boost::system::error_code &, std::size_t length);
    void UpdateLinkLocalRoute(const Ip4Address &addr, uint8_t plen, bool del_rt);
private:
    friend class TestVnswIf;

    DISALLOW_COPY_AND_ASSIGN(VnswInterfaceListenerWindows);
};

typedef VnswInterfaceListenerWindows VnswInterfaceListener;

#endif /* windows_vnsw_agent_router_id_h */
