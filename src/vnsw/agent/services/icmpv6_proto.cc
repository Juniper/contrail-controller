/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/os.h"
#include <cmn/agent_cmn.h>
#include <pkt/pkt_handler.h>
#include <oper/route_common.h>
#include <services/icmpv6_proto.h>

Icmpv6Proto::Icmpv6Proto(Agent *agent, boost::asio::io_service &io) :
    Proto(agent, "Agent::Services", PktHandler::ICMPV6, io) {
    vn_table_listener_id_ = agent->vn_table()->Register(
                             boost::bind(&Icmpv6Proto::VnNotify, this, _2));
    vrf_table_listener_id_ = agent->vrf_table()->Register(
                             boost::bind(&Icmpv6Proto::VrfNotify, this, _2));
    interface_listener_id_ = agent->interface_table()->Register(
                             boost::bind(&Icmpv6Proto::InterfaceNotify,
                                         this, _2));

    boost::shared_ptr<PktInfo> pkt_info(new PktInfo(NULL));
    routing_advert_handler_.reset(new Icmpv6Handler(agent, pkt_info, io));

    timer_ = TimerManager::CreateTimer(io, "Icmpv6Timer",
             TaskScheduler::GetInstance()->GetTaskId("Agent::Services"),
             PktHandler::ICMPV6);
    timer_->Start(kRouterAdvertTimeout,
                  boost::bind(&Icmpv6Handler::RouterAdvertisement,
                              routing_advert_handler_.get(), this));
}

Icmpv6Proto::~Icmpv6Proto() {
    agent_->vn_table()->Unregister(vn_table_listener_id_);
    agent_->vrf_table()->Unregister(vrf_table_listener_id_);
    agent_->interface_table()->Unregister(interface_listener_id_);
    timer_->Cancel();
    TimerManager::DeleteTimer(timer_);
}

ProtoHandler *Icmpv6Proto::AllocProtoHandler(boost::shared_ptr<PktInfo> info,
                                             boost::asio::io_service &io) {
    return new Icmpv6Handler(agent(), info, io);
}

void Icmpv6Proto::VnNotify(DBEntryBase *entry) {
    if (entry->IsDeleted()) return;

    VnEntry *vn = static_cast<VnEntry *>(entry);
    VrfEntry *vrf = vn->GetVrf();
    if (!vrf) return;

    if (vrf->GetName() == agent_->fabric_vrf_name())
        return;

    if (vn->layer3_forwarding()) {
        boost::system::error_code ec;
        Ip6Address addr = Ip6Address::from_string(IPV6_ALL_ROUTERS_ADDRESS, ec);
        static_cast<InetUnicastAgentRouteTable *>
            (vrf->GetInet6UnicastRouteTable())->AddHostRoute(vrf->GetName(),
                                                             addr, 128,
                                                             vn->GetName());
    }
}

void Icmpv6Proto::VrfNotify(DBEntryBase *entry) {
    VrfEntry *vrf = static_cast<VrfEntry *>(entry);
    if (vrf->GetName() == agent_->fabric_vrf_name())
        return;

    if (entry->IsDeleted()) {
        boost::system::error_code ec;
        Ip6Address addr = Ip6Address::from_string(IPV6_ALL_ROUTERS_ADDRESS, ec);
        static_cast<InetUnicastAgentRouteTable *>
            (vrf->GetInet6UnicastRouteTable())->DeleteReq(agent_->local_peer(),
                                                          vrf->GetName(),
                                                          addr, 128, NULL);
    }
}

void Icmpv6Proto::InterfaceNotify(DBEntryBase *entry) {
    Interface *interface = static_cast<Interface *>(entry);
    if (interface->type() != Interface::VM_INTERFACE)
        return;

    VmInterface *vm_interface = static_cast<VmInterface *>(entry);
    if (interface->IsDeleted())
        vm_interfaces_.erase(vm_interface);
    else
        vm_interfaces_.insert(vm_interface);
}
