/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "services/arp_proto.h"
#include "services/services_sandesh.h"
#include "services/services_init.h"
#include "oper/route_common.h"

ArpEntry::ArpEntry(boost::asio::io_service &io, ArpHandler *handler,
                   ArpKey &key, State state)
    : key_(key), state_(state), retry_count_(0), handler_(handler),
      arp_timer_(NULL) {
    memset(mac_address_, 0, ETHER_ADDR_LEN);
    arp_timer_ = TimerManager::CreateTimer(io, "Arp Entry timer",
                 TaskScheduler::GetInstance()->GetTaskId("Agent::Services"),
                 PktHandler::ARP);
}

ArpEntry::~ArpEntry() {
    arp_timer_->Cancel();
    TimerManager::DeleteTimer(arp_timer_);
}

bool ArpEntry::HandleArpRequest() {
    if (IsResolved())
        AddArpRoute(true);
    else {
        AddArpRoute(false);
        if (state_ & ArpEntry::INITING) {
            state_ = ArpEntry::RESOLVING;
            SendArpRequest();
        }
    }
    return true;
}

void ArpEntry::HandleArpReply(uint8_t *mac) {
    if ((state_ == ArpEntry::RESOLVING) || (state_ == ArpEntry::ACTIVE) ||
        (state_ == ArpEntry::INITING) || (state_ == ArpEntry::RERESOLVING)) {
        ArpProto *arp_proto = handler_->agent()->GetArpProto();
        arp_timer_->Cancel();
        retry_count_ = 0;
        memcpy(mac_address_, mac, ETHER_ADDR_LEN);
        if (state_ == ArpEntry::RESOLVING)
            arp_proto->IncrementStatsResolved();
        state_ = ArpEntry::ACTIVE;
        StartTimer(arp_proto->aging_timeout(), ArpProto::AGING_TIMER_EXPIRED);
        AddArpRoute(true);
    }
}

bool ArpEntry::RetryExpiry() {
    if (state_ & ArpEntry::ACTIVE)
        return true;
    ArpProto *arp_proto = handler_->agent()->GetArpProto();
    if (retry_count_ < arp_proto->max_retries()) {
        retry_count_++;
        SendArpRequest();
    } else {
        Ip4Address ip(key_.ip);
        ARP_TRACE(Trace, "Retry exceeded", ip.to_string(), 
                  key_.vrf->GetName(), "");
        arp_proto->IncrementStatsMaxRetries();

        // if Arp NH is not present, let the entry be deleted
        if (DeleteArpRoute())
            return false;

        // keep retrying till Arp NH is deleted
        retry_count_ = 0;
        SendArpRequest();
    }
    return true;
}

bool ArpEntry::AgingExpiry() {
    Ip4Address ip(key_.ip);
    const string& vrf_name = key_.vrf->GetName();
    ArpNHKey nh_key(vrf_name, ip);
    ArpNH *arp_nh = static_cast<ArpNH *>(handler_->agent()->nexthop_table()->
                                         FindActiveEntry(&nh_key));
    if (!arp_nh) {
        // do not re-resolve if Arp NH doesnt exist
        return false;
    }
    state_ = ArpEntry::RERESOLVING;
    SendArpRequest();
    return true;
}

