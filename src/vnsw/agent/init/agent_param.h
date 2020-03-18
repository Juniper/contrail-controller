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

struct LlgrParams {
public:
    //In seconds
    static const int kStaleConfigCleanupTime = 100;
    static const int kConfigPollTime = 5;
    static const int kConfigInactivityTime = 15;
    static const int kConfigFallbackTimeOut = 900;
    static const int kEorTxPollTime = 5;
    static const int kEorTxFallbackTimeOut = 60;
    static const int kEorTxInactivityTime = 15;
    static const int kEorRxFallbackTime = 60;
    static const int kLlgrStaleTime = 2592000; //One month

    LlgrParams();
    virtual ~LlgrParams() { }

    /*
     * stale_config_cleanup_time_ - On receiving all config, remove stale
     * config.
     */
    uint16_t stale_config_cleanup_time() const {return stale_config_cleanup_time_;}

    /*
     * config_poll_time_ - Timer poll time
     */
    uint16_t config_poll_time() const {return config_poll_time_;}
    /*
     * config_inactivity_time_ - Silence time to conclude end of config
     */
    uint16_t config_inactivity_time() const {
        return config_inactivity_time_;
    }
    /*
     * config_fallback_time_ - Maximum time to wait for silence. In case
     * silence is never seen use this time to conclude end of config.
     */
    uint16_t config_fallback_time() const {
        return config_fallback_time_;
    }

    /*
     * end_of_rib_tx_poll_time_ - End of rib timer poll time.
     */
    uint16_t end_of_rib_tx_poll_time() const {
        return end_of_rib_tx_poll_time_;
    }
    /*
     * end_of_rib_tx_fallback_time_ - Maximum time to wait for silence, if
     * silence not seen then use this time to conclude fallback
     */
    uint16_t end_of_rib_tx_fallback_time() const {
        return end_of_rib_tx_fallback_time_;
    }
    /*
     * end_of_rib_tx_inactivity_time_ - Silence time on route publish to
     * conclude end of rib
     */
    uint16_t end_of_rib_tx_inactivity_time() const {
        return end_of_rib_tx_inactivity_time_;
    }

    /*
     * end_of_rib_rx_fallback_time_ - Maximum time to wait for end of rib from
     * CN on a channel
     */
    uint16_t end_of_rib_rx_fallback_time() const {
        return end_of_rib_rx_fallback_time_;
    }

    /*
     * llgr_stale_time_ - Maximum time to wait after CN is not ready to retain
     * stale routes.
     */
    uint32_t llgr_stale_time() const {
        return llgr_stale_time_;
    }

private:
    friend class AgentParam;

