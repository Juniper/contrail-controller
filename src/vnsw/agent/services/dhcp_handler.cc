/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "vr_defs.h"
#include "cmn/agent_cmn.h"
#include "oper/agent_route.h"
#include "ksync/interface_ksync.h"
#include "pkt/pkt_init.h"
#include "services/dhcp_proto.h"
#include "services/services_types.h"
#include "services/services_init.h"
#include "services/dns_proto.h"
#include "services/services_sandesh.h"
#include "bind/bind_util.h"
#include "bind/xmpp_dns_agent.h"

#define DHCP_TRACE(obj, arg)                                                 \
do {                                                                         \
    std::ostringstream _str;                                                 \
    _str << arg;                                                             \
    Dhcp##obj::TraceMsg(DhcpTraceBuf, __FILE__, __LINE__, _str.str());       \
} while (false)                                                              \

DhcpHandler::DhcpHandler(Agent *agent, boost::shared_ptr<PktInfo> info,
                         boost::asio::io_service &io)
        : ProtoHandler(agent, info, io), vm_itf_(NULL), vm_itf_index_(-1),
          msg_type_(DHCP_UNKNOWN), out_msg_type_(DHCP_UNKNOWN), req_ip_addr_(0),
          nak_msg_("cannot assign requested address") {
    dhcp_ = (dhcphdr *) pkt_info_->data;
    ipam_type_.ipam_dns_method = "none";
};

bool DhcpHandler::Run() {
    DhcpProto *dhcp_proto = agent()->GetDhcpProto();
    Interface *itf = agent()->GetInterfaceTable()->FindInterface(GetIntf());
    if (itf == NULL) {
        dhcp_proto->IncrStatsOther();
        DHCP_TRACE(Error, "Received DHCP packet on invalid interface : "
                   << GetIntf());
        return true;
    }

    // We snoop DHCP packets to / from VM interfaces in default VRF
    if (itf == dhcp_proto->ip_fabric_interface()) {
        RelayResponseFromFabric();
        return true;
    }

    if (itf->type() != Interface::VM_INTERFACE) {
        dhcp_proto->IncrStatsErrors();
        DHCP_TRACE(Error, "Received DHCP packet on non VM port interface : "
                   << GetIntf());
        return true;
    }
    vm_itf_ = static_cast<VmInterface *>(itf);
    if (!vm_itf_->ipv4_forwarding()) {
        DHCP_TRACE(Error, "DHCP request on VM port with disabled ipv4 service: "
                   << GetIntf());
        return true;
    }

    // For VM interfaces in default VRF, if the config doesnt have IP address,
    // send the request to fabric
    if (vm_itf_->vrf() &&
        vm_itf_->vrf()->GetName() == agent()->GetDefaultVrf() &&
        (!vm_itf_->ip_addr().to_ulong() || vm_itf_->dhcp_snoop_ip())) {
        RelayRequestToFabric();
        return true;
    }

    if (!ReadOptions())
        return true;

    switch (msg_type_) {
        case DHCP_DISCOVER:
            out_msg_type_ = DHCP_OFFER;
            dhcp_proto->IncrStatsDiscover();
            DHCP_TRACE(Trace, "DHCP discover received on interface : "
                       << vm_itf_->name());
            break;

        case DHCP_REQUEST:
            out_msg_type_ = DHCP_ACK;
            dhcp_proto->IncrStatsRequest();
            DHCP_TRACE(Trace, "DHCP request received on interface : "
                       << vm_itf_->name());
            break;

        case DHCP_INFORM:
            out_msg_type_ = DHCP_ACK;
            dhcp_proto->IncrStatsInform();
            DHCP_TRACE(Trace, "DHCP inform received on interface : "
                       << vm_itf_->name());
            break;

        case DHCP_DECLINE:
            dhcp_proto->IncrStatsDecline();
            DHCP_TRACE(Error, "DHCP Client declined the offer : vrf = " << 
                       pkt_info_->vrf << " ifindex = " << GetIntf());
            return true;

        case DHCP_ACK:
        case DHCP_NAK:
        case DHCP_RELEASE:
        case DHCP_LEASE_QUERY:
        case DHCP_LEASE_UNASSIGNED:
        case DHCP_LEASE_UNKNOWN:
        case DHCP_LEASE_ACTIVE:
        default:
            DHCP_TRACE(Trace, ServicesSandesh::DhcpMsgType(msg_type_) <<
                       " received on interface : " << vm_itf_->name() <<
                       "; ignoring");
            dhcp_proto->IncrStatsOther();
            return true;
    }

    if (FindLeaseData()) {
        UpdateDnsServer();
        SendDhcpResponse();
        Ip4Address ip(config_.ip_addr);
        DHCP_TRACE(Trace, "DHCP response sent; message = " << 
                   ServicesSandesh::DhcpMsgType(out_msg_type_) << 
                   "; ip = " << ip.to_string());
    }

    return true;
}

