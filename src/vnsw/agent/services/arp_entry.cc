/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/os.h"
#include "services/arp_proto.h"
#include "services/services_sandesh.h"
#include "services/services_init.h"
#include "oper/route_common.h"

ArpEntry::ArpEntry(boost::asio::io_service &io, ArpHandler *handler,
                   ArpKey &key, const VrfEntry *vrf, State state,
                   const Interface *itf)
    : io_(io), key_(key), nh_vrf_(vrf), state_(state), retry_count_(0),
      handler_(handler), arp_timer_(NULL), interface_(itf) {
    if (!IsDerived()) {
        arp_timer_ = TimerManager::CreateTimer(io, "Arp Entry timer",
                TaskScheduler::GetInstance()->GetTaskId("Agent::Services"),
                PktHandler::ARP);
    }
}

ArpEntry::~ArpEntry() {
    if (!IsDerived()) {
        arp_timer_->Cancel();
        TimerManager::DeleteTimer(arp_timer_);
    }
    handler_.reset(NULL);
}

void ArpEntry::HandleDerivedArpRequest() {
    ArpProto *arp_proto = handler_->agent()->GetArpProto();
    //Add ArpRoute for Derived entry
    AddArpRoute(IsResolved());

    ArpKey key(key_.ip, nh_vrf_);
    ArpEntry *entry = arp_proto->FindArpEntry(key);
    if (entry) {
        entry->HandleArpRequest();
    } else {
        entry = new ArpEntry(io_, handler_.get(), key, nh_vrf_, ArpEntry::INITING,
                             interface_);
        if (arp_proto->AddArpEntry(entry) == false) {
            delete entry;
            return;
        }
        entry->HandleArpRequest();
    }
}

