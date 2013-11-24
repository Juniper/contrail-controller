/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */
#ifndef vnsw_agent_vgw_cfg_h
#define vnsw_agent_vgw_cfg_h

#include <net/address.h>

// Simple Virtual Gateway config class
// Supports virtual-gateway for single virtual-network for now.
class VirtualGatewayConfig {
public:
    VirtualGatewayConfig() : vrf_(""), interface_(""), ip_(0), plen_(0) { }
    ~VirtualGatewayConfig() {};

    const std::string &vrf() const {return vrf_;};
    const std::string &interface() const {return interface_;};
    const Ip4Address &ip() const {return ip_;};
    uint8_t plen() const {return plen_;};

    void Init(const char *init_file);
    void Shutdown();
private:
    // Public network name
    std::string vrf_;
    // Interface connecting gateway to host-os
    std::string interface_;
    // IP address of interface connecting to host-os
    Ip4Address ip_;
    // Prefix-Len
    uint8_t plen_;

    void OnIntfCreate(DBEntryBase *entry);
    DISALLOW_COPY_AND_ASSIGN(VirtualGatewayConfig);
};
#endif //vnsw_agent_vgw_cfg_h
