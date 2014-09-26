/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "vr_defs.h"
#include "cmn/agent_cmn.h"
#include "pkt/pkt_init.h"
#include "oper/vn.h"
#include "services/dhcp_proto.h"
#include "services/dhcpv6_proto.h"
#include "services/services_types.h"
#include "services/services_init.h"
#include "services/dns_proto.h"
#include "services/services_sandesh.h"
#include "bind/bind_util.h"

#include <boost/assign/list_of.hpp>
#include <bind/bind_util.h>

using namespace boost::assign;

DhcpHandlerBase::DhcpHandlerBase(Agent *agent, boost::shared_ptr<PktInfo> info,
                                 boost::asio::io_service &io)
        : ProtoHandler(agent, info, io), vm_itf_(NULL), vm_itf_index_(-1),
          option_(NULL), flags_(), host_routes_level_(Invalid) {
    ipam_type_.ipam_dns_method = "none";
}

DhcpHandlerBase::~DhcpHandlerBase() {
}

// Add option taking no data (length = 0)
uint16_t DhcpHandlerBase::AddNoDataOption(uint32_t option, uint16_t opt_len) {
    option_->WriteData(option, 0, NULL, &opt_len);
    return opt_len;
}

// Add option taking a byte from input
uint16_t DhcpHandlerBase::AddByteOption(uint32_t option, uint16_t opt_len,
                                        const std::string &input) {
    std::stringstream value(input);
    uint32_t data = 0;
    value >> data;
    if (value.bad() || value.fail() || data > 0xFF) {
        DHCP_BASE_TRACE("Invalid DHCP option " << option << " data : " <<
                        input << "is invalid");
    } else {
        uint8_t byte = (uint8_t)data;
        option_->WriteData(option, 1, &byte, &opt_len);
    }
    return opt_len;
}

// Add option taking array of bytes from input
uint16_t DhcpHandlerBase::AddByteArrayOption(uint32_t option, uint16_t opt_len,
                                             const std::string &input) {
    option_->WriteData(option, 0, NULL, &opt_len);
    std::stringstream value(input);
    uint8_t byte = 0;
    value >> byte;
    while (!value.bad() && !value.fail()) {
        option_->AppendData(1, &byte, &opt_len);
        if (value.eof()) break;
        value >> byte;
    }

    // if atleast one byte is not added, ignore this option
    if (!option_->GetLen() || !value.eof()) {
        DHCP_BASE_TRACE("Invalid DHCP option " << option << " data : " <<
                        input << "is invalid");
        return opt_len - option_->GetLen() - option_->GetFixedLen();
    }

    return opt_len;
}

// Add option taking a byte followed by a string, from input
uint16_t DhcpHandlerBase::AddByteStringOption(uint32_t option, uint16_t opt_len,
                                              const std::string &input) {
    std::stringstream value(input);
    uint32_t data = 0;
    value >> data;
    if (value.fail() || value.bad() || data > 0xFF) {
        DHCP_BASE_TRACE("Invalid DHCP option " << option << " data : " <<
                        input << "is invalid");
        return opt_len;
    }

    uint8_t byte = (uint8_t)data;
    std::string str;
    value >> str;
    option_->WriteData(option, 1, &byte, &opt_len);
    option_->AppendData(str.length(), str.c_str(), &opt_len);

    return opt_len;
}

// Add option taking a byte followed by one or more IPs, from input
uint16_t DhcpHandlerBase::AddByteIPOption(uint32_t option, uint16_t opt_len,
                                          const std::string &input) {
    std::stringstream value(input);
    uint32_t data = 0;
    value >> data;
    uint8_t byte = (uint8_t)data;

    if (value.fail() || value.bad() || data > 0xFF) {
        DHCP_BASE_TRACE("Invalid DHCP option " << option << " data : " <<
                        input << "is invalid");
        return opt_len;
    }

    option_->WriteData(option, 1, &byte, &opt_len);
    while (value.good()) {
        std::string ipstr;
        value >> ipstr;
        opt_len = AddIP(opt_len, ipstr);
    }

    // if atleast one IP is not added, ignore this option
    if (option_->GetLen() == 1) {
        DHCP_BASE_TRACE("Invalid DHCP option " << option << " data : " <<
                        input << "is invalid");
        return opt_len - option_->GetLen() - option_->GetFixedLen();
    }

    return opt_len;
}

// Add option taking string from input
uint16_t DhcpHandlerBase::AddStringOption(uint32_t option, uint16_t opt_len,
                                          const std::string &input) {
    option_->WriteData(option, input.length(), input.c_str(), &opt_len);
    return opt_len;
}

