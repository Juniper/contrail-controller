/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "vr_defs.h"
#include "oper/route_common.h"
#include "oper/operdb_init.h"
#include "oper/path_preference.h"
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
    if (agent()->GetArpProto()->ip_fabric_interface() == NULL) {
        delete pkt_info_->ipc;
        return true;
    }

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
        arp_tpa_ = ntohl(pkt_info_->ip->ip_dst.s_addr);
        arp_cmd = ARPOP_REQUEST;
    } else if (pkt_info_->arp) {
        arp_ = pkt_info_->arp;
        if ((ntohs(arp_->ea_hdr.ar_hrd) != ARPHRD_ETHER) ||
            (ntohs(arp_->ea_hdr.ar_pro) != ETHERTYPE_IP) ||
            (arp_->ea_hdr.ar_hln != ETHER_ADDR_LEN) ||
            (arp_->ea_hdr.ar_pln != IPv4_ALEN)) {
            arp_proto->IncrementStatsInvalidPackets();
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
        if (arp_tpa_ == agent()->router_id().to_ulong()) {
            arp_proto->IncrementStatsGratuitous();
            return true;
        }

        if (tpa == spa) {
            arp_cmd = GRATUITOUS_ARP;
        }
    } else {
        arp_proto->IncrementStatsInvalidPackets();
        ARP_TRACE(Error, "ARP : Received Invalid packet");
        return true;
    }

    const Interface *itf =
        agent()->interface_table()->FindInterface(GetInterfaceIndex());
    if (!itf || !itf->IsActive()) {
        arp_proto->IncrementStatsInvalidInterface();
        ARP_TRACE(Error, "Received ARP packet from invalid / inactive interface");
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
    if (!vrf || !vrf->IsActive()) {
        arp_proto->IncrementStatsInvalidVrf();
        ARP_TRACE(Error, "ARP : Interface " + itf->name() +
                         " has no / inactive VRF");
        return true;
    }

    // if broadcast ip, return
    Ip4Address arp_addr(arp_tpa_);
    if (arp_tpa_ == 0xFFFFFFFF || !arp_tpa_) {
        arp_proto->IncrementStatsInvalidAddress();
        ARP_TRACE(Error, "ARP : ignoring broadcast address" +
                  arp_addr.to_string());
        return true;
    }

    //Look for subnet broadcast
    AgentRoute *route = 
        static_cast<Inet4UnicastAgentRouteTable *>(vrf->
            GetInet4UnicastRouteTable())->FindLPM(arp_addr);
    if (route) {
        if (route->is_multicast()) {
            arp_proto->IncrementStatsInvalidAddress();
            ARP_TRACE(Error, "ARP : ignoring multicast address" +
                      arp_addr.to_string());
            return true;
        }
    }

    ArpKey key(arp_tpa_, vrf);
    ArpEntry *entry = arp_proto->FindArpEntry(key);

    switch (arp_cmd) {
        case ARPOP_REQUEST: {
            arp_proto->IncrementStatsArpReq();
            if (entry) {
                entry->HandleArpRequest();
                return true;
            } else {
                entry = new ArpEntry(io_, this, key, ArpEntry::INITING);
                arp_proto->AddArpEntry(entry);
                delete[] pkt_info_->pkt;
                pkt_info_->pkt = NULL;
                entry->HandleArpRequest();
                return false;
            }
        }

        case ARPOP_REPLY:  {
            arp_proto->IncrementStatsArpReplies();
            if (entry) {
                entry->HandleArpReply(arp_->arp_sha);
                return true;
            } else {
                entry = new ArpEntry(io_, this, key, ArpEntry::INITING);
                arp_proto->AddArpEntry(entry);
                entry->HandleArpReply(arp_->arp_sha);
                delete[] pkt_info_->pkt;
                pkt_info_->pkt = NULL;
                arp_ = NULL;
                return false;
            }
        }

        case GRATUITOUS_ARP: {
            arp_proto->IncrementStatsGratuitous();
            if (itf->type() == Interface::VM_INTERFACE) {
                uint32_t ip;
                memcpy(&ip, arp_->arp_spa, sizeof(ip));
                ip = ntohl(ip);
                //Enqueue a request to trigger state machine
                agent()->oper_db()->route_preference_module()->
                    EnqueueTrafficSeen(Ip4Address(ip), 32, itf->id(),
                                       vrf->vrf_id());
            }

            if (entry) {
                entry->HandleArpReply(arp_->arp_sha);
                return true;
            } else {
                entry = new ArpEntry(io_, this, key, ArpEntry::INITING);
                entry->HandleArpReply(arp_->arp_sha);
                arp_proto->AddArpEntry(entry);
                delete[] pkt_info_->pkt;
                pkt_info_->pkt = NULL;
                arp_ = NULL;
                return false;
            }
        }

        default:
            ARP_TRACE(Error, "Received Invalid ARP command : " +
                      integerToString(arp_cmd));
            return true;
    }
}

bool ArpHandler::HandleMessage() {
    bool ret = true;
    ArpProto::ArpIpc *ipc = static_cast<ArpProto::ArpIpc *>(pkt_info_->ipc);
    ArpProto *arp_proto = agent()->GetArpProto();
    switch(pkt_info_->ipc->cmd) {
        case ArpProto::ARP_RESOLVE: {
            ArpEntry *entry = arp_proto->FindArpEntry(ipc->key);
            if (!entry) {
                entry = new ArpEntry(io_, this, ipc->key, ArpEntry::INITING);
                arp_proto->AddArpEntry(entry);
                ret = false;
            }
            arp_proto->IncrementStatsArpReq();
            entry->HandleArpRequest();
            break;
        }

        case ArpProto::ARP_SEND_GRATUITOUS: {
            if (!arp_proto->gratuitous_arp_entry()) {
                arp_proto->set_gratuitous_arp_entry(
                           new ArpEntry(io_, this, ipc->key, ArpEntry::ACTIVE));
                ret = false;
            }
            arp_proto->gratuitous_arp_entry()->SendGratuitousArp();
            break;
        }

        case ArpProto::ARP_DELETE: {
            EntryDelete(ipc->key);
            break;
        }

        case ArpProto::RETRY_TIMER_EXPIRED: {
            ArpEntry *entry = arp_proto->FindArpEntry(ipc->key);
            if (entry && !entry->RetryExpiry()) {
                arp_proto->DeleteArpEntry(entry);
            }
            break;
        }

        case ArpProto::AGING_TIMER_EXPIRED: {
            ArpEntry *entry = arp_proto->FindArpEntry(ipc->key);
            if (entry && !entry->AgingExpiry()) {
                arp_proto->DeleteArpEntry(entry);
            }
            break;
        }

        case ArpProto::GRATUITOUS_TIMER_EXPIRED: {
           if (arp_proto->gratuitous_arp_entry())
               arp_proto->gratuitous_arp_entry()->SendGratuitousArp();
            break;
        }

        default:
            ARP_TRACE(Error, "Received Invalid internal ARP message : " +
                      integerToString(pkt_info_->ipc->cmd));
            break;
    }
    delete ipc;
    return ret;
}

void ArpHandler::EntryDelete(ArpKey &key) {
    ArpProto *arp_proto = agent()->GetArpProto();
    ArpEntry *entry = arp_proto->FindArpEntry(key);
    if (entry) {
        arp_proto->DeleteArpEntry(entry);
        // this request comes when ARP NH is deleted; nothing more to do
    }
}

uint16_t ArpHandler::ArpHdr(const unsigned char *smac, in_addr_t sip, 
         const unsigned char *tmac, in_addr_t tip, uint16_t op) {
    arp_->ea_hdr.ar_hrd = htons(ARPHRD_ETHER);
    arp_->ea_hdr.ar_pro = htons(0x800);
    arp_->ea_hdr.ar_hln = ETHER_ADDR_LEN;
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
    pkt_info_->eth = (ether_header *) (buf + sizeof(ether_header) + sizeof(agent_hdr));
    arp_ = pkt_info_->arp = (ether_arp *) (pkt_info_->eth + 1);
    arp_tpa_ = tip;

    const unsigned char bcast_mac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    ArpHdr(smac, sip, tmac, tip, op);
    EthHdr(smac, bcast_mac, 0x806);

    Send(sizeof(ether_header) + sizeof(ether_arp), itf, vrf, AGENT_CMD_SWITCH, PktHandler::ARP);
}

