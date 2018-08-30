/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_igmp_handler_h_
#define vnsw_agent_igmp_handler_h_

#include "pkt/proto_handler.h"
#include "services/multicast/gmp_map/gmp_proto.h"

// IGMP protocol handler
class IgmpHandler : public ProtoHandler {
public:
    IgmpHandler(Agent *agent, boost::shared_ptr<PktInfo> info,
                boost::asio::io_service &io);
    virtual ~IgmpHandler();

    bool Run();

private:
    bool HandleVmIgmpPacket();
    bool CheckPacket() const;
    void SendPacket(const VmInterface *vm_itf, const VrfEntry *vrf,
                const IpAddress& gmp_addr, GmpPacket *packet);

    struct igmp *igmp_;
    uint16_t igmp_len_;

    friend class IgmpProto;

    DISALLOW_COPY_AND_ASSIGN(IgmpHandler);
};

#endif // vnsw_agent_igmp_handler_h_