// Add option taking integer from input
uint16_t DhcpHandlerBase::AddIntegerOption(uint32_t option, uint16_t opt_len,
                                           const std::string &input) {
    std::stringstream value(input);
    uint32_t data = 0;
    value >> data;
    if (value.bad() || value.fail()) {
        DHCP_BASE_TRACE("Invalid DHCP option " << option << " data : " <<
                        input << "is invalid");
    } else {
        data = htonl(data);
        option_->WriteData(option, 4, &data, &opt_len);
    }
    return opt_len;
}

// Add option taking array of short from input
uint16_t DhcpHandlerBase::AddShortArrayOption(uint32_t option, uint16_t opt_len,
                                              const std::string &input,
                                              bool array) {
    option_->WriteData(option, 0, NULL, &opt_len);
    std::stringstream value(input);
    uint16_t data = 0;
    value >> data;
    while (!value.bad() && !value.fail()) {
        data = htons(data);
        option_->AppendData(2, &data, &opt_len);
        if (!array || value.eof()) break;
        value >> data;
    }

    // if atleast one short is not added, ignore this option
    if (!option_->GetLen() || !value.eof()) {
        DHCP_BASE_TRACE("Invalid DHCP option " << option << " data : " <<
                        input << "is invalid");
        return opt_len - option_->GetLen() - option_->GetFixedLen();
    }

    return opt_len;
}

// Add option taking number of Ipv4 addresses
uint16_t DhcpHandlerBase::AddIpv4Option(uint32_t option, uint16_t opt_len,
                                        const std::string &input,
                                        uint8_t min_count,
                                        uint8_t max_count,
                                        uint8_t multiples) {
    option_->WriteData(option, 0, NULL, &opt_len);
    std::stringstream value(input);
    while (value.good()) {
        std::string ipstr;
        value >> ipstr;
        opt_len = AddIP(opt_len, ipstr);
    }

    if (option == DHCP_OPTION_DNS) {
        // Add DNS servers from the IPAM dns method and server config also
        opt_len = AddDnsServers(opt_len);
    } else if (option == DHCP_OPTION_ROUTER) {
        // Add our gw as well
        if (!config_.gw_addr.is_unspecified()) {
            opt_len = AddIP(opt_len, config_.gw_addr.to_string());
        }
    }

    // check that atleast min_count IP addresses are added
    if (min_count && option_->GetLen() < min_count * 4) {
        DHCP_BASE_TRACE("Invalid DHCP option " << option << " data : " <<
                        input << "is invalid");
        return opt_len - option_->GetLen() - option_->GetFixedLen();
    }

    // if specified, check that we dont add more than max_count IP addresses
    if (max_count && option_->GetLen() > max_count * 4) {
        DHCP_BASE_TRACE("Invalid DHCP option " << option << " data : " <<
                        input << "is invalid");
        return opt_len - option_->GetLen() - option_->GetFixedLen();
    }

    // if specified, check that we add in multiples of IP addresses
    if (multiples && option_->GetLen() % (multiples * 4)) {
        DHCP_BASE_TRACE("Invalid DHCP option " << option << " data : " <<
                        input << "is invalid");
        return opt_len - option_->GetLen() - option_->GetFixedLen();
    }

    return opt_len;
}

// Add option taking number of Ipv6 addresses
uint16_t DhcpHandlerBase::AddIpv6Option(uint32_t option, uint16_t opt_len,
                                        const std::string &input, bool list) {
    option_->WriteData(option, 0, NULL, &opt_len);
    std::stringstream value(input);
    while (value.good()) {
        std::string ipstr;
        value >> ipstr;
        opt_len = AddIP(opt_len, ipstr);
        if (!list) break;
    }

    if (option == DHCPV6_OPTION_DNS_SERVERS) {
        // Add DNS servers from the IPAM dns method and server config also
        opt_len = AddDnsServers(opt_len);
    }

    // check that atleast one IP address is added
    if (option_->GetLen() < 16) {
        DHCPV6_TRACE(Error, "Invalid DHCP option " << option << " for VM " <<
                     config_.ip_addr.to_string() << "; data missing");
        return opt_len - option_->GetLen() - option_->GetFixedLen();
    }

    return opt_len;
}

