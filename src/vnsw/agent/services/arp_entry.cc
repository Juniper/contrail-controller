/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "services/arp_proto.h"
#include "services/services_sandesh.h"
#include "services/services_init.h"

ArpEntry::ArpEntry(boost::asio::io_service &io, ArpHandler *handler,
                   in_addr_t ip, const VrfEntry *vrf, State state)
    : key_(ip, vrf), state_(state), retry_count_(0), handler_(handler),
      arp_timer_(NULL) {
    memset(mac_address_, 0, ETH_ALEN);
    arp_timer_ = TimerManager::CreateTimer(io, "Arp Entry timer");
}

ArpEntry::~ArpEntry() {
    arp_timer_->Cancel();
    TimerManager::DeleteTimer(arp_timer_);
}

bool ArpEntry::HandleArpRequest() {
    if (IsResolved())
        UpdateNhDBEntry(DBRequest::DB_ENTRY_ADD_CHANGE, true);
    else if (state_ & ArpEntry::INITING) {
        state_ = ArpEntry::RESOLVING;
        SendArpRequest();
        UpdateNhDBEntry(DBRequest::DB_ENTRY_ADD_CHANGE, false);
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
            arp_proto->StatsResolved();
        state_ = ArpEntry::ACTIVE;
        StartTimer(arp_proto->aging_timeout(), ArpHandler::AGING_TIMER_EXPIRED);
        UpdateNhDBEntry(DBRequest::DB_ENTRY_ADD_CHANGE, true);
    }
}

void ArpEntry::RetryExpiry() {
    if (state_ & ArpEntry::ACTIVE)
        return;
    ArpProto *arp_proto = handler_->agent()->GetArpProto();
    if (retry_count_ < arp_proto->max_retries()) {
        retry_count_++;
        SendArpRequest();
    } else {
        UpdateNhDBEntry(DBRequest::DB_ENTRY_DELETE);

        // keep retrying till Arp NH is deleted
        retry_count_ = 0;
        SendArpRequest();
        arp_proto->StatsMaxRetries();

        Ip4Address ip(key_.ip);
        if (state_ == ArpEntry::RERESOLVING) {
            ARP_TRACE(Trace, "Retry", ip.to_string(), key_.vrf->GetName(), "");
        } else {
            ARP_TRACE(Trace, "Retry exceeded", ip.to_string(), 
                      key_.vrf->GetName(), "");
        }
    }
}

void ArpEntry::AgingExpiry() {
    state_ = ArpEntry::RERESOLVING;
    SendArpRequest();
}

void ArpEntry::SendGraciousArp() {
    const unsigned char zero_mac[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    Agent *agent = handler_->agent();
    ArpProto *arp_proto = agent->GetArpProto();
    if (agent->GetRouterIdConfigured()) {
        handler_->SendArp(ARPOP_REQUEST, arp_proto->ip_fabric_interface_mac(),
                          agent->GetRouterId().to_ulong(), zero_mac,
                          agent->GetRouterId().to_ulong(),
                          arp_proto->ip_fabric_interface_index(), 
                          key_.vrf->GetVrfId());
    }

    if (retry_count_ == ArpProto::kGratRetries) {
        // Retaining this entry till arp module is deleted
        // arp_proto->DelGraciousArpEntry();
        return;
    }

    retry_count_++;
    StartTimer(ArpProto::kGratRetryTimeout, ArpHandler::GRACIOUS_TIMER_EXPIRED);
}

void ArpEntry::Delete() {
    UpdateNhDBEntry(DBRequest::DB_ENTRY_DELETE);
}

bool ArpEntry::IsResolved() {
    return (state_ & (ArpEntry::ACTIVE | ArpEntry::RERESOLVING));
}
void ArpEntry::StartTimer(uint32_t timeout, ArpHandler::ArpMsgType mtype) {
    arp_timer_->Cancel();
    arp_timer_->Start(timeout, boost::bind(&ArpProto::TimerExpiry, 
                                           handler_->agent()->GetArpProto(),
                                           key_, mtype)); 
}

void ArpEntry::SendArpRequest() {
    Agent *agent = handler_->agent();
    ArpProto *arp_proto = agent->GetArpProto();
    uint16_t itf_index = arp_proto->ip_fabric_interface_index();
    const unsigned char *smac = arp_proto->ip_fabric_interface_mac();
    Ip4Address ip = agent->GetRouterId();

    handler_->SendArp(ARPOP_REQUEST, smac, ip.to_ulong(), 
                      mac_address_, key_.ip, itf_index, 
                      agent->GetVrfTable()->FindVrfFromName(
                          agent->GetDefaultVrf())->GetVrfId());

    StartTimer(arp_proto->retry_timeout(), ArpHandler::RETRY_TIMER_EXPIRED);
}

void ArpEntry::UpdateNhDBEntry(DBRequest::DBOperation op, bool resolved) {
    Ip4Address ip(key_.ip);
    struct ether_addr mac;
    memcpy(mac.ether_addr_octet, mac_address_, ETH_ALEN);
    ArpProto *arp_proto = handler_->agent()->GetArpProto();
    Interface *itf = arp_proto->ip_fabric_interface();
    if (key_.vrf->GetName() == handler_->agent()->GetLinkLocalVrfName()) {
        // Do not squash existing route entry.
        // UpdateArp should be smarter and not replace an existing route.
        return;
    }
    arp_proto->UpdateArp(ip, mac, key_.vrf->GetName(),
                         *itf, op, resolved);
}
