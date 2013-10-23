/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_init_cfg_hpp
#define vnsw_agent_init_cfg_hpp

#include <cmn/agent_cmn.h>
#include <ifmap/ifmap_agent_table.h>
#include <sandesh/sandesh_trace.h>

using namespace boost::uuids;

struct AgentCmdLineParams {
    std::string collector_;
    int collector_port_;
    std::string log_file_;
    std::string cfg_file_;
    bool log_local_;
    std::string log_level_;
    std::string log_category_;
    int http_server_port_;
    std::string hostname_;
    AgentCmdLineParams(std::string coll, int coll_port, std::string log_file, 
                       std::string cfg_file, bool log_local, 
                       std::string log_level, std::string log_category, 
                       int http_port, std::string hostname) :
        collector_(coll), collector_port_(coll_port), log_file_(log_file),
        cfg_file_(cfg_file), log_local_(log_local), log_level_(log_level), 
        log_category_(log_category), http_server_port_(http_port), hostname_(hostname) {}
};

class AgentConfig  {
public:
    enum Mode {
        MODE_INVALID,
        MODE_KVM,
        MODE_XEN
    };

    struct NativePort {
        NativePort() : 
            name_(""), vrf_(""), addr_(0), prefix_(0), plen_(0), gw_(0) {};
        ~NativePort() { };

        std::string name_;
        std::string vrf_;
        Ip4Address addr_;
        Ip4Address prefix_;
        int plen_;
        Ip4Address gw_;
    };

    typedef boost::function<void(void)> Callback;

    AgentConfig(const std::string &vhost_name, const std::string &vhost_addr,
                const Ip4Address &vhost_prefix, int vhost_plen, 
                const std::string &vhost_gw, const std::string &eth_port,
                const std::string &xmpp_addr_1, const std::string &xmpp_addr_2,
                const std::string &dns_addr_1, const std::string &dns_addr_2,
                const std::string &tunnel_type,
                const std::string &dss_addr, int dss_xs_instances, 
                AgentCmdLineParams cmd_line);
    virtual ~AgentConfig();

    static bool IsVHostConfigured();
    const std::string &GetVHostName() const {return vhost_name_;};
    const std::string &GetVHostAddr() const {return vhost_addr_;};
    const Ip4Address &GetVHostPrefix() const {return vhost_prefix_;};
    const int GetVHostPlen() const {return vhost_plen_;};
    const std::string &GetVHostGateway() const {return vhost_gw_;};
    const std::string &GetEthPort() const {return eth_port_;};
    const std::string &GetXmppServer_1() const {return xmpp_server_1_;};
    const std::string &GetXmppServer_2() const {return xmpp_server_2_;};
    const std::string &GetDnsServer_1() const {return dns_server_1_;};
    const std::string &GetDnsServer_2() const {return dns_server_2_;};
    const std::string &GetTunnelType() const {return tunnel_type_;};
    const std::string &GetDiscoveryServer() const {return dss_server_;};
    const int GetDiscoveryXmppServerInstances() const {return dss_xs_instances_;};
    const AgentCmdLineParams& GetCmdLineParams() const {return cmd_params_;};
    const void GetXenInfo(std::string &ifname, Ip4Address &addr, int &plen) const {
        ifname = xen_ll_.name_;
        addr = xen_ll_.addr_;
        plen = xen_ll_.plen_;
    }

    Mode GetMode() const {return mode_;}
    void SetMode(Mode mode) {mode_ = mode;}
    bool isXenMode() const { return mode_ == MODE_XEN; }
    bool isKvmMode() const { return mode_ == MODE_KVM; }
    void SetXenInfo(const std::string &ifname, Ip4Address addr, int plen);
    void LogConfig() const;
    static void Init(DB *db, const char *init_file, Callback cb = NULL);
    static void InitConfig(const char *init_file, AgentCmdLineParams cmd_line);
    static void DeleteStaticEntries();
    static void Shutdown();
    static void InitXenLinkLocalIntf();

