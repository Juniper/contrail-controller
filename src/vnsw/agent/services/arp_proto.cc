/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/logging.h"
#include "sandesh/sandesh_types.h"
#include "sandesh/sandesh.h"
#include "oper/nexthop.h"
#include "oper/tunnel_nh.h"
#include "oper/mirror_table.h"
#include "oper/agent_route.h"
#include "ksync/ksync_index.h"
#include "ksync/interface_ksync.h"
#include "pkt/pkt_init.h"
#include "services/arp_proto.h"
#include "services/services_sandesh.h"
#include "services_init.h"

///////////////////////////////////////////////////////////////////////////////

#define ARP_TRACE(obj, ...)                                                 \
do {                                                                        \
    Arp##obj::TraceMsg(ArpTraceBuf, __FILE__, __LINE__, ##__VA_ARGS__);     \
} while (false)                                                             \

///////////////////////////////////////////////////////////////////////////////

void ArpProto::Init(boost::asio::io_service &io, bool run_with_vrouter) {
}

void ArpProto::Shutdown() {
}

ArpProto::ArpProto(Agent *agent, boost::asio::io_service &io,
                   bool run_with_vrouter) :
    Proto(agent, "Agent::Services", PktHandler::ARP, io),
    run_with_vrouter_(run_with_vrouter), ip_fabric_intf_index_(-1),
    ip_fabric_intf_(NULL), gracious_arp_entry_(NULL), max_retries_(kMaxRetries),
    retry_timeout_(kRetryTimeout), aging_timeout_(kAgingTimeout) {

    arp_nh_client_ = new ArpNHClient(io);
    memset(ip_fabric_intf_mac_, 0, ETH_ALEN);
    vid_ = agent->GetVrfTable()->Register(
                  boost::bind(&ArpProto::VrfUpdate, this, _1, _2));
    iid_ = agent->GetInterfaceTable()->Register(
                  boost::bind(&ArpProto::ItfUpdate, this, _2));
}

ArpProto::~ArpProto() {
    delete arp_nh_client_;
    agent_->GetVrfTable()->Unregister(vid_);
    agent_->GetInterfaceTable()->Unregister(iid_);
    DelGraciousArpEntry();
}

ProtoHandler *ArpProto::AllocProtoHandler(boost::shared_ptr<PktInfo> info,
                                          boost::asio::io_service &io) {
    return new ArpHandler(agent(), info, io);
}

void ArpProto::VrfUpdate(DBTablePartBase *part, DBEntryBase *entry) {
    VrfEntry *vrf = static_cast<VrfEntry *>(entry);
    ArpVrfState *state;

    state = static_cast<ArpVrfState *>(entry->GetState(part->parent(), vid_)); 
    if (entry->IsDeleted()) {
        ArpHandler::SendArpIpc(ArpHandler::VRF_DELETE, 0, vrf);
        if (state) {
            vrf->GetRouteTable(AgentRouteTableAPIS::INET4_UNICAST)->
                Unregister(fabric_route_table_listener_);
            entry->ClearState(part->parent(), vid_);
            delete state;
        }
        return;
    }

    if (!state && vrf->GetName() == agent_->GetDefaultVrf()) {
        state = new ArpVrfState;
        //Set state to seen
        state->seen_ = true;
        fabric_route_table_listener_ = vrf->
            GetRouteTable(AgentRouteTableAPIS::INET4_UNICAST)->
            Register(boost::bind(&ArpProto::RouteUpdate, this,  _1, _2));
        entry->SetState(part->parent(), vid_, state);
    }
}