    /** stale config cleanup time */
    uint16_t stale_config_cleanup_time_;
    /** end of config timer time values */
    uint16_t config_poll_time_;
    uint16_t config_inactivity_time_;
    uint16_t config_fallback_time_;
    /** End of rib Tx times */
    uint16_t end_of_rib_tx_poll_time_;
    uint16_t end_of_rib_tx_fallback_time_;
    uint16_t end_of_rib_tx_inactivity_time_;
    /** End of rib rx times */
    uint16_t end_of_rib_rx_fallback_time_;
    uint32_t llgr_stale_time_;
};

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
        TSN_NO_FORWARDING_AGENT,
        TOR_AGENT,
    };

    // Gateway mode that the agent is running in
    enum GatewayMode {
        VCPE,
        SERVER, // also has VMs on a remote server & vrouter maps vlans to VMIs
        PBB,
        NONE
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

    std::map<std::string, uint32_t> trace_buff_size_map;
    std::map<std::string, uint32_t>::iterator  trace_buff_size_iter;
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
    void set_vhost_addr(const Ip4Address &ip) {
        vhost_.addr_ = ip;
    }
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

    const std::string &crypt_port() const { return crypt_port_; }
    const bool &crypt_port_no_arp() const { return crypt_port_no_arp_; }
    const std::string &crypt_port_encap_type() const { return crypt_port_encap_type_; }

    const std::vector<std::string> controller_server_list() const {
        return controller_server_list_;
    }
    const std::string &subcluster_name() const { return subcluster_name_; }
    const std::vector<std::string> dns_server_list() const {
        return dns_server_list_;
    }
    const std::vector<std::string> tsn_server_list() const {
        return tsn_server_list_;
    }
    const uint16_t dns_client_port() const {
        if (test_mode_)
            return 0;
        return dns_client_port_;
    }
    const uint32_t dns_timeout() const { return dns_timeout_; }
    const uint32_t dns_max_retries() const { return dns_max_retries_; }
    const uint16_t mirror_client_port() const {
        if (test_mode_)
            return 0;
        return mirror_client_port_;
    }
    const Ip4Address &mgmt_ip() const { return mgmt_ip_; }
    const std::string &tunnel_type() const { return tunnel_type_; }
    const std::string &metadata_shared_secret() const { return metadata_shared_secret_; }
    uint16_t metadata_proxy_port() const {
        if (test_mode_)
            return 0;
        return metadata_proxy_port_;
    }
    const bool metadata_use_ssl() const { return metadata_use_ssl_; }
    std::string metadata_client_cert() const { return metadata_client_cert_;}
    std::string metadata_client_cert_type() const {
        return metadata_client_cert_type_;
    }
    std::string metadata_client_key() const { return metadata_client_key_;}
    std::string metadata_ca_cert() const { return metadata_ca_cert_;}
    float max_vm_flows() const { return max_vm_flows_; }
    uint32_t linklocal_system_flows() const { return linklocal_system_flows_; }
    uint32_t linklocal_vm_flows() const { return linklocal_vm_flows_; }
    uint32_t flow_cache_timeout() const {return flow_cache_timeout_;}
    uint16_t flow_index_sm_log_count() const {return flow_index_sm_log_count_;}
    uint32_t flow_add_tokens() const {return flow_add_tokens_;}
    uint32_t flow_ksync_tokens() const {return flow_ksync_tokens_;}
    uint32_t flow_del_tokens() const {return flow_del_tokens_;}
    uint32_t flow_update_tokens() const {return flow_update_tokens_;}
    uint32_t stale_interface_cleanup_timeout() const {
        return stale_interface_cleanup_timeout_;
    }
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
    std::string si_lbaas_auth_conf() const {
        return si_lbaas_auth_conf_;
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
    const std::vector<std::string> &get_sample_destination() {
        return sample_destination_;
    }
    const std::vector<std::string> &get_slo_destination() {
        return slo_destination_;
    }
    const std::string &log_level() const { return log_level_; }
    const std::string &log_category() const { return log_category_; }
    const std::string &log_property_file() const { return log_property_file_; }
    const bool use_syslog() const { return use_syslog_; }
    const std::string syslog_facility() const { return syslog_facility_; }
    const std::vector<std::string> collector_server_list() const {
        return collector_server_list_;
    }
    const std::map<std::string, std::map<std::string, std::string> > derived_stats_map() const {
        return derived_stats_map_;
    }
    uint16_t http_server_port() const { return http_server_port_; }
    const std::string &host_name() const { return host_name_; }
    int agent_stats_interval() const {
        if (test_mode_) {
            return agent_stats_interval_;
        }
        return vmi_vm_vn_uve_interval_msecs();
    }
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
    void ReInit();
    void DebugInit();

    void LogConfig() const;
    void LogFilteredConfig() const;
    void PostValidateLogConfig() const;
    void InitVhostAndXenLLPrefix();
    void InitPlatform();
    void set_test_mode(bool mode);
    bool test_mode() const { return test_mode_; }

    void AddOptions(const boost::program_options::options_description &opt);
    void ConfigAddOptions(const boost::program_options::options_description &opt);
    void ParseArguments(int argc, char *argv[]);
    const boost::program_options::variables_map &var_map() const {
        return var_map_;
    }

    boost::program_options::options_description options() const {
        return options_;
    }
    AgentMode agent_mode() const { return agent_mode_; }
    bool isTsnAgent() const {
        return ((agent_mode_ == TSN_AGENT) |
                (agent_mode_ == TSN_NO_FORWARDING_AGENT));
    }
    bool isTorAgent() const { return agent_mode_ == TOR_AGENT; }
    bool IsForwardingEnabled() const {
        return agent_mode_ != TSN_NO_FORWARDING_AGENT;
    }
    bool isServerGatewayMode() const { return gateway_mode_ == SERVER; }
    bool isVcpeGatewayMode() const { return gateway_mode_ == VCPE; }
    bool isPbbGatewayMode() const { return gateway_mode_ == PBB; }
    GatewayMode gateway_mode() const { return gateway_mode_; }

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
    const std::string &bgp_as_a_service_port_range() const {
        return bgp_as_a_service_port_range_;
    }
    const std::vector<uint16_t> &bgp_as_a_service_port_range_value() const {
        return bgp_as_a_service_port_range_value_;
    }
    uint32_t services_queue_limit() { return services_queue_limit_; }
    uint32_t bgpaas_max_shared_sessions() const {
        return bgpaas_max_shared_sessions_;
    }
    void set_bgpaas_max_shared_sessions(uint32_t val) {
        bgpaas_max_shared_sessions_ = val;
    }


    const SandeshConfig &sandesh_config() const { return sandesh_config_; }

    uint16_t flow_thread_count() const { return flow_thread_count_; }
    void set_flow_thread_count(uint16_t count) { flow_thread_count_ = count; }

    bool flow_trace_enable() const { return flow_trace_enable_; }
    void set_flow_trace_enable(bool val) { flow_trace_enable_ = val; }

    bool flow_use_rid_in_hash() const { return !flow_hash_excl_rid_; }

    uint16_t flow_task_latency_limit() const { return flow_latency_limit_; }
    void set_flow_task_latency_limit(uint16_t count) {
        flow_latency_limit_ = count;
    }

    uint16_t max_sessions_per_aggregate() const {
        return max_sessions_per_aggregate_;
    }
    uint16_t max_aggregates_per_session_endpoint() const {
        return max_aggregates_per_session_endpoint_;
    }
    uint16_t max_endpoints_per_session_msg() const {
        return max_endpoints_per_session_msg_;
    }
    std::string ksync_thread_cpu_pin_policy() const {
        return ksync_thread_cpu_pin_policy_;
    }
    uint32_t tbb_thread_count() const { return tbb_thread_count_; }
    uint32_t tbb_exec_delay() const { return tbb_exec_delay_; }
    uint32_t tbb_schedule_delay() const { return tbb_schedule_delay_; }
    uint32_t tbb_keepawake_timeout() const { return tbb_keepawake_timeout_; }
    uint32_t task_monitor_timeout_msec() const {
        return task_monitor_timeout_msec_;
    }
    uint16_t min_aap_prefix_len() const {
        if ((min_aap_prefix_len_ < 1) || (min_aap_prefix_len_ > 31)) {
            return Agent::kMinAapPrefixLen;
        }
        return min_aap_prefix_len_;
    }
    uint16_t vmi_vm_vn_uve_interval() const { return vmi_vm_vn_uve_interval_; }
    uint32_t vmi_vm_vn_uve_interval_msecs() const {
        return (vmi_vm_vn_uve_interval_ * 1000);
    }

    uint16_t fabric_snat_hash_table_size() const {
        return fabric_snat_hash_table_size_;
    }

    // Restart parameters
    bool restart_backup_enable() const { return restart_backup_enable_; }
    void set_restart_backup_enable(bool val) {
        restart_backup_enable_ = val;
    }
    uint64_t restart_backup_idle_timeout() const {
        return restart_backup_idle_timeout_;
    }
    const std::string& restart_backup_dir() const {
        return restart_backup_dir_;
    }
    uint16_t restart_backup_count() const { return restart_backup_count_; }
    bool restart_restore_enable() const { return restart_restore_enable_; }
    uint64_t restart_restore_audit_timeout() const {
        return restart_restore_audit_timeout_;
    }

    const std::string huge_page_file_1G(uint16_t index) const {
        if (huge_page_file_1G_.size() > index)
            return huge_page_file_1G_[index];
        else
            return "";
    }
    const std::string huge_page_file_2M(uint16_t index) const {
        if (huge_page_file_2M_.size() > index)
            return huge_page_file_2M_[index];
        else
            return "";
    }

    // pkt0 tx buffer
    uint32_t pkt0_tx_buffer_count() const { return pkt0_tx_buffer_count_; }
    void set_pkt0_tx_buffer_count(uint32_t val) { pkt0_tx_buffer_count_ = val; }
    bool measure_queue_delay() const { return measure_queue_delay_; }
    void set_measure_queue_delay(bool val) { measure_queue_delay_ = val; }
    const std::set<uint16_t>& nic_queue_list() const {
        return nic_queue_list_;
    }
    uint16_t get_nic_queue(uint16_t queue) {
        std::map<uint16_t, uint16_t>::iterator it = qos_queue_map_.find(queue);
        if (it != qos_queue_map_.end()) {
            return it->second;
        }
        return default_nic_queue_;
    }

    uint16_t default_nic_queue() const {
        return default_nic_queue_;
    }

    void add_nic_queue(uint16_t queue, uint16_t nic_queue) {
        qos_queue_map_[queue] = nic_queue;
    }
    const LlgrParams &llgr_params() const {return llgr_params_;}

    void set_mac_learning_thread_count(uint32_t threads) {
        mac_learning_thread_count_ = threads;
    }

    uint32_t mac_learning_thread_count() const {
        return mac_learning_thread_count_;
    }

    void set_mac_learning_add_tokens(uint32_t add_tokens) {
        mac_learning_add_tokens_ = add_tokens;
    }

    uint32_t mac_learning_add_tokens() const {
        return mac_learning_add_tokens_;
    }

    void set_mac_learning_update_tokens(uint32_t update_tokens) {
        mac_learning_update_tokens_ = update_tokens;
    }

    uint32_t mac_learning_update_tokens() const {
        return mac_learning_update_tokens_;
    }

    void set_mac_learning_delete_tokens(uint32_t delete_tokens) {
        mac_learning_delete_tokens_ = delete_tokens;
    }

    uint32_t mac_learning_delete_tokens() {
        return mac_learning_delete_tokens_;
    }

    bool qos_priority_tagging() const { return qos_priority_tagging_; }
    bool IsConfiguredTsnHostRoute(std::string addr) const;

    bool mvpn_ipv4_enable() const { return mvpn_ipv4_enable_; }
    void set_mvpn_ipv4_enable(bool val) { mvpn_ipv4_enable_ = val; }

    //ATF stands for Agent Test Framework
    bool cat_is_agent_mocked() const { return AgentMock_; }

    bool cat_is_dpdk_mocked() const { return cat_MockDPDK_; }

    std::string cat_ksocketdir() const { return cat_kSocketDir_; }

    float vr_object_high_watermark() const { return vr_object_high_watermark_; }

protected:
    void set_hypervisor_mode(HypervisorMode m) { hypervisor_mode_ = m; }
    virtual void InitFromSystem();
    virtual void InitFromConfig();
    virtual void ReInitFromConfig();
    virtual void DebugInitFromConfig();
    virtual void ProcessTraceArguments();
    virtual void ProcessArguments();
    boost::property_tree::ptree &tree() { return tree_; }
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
    void ParseTestFrameworkArguments
        (const boost::program_options::variables_map &var_map);

private:
    friend class AgentParamTest;
    void UpdateBgpAsaServicePortRange();
    void UpdateBgpAsaServicePortRangeValue();
    void ComputeFlowLimits();
    void ComputeVrWatermark();
    static std::map<string, std::map<string, string> > ParseDerivedStats(
        const std::vector<std::string> &dsvec);
    void ParseQueue();
    void set_agent_mode(const std::string &mode);
    void set_gateway_mode(const std::string &mode);

    void ParseCollectorDSArguments
        (const boost::program_options::variables_map &v);
    void ParseCollectorArguments
        (const boost::program_options::variables_map &var_map);
    void ParseControllerServersArguments
        (const boost::program_options::variables_map &var_map);
    void ParseDnsServersArguments
        (const boost::program_options::variables_map &var_map);
    void ParseTsnServersArguments
        (const boost::program_options::variables_map &var_map);
    void ParseVirtualHostArguments
        (const boost::program_options::variables_map &v);
    void ParseDnsArguments
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
    void ParseDhcpRelayModeArguments
        (const boost::program_options::variables_map &var_map);
    void ParseSimulateEvpnTorArguments
        (const boost::program_options::variables_map &var_map);
    void ParseServiceInstanceArguments
        (const boost::program_options::variables_map &v);
    void ParseAgentInfoArguments
        (const boost::program_options::variables_map &v);
    void ParseNexthopServerArguments
        (const boost::program_options::variables_map &v);
    void ParsePlatformArguments
        (const boost::program_options::variables_map &v);
    void ParseServicesArguments
        (const boost::program_options::variables_map &v);
    void ParseSandeshArguments
        (const boost::program_options::variables_map &v);
    void ParseRestartArguments
        (const boost::program_options::variables_map &v);
    void ParseLlgrArguments
        (const boost::program_options::variables_map &v);
    void ParseMacLearning
        (const boost::program_options::variables_map &v);
    void ParseCryptArguments
        (const boost::program_options::variables_map &v);
    void ParseSessionDestinationArguments
        (const boost::program_options::variables_map &v);
    void ParseTraceArguments
        (const boost::program_options::variables_map &v);

    boost::program_options::variables_map var_map_;
    boost::program_options::options_description options_;
    boost::program_options::options_description config_file_options_;
    bool enable_flow_options_;
    bool enable_vhost_options_;
    bool enable_hypervisor_options_;
    bool enable_service_options_;
    AgentMode agent_mode_;
    GatewayMode gateway_mode_;

    Agent *agent_;
    PortInfo vhost_;
    // Number of tx-buffers on pkt0 device
    uint32_t pkt0_tx_buffer_count_;
    bool measure_queue_delay_;

    std::string agent_name_;
    std::string eth_port_;
    bool eth_port_no_arp_;
    std::string eth_port_encap_type_;
    std::string crypt_port_;
    bool crypt_port_no_arp_;
    std::string crypt_port_encap_type_;
    std::vector<std::string> controller_server_list_;
    std::string subcluster_name_;
    std::vector<std::string> dns_server_list_;
    std::vector<std::string> tsn_server_list_;
    uint16_t dns_client_port_;
    uint32_t dns_timeout_;
    uint32_t dns_max_retries_;
    uint16_t mirror_client_port_;
    Ip4Address mgmt_ip_;
    HypervisorMode hypervisor_mode_;
    PortInfo xen_ll_;
    std::string tunnel_type_;
    std::string metadata_shared_secret_;
    uint16_t metadata_proxy_port_;
    bool metadata_use_ssl_;
    std::string metadata_client_cert_;
    std::string metadata_client_cert_type_;
    std::string metadata_client_key_;
    std::string metadata_ca_cert_;
    float max_vm_flows_;
    uint16_t linklocal_system_flows_;
    uint16_t linklocal_vm_flows_;
    uint16_t flow_cache_timeout_;
    uint16_t flow_index_sm_log_count_;
    uint32_t flow_add_tokens_;
    uint32_t flow_ksync_tokens_;
    uint32_t flow_del_tokens_;
    uint32_t flow_update_tokens_;
    uint32_t flow_netlink_pin_cpuid_;
    uint32_t stale_interface_cleanup_timeout_;

    // Parameters configured from command line arguments only (for now)
    std::string config_file_;
    std::string program_name_;
    std::string log_file_;
    int log_files_count_;
    long log_file_size_;
    std::string log_property_file_;
    bool log_local_;
    bool log_flow_;
    std::vector<std::string> slo_destination_;
    std::vector<std::string> sample_destination_;
    std::string log_level_;
    std::string log_category_;
    bool use_syslog_;
    std::string syslog_facility_;
    std::vector<std::string> collector_server_list_;
    std::map<std::string, std::map<std::string, std::string> > derived_stats_map_;
    uint16_t http_server_port_;
    std::string host_name_;
    int agent_stats_interval_;
    int flow_stats_interval_;
    int vrouter_stats_interval_;
    std::string vmware_physical_port_;
    bool test_mode_;
    boost::property_tree::ptree tree_;
    std::auto_ptr<VirtualGatewayConfigTable> vgw_config_table_;
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
    std::string si_lbaas_auth_conf_;
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
    uint16_t flow_thread_count_;
    bool flow_trace_enable_;
    bool flow_hash_excl_rid_;
    uint16_t flow_latency_limit_;
    uint16_t max_sessions_per_aggregate_;
    uint16_t max_aggregates_per_session_endpoint_;
    uint16_t max_endpoints_per_session_msg_;
    bool subnet_hosts_resolvable_;
    std::string bgp_as_a_service_port_range_;
    std::vector<uint16_t> bgp_as_a_service_port_range_value_;
    uint32_t services_queue_limit_;
    uint32_t bgpaas_max_shared_sessions_;

    // Sandesh config options
    SandeshConfig sandesh_config_;

    // Agent config backup options
    bool restart_backup_enable_;
    // Config backup idle timeout in msec
    // Backup is trigerred if there is no config change in this time
    uint64_t restart_backup_idle_timeout_;
    // Config backup directory
    std::string restart_backup_dir_;
    // Number of config backup files
    uint16_t restart_backup_count_;
    // Config restore options
    bool restart_restore_enable_;
    // Config restore audit timeout in msec
    uint64_t restart_restore_audit_timeout_;

    std::vector<std::string> huge_page_file_1G_;
    std::vector<std::string> huge_page_file_2M_;

    std::string ksync_thread_cpu_pin_policy_;
    // TBB related
    uint32_t tbb_thread_count_;
    uint32_t tbb_exec_delay_;
    uint32_t tbb_schedule_delay_;
    uint32_t tbb_keepawake_timeout_;
    // Monitor task library and assert if inactivity detected
    uint32_t task_monitor_timeout_msec_;
    //Knob to configure priority tagging when in DCB mode.
    bool qos_priority_tagging_;
    std::map<uint16_t, uint16_t> qos_queue_map_;
    std::set<uint16_t> nic_queue_list_;
    uint16_t default_nic_queue_;
    LlgrParams llgr_params_;
    uint32_t mac_learning_thread_count_;
    uint32_t mac_learning_add_tokens_;
    uint32_t mac_learning_update_tokens_;
    uint32_t mac_learning_delete_tokens_;
    uint16_t min_aap_prefix_len_;
    uint16_t vmi_vm_vn_uve_interval_;
    uint16_t fabric_snat_hash_table_size_;
    bool mvpn_ipv4_enable_;
    //test framework parameters
    bool AgentMock_;
    bool cat_MockDPDK_;
    std::string cat_kSocketDir_;
    float vr_object_high_watermark_;
    DISALLOW_COPY_AND_ASSIGN(AgentParam);
};

#endif // vnsw_agent_param_hpp