// read DHCP options in the incoming packet
bool DhcpHandler::ReadOptions() {
    // take out eth, ip, udp and DHCP fixed length from the total length
    int16_t opt_rem_len = pkt_info_->len - IPC_HDR_LEN - sizeof(ethhdr) 
                          - sizeof(iphdr) - sizeof(udphdr) - DHCP_FIXED_LEN;

    // verify magic cookie
    if ((opt_rem_len < 4) || 
        memcmp(dhcp_->options, DHCP_OPTIONS_COOKIE, 4)) {
        agent()->GetDhcpProto()->IncrStatsErrors();
        DHCP_TRACE(Error, "DHCP options cookie missing; vrf = " <<
                   pkt_info_->vrf << " ifindex = " << GetIntf());
        return false;
    }

    opt_rem_len -= 4;
    DhcpOptions *opt = (DhcpOptions *)(dhcp_->options + 4);
    // parse thru the option fields
    while ((opt_rem_len > 0) && (opt->code != DHCP_OPTION_END)) {
        switch (opt->code) {
            case DHCP_OPTION_PAD:
                opt_rem_len -= 1;
                opt = (DhcpOptions *)((uint8_t *)opt + 1);
                continue;

            case DHCP_OPTION_MSG_TYPE:
                if (opt_rem_len >= opt->len + 2)
                    msg_type_ = *(uint8_t *)opt->data;
                break;

            case DHCP_OPTION_REQ_IP_ADDRESS:
                if (opt_rem_len >= opt->len + 2) {
                    union {
                        uint8_t data[sizeof(in_addr_t)];
                        in_addr_t addr;
                    } bytes;
                    memcpy(bytes.data, opt->data, sizeof(in_addr_t));
                    req_ip_addr_ = ntohl(bytes.addr);
                }
                break;

            case DHCP_OPTION_HOST_NAME:
                if (opt_rem_len >= opt->len + 2)
                    client_name_.assign((char *)opt->data, opt->len);
                break;

            case DHCP_OPTION_DOMAIN_NAME:
                if (opt_rem_len >= opt->len + 2)
                    domain_name_.assign((char *)opt->data, opt->len);
                break;

            default:
                break;
        }
        opt_rem_len -= (2 + opt->len);
        opt = (DhcpOptions *)((uint8_t *)opt + 2 + opt->len);
    }

    return true;
}

void DhcpHandler::FillDhcpInfo(uint32_t addr, int plen, uint32_t gw, uint32_t dns) {
    config_.ip_addr = addr;
    config_.plen = plen;
    uint32_t mask = plen? (0xFFFFFFFF << (32 - plen)) : 0;
    config_.subnet_mask = mask;
    config_.bcast_addr = (addr & mask) | ~mask;
    config_.gw_addr = gw;
    config_.dns_addr = dns;
}


