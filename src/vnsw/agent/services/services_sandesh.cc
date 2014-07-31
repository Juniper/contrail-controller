/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/assign/list_of.hpp>
#include <pkt/pkt_handler.h>
#include <oper/mirror_table.h>
#include <oper/vn.h>
#include <pkt/pkt_init.h>
#include <services/services_init.h>
#include <services/dhcp_proto.h>
#include <services/arp_proto.h>
#include <services/dns_proto.h>
#include <services/icmp_proto.h>
#include <services/metadata_proxy.h>
#include <services/services_types.h>
#include <services/services_sandesh.h>
#include <vr_defs.h>

std::map<uint16_t, std::string> g_ip_protocol_map =
                    boost::assign::map_list_of<uint16_t, std::string>
                            (1, "icmp")
                            (2, "igmp")
                            (4, "ipv4")
                            (6, "tcp")
                            (17, "udp")
                            (41, "ipv6")
                            (47, "gre");

std::map<uint32_t, std::string> g_dhcp_msg_types =
                    boost::assign::map_list_of<uint32_t, std::string>
                            (DHCP_UNKNOWN, "Unknown")
                            (DHCP_DISCOVER, "Discover")
                            (DHCP_OFFER, "Offer")
                            (DHCP_REQUEST, "Request")
                            (DHCP_ACK, "Ack")
                            (DHCP_NAK, "Nack")
                            (DHCP_INFORM, "Inform")
                            (DHCP_DECLINE, "Decline")
                            (DHCP_RELEASE, "Release")
                            (DHCP_LEASE_QUERY, "Lease Query")
                            (DHCP_LEASE_UNASSIGNED, "Lease Unassigned")
                            (DHCP_LEASE_UNKNOWN, "Lease Unknown")
                            (DHCP_LEASE_ACTIVE, "Lease Active");