void ArpProto::RouteUpdate(DBTablePartBase *part, DBEntryBase *entry) {
    Inet4UnicastRouteEntry *route = static_cast<Inet4UnicastRouteEntry *>(entry);
    ArpRouteState *state;

    state = static_cast<ArpRouteState *>
        (entry->GetState(part->parent(), fabric_route_table_listener_));

    if (entry->IsDeleted()) {
        if (state) {
            if (GraciousArpEntry() && GraciousArpEntry()->Key().ip ==
                                      route->GetIpAddress().to_ulong()) {
                DelGraciousArpEntry();
            }
            entry->ClearState(part->parent(), fabric_route_table_listener_);
            delete state;
        }
        return;
    }

    if (!state && route->GetActiveNextHop()->GetType() == NextHop::RECEIVE) {
        state = new ArpRouteState;
        state->seen_ = true;
        entry->SetState(part->parent(), fabric_route_table_listener_, state);
        DelGraciousArpEntry();
        //Send Grat ARP
        ArpHandler::SendArpIpc(ArpHandler::ARP_SEND_GRACIOUS, 
                               route->GetIpAddress().to_ulong(), 
                               route->GetVrfEntry());
    }
}

void ArpProto::ItfUpdate(DBEntryBase *entry) {
    Interface *itf = static_cast<Interface *>(entry);
    if (entry->IsDeleted()) {
        if (itf->type() == Interface::PHYSICAL && 
            itf->name() == agent_->GetIpFabricItfName()) {
            ArpHandler::SendArpIpc(ArpHandler::ITF_DELETE, 0, itf->vrf());
            //assert(0);
        }
    } else {
        if (itf->type() == Interface::PHYSICAL && 
            itf->name() == agent_->GetIpFabricItfName()) {
            IPFabricIntf(itf);
            IPFabricIntfIndex(itf->id());
            if (run_with_vrouter_) {
                IPFabricIntfMac((char *)itf->mac().ether_addr_octet);
            } else {
                char mac[ETH_ALEN];
                memset(mac, 0, ETH_ALEN);
                IPFabricIntfMac(mac);
            }
        }
    }
}

bool ArpProto::TimerExpiry(ArpKey &key, ArpHandler::ArpMsgType timer_type) {
    ArpHandler::SendArpIpc(timer_type, key);
    return false;
}

void ArpProto::DelGraciousArpEntry() {
    if (gracious_arp_entry_) {
        delete gracious_arp_entry_;
        gracious_arp_entry_ = NULL;
    }
}

///////////////////////////////////////////////////////////////////////////////

bool ArpHandler::Run() {
    // Process ARP only when the IP Fabric interface is configured
    if (agent()->GetArpProto()->IPFabricIntf() == NULL)
        return true;

    switch(pkt_info_->type) {
        case PktType::MESSAGE:
            return HandleMessage();

        default:
            return HandlePacket();
    }
}