// Add option taking compressed name
uint16_t DhcpHandlerBase::AddCompressedNameOption(uint32_t option,
                                                  uint16_t opt_len,
                                                  const std::string &input,
                                                  bool list) {
    option_->WriteData(option, 0, NULL, &opt_len);
    std::stringstream value(input);
    while (value.good()) {
        std::string str;
        value >> str;
        if (str.size()) {
            opt_len = AddCompressedName(opt_len, str);
            if (!list) break;
        }
    }

    if (!option_->GetLen()) {
        DHCP_BASE_TRACE("Invalid DHCP option " << option << " data : " <<
                        input << "is invalid");
        return opt_len - option_->GetLen() - option_->GetFixedLen();
    }

    return opt_len;
}

// Add option taking a byte followed by a compressed name
uint16_t DhcpHandlerBase::AddByteCompressedNameOption(uint32_t option,
                                                      uint16_t opt_len,
                                                      const std::string &input) {
    option_->WriteData(option, 0, NULL, &opt_len);

    std::stringstream value(input);
    uint32_t data = 0;
    value >> data;
    if (value.bad() || value.fail() || data > 0xFF) {
        DHCP_BASE_TRACE("Invalid DHCP option " << option << " data : " <<
                        input << "is invalid");
        return opt_len - option_->GetLen() - option_->GetFixedLen();
    } else {
        uint8_t byte = (uint8_t)data;
        option_->WriteData(option, 1, &byte, &opt_len);
    }

    std::string str;
    value >> str;
    if (value.bad() || value.fail() || !str.size()) {
        DHCP_BASE_TRACE("Invalid DHCP option " << option << " data : " <<
                        input << "is invalid");
        return opt_len - option_->GetLen() - option_->GetFixedLen();
    }
    return AddCompressedName(opt_len, str);
}

uint16_t DhcpHandlerBase::AddCompressedName(uint16_t opt_len,
                                            const std::string &input) {
    uint8_t name[input.size() * 2 + 2];
    uint16_t len = 0;
    BindUtil::AddName(name, input, 0, 0, len);
    option_->AppendData(len, name, &opt_len);
    return opt_len;
}

// Add an DNS server addresses from IPAM to the option
// Option code and len are added already; add only the addresses
uint16_t DhcpHandlerBase::AddDnsServers(uint16_t opt_len) {
    if (ipam_type_.ipam_dns_method == "default-dns-server" ||
        ipam_type_.ipam_dns_method == "virtual-dns-server" ||
        ipam_type_.ipam_dns_method == "") {
        if (!config_.dns_addr.is_unspecified()) {
            opt_len = AddIP(opt_len, config_.dns_addr.to_string());
        }
    } else if (ipam_type_.ipam_dns_method == "tenant-dns-server") {
        for (unsigned int i = 0; i < ipam_type_.ipam_dns_server.
             tenant_dns_server_address.ip_address.size(); ++i) {
            opt_len = AddIP(opt_len, ipam_type_.ipam_dns_server.
                                     tenant_dns_server_address.ip_address[i]);
        }
    }
    return opt_len;
}

// Read the list of <subnet/plen, gw> from input and store them
// for later processing in AddClasslessRouteOption
void DhcpHandlerBase::ReadClasslessRoute(uint32_t option, uint16_t opt_len,
                                         const std::string &input) {
    std::stringstream value(input);
    while (value.good()) {
        std::string snetstr;
        value >> snetstr;
        OperDhcpOptions::Subnet subnet;
        boost::system::error_code ec = Ip4PrefixParse(snetstr, &subnet.prefix_,
                                                      (int *)&subnet.plen_);
        if (ec || subnet.plen_ > 32 || !value.good()) {
            DHCP_BASE_TRACE("Invalid Classless route DHCP option -"
                            "has to be list of <subnet/plen gw>");
            break;
        }
        host_routes_.push_back(subnet);

        // ignore the gw, as we use always use subnet's gw
        value >> snetstr;
    }
}

