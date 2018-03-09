/*
 * Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
 */

#ifndef VNSW_AGENT_CMN_XMPP_SERVER_ADDRESS_PARSER_HPP
#define VNSW_AGENT_CMN_XMPP_SERVER_ADDRESS_PARSER_HPP

#include <string>
#include <vector>
#include <stdint.h>

class XmppServerAddressParser {
public:
    XmppServerAddressParser(uint16_t default_port, uint32_t max_servers_count);
    void ParseAddress(const std::string &address, std::string *out_ip, uint16_t *out_port) const;
    void ParseAddresses(const std::vector<std::string> &addresses,
                        std::string out_ips[], uint16_t out_ports[]) const;

private:
    uint16_t default_port;
    uint32_t max_servers_count;
};

#endif // VNSW_AGENT_CMN_XMPP_SERVER_ADDRESS_PARSER_HPP