bool ArpEntry::HandleArpRequest() {
    if (IsDerived()) {
        HandleDerivedArpRequest();
        return true;
    }
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

void ArpEntry::HandleArpReply(const MacAddress &mac) {

    if (IsDerived()) {
        /* We don't expect ARP replies in derived Vrf */
        return;
    }
    if ((state_ == ArpEntry::RESOLVING) || (state_ == ArpEntry::ACTIVE) ||
        (state_ == ArpEntry::INITING) || (state_ == ArpEntry::RERESOLVING)) {
        ArpProto *arp_proto = handler_->agent()->GetArpProto();
        arp_timer_->Cancel();
        retry_count_ = 0;
        mac_address_ = mac;
        if (state_ == ArpEntry::RESOLVING) {
            arp_proto->IncrementStatsResolved();
            arp_proto->IncrementStatsResolved(interface_->id());
        }
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
    ArpNHKey nh_key(vrf_name, ip, false);
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
    Agent *agent = handler_->agent();
    ArpProto *arp_proto = agent->GetArpProto();
    if (agent->router_id_configured()) {
        handler_->SendArp(ARPOP_REQUEST, arp_proto->ip_fabric_interface_mac(),
                          agent->router_id().to_ulong(), MacAddress(),
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

bool ArpEntry::IsDerived() {
    if (key_.vrf != nh_vrf_) {
        return true;
    }
    return false;
}

void ArpEntry::StartTimer(uint32_t timeout, uint32_t mtype) {
    arp_timer_->Cancel();
    arp_timer_->Start(timeout, boost::bind(&ArpProto::TimerExpiry,
                                           handler_->agent()->GetArpProto(),
                                           key_, mtype, interface_));
}

void ArpEntry::SendArpRequest() {
    assert(!IsDerived());
    Agent *agent = handler_->agent();
    ArpProto *arp_proto = agent->GetArpProto();
    uint32_t vrf_id = VrfEntry::kInvalidIndex;
    uint32_t intf_id = arp_proto->ip_fabric_interface_index();
    Ip4Address ip;
    MacAddress smac;
    if (interface_->type() == Interface::VM_INTERFACE) {
        const VmInterface *vmi = static_cast<const VmInterface *>(interface_);
        ip = vmi->GetServiceIp(Ip4Address(key_.ip)).to_v4();
        vrf_id = nh_vrf_->vrf_id();
        if (vmi->parent()) {
            intf_id = vmi->id();
            smac = vmi->parent()->mac();
        }
    } else {
        ip = agent->router_id();
        VrfEntry *vrf =
            agent->vrf_table()->FindVrfFromName(agent->fabric_vrf_name());
        if (vrf) {
            vrf_id = vrf->vrf_id();
        }
        smac = interface_->mac();
    }

    if (vrf_id != VrfEntry::kInvalidIndex) {
        handler_->SendArp(ARPOP_REQUEST, smac, ip.to_ulong(),
                          MacAddress(), key_.ip, intf_id, vrf_id);
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
    ArpNHKey nh_key(nh_vrf_->GetName(), ip, false);
    ArpNH *arp_nh = static_cast<ArpNH *>(handler_->agent()->nexthop_table()->
                                         FindActiveEntry(&nh_key));

    MacAddress mac = mac_address();
    if (arp_nh && arp_nh->GetResolveState() &&
        mac.CompareTo(arp_nh->GetMac()) == 0) {
        // MAC address unchanged, ignore
        if (!IsDerived()) {
            return;
        } else {
            /* Return if the route is already existing */
            InetUnicastRouteKey *rt_key = new InetUnicastRouteKey(
                    handler_->agent()->local_peer(), vrf_name, ip, 32); 
            AgentRoute *entry = key_.vrf->GetInet4UnicastRouteTable()->
                FindActiveEntry(rt_key);
            delete rt_key;
            if (entry) {
                return;
            }
            resolved = true;
        }
    }

    ARP_TRACE(Trace, "Add", ip.to_string(), vrf_name, mac.ToString());
    AgentRoute *entry = key_.vrf->GetInet4UnicastRouteTable()->FindLPM(ip);

    bool policy = false;
    SecurityGroupList sg;
    VnListType vn_list;
    if (entry) {
        policy = entry->GetActiveNextHop()->PolicyEnabled();
        sg = entry->GetActivePath()->sg_list();
        vn_list = entry->GetActivePath()->dest_vn_list();
    }

    const Interface *itf = NULL;
    if (interface_->type() == Interface::VM_INTERFACE) {
        itf = interface_;
    } else {
        itf = handler_->agent()->GetArpProto()->ip_fabric_interface();
    }
    handler_->agent()->fabric_inet4_unicast_table()->ArpRoute(
                       DBRequest::DB_ENTRY_ADD_CHANGE, vrf_name, ip, mac,
                       nh_vrf_->GetName(), *itf, resolved, 32, policy,
                       vn_list, sg);
}

bool ArpEntry::DeleteArpRoute() {
    if (key_.vrf->GetName() == handler_->agent()->linklocal_vrf_name()) {
        return true;
    }

    Ip4Address ip(key_.ip);
    const string& vrf_name = key_.vrf->GetName();
    ArpNHKey nh_key(nh_vrf_->GetName(), ip, false);
    ArpNH *arp_nh = static_cast<ArpNH *>(handler_->agent()->nexthop_table()->
                                         FindActiveEntry(&nh_key));
    if (!arp_nh)
        return true;

    MacAddress mac = mac_address();
    ARP_TRACE(Trace, "Delete", ip.to_string(), vrf_name, mac.ToString());
    if (IsDerived()) {
        //Just enqueue a delete, no need to mark nexthop invalid
        InetUnicastAgentRouteTable::Delete(handler_->agent()->local_peer(),
                                           vrf_name, ip, 32);
        return true;
    }

    handler_->agent()->fabric_inet4_unicast_table()->ArpRoute(
                       DBRequest::DB_ENTRY_DELETE, vrf_name, ip, mac, nh_vrf_->GetName(),
                       *interface_, false, 32, false, Agent::NullStringList(),
                       SecurityGroupList());
    return false;
}

void ArpEntry::Resync(bool policy, const VnListType &vnlist,
                      const SecurityGroupList &sg) {
    Ip4Address ip(key_.ip);
    handler_->agent()->fabric_inet4_unicast_table()->ArpRoute(
                       DBRequest::DB_ENTRY_ADD_CHANGE, key_.vrf->GetName(), ip,
                       mac_address_, nh_vrf_->GetName(), *interface_, IsResolved(),
                       32, policy, vnlist, sg);
}