    IFMapAgentTable *GetVmInterfaceTable() {return cfg_vm_interface_table_;};
    IFMapAgentTable *GetVmTable() {return cfg_vm_table_;};
    IFMapAgentTable *GetVnTable() {return cfg_vn_table_;};
    IFMapAgentTable *GetSgTable() {return cfg_sg_table_;};
    IFMapAgentTable *GetAclTable() {return cfg_acl_table_;};
    IFMapAgentTable *GetVrfTable() {return cfg_vrf_table_;};
    IFMapAgentTable *GetInstanceIpTable() {return cfg_instanceip_table_;};
    IFMapAgentTable *GetFloatingIpTable() {return cfg_floatingip_table_;};
    IFMapAgentTable *GetFloatingIpPoolTable() {return cfg_floatingip_pool_table_;};
    IFMapAgentTable *GetNetworkIpamTable() {return network_ipam_table_;};
    IFMapAgentTable *GetVnNetworkIpamTable() {return vn_network_ipam_table_;};
    IFMapAgentTable *GetVmPortVrfTable() {return vm_port_vrf_table_;};

    void SetVmInterfaceTable(IFMapAgentTable *table) {cfg_vm_interface_table_ = table;};
    void SetVmTable(IFMapAgentTable *table) {cfg_vm_table_ = table;};
    void SetVnTable(IFMapAgentTable *table) {cfg_vn_table_ = table;};
    void SetSgTable(IFMapAgentTable *table) {cfg_sg_table_ = table;};
    void SetAclTable(IFMapAgentTable *table) {cfg_acl_table_ = table;};
    void SetVrfTable(IFMapAgentTable *table) {cfg_vrf_table_ = table;};
    void SetInstanceIpTable(IFMapAgentTable *table) {cfg_instanceip_table_ = table;};
    void SetFloatingIpTable(IFMapAgentTable *table) {cfg_floatingip_table_ = table;};
    void SetFloatingIpPoolTable(IFMapAgentTable *table) {cfg_floatingip_pool_table_ = table;};
    void SetNetworkIpamTable(IFMapAgentTable *table) {network_ipam_table_ = table;};
    void SetVnNetworkIpamTable(IFMapAgentTable *table) {vn_network_ipam_table_ = table;};
    void SetVmPortVrfTable(IFMapAgentTable *table) {vm_port_vrf_table_ = table;};

    static AgentConfig *GetInstance() {
        return singleton_;
    }

private:
    friend class CfgModule;
    void OnItfCreate(DBEntryBase *entry, AgentConfig::Callback cb);
    void OnItfCreate(DBEntryBase *entry, const char *init_file,
                     std::string name, std::string addr, std::string gw, 
                     AgentConfig::Callback cb);

    IFMapAgentTable *cfg_vm_interface_table_;
    IFMapAgentTable *cfg_vm_table_;
    IFMapAgentTable *cfg_vn_table_;
    IFMapAgentTable *cfg_sg_table_;
    IFMapAgentTable *cfg_acl_table_;
    IFMapAgentTable *cfg_vrf_table_;
    IFMapAgentTable *cfg_instanceip_table_;
    IFMapAgentTable *cfg_floatingip_table_;
    IFMapAgentTable *cfg_floatingip_pool_table_;
    IFMapAgentTable *network_ipam_table_;
    IFMapAgentTable *vn_network_ipam_table_;
    IFMapAgentTable *vm_port_vrf_table_;

    static AgentConfig *singleton_;
    DBTableBase::ListenerId lid_;
    std::string vhost_name_;
    std::string vhost_addr_;
    Ip4Address vhost_prefix_;
    int vhost_plen_;
    std::string vhost_gw_;
    std::string eth_port_;
    std::string xmpp_server_1_;
    std::string xmpp_server_2_;
    std::string dns_server_1_;
    std::string dns_server_2_;
    std::string dss_server_;
    int dss_xs_instances_;
    TaskTrigger *trigger_;
    Mode mode_;
    NativePort xen_ll_;
    std::string tunnel_type_;
    AgentCmdLineParams cmd_params_;

    DISALLOW_COPY_AND_ASSIGN(AgentConfig);
};

class CfgModule {
public:
    static void CreateDBTables(DB *db);
    static void DeleteDBTables();
    static void RegisterDBClients(DB *db);
    static void UnregisterDBClients();
    static void Shutdown();

    static CfgListener      *cfg_listener_;
    static IFMapAgentParser *cfg_parser_; 
    static DBGraph          *cfg_graph_;

    DISALLOW_COPY_AND_ASSIGN(CfgModule);
};

extern SandeshTraceBufferPtr CfgTraceBuf;

#define CFG_TRACE(obj, ...) \
do {\
    Cfg##obj::TraceMsg(CfgTraceBuf, __FILE__, __LINE__, ##__VA_ARGS__);\
} while(0);\

#endif // vnsw_agent_init_cfg_hpp
