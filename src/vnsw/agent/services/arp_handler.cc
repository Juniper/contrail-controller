/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "vr_defs.h"
#include "oper/route_common.h"
#include "pkt/pkt_init.h"
#include "services/arp_proto.h"
#include "services/services_init.h"
#include "services/services_sandesh.h"

ArpHandler::ArpHandler(Agent *agent, boost::shared_ptr<PktInfo> info,
                       boost::asio::io_service &io)
    : ProtoHandler(agent, info, io), arp_(NULL), arp_tpa_(0) {
}

ArpHandler::~ArpHandler() {
}

bool ArpHandler::Run() {
    // Process ARP only when the IP Fabric interface is configured
    if (agent()->GetArpProto()->ip_fabric_interface() == NULL)
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
        
        // if it is our own, ignore
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

    const Interface *itf = agent()->GetInterfaceTable()->FindInterface(GetIntf());
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
    AgentRoute *route = 
        static_cast<Inet4UnicastAgentRouteTable *>(vrf->
            GetInet4UnicastRouteTable())->FindLPM(arp_addr);
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
    ArpEntry *entry = arp_proto->FindArpEntry(key);

    switch (arp_cmd) {
        case ARPOP_REQUEST: {
            arp_proto->StatsArpReq();
            if (entry) {
                entry->HandleArpRequest();
                return true;
            } else {
                entry = new ArpEntry(io_, this, arp_tpa_, vrf);
                arp_proto->AddArpEntry(entry->key(), entry);
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
                return true;
            } else {
                entry = new ArpEntry(io_, this, arp_tpa_, vrf);
                arp_proto->AddArpEntry(entry->key(), entry);
                delete[] pkt_info_->pkt;
                pkt_info_->pkt = NULL;
                entry->HandleArpReply(arp_->arp_sha);
                return false;
            }
        }

        case GRATUITOUS_ARP: {
            arp_proto->StatsGracious();
            if (entry) {
                entry->HandleArpReply(arp_->arp_sha);
                return true;
            } else {
                entry = new ArpEntry(io_, this, arp_tpa_, vrf);
                entry->HandleArpReply(arp_->arp_sha);
                arp_proto->AddArpEntry(entry->key(), entry);
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
    ArpProto::ArpIpc *ipc = static_cast<ArpProto::ArpIpc *>(pkt_info_->ipc);
    ArpProto *arp_proto = agent()->GetArpProto();
    switch(pkt_info_->ipc->cmd) {
        case VRF_DELETE: {
            const ArpProto::ArpCache &cache = arp_proto->arp_cache();
            for (ArpProto::ArpCache::const_iterator it = cache.begin();
                 it != cache.end(); it++) {
                OnVrfDelete(it->second, ipc->key.vrf);
            }
            for (std::vector<ArpEntry *>::iterator it = arp_del_list_.begin();
                 it != arp_del_list_.end(); it++) {
                (*it)->Delete();
            }
            arp_del_list_.clear();
            break;
        }

        case ITF_DELETE: {
            const ArpProto::ArpCache &cache = arp_proto->arp_cache();
            for (ArpProto::ArpCache::const_iterator it = cache.begin();
                 it != cache.end(); it++) {
                it->second->Delete();
            }
            // arp_proto->set_ip_fabric_interface(NULL);
            // arp_proto->set_ip_fabric_interface_index(-1);
            break;
        }

        case ARP_RESOLVE: {
            ArpEntry *entry = arp_proto->FindArpEntry(ipc->key);
            if (!entry) {
                entry = new ArpEntry(io_, this, ipc->key.ip, ipc->key.vrf);
                arp_proto->AddArpEntry(entry->key(), entry);
                entry->HandleArpRequest();
                arp_proto->StatsArpReq();
                ret = false;
            }
            break;
        }

        case RETRY_TIMER_EXPIRED: {
            if (ArpEntry *entry = arp_proto->FindArpEntry(ipc->key))
                entry->RetryExpiry();
            break;
        }

        case AGING_TIMER_EXPIRED: {
            if (ArpEntry *entry = arp_proto->FindArpEntry(ipc->key))
                entry->AgingExpiry();
            break;
        }

        case ARP_SEND_GRACIOUS: {
            if (!arp_proto->gracious_arp_entry()) {
                arp_proto->set_gracious_arp_entry(
                           new ArpEntry(io_, this, ipc->key.ip,
                                        ipc->key.vrf, ArpEntry::ACTIVE));
                ret = false;
            }
            arp_proto->gracious_arp_entry()->SendGraciousArp();
            break;
        }

        case GRACIOUS_TIMER_EXPIRED: {
            if (arp_proto->gracious_arp_entry())
                arp_proto->gracious_arp_entry()->SendGraciousArp();
            break;
        }

        case ARP_DELETE: {
            EntryDelete(ipc->key);
            break;
        }

        default:
            assert(0);
    }
    delete ipc;
    return ret;
}

bool ArpHandler::OnVrfDelete(ArpEntry *entry, const VrfEntry *vrf) {
    if (entry->key().vrf == vrf)
        arp_del_list_.push_back(entry);
    return true;
}

void ArpHandler::EntryDelete(ArpKey &key) {
    ArpProto *arp_proto = agent()->GetArpProto();
    ArpEntry *entry = arp_proto->FindArpEntry(key);
    if (entry) {
        arp_proto->DeleteArpEntry(key);
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

    Send(sizeof(ethhdr) + sizeof(ether_arp), itf, vrf, AGENT_CMD_SWITCH, PktHandler::ARP);
}
