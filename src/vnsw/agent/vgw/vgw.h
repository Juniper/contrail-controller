/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */
#ifndef vnsw_agent_vgw_h
#define vnsw_agent_vgw_h

// Simple Virtual Gateway operational class
// Creates the interface, route and nexthop for virtual-gateway
class VirtualGateway {
public:
    VirtualGateway(Agent *agent);
    ~VirtualGateway() {};
    void InterfaceNotify(DBTablePartBase *partition, DBEntryBase *entry);

    void Init();
    void Shutdown();
    void CreateVrf();
    void CreateVrf(const std::string &vrf_name);
    void DeleteVrf(const std::string &vrf_name);
    void CreateInterfaces();
    void CreateInterface(const std::string &interface_name,
                         const std::string &vrf_name);
    void DeleteInterface(const std::string &interface_name);
    void SubnetUpdate(const VirtualGatewayConfig &vgw,
                      const VirtualGatewayConfig::SubnetList &add_list,
                      const VirtualGatewayConfig::SubnetList &del_list);
    void RouteUpdate(const VirtualGatewayConfig &vgw,
                     const VirtualGatewayConfig::SubnetList &new_list,
                     const VirtualGatewayConfig::SubnetList &add_list,
                     const VirtualGatewayConfig::SubnetList &del_list,
                     bool add_default_route);
    void RegisterDBClients();

private:
    void SubnetUpdate(const std::string &vrf,
                      Inet4UnicastAgentRouteTable *rt_table,
                      const VirtualGatewayConfig::SubnetList &add_list,
                      const VirtualGatewayConfig::SubnetList &del_list);
    void RouteUpdate(const VirtualGatewayConfig &vgw, uint32_t new_list_size,
                     Inet4UnicastAgentRouteTable *rt_table,
                     const VirtualGatewayConfig::SubnetList &add_list,
                     const VirtualGatewayConfig::SubnetList &del_list,
                     bool add_default_route, bool del_default_route);

    // Cached entries
    Agent *agent_;
    DBTableBase::ListenerId listener_id_;
    VirtualGatewayConfigTable *vgw_config_table_;

    DISALLOW_COPY_AND_ASSIGN(VirtualGateway);
};

#endif //vnsw_agent_vgw_h
