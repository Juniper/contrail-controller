/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */
#ifndef vnsw_agent_vgw_cfg_h
#define vnsw_agent_vgw_cfg_h

#include <net/address.h>
#include <boost/property_tree/ptree.hpp>

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

    VirtualGatewayConfig(const std::string &interface_name) :
        interface_name_(interface_name), vrf_name_(""), subnets_(), routes_(),
        interface_(), version_(0) {}
    VirtualGatewayConfig(const std::string &interface_name,
                         const std::string &vrf_name,
                         const SubnetList &subnets,
                         const SubnetList &routes,
                         uint32_t version) : 
        interface_name_(interface_name), vrf_name_(vrf_name),
        subnets_(subnets), routes_(routes), version_(version) {}
    VirtualGatewayConfig(const VirtualGatewayConfig &rhs) :
        interface_name_(rhs.interface_name_), vrf_name_(rhs.vrf_name_),
        subnets_(rhs.subnets_), routes_(rhs.routes_), version_(rhs.version_) {}
    ~VirtualGatewayConfig() {}

    const std::string& interface_name() const { return interface_name_; }
    const std::string& vrf_name() const { return vrf_name_; }
    const SubnetList& subnets() const { return subnets_; }
    const SubnetList& routes() const { return routes_; }
    uint32_t version() const { return version_; }
    void set_subnets(const SubnetList &subnets) const { subnets_ = subnets; }
    void set_routes(const SubnetList &routes) const { routes_ = routes; }
    const InetInterface *interface() const { return interface_; }
    void set_interface(InetInterface *interface) const {
        interface_ = interface;
    }
    void set_version(uint32_t version) const { version_ = version; }

private:
    // Interface connecting gateway to host-os
    std::string interface_name_;
    // Public network name
    std::string vrf_name_;
    // Vector of subnets
    mutable SubnetList subnets_;
    // Vector of routes
    mutable SubnetList routes_;
    // Inet interface pointer
    mutable InetInterface *interface_;
    // client version number of the entry
    mutable uint32_t version_;
};

struct VirtualGatewayInfo {
    std::string interface_name_;
    std::string vrf_name_;
    VirtualGatewayConfig::SubnetList subnets_;
    VirtualGatewayConfig::SubnetList routes_;

    VirtualGatewayInfo(const std::string &interface)
        : interface_name_(interface) {}
    VirtualGatewayInfo(const std::string &interface, const std::string &vrf,
                       VirtualGatewayConfig::SubnetList &subnets,
                       VirtualGatewayConfig::SubnetList &routes)
        : interface_name_(interface), vrf_name_(vrf) {
        subnets_.swap(subnets);
        routes_.swap(routes);
    }
};

struct VirtualGatewayData {
    enum MessageType {
        Add,
        Delete,
        Audit
    };

    MessageType message_type_;
    std::vector<VirtualGatewayInfo> vgw_list_;
    uint32_t version_;

    VirtualGatewayData(MessageType type, std::vector<VirtualGatewayInfo> &list,
                       uint32_t version)
        : message_type_(type), version_(version) {
        vgw_list_.swap(list);
    }
    VirtualGatewayData(MessageType type, uint32_t version)
        : message_type_(type), version_(version) {}
};

class VirtualGatewayConfigTable {
public:
    struct VirtualGatewayConfigCompare {
        bool operator()(const VirtualGatewayConfig &cfg1,
                        const VirtualGatewayConfig &cfg2) const {
            return cfg1.interface_name() < cfg2.interface_name();
        }
    };

    typedef std::set<VirtualGatewayConfig, VirtualGatewayConfigCompare> Table;

    VirtualGatewayConfigTable(Agent *agent) : agent_(agent),
        work_queue_(TaskScheduler::GetInstance()->GetTaskId("db::DBTable"), 0,
                    boost::bind(&VirtualGatewayConfigTable::ProcessRequest,
                                this, _1)) { }
    ~VirtualGatewayConfigTable() { }

    void Init(const boost::property_tree::ptree pt);
    void Shutdown();
    const Table &table() const {return table_;}

    void Enqueue(boost::shared_ptr<VirtualGatewayData> request);
    bool ProcessRequest(boost::shared_ptr<VirtualGatewayData> request);

private:
    void BuildSubnetList(const std::string &subnets, 
                         VirtualGatewayConfig::SubnetList &results);
    bool AddVgw(VirtualGatewayInfo &vgw, uint32_t version);
    bool DeleteVgw(const std::string &interface_name);
    void DeleteVgw(Table::iterator it);
    void DeleteAllOldVersionVgw(uint32_t version);
    bool FindChange(const VirtualGatewayConfig::SubnetList &old_subnets,
                    const VirtualGatewayConfig::SubnetList &new_subnets,
                    VirtualGatewayConfig::SubnetList &add_list,
                    VirtualGatewayConfig::SubnetList &del_list);

    Agent *agent_;
    Table table_;
    WorkQueue<boost::shared_ptr<VirtualGatewayData> > work_queue_;
    DISALLOW_COPY_AND_ASSIGN(VirtualGatewayConfigTable);
};

#endif //vnsw_agent_vgw_cfg_h