bool DhcpHandler::FindLeaseData() {
    config_.ifindex = GetIntf();
    Ip4Address ip = vm_itf_->ip_addr();
    // Change client name to VM name; this is the name assigned to the VM
    client_name_ = vm_itf_->vm_name();
    if (vm_itf_->active()) {
        if (vm_itf_->fabric_port()) {
            Inet4UnicastRouteEntry *rt = 
                Inet4UnicastAgentRouteTable::FindResolveRoute(
                             vm_itf_->vrf()->GetName(), ip);
            if (rt) {
                uint8_t plen = rt->GetPlen();
                uint32_t gw = agent()->GetGatewayId().to_ulong();
                uint32_t mask = plen ? (0xFFFFFFFF << (32 - plen)) : 0;
                boost::system::error_code ec;
                if ((rt->GetIpAddress().to_ulong() & mask) == 
                    Ip4Address::from_string("169.254.0.0", ec).to_ulong())
                    gw = 0;
                FillDhcpInfo(ip.to_ulong(), rt->GetPlen(), gw, gw);
                return true;
            }
            agent()->GetDhcpProto()->IncrStatsErrors();
            DHCP_TRACE(Error, "DHCP fabric port request failed : "
                       "could not find route for " << ip.to_string());
            return false;
        }

        const std::vector<VnIpam> &ipam = vm_itf_->vn()->GetVnIpam();
        unsigned int i;
        for (i = 0; i < ipam.size(); ++i) {
            uint32_t mask = ipam[i].plen ? (0xFFFFFFFF << (32 - ipam[i].plen)) : 0;
            if ((ip.to_ulong() & mask) == 
                (ipam[i].ip_prefix.to_ulong() & mask)) {
                uint32_t default_gw = ipam[i].default_gw.to_ulong();
                FillDhcpInfo(ip.to_ulong(), ipam[i].plen, default_gw, default_gw);
                return true;
            }
        }
    }

    // We dont have the config yet; give a short lease
    config_.lease_time = DHCP_SHORTLEASE_TIME;
    if (ip.to_ulong()) {
        // Give address received from Nova
        FillDhcpInfo(ip.to_ulong(), 32, 0, 0);
    } else {
        // Give a link local address
        boost::system::error_code ec;
        uint32_t gwip = Ip4Address::from_string(LINK_LOCAL_GW, ec).to_ulong();
        FillDhcpInfo((gwip & 0xFFFF0000) | (vm_itf_->id() & 0xFF),
                     16, 0, 0);
    }
    return true;
}

void DhcpHandler::UpdateDnsServer() {
    if (config_.lease_time != (uint32_t) -1)
        return;

    if (!vm_itf_->vn() || 
        !vm_itf_->vn()->GetIpamData(vm_itf_->ip_addr(), &ipam_name_,
                                    &ipam_type_)) {
        DHCP_TRACE(Trace, "Ipam data not found; VM = " << vm_itf_->name());
        return;
    }

    if (ipam_type_.ipam_dns_method != "virtual-dns-server" ||
        !agent()->GetDomainConfigTable()->GetVDns(ipam_type_.ipam_dns_server.
                                          virtual_dns_server_name, &vdns_type_))
        return;

    if (domain_name_.size() && domain_name_ != vdns_type_.domain_name) {
        DHCP_TRACE(Trace, "Client domain " << domain_name_ << 
                   " doesnt match with configured domain " << 
                   vdns_type_.domain_name << "; Client name = " << 
                   client_name_);
    }
    std::size_t pos;
    if (client_name_.size() &&
        ((pos = client_name_.find('.', 0)) != std::string::npos) &&
        (client_name_.substr(pos + 1) != vdns_type_.domain_name)) {
        DHCP_TRACE(Trace, "Client domain doesnt match with configured domain "
                   << vdns_type_.domain_name << "; Client name = " 
                   << client_name_);
        client_name_.replace(client_name_.begin() + pos + 1, client_name_.end(),
                             vdns_type_.domain_name);
    }
    domain_name_ = vdns_type_.domain_name;

    if (out_msg_type_ != DHCP_ACK)
        return;

    agent()->GetDnsProto()->UpdateDnsEntry(
        vm_itf_, client_name_, vm_itf_->ip_addr(), config_.plen,
        ipam_type_.ipam_dns_server.virtual_dns_server_name, vdns_type_,
        false, false);
}

void DhcpHandler::WriteOption82(DhcpOptions *opt, uint16_t &optlen) {
    optlen += 2;
    opt->code = DHCP_OPTION_82;
    opt->len = sizeof(uint32_t) + 2 + sizeof(VmInterface *) + 2;
    DhcpOptions *subopt = reinterpret_cast<DhcpOptions *>(opt->data);
    subopt->WriteWord(DHCP_SUBOP_CKTID, vm_itf_->id(), optlen);
    subopt = subopt->GetNextOptionPtr();
    subopt->WriteData(DHCP_SUBOP_REMOTEID, sizeof(VmInterface *),
                      &vm_itf_, optlen);
}