bool ArpHandler::HandlePacket() {
    ArpProto *arp_proto = agent()->GetArpProto();
    uint16_t arp_cmd;
    if (pkt_info_->ip) {
        arp_tpa_ = ntohl(pkt_info_->ip->daddr);
        arp_cmd = ARPOP_REQUEST;
        arp_proto->StatsPktsDropped();
    } else if (pkt_info_->arp) {
        arp_ = pkt_info_->arp;
        if ((ntohs(arp_->ea_hdr.ar_hrd) != ARPHRD_ETHER) || 
            (ntohs(arp_->ea_hdr.ar_pro) != 0x800) ||
            (arp_->ea_hdr.ar_hln != ETH_ALEN) || 
            (arp_->ea_hdr.ar_pln != IPv4_ALEN)) {
            arp_proto->StatsErrors();
            ARP_TRACE(Error, "Received Invalid ARP packet");
            return true;
        }
        arp_cmd = ntohs(arp_->ea_hdr.ar_op);
        union {
            uint8_t data[sizeof(in_addr_t)];
            in_addr_t addr;
        } bytes;
        memcpy(bytes.data, arp_->arp_tpa, sizeof(in_addr_t));
        in_addr_t tpa = ntohl(bytes.addr);
        memcpy(bytes.data, arp_->arp_spa, sizeof(in_addr_t));
        in_addr_t spa = ntohl(bytes.addr);
        if (arp_cmd == ARPOP_REQUEST)
            arp_tpa_ = tpa;
        else
            arp_tpa_ = spa;
        
        // if it is our own, ignore for now : TODO check also for MAC
        if (arp_tpa_ == agent()->GetRouterId().to_ulong()) {
            arp_proto->StatsGracious();
            return true;
        }

        if (tpa == spa) {
            arp_cmd = GRATUITOUS_ARP;
        }
    } else {
        arp_proto->StatsPktsDropped();
        ARP_TRACE(Error, "ARP : Received Invalid packet");
        return true;
    }

    const Interface *itf = InterfaceTable::GetInstance()->FindInterface(GetIntf());
    if (!itf) {
        arp_proto->StatsErrors();
        ARP_TRACE(Error, "Received ARP packet with invalid interface index");
        return true;
    }
    if (itf->type() == Interface::VM_INTERFACE) {
        const VmInterface *vm_itf = static_cast<const VmInterface *>(itf);
        if (!vm_itf->ipv4_forwarding()) {
            ARP_TRACE(Error, "Received ARP packet on ipv4 disabled interface");
            return true;
        }
    }

    const VrfEntry *vrf = itf->vrf();
    if (!vrf) {
        arp_proto->StatsErrors();
        ARP_TRACE(Error, "ARP : Interface " + itf->name() + " has no VRF");
        return true;
    }

    // if broadcast ip, return
    if (arp_tpa_ == 0xFFFFFFFF) {
        arp_proto->StatsErrors();
        ARP_TRACE(Error, "ARP : ignoring broadcast address");
        return true;
    }

    //Look for subnet broadcast
    Ip4Address arp_addr(arp_tpa_);
    RouteEntry *route = 
        static_cast<Inet4UnicastAgentRouteTable *>(vrf->
            GetRouteTable(AgentRouteTableAPIS::INET4_UNICAST))->
            FindLPM(arp_addr);
    if (route) {
        if (route->IsMulticast()) {
            arp_proto->StatsErrors();
            ARP_TRACE(Error, "ARP : ignoring broadcast address");
            return true;
        }

        Inet4UnicastRouteEntry *uc_rt = 
            static_cast<Inet4UnicastRouteEntry *>(route);
        uint8_t plen = uc_rt->GetPlen();
        uint32_t mask = (plen == 32) ? 0xFFFFFFFF : (0xFFFFFFFF >> plen);
        if (!(arp_tpa_ & mask) || !(arp_tpa_)) {
            arp_proto->StatsErrors();
            ARP_TRACE(Error, "ARP : ignoring invalid address");
            return true;
        }
    }

    ArpKey key(arp_tpa_, vrf);
    ArpEntry *entry = arp_proto->Find(key);

    switch (arp_cmd) {
        case ARPOP_REQUEST: {
            arp_proto->StatsArpReq();
            if (entry) {
                entry->HandleArpRequest();
                return true;
            } else {
                entry = new ArpEntry(io_, this, arp_tpa_, vrf);
                arp_proto->Add(entry->Key(), entry);
                delete[] pkt_info_->pkt;
                pkt_info_->pkt = NULL;
                entry->HandleArpRequest();
                return false;
            }
        }

        case ARPOP_REPLY:  {
            arp_proto->StatsArpReplies();
            if (entry) {
                entry->HandleArpReply(arp_->arp_sha);
            }
            return true;
        }

        case GRATUITOUS_ARP: {
            arp_proto->StatsGracious();
            if (entry) {
                entry->HandleArpReply(arp_->arp_sha);
                return true;
            } else {
                entry = new ArpEntry(io_, this, arp_tpa_, vrf);
                entry->HandleArpReply(arp_->arp_sha);
                arp_proto->Add(entry->Key(), entry);
                delete[] pkt_info_->pkt;
                pkt_info_->pkt = NULL;
                return false;
            }
        }

        default:
             assert(0);
    }
    return true;
}

