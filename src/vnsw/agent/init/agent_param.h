/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_param_hpp
#define vnsw_agent_param_hpp

#include <boost/property_tree/ptree.hpp>
#include <boost/program_options.hpp>

class VirtualGatewayConfigTable;

// Class handling agent configuration parameters from config file and 
// arguments
class AgentParam  {
public:
    // Hypervisor mode we are working on
    enum Mode {
        MODE_INVALID,
        MODE_KVM,
        MODE_XEN,
        MODE_VMWARE
    };

    struct PortInfo {
        PortInfo() : 
            name_(""), vrf_(""), addr_(0), prefix_(0), plen_(0), gw_(0) {};
        ~PortInfo() { };

        std::string name_;
        std::string vrf_;
        Ip4Address addr_;
        Ip4Address prefix_;
        int plen_;
        Ip4Address gw_;
    };

    AgentParam(Agent *agent);
    virtual ~AgentParam();

    bool IsVHostConfigured() {
        return vhost_.addr_.to_ulong() != 0? true : false;
    }

    const std::string &vhost_name() const { return vhost_.name_; }
    const Ip4Address &vhost_addr() const { return vhost_.addr_; }
    const Ip4Address &vhost_prefix() const { return vhost_.prefix_; }
    const int vhost_plen() const { return vhost_.plen_; }
    const Ip4Address &vhost_gw() const { return vhost_.gw_; }

    const std::string &xen_ll_name() const { return xen_ll_.name_; }
    const void set_xen_ll_name(const std::string &name) { 
        xen_ll_.name_ = name;
    }
    const Ip4Address &xen_ll_addr() const { return xen_ll_.addr_; }
    const Ip4Address &xen_ll_prefix() const { return xen_ll_.prefix_; }
    const int xen_ll_plen() const { return xen_ll_.plen_; }
    const Ip4Address &xen_ll_gw() const { return xen_ll_.gw_; }

    const std::string &eth_port() const { return eth_port_; }
    const Ip4Address &xmpp_server_1() const { return xmpp_server_1_; }
    const Ip4Address &xmpp_server_2() const { return xmpp_server_2_; }
    const Ip4Address &dns_server_1() const { return dns_server_1_; }
    const Ip4Address &dns_server_2() const { return dns_server_2_; }
    const Ip4Address &discovery_server() const { return dss_server_; }
    const Ip4Address &mgmt_ip() const { return mgmt_ip_; }
    const int xmpp_instance_count() const { return xmpp_instance_count_; }
    const std::string &tunnel_type() const { return tunnel_type_; }
    const std::string &metadata_shared_secret() const { return metadata_shared_secret_; }
    uint32_t linklocal_system_flows() const { return linklocal_system_flows_; }
    uint32_t linklocal_vm_flows() const { return linklocal_vm_flows_; }
    uint32_t flow_cache_timeout() const {return flow_cache_timeout_;}

    const std::string &config_file() const { return config_file_; }
    const std::string &program_name() const { return program_name_;}
    const std::string log_file() const { return log_file_; }
    bool log_local() const { return log_local_; }
    const std::string &log_level() const { return log_level_; }
    const std::string &log_category() const { return log_category_; }
    const Ip4Address &collector() const { return collector_; }
    uint16_t collector_port() const {return collector_port_; }
    uint16_t http_server_port() const { return http_server_port_; }
    const std::string &host_name() const { return host_name_; }
    int agent_stats_interval() const { return agent_stats_interval_; }
    int flow_stats_interval() const { return flow_stats_interval_; }
    void set_agent_stats_interval(int val) { agent_stats_interval_ = val; }
    void set_flow_stats_interval(int val) { flow_stats_interval_ = val; }
    VirtualGatewayConfigTable *vgw_config_table() const { 
        return vgw_config_table_.get();
    }
    const std::string &vmware_physical_port() const {
        return vmware_physical_port_;
    }
    bool debug() const { return debug_; }

    Mode mode() const { return mode_; }
    bool isXenMode() const { return mode_ == MODE_XEN; }
    bool isKvmMode() const { return mode_ == MODE_KVM; }
    bool isVmwareMode() const { return mode_ == MODE_VMWARE; }

    void Init(const std::string &config_file,
              const std::string &program_name,
              const boost::program_options::variables_map &var_map);

    void Validate();
    void LogConfig() const;
    void InitVhostAndXenLLPrefix();
    void set_test_mode(bool mode);
    bool test_mode() const { return test_mode_; }
private:
    void ComputeLinkLocalFlowLimits();
    void InitFromSystem();
    void InitFromConfig();
    void InitFromArguments
        (const boost::program_options::variables_map &var_map);
    template <typename ValueType>
    bool GetOptValue(const boost::program_options::variables_map &var_map, 
                     ValueType &var, const std::string &val);
    template <typename ValueType>
    bool GetValueFromTree(ValueType &var, const std::string &val);
    bool GetIpAddress(const std::string &str, Ip4Address *addr);
    bool ParseIp(const std::string &key, Ip4Address *server);
    bool ParseServerList(const std::string &key, Ip4Address *s1, Ip4Address *s2);
    void ParseIpArgument(const boost::program_options::variables_map &var_map, 
                         Ip4Address &server, const std::string &key);
    bool ParseServerListArguments
    (const boost::program_options::variables_map &var_map, Ip4Address &server1,
     Ip4Address &server2, const std::string &key);
    void ParseCollector();
    void ParseVirtualHost();
    void ParseDiscovery();
    void ParseNetworks();
    void ParseHypervisor();
    void ParseDefaultSection();
    void ParseMetadataProxy();
    void ParseLinklocal();

    void ParseCollectorArguments
        (const boost::program_options::variables_map &v);
    void ParseVirtualHostArguments
        (const boost::program_options::variables_map &v);
    void ParseDiscoveryArguments
        (const boost::program_options::variables_map &v);
    void ParseNetworksArguments
        (const boost::program_options::variables_map &v);
    void ParseHypervisorArguments
        (const boost::program_options::variables_map &v);
    void ParseDefaultSectionArguments
        (const boost::program_options::variables_map &v);
    void ParseMetadataProxyArguments
        (const boost::program_options::variables_map &v);
    void ParseLinklocalArguments
        (const boost::program_options::variables_map &v);

    PortInfo vhost_;
    std::string eth_port_;
    uint16_t xmpp_instance_count_;
    Ip4Address xmpp_server_1_;
    Ip4Address xmpp_server_2_;
    Ip4Address dns_server_1_;
    Ip4Address dns_server_2_;
    Ip4Address dss_server_;
    Ip4Address mgmt_ip_;
    Mode mode_;
    PortInfo xen_ll_;
    std::string tunnel_type_;
    std::string metadata_shared_secret_;
    uint16_t linklocal_system_flows_;
    uint16_t linklocal_vm_flows_;
    uint16_t flow_cache_timeout_;

    // Parameters configured from command linke arguments only (for now)
    std::string config_file_;
    std::string program_name_;
    std::string log_file_;
    bool log_local_;
    std::string log_level_;
    std::string log_category_;
    Ip4Address collector_;
    uint16_t collector_port_;
    uint16_t http_server_port_;
    std::string host_name_;
    int agent_stats_interval_;
    int flow_stats_interval_;
    std::string vmware_physical_port_;
    bool test_mode_;
    bool debug_;
    boost::property_tree::ptree tree_;
    std::auto_ptr<VirtualGatewayConfigTable> vgw_config_table_;

    DISALLOW_COPY_AND_ASSIGN(AgentParam);
};

#endif // vnsw_agent_param_hpp
