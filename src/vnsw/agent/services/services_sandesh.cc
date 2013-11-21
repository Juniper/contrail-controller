/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/assign/list_of.hpp>
#include <pkt/pkt_handler.h>
#include <oper/mirror_table.h>
#include <services/dhcp_proto.h>
#include <services/arp_proto.h>
#include <services/dns_proto.h>
#include <services/icmp_proto.h>
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

bool ServicesSandesh::ValidateIP(std::string ip) {
    struct sockaddr_in sa;
    int result = inet_pton(AF_INET, ip.c_str(), &(sa.sin_addr));
    return result != 0;
}

bool ServicesSandesh::ValidateMac(std::string mac, unsigned char *mac_addr) {
    unsigned int pos = 0;
    int colon = 0;
    std::string mac_copy = mac;
    while (!(mac.empty()) && (pos = mac.find(':', pos)) != std::string::npos) {
        colon++;
        pos += 1;
    }
    if (colon != 5) 
        return false;
    unsigned int val[MAC_ALEN] = {0, 0, 0, 0, 0, 0};
    sscanf(mac_copy.data(), "%x:%x:%x:%x:%x:%x", &val[0], &val[1], 
           &val[2], &val[3], &val[4], &val[5]);
    for (int i = 0; i < MAC_ALEN; i++)
        mac_addr[i] = val[i];
    return true;
}

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
    PktHandler::PktStats stats = PktHandler::GetPktHandler()->GetStats();
    resp->set_total_rcvd(stats.total_rcvd);
    resp->set_dhcp_rcvd(stats.dhcp_rcvd);
    resp->set_arp_rcvd(stats.arp_rcvd);
    resp->set_dns_rcvd(stats.dns_rcvd);
    resp->set_icmp_rcvd(stats.icmp_rcvd);
    resp->set_flow_rcvd(stats.flow_rcvd);
    resp->set_dropped(stats.dropped);
    resp->set_total_sent(stats.total_sent);
    resp->set_dhcp_sent(stats.dhcp_sent);
    resp->set_arp_sent(stats.arp_sent);
    resp->set_dns_sent(stats.dns_sent);
    resp->set_icmp_sent(stats.icmp_sent);
    resp->set_context(ctxt);
    resp->set_more(more);
    resp->Response();
}

void ServicesSandesh::DhcpStatsSandesh(std::string ctxt, bool more) {
    DhcpStats *dhcp = new DhcpStats();
    DhcpProto::DhcpStats dstats = 
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
    ArpProto::ArpStats astats = arp_proto->GetStats();
    arp->set_arp_entries(arp_proto->GetArpCacheSize());
    arp->set_arp_requests(astats.arp_req);
    arp->set_arp_replies(astats.arp_replies);
    arp->set_arp_gracious(astats.arp_gracious);
    arp->set_arp_resolved(astats.resolved);
    arp->set_arp_max_retries_exceeded(astats.max_retries_exceeded);
    arp->set_arp_errors(astats.errors);
    arp->set_pkts_dropped(astats.pkts_dropped);
    arp->set_context(ctxt);
    arp->set_more(more);
    arp->Response();
}