void ServicesSandesh::MacToString(const unsigned char *mac, std::string &mac_str) {
    char mstr[32];
    snprintf(mstr, 32, "%02x:%02x:%02x:%02x:%02x:%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    mac_str.assign(mstr);
}

std::string ServicesSandesh::IntToString(uint32_t val) {
    std::ostringstream str;
    str << val;
    return str.str();
}

std::string ServicesSandesh::IntToHexString(uint32_t val) {
    std::ostringstream str;
    str << "0x" << std::hex << val;
    return str.str();
}

void ServicesSandesh::PktToHexString(uint8_t *pkt, int32_t len,
                                     std::string &msg) {
    for (int32_t i = 0; i < len; i++) {
        char val[4];
        snprintf(val, 4, "%02x ", pkt[i]);
        msg.append(val);
    }
}

std::string ServicesSandesh::IpProtocol(uint16_t prot) {
    std::map<uint16_t, std::string>::iterator it = g_ip_protocol_map.find(prot);
    if (it == g_ip_protocol_map.end()) {
        return IntToString(prot);
    }
    return it->second;
}

std::string &ServicesSandesh::DhcpMsgType(uint32_t msg_type) {
    std::map<uint32_t, std::string>::iterator it = g_dhcp_msg_types.find(msg_type);
    if (it == g_dhcp_msg_types.end())
        return DhcpMsgType(DHCP_UNKNOWN);
    return it->second;
}

void ServicesSandesh::PktStatsSandesh(std::string ctxt, bool more) {
    PktStats *resp = new PktStats();
    const PktHandler::PktStats &stats = Agent::GetInstance()->pkt()->pkt_handler()->GetStats();
    uint32_t total_rcvd = 0;
    uint32_t total_sent = 0;
    for (int i = 0; i < PktHandler::MAX_MODULES; ++i) {
        total_rcvd += stats.received[i];
        total_sent += stats.sent[i];
    }
    resp->set_total_rcvd(total_rcvd);
    resp->set_dhcp_rcvd(stats.received[PktHandler::DHCP]);
    resp->set_arp_rcvd(stats.received[PktHandler::ARP]);
    resp->set_dns_rcvd(stats.received[PktHandler::DNS]);
    resp->set_icmp_rcvd(stats.received[PktHandler::ICMP]);
    resp->set_flow_rcvd(stats.received[PktHandler::FLOW]);
    resp->set_dropped(stats.dropped);
    resp->set_total_sent(total_sent);
    resp->set_dhcp_sent(stats.sent[PktHandler::DHCP]);
    resp->set_arp_sent(stats.sent[PktHandler::ARP]);
    resp->set_dns_sent(stats.sent[PktHandler::DNS]);
    resp->set_icmp_sent(stats.sent[PktHandler::ICMP]);
    resp->set_dhcp_q_threshold_exceeded(stats.q_threshold_exceeded[PktHandler::DHCP]);
    resp->set_arp_q_threshold_exceeded(stats.q_threshold_exceeded[PktHandler::ARP]);
    resp->set_dns_q_threshold_exceeded(stats.q_threshold_exceeded[PktHandler::DNS]);
    resp->set_icmp_q_threshold_exceeded(stats.q_threshold_exceeded[PktHandler::ICMP]);
    resp->set_flow_q_threshold_exceeded(stats.q_threshold_exceeded[PktHandler::FLOW]);
    resp->set_context(ctxt);
    resp->set_more(more);
    resp->Response();
}

void ServicesSandesh::DhcpStatsSandesh(std::string ctxt, bool more) {
    DhcpStats *dhcp = new DhcpStats();
    const DhcpProto::DhcpStats &dstats =
                Agent::GetInstance()->GetDhcpProto()->GetStats();
    dhcp->set_dhcp_discover(dstats.discover);
    dhcp->set_dhcp_request(dstats.request);
    dhcp->set_dhcp_inform(dstats.inform);
    dhcp->set_dhcp_decline(dstats.decline);
    dhcp->set_dhcp_other(dstats.other);
    dhcp->set_dhcp_errors(dstats.errors);
    dhcp->set_offers_sent(dstats.offers);
    dhcp->set_acks_sent(dstats.acks);
    dhcp->set_nacks_sent(dstats.nacks);
    dhcp->set_relay_request(dstats.relay_req);
    dhcp->set_relay_response(dstats.relay_resp);
    dhcp->set_context(ctxt);
    dhcp->set_more(more);
    dhcp->Response();
}

void ServicesSandesh::ArpStatsSandesh(std::string ctxt, bool more) {
    ArpProto *arp_proto = Agent::GetInstance()->GetArpProto();
    ArpStats *arp = new ArpStats();
    const ArpProto::ArpStats &astats = arp_proto->GetStats();
    arp->set_arp_entries(arp_proto->GetArpCacheSize());
    arp->set_arp_requests(astats.arp_req);
    arp->set_arp_replies(astats.arp_replies);
    arp->set_arp_gratuitous(astats.arp_gratuitous);
    arp->set_arp_resolved(astats.resolved);
    arp->set_arp_max_retries_exceeded(astats.max_retries_exceeded);
    arp->set_arp_errors(astats.errors);
    arp->set_arp_invalid_packets(astats.arp_invalid_packets);
    arp->set_arp_invalid_interface(astats.arp_invalid_interface);
    arp->set_arp_invalid_vrf(astats.arp_invalid_vrf);
    arp->set_arp_invalid_address(astats.arp_invalid_address);
    arp->set_context(ctxt);
    arp->set_more(more);
    arp->Response();
}

void ServicesSandesh::DnsStatsSandesh(std::string ctxt, bool more) {
    DnsStats *dns = new DnsStats();
    const DnsProto::DnsStats &nstats = Agent::GetInstance()->GetDnsProto()->GetStats();
    dns->set_dns_requests(nstats.requests);
    dns->set_dns_resolved(nstats.resolved);
    dns->set_dns_retransmit_reqs(nstats.retransmit_reqs);
    dns->set_dns_unsupported(nstats.unsupported);
    dns->set_dns_failures(nstats.fail);
    dns->set_dns_drops(nstats.drop);
    dns->set_context(ctxt);
    dns->set_more(more);
    dns->Response();
}

void ServicesSandesh::IcmpStatsSandesh(std::string ctxt, bool more) {
    IcmpStats *icmp = new IcmpStats();
    const IcmpProto::IcmpStats &istats = Agent::GetInstance()->GetIcmpProto()->GetStats();
    icmp->set_icmp_gw_ping(istats.icmp_gw_ping);
    icmp->set_icmp_gw_ping_err(istats.icmp_gw_ping_err);
    icmp->set_icmp_drop(istats.icmp_drop);
    icmp->set_context(ctxt);
    icmp->set_more(more);
    icmp->Response();
}

void ServicesSandesh::FillPktData(PktTrace::Pkt &pkt, PktData &resp) {
    switch (pkt.dir) {
        case PktTrace::In:
            resp.direction = "in";
            break;
        case PktTrace::Out:
            resp.direction = "out";
            break;
        default:
            resp.direction = "error";
            break;
    }
    resp.len = pkt.len;
}

uint16_t ServicesSandesh::FillVrouterHdr(PktTrace::Pkt &pkt, VrouterHdr &resp) {
    boost::array<std::string, MAX_AGENT_HDR_COMMANDS> commands =
           { { "switch", "route", "arp", "l2-protocol", "trap-nexthop",
               "trap-resolve", "trap-flow-miss", "trap-l3-protocol",
               "trap-diag", "trap-ecmp-resolve" } };
    uint8_t *ptr = pkt.pkt;
    AgentHdr *hdr = reinterpret_cast<AgentHdr *>(ptr);
    resp.ifindex = ntohs(hdr->ifindex);
    resp.vrf = ntohs(hdr->vrf);
    uint16_t cmd = ntohs(hdr->cmd);
    if (cmd < MAX_AGENT_HDR_COMMANDS)
        resp.cmd = commands.at(cmd);
    else
        resp.cmd = "unknown";
    resp.cmd_param = ntohl(hdr->cmd_param);
    resp.nh = ntohl(hdr->nh);
    return sizeof(AgentHdr);
}

void ServicesSandesh::FillMacHdr(ether_header *eth, MacHdr &resp) {
    MacToString(eth->ether_dhost, resp.dest_mac);
    MacToString(eth->ether_shost, resp.src_mac);
    uint16_t type = ntohs(eth->ether_type);
    resp.type = (type == ETHERTYPE_IP) ? "ip" :
                 (type == ETHERTYPE_ARP) ? "arp" : IntToString(type);
}

static uint32_t get_val(void *data) {
    union {
        uint8_t data[sizeof(uint32_t)];
        uint32_t addr;
    } bytes;
    memcpy(bytes.data, data, sizeof(uint32_t));
    return ntohl(bytes.addr);
}

void ServicesSandesh::FillArpHdr(ether_arp *arp, ArpHdr &resp) {
    uint16_t val = ntohs(arp->arp_hrd);
    resp.htype = (val == 1) ? "ethernet" : IntToString(val);
    val = ntohs(arp->arp_pro);
    resp.protocol = (val == 0x800) ? "ip" : IntToString(val);
    resp.hw_size = arp->arp_hln;
    resp.prot_size = arp->arp_pln;
    val = ntohs(arp->arp_op);
    resp.opcode = (val == 1) ? "request" :
                  (val == 2) ? "response" : IntToString(val);
    MacToString(arp->arp_sha, resp.sender_mac);
    MacToString(arp->arp_tha, resp.target_mac);
    Ip4Address spa(get_val(arp->arp_spa));
    Ip4Address tpa(get_val(arp->arp_tpa));
    resp.sender_ip = spa.to_string();
    resp.target_ip = tpa.to_string();
}

void ServicesSandesh::FillIpv4Hdr(ip *ip, Ipv4Hdr &resp) {
    resp.vers = ip->ip_v;
    resp.hdrlen = ip->ip_hl;
    resp.tos = ip->ip_tos;
    resp.len = ntohs(ip->ip_len);
    resp.id = IntToHexString(ntohs(ip->ip_id));
    resp.frag = IntToHexString(ntohs(ip->ip_off));
    resp.ttl = ip->ip_ttl;
    resp.protocol = IpProtocol(ip->ip_p);
    resp.csum = IntToHexString(ntohs(ip->ip_sum));
    Ip4Address sa(ntohl(ip->ip_src.s_addr));
    Ip4Address da(ntohl(ip->ip_dst.s_addr));
    resp.src_ip = sa.to_string();
    resp.dest_ip = da.to_string();
}

void ServicesSandesh::FillIcmpv4Hdr(icmp *icmp, Icmpv4Hdr &resp) {
    resp.type = (icmp->icmp_type == ICMP_ECHO) ? "echo request":
                 (icmp->icmp_type == ICMP_ECHOREPLY) ? "echo reply" :
                 IntToString(icmp->icmp_type);
    resp.code = icmp->icmp_code;
    resp.csum = IntToHexString(ntohs(icmp->icmp_cksum));
}


void ServicesSandesh::FillUdpHdr(udphdr *udp, UdpHdr &resp) {
    resp.src_port = ntohs(udp->uh_sport);
    resp.dest_port = ntohs(udp->uh_dport);
    resp.length = ntohs(udp->uh_ulen);
    resp.csum = IntToHexString(ntohs(udp->uh_sum));
}

std::string
ServicesSandesh::FillOptionString(char *data, int32_t len, std::string msg) {
    std::string str;
    if (len > 0)
        str.assign(data, len);
    return (msg + str + "; ");
}

std::string
ServicesSandesh::FillOptionInteger(uint32_t data, std::string msg) {
    std::stringstream str;
    str << msg << data << "; ";
    return str.str();
}

std::string
ServicesSandesh::FillOptionIp(uint32_t data, std::string msg) {
    Ip4Address addr(data);
    return msg + addr.to_string() + "; ";
}

void
ServicesSandesh::FillHostRoutes(uint8_t *data, int len,
                                std::string &resp) {
    std::stringstream routes;
    int parsed = 0;
    uint8_t *ptr = data;
    while (parsed < len) {
        uint32_t addr = 0;
        int plen = *ptr++;
        parsed++;
        int j;
        for (j = 0; plen && j <= (plen - 1) / 8; ++j) {
            addr = (addr << 8) | *ptr++;
            parsed++;
        }
        addr = addr << (4 - j) * 8;
        Ip4Address prefix(addr);
        Ip4Address route(get_val(ptr));
        routes << prefix.to_string() << "/" << plen << " -> "
               << route.to_string() << ";";
        ptr += 4;
        parsed += 4;
    }
    resp += routes.str();
}

void ServicesSandesh::FillDhcpOptions(DhcpOptions *opt, std::string &resp,
                                      std::string &other, int32_t len) {
    while (opt->code != DHCP_OPTION_END && len > 0) {
        switch (opt->code) {
            case DHCP_OPTION_PAD:
                len -= 1;
                opt = (DhcpOptions *)((uint8_t *)opt + 1);
                continue;

            case DHCP_OPTION_MSG_TYPE:
                if (len >= (2 + opt->len))
                    resp += DhcpMsgType(*(uint8_t *)opt->data) + "; ";
                break;

            case DHCP_OPTION_REQ_IP_ADDRESS: {
                if (len >= (2 + opt->len)) {
                    Ip4Address addr(get_val(opt->data));
                    resp += "Requested IP : " + addr.to_string() + "; ";
                }
                break;
            }

            case DHCP_OPTION_HOST_NAME:
                if (len >= (2 + opt->len))
                    resp += FillOptionString((char *)opt->data, opt->len,
                                             "Host Name : ");
                break;

            case DHCP_OPTION_DOMAIN_NAME:
                if (len >= (2 + opt->len))
                    resp += FillOptionString((char *)opt->data, opt->len,
                                             "Domain Name : ");
                break;

            case DHCP_OPTION_MESSAGE:
                if (len >= (2 + opt->len))
                    resp += FillOptionString((char *)opt->data, opt->len,
                                             "Msg : ");
                break;

            case DHCP_OPTION_IP_LEASE_TIME:
                if (len >= (2 + opt->len))
                    resp += FillOptionInteger(get_val(opt->data), "Lease time : ");
                break;

            case DHCP_OPTION_SUBNET_MASK:
                if (len >= (2 + opt->len))
                    resp += FillOptionIp(get_val(opt->data), "Subnet mask : ");
                break;

            case DHCP_OPTION_BCAST_ADDRESS:
                if (len >= (2 + opt->len))
                    resp += FillOptionIp(get_val(opt->data), "Broadcast : ");
                break;

            case DHCP_OPTION_ROUTER:
                if (len >= (2 + opt->len))
                    resp += FillOptionIp(get_val(opt->data), "Gateway : ");
                break;

            case DHCP_OPTION_DNS:
                if (len >= (2 + opt->len))
                    resp += FillOptionIp(get_val(opt->data), "DNS : ");
                break;

            case DHCP_OPTION_TIME_SERVER:
                if (len >= (2 + opt->len))
                    resp += FillOptionIp(get_val(opt->data), "Time Server : ");
                break;

            case DHCP_OPTION_SERVER_IDENTIFIER:
                if (len >= (2 + opt->len))
                    resp += FillOptionIp(get_val(opt->data), "Server : ");
                break;

            case DHCP_OPTION_CLASSLESS_ROUTE:
                if (len >= (2 + opt->len)) {
                    resp += "Host Routes : ";
                    FillHostRoutes(opt->data, opt->len, resp);
                }
                break;

            default:
                PktToHexString((uint8_t *)opt, opt->len + 2, other);
                break;
        }
        len -= (2 + opt->len);
        opt = (DhcpOptions *)((uint8_t *)opt + 2 + opt->len);
    }
}

void ServicesSandesh::FillDhcpv4Hdr(dhcphdr *dhcp, Dhcpv4Hdr &resp,
                                    int32_t len) {
    resp.op = (dhcp->op == 1) ? "request" : "reply";
    resp.htype = (dhcp->htype == 1) ? "ethernet" : IntToString(dhcp->htype);
    resp.hlen = dhcp->hlen;
    resp.hops = dhcp->hops;
    resp.xid = IntToHexString(dhcp->xid);
    resp.secs = ntohs(dhcp->secs);
    resp.flags =
        (ntohs(dhcp->flags) & DHCP_BCAST_FLAG) ? "broadcast" : "unicast";
    Ip4Address ciaddr(ntohl(dhcp->ciaddr));
    Ip4Address yiaddr(ntohl(dhcp->yiaddr));
    Ip4Address siaddr(ntohl(dhcp->siaddr));
    Ip4Address giaddr(ntohl(dhcp->giaddr));
    resp.ciaddr = ciaddr.to_string();
    resp.yiaddr = yiaddr.to_string();
    resp.siaddr = siaddr.to_string();
    resp.giaddr = giaddr.to_string();
    PktToHexString(dhcp->chaddr, DHCP_CHADDR_LEN, resp.chaddr);
    PktToHexString(dhcp->sname, DHCP_NAME_LEN, resp.sname);
    PktToHexString(dhcp->file, DHCP_FILE_LEN, resp.file);
    PktToHexString(dhcp->options, 4, resp.cookie);
    len -= (DHCP_FIXED_LEN + 4);
    FillDhcpOptions((DhcpOptions *)(dhcp->options + 4),
                    resp.dhcp_options, resp.other_options, len);
}

void ServicesSandesh::FillDnsHdr(dnshdr *dns, DnsHdr &resp, int32_t dnslen) {
    if (dnslen < (int32_t)sizeof(dnshdr))
        return;

    resp.xid = ntohs(dns->xid);
    if (dns->flags.rd)
        resp.flags += "recursion desired; ";
    if (dns->flags.trunc)
        resp.flags += "truncated; ";
    if (dns->flags.auth)
        resp.flags += "authoritative answer; ";
    resp.flags += (!dns->flags.op) ? "query; " :
                  ((dns->flags.op == DNS_OPCODE_UPDATE)? "update; " :
                   "other op; ");
    if (dns->flags.req) {
        resp.flags += "response; ";
        if (dns->flags.ret)
            resp.flags += "failure response : " +
                          IntToString(dns->flags.ret) + "; ";
        else
            resp.flags += "success response; ";
    } else {
        resp.flags += "request; ";
    }
    if (dns->flags.cd)
        resp.flags += "checking disabled; ";
    if (dns->flags.ad)
        resp.flags += "answer authenticated; ";
    if (dns->flags.ra)
        resp.flags += "recursion available; ";
    resp.ques = ntohs(dns->ques_rrcount);
    resp.ans = ntohs(dns->ans_rrcount);
    resp.auth = ntohs(dns->auth_rrcount);
    resp.add = ntohs(dns->add_rrcount);
    dnslen -= sizeof(dnshdr);
    ServicesSandesh::PktToHexString((uint8_t *)(dns + 1), dnslen, resp.rest);
}

void ServicesSandesh::ArpPktTrace(PktTrace::Pkt &pkt, ArpPktSandesh *resp) {
    ArpPkt data;
    FillPktData(pkt, data.info);
    uint16_t hdr_len = FillVrouterHdr(pkt, data.agent_hdr);
    uint8_t *ptr = pkt.pkt + hdr_len;
    FillMacHdr((ether_header *)ptr, data.mac_hdr);
    ptr += sizeof(ether_header);
    FillArpHdr((ether_arp *)ptr, data.arp_hdr);
    std::vector<ArpPkt> &list =
        const_cast<std::vector<ArpPkt>&>(resp->get_pkt_list());
    list.push_back(data);
}

void ServicesSandesh::DhcpPktTrace(PktTrace::Pkt &pkt, DhcpPktSandesh *resp) {
    DhcpPkt data;
    FillPktData(pkt, data.info);
    uint16_t hdr_len = FillVrouterHdr(pkt, data.agent_hdr);
    uint8_t *ptr = pkt.pkt + hdr_len;
    FillMacHdr((ether_header*)ptr, data.mac_hdr);
    ptr += sizeof(ether_header);
    FillIpv4Hdr((ip *)ptr, data.ip_hdr);
    ptr += (data.ip_hdr.hdrlen * 4);
    FillUdpHdr((udphdr *)ptr, data.udp_hdr);
    ptr += sizeof(udphdr);
    PktHandler *pkt_handler = Agent::GetInstance()->pkt()->pkt_handler();
    std::size_t trace_size = pkt_handler->PktTraceSize(PktHandler::DHCP);
    int32_t remaining = std::min(pkt.len, trace_size) -
                        (2 * sizeof(ether_header) + sizeof(agent_hdr) +
                         data.ip_hdr.hdrlen * 4 + sizeof(udphdr));
    FillDhcpv4Hdr((dhcphdr *)ptr, data.dhcp_hdr, remaining);
    std::vector<DhcpPkt> &list =
        const_cast<std::vector<DhcpPkt>&>(resp->get_pkt_list());
    list.push_back(data);
}

void ServicesSandesh::DnsPktTrace(PktTrace::Pkt &pkt, DnsPktSandesh *resp) {
    DnsPkt data;
    FillPktData(pkt, data.info);
    uint16_t hdr_len = FillVrouterHdr(pkt, data.agent_hdr);
    uint8_t *ptr = pkt.pkt + hdr_len;
    FillMacHdr((ether_header *)ptr, data.mac_hdr);
    ptr += sizeof(ether_header);
    FillIpv4Hdr((ip *)ptr, data.ip_hdr);
    ptr += (data.ip_hdr.hdrlen * 4);
    FillUdpHdr((udphdr *)ptr, data.udp_hdr);
    ptr += sizeof(udphdr);
    PktHandler *pkt_handler = Agent::GetInstance()->pkt()->pkt_handler();
    std::size_t trace_size = pkt_handler->PktTraceSize(PktHandler::DNS);
    int32_t remaining = std::min(pkt.len, trace_size) -
                        (2 * sizeof(ether_header) + sizeof(agent_hdr) +
                         data.ip_hdr.hdrlen * 4 + sizeof(udphdr));
    FillDnsHdr((dnshdr *)ptr, data.dns_hdr, remaining);
    std::vector<DnsPkt> &list =
        const_cast<std::vector<DnsPkt>&>(resp->get_pkt_list());
    list.push_back(data);
}

void ServicesSandesh::IcmpPktTrace(PktTrace::Pkt &pkt, IcmpPktSandesh *resp) {
    IcmpPkt data;
    FillPktData(pkt, data.info);
    uint16_t hdr_len = FillVrouterHdr(pkt, data.agent_hdr);
    uint8_t *ptr = pkt.pkt + hdr_len;
    FillMacHdr((ether_header *)ptr, data.mac_hdr);
    ptr += sizeof(ether_header);
    FillIpv4Hdr((ip *)ptr, data.ip_hdr);
    ptr += (data.ip_hdr.hdrlen * 4);
    FillIcmpv4Hdr((icmp *)ptr, data.icmp_hdr);
    std::vector<IcmpPkt> &list =
        const_cast<std::vector<IcmpPkt>&>(resp->get_pkt_list());
    list.push_back(data);
}

void ServicesSandesh::OtherPktTrace(PktTrace::Pkt &pkt, PktSandesh *resp) {
    PktDump data;
    FillPktData(pkt, data.info);
    uint16_t hdr_len = FillVrouterHdr(pkt, data.agent_hdr);
    uint8_t *ptr = pkt.pkt + hdr_len;
    FillMacHdr((ether_header *)ptr, data.mac_hdr);
    ptr += sizeof(ether_header);
    PktHandler *pkt_handler = Agent::GetInstance()->pkt()->pkt_handler();
    std::size_t trace_size = pkt_handler->PktTraceSize(PktHandler::FLOW);
    int32_t remaining = std::min(pkt.len, trace_size) -
                        (2 * sizeof(ether_header) + sizeof(agent_hdr));
    PktToHexString(ptr, remaining, data.pkt);
    std::vector<PktDump> &list =
        const_cast<std::vector<PktDump>&>(resp->get_pkt_list());
    list.push_back(data);
}

void ServicesSandesh::HandleRequest(PktHandler::PktModuleName mod,
                                    std::string ctxt, bool more = false) {
    SandeshResponse *resp = NULL;
    boost::function<void(PktTrace::Pkt &)> cb;
    boost::array<std::string, PktHandler::MAX_MODULES> names =
        { { "Invalid", "Flow", "Arp", "Dhcp", "Dns", "Icmp", "Diagnostics" } };

    switch (mod) {
        case PktHandler::ARP:
            resp = new ArpPktSandesh();
            ArpStatsSandesh(ctxt, true);
            static_cast<ArpPktSandesh *>(resp)->set_type(names.at(mod));
            cb = boost::bind(&ServicesSandesh::ArpPktTrace, this, _1,
                             static_cast<ArpPktSandesh *>(resp));
            break;

        case PktHandler::DHCP:
            resp = new DhcpPktSandesh();
            DhcpStatsSandesh(ctxt, true);
            static_cast<DhcpPktSandesh *>(resp)->set_type(names.at(mod));
            cb = boost::bind(&ServicesSandesh::DhcpPktTrace, this, _1,
                             static_cast<DhcpPktSandesh *>(resp));
            break;

        case PktHandler::DNS:
            resp = new DnsPktSandesh();
            DnsStatsSandesh(ctxt, true);
            static_cast<DnsPktSandesh *>(resp)->set_type(names.at(mod));
            cb = boost::bind(&ServicesSandesh::DnsPktTrace, this, _1,
                             static_cast<DnsPktSandesh *>(resp));
            break;

        case PktHandler::ICMP:
            resp = new IcmpPktSandesh();
            IcmpStatsSandesh(ctxt, true);
            static_cast<IcmpPktSandesh *>(resp)->set_type(names.at(mod));
            cb = boost::bind(&ServicesSandesh::IcmpPktTrace, this, _1,
                             static_cast<IcmpPktSandesh *>(resp));
            break;

        case PktHandler::FLOW:
        case PktHandler::DIAG:
        case PktHandler::INVALID:
            resp = new PktSandesh();
            static_cast<PktSandesh *>(resp)->set_type(names.at(mod));
            cb = boost::bind(&ServicesSandesh::OtherPktTrace, this, _1,
                             static_cast<PktSandesh *>(resp));
            break;

        default:
            break;
    }

    Agent::GetInstance()->pkt()->pkt_handler()->PktTraceIterate(mod, cb);
    resp->set_context(ctxt);
    resp->set_more(more);
    resp->Response();
}

void ServicesSandesh::MetadataHandleRequest(std::string ctxt, bool more = false) {
    MetadataResponse *resp = new MetadataResponse();
    resp->set_metadata_server_port(
          Agent::GetInstance()->metadata_server_port());
    const MetadataProxy::MetadataStats &stats =
          Agent::GetInstance()->services()->metadataproxy()->metadatastats();
    resp->set_metadata_requests(stats.requests);
    resp->set_metadata_responses(stats.responses);
    resp->set_metadata_proxy_sessions(stats.proxy_sessions);
    resp->set_metadata_internal_errors(stats.internal_errors);
    resp->set_context(ctxt);
    resp->set_more(more);
    resp->Response();
}

///////////////////////////////////////////////////////////////////////////////

void DhcpInfo::HandleRequest() const {
    ServicesSandesh ssandesh;
    ssandesh.HandleRequest(PktHandler::DHCP, context());
}

void ArpInfo::HandleRequest() const {
    ServicesSandesh ssandesh;
    ssandesh.HandleRequest(PktHandler::ARP, context());
}

void DnsInfo::HandleRequest() const {
    ServicesSandesh ssandesh;
    ssandesh.HandleRequest(PktHandler::DNS, context());
}

void IcmpInfo::HandleRequest() const {
    ServicesSandesh ssandesh;
    ssandesh.HandleRequest(PktHandler::ICMP, context());
}

void MetadataInfo::HandleRequest() const {
    ServicesSandesh ssandesh;
    ssandesh.MetadataHandleRequest(context());
}

void ShowAllInfo::HandleRequest() const {
    ServicesSandesh ssandesh;
    ssandesh.PktStatsSandesh(context(), true);
    ssandesh.HandleRequest(PktHandler::DHCP, context(), true);
    ssandesh.HandleRequest(PktHandler::ARP, context(), true);
    ssandesh.HandleRequest(PktHandler::DNS, context(), true);
    ssandesh.HandleRequest(PktHandler::ICMP, context(), true);
    ssandesh.HandleRequest(PktHandler::FLOW, context(), true);
    ssandesh.HandleRequest(PktHandler::DIAG, context(), true);
    ssandesh.HandleRequest(PktHandler::INVALID, context(), true);
    ssandesh.MetadataHandleRequest(context(), false);
}

void ClearAllInfo::HandleRequest() const {
    Agent *agent = Agent::GetInstance();
    PktHandler *pkt_handler = agent->pkt()->pkt_handler();
    pkt_handler->PktTraceClear(PktHandler::DHCP);
    pkt_handler->PktTraceClear(PktHandler::ARP);
    pkt_handler->PktTraceClear(PktHandler::DNS);
    pkt_handler->PktTraceClear(PktHandler::ICMP);
    pkt_handler->PktTraceClear(PktHandler::FLOW);
    pkt_handler->PktTraceClear(PktHandler::DIAG);
    pkt_handler->PktTraceClear(PktHandler::INVALID);
    pkt_handler->ClearStats();
    agent->GetDhcpProto()->ClearStats();
    agent->GetArpProto()->ClearStats();
    agent->GetDnsProto()->ClearStats();
    agent->GetIcmpProto()->ClearStats();
    agent->services()->metadataproxy()->ClearStats();

    PktErrorResp *resp = new PktErrorResp();
    resp->set_context(context());
    resp->Response();
}

void PktTraceInfo::HandleRequest() const {
    uint32_t buffers = get_num_buffers();
    uint32_t flow_buffers = get_flow_num_buffers();
    PktTraceInfoResponse *resp = new PktTraceInfoResponse();
    PktHandler *handler = Agent::GetInstance()->pkt()->pkt_handler();
    if (buffers > PktTrace::kPktMaxNumBuffers ||
        flow_buffers > PktTrace::kPktMaxNumBuffers) {
        resp->set_resp("Invalid Input !!");
        buffers = handler->PktTraceBuffers(PktHandler::INVALID);
        flow_buffers = handler->PktTraceBuffers(PktHandler::FLOW);
    } else {
        handler->PktTraceBuffers(PktHandler::FLOW, flow_buffers);
        for (uint32_t i = 0; i < PktHandler::MAX_MODULES; ++i) {
            if (i == PktHandler::FLOW)
                continue;
            handler->PktTraceBuffers((PktHandler::PktModuleName)i, buffers);
        }
    }
    resp->set_num_buffers(buffers);
    resp->set_flow_num_buffers(flow_buffers);
    resp->set_context(context());
    resp->Response();
}

///////////////////////////////////////////////////////////////////////////////

void ShowDnsEntries::HandleRequest() const {
    AgentDnsEntries *resp = new AgentDnsEntries();
    std::vector<VmDnsSandesh> dns_list;
    const DnsProto::DnsUpdateSet &dns_update_set =
                    Agent::GetInstance()->GetDnsProto()->update_set();
    for (DnsProto::DnsUpdateSet::const_iterator it = dns_update_set.begin();
         it != dns_update_set.end(); ++it) {
        VmDnsSandesh vmdata;
        vmdata.vm = (*it)->itf->name();
        vmdata.is_floating = ((*it)->floatingIp ? "yes" : "no");
        if ((*it)->xmpp_data) {
            vmdata.virtual_dns = (*it)->xmpp_data->virtual_dns;
            vmdata.zone = (*it)->xmpp_data->zone;
            for (DnsItems::iterator item = (*it)->xmpp_data->items.begin();
                 item != (*it)->xmpp_data->items.end(); ++item) {
                VmDnsRecord record;
                record.name = (*item).name;
                record.type = BindUtil::DnsType((*item).type);
                record.data = (*item).data;
                record.ttl = (*item).ttl;
                record.eclass = BindUtil::DnsClass((*item).eclass);
                vmdata.records.push_back(record);
            }
        }
        dns_list.push_back(vmdata);
    }
    resp->set_dns_list(dns_list);
    resp->set_context(context());
    resp->Response();
}

void VmVdnsListReq::HandleRequest() const {
    VmVdnsListResponse *resp = new VmVdnsListResponse();
    resp->set_context(context());

    const DnsProto::VmDataMap& vm_list = Agent::GetInstance()->GetDnsProto()->all_vms();
    std::vector<VmVdnsListEntry> &list =
        const_cast<std::vector<VmVdnsListEntry>&>(resp->get_rlist());
    DnsProto::VmDataMap::const_iterator it = vm_list.begin();
    while (it != vm_list.end()) {
        const VmInterface *itf = it->first;
        VmVdnsListEntry entry;
        entry.set_name(itf->name());
        entry.set_vm_interface_index(itf->id());
        list.push_back(entry);
        ++it;
    }
    resp->Response();
}

void VmVdnsDataReq::HandleRequest() const {
    VmVdnsDataResponse *resp = new VmVdnsDataResponse();
    resp->set_context(context());

    Agent *agent = Agent::GetInstance();
    const VmInterface *vmi = static_cast<const VmInterface *>
        (agent->interface_table()->FindInterface(get_vm_interface_index()));
    if (vmi == NULL) {
        resp->Response();
        return;
    }
    const DnsProto::VmDataMap& all_vms = agent->GetDnsProto()->all_vms();
    DnsProto::VmDataMap::const_iterator it = all_vms.find(vmi);
    if (it == all_vms.end()) {
        resp->Response();
        return;
    }
    const DnsProto::IpVdnsMap &ip_list = it->second;
    DnsProto::IpVdnsMap::const_iterator ip_it = ip_list.begin();
    std::vector<VmVdnsDataEntry> &list =
        const_cast<std::vector<VmVdnsDataEntry>&>(resp->get_rlist());
    while (ip_it != ip_list.end()) {
        VmVdnsDataEntry entry;
        Ip4Address ip4(ip_it->first);
        entry.set_ip(ip4.to_string());
        entry.set_vdns_name(ip_it->second);
        list.push_back(entry);
        ++ip_it;
    }
    resp->Response();
}

void FipVdnsDataReq::HandleRequest() const {
    FipVdnsDataResponse *resp = new FipVdnsDataResponse();
    resp->set_context(context());
    const DnsProto::DnsFipSet& fip_list = Agent::GetInstance()->GetDnsProto()
                                                                ->fip_list();
    std::vector<FipVdnsEntry> &list =
        const_cast<std::vector<FipVdnsEntry>&>(resp->get_rlist());
    DnsProto::DnsFipSet::const_iterator it = fip_list.begin();
    while (it != fip_list.end()) {
        FipVdnsEntry entry;
        const DnsProto::DnsFipEntry *fip = (*it).get();
        Ip4Address ip4(fip->floating_ip_);
        entry.set_vn(fip->vn_->GetName());
        entry.set_ip(ip4.to_string());
        entry.set_vm_interface(fip->interface_->name());
        entry.set_vdns_name(fip->vdns_name_);
        list.push_back(entry);
        ++it;
    }

    resp->Response();
}

///////////////////////////////////////////////////////////////////////////////

void ShowArpCache::HandleRequest() const {
    ArpCacheResp *resp = new ArpCacheResp();
    resp->set_context(context());
    ArpSandesh *arp_sandesh = new ArpSandesh(resp);
    const ArpProto::ArpCache &cache =
              (Agent::GetInstance()->GetArpProto()->arp_cache());
    for (ArpProto::ArpCache::const_iterator it = cache.begin();
         it != cache.end(); it++) {
        if (!arp_sandesh->SetArpEntry(it->first, it->second))
            break;
    }
    arp_sandesh->Response();
    delete arp_sandesh;
}

bool ArpSandesh::SetArpEntry(const ArpKey &key, const ArpEntry *entry) {
    ArpCacheResp *vresp = static_cast<ArpCacheResp *>(resp_);
    ArpSandeshData data;
    boost::asio::ip::address_v4 ip(key.ip);
    data.set_ip(ip.to_string());
    data.set_vrf(key.vrf->GetName());
    std::string mac_str;
    ServicesSandesh::MacToString(entry->mac_address(), mac_str);
    data.set_mac(mac_str);
    switch (entry->state()) {
        case ArpEntry::INITING:
            data.set_state("initing");
            break;
        case ArpEntry::RESOLVING:
            data.set_state("resolving");
            break;
        case ArpEntry::ACTIVE:
            data.set_state("active");
            break;
        case ArpEntry::RERESOLVING:
            data.set_state("re-resolving");
            break;
    }

    std::vector<ArpSandeshData> &list =
                const_cast<std::vector<ArpSandeshData>&>(vresp->get_arp_list());
    list.push_back(data);
    iter_++;
    if (!(iter_ % ArpSandesh::entries_per_sandesh)) {
        // send partial sandesh
        ArpCacheResp *resp = new ArpCacheResp();
        resp->set_context(resp_->context());
        resp_->set_context(resp_->context());
        resp_->set_more(true);
        resp_->Response();
        resp_ = resp;
    }
    return true;
}