// Add host route options coming via config. Config priority is
// 1) options at VM interface level (host routes specifically set)
// 2) options at VM interface level (classless routes from --extra-dhcp-opts)
// 3) options at subnet level (host routes at subnet level - neutron config)
// 4) options at subnet level (classless routes from dhcp options)
// 5) options at IPAM level (host routes from IPAM)
// 6) options at IPAM level (classless routes from IPAM dhcp options)
// Add the options defined at the highest level in priority
uint16_t DhcpHandlerBase::AddClasslessRouteOption(uint16_t opt_len) {
    std::vector<OperDhcpOptions::Subnet> host_routes;
    do {
        // Port specific host route options
        // TODO: should we remove host routes at port level from schema ?
        if (vm_itf_->oper_dhcp_options().are_host_routes_set()) {
            host_routes = vm_itf_->oper_dhcp_options().host_routes();
            break;
        }
        // Host routes from port specific DHCP options (neutron configuration)
        if (host_routes_level_ == InterfaceLevel && host_routes_.size()) {
            host_routes.swap(host_routes_);
            break;
        }

        if (vm_itf_->vn()) {
            Ip4Address ip = config_.ip_addr.to_v4();
            const std::vector<VnIpam> &vn_ipam = vm_itf_->vn()->GetVnIpam();
            uint32_t index;
            for (index = 0; index < vn_ipam.size(); ++index) {
                if (vn_ipam[index].IsSubnetMember(ip)) {
                    break;
                }
            }
            if (index < vn_ipam.size()) {
                // Subnet level host route options
                // Preference to host routes at subnet (neutron configuration)
                if (vn_ipam[index].oper_dhcp_options.are_host_routes_set()) {
                    host_routes = vn_ipam[index].oper_dhcp_options.host_routes();
                    break;
                }
                // Host route options from subnet level DHCP options
                if (host_routes_level_ == SubnetLevel && host_routes_.size()) {
                    host_routes.swap(host_routes_);
                    break;
                }
            }

            // TODO: should remove host routes at VN level from schema ?
            vm_itf_->vn()->GetVnHostRoutes(ipam_name_, &host_routes);
            if (host_routes.size() > 0)
                break;
        }

        // IPAM level host route options
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
        // Host route options from IPAM level DHCP options
        if (!host_routes.size() && host_routes_.size()) {
            host_routes.swap(host_routes_);
            break;
        }
    } while (false);

    if (host_routes.size()) {
        option_->SetCode(DHCP_OPTION_CLASSLESS_ROUTE);
        uint8_t *ptr = option_->GetData();
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
            *(uint32_t *)ptr = htonl(config_.gw_addr.to_v4().to_ulong());
            ptr += sizeof(uint32_t);
            len += sizeof(uint32_t);
        }
        option_->SetLen(len);
        opt_len += 2 + len;
        set_flag(DHCP_OPTION_CLASSLESS_ROUTE);
    }
    return opt_len;
}

uint16_t DhcpHandlerBase::AddDhcpOptions(
                        uint16_t opt_len,
                        std::vector<autogen::DhcpOptionType> &options,
                        DhcpOptionLevel level) {
    for (unsigned int i = 0; i < options.size(); ++i) {
        // get the option code
        uint32_t option = OptionCode(options[i].dhcp_option_name);
        if (!option) {
            DHCP_BASE_TRACE("Invalid DHCP option " <<
                            options[i].dhcp_option_name);
            continue;
        }

        // if option is already set in the response (from higher level), ignore
        if (is_flag_set(option))
            continue;

        uint16_t old_opt_len = opt_len;
        DhcpOptionCategory category = OptionCategory(option);
        option_->SetNextOptionPtr(opt_len);

        switch(category) {
            case None:
                break;

            case NoData:
                opt_len = AddNoDataOption(option, opt_len);
                break;

            case Bool:
            case Byte:
                opt_len = AddByteOption(option, opt_len,
                                        options[i].dhcp_option_value);
                break;

            case ByteArray:
                opt_len = AddByteArrayOption(option, opt_len,
                                             options[i].dhcp_option_value);
                break;

            case ByteString:
                opt_len = AddByteStringOption(option, opt_len,
                                              options[i].dhcp_option_value);
                break;

            case ByteOneIPPlus:
                opt_len = AddByteIPOption(option, opt_len,
                                          options[i].dhcp_option_value);
                break;

            case String:
                opt_len = AddStringOption(option, opt_len,
                                          options[i].dhcp_option_value);
                break;

            case Int32bit:
            case Uint32bit:
                opt_len = AddIntegerOption(option, opt_len,
                                           options[i].dhcp_option_value);
                break;

            case Uint16bit:
                opt_len = AddShortArrayOption(option, opt_len,
                                              options[i].dhcp_option_value,
                                              false);
                break;

            case Uint16bitArray:
                opt_len = AddShortArrayOption(option, opt_len,
                                              options[i].dhcp_option_value,
                                              true);
                break;

            case OneIPv4:
                opt_len = AddIpv4Option(option, opt_len,
                                        options[i].dhcp_option_value, 1, 1, 0);
                break;

            case ZeroIPv4Plus:
                opt_len = AddIpv4Option(option, opt_len,
                                        options[i].dhcp_option_value, 0, 0, 0);
                break;

            case OneIPv4Plus:
                if (option == DHCP_OPTION_ROUTER) {
                    // Router option is added later
                    routers_ = options[i].dhcp_option_value;
                } else {
                    opt_len = AddIpv4Option(option, opt_len,
                                            options[i].dhcp_option_value,
                                            1, 0, 0);
                }
                break;

            case TwoIPv4Plus:
                opt_len = AddIpv4Option(option, opt_len,
                                        options[i].dhcp_option_value, 2, 0, 2);
                break;

            case OneIPv6:
                opt_len = AddIpv6Option(option, opt_len,
                                        options[i].dhcp_option_value,
                                        false);
                break;

            case OneIPv6Plus:
                opt_len = AddIpv6Option(option, opt_len,
                                        options[i].dhcp_option_value,
                                        true);
                break;

            case ClasslessRoute:
                ReadClasslessRoute(option, opt_len,
                                   options[i].dhcp_option_value);
                if (host_routes_.size()) {
                    // set flag & level for classless route so that we dont
                    // update these at other level (subnet or ipam)
                    set_flag(DHCP_OPTION_CLASSLESS_ROUTE);
                    host_routes_level_ = level;
                }
                break;

            case NameCompression:
                opt_len = AddCompressedNameOption(option, opt_len,
                                                  options[i].dhcp_option_value,
                                                  false);
                break;

            case NameCompressionArray:
                opt_len = AddCompressedNameOption(option, opt_len,
                                                  options[i].dhcp_option_value,
                                                  true);
                break;

            case ByteNameCompression:
                opt_len = AddByteCompressedNameOption(option, opt_len,
                                                  options[i].dhcp_option_value);
                break;

            default:
                DHCP_BASE_TRACE("Invalid DHCP option " <<
                                options[i].dhcp_option_name);
                break;
        }

        if (opt_len != old_opt_len)
            set_flag(option);
    }

    return opt_len;
}