void ServicesSandesh::DnsStatsSandesh(std::string ctxt, bool more) {
    DnsStats *dns = new DnsStats();
    DnsProto::DnsStats nstats = Agent::GetInstance()->GetDnsProto()->GetStats();
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
    IcmpProto::IcmpStats istats = Agent::GetInstance()->GetIcmpProto()->GetStats();
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

void ServicesSandesh::FillVrouterHdr(PktTrace::Pkt &pkt, VrouterHdr &resp) {
    boost::array<std::string, MAX_AGENT_HDR_COMMANDS> commands = 
           { { "switch", "route", "arp", "l2-protocol", "trap-nexthop",
               "trap-resolve", "trap-flow-miss", "trap-l3-protocol",
               "trap-diag", "trap-ecmp-resolve" } };
    uint8_t *ptr = pkt.pkt;
    ptr += sizeof(ethhdr);   // skip the outer ethernet header
    agent_hdr *hdr = reinterpret_cast<agent_hdr *>(ptr);
    resp.ifindex = ntohs(hdr->hdr_ifindex);
    resp.vrf = ntohs(hdr->hdr_vrf);
    uint16_t cmd = ntohs(hdr->hdr_cmd);
    if (cmd < MAX_AGENT_HDR_COMMANDS)
        resp.cmd = commands.at(cmd);
    else
        resp.cmd = "unknown";
    resp.cmd_param = ntohl(hdr->hdr_cmd_param);
    resp.nh = ntohl(hdr->hdr_nh);
}

void ServicesSandesh::FillMacHdr(ethhdr *eth, MacHdr &resp) {
    MacToString(eth->h_dest, resp.dest_mac);
    MacToString(eth->h_source, resp.src_mac);
    uint16_t type = ntohs(eth->h_proto);
    resp.type = (type == 0x800) ? "ip" : 
                 (type == 0x806) ? "arp" : IntToString(type);
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

void ServicesSandesh::FillIpv4Hdr(iphdr *ip, Ipv4Hdr &resp) {
    resp.vers = ip->version;
    resp.hdrlen = ip->ihl;
    resp.tos = ip->tos;
    resp.len = ntohs(ip->tot_len);
    resp.id = IntToHexString(ntohs(ip->id));
    resp.frag = IntToHexString(ntohs(ip->frag_off));
    resp.ttl = ip->ttl;
    resp.protocol = IpProtocol(ip->protocol);
    resp.csum = IntToHexString(ntohs(ip->check));
    Ip4Address sa(ntohl(ip->saddr));
    Ip4Address da(ntohl(ip->daddr));
    resp.src_ip = sa.to_string();
    resp.dest_ip = da.to_string();
}

void ServicesSandesh::FillIcmpv4Hdr(icmphdr *icmp, Icmpv4Hdr &resp) {
    resp.type = (icmp->type == ICMP_ECHO) ? "echo request":
                 (icmp->type == ICMP_ECHOREPLY) ? "echo reply" : 
                 IntToString(icmp->type);
    resp.code = icmp->code;
    resp.csum = IntToHexString(ntohs(icmp->checksum));
}

void ServicesSandesh::FillUdpHdr(udphdr *udp, UdpHdr &resp) {
    resp.src_port = ntohs(udp->source);
    resp.dest_port = ntohs(udp->dest);
    resp.length = ntohs(udp->len);
    resp.csum = IntToHexString(ntohs(udp->check));
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

            case DHCP_OPTION_NTP:
                if (len >= (2 + opt->len))
                    resp += FillOptionIp(get_val(opt->data), "NTP : ");
                break;

            case DHCP_OPTION_SERVER_IDENTIFIER:
                if (len >= (2 + opt->len))
                    resp += FillOptionIp(get_val(opt->data), "Server : ");
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
    FillVrouterHdr(pkt, data.agent_hdr);
    uint8_t *ptr = pkt.pkt + sizeof(ethhdr) + sizeof(agent_hdr);
    FillMacHdr((ethhdr *)ptr, data.mac_hdr);
    ptr += sizeof(ethhdr);
    FillArpHdr((ether_arp *)ptr, data.arp_hdr);
    std::vector<ArpPkt> &list =
        const_cast<std::vector<ArpPkt>&>(resp->get_pkt_list());
    list.push_back(data);
}

void ServicesSandesh::DhcpPktTrace(PktTrace::Pkt &pkt, DhcpPktSandesh *resp) {
    DhcpPkt data;
    FillPktData(pkt, data.info);
    FillVrouterHdr(pkt, data.agent_hdr);
    uint8_t *ptr = pkt.pkt + sizeof(ethhdr) + sizeof(agent_hdr);
    FillMacHdr((ethhdr *)ptr, data.mac_hdr);
    ptr += sizeof(ethhdr);
    FillIpv4Hdr((iphdr *)ptr, data.ip_hdr);
    ptr += (data.ip_hdr.hdrlen * 4);
    FillUdpHdr((udphdr *)ptr, data.udp_hdr); 
    ptr += sizeof(udphdr);
    int32_t remaining = std::min(pkt.len, PktTrace::pkt_trace_size) - 
                        (2 * sizeof(ethhdr) + sizeof(agent_hdr) + 
                         data.ip_hdr.hdrlen * 4 + sizeof(udphdr));
    FillDhcpv4Hdr((dhcphdr *)ptr, data.dhcp_hdr, remaining);
    std::vector<DhcpPkt> &list =
        const_cast<std::vector<DhcpPkt>&>(resp->get_pkt_list());
    list.push_back(data);
}

void ServicesSandesh::DnsPktTrace(PktTrace::Pkt &pkt, DnsPktSandesh *resp) {
    DnsPkt data;
    FillPktData(pkt, data.info);
    FillVrouterHdr(pkt, data.agent_hdr);
    uint8_t *ptr = pkt.pkt + sizeof(ethhdr) + sizeof(agent_hdr);
    FillMacHdr((ethhdr *)ptr, data.mac_hdr);
    ptr += sizeof(ethhdr);
    FillIpv4Hdr((iphdr *)ptr, data.ip_hdr);
    ptr += (data.ip_hdr.hdrlen * 4);
    FillUdpHdr((udphdr *)ptr, data.udp_hdr); 
    ptr += sizeof(udphdr);
    int32_t remaining = std::min(pkt.len, PktTrace::pkt_trace_size) - 
                        (2 * sizeof(ethhdr) + sizeof(agent_hdr) + 
                         data.ip_hdr.hdrlen * 4 + sizeof(udphdr));
    FillDnsHdr((dnshdr *)ptr, data.dns_hdr, remaining);
    std::vector<DnsPkt> &list =
        const_cast<std::vector<DnsPkt>&>(resp->get_pkt_list());
    list.push_back(data);
}

void ServicesSandesh::IcmpPktTrace(PktTrace::Pkt &pkt, IcmpPktSandesh *resp) {
    IcmpPkt data;
    FillPktData(pkt, data.info);
    FillVrouterHdr(pkt, data.agent_hdr);
    uint8_t *ptr = pkt.pkt + sizeof(ethhdr) + sizeof(agent_hdr);
    FillMacHdr((ethhdr *)ptr, data.mac_hdr);
    ptr += sizeof(ethhdr);
    FillIpv4Hdr((iphdr *)ptr, data.ip_hdr);
    ptr += (data.ip_hdr.hdrlen * 4);
    FillIcmpv4Hdr((icmphdr *)ptr, data.icmp_hdr); 
    std::vector<IcmpPkt> &list =
        const_cast<std::vector<IcmpPkt>&>(resp->get_pkt_list());
    list.push_back(data);
}

void ServicesSandesh::OtherPktTrace(PktTrace::Pkt &pkt, PktSandesh *resp) {
    PktDump data;
    FillPktData(pkt, data.info);
    FillVrouterHdr(pkt, data.agent_hdr);
    uint8_t *ptr = pkt.pkt + sizeof(ethhdr) + sizeof(agent_hdr);
    FillMacHdr((ethhdr *)ptr, data.mac_hdr);
    ptr += sizeof(ethhdr);
    int32_t remaining = std::min(pkt.len, PktTrace::pkt_trace_size) - 
                        (2 * sizeof(ethhdr) + sizeof(agent_hdr));
    PktToHexString(ptr, remaining, data.pkt);
    std::vector<PktDump> &list =
        const_cast<std::vector<PktDump>&>(resp->get_pkt_list());
    list.push_back(data);
}

void ServicesSandesh::HandleRequest(PktHandler::ModuleName mod,
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

    PktHandler::GetPktHandler()->PktTraceIterate(mod, cb);
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
    MetadataResponse *resp = new MetadataResponse();
    resp->set_metadata_server_port(
          Agent::GetInstance()->GetMetadataServerPort());
    resp->set_context(context());
    resp->Response();
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
    ssandesh.HandleRequest(PktHandler::INVALID, context(), false);
}

void ClearAllInfo::HandleRequest() const {
    PktHandler::GetPktHandler()->PktTraceClear(PktHandler::DHCP);
    PktHandler::GetPktHandler()->PktTraceClear(PktHandler::ARP);
    PktHandler::GetPktHandler()->PktTraceClear(PktHandler::DNS);
    PktHandler::GetPktHandler()->PktTraceClear(PktHandler::ICMP);
    PktHandler::GetPktHandler()->PktTraceClear(PktHandler::FLOW);
    PktHandler::GetPktHandler()->PktTraceClear(PktHandler::DIAG);
    PktHandler::GetPktHandler()->PktTraceClear(PktHandler::INVALID);
    PktHandler::GetPktHandler()->ClearStats();
    Agent::GetInstance()->GetDhcpProto()->ClearStats();
    Agent::GetInstance()->GetArpProto()->ClearStats();
    Agent::GetInstance()->GetDnsProto()->ClearStats();
    Agent::GetInstance()->GetIcmpProto()->ClearStats();

    PktErrorResp *resp = new PktErrorResp();
    resp->set_context(context());
    resp->Response();
}

///////////////////////////////////////////////////////////////////////////////

void ShowDnsEntries::HandleRequest() const {
    AgentDnsEntries *resp = new AgentDnsEntries();
    std::vector<VmDnsSandesh> dns_list;
    const DnsProto::DnsUpdateSet &dns_update_set = 
                    Agent::GetInstance()->GetDnsProto()->GetUpdateRequestSet();
    for (DnsProto::DnsUpdateSet::const_iterator it = dns_update_set.begin();
         it != dns_update_set.end(); ++it) {
        VmDnsSandesh vmdata;
        vmdata.vm = (*it)->itf->GetName();
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

///////////////////////////////////////////////////////////////////////////////

void ShowArpCache::HandleRequest() const {
    ArpCacheResp *resp = new ArpCacheResp();
    resp->set_context(context());
    ArpSandesh *arp_sandesh = new ArpSandesh(resp);
    ArpProto::ArpCache &cache = const_cast<ArpProto::ArpCache &>
              (Agent::GetInstance()->GetArpProto()->GetArpCache());
    cache.Iterate(boost::bind(&ArpSandesh::SetArpEntry, arp_sandesh, _1, _2));
    arp_sandesh->Response();
    delete arp_sandesh;
}

bool ArpSandesh::SetArpEntry(const ArpKey &key, ArpEntry *&entry) {
    ArpCacheResp *vresp = static_cast<ArpCacheResp *>(resp_);
    ArpSandeshData data;
    boost::asio::ip::address_v4 ip(key.ip);
    data.set_ip(ip.to_string());
    data.set_vrf(key.vrf->GetName());
    std::string mac_str;
    ServicesSandesh::MacToString(entry->Mac(), mac_str);
    data.set_mac(mac_str);
    switch (entry->GetState()) {
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
