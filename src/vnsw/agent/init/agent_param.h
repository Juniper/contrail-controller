/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_param_hpp
#define vnsw_agent_param_hpp

#include <boost/property_tree/ptree.hpp>
#include <boost/program_options.hpp>
#include <cmn/agent_cmn.h>

class Agent;
class VirtualGatewayConfigTable;

// Class handling agent configuration parameters from config file and 
// arguments
class AgentParam  {
public:
    static const uint32_t kAgentStatsInterval = (30 * 1000); // time in millisecs
    static const uint32_t kFlowStatsInterval = (1000); // time in milliseconds
    static const uint32_t kVrouterStatsInterval = (30 * 1000); //time-millisecs
    typedef std::vector<Ip4Address> AddressList;

    // Agent mode we are running in
    enum AgentMode {
        VROUTER_AGENT,
        TSN_AGENT,
        TOR_AGENT,
    };

    // Hypervisor mode we are working on
    enum HypervisorMode {
        MODE_INVALID,
        MODE_KVM,
        MODE_XEN,
        MODE_VMWARE,
        MODE_DOCKER,
    };

    enum VmwareMode {
        ESXI_NEUTRON,
        VCENTER
    };

    enum Platform {
        VROUTER_ON_HOST,
        VROUTER_ON_HOST_DPDK,
        VROUTER_ON_NIC
    };

    struct PortInfo {
        PortInfo() : 
            name_(""), vrf_(""), addr_(0), prefix_(0), plen_(0), gw_(0) {}
        ~PortInfo() { };

        std::string name_;
        std::string vrf_;
        Ip4Address addr_;
        Ip4Address prefix_;
        int plen_;
        Ip4Address gw_;
    };

    AgentParam(bool enable_flow_options = true,
               bool enable_vhost_options = true,
               bool enable_hypervisor_options = true,
               bool enable_service_options = true,
               AgentMode agent_mode = VROUTER_AGENT);
    virtual ~AgentParam();

    virtual int Validate();

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

    const std::string &agent_name() const { return agent_name_; }
    const std::string &eth_port() const { return eth_port_; }
    const bool &eth_port_no_arp() const { return eth_port_no_arp_; }
    const std::string &eth_port_encap_type() const { return eth_port_encap_type_; }
    const Ip4Address &xmpp_server_1() const { return xmpp_server_1_; }
    const Ip4Address &xmpp_server_2() const { return xmpp_server_2_; }
    const Ip4Address &dns_server_1() const { return dns_server_1_; }
    const Ip4Address &dns_server_2() const { return dns_server_2_; }
    const uint16_t dns_port_1() const { return dns_port_1_; }
    const uint16_t dns_port_2() const { return dns_port_2_; }
    const uint16_t dns_client_port() const {
        if (test_mode_)
            return 0;
        return dns_client_port_;
    }
    const uint16_t mirror_client_port() const {
        if (test_mode_)
            return 0;
        return mirror_client_port_;
    }
    const std::string &discovery_server() const { return dss_server_; }
    const Ip4Address &mgmt_ip() const { return mgmt_ip_; }
    const int xmpp_instance_count() const { return xmpp_instance_count_; }
    const std::string &tunnel_type() const { return tunnel_type_; }
    const std::string &metadata_shared_secret() const { return metadata_shared_secret_; }
    uint16_t metadata_proxy_port() const {
        if (test_mode_)
            return 0;
        return metadata_proxy_port_;
    }
    float max_vm_flows() const { return max_vm_flows_; }
    uint32_t linklocal_system_flows() const { return linklocal_system_flows_; }
    uint32_t linklocal_vm_flows() const { return linklocal_vm_flows_; }
    uint32_t flow_cache_timeout() const {return flow_cache_timeout_;}
    uint16_t flow_index_sm_log_count() const {return flow_index_sm_log_count_;}
    bool headless_mode() const {return headless_mode_;}
    bool dhcp_relay_mode() const {return dhcp_relay_mode_;}
    bool xmpp_auth_enabled() const {return xmpp_auth_enable_;}
    std::string xmpp_server_cert() const { return xmpp_server_cert_;}
    std::string xmpp_server_key() const { return xmpp_server_key_;}
    std::string xmpp_ca_cert() const { return xmpp_ca_cert_;}
    bool xmpp_dns_auth_enabled() const {return xmpp_dns_auth_enable_;}
    bool simulate_evpn_tor() const {return simulate_evpn_tor_;}
    std::string si_netns_command() const {return si_netns_command_;}
    std::string si_docker_command() const {return si_docker_command_;}
    const int si_netns_workers() const {return si_netns_workers_;}
    const int si_netns_timeout() const {return si_netns_timeout_;}
    std::string si_lb_ssl_cert_path() const {
        return si_lb_ssl_cert_path_;
    }
    std::string si_lb_keystone_auth_conf_path() const {
        return si_lb_keystone_auth_conf_path_;
    }