bool DhcpHandler::ReadOption82(DhcpOptions *opt) {
    if (opt->len != sizeof(uint32_t) + 2 + sizeof(VmInterface *) + 2)
        return false;

    DhcpOptions *subopt = reinterpret_cast<DhcpOptions *>(opt->data);
    for (int i = 0; i < 2; i++) {
        switch (subopt->code) {
            case DHCP_SUBOP_CKTID:
                if (subopt->len != sizeof(uint32_t))
                    return false;
                union {
                    uint8_t data[sizeof(uint32_t)];
                    uint32_t index;
                } bytes;

                memcpy(bytes.data, subopt->data, sizeof(uint32_t));
                vm_itf_index_ = ntohl(bytes.index);
                break;

            case DHCP_SUBOP_REMOTEID:
                if (subopt->len != sizeof(VmInterface *))
                    return false;
                memcpy(&vm_itf_, subopt->data, subopt->len);
                break;

            default:
                return false;
        }
        subopt = subopt->GetNextOptionPtr();
    }

    return true;
}

bool DhcpHandler::CreateRelayPacket(bool is_request) {
    PktInfo in_pkt_info = *pkt_info_.get();
    pkt_info_->pkt = new uint8_t[DHCP_PKT_SIZE];
    memset(pkt_info_->pkt, 0, DHCP_PKT_SIZE);
    pkt_info_->vrf = in_pkt_info.vrf;
    pkt_info_->eth = (ethhdr *)(pkt_info_->pkt + sizeof(ethhdr) + sizeof(agent_hdr));
    pkt_info_->ip = (iphdr *)(pkt_info_->eth + 1);
    pkt_info_->transp.udp = (udphdr *)(pkt_info_->ip + 1);
    dhcphdr *dhcp = (dhcphdr *)(pkt_info_->transp.udp + 1);

    memcpy((uint8_t *)dhcp, (uint8_t *)dhcp_, DHCP_FIXED_LEN);
    memcpy(dhcp->options, DHCP_OPTIONS_COOKIE, 4);

    int16_t opt_rem_len = in_pkt_info.len - IPC_HDR_LEN - sizeof(ethhdr) 
                          - sizeof(iphdr) - sizeof(udphdr) - DHCP_FIXED_LEN - 4;
    uint16_t opt_len = 4;
    DhcpOptions *read_opt = (DhcpOptions *)(dhcp_->options + 4);
    DhcpOptions *write_opt = (DhcpOptions *)(dhcp->options + 4);
    while ((opt_rem_len > 0) && (read_opt->code != DHCP_OPTION_END)) {
        switch (read_opt->code) {
            case DHCP_OPTION_PAD:
                write_opt->WriteByte(DHCP_OPTION_PAD, opt_len);
                write_opt = write_opt->GetNextOptionPtr();
                opt_rem_len -= 1;
                read_opt = read_opt->GetNextOptionPtr();
                continue;

            case DHCP_OPTION_82:
                if (!is_request) {
                    if (!ReadOption82(read_opt) || !vm_itf_ || 
                        agent()->GetInterfaceTable()->
                        FindInterface(vm_itf_index_) != vm_itf_)
                        return false;
                }
                break;

            case DHCP_OPTION_MSG_TYPE:
                msg_type_ = *(uint8_t *)read_opt->data;
                write_opt->WriteData(read_opt->code, read_opt->len, &msg_type_, opt_len);
                write_opt = write_opt->GetNextOptionPtr();
                break;

            case DHCP_OPTION_HOST_NAME:
                if (is_request) {
                    client_name_ = vm_itf_->vm_name();
                    write_opt->WriteData(DHCP_OPTION_HOST_NAME, client_name_.size(), 
                                         client_name_.c_str(), opt_len);
                    write_opt = write_opt->GetNextOptionPtr();
                }
                break;

            default:
                write_opt->WriteData(read_opt->code, read_opt->len, &read_opt->data, opt_len);
                write_opt = write_opt->GetNextOptionPtr();
                break;

        }
        opt_rem_len -= (2 + read_opt->len);
        read_opt = read_opt->GetNextOptionPtr();
    }
    if (is_request) {
        dhcp->giaddr = ntohl(agent()->GetRouterId().to_ulong());
        WriteOption82(write_opt, opt_len);
        write_opt = write_opt->GetNextOptionPtr();
        pkt_info_->sport = DHCP_CLIENT_PORT;
        pkt_info_->dport = DHCP_SERVER_PORT;
    } else {
        if (!vm_itf_) {
            return false;
        }
        dhcp->giaddr = 0;
        client_name_ = vm_itf_->vm_name();
        write_opt->WriteData(DHCP_OPTION_HOST_NAME, client_name_.size(), 
                             client_name_.c_str(), opt_len);
        write_opt = write_opt->GetNextOptionPtr();
        pkt_info_->sport = DHCP_SERVER_PORT;
        pkt_info_->dport = DHCP_CLIENT_PORT;
    }
    write_opt->WriteByte(DHCP_OPTION_END, opt_len);
    pkt_info_->len = DHCP_FIXED_LEN + opt_len + sizeof(udphdr);

    UdpHdr(pkt_info_->len, in_pkt_info.ip->saddr, pkt_info_->sport,
           in_pkt_info.ip->daddr, pkt_info_->dport);
    pkt_info_->len += sizeof(iphdr);
    IpHdr(pkt_info_->len, in_pkt_info.ip->saddr,
          in_pkt_info.ip->daddr, IPPROTO_UDP);
    if (is_request) {
        EthHdr(agent()->GetDhcpProto()->ip_fabric_interface_mac(),
               in_pkt_info.eth->h_dest, 0x800);
    } else {
        EthHdr(in_pkt_info.eth->h_source, dhcp->chaddr, 0x800);
    }
    pkt_info_->len += sizeof(ethhdr);
    return true;
}

