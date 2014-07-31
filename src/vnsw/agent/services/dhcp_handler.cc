/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <sys/types.h>
#include <net/ethernet.h>
#include "vr_defs.h"
#include "cmn/agent_cmn.h"
#include "oper/route_common.h"
#include "oper/vn.h"
#include "pkt/pkt_init.h"
#include "services/dhcp_proto.h"
#include "services/services_types.h"
#include "services/services_init.h"
#include "services/dns_proto.h"
#include "services/services_sandesh.h"
#include "bind/bind_util.h"
#include "bind/xmpp_dns_agent.h"

DhcpHandler::DhcpHandler(Agent *agent, boost::shared_ptr<PktInfo> info,
                         boost::asio::io_service &io)
        : ProtoHandler(agent, info, io), vm_itf_(NULL), vm_itf_index_(-1),
          msg_type_(DHCP_UNKNOWN), out_msg_type_(DHCP_UNKNOWN),
          nak_msg_("cannot assign requested address") {
    ipam_type_.ipam_dns_method = "none";
};

bool DhcpHandler::Run() {
    switch(pkt_info_->type) {
        case PktType::MESSAGE:
            return HandleMessage();

       default:
            return HandleVmRequest();
    }
}    

bool DhcpHandler::HandleVmRequest() {
    dhcp_ = (dhcphdr *) pkt_info_->data;
    request_.UpdateData(dhcp_->xid, ntohs(dhcp_->flags), dhcp_->chaddr);
    DhcpProto *dhcp_proto = agent()->GetDhcpProto();
    Interface *itf =
        agent()->interface_table()->FindInterface(GetInterfaceIndex());
    if (itf == NULL) {
        dhcp_proto->IncrStatsOther();
        DHCP_TRACE(Error, "Received DHCP packet on invalid interface : "
                   << GetInterfaceIndex());
        return true;
    }

    if (itf->type() != Interface::VM_INTERFACE) {
        dhcp_proto->IncrStatsErrors();
        DHCP_TRACE(Error, "Received DHCP packet on non VM port interface : "
                   << GetInterfaceIndex());
        return true;
    }
    vm_itf_ = static_cast<VmInterface *>(itf);
    if (!vm_itf_->ipv4_forwarding()) {
        DHCP_TRACE(Error, "DHCP request on VM port with disabled ipv4 service: "
                   << GetInterfaceIndex());
        return true;
    }

    // For VM interfaces in default VRF, if the config doesnt have IP address,
    // send the request to fabric
    if (vm_itf_->vrf() && vm_itf_->do_dhcp_relay()) {
        RelayRequestToFabric();
        return true;
    }

    // options length = pkt length - size of headers
    int16_t options_len = pkt_info_->len - IPC_HDR_LEN -
                            sizeof(struct ether_header) -
                            sizeof(struct ip) - sizeof(struct udphdr) -
                            DHCP_FIXED_LEN;
    if (!ReadOptions(options_len))
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
                       pkt_info_->vrf << " ifindex = " << GetInterfaceIndex());
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

bool DhcpHandler::HandleMessage() {
    switch (pkt_info_->ipc->cmd) {
        case DhcpProto::DHCP_VHOST_MSG:
            // DHCP message from DHCP server port that we listen on
            return HandleDhcpFromFabric();

        default:
            assert(0);
    }
}

// Handle any DHCP response coming from fabric for a request that we relayed
bool DhcpHandler::HandleDhcpFromFabric() {
    DhcpProto::DhcpVhostMsg *ipc = static_cast<DhcpProto::DhcpVhostMsg *>(pkt_info_->ipc);
    pkt_info_->len = ipc->len;
    dhcp_ = reinterpret_cast<dhcphdr *>(ipc->pkt);
    request_.UpdateData(dhcp_->xid, ntohs(dhcp_->flags), dhcp_->chaddr);

    int16_t options_len = ipc->len - DHCP_FIXED_LEN;
    if (ReadOptions(options_len) && vm_itf_ &&
        agent()->interface_table()->FindInterface(vm_itf_index_) == vm_itf_) {
        // this is a DHCP relay response for our request
        RelayResponseFromFabric();
    }

    delete ipc;
    return true;
}