bool ArpHandler::HandleMessage() {
    bool ret = true;
    ArpIpc *ipc = static_cast<ArpIpc *>(pkt_info_->ipc);
    ArpProto *arp_proto = agent()->GetArpProto();
    switch(pkt_info_->ipc->cmd) {
        case VRF_DELETE: {
            arp_proto->Iterate(boost::bind(
                      &ArpHandler::OnVrfDelete, this, _2, ipc->key.vrf));
            for (std::vector<ArpEntry *>::iterator it = arp_del_list_.begin();
                 it != arp_del_list_.end(); it++) {
                (*it)->Delete();
            }
            arp_del_list_.clear();
            break;
        }

        case ITF_DELETE: {
            arp_proto->Iterate(boost::bind(&ArpHandler::EntryDelete, this, _2));
            arp_proto->IPFabricIntf(NULL);
            arp_proto->IPFabricIntfIndex(-1);
            break;
        }

        case ARP_RESOLVE: {
            ArpEntry *entry = arp_proto->Find(ipc->key);
            if (!entry) {
                entry = new ArpEntry(io_, this, ipc->key.ip, ipc->key.vrf);
                arp_proto->Add(entry->Key(), entry);
                entry->HandleArpRequest();
                arp_proto->StatsArpReq();
                ret = false;
            }
            break;
        }

        case RETRY_TIMER_EXPIRED: {
            if (ArpEntry *entry = arp_proto->Find(ipc->key))
                entry->RetryExpiry();
            break;
        }

        case AGING_TIMER_EXPIRED: {
            if (ArpEntry *entry = arp_proto->Find(ipc->key))
                entry->AgingExpiry();
            break;
        }

        case ARP_SEND_GRACIOUS: {
            if (!arp_proto->GraciousArpEntry()) {
                arp_proto->GraciousArpEntry(new ArpEntry(io_, this, ipc->key.ip,
                                            ipc->key.vrf, ArpEntry::ACTIVE));
                ret = false;
            }
            arp_proto->GraciousArpEntry()->SendGraciousArp();
            break;
        }

        case GRACIOUS_TIMER_EXPIRED: {
            if (arp_proto->GraciousArpEntry())
                arp_proto->GraciousArpEntry()->SendGraciousArp();
            break;
        }

        case ARP_DELETE: {
            EntryDeleteWithKey(ipc->key);
            break;
        }

        default:
            assert(0);
    }
    delete ipc;
    return ret;
}

bool ArpHandler::OnVrfDelete(ArpEntry *&entry, const VrfEntry *vrf) {
    if (entry->Key().vrf == vrf)
        arp_del_list_.push_back(entry);
    return true;
}

bool ArpHandler::EntryDelete(ArpEntry *&entry) {
    entry->Delete();
    return true;
}

void ArpHandler::EntryDeleteWithKey(ArpKey &key) {
    ArpProto *arp_proto = agent()->GetArpProto();
    ArpEntry *entry = arp_proto->Find(key);
    if (entry) {
        arp_proto->Delete(key);
        // this request comes when ARP NH is deleted; nothing more to do
        delete entry;
    }
}

uint16_t ArpHandler::ArpHdr(const unsigned char *smac, in_addr_t sip, 
         const unsigned char *tmac, in_addr_t tip, uint16_t op) {
    arp_->ea_hdr.ar_hrd = htons(ARPHRD_ETHER);
    arp_->ea_hdr.ar_pro = htons(0x800);
    arp_->ea_hdr.ar_hln = ETH_ALEN;
    arp_->ea_hdr.ar_pln = IPv4_ALEN;
    arp_->ea_hdr.ar_op = htons(op);
    memcpy(arp_->arp_sha, smac, ETHER_ADDR_LEN);
    sip = htonl(sip);
    memcpy(arp_->arp_spa, &sip, sizeof(in_addr_t));
    memcpy(arp_->arp_tha, tmac, ETHER_ADDR_LEN);
    tip = htonl(tip);
    memcpy(arp_->arp_tpa, &tip, sizeof(in_addr_t));
    return sizeof(ether_arp);
}