    std::string nexthop_server_endpoint() const {
        return nexthop_server_endpoint_;
    }

    const std::string &config_file() const { return config_file_; }
    const std::string &program_name() const { return program_name_;}
    const std::string log_file() const { return log_file_; }
    const int log_files_count() const { return log_files_count_; }
    const long log_file_size() const { return log_file_size_; }
    bool log_local() const { return log_local_; }
    bool log_flow() const { return log_flow_; }
    const std::string &log_level() const { return log_level_; }
    const std::string &log_category() const { return log_category_; }
    const std::string &log_property_file() const { return log_property_file_; }
    const bool use_syslog() const { return use_syslog_; }
    const std::string syslog_facility() const { return syslog_facility_; }
    const std::vector<std::string> collector_server_list() const {
        return collector_server_list_;
    }
    uint16_t http_server_port() const { return http_server_port_; }
    const std::string &host_name() const { return host_name_; }
    int agent_stats_interval() const { return agent_stats_interval_; }
    int flow_stats_interval() const { return flow_stats_interval_; }
    int vrouter_stats_interval() const { return vrouter_stats_interval_; }
    void set_agent_stats_interval(int val) { agent_stats_interval_ = val; }
    void set_flow_stats_interval(int val) { flow_stats_interval_ = val; }
    void set_vrouter_stats_interval(int val) { vrouter_stats_interval_ = val; }
    void set_subnet_hosts_resolvable(bool val) {
        subnet_hosts_resolvable_ = val;
    }
    VirtualGatewayConfigTable *vgw_config_table() const { 
        return vgw_config_table_.get();
    }
    const std::string &vmware_physical_port() const {
        return vmware_physical_port_;
    }
    bool debug() const { return debug_; }

    HypervisorMode mode() const { return hypervisor_mode_; }
    bool isXenMode() const { return hypervisor_mode_ == MODE_XEN; }
    bool isKvmMode() const { return hypervisor_mode_ == MODE_KVM; }
    bool isDockerMode() const { return hypervisor_mode_ == MODE_DOCKER; }
    bool isVmwareMode() const { return hypervisor_mode_ == MODE_VMWARE; }
    bool isVmwareVcenterMode() const { return vmware_mode_ == VCENTER; }
    VmwareMode vmware_mode() const { return vmware_mode_; }
    Platform platform() const { return platform_; }
    bool vrouter_on_nic_mode() const {
        return platform_ == VROUTER_ON_NIC;
    }
    bool vrouter_on_host_dpdk() const {
        return platform_ == VROUTER_ON_HOST_DPDK;
    }
    bool vrouter_on_host() const {
        return platform_ == VROUTER_ON_HOST;
    }
    bool subnet_hosts_resolvable() const {
        return subnet_hosts_resolvable_;
    }

    void Init(const std::string &config_file,
              const std::string &program_name);

    void LogConfig() const;
    void PostValidateLogConfig() const;
    void InitVhostAndXenLLPrefix();
    void InitPlatform();
    void set_test_mode(bool mode);
    bool test_mode() const { return test_mode_; }

    void AddOptions(const boost::program_options::options_description &opt);
    void ParseArguments(int argc, char *argv[]);
    const boost::program_options::variables_map &var_map() const {
        return var_map_;
    }

    boost::program_options::options_description options() const {
        return options_;
    }
    AgentMode agent_mode() const { return agent_mode_; }
    bool isTsnAgent() const { return agent_mode_ == TSN_AGENT; }
    bool isTorAgent() const { return agent_mode_ == TOR_AGENT; }

    const AddressList &compute_node_address_list() const {
        return compute_node_address_list_;
    }
    void BuildAddressList(const std::string &val);

    std::string exception_packet_interface() const {
        return exception_packet_interface_;
    }
    std::string physical_interface_pci_addr() const {
        return physical_interface_pci_addr_;
    }
    std::string physical_interface_mac_addr() const {
        return physical_interface_mac_addr_;
    }
    std::string agent_base_dir() const { return agent_base_dir_; }
    uint32_t sandesh_send_rate_limit() { return send_ratelimit_; }
    const std::string &bgp_as_a_service_port_range() const {
        return bgp_as_a_service_port_range_;
    }