void DhcpHandler::RelayRequestToFabric() {
    CreateRelayPacket(true);
    DhcpProto *dhcp_proto = agent()->GetDhcpProto();
    Send(pkt_info_->len, dhcp_proto->ip_fabric_interface_index(),
         pkt_info_->vrf, AGENT_CMD_SWITCH, PktHandler::DHCP);
    dhcp_proto->IncrStatsRelayReqs();
}

void DhcpHandler::RelayResponseFromFabric() {
    if (!CreateRelayPacket(false)) {
        DHCP_TRACE(Trace, "Ignoring received DHCP packet from fabric interface");
        return;
    }

    if (msg_type_ == DHCP_ACK) {
        // Update the interface with the IP address, to trigger a route add
        DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
        req.key.reset(new VmInterfaceKey(AgentKey::RESYNC, vm_itf_->GetUuid(),
                                         vm_itf_->name()));
        req.data.reset(new VmInterfaceIpAddressData
                       (Ip4Address(ntohl(dhcp_->yiaddr))));
        agent()->GetInterfaceTable()->Enqueue(&req);
    }

    Send(pkt_info_->len, vm_itf_index_,
         pkt_info_->vrf, AGENT_CMD_SWITCH, PktHandler::DHCP);
    agent()->GetDhcpProto()->IncrStatsRelayResps();
}

uint16_t DhcpHandler::AddClasslessRouteOption(uint16_t opt_len) {
    std::set<VnSubnet> host_routes;
    vm_itf_->vn()->GetVnHostRoutes(ipam_name_, &host_routes);
    if (host_routes.size()) {
        DhcpOptions *opt = GetNextOptionPtr(opt_len);
        opt->code = DHCP_OPTION_CLASSLESS_ROUTE;
        uint8_t *ptr = opt->data;
        uint8_t len = 0;
        for (std::set<VnSubnet>::iterator it = host_routes.begin();
             it != host_routes.end(); ++it) {
            uint32_t prefix = it->prefix.to_ulong();
            uint32_t plen = it->plen;
            *ptr++ = plen;
            len++;
            for (unsigned int i = 0; plen && i <= (plen - 1) / 8; ++i) {
                *ptr++ = (prefix >> 8 * (3 - i)) & 0xFF;
                len++;
            }
            *(uint32_t *)ptr = htonl(config_.gw_addr);
            ptr += sizeof(uint32_t);
            len += sizeof(uint32_t);
        }
        opt->len = len;
        opt_len += 2 + len;
    }
    return opt_len;
}

