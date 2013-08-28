/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/logging.h"
#include "sandesh/sandesh_types.h"
#include "sandesh/sandesh.h"
#include "oper/nexthop.h"
#include "oper/tunnel_nh.h"
#include "oper/mirror_table.h"
#include "oper/inet4_route.h"
#include "ksync/ksync_index.h"
#include "ksync/interface_ksync.h"
#include "services/arp_proto.h"
#include "services/services_sandesh.h"
#include "services_init.h"

template<>
Proto<ArpHandler> *Proto<ArpHandler>::instance_ = NULL;
DBTableBase::ListenerId ArpProto::vid_ = DBTableBase::kInvalidId;
DBTableBase::ListenerId ArpProto::iid_ = DBTableBase::kInvalidId;
uint16_t ArpHandler::max_retries_ = 8;
uint32_t ArpHandler::retry_timeout_ = 2000;    // milli seconds
uint32_t ArpHandler::aging_timeout_ = (5 * 60 * 1000);  // milli seconds; 5min
uint16_t ArpHandler::ip_fabric_intf_index_ = -1;
unsigned char ArpHandler::ip_fabric_intf_mac_[MAC_ALEN];
Interface *ArpHandler::ip_fabric_intf_ = NULL;
ArpHandler::ArpStats ArpHandler::arp_stats_;
Cache<ArpKey, ArpEntry *> ArpHandler::arp_cache_;
ArpEntry *ArpProto::gracious_arp_entry_ = NULL;

ArpNHClient* ArpNHClient::singleton_; 
DBTableBase::ListenerId ArpNHClient::listener_id_ = DBTableBase::kInvalidId;

#define ARP_TRACE(obj, ...)                                                 \
do {                                                                        \
    Arp##obj::TraceMsg(ArpTraceBuf, __FILE__, __LINE__, ##__VA_ARGS__);     \
} while (false)                                                             \

