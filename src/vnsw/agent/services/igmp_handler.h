/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_igmp_handler_h_
#define vnsw_agent_igmp_handler_h_

#include "pkt/proto_handler.h"

// IGMP protocol handler
class IgmpHandler : public ProtoHandler {
public:
    IgmpHandler(Agent *agent, boost::shared_ptr<PktInfo> info,
                boost::asio::io_service &io);
    virtual ~IgmpHandler();

    bool Run();

private:
    bool CheckPacket();
    void SendMessage(VmInterface *vm_intf);

    struct igmp *igmp_;
    uint16_t igmp_len_;
    DISALLOW_COPY_AND_ASSIGN(IgmpHandler);
};

#endif // vnsw_agent_igmp_handler_h_