// Add the option defined at the highest level in priority
// Take all options defined at VM interface level, take those from subnet which
// are not configured at interface, then take those from ipam which are not
// configured at interface or subnet.
uint16_t DhcpHandlerBase::AddConfigDhcpOptions(uint16_t opt_len, bool is_v6) {
    std::vector<autogen::DhcpOptionType> options;
    if (vm_itf_->GetInterfaceDhcpOptions(&options))
        opt_len = AddDhcpOptions(opt_len, options, InterfaceLevel);

    if (vm_itf_->GetSubnetDhcpOptions(&options, is_v6))
        opt_len = AddDhcpOptions(opt_len, options, SubnetLevel);

    if (vm_itf_->GetIpamDhcpOptions(&options, is_v6))
        opt_len = AddDhcpOptions(opt_len, options, IpamLevel);

    return opt_len;
}

void DhcpHandlerBase::FindDomainName(const IpAddress &vm_addr) {
    if (config_.lease_time != (uint32_t) -1 ||
        config_.valid_time != (uint32_t) -1)
        return;

    if (!vm_itf_->vn() || 
        !vm_itf_->vn()->GetIpamData(vm_addr, &ipam_name_, &ipam_type_)) {
        DHCP_BASE_TRACE("Ipam data not found");
        return;
    }

    if (ipam_type_.ipam_dns_method != "virtual-dns-server" ||
        !agent()->domain_config_table()->GetVDns(ipam_type_.ipam_dns_server.
                                                 virtual_dns_server_name,
                                                 &vdns_type_))
        return;

    if (config_.domain_name_.size() &&
        config_.domain_name_ != vdns_type_.domain_name) {
        DHCP_BASE_TRACE("Client domain " << config_.domain_name_ << 
                        " doesnt match with configured domain " << 
                        vdns_type_.domain_name);
    }
    std::size_t pos;
    if (config_.client_name_.size() &&
        ((pos = config_.client_name_.find('.', 0)) != std::string::npos) &&
        (config_.client_name_.substr(pos + 1) != vdns_type_.domain_name)) {
        DHCP_BASE_TRACE("Client domain doesnt match with configured domain "
                        << vdns_type_.domain_name << "; Client name = " 
                        << config_.client_name_);
        config_.client_name_.replace(config_.client_name_.begin() + pos + 1,
                                     config_.client_name_.end(),
                                     vdns_type_.domain_name);
    }

    config_.domain_name_ = vdns_type_.domain_name;
}