void ArpHandler::SendArp(uint16_t op, const unsigned char *smac, in_addr_t sip, 
                         const unsigned char *tmac, in_addr_t tip, 
                         uint16_t itf, uint16_t vrf) {
    pkt_info_->pkt = new uint8_t[MIN_ETH_PKT_LEN + IPC_HDR_LEN];
    uint8_t *buf = pkt_info_->pkt;
    memset(buf, 0, MIN_ETH_PKT_LEN + IPC_HDR_LEN);
    pkt_info_->eth = (ethhdr *) (buf + sizeof(ethhdr) + sizeof(agent_hdr));
    arp_ = pkt_info_->arp = (ether_arp *) (pkt_info_->eth + 1);
    arp_tpa_ = tip;

    const unsigned char bcast_mac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    ArpHdr(smac, sip, tmac, tip, op);
    EthHdr(smac, bcast_mac, 0x806);

    Send(MIN_ETH_PKT_LEN, itf, vrf, AGENT_CMD_SWITCH, PktHandler::ARP);
}

void ArpHandler::SendArpIpc(ArpHandler::ArpMsgType type, 
                            in_addr_t ip, const VrfEntry *vrf) {
    ArpIpc *ipc = new ArpIpc(type, ip, vrf);
    Agent::GetInstance()->pkt()->pkt_handler()->SendMessage(PktHandler::ARP,
                                                            ipc);
}

void ArpHandler::SendArpIpc(ArpHandler::ArpMsgType type, ArpKey &key) {
    ArpIpc *ipc = new ArpIpc(type, key);
    Agent::GetInstance()->pkt()->pkt_handler()->SendMessage(PktHandler::ARP,
                                                            ipc);
}

///////////////////////////////////////////////////////////////////////////////

void ArpEntry::StartTimer(uint32_t timeout, ArpHandler::ArpMsgType mtype) {
    arp_timer_->Cancel();
    arp_timer_->Start(timeout, boost::bind(&ArpProto::TimerExpiry, 
                                           Agent::GetInstance()->GetArpProto(),
                                           key_, mtype)); 
}

void ArpEntry::HandleArpReply(uint8_t *mac) {
    if ((state_ == ArpEntry::RESOLVING) || (state_ == ArpEntry::ACTIVE) ||
        (state_ == ArpEntry::INITING) || (state_ == ArpEntry::RERESOLVING)) {
        ArpProto *arp_proto = Agent::GetInstance()->GetArpProto();
        arp_timer_->Cancel();
        retry_count_ = 0;
        memcpy(mac_, mac, ETHER_ADDR_LEN);
        if (state_ == ArpEntry::RESOLVING)
            arp_proto->StatsResolved();
        state_ = ArpEntry::ACTIVE;
        StartTimer(arp_proto->AgingTimeout(), ArpHandler::AGING_TIMER_EXPIRED);
        UpdateNhDBEntry(DBRequest::DB_ENTRY_ADD_CHANGE, true);
    }
}

