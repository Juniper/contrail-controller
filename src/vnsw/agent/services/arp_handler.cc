/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <stdint.h>
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
        refcount_ = 0;
}

ArpHandler::~ArpHandler() {
}

bool ArpHandler::Run() {
    // Process ARP only when the IP Fabric interface is configured
    assert(agent());
    assert(agent()->GetArpProto());
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

        if (tpa == spa || spa == 0) {
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

    const VrfEntry *vrf = agent()->vrf_table()->FindVrfFromId(pkt_info_->vrf);
    if (!vrf || !vrf->IsActive()) {
        arp_proto->IncrementStatsInvalidVrf();
        ARP_TRACE(Error, "ARP : AgentHdr " + itf->name() +
                         " has no / inactive VRF ");
        return true;
    }
    const VrfEntry *nh_vrf = itf->vrf();
    if (!nh_vrf || !nh_vrf->IsActive()) {
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
        static_cast<InetUnicastAgentRouteTable *>(vrf->
            GetInet4UnicastRouteTable())->FindLPM(arp_addr);
    if (route) {
        if (route->is_multicast()) {
            arp_proto->IncrementStatsInvalidAddress();
            ARP_TRACE(Error, "ARP : ignoring multicast address" +
                      arp_addr.to_string());
            return true;
        }
        if (route->GetActiveNextHop()->GetType() == NextHop::RESOLVE) {
            const ResolveNH *nh =
                static_cast<const ResolveNH *>(route->GetActiveNextHop());
            itf = nh->interface();
            nh_vrf = itf->vrf();
        }
    }

    ArpKey key(arp_tpa_, vrf);
    ArpEntry *entry = arp_proto->FindArpEntry(key);

    switch (arp_cmd) {
        case ARPOP_REQUEST: {
            arp_proto->IncrementStatsArpReq();
            arp_proto->IncrementStatsArpRequest(itf->id());
            if (entry) {
                entry->HandleArpRequest();
                return true;
            } else {
                entry = new ArpEntry(io_, this, key, nh_vrf, ArpEntry::INITING,
                                     itf);
                if (arp_proto->AddArpEntry(entry) == false) {
                    delete entry;
                    return true;
                }
                entry->HandleArpRequest();
                return false;
            }
        }

        case ARPOP_REPLY:  {
            arp_proto->IncrementStatsArpReplies();
            arp_proto->IncrementStatsArpReply(itf->id());
            if (itf->type() == Interface::VM_INTERFACE) {
                uint32_t ip;
                memcpy(&ip, arp_->arp_spa, sizeof(ip));
                ip = ntohl(ip);
                //Enqueue a request to trigger state machine
                agent()->oper_db()->route_preference_module()->
                    EnqueueTrafficSeen(Ip4Address(ip), 32, itf->id(),
                                       vrf->vrf_id(),
                                       MacAddress(arp_->arp_sha));
                if(entry) {
                    entry->HandleArpReply(MacAddress(arp_->arp_sha));
                }
                return true;
            }
            if(entry) {
                entry->HandleArpReply(MacAddress(arp_->arp_sha));
                return true;
            } else { 
                entry = new ArpEntry(io_, this, key, nh_vrf, ArpEntry::INITING,
                                     itf);
                if (arp_proto->AddArpEntry(entry) == false) {
                    delete entry;
                    return true;
                }
                entry->HandleArpReply(MacAddress(arp_->arp_sha));
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
                                       vrf->vrf_id(),
                                       MacAddress(arp_->arp_sha));
                return true;
            } else if (entry) {
                entry->HandleArpReply(MacAddress(arp_->arp_sha));
                return true;
            } else {
                // ignore gratuitous ARP when entry is not present in cache
                return true;
            }
        }

        default:
            ARP_TRACE(Error, "Received Invalid ARP command : " +
                      integerToString(arp_cmd));
            return true;
    }
}

/* This API is invoked from the following paths
   - NextHop notification for ARP_NH
   - ARP Timer expiry
   - Sending Gratituous ARP for Receive Nexthops
   In all these above paths we expect route_vrf and nh_vrf for ArpRoute to
   be same
   */
bool ArpHandler::HandleMessage() {
    bool ret = true;
    ArpProto::ArpIpc *ipc = static_cast<ArpProto::ArpIpc *>(pkt_info_->ipc);
    ArpProto *arp_proto = agent()->GetArpProto();
    switch(pkt_info_->ipc->cmd) {
        case ArpProto::ARP_RESOLVE: {
            ArpEntry *entry = arp_proto->FindArpEntry(ipc->key);
            if (!entry) {
                entry = new ArpEntry(io_, this, ipc->key, ipc->key.vrf,
                                     ArpEntry::INITING, ipc->interface.get());
                if (arp_proto->AddArpEntry(entry) == false) {
                    delete entry;
                    break;
                }
                ret = false;
            }
            arp_proto->IncrementStatsArpReq();
            arp_proto->IncrementStatsArpRequest(ipc->interface->id());
            entry->HandleArpRequest();
            break;
        }

        case ArpProto::ARP_SEND_GRATUITOUS: {
            bool key_valid = false;
            ArpProto::GratuitousArpIterator it =
            arp_proto->GratuitousArpEntryIterator(ipc->key, &key_valid);
            if (key_valid && !ipc->interface->IsDeleted()) {
                ArpEntry *entry = NULL;
                ArpProto::ArpEntrySet::iterator sit = it->second.begin();
                for (; sit != it->second.end(); sit++) {
                    entry = *sit;
                    if (entry->interface() == ipc->interface.get())
                        break;
                }
                if (sit == it->second.end()) {
                    entry = new ArpEntry(io_, this, ipc->key, ipc->key.vrf,
                                         ArpEntry::ACTIVE, ipc->interface.get());
                    it->second.insert(entry);
                    ret = false;
                }
                if (entry)
                    entry->SendGratuitousArp();
                break;
            }
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
            ArpEntry *entry =
                arp_proto->GratuitousArpEntry(ipc->key, ipc->interface.get());
            if (entry && entry->retry_count() <= ArpProto::kGratRetries) {
                entry->SendGratuitousArp();
            } else {
                // Need to validate deleting the Arp entry upon fabric vrf Delete only
                if (ipc->key.vrf->GetName() != agent()->fabric_vrf_name()) {
                    arp_proto->DeleteGratuitousArpEntry(entry);
                }
            }
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

uint16_t ArpHandler::ArpHdr(const MacAddress &smac, in_addr_t sip,
         const MacAddress &tmac, in_addr_t tip, uint16_t op) {
    arp_->ea_hdr.ar_hrd = htons(ARPHRD_ETHER);
    arp_->ea_hdr.ar_pro = htons(0x800);
    arp_->ea_hdr.ar_hln = ETHER_ADDR_LEN;
    arp_->ea_hdr.ar_pln = IPv4_ALEN;
    arp_->ea_hdr.ar_op = htons(op);
    smac.ToArray(arp_->arp_sha, sizeof(arp_->arp_sha));
    sip = htonl(sip);
    memcpy(arp_->arp_spa, &sip, sizeof(in_addr_t));
    tmac.ToArray(arp_->arp_tha, sizeof(arp_->arp_tha));
    tip = htonl(tip);
    memcpy(arp_->arp_tpa, &tip, sizeof(in_addr_t));
    return sizeof(ether_arp);
}

void ArpHandler::SendArp(uint16_t op, const MacAddress &smac, in_addr_t sip,
                         const MacAddress &tmac, const MacAddress &dmac,
                         in_addr_t tip, uint32_t itf, uint32_t vrf) {

    if (pkt_info_->packet_buffer() == NULL) {
        pkt_info_->AllocPacketBuffer(agent(), PktHandler::ARP, ARP_TX_BUFF_LEN,
                                     0);
    }

    char *buf = (char *)pkt_info_->packet_buffer()->data();
    memset(buf, 0, pkt_info_->packet_buffer()->data_len());
    pkt_info_->eth = (struct ether_header *)buf;
    int l2_len = EthHdr(buf, pkt_info_->packet_buffer()->data_len(),
                        itf, smac, dmac, ETHERTYPE_ARP);
    arp_ = pkt_info_->arp = (ether_arp *) (buf + l2_len);
    arp_tpa_ = tip;

    ArpHdr(smac, sip, tmac, tip, op);
    pkt_info_->set_len(l2_len + sizeof(ether_arp));

    Send(itf, vrf, AgentHdr::TX_SWITCH, PktHandler::ARP);
}

void intrusive_ptr_add_ref(const ArpHandler *p) {
    p->refcount_++;
}

void intrusive_ptr_release(const ArpHandler *p) {
    if (p->refcount_.fetch_and_decrement() == 1) {
        delete p;
    }
}