uint16_t DhcpHandler::DhcpHdr(in_addr_t yiaddr, 
                              in_addr_t siaddr, uint8_t *chaddr) {
    int num_domain_name = 0;

    dhcp_->op = BOOT_REPLY;
    dhcp_->htype = HW_TYPE_ETHERNET;
    dhcp_->hlen = ETH_ALEN;
    dhcp_->hops = 0;
    // dhcp_->xid = dhcp_->xid;
    dhcp_->secs = 0;
    // dhcp_->flags = dhcp_->flags;
    dhcp_->ciaddr = 0;
    dhcp_->yiaddr = yiaddr;
    dhcp_->siaddr = siaddr;
    // dhcp_->giaddr = 0;
    // memset (dhcp_->chaddr, 0, DHCP_CHADDR_LEN + DHCP_NAME_LEN + DHCP_FILE_LEN);
    // memcpy(dhcp_->chaddr, chaddr, ETH_ALEN);
    // not supporting dhcp_->sname, dhcp_->file for now
    memset(dhcp_->sname, 0, DHCP_NAME_LEN);
    memset(dhcp_->file, 0, DHCP_FILE_LEN);

    memcpy(dhcp_->options, DHCP_OPTIONS_COOKIE, 4);

    uint16_t opt_len = 4;
    DhcpOptions *opt = GetNextOptionPtr(opt_len);
    opt->WriteData(DHCP_OPTION_MSG_TYPE, 1, &out_msg_type_, opt_len);

    opt = GetNextOptionPtr(opt_len);
    opt->WriteData(DHCP_OPTION_SERVER_IDENTIFIER, 4, &siaddr, opt_len);

    if (out_msg_type_ == DHCP_NAK) {
        opt = GetNextOptionPtr(opt_len);
        opt->WriteData(DHCP_OPTION_MESSAGE, nak_msg_.size(), 
                       nak_msg_.data(), opt_len);
    }
    else {
        if (msg_type_ != DHCP_INFORM) {
            opt = GetNextOptionPtr(opt_len);
            opt->WriteWord(DHCP_OPTION_IP_LEASE_TIME,
                           config_.lease_time, opt_len);
        }

        if (config_.subnet_mask) {
            opt = GetNextOptionPtr(opt_len);
            opt->WriteWord(DHCP_OPTION_SUBNET_MASK, config_.subnet_mask, opt_len);
        }

        if (config_.bcast_addr) {
            opt = GetNextOptionPtr(opt_len);
            opt->WriteWord(DHCP_OPTION_BCAST_ADDRESS, config_.bcast_addr, opt_len);
        }

        if (config_.gw_addr) {
            opt = GetNextOptionPtr(opt_len);
            opt->WriteWord(DHCP_OPTION_ROUTER, config_.gw_addr, opt_len);
        }

        if (client_name_.size()) {
            opt = GetNextOptionPtr(opt_len);
            opt->WriteData(DHCP_OPTION_HOST_NAME, client_name_.size(), 
                           client_name_.c_str(), opt_len);
        }

        if (ipam_type_.ipam_dns_method == "default-dns-server" ||
            ipam_type_.ipam_dns_method == "") {
            if (config_.dns_addr) {
                opt = GetNextOptionPtr(opt_len);
                opt->WriteWord(DHCP_OPTION_DNS, config_.dns_addr, opt_len);
            }
        } else if (ipam_type_.ipam_dns_method == "tenant-dns-server") {
            for (unsigned int i = 0; i < ipam_type_.ipam_dns_server.
                 tenant_dns_server_address.ip_address.size(); ++i) {
                boost::system::error_code ec;
                uint32_t ip = 
                    Ip4Address::from_string(ipam_type_.ipam_dns_server.
                    tenant_dns_server_address.ip_address[i], ec).to_ulong();
                if (ec.value()) {
                    DHCP_TRACE(Trace, "Invalid DNS server address : " << 
                               boost::system::system_error(ec).what());
                    continue;
                }
                opt = GetNextOptionPtr(opt_len);
                opt->WriteWord(DHCP_OPTION_DNS, ip, opt_len);
            }
        } else if (ipam_type_.ipam_dns_method == "virtual-dns-server") {
            if (config_.dns_addr) {
                opt = GetNextOptionPtr(opt_len);
                opt->WriteWord(DHCP_OPTION_DNS, config_.dns_addr, opt_len);
            }
            if (domain_name_.size()) {
                num_domain_name++;
                opt = GetNextOptionPtr(opt_len);
                opt->WriteData(DHCP_OPTION_DOMAIN_NAME, domain_name_.size(), 
                               domain_name_.c_str(), opt_len);
            }
        }

        // Add dhcp options coming from Ipam
        // adding them irrespective of the DNS method chosen
        std::vector<autogen::DhcpOptionType> &options = 
                        ipam_type_.dhcp_option_list.dhcp_option;
        for (unsigned int i = 0; i < options.size(); ++i) {
            uint32_t option_type;
            std::stringstream str(options[i].dhcp_option_name);
            str >> option_type;
            switch(option_type) {
                case DHCP_OPTION_NTP:
                case DHCP_OPTION_DNS: {
                    boost::system::error_code ec;
                    uint32_t ip = Ip4Address::from_string(options[i].
                                  dhcp_option_value, ec).to_ulong();
                    if (!ec.value()) {
                        opt = GetNextOptionPtr(opt_len);
                        opt->WriteWord(option_type, ip, opt_len);
                    } else {
                        Ip4Address ip(config_.ip_addr);
                        DHCP_TRACE(Error, "Invalid DHCP option " <<
                                   option_type << " for VM " << 
                                   ip.to_string() << "; has to be IP address");
                    }
                    break;
                }

                case DHCP_OPTION_DOMAIN_NAME:
                    // allow only one domain name option in a DHCP response
                    if (!num_domain_name &&
                        options[i].dhcp_option_value.size()) {
                        num_domain_name++;
                        opt = GetNextOptionPtr(opt_len);
                        opt->WriteData(option_type, 
                                       options[i].dhcp_option_value.size(), 
                                       options[i].dhcp_option_value.c_str(), 
                                       opt_len);
                    }
                    break;

                default:
                    DHCP_TRACE(Error, "Unsupported DHCP option in Ipam : " +
                               options[i].dhcp_option_name);
                    break;
            }
        }

        // Add classless route option
        if (vm_itf_->vn()) {
            opt_len = AddClasslessRouteOption(opt_len);
        }
    }

    opt = GetNextOptionPtr(opt_len);
    opt->code = DHCP_OPTION_END;
    opt_len += 1;

    return (DHCP_FIXED_LEN + opt_len);
}

