/*
 * Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
 */

#include "xmpp_server_address_parser.h"

#include <algorithm>
#include <sstream>
#include <boost/algorithm/string.hpp>

XmppServerAddressParser::XmppServerAddressParser(uint16_t default_port, uint32_t max_servers_count) :
    default_port(default_port),
    max_servers_count(max_servers_count) {

}

void XmppServerAddressParser::ParseAddress(const std::string &address,
                                           std::string *out_ip, uint16_t *out_port) const {
    std::vector<std::string> parts;
    boost::split(parts, address, boost::is_any_of(":"));

    *out_ip = parts[0];
    *out_port = default_port;

    if (parts.size() >= 2) {
        uint16_t port;
        std::istringstream converter(parts[1]);
        if (converter >> port) {
            *out_port = port;
        }
    }
}

void XmppServerAddressParser::ParseAddresses(const std::vector<std::string> &addresses,
                                             std::string out_ips[], uint16_t out_ports[]) const {
    const uint32_t count = std::min(static_cast<uint32_t>(addresses.size()), max_servers_count);
    for (uint32_t i = 0; i < count; ++i) {
        ParseAddress(addresses[i], &out_ips[i], &out_ports[i]);
    }
}