void ArpEntry::SendGratuitousArp() {
    const unsigned char zero_mac[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    Agent *agent = handler_->agent();
    ArpProto *arp_proto = agent->GetArpProto();
    if (agent->router_id_configured()) {
        handler_->SendArp(ARPOP_REQUEST, arp_proto->ip_fabric_interface_mac(),
                          agent->router_id().to_ulong(), zero_mac,
                          agent->router_id().to_ulong(),
                          arp_proto->ip_fabric_interface_index(), 
                          key_.vrf->vrf_id());
    }

    if (retry_count_ == ArpProto::kGratRetries) {
        // Retaining this entry till arp module is deleted
        // arp_proto->DelGratuitousArpEntry();
        return;
    }

    retry_count_++;
    StartTimer(ArpProto::kGratRetryTimeout, ArpProto::GRATUITOUS_TIMER_EXPIRED);
}

bool ArpEntry::IsResolved() {
    return (state_ & (ArpEntry::ACTIVE | ArpEntry::RERESOLVING));
}

void ArpEntry::StartTimer(uint32_t timeout, uint32_t mtype) {
    arp_timer_->Cancel();
    arp_timer_->Start(timeout, boost::bind(&ArpProto::TimerExpiry, 
                                           handler_->agent()->GetArpProto(),
                                           key_, mtype)); 
}

void ArpEntry::SendArpRequest() {
    const unsigned char zero_mac[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    Agent *agent = handler_->agent();
    ArpProto *arp_proto = agent->GetArpProto();
    uint16_t itf_index = arp_proto->ip_fabric_interface_index();
    const unsigned char *smac = arp_proto->ip_fabric_interface_mac();
    Ip4Address ip = agent->router_id();

    VrfEntry *vrf =
        agent->vrf_table()->FindVrfFromName(agent->fabric_vrf_name());
    if (vrf) {
        handler_->SendArp(ARPOP_REQUEST, smac, ip.to_ulong(), 
                          zero_mac, key_.ip, itf_index, vrf->vrf_id());
    }

    StartTimer(arp_proto->retry_timeout(), ArpProto::RETRY_TIMER_EXPIRED);
}

void ArpEntry::AddArpRoute(bool resolved) {
    if (key_.vrf->GetName() == handler_->agent()->linklocal_vrf_name()) {
        // Do not squash existing route entry.
        // should be smarter and not replace an existing route.
        return;
    }

    Ip4Address ip(key_.ip);
    const string& vrf_name = key_.vrf->GetName();
    ArpNHKey nh_key(vrf_name, ip);
    ArpNH *arp_nh = static_cast<ArpNH *>(handler_->agent()->nexthop_table()->
                                         FindActiveEntry(&nh_key));

    struct ether_addr mac;
#if defined(__linux__)
    memcpy(mac.ether_addr_octet, mac_address_, ETH_ALEN);
#elif defined(__FreeBSD__)
    memcpy(mac.octet, mac_address_, ETHER_ADDR_LEN);
#else
#error "Unsupported platform"
#endif
    if (arp_nh && arp_nh->GetResolveState() && 
        memcmp(&mac, arp_nh->GetMac(), sizeof(mac)) == 0) {
        // MAC address unchanged, ignore
        return;
    }

    stringstream mac_string;
    mac_string << ether_ntoa((struct ether_addr *)&mac);
    ARP_TRACE(Trace, "Add", ip.to_string(), vrf_name, mac_string.str());

    Interface *itf = handler_->agent()->GetArpProto()->ip_fabric_interface();
    handler_->agent()->fabric_inet4_unicast_table()->ArpRoute(
                       DBRequest::DB_ENTRY_ADD_CHANGE, ip, mac,
                       vrf_name, *itf, resolved, 32);
}

bool ArpEntry::DeleteArpRoute() {
    if (key_.vrf->GetName() == handler_->agent()->linklocal_vrf_name()) {
        return true;
    }

    Ip4Address ip(key_.ip);
    const string& vrf_name = key_.vrf->GetName();
    ArpNHKey nh_key(vrf_name, ip);
    ArpNH *arp_nh = static_cast<ArpNH *>(handler_->agent()->nexthop_table()->
                                         FindActiveEntry(&nh_key));
    if (!arp_nh)
        return true;

    struct ether_addr mac;
#if defined(__linux__)
    memcpy(mac.ether_addr_octet, mac_address_, ETH_ALEN);
#elif defined(__FreeBSD__)
    memcpy(mac.octet, mac_address_, ETHER_ADDR_LEN);
#else
#error "Unsupported platform"
#endif
    stringstream mac_string;
    mac_string << ether_ntoa((struct ether_addr *)&mac);
    ARP_TRACE(Trace, "Delete", ip.to_string(), vrf_name, mac_string.str());

    Interface *itf = handler_->agent()->GetArpProto()->ip_fabric_interface();
    handler_->agent()->fabric_inet4_unicast_table()->ArpRoute(
                       DBRequest::DB_ENTRY_DELETE, ip, mac, vrf_name,
                       *itf, false, 32);
    return false;
}
