/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */
#ifndef vnsw_agent_vgw_cfg_h
#define vnsw_agent_vgw_cfg_h

#include <net/address.h>
#include <boost/program_options.hpp>

// Simple Virtual Gateway config class
// Supports virtual-gateway for single virtual-network for now.
class VirtualGatewayConfig {
public:
    struct Subnet {
        Subnet() : ip_(0), plen_(0) {}
        Subnet(const Ip4Address &ip, uint8_t plen) : ip_(ip), plen_(plen) {}
        ~Subnet() {}

        Ip4Address ip_;
        uint8_t plen_;
    };
    typedef std::vector<Subnet> SubnetList;

    VirtualGatewayConfig(const std::string &interface) :
        interface_(interface), vrf_(""), subnets_(), routes_() { }
    VirtualGatewayConfig(const std::string &interface, const std::string &vrf,
                         const SubnetList &subnets, const SubnetList &routes) : 
        interface_(interface), vrf_(vrf), subnets_(subnets), routes_(routes) {}
    VirtualGatewayConfig(const std::string &interface, const std::string &vrf,
                         const SubnetList &subnets) : 
        interface_(interface), vrf_(vrf), subnets_(subnets), routes_() {}
    VirtualGatewayConfig(const VirtualGatewayConfig &rhs) :
        interface_(rhs.interface_), vrf_(rhs.vrf_), subnets_(rhs.subnets_),
        routes_(rhs.routes_) {}
    ~VirtualGatewayConfig() {}

    const std::string interface() const {return interface_;}
    const std::string vrf() const {return vrf_;}
    SubnetList &subnets() {return subnets_;}
    SubnetList &routes() {return routes_;}
private:
    // Interface connecting gateway to host-os
    std::string interface_;
    // Public network name
    std::string vrf_;
    // Vector of subnets
    SubnetList subnets_;
    // Vector of routes
    SubnetList routes_;
};

class VirtualGatewayConfigTable {
public:
    typedef std::map<std::string, VirtualGatewayConfig> Table;
    typedef std::pair<std::string, VirtualGatewayConfig> VgwCfgPair;

    VirtualGatewayConfigTable() { }
    ~VirtualGatewayConfigTable() { }

    void Init(const boost::program_options::variables_map &var_map);
    void Shutdown();
    Table &table() {return table_;}
private:
    bool VgwCfgItems(const std::string &input, std::string &vrf, 
                     std::string &itf, Ip4Address &addr, int &plen);
    Table table_;
    DISALLOW_COPY_AND_ASSIGN(VirtualGatewayConfigTable);
};

#endif //vnsw_agent_vgw_cfg_h
