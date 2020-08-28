/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_bfd_handler_h_
#define vnsw_agent_bfd_handler_h_

#include <netinet/udp.h>
#include "pkt/proto_handler.h"

class BfdHandler : public ProtoHandler {
public:
    BfdHandler(Agent *agent, boost::shared_ptr<PktInfo> info,
               boost::asio::io_service &io);
    virtual ~BfdHandler();

    bool Run();
    bool HandleReceive();
    void SendPacket(const boost::asio::ip::udp::endpoint &local_endpoint,
                    const boost::asio::ip::udp::endpoint &remote_endpoint,
                    uint32_t interface_id,
                    const boost::asio::mutable_buffer &packet,
                    int packet_length);

private:
    DISALLOW_COPY_AND_ASSIGN(BfdHandler);
};

#endif // vnsw_agent_bfd_handler_h_
