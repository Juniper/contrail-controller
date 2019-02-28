/*
 * Copyright (c) 2019 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/asio.hpp>
#include "vnswif_listener_base.h"

namespace local = boost::asio::local;

class VnswInterfaceListenerBaseNix : public VnswInterfaceListenerBase {
protected:
    virtual int CreateSocket() = 0;
    virtual void SyncCurrentState() = 0;
    virtual void RegisterAsyncReadHandler() = 0;
    virtual void UpdateLinkLocalRoute(const Ip4Address &addr, uint8_t plen,
                                      bool del_rt) = 0;

    void Init();
    void Shutdown();

    int sock_fd_;
    local::datagram_protocol::socket sock_;
public:
    VnswInterfaceListenerBaseNix(Agent *agent);
    virtual ~VnswInterfaceListenerBaseNix();
};