// read DHCP options in the incoming packet
bool DhcpHandler::ReadOptions(int16_t opt_rem_len) {
    // verify magic cookie
    if ((opt_rem_len < 4) || 
        memcmp(dhcp_->options, DHCP_OPTIONS_COOKIE, 4)) {
        agent()->GetDhcpProto()->IncrStatsErrors();
        DHCP_TRACE(Error, "DHCP options cookie missing; vrf = " <<
                   pkt_info_->vrf << " ifindex = " << GetInterfaceIndex());
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
                    request_.ip_addr = ntohl(bytes.addr);
                }
                break;

            case DHCP_OPTION_HOST_NAME:
                if (opt_rem_len >= opt->len + 2)
                    config_.client_name_.assign((char *)opt->data, opt->len);
                break;

            case DHCP_OPTION_DOMAIN_NAME:
                if (opt_rem_len >= opt->len + 2)
                    config_.domain_name_.assign((char *)opt->data, opt->len);
                break;

            case DHCP_OPTION_82:
                ReadOption82(opt);
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
    Ip4Address ip = vm_itf_->ip_addr();
    // Change client name to VM name; this is the name assigned to the VM
    config_.client_name_ = vm_itf_->vm_name();
    if (vm_itf_->ipv4_active()) {
        if (vm_itf_->fabric_port()) {
            Inet4UnicastRouteEntry *rt = 
                Inet4UnicastAgentRouteTable::FindResolveRoute(
                             vm_itf_->vrf()->GetName(), ip);
            if (rt) {
                uint32_t gw = agent()->vhost_default_gateway().to_ulong();
                boost::system::error_code ec;
                if (IsIp4SubnetMember(rt->addr(),
                    Ip4Address::from_string("169.254.0.0", ec), rt->plen()))
                    gw = 0;
                FillDhcpInfo(ip.to_ulong(), rt->plen(), gw, gw);
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
            if (IsIp4SubnetMember(ip, ipam[i].ip_prefix, ipam[i].plen)) {
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
        !agent()->domain_config_table()->GetVDns(ipam_type_.ipam_dns_server.
                                          virtual_dns_server_name, &vdns_type_))
        return;

    if (config_.domain_name_.size() &&
        config_.domain_name_ != vdns_type_.domain_name) {
        DHCP_TRACE(Trace, "Client domain " << config_.domain_name_ << 
                   " doesnt match with configured domain " << 
                   vdns_type_.domain_name << "; Client name = " << 
                   config_.client_name_);
    }
    std::size_t pos;
    if (config_.client_name_.size() &&
        ((pos = config_.client_name_.find('.', 0)) != std::string::npos) &&
        (config_.client_name_.substr(pos + 1) != vdns_type_.domain_name)) {
        DHCP_TRACE(Trace, "Client domain doesnt match with configured domain "
                   << vdns_type_.domain_name << "; Client name = " 
                   << config_.client_name_);
        config_.client_name_.replace(config_.client_name_.begin() + pos + 1,
                                     config_.client_name_.end(),
                                     vdns_type_.domain_name);
    }
    config_.domain_name_ = vdns_type_.domain_name;

    if (out_msg_type_ != DHCP_ACK)
        return;

    agent()->GetDnsProto()->SendUpdateDnsEntry(
        vm_itf_, config_.client_name_, vm_itf_->ip_addr(), config_.plen,
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

bool DhcpHandler::CreateRelayPacket() {
    PktInfo in_pkt_info = *pkt_info_.get();
    pkt_info_->pkt = new uint8_t[DHCP_PKT_SIZE];
    memset(pkt_info_->pkt, 0, DHCP_PKT_SIZE);
    pkt_info_->vrf = in_pkt_info.vrf;
    pkt_info_->eth = (ether_header *)(pkt_info_->pkt + sizeof(ether_header) + 
            sizeof(agent_hdr));
    pkt_info_->ip = (ip *)(pkt_info_->eth + 1);
    pkt_info_->transp.udp = (udphdr *)(pkt_info_->ip + 1);
    dhcphdr *dhcp = (dhcphdr *)(pkt_info_->transp.udp + 1);

    memcpy((uint8_t *)dhcp, (uint8_t *)dhcp_, DHCP_FIXED_LEN);
    memcpy(dhcp->options, DHCP_OPTIONS_COOKIE, 4);

    int16_t opt_rem_len = in_pkt_info.len - IPC_HDR_LEN - sizeof(ether_header)
                          - sizeof(ip) - sizeof(udphdr) - DHCP_FIXED_LEN - 4;
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
                break;

            case DHCP_OPTION_MSG_TYPE:
                msg_type_ = *(uint8_t *)read_opt->data;
                write_opt->WriteData(read_opt->code, read_opt->len, &msg_type_, opt_len);
                write_opt = write_opt->GetNextOptionPtr();
                break;

            case DHCP_OPTION_HOST_NAME:
                config_.client_name_ = vm_itf_->vm_name();
                write_opt->WriteData(DHCP_OPTION_HOST_NAME,
                                     config_.client_name_.size(),
                                     config_.client_name_.c_str(), opt_len);
                write_opt = write_opt->GetNextOptionPtr();
                break;

            default:
                write_opt->WriteData(read_opt->code, read_opt->len, &read_opt->data, opt_len);
                write_opt = write_opt->GetNextOptionPtr();
                break;
        }
        opt_rem_len -= (2 + read_opt->len);
        read_opt = read_opt->GetNextOptionPtr();
    }
    dhcp_ = dhcp;
    dhcp->giaddr = htonl(agent()->router_id().to_ulong());
    WriteOption82(write_opt, opt_len);
    write_opt = write_opt->GetNextOptionPtr();
    pkt_info_->sport = DHCP_SERVER_PORT;
    pkt_info_->dport = DHCP_SERVER_PORT;
    write_opt->WriteByte(DHCP_OPTION_END, opt_len);
    pkt_info_->len = DHCP_FIXED_LEN + opt_len + sizeof(udphdr);

    UdpHdr(pkt_info_->len, in_pkt_info.ip->ip_src.s_addr, pkt_info_->sport,
           in_pkt_info.ip->ip_dst.s_addr, pkt_info_->dport);
    pkt_info_->len += sizeof(ip);

    IpHdr(pkt_info_->len, htonl(agent()->router_id().to_ulong()),
          0xFFFFFFFF, IPPROTO_UDP);
    EthHdr(agent()->GetDhcpProto()->ip_fabric_interface_mac(),
           in_pkt_info.eth->ether_dhost, ETHERTYPE_IP);
    pkt_info_->len += sizeof(ether_header);

    return true;
}

bool DhcpHandler::CreateRelayResponsePacket() {
    PktInfo in_pkt_info = *pkt_info_.get();
    pkt_info_->pkt = new uint8_t[DHCP_PKT_SIZE];
    memset(pkt_info_->pkt, 0, DHCP_PKT_SIZE);
    pkt_info_->vrf = vm_itf_->vrf()->vrf_id();
    pkt_info_->eth = (struct ether_header *)(pkt_info_->pkt +
        sizeof(struct ether_header) + sizeof(agent_hdr));
    pkt_info_->ip = (struct ip *)(pkt_info_->eth + 1);
    pkt_info_->transp.udp = (udphdr *)(pkt_info_->ip + 1);
    dhcphdr *dhcp = (dhcphdr *)(pkt_info_->transp.udp + 1);

    memcpy((uint8_t *)dhcp, (uint8_t *)dhcp_, DHCP_FIXED_LEN);
    memcpy(dhcp->options, DHCP_OPTIONS_COOKIE, 4);

    int16_t opt_rem_len = in_pkt_info.len - DHCP_FIXED_LEN - 4;
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
                break;

            case DHCP_OPTION_MSG_TYPE:
                msg_type_ = *(uint8_t *)read_opt->data;
                write_opt->WriteData(read_opt->code, read_opt->len, &msg_type_, opt_len);
                write_opt = write_opt->GetNextOptionPtr();
                break;

            default:
                write_opt->WriteData(read_opt->code, read_opt->len, &read_opt->data, opt_len);
                write_opt = write_opt->GetNextOptionPtr();
                break;

        }
        opt_rem_len -= (2 + read_opt->len);
        read_opt = read_opt->GetNextOptionPtr();
    }
    dhcp_ = dhcp;
    dhcp->giaddr = 0;
    config_.client_name_ = vm_itf_->vm_name();
    write_opt->WriteData(DHCP_OPTION_HOST_NAME, config_.client_name_.size(),
                         config_.client_name_.c_str(), opt_len);
    write_opt = write_opt->GetNextOptionPtr();
    pkt_info_->sport = DHCP_SERVER_PORT;
    pkt_info_->dport = DHCP_CLIENT_PORT;
    write_opt->WriteByte(DHCP_OPTION_END, opt_len);
    pkt_info_->len = DHCP_FIXED_LEN + opt_len + sizeof(udphdr);

    UdpHdr(pkt_info_->len, agent()->router_id().to_ulong(), pkt_info_->sport,
           0xFFFFFFFF, pkt_info_->dport);
    pkt_info_->len += sizeof(ip);
    IpHdr(pkt_info_->len, htonl(agent()->router_id().to_ulong()),
          0xFFFFFFFF, IPPROTO_UDP);
    EthHdr(agent()->pkt()->pkt_handler()->mac_address(), dhcp->chaddr, 0x800);
    pkt_info_->len += sizeof(ether_header);
    return true;
}

void DhcpHandler::RelayRequestToFabric() {
    CreateRelayPacket();
    DhcpProto *dhcp_proto = agent()->GetDhcpProto();
    Send(pkt_info_->len, dhcp_proto->ip_fabric_interface_index(),
         pkt_info_->vrf, AGENT_CMD_SWITCH, PktHandler::DHCP);
    dhcp_proto->IncrStatsRelayReqs();
}

void DhcpHandler::RelayResponseFromFabric() {
    if (!CreateRelayResponsePacket()) {
        DHCP_TRACE(Trace, "Ignoring received DHCP packet from fabric interface");
        return;
    }

    if (msg_type_ == DHCP_ACK) {
        // Populate the DHCP Snoop table
        agent()->interface_table()->AddDhcpSnoopEntry
            (vm_itf_->name(), Ip4Address(ntohl(dhcp_->yiaddr)));
        // Enqueue RESYNC to update the IP address
        DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
        req.key.reset(new VmInterfaceKey(AgentKey::RESYNC, vm_itf_->GetUuid(),
                                         vm_itf_->name()));
        req.data.reset(new VmInterfaceIpAddressData());
        agent()->interface_table()->Enqueue(&req);
    }

    Send(pkt_info_->len, vm_itf_index_,
         pkt_info_->vrf, AGENT_CMD_SWITCH, PktHandler::DHCP);
    agent()->GetDhcpProto()->IncrStatsRelayResps();
}

// Add the DHCP options coming via config. Config priority is
// 1) options at VM interface level
// 2) options at subnet level
// 3) options at IPAM level
// Add the options defined at the highest level in priority
uint16_t DhcpHandler::AddConfigDhcpOptions(uint16_t opt_len,
                                           bool &domain_name_added,
                                           bool &dns_server_added) {
    std::vector<autogen::DhcpOptionType> options;
    if (!vm_itf_->GetDhcpOptions(&options))
        return opt_len;

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
                    DhcpOptions *opt = GetNextOptionPtr(opt_len);
                    opt->WriteWord(option_type, ip, opt_len);
                    if (option_type == DHCP_OPTION_DNS)
                        dns_server_added = true;
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
                if (!domain_name_added && options[i].dhcp_option_value.size()) {
                    domain_name_added = true;
                    DhcpOptions *opt = GetNextOptionPtr(opt_len);
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

    return opt_len;
}

// Add host route options coming via config. Config priority is
// 1) options at VM interface level
// 2) options at subnet level
// 3) options at IPAM level
// Add the options defined at the highest level in priority
uint16_t DhcpHandler::AddClasslessRouteOption(uint16_t opt_len) {
    std::vector<OperDhcpOptions::Subnet> host_routes;
    do {
        if (vm_itf_->oper_dhcp_options().are_host_routes_set()) {
            host_routes = vm_itf_->oper_dhcp_options().host_routes();
            break;
        }

        if (vm_itf_->vn()) {
            Ip4Address ip(config_.ip_addr);
            const std::vector<VnIpam> &vn_ipam = vm_itf_->vn()->GetVnIpam();
            uint32_t index;
            for (index = 0; index < vn_ipam.size(); ++index) {
                if (vn_ipam[index].IsSubnetMember(ip)) {
                    break;
                }
            }
            if (index < vn_ipam.size() &&
                vn_ipam[index].oper_dhcp_options.are_host_routes_set()) {
                host_routes = vn_ipam[index].oper_dhcp_options.host_routes();
                break;
            }

            vm_itf_->vn()->GetVnHostRoutes(ipam_name_, &host_routes);
            if (host_routes.size() > 0)
                break;
        }

        const std::vector<autogen::RouteType> &routes =
            ipam_type_.host_routes.route;
        for (unsigned int i = 0; i < routes.size(); ++i) {
            OperDhcpOptions::Subnet subnet;
            boost::system::error_code ec = Ip4PrefixParse(routes[i].prefix,
                                                          &subnet.prefix_,
                                                          (int *)&subnet.plen_);
            if (ec || subnet.plen_ > 32) {
                continue;
            }
            host_routes.push_back(subnet);
        }
    } while (false);

    if (host_routes.size()) {
        DhcpOptions *opt = GetNextOptionPtr(opt_len);
        opt->code = DHCP_OPTION_CLASSLESS_ROUTE;
        uint8_t *ptr = opt->data;
        uint8_t len = 0;
        for (uint32_t i = 0; i < host_routes.size(); ++i) {
            uint32_t prefix = host_routes[i].prefix_.to_ulong();
            uint32_t plen = host_routes[i].plen_;
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

uint16_t DhcpHandler::DhcpHdr(in_addr_t yiaddr, in_addr_t siaddr) {
    bool domain_name_added = false;
    bool dns_server_added = false;

    dhcp_->op = BOOT_REPLY;
    dhcp_->htype = HW_TYPE_ETHERNET;
    dhcp_->hlen = ETHER_ADDR_LEN;
    dhcp_->hops = 0;
    dhcp_->xid = request_.xid;
    dhcp_->secs = 0;
    dhcp_->flags = htons(request_.flags);
    dhcp_->ciaddr = 0;
    dhcp_->yiaddr = yiaddr;
    dhcp_->siaddr = siaddr;
    dhcp_->giaddr = 0;
    memset (dhcp_->chaddr, 0, DHCP_CHADDR_LEN);
    memcpy(dhcp_->chaddr, request_.mac_addr, ETHER_ADDR_LEN);
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

        if (config_.client_name_.size()) {
            opt = GetNextOptionPtr(opt_len);
            opt->WriteData(DHCP_OPTION_HOST_NAME, config_.client_name_.size(),
                           config_.client_name_.c_str(), opt_len);
        }

        // Add dhcp options coming from Config
        opt_len = AddConfigDhcpOptions(opt_len, domain_name_added,
                                       dns_server_added);

        // Add classless route option
        uint16_t old_opt_len = opt_len;
        opt_len = AddClasslessRouteOption(opt_len);

        // Add GW only if classless route option is not present
        if (opt_len == old_opt_len && config_.gw_addr) {
            opt = GetNextOptionPtr(opt_len);
            opt->WriteWord(DHCP_OPTION_ROUTER, config_.gw_addr, opt_len);
        }

        if (ipam_type_.ipam_dns_method == "default-dns-server" ||
            ipam_type_.ipam_dns_method == "") {
            if (!dns_server_added && config_.dns_addr) {
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
            if (!dns_server_added && config_.dns_addr) {
                opt = GetNextOptionPtr(opt_len);
                opt->WriteWord(DHCP_OPTION_DNS, config_.dns_addr, opt_len);
            }
            if (!domain_name_added && config_.domain_name_.size()) {
                opt = GetNextOptionPtr(opt_len);
                opt->WriteData(DHCP_OPTION_DOMAIN_NAME,
                               config_.domain_name_.size(),
                               config_.domain_name_.c_str(), opt_len);
            }
        }

    }

    opt = GetNextOptionPtr(opt_len);
    opt->code = DHCP_OPTION_END;
    opt_len += 1;

    return (DHCP_FIXED_LEN + opt_len);
}

uint16_t DhcpHandler::FillDhcpResponse(unsigned char *dest_mac,
                                       in_addr_t src_ip, in_addr_t dest_ip,
                                       in_addr_t siaddr, in_addr_t yiaddr) {

    pkt_info_->eth = (ether_header *)(pkt_info_->pkt + IPC_HDR_LEN);
    EthHdr(agent()->pkt()->pkt_handler()->mac_address(), dest_mac, 
           ETHERTYPE_IP);

    uint16_t header_len = sizeof(ether_header);
    
    if (vm_itf_->vlan_id() != VmInterface::kInvalidVlanId) {
        // cfi and priority are zero
        VlanHdr(pkt_info_->pkt + IPC_HDR_LEN + 12, vm_itf_->vlan_id());
        header_len += sizeof(vlanhdr);
    }
    pkt_info_->ip = (ip *)(pkt_info_->pkt + IPC_HDR_LEN + header_len);
    pkt_info_->transp.udp = (udphdr *)(pkt_info_->ip + 1);
    dhcphdr *dhcp = (dhcphdr *)(pkt_info_->transp.udp + 1);
    dhcp_ = dhcp;

    uint16_t len = DhcpHdr(yiaddr, siaddr);
    len += sizeof(udphdr);
    UdpHdr(len, src_ip, DHCP_SERVER_PORT, dest_ip, DHCP_CLIENT_PORT);

    len += sizeof(ip);
    
    IpHdr(len, src_ip, dest_ip, IPPROTO_UDP);

    return len + header_len;
}

void DhcpHandler::SendDhcpResponse() {
    // TODO: If giaddr is set, what to do ?

    in_addr_t src_ip = htonl(config_.gw_addr);
    in_addr_t dest_ip = 0xFFFFFFFF;
    in_addr_t yiaddr = htonl(config_.ip_addr);
    in_addr_t siaddr = src_ip;
    unsigned char dest_mac[ETHER_ADDR_LEN] = { 0xFF, 0xFF, 0xFF, 0xFF, 
                                               0xFF, 0xFF };

    // If requested IP address is not available, send NAK
    if ((msg_type_ == DHCP_REQUEST) && (request_.ip_addr) &&
        (config_.ip_addr != request_.ip_addr)) {
        out_msg_type_ = DHCP_NAK;
        yiaddr = 0;
        siaddr = 0;
    }

    // send a unicast response when responding to INFORM 
    // or when incoming giaddr is zero and ciaddr is set
    // or when incoming bcast flag is not set (with giaddr & ciaddr being zero)
    if ((msg_type_ == DHCP_INFORM) ||
        (!dhcp_->giaddr && (dhcp_->ciaddr || 
                            !(request_.flags & DHCP_BCAST_FLAG)))) {
        dest_ip = yiaddr;
        memcpy(dest_mac, dhcp_->chaddr, ETHER_ADDR_LEN);
        if (msg_type_ == DHCP_INFORM)
            yiaddr = 0;
    }
        
    UpdateStats();

    uint16_t len = FillDhcpResponse(dest_mac, src_ip, dest_ip, siaddr, yiaddr);
    Send(len, GetInterfaceIndex(), pkt_info_->vrf,
         AGENT_CMD_SWITCH, PktHandler::DHCP);
}

void DhcpHandler::UpdateStats() {
    DhcpProto *dhcp_proto = agent()->GetDhcpProto();
    (out_msg_type_ == DHCP_OFFER) ? dhcp_proto->IncrStatsOffers() :
        ((out_msg_type_ == DHCP_ACK) ? dhcp_proto->IncrStatsAcks() : 
                                       dhcp_proto->IncrStatsNacks());
}
