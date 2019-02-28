/*
 * Copyright (c) 2019 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_router_vnswif_listener_base_nix_h
#define vnsw_agent_router_vnswif_listener_base_nix_h

#include <boost/asio.hpp>
#include "vnswif_listener_base.h"

namespace local = boost::asio::local;

class VnswInterfaceListenerBaseNix : public VnswInterfaceListenerBase {
public:
    VnswInterfaceListenerBaseNix(Agent *agent);
    virtual ~VnswInterfaceListenerBaseNix();

    void Init();
    void Shutdown();
    virtual void UpdateLinkLocalRouteAndCount(const Ip4Address &addr, uint8_t plen, bool del_rt);

protected:
    virtual int CreateSocket() = 0;
    virtual void SyncCurrentState() = 0;
    virtual void RegisterAsyncReadHandler() = 0;
    virtual void UpdateLinkLocalRoute(const Ip4Address &addr, uint8_t plen,
                                      bool del_rt) = 0;

    int sock_fd_;
    local::datagram_protocol::socket sock_;
};

#endif //vnsw_agent_router_vnswif_listener_base_nix_h