void ArpEntry::SendArpRequest() {
    ArpProto *arp_proto = Agent::GetInstance()->GetArpProto();
    uint16_t itf_index = arp_proto->IPFabricIntfIndex();
    const unsigned char *smac = arp_proto->IPFabricIntfMac();
    Ip4Address ip = Agent::GetInstance()->GetRouterId();

    handler_->SendArp(ARPOP_REQUEST, smac, ip.to_ulong(), 
                      mac_, key_.ip, itf_index, 
                      Agent::GetInstance()->GetVrfTable()->FindVrfFromName(
                          Agent::GetInstance()->GetDefaultVrf())->GetVrfId());

    StartTimer(arp_proto->RetryTimeout(), ArpHandler::RETRY_TIMER_EXPIRED);
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

void ArpEntry::RetryExpiry() {
    if (state_ & ArpEntry::ACTIVE)
        return;
    ArpProto *arp_proto = Agent::GetInstance()->GetArpProto();
    if (retry_count_ < arp_proto->MaxRetries()) {
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

void ArpEntry::UpdateNhDBEntry(DBRequest::DBOperation op, bool resolved) {
    Ip4Address ip(key_.ip);
    struct ether_addr mac;
    memcpy(mac.ether_addr_octet, mac_, ETH_ALEN);
    ArpProto *arp_proto = Agent::GetInstance()->GetArpProto();
    Interface *itf = arp_proto->IPFabricIntf();
    if (key_.vrf->GetName() == Agent::GetInstance()->GetLinkLocalVrfName()) {
        // Do not squash existing route entry.
        // UpdateArp should be smarter and not replace an existing route.
        return;
    }
    arp_proto->GetArpNHClient()->UpdateArp(ip, mac, key_.vrf->GetName(),
                                           *itf, op, resolved);
}

void ArpEntry::SendGraciousArp() {
    const unsigned char zero_mac[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    ArpProto *arp_proto = Agent::GetInstance()->GetArpProto();
    if (Agent::GetInstance()->GetRouterIdConfigured()) {
        handler_->SendArp(ARPOP_REQUEST, arp_proto->IPFabricIntfMac(),
                       Agent::GetInstance()->GetRouterId().to_ulong(), zero_mac,
                       Agent::GetInstance()->GetRouterId().to_ulong(),
                       arp_proto->IPFabricIntfIndex(), 
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

///////////////////////////////////////////////////////////////////////////////

ArpNHClient::ArpNHClient(boost::asio::io_service &io) : io_(io) {
    listener_id_ = Agent::GetInstance()->GetNextHopTable()->Register
                   (boost::bind(&ArpNHClient::Notify, this, _1, _2));
}

ArpNHClient::~ArpNHClient() {
    Agent::GetInstance()->GetNextHopTable()->Unregister(listener_id_);
}

void ArpNHClient::UpdateArp(Ip4Address &ip, struct ether_addr &mac,
                            const string &vrf_name, const Interface &intf,
                            DBRequest::DBOperation op, bool resolved) {
    ArpNH      *arp_nh;
    ArpNHKey   nh_key(vrf_name, ip);
    arp_nh = static_cast<ArpNH *>(Agent::GetInstance()->GetNextHopTable()->FindActiveEntry(&nh_key));

    std::string mac_str;
    ServicesSandesh::MacToString(mac.ether_addr_octet, mac_str);

    switch (op) {
    case DBRequest::DB_ENTRY_ADD_CHANGE: {
        if (arp_nh) {
            if (arp_nh->GetResolveState() && 
                memcmp(&mac, arp_nh->GetMac(), sizeof(mac)) == 0) {
                // MAC address unchanges ignore
                return;
            }
        }

        ARP_TRACE(Trace, "Add", ip.to_string(), vrf_name, mac_str);
        break;
    }
    case DBRequest::DB_ENTRY_DELETE:
        if (!arp_nh)
            return;
        ARP_TRACE(Trace, "Delete", ip.to_string(), vrf_name, mac_str);
        break;

    default:
        assert(0);
    }

	Agent::GetInstance()->GetDefaultInet4UnicastRouteTable()->ArpRoute(
                              op, ip, mac, vrf_name, intf, resolved, 32);
}

void ArpNHClient::HandleArpNHmodify(ArpNH *nh) {
    if (nh->IsDeleted()) {
        ArpHandler::SendArpIpc(ArpHandler::ARP_DELETE,
                               nh->GetIp()->to_ulong(), nh->GetVrf());
    } else if (nh->IsValid() == false) { 
        ArpHandler::SendArpIpc(ArpHandler::ARP_RESOLVE,
                               nh->GetIp()->to_ulong(), nh->GetVrf());
    }
}

void ArpNHClient::Notify(DBTablePartBase *partition, DBEntryBase *e) {
    NextHop *nh = static_cast<NextHop *>(e);

    switch(nh->GetType()) {
    case NextHop::ARP:
        HandleArpNHmodify(static_cast<ArpNH *>(nh));
        break;

    default:
        break;
    }
}

///////////////////////////////////////////////////////////////////////////////
