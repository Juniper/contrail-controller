/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */
#ifndef vnsw_agent_vgw_cfg_h
#define vnsw_agent_vgw_cfg_h

#include <net/address.h>

class InetInterface;

// Simple Virtual Gateway config class
// Supports virtual-gateway for single virtual-network for now.
class VirtualGatewayConfig {
public:
    struct Subnet {
        Subnet() : ip_(0), plen_(0) {}
        Subnet(const Ip4Address &ip, uint8_t plen) : ip_(ip), plen_(plen) {}
        ~Subnet() {}
        bool operator<(const Subnet &rhs) const {
            if (ip_ != rhs.ip_)
                return ip_ < rhs.ip_;

            return (plen_ < rhs.plen_);
        } 

        Ip4Address ip_;
        uint8_t plen_;
    };
    typedef std::vector<Subnet> SubnetList;

    VirtualGatewayConfig(const std::string &interface) :
        interface_(interface), vrf_(""), subnets_(), routes_(),
        inet_interface_() { }
    VirtualGatewayConfig(const std::string &interface, const std::string &vrf,
                         const SubnetList &subnets, const SubnetList &routes) : 
        interface_(interface), vrf_(vrf), subnets_(subnets), routes_(routes) {}
    VirtualGatewayConfig(const VirtualGatewayConfig &rhs) :
        interface_(rhs.interface_), vrf_(rhs.vrf_), subnets_(rhs.subnets_),
        routes_(rhs.routes_) {}
    ~VirtualGatewayConfig() {}

    const std::string interface() const {return interface_;}
    const std::string vrf() const {return vrf_;}
    const SubnetList subnets() const {return subnets_;}
    const SubnetList routes() const {return routes_;}
    void set_subnets(const SubnetList &subnets) const { subnets_ = subnets; }
    void set_routes(const SubnetList &routes) const { routes_ = routes; }
    const InetInterface *inet_interface() const { return inet_interface_; }
    void set_inet_interface(InetInterface *interface) const {
        inet_interface_ = interface;
    }

private:
    // Interface connecting gateway to host-os
    std::string interface_;
    // Public network name
    std::string vrf_;
    // Vector of subnets
    mutable SubnetList subnets_;
    // Vector of routes
    mutable SubnetList routes_;
    // Inet interface pointer
    mutable InetInterface *inet_interface_;
};

class VirtualGatewayConfigTable {
public:
    struct VirtualGatewayConfigCompare {
        bool operator()(const VirtualGatewayConfig &cfg1,
                        const VirtualGatewayConfig &cfg2) const {
            return cfg1.interface() < cfg2.interface();
        }
    };

    typedef std::set<VirtualGatewayConfig, VirtualGatewayConfigCompare> Table;

    VirtualGatewayConfigTable(Agent *agent) : agent_(agent) { }
    ~VirtualGatewayConfigTable() { }

    void Init(const char *init_file);
    void Shutdown();
    const Table &table() const {return table_;}

    void AddVgw(const std::string &interface, const std::string &vrf,
                VirtualGatewayConfig::SubnetList &subnets,
                VirtualGatewayConfig::SubnetList &routes);
    void DeleteVgw(const std::string &interface);

private:
    bool FindChange(const VirtualGatewayConfig::SubnetList &old_subnets,
                    const VirtualGatewayConfig::SubnetList &new_subnets,
                    VirtualGatewayConfig::SubnetList &add_list,
                    VirtualGatewayConfig::SubnetList &del_list);

    Agent *agent_;
    Table table_;
    DISALLOW_COPY_AND_ASSIGN(VirtualGatewayConfigTable);
};

#endif //vnsw_agent_vgw_cfg_h
