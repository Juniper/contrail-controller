/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#include "cmn/agent_cmn.h"
#include "services/dhcpv6_proto.h"
#include "services/services_types.h"
#include "services/services_init.h"
#include "pkt/pkt_init.h"

using namespace boost::asio;
using boost::asio::ip::udp;

Dhcpv6Proto::Dhcpv6Proto(Agent *agent, boost::asio::io_service &io,
                         bool run_with_vrouter) :
    Proto(agent, "Agent::Services", PktHandler::DHCPV6, io),
    run_with_vrouter_(run_with_vrouter) {
    // server duid based on vrrp mac
    server_duid_.type = htons(DHCPV6_DUID_TYPE_LL);
    server_duid_.hw_type = 0;
    agent->vrrp_mac().ToArray(server_duid_.mac, sizeof(server_duid_.mac));
}

Dhcpv6Proto::~Dhcpv6Proto() {
}

ProtoHandler *Dhcpv6Proto::AllocProtoHandler(boost::shared_ptr<PktInfo> info,
                                             boost::asio::io_service &io) {
    return new Dhcpv6Handler(agent(), info, io);
}

void Dhcpv6Proto::Shutdown() {
}