void DhcpHandler::SendDhcpResponse() {
    // TODO: If giaddr is set, what to do ?

    in_addr_t src_ip = htonl(config_.gw_addr);
    in_addr_t dest_ip = 0xFFFFFFFF;
    in_addr_t yiaddr = htonl(config_.ip_addr);
    in_addr_t siaddr = src_ip;
    unsigned char dest_mac[ETH_ALEN] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

    // If requested IP address is not available, send NAK
    if ((msg_type_ == DHCP_REQUEST) && (req_ip_addr_) 
        && (config_.ip_addr != req_ip_addr_)) {
        out_msg_type_ = DHCP_NAK;
        yiaddr = 0;
        siaddr = 0;
    }

    // send a unicast response when responding to INFORM 
    // or when incoming giaddr is zero and ciaddr is set
    // or when incoming bcast flag is not set (with giaddr & ciaddr being zero)
    if ((msg_type_ == DHCP_INFORM) ||
        (!dhcp_->giaddr && (dhcp_->ciaddr || 
                            !(ntohs(dhcp_->flags) & DHCP_BCAST_FLAG)))) {
        dest_ip = yiaddr;
        memcpy(dest_mac, dhcp_->chaddr, ETH_ALEN);
        if (msg_type_ == DHCP_INFORM)
            yiaddr = 0;
    }
        
    UpdateStats();

    // fill in the response
    uint16_t len = DhcpHdr(yiaddr, siaddr, dhcp_->chaddr);
    len += sizeof(udphdr);
    UdpHdr(len, src_ip, DHCP_SERVER_PORT, dest_ip, DHCP_CLIENT_PORT);
    len += sizeof(iphdr);
    IpHdr(len, src_ip, dest_ip, IPPROTO_UDP);
    EthHdr(agent()->pkt()->pkt_handler()->mac_address(), dest_mac, 0x800);
    len += sizeof(ethhdr);

    Send(len, GetIntf(), pkt_info_->vrf, AGENT_CMD_SWITCH, PktHandler::DHCP);
}

void DhcpHandler::UpdateStats() {
    DhcpProto *dhcp_proto = agent()->GetDhcpProto();
    (out_msg_type_ == DHCP_OFFER) ? dhcp_proto->IncrStatsOffers() :
        ((out_msg_type_ == DHCP_ACK) ? dhcp_proto->IncrStatsAcks() : 
                                       dhcp_proto->IncrStatsNacks());
}