    uint16_t flow_thread_count() const { return flow_thread_count_; }
    void set_flow_thread_count(uint16_t count) { flow_thread_count_ = count; }

    uint32_t tbb_thread_count() const { return tbb_thread_count_; }
    uint32_t tbb_exec_delay() const { return tbb_exec_delay_; }
    uint32_t tbb_schedule_delay() const { return tbb_schedule_delay_; }
    uint32_t tbb_keepawake_timeout() const { return tbb_keepawake_timeout_; }

    // pkt0 tx buffer
    uint32_t pkt0_tx_buffer_count() const { return pkt0_tx_buffer_count_; }
    void set_pkt0_tx_buffer_count(uint32_t val) { pkt0_tx_buffer_count_ = val; }
protected:
    void set_hypervisor_mode(HypervisorMode m) { hypervisor_mode_ = m; }
    virtual void InitFromSystem();
    virtual void InitFromConfig();
    virtual void InitFromArguments();
    boost::property_tree::ptree &tree() { return tree_; }
    template <typename ValueType>
    bool GetOptValue(const boost::program_options::variables_map &var_map, 
                     ValueType &var, const std::string &val) {
        return GetOptValueImpl(var_map, var, val,
            static_cast<ValueType *>(0));
    }
    // Implementation overloads
    template <typename ValueType>
    bool GetOptValueImpl(const boost::program_options::variables_map &var_map,
                         ValueType &var, const std::string &val, ValueType*) {
        // Check if the value is present.
        if (var_map.count(val) && !var_map[val].defaulted()) {
            var = var_map[val].as<ValueType>();
            return true;
        }
        return false;
    }
    template <typename ElementType>
    bool GetOptValueImpl(const boost::program_options::variables_map &var_map,
                         std::vector<ElementType> &var, const std::string &val,
                         std::vector<ElementType> *);
    template <typename ValueType>
    bool GetValueFromTree(ValueType &var, const std::string &val) {
        boost::optional<ValueType> opt;

        if (opt = tree_.get_optional<ValueType>(val)) {
            var = opt.get();
            return true;
        }
        return false;
    }
    bool GetIpAddress(const std::string &str, Ip4Address *addr);
    bool ParseIp(const std::string &key, Ip4Address *server);
    bool ParseServerList(const std::string &key, Ip4Address *s1, Ip4Address *s2);
    bool ParseAddress(const std::string &addr_string,
                      Ip4Address *server, uint16_t *port);
    bool ParseServerList(const std::string &key, Ip4Address *server1,
                         uint16_t *port1, Ip4Address *server2, uint16_t *port2);
    void ParseIpArgument(const boost::program_options::variables_map &var_map, 
                         Ip4Address &server, const std::string &key);
    bool ParseServerListArguments
    (const boost::program_options::variables_map &var_map, Ip4Address &server1,
     Ip4Address &server2, const std::string &key);
    bool ParseServerListArguments
    (const boost::program_options::variables_map &var_map, Ip4Address *server1,
     uint16_t *port1, Ip4Address *server2, uint16_t *port2,
     const std::string &key);

private:
    friend class AgentParamTest;
    void ComputeFlowLimits();
    void ParseCollector();
    void ParseVirtualHost();
    void ParseDns();
    void ParseDiscovery();
    void ParseNetworks();
    void ParseHypervisor();
    void ParseDefaultSection();
    void ParseTaskSection();
    void ParseMetadataProxy();
    void ParseFlows();
    void ParseHeadlessMode();
    void ParseDhcpRelayMode();
    void ParseSimulateEvpnTor();
    void ParseServiceInstance();
    void ParseAgentInfo();
    void ParseNexthopServer();
    void ParsePlatform();
    void ParseBgpAsAServicePortRange();
    void set_agent_mode(const std::string &mode);

    void ParseCollectorArguments
        (const boost::program_options::variables_map &v);
    void ParseVirtualHostArguments
        (const boost::program_options::variables_map &v);
    void ParseDnsArguments
        (const boost::program_options::variables_map &v);
    void ParseDiscoveryArguments
        (const boost::program_options::variables_map &v);
    void ParseNetworksArguments
        (const boost::program_options::variables_map &v);
    void ParseHypervisorArguments
        (const boost::program_options::variables_map &v);
    void ParseDefaultSectionArguments
        (const boost::program_options::variables_map &v);
    void ParseTaskSectionArguments
        (const boost::program_options::variables_map &v);
    void ParseMetadataProxyArguments
        (const boost::program_options::variables_map &v);
    void ParseFlowArguments
        (const boost::program_options::variables_map &v);
    void ParseHeadlessModeArguments
        (const boost::program_options::variables_map &v);
    void ParseDhcpRelayModeArguments
        (const boost::program_options::variables_map &var_map);
    void ParseServiceInstanceArguments
        (const boost::program_options::variables_map &v);
    void ParseAgentInfoArguments
        (const boost::program_options::variables_map &v);
    void ParseNexthopServerArguments
        (const boost::program_options::variables_map &v);
    void ParsePlatformArguments
        (const boost::program_options::variables_map &v);
    void ParseBgpAsAServicePortRangeArguments
        (const boost::program_options::variables_map &v);