static const unsigned char bcast_mac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
static const unsigned char zero_mac[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

void ArpProto::Init(boost::asio::io_service &io, bool run_with_vrouter) {
    assert(Proto<ArpHandler>::instance_ == NULL);
    Proto<ArpHandler>::instance_ = new ArpProto(io, run_with_vrouter);
    VrfTable *vrf_table = Agent::GetVrfTable();
    vid_ = vrf_table->Register(boost::bind(&ArpProto::VrfUpdate, 
                               GetInstance(), _1, _2));
    InterfaceTable *itf_table = Agent::GetInterfaceTable();
    iid_ = itf_table->Register(boost::bind(&ArpProto::ItfUpdate,
                                           GetInstance(), _2));
    ArpNHClient::Init(io);
}

void ArpProto::VrfUpdate(DBTablePartBase *part, DBEntryBase *entry) {
    VrfEntry *vrf = static_cast<VrfEntry *>(entry);
    ArpVrfState *state;

    state = static_cast<ArpVrfState *>(entry->GetState(part->parent(), vid_)); 
    if (entry->IsDeleted()) {
        ArpHandler::SendArpIpc(ArpHandler::VRF_DELETE, 0, vrf);
        if (state) {
            vrf->GetInet4UcRouteTable()->Unregister(fabric_route_table_listener_);
            entry->ClearState(part->parent(), vid_);
            delete state;
        }
        return;
    }

    if (!state && vrf->GetName() == Agent::GetDefaultVrf()) {
        state = new ArpVrfState;
        //Set state to seen
        state->seen_ = true;
        fabric_route_table_listener_ = vrf->GetInet4UcRouteTable()->
            Register(boost::bind(&ArpProto::RouteUpdate, GetInstance(),  _1, _2));
        entry->SetState(part->parent(), vid_, state);
    }
}

void ArpProto::RouteUpdate(DBTablePartBase *part, DBEntryBase *entry) {
    Inet4UcRoute *route = static_cast<Inet4UcRoute *>(entry);
    ArpRouteState *state;

    state = static_cast<ArpRouteState *>
        (entry->GetState(part->parent(), fabric_route_table_listener_));

    if (entry->IsDeleted()) {
        if (state) {
            if (ArpProto::GraciousArpEntry() && ArpProto::GraciousArpEntry()->Key().ip ==
                    route->GetIpAddress().to_ulong()) {
                ArpProto::DelGraciousArpEntry();
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
        ArpProto::DelGraciousArpEntry();
        //Send Grat ARP
        ArpHandler::SendArpIpc(ArpHandler::ARP_SEND_GRACIOUS, 
                               route->GetIpAddress().to_ulong(), 
                               route->GetVrfEntry());
    }
}

void ArpProto::Shutdown() {
    ArpNHClient::Shutdown();
    delete Proto<ArpHandler>::instance_;
    Proto<ArpHandler>::instance_ = NULL;
    ArpHandler::Shutdown();
}

void ArpProto::ItfUpdate(DBEntryBase *entry) {
    Interface *itf = static_cast<Interface *>(entry);
    if (entry->IsDeleted()) {
        if (itf->GetType() == Interface::ETH && 
            itf->GetName() == Agent::GetIpFabricItfName()) {
            ArpHandler::SendArpIpc(ArpHandler::ITF_DELETE, 0, itf->GetVrf());
            //assert(0);
        }
    } else {
        if (itf->GetType() == Interface::ETH && 
            itf->GetName() == Agent::GetIpFabricItfName()) {
            ArpHandler::IPFabricIntf(itf);
            ArpHandler::IPFabricIntfIndex(itf->GetInterfaceId());
            if (run_with_vrouter_) {
                ArpHandler::IPFabricIntfMac
                    ((char *)itf->GetMacAddr().ether_addr_octet);
            } else {
                char mac[MAC_ALEN];
                memset(mac, 0, MAC_ALEN);
                ArpHandler::IPFabricIntfMac(mac);
            }
        }
    }
}

void ArpProto::TimerExpiry(ArpKey &key, ArpHandler::ArpMsgType timer_type,
                           const boost::system::error_code &ec) {
    if (!ec) 
        ArpHandler::SendArpIpc(timer_type, key);
}

void ArpProto::DelGraciousArpEntry() {
    if (gracious_arp_entry_) {
        delete gracious_arp_entry_;
        gracious_arp_entry_ = NULL;
    }
}

bool ArpHandler::Run() {
    // Process ARP only when the IP Fabric interface is configured
    if (ArpHandler::IPFabricIntf() == NULL)
        return true;

    switch(pkt_info_->type) {
        case PktType::MESSAGE:
            return HandleMessage();

        default:
            return HandlePacket();
    }
}

bool ArpHandler::HandlePacket() {
    uint16_t arp_cmd;
    if (pkt_info_->ip) {
        arp_tpa_ = ntohl(pkt_info_->ip->daddr);
        arp_cmd = ARPOP_REQUEST;
        ArpHandler::StatsPktsDropped();
    } else if (pkt_info_->arp) {
        arp_ = pkt_info_->arp;
        if ((ntohs(arp_->ea_hdr.ar_hrd) != ARPHRD_ETHER) || 
            (ntohs(arp_->ea_hdr.ar_pro) != 0x800) ||
            (arp_->ea_hdr.ar_hln != MAC_ALEN) || 
            (arp_->ea_hdr.ar_pln != IPv4_ALEN)) {
            ArpHandler::StatsErrors();
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
        if (arp_tpa_ == Agent::GetRouterId().to_ulong()) {
            ArpHandler::StatsGracious();
            return true;
        }

        if (arp_tpa_ == ntohl(inet_addr(GW_IP_ADDR))) {
            SendGwArpReply();
            return true;
        }

        if (tpa == spa) {
            arp_cmd = GRATUITOUS_ARP;
        }
    } else {
        ArpHandler::StatsPktsDropped();
        ARP_TRACE(Error, "ARP : Received Invalid packet");
        return true;
    }

    const Interface *itf = InterfaceTable::FindInterface(GetIntf());
    if (!itf) {
        ArpHandler::StatsErrors();
        ARP_TRACE(Error, "Received ARP packet with invalid interface index");
        return true;
    }
    const VrfEntry *vrf = itf->GetVrf();
    if (!vrf) {
        ArpHandler::StatsErrors();
        ARP_TRACE(Error, "ARP : Interface " + itf->GetName() + " has no VRF");
        return true;
    }

    // if broadcast ip, return
    if (arp_tpa_ == 0xFFFFFFFF) {
        ArpHandler::StatsErrors();
        ARP_TRACE(Error, "ARP : ignoring broadcast address");
        return true;
    }

    //Look for subnet broadcast
    Ip4Address arp_addr(arp_tpa_);
    Inet4Route *route = vrf->GetInet4UcRouteTable()->FindLPM(arp_addr);
    if (route) {
        if (route->IsSbcast()) {
            ArpHandler::StatsErrors();
            ARP_TRACE(Error, "ARP : ignoring broadcast address");
            return true;
        }

        uint8_t plen = route->GetPlen();
        uint32_t mask = (plen == 32) ? 0xFFFFFFFF : (0xFFFFFFFF >> plen);
        if (!(arp_tpa_ & mask) || !(arp_tpa_)) {
            ArpHandler::StatsErrors();
            ARP_TRACE(Error, "ARP : ignoring invalid address");
            return true;
        }
    }

    ArpKey key(arp_tpa_, vrf);
    ArpEntry *entry = arp_cache_.Find(key);

    switch (arp_cmd) {
        case ARPOP_REQUEST: {
            ArpHandler::StatsArpReq();
            if (entry) {
                entry->HandleArpRequest();
                return true;
            } else {
                entry = new ArpEntry(io_, this, arp_tpa_, vrf);
                arp_cache_.Add(entry->Key(), entry);
                delete[] pkt_info_->pkt;
                pkt_info_->pkt = NULL;
                entry->HandleArpRequest();
                return false;
            }
        }

        case ARPOP_REPLY:  {
            ArpHandler::StatsArpReplies();
            if (entry) {
                entry->HandleArpReply(arp_->arp_sha);
            }
            return true;
        }

        case GRATUITOUS_ARP: {
            ArpHandler::StatsGracious();
            if (entry) {
                entry->HandleArpReply(arp_->arp_sha);
                return true;
            } else {
                entry = new ArpEntry(io_, this, arp_tpa_, vrf);
                entry->HandleArpReply(arp_->arp_sha);
                arp_cache_.Add(entry->Key(), entry);
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
    switch(pkt_info_->ipc->cmd) {
        case VRF_DELETE: {
            arp_cache_.Iterate(boost::bind(&ArpHandler::OnVrfDelete, 
                                           this, _2, ipc->key.vrf));
            for (std::vector<ArpEntry *>::iterator it = arp_del_list_.begin();
                 it != arp_del_list_.end(); it++) {
                (*it)->Delete();
            }
            arp_del_list_.clear();
            break;
        }

        case ITF_DELETE: {
            arp_cache_.Iterate(boost::bind(&ArpHandler::EntryDelete, this, _2));
            ArpHandler::IPFabricIntf(NULL);
            ArpHandler::IPFabricIntfIndex(-1);
            break;
        }

        case ARP_RESOLVE: {
            ArpEntry *entry = arp_cache_.Find(ipc->key);
            if (!entry) {
                entry = new ArpEntry(io_, this, ipc->key.ip, ipc->key.vrf);
                arp_cache_.Add(entry->Key(), entry);
                entry->HandleArpRequest();
                ArpHandler::StatsArpReq();
                ret = false;
            }
            break;
        }

        case RETRY_TIMER_EXPIRED: {
            if (ArpEntry *entry = arp_cache_.Find(ipc->key))
                entry->RetryExpiry();
            break;
        }

        case AGING_TIMER_EXPIRED: {
            if (ArpEntry *entry = arp_cache_.Find(ipc->key))
                entry->AgingExpiry();
            break;
        }

        case ARP_SEND_GRACIOUS: {
            if (!ArpProto::GraciousArpEntry()) {
                ArpProto::GraciousArpEntry(new ArpEntry(io_, this, ipc->key.ip, 
                                           ipc->key.vrf, ArpEntry::ACTIVE));
                ret = false;
            }
            ArpProto::GraciousArpEntry()->SendGraciousArp();
            break;
        }

        case GRACIOUS_TIMER_EXPIRED: {
            if (ArpProto::GraciousArpEntry())
                ArpProto::GraciousArpEntry()->SendGraciousArp();
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
    ArpEntry *entry = arp_cache_.Find(key);
    if (entry) {
        arp_cache_.Remove(key);
        // this request comes when ARP NH is deleted; nothing more to do
        delete entry;
    }
}

uint16_t ArpHandler::ArpHdr(const unsigned char *smac, in_addr_t sip, 
         const unsigned char *tmac, in_addr_t tip, uint16_t op) {
    arp_->ea_hdr.ar_hrd = htons(ARPHRD_ETHER);
    arp_->ea_hdr.ar_pro = htons(0x800);
    arp_->ea_hdr.ar_hln = MAC_ALEN;
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

void ArpHandler::SendGwArpReply() {
    in_addr_t src_ip = ntohl(inet_addr(GW_IP_ADDR));

    // Reuse the incoming buffer
    uint16_t len = 
        ArpHdr(agent_vrrp_mac, src_ip, agent_vrrp_mac, src_ip, ARPOP_REPLY);
    EthHdr(agent_vrrp_mac, pkt_info_->eth->h_source, 0x806);
    len += sizeof(ethhdr);

    Send(len, GetIntf(), pkt_info_->vrf, 0, PktHandler::ARP);
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

    ArpHdr(smac, sip, tmac, tip, op);
    EthHdr(smac, bcast_mac, 0x806);

    Send(MIN_ETH_PKT_LEN, itf, vrf, AGENT_CMD_SWITCH, PktHandler::ARP);
}

void ArpHandler::SendArpIpc(ArpHandler::ArpMsgType type, 
                            in_addr_t ip, const VrfEntry *vrf) {
    ArpIpc *ipc = new ArpIpc(type, ip, vrf);
    PktHandler::GetPktHandler()->SendMessage(PktHandler::ARP, ipc);
}

void ArpHandler::SendArpIpc(ArpHandler::ArpMsgType type, ArpKey &key) {
    ArpIpc *ipc = new ArpIpc(type, key);
    PktHandler::GetPktHandler()->SendMessage(PktHandler::ARP, ipc);
}

void ArpEntry::HandleArpReply(uint8_t *mac) {
    if ((state_ == ArpEntry::RESOLVING) || (state_ == ArpEntry::ACTIVE) ||
        (state_ == ArpEntry::INITING) || (state_ == ArpEntry::RERESOLVING)) {
        boost::system::error_code ec;
        arp_timer_.cancel(ec);
        assert(ec.value() == 0);
        retry_count_ = 0;
        memcpy(mac_, mac, ETHER_ADDR_LEN);
        if (state_ == ArpEntry::RESOLVING)
            ArpHandler::StatsResolved();
        state_ = ArpEntry::ACTIVE;
        arp_timer_.expires_from_now(
                boost::posix_time::millisec(handler_->AgingTimeout()), ec);
        assert(ec.value() == 0);
        arp_timer_.async_wait(boost::bind(&ArpProto::TimerExpiry, 
                              ArpProto::GetInstance(), key_, 
                              ArpHandler::AGING_TIMER_EXPIRED, 
                              boost::asio::placeholders::error));
        UpdateNhDBEntry(DBRequest::DB_ENTRY_ADD_CHANGE, true);
    }
}

void ArpEntry::SendArpRequest() {
    uint16_t itf_index = ArpHandler::IPFabricIntfIndex();
    const unsigned char *smac = ArpHandler::IPFabricIntfMac();
    Ip4Address ip = Agent::GetRouterId();

    handler_->SendArp(ARPOP_REQUEST, smac, ip.to_ulong(), 
                      mac_, key_.ip, itf_index, 
                      Agent::GetVrfTable()->FindVrfFromName(
                          Agent::GetDefaultVrf())->GetVrfId());

    boost::system::error_code ec;
    arp_timer_.expires_from_now(
        boost::posix_time::millisec(handler_->RetryTimeout()), ec);
    assert(ec.value() == 0);
    arp_timer_.async_wait(boost::bind(&ArpProto::TimerExpiry,
                          ArpProto::GetInstance(), key_,
                          ArpHandler::RETRY_TIMER_EXPIRED,
                          boost::asio::placeholders::error));
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
    if (retry_count_ < handler_->MaxRetries()) {
        retry_count_++;
        SendArpRequest();
    } else {
        UpdateNhDBEntry(DBRequest::DB_ENTRY_DELETE);

        // keep retrying till Arp NH is deleted
        retry_count_ = 0;
        SendArpRequest();
        ArpHandler::StatsMaxRetries();

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
    memcpy(mac.ether_addr_octet, mac_, MAC_ALEN);
    Interface *itf = ArpHandler::IPFabricIntf();
    if (key_.vrf->GetName() == Agent::GetLinkLocalVrfName()) {
        // Do not squash existing route entry.
        // UpdateArp should be smarter and not replace an existing route.
        return;
    }
    ArpNHClient::GetArpNHClient()->UpdateArp(
                           ip, mac, key_.vrf->GetName(), *itf, op, resolved);
}

void ArpEntry::SendGraciousArp() {
    if (Agent::GetRouterIdConfigured()) {
        handler_->SendArp(ARPOP_REQUEST, ArpHandler::IPFabricIntfMac(),
                Agent::GetRouterId().to_ulong(), zero_mac,
                Agent::GetRouterId().to_ulong(),
                ArpHandler::IPFabricIntfIndex(), 
                key_.vrf->GetVrfId());
    }

    if (retry_count_ == ArpHandler::grat_retries_) {
        // Retaining this entry till arp module is deleted
        // ArpProto::DelGraciousArpEntry();
        return;
    }

    retry_count_++;
    boost::system::error_code errc;
    arp_timer_.expires_from_now(
        boost::posix_time::millisec(ArpHandler::grat_retry_timeout_), errc);
    assert(errc.value() == 0);
    arp_timer_.async_wait(boost::bind(&ArpProto::TimerExpiry, 
                          ArpProto::GetInstance(), key_, 
                          ArpHandler::GRACIOUS_TIMER_EXPIRED, 
                          boost::asio::placeholders::error));
}

void ArpNHClient::UpdateArp(Ip4Address &ip, struct ether_addr &mac,
                            const string &vrf_name, const Interface &intf,
                            DBRequest::DBOperation op, bool resolved) {
    ArpNH      *arp_nh;
    ArpNHKey   nh_key(vrf_name, ip);
    arp_nh = static_cast<ArpNH *>(Agent::GetNextHopTable()->FindActiveEntry(&nh_key));

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

	Agent::GetDefaultInet4UcRouteTable()->ArpRoute(
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

void ArpNHClient::Init(boost::asio::io_service &io)
{
    singleton_ = new ArpNHClient(io);
    listener_id_ = Agent::GetNextHopTable()->Register
        (boost::bind(&ArpNHClient::Notify, singleton_, _1, _2));
}

void ArpNHClient::Shutdown() {
    Agent::GetNextHopTable()->Unregister(listener_id_);
    listener_id_ = DBTableBase::kInvalidId;
    delete singleton_;
    singleton_ = NULL;
}