    boost::program_options::variables_map var_map_;
    boost::program_options::options_description options_;
    bool enable_flow_options_;
    bool enable_vhost_options_;
    bool enable_hypervisor_options_;
    bool enable_service_options_;
    AgentMode agent_mode_;

    Agent *agent_;
    PortInfo vhost_;
    // Number of tx-buffers on pkt0 device
    uint32_t pkt0_tx_buffer_count_;

    std::string agent_name_;
    std::string eth_port_;
    bool eth_port_no_arp_;
    std::string eth_port_encap_type_;
    uint16_t xmpp_instance_count_;
    Ip4Address xmpp_server_1_;
    Ip4Address xmpp_server_2_;
    Ip4Address dns_server_1_;
    Ip4Address dns_server_2_;
    uint16_t dns_port_1_;
    uint16_t dns_port_2_;
    uint16_t dns_client_port_;
    uint16_t mirror_client_port_;
    std::string dss_server_;
    uint16_t dss_port_;
    Ip4Address mgmt_ip_;
    HypervisorMode hypervisor_mode_;
    PortInfo xen_ll_;
    std::string tunnel_type_;
    std::string metadata_shared_secret_;
    uint16_t metadata_proxy_port_;
    float max_vm_flows_;
    uint16_t linklocal_system_flows_;
    uint16_t linklocal_vm_flows_;
    uint16_t flow_cache_timeout_;
    uint16_t flow_index_sm_log_count_;

    // Parameters configured from command line arguments only (for now)
    std::string config_file_;
    std::string program_name_;
    std::string log_file_;
    int log_files_count_;
    long log_file_size_;
    std::string log_property_file_;

    bool log_local_;
    bool log_flow_;
    std::string log_level_;
    std::string log_category_;
    bool use_syslog_;
    std::string syslog_facility_;
    std::vector<std::string> collector_server_list_;
    uint16_t http_server_port_;
    std::string host_name_;
    int agent_stats_interval_;
    int flow_stats_interval_;
    int vrouter_stats_interval_;
    std::string vmware_physical_port_;
    bool test_mode_;
    bool debug_;
    boost::property_tree::ptree tree_;
    std::auto_ptr<VirtualGatewayConfigTable> vgw_config_table_;
    bool headless_mode_;
    bool dhcp_relay_mode_;
    bool xmpp_auth_enable_;
    std::string xmpp_server_cert_;
    std::string xmpp_server_key_;
    std::string xmpp_ca_cert_;
    bool xmpp_dns_auth_enable_;
    //Simulate EVPN TOR mode moves agent into L2 mode. This mode is required
    //only for testing where MX and bare metal are simulated. VM on the
    //simulated compute node behaves as bare metal.
    bool simulate_evpn_tor_;
    std::string si_netns_command_;
    std::string si_docker_command_;
    int si_netns_workers_;
    int si_netns_timeout_;
    std::string si_lb_ssl_cert_path_;
    std::string si_lb_keystone_auth_conf_path_;
    VmwareMode vmware_mode_;
    // List of IP addresses on the compute node.
    AddressList compute_node_address_list_;
    std::string nexthop_server_endpoint_;
    bool nexthop_server_add_pid_;
    bool vrouter_on_nic_mode_;
    std::string exception_packet_interface_;
    Platform platform_;
    std::string physical_interface_pci_addr_;
    std::string physical_interface_mac_addr_;
    std::string agent_base_dir_;
    uint32_t send_ratelimit_;
    uint16_t flow_thread_count_;
    bool subnet_hosts_resolvable_;
    std::string bgp_as_a_service_port_range_;

    // TBB related
    uint32_t tbb_thread_count_;
    uint32_t tbb_exec_delay_;
    uint32_t tbb_schedule_delay_;
    uint32_t tbb_keepawake_timeout_;
    DISALLOW_COPY_AND_ASSIGN(AgentParam);
};

#endif // vnsw_agent_param_hpp